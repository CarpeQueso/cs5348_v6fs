#include "v6fs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>


FILE *v6FileSystem = NULL;


static int8_t v6_alloc(Superblock *sb, uint16_t *blockNumber);
static int8_t v6_free(Superblock *sb, uint16_t blockNumber);
static int8_t v6_read_block(uint16_t blockNumber, void *data, size_t size);
static int8_t v6_write_block(uint16_t blockNumber, void *data, size_t size);
static uint16_t createFile(Superblock *sb, char *filename);
static uint16_t createDirectory(Superblock *sb, char *filename);
static uint16_t getTerminalInodeNumber(Superblock *sb, char *filename);
static Inode* loadInode(Superblock *sb, uint16_t inodeNumber);
static int8_t saveInode(Superblock *sb, uint16_t inodeNumber, Inode *inode);
static int8_t initInode(Inode *inode);

static int8_t freeInodeData(Inode *inode);
static int8_t repopulateInodeList(Superblock *sb);
static int8_t addAllocatedBlockToInode(Inode *inode, uint16_t blockNumber, uint16_t numBytes);
static uint16_t getNextAllocatedBlockNumber(Inode *inode);
static int8_t addDirectoryEntry(Superblock *sb, Inode *inode, char *filename, uint16_t inodeNumber);
static int8_t removeDirectoryEntry(Inode *inode, char *filename);
static uint16_t findDirectoryEntry(Inode *inode, char *filename);
static void convertBytesToSuperblock(uint8_t *data, Superblock *sb);
static void convertSuperblockToBytes(Superblock* sb, uint8_t *data);
static void convertBytesToInode(uint8_t *data, Inode *inode);
static void convertInodeToBytes(Inode *inode, uint8_t *data);
static uint16_t inodeIsDirectory(Inode *inode);
static size_t getBlockAddress(uint16_t blockNumber);
static uint32_t getFileSize(Inode *inode);
static void setFileSize(Inode *inode, uint32_t fileSize);

static Inode* getNewInode(Superblock *sb);


Superblock * v6_loadfs(char *v6FileSystemName) {
    Superblock *sb;
    size_t sbSize = sizeof(Superblock);
    // Array to store raw superblock data before it is assigned.
    uint8_t sbBytes[BLOCK_SIZE];
    int8_t blockReadSuccess;

    v6FileSystem = fopen(v6FileSystemName, "r+");

    if (v6FileSystem == NULL) {
        v6FileSystem = fopen(v6FileSystemName, "w+");
        if (v6FileSystem == NULL) {
            return NULL;
        }
    }

    blockReadSuccess = v6_read_block(1, sbBytes, 1);

    if (blockReadSuccess != 0) {
        return NULL;
    }

    // Allocate sb and set values.
    sb = malloc(sbSize);

    convertBytesToSuperblock(sbBytes, sb);

    return sb;
}

Superblock * v6_initfs(uint16_t numBlocks, uint16_t numInodes) {
    Superblock *sb;
    uint8_t block[BLOCK_SIZE] = { 0 };
    // The number of blocks required to hold the designated number of inodes.
    uint16_t numInodeBlocks;
    // Total number of data blocks, including those that will be used for free list blocks.
    uint16_t totalDataBlocks;
    uint16_t numFreeListBlocks, numFreeBlocks;
    uint16_t firstDataBlockNumber, firstFreeBlockNumber;

    if (v6FileSystem == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < numBlocks; i++) {
        // Write empty blocks to extend the size of the file.
        v6_write_block((uint16_t) i, block, 1);
    }

    if (numInodes % 16 == 0) {
        numInodeBlocks = numInodes / 16;
    }
    else {
        numInodeBlocks = numInodes / 16 + 1;
    }

    // TODO: Decide which of these you need.
    // Add two to numInodeBlocks to account for 0 and 1 block which are reserved.
    totalDataBlocks = numBlocks - (numInodeBlocks + 2);
    // +1 comes from the free array which removes one block from needing to be used for the free list.
    numFreeBlocks = (99 * totalDataBlocks) / 100 + 1;
    // Blocks initially allocated for the free list.
    numFreeListBlocks = totalDataBlocks - numFreeBlocks;
    // First block following blocks 0, 1 and i-node blocks.
    firstDataBlockNumber = numInodeBlocks + 2;
    // First block which is not part of the initial free list blocks.
    firstFreeBlockNumber = firstDataBlockNumber + numFreeListBlocks;

    // Create Superblock
    sb = malloc(sizeof(Superblock));
    sb->isize = numInodeBlocks;
    sb->fsize = numBlocks;
    sb->nfree = 1;
    // Set the pointer to the previous free list block to zero. There are no others prior to this one.
    sb->free[0] = 0;
    sb->ninode = 0;

    // TODO: set time?

    // Create free list
    for (size_t blockNum = firstDataBlockNumber; blockNum < numBlocks; blockNum++) {
        v6_free(sb, (uint16_t) blockNum);
    }

    // Create i-nodes (going to fill blocks allocated for i-nodes completely,
    // no matter what the number specified was)
    Inode inodes[16];
    for (size_t i = 0; i < 16; i++) {
        initInode(&inodes[i]);
    }
    inodes[0].flags = FLAG_INODE_ALLOCATED | FILE_TYPE_DIRECTORY | FLAG_OWNER_PERMISSIONS
                      | FLAG_GROUP_READ | FLAG_GROUP_EXECUTE | FLAG_OTHER_READ | FLAG_OTHER_EXECUTE;
    v6_write_block(2, inodes, 32);
    // Reset inode so all nodes in this group are cleared for setup of all other inode blocks
    initInode(&inodes[0]);

    // Write remaining blocks in a loop
    for (size_t blockNum = 3; blockNum < numInodeBlocks + 2; blockNum++) {
        v6_write_block((uint16_t) blockNum, inodes, 32);
    }

    repopulateInodeList(sb);

    // Init root i-node
    Inode *inode = loadInode(sb, 1);

    addDirectoryEntry(sb, inode, ".", 1);
    addDirectoryEntry(sb, inode, "..", 0);

    saveInode(sb, inode, 1);

    return sb;
}

int8_t v6_cpin(Superblock *sb, char *externalFilename, char *v6Filename) {
    FILE *f = fopen(externalFilename, "r");
    Inode *inode;
    uint16_t inodeNumber;
    uint16_t blockNumber;
    uint8_t data[BLOCK_SIZE] = { 0 };

    if (f == NULL) {
        return E_FILE_OPEN_FAILURE;
    }

    // Create i-node for the new file and any new directory i-nodes leading
    // up to the file location.
    inodeNumber = createFile(sb, v6Filename);
    inode = loadInode(sb, inodeNumber);

    // Allocate blocks and add to i-node sequentially from external file
    while (feof(f) == 0) {
        v6_alloc(sb, blockNumber);
        size_t numBytes = fread(data, 1, BLOCK_SIZE, f);
        v6_write_block(blockNumber, data, 1);
        addAllocatedBlockToInode(inode, blockNumber, (uint16_t) numBytes);
    }

    return 0;
}

int8_t v6_cpout(Superblock *sb, char *v6Filename, char *externalFilename) {
    FILE *f = fopen(externalFilename, "w");
    Inode *inode;
    uint16_t inodeNumber;
    uint32_t remainingBytes;
    uint16_t blockNumber;
    uint8_t data[BLOCK_SIZE] = { 0 };

    if (f == NULL) {
        return E_FILE_OPEN_FAILURE;
    }

    // Create i-node for the new file and any new directory i-nodes leading
    // up to the file location.
    inodeNumber = getTerminalInodeNumber(sb, v6Filename);
    inode = loadInode(sb, inodeNumber);

    if (inode == NULL) {
        return E_NO_SUCH_FILE;
    }

    remainingBytes = getFileSize(inode);
    blockNumber = getNextAllocatedBlockNumber(inode);

    // Allocate blocks and add to i-node sequentially from external file
    while (remainingBytes > 0) {
        v6_read_block(blockNumber, data, 1);
        if (remainingBytes >= 512) {
            fwrite(data, 1, BLOCK_SIZE, f);
            remainingBytes -= 512;
        } else {
            fwrite(data, 1, remainingBytes, f);
            remainingBytes = 0;
        }
        getNextAllocatedBlockNumber(NULL);
    }

    return 0;
}

int8_t v6_mkdir(Superblock *sb, char *v6DirectoryName) {
    uint16_t inodeNumber;

    inodeNumber = createDirectory(sb, v6DirectoryName);

    if (inodeNumber == 0) {
        return -1;
    }
    return 0;
}

int8_t v6_rm(Superblock *sb, char *v6Filename) {
    Inode *inode;
    uint16_t inodeNumber;
    uint16_t blockNumber;

    inodeNumber = getTerminalInodeNumber(sb, v6Filename);
    inode = loadInode(sb, inodeNumber);

    if (inode == NULL) {
        return E_NO_SUCH_FILE;
    }

    freeInodeData(inode);
    //freeInode(sb, inodeNumber);

    return 0;
}

int8_t v6_quit(Superblock *sb) {
    uint8_t superblockData[512];
    int8_t writeSuccess;

    convertSuperblockToBytes(sb, superblockData);

    writeSuccess = v6_write_block(1, superblockData, 1);

    if (writeSuccess != 0) {
        return writeSuccess;
    }

    fclose(v6FileSystem);
    return 0;
}

/*
 * Allocates a new block number where the v6 file system can write.
 * This new block number is stored in *blockNumber. blockNumber will
 * not be changed unless alloc is successful (returns 0).
 *
 * Returns 0 if allocation successful.
 */
static int8_t v6_alloc(Superblock *sb, uint16_t *blockNumber) {
    uint16_t freeBlockNumber;
    // Stores data read from the next free list block.
    // Shouldn't be allocated unless we need it.
    uint16_t *blockData;
    int8_t blockReadSuccess;

    sb->nfree--;
    freeBlockNumber = sb->free[sb->nfree];

    if (sb->nfree == 0) {
        blockData = malloc(sizeof(uint16_t) * 256);

        blockReadSuccess = v6_read_block(freeBlockNumber, blockData, 2);

        if (blockReadSuccess != 0) {
            free(blockData);
            return blockReadSuccess;
        }

        sb->nfree = blockData[0];
        memcpy(sb->free, &blockData[1], sb->nfree * 2);

        free(blockData);
    }

    *blockNumber = freeBlockNumber;

    return 0;
}

/*
 * Frees the given block number. Updates the superblock accordingly.
 */
static int8_t v6_free(Superblock *sb, uint16_t blockNumber) {
    size_t numBytesWritten;
    uint16_t *blockData;
    int8_t blockWriteSuccess;

    if (blockNumber < 2 || blockNumber >= sb->fsize) {
        return E_INVALID_BLOCK_NUMBER;
    }

    if (sb->nfree == 100) {
        // Allocate one 512 byte chunk to buffer write data
        blockData = malloc(sizeof(uint16_t) * 256);

        blockData[0] = sb->nfree;
        memcpy(&blockData[1], sb->free, sb->nfree * 2);

        blockWriteSuccess = v6_write_block(blockNumber, blockData, 1);
        if (blockWriteSuccess != 0) {
            free(blockData);
            return blockWriteSuccess;
        }

        sb->nfree = 0;

        free(blockData);
    }

    sb->free[sb->nfree] = blockNumber;
    sb->nfree++;

    return 0;
}

/*
 * Read a single block from the file system.
 * Assumes that data can hold one block's worth of bytes (defined by BLOCK_SIZE).
 *
 * blockNumber - the block number from which to read
 * data - the array where the data will be stored
 * size - the size of an element in data
 *
 * returns 0 if the entire block could be read
 */
static int8_t v6_read_block(uint16_t blockNumber, void *data, size_t size) {
    size_t numBytesRead;
    size_t numElementsToRead = BLOCK_SIZE / size;
    size_t expectedBytesRead = numElementsToRead * size;

    if (fseek(v6FileSystem, getBlockAddress(blockNumber), SEEK_SET) != 0) {
        return E_SEEK_FAILURE;
    }

    numBytesRead = fread(data, size, numElementsToRead, v6FileSystem);

    if (numBytesRead < expectedBytesRead) {
        return E_BLOCK_READ_FAILURE;
    }

    return 0;
}

static int8_t v6_write_block(uint16_t blockNumber, void *data, size_t size) {
    size_t numBytesWritten;
    size_t numElementsToWrite = BLOCK_SIZE / size;
    size_t expectedBytesWritten = numElementsToWrite * size;

    if (fseek(v6FileSystem, getBlockAddress(blockNumber), SEEK_SET) != 0) {
        return E_SEEK_FAILURE;
    }

    numBytesWritten = fwrite(data, size, numElementsToWrite, v6FileSystem);

    if (numBytesWritten < expectedBytesWritten) {
        return E_BLOCK_WRITE_FAILURE;
    }

    return 0;
}

static uint16_t createFile(Superblock *sb, char *filename) {

    return 0;
}

static uint16_t createDirectory(Superblock *sb, char *filename) {

    return 0;
}
/*
 * Traverses inodes along a specified path, returning the inode number of the file
 */
static uint16_t getTerminalInodeNumber(Superblock *sb, char *filename) {
    char* filePathToken = strtok(filename, "/");
    char delim[] = "/\0";
    Inode *root = loadInode(&sb, 1);
    uint16_t terminalInodeNumber;
    terminalInodeNumber = findDirectoryEntry(root, filePathToken);
    while(filePathToken != NULL) {
        filePathToken = strtok(NULL, delim);
        Inode *nextInode = loadInode(&sb, terminalInodeNumber);
        terminalInodeNumber = findDirectoryEntry(nextInode, filePathToken);
    }
    return terminalInodeNumber;
}

/*
 * Traverses the inode blocks and adds any available inodes to the inode list
 */
static int8_t repopulateInodeList(Superblock *sb) {
    Inode inodes[16];
    for (size_t inodeBlockNum = 2; inodeBlockNum < sb->isize + 2; inodeBlockNum++) {
        v6_read_block((uint16_t) inodeBlockNum, inodes, 32);

        for (size_t i = 0; i < 16; i++) {
            if (sb->ninode == 100) {
                // The inode array is full. Stop.
                return 0;
            }

            if ((inodes[i].flags & (uint16_t) FLAG_INODE_ALLOCATED) == 0) {
                sb->inode[sb->ninode] = (uint16_t) ((inodeBlockNum - 2) * 16 + i + 1);
                sb->ninode++;
            }
        }
    }
    return 0;
}

static Inode* loadInode(Superblock *sb, uint16_t inodeNumber) {
    Inode *inode;
    uint16_t inodeBlockNumber, offsetInBlock;
    uint8_t blockData[512];

    if (inodeNumber == 0 || inodeNumber > sb->isize) {
        return NULL;
    }

    // Add 1 since inodes are typically indexed from 1.
    inodeBlockNumber = (inodeNumber - 1) / 16 + 2;
    offsetInBlock = ((inodeNumber - 1) % 16) * 32;

    inode = malloc(sizeof(Inode));

    v6_read_block(inodeBlockNumber, blockData, 1);
    convertBytesToInode(&blockData[offsetInBlock], inode);

    return inode;
}

static int8_t saveInode(Superblock *sb, uint16_t inodeNumber, Inode *inode) {
    uint16_t inodeBlockNumber, offsetInBlock;
    uint8_t blockData[512];

    if (inodeNumber > sb->isize) {
        return -1;
    }

    // Add 1 since inodes are typically indexed from 1.
    inodeBlockNumber = (inodeNumber - 1) / 16 + 2;
    offsetInBlock = ((inodeNumber - 1) % 16) * 32;

    v6_read_block(inodeBlockNumber, blockData, 1);
    convertInodeToBytes(inode, &blockData[offsetInBlock]);
    v6_write_block(inodeBlockNumber, blockData, 1);

    return 0;
}

static int8_t initInode(Inode *inode) {
    inode->flags = 0;
    inode->nlinks = 0;
    inode->uid = 0;
    inode->gid = 0;
    inode->size0 = 0;
    inode->size1 = 0;
    for (size_t i = 0; i < 8; i++) {
        inode->addr[i] = 0;
    }
    inode->actime[0] = 0;
    inode->actime[1] = 0;
    inode->modtime[0] = 0;
    inode->modtime[1] = 0;
}

static int8_t freeInodeData(Inode *inode) {



    return 0;
}

static int8_t addAllocatedBlockToInode(Inode *inode, uint16_t blockNumber, uint16_t numBytes) {
    uint32_t blockIndex = 0;
    uint16_t blockData[256];

    // TODO: set inode size += numBytes

    if (inode->flags & FLAG_LARGE_FILE) {

    } else {
        while (blockIndex < 8) {
            if (inode->addr[blockIndex] == 0) {
                inode->addr[blockIndex] = blockNumber;
                break;
            }
        }
    }

    uint32_t inodeSize = getFileSize(inode);
    setFileSize(inode, inodeSize + numBytes);

    return 0;
}

/*
 *
 */
static uint16_t getNextAllocatedBlockNumber(Inode *inode) {
    static Inode *persistentInode;
    static uint16_t isLargeFile;
    // Note: In this file system, 32 bit type is enough to hold our max number
    // of bytes in a file (32MB). If larger files are allowed, increase
    // the width of byteOffset.
    // byteOffset holds the offset of the *current* call to getNextAllocatedBlockNumber
    // and is incremented by the block size after the next block has been found.
    static uint32_t blockIndex;


    if (inode == NULL) {
        if (persistentInode == NULL) {
            return 0;
        }
    } else {
        // Set static references and start from beginning of i-node
        persistentInode = inode;
        isLargeFile = inode->flags & FLAG_LARGE_FILE;
        blockIndex = 0;
    }

    uint16_t nextAddressIndex = 0;
    uint16_t nextSinglyIndirectBlockNumber = 0;
    uint16_t singlyIndirectBlockData[256] = { 0 };
    uint16_t doublyIndirectBlockData[256] = { 0 };

    if (isLargeFile) {
        nextAddressIndex = blockIndex / 256;

        if (nextAddressIndex < 7) {
            // Singly indirect blocks here
            while (nextAddressIndex < 7) {
                nextSinglyIndirectBlockNumber = persistentInode->addr[nextAddressIndex];
                if (nextSinglyIndirectBlockNumber != 0) {
                    // Valid indirect block number
                    v6_read_block(nextSinglyIndirectBlockNumber, singlyIndirectBlockData, 2);
                    uint16_t singlyIndirectBlockIndex = blockIndex % 256;
                    while (singlyIndirectBlockIndex < 256) {
                        uint16_t blockNumber = singlyIndirectBlockData[singlyIndirectBlockIndex];
                        blockIndex += 1;
                        if (blockNumber != 0) {
                            // Valid block number
                            return blockNumber;
                        } else {
                            singlyIndirectBlockIndex++;
                        }
                    }
                } else {
                    blockIndex += 256;
                    nextAddressIndex++;
                }
            }
            // If we reach this point, the only place left to check is the doubly indirect block.
            // For simplicity's sake, make the recursive call since it should only result in one
            // level of recursion.
            return getNextAllocatedBlockNumber(NULL);
        } else {
            if (blockIndex >= 1UL << 25) {
                // Larger than allowed 32MB file size.
                return 0;
            }
            // Doubly indirect block
            if (persistentInode->addr[nextAddressIndex] != 0) {
                v6_read_block(persistentInode->addr[nextAddressIndex], doublyIndirectBlockData, 2);
                uint16_t doublyIndirectBlockIndex = blockIndex / 256 - 7;
                while (doublyIndirectBlockIndex < 256) {
                    nextSinglyIndirectBlockNumber = doublyIndirectBlockData[doublyIndirectBlockIndex];
                    if (nextSinglyIndirectBlockNumber != 0) {
                        v6_read_block(nextSinglyIndirectBlockNumber, singlyIndirectBlockData, 2);
                        uint16_t singlyIndirectBlockIndex = blockIndex % 256;
                        while (singlyIndirectBlockIndex < 256) {
                            uint16_t blockNumber = singlyIndirectBlockData[singlyIndirectBlockIndex];
                            blockIndex += 1;
                            if (blockNumber != 0) {
                                // Valid block number
                                return blockNumber;
                            } else {
                                singlyIndirectBlockIndex++;
                            }
                        }
                    } else {
                        blockIndex += 256;
                        doublyIndirectBlockIndex++;
                    }
                }
            }
        }
    } else {
        if (blockIndex >= 8) {
            return 0;
        } else {
            while (blockIndex < 8) {
                uint16_t blockNumber = persistentInode->addr[blockIndex];
                blockIndex += 1;
                if (blockNumber != 0) {
                    // We have a valid block number
                    return blockNumber;
                }
            }
        }
    }

    // If we haven't found a block number by this point,
    // we've run out of places to look. There are no more blocks.
    return 0;
}

/*
 * Needs the Superblock since it may have to allocate new blocks.
 * Seems messy, but I'm not sure if there's a better way to do that.
 */
static int8_t addDirectoryEntry(Superblock *sb, Inode *inode, char *filename, uint16_t inodeNumber) {
    uint8_t blockData[BLOCK_SIZE];
    uint16_t blockNumber;
    uint16_t tempInodeNumber = 0;
    char inodeFilename[15] = { 0 };

    if (inodeIsDirectory(inode) == 0) {
        return -1;
    }

    if (findDirectoryEntry(inode, filename)) {
        // Directory already exists
        return -1;
    }

    // First, look for an empty slot in one of the allocated blocks.
    blockNumber = getNextAllocatedBlockNumber(inode);
    // Copy to inodeFilename for correct padding.
    strncpy(inodeFilename, filename, 14);

    while (blockNumber != 0) {
        v6_read_block(blockNumber, blockData, 1);
        for (size_t i = 0; i < 32; i++) {
            memcpy(&tempInodeNumber, &blockData[i * 16], 2);

            if (tempInodeNumber == 0) {
                memcpy(&blockData[i * 16], &inodeNumber, 2);
                memcpy(&blockData[(i * 16) + 2], inodeFilename, 14);
                v6_write_block(blockNumber, blockData, 1);
                return 0;
            }
        }
        blockNumber = getNextAllocatedBlockNumber(NULL);
    }

    // If there isn't one, allocate a block and add it to the inode.
    uint16_t newBlockNumber;
    uint8_t newBlockData[BLOCK_SIZE] = { 0 };
    v6_alloc(sb, &newBlockNumber);
    memcpy(&newBlockData[0], &inodeNumber, 2);
    memcpy(&newBlockData[2], inodeFilename, 14);
    v6_write_block(newBlockNumber, newBlockData, 1);
    addAllocatedBlockToInode(inode, newBlockNumber, 512);

    return 0;
}

static int8_t removeDirectoryEntry(Inode *inode, char *filename) {
    uint8_t blockData[BLOCK_SIZE];
    uint16_t blockNumber = getNextAllocatedBlockNumber(inode);
    uint16_t inodeNumber = 0;
    char inodeFilename[15] = { 0 };

    if (inodeIsDirectory(inode) == 0) {
        return -1;
    }

    while (blockNumber != 0) {
        v6_read_block(blockNumber, blockData, 1);
        for (size_t i = 0; i < 32; i++) {
            memcpy(&inodeNumber, &blockData[i * 16], 2);
            memcpy(inodeFilename, &blockData[(i * 16) + 2], 14);

            if (inodeNumber > 0) {
                if (strncmp(filename, inodeFilename, 14) == 0) {
                    inodeNumber = 0;
                    memcpy(&blockData[i * 16], &inodeNumber, 2);
                    v6_write_block(blockNumber, blockData, 1);
                    return 0;
                }
            }
        }
        blockNumber = getNextAllocatedBlockNumber(NULL);
    }

    return -1;
}

/*
 * Find the inode number of the file designated by filename.
 *
 * If the directory could not be found, return 0.
 */
static uint16_t findDirectoryEntry(Inode *inode, char *filename) {
    uint8_t blockData[BLOCK_SIZE];
    uint16_t blockNumber = getNextAllocatedBlockNumber(inode);
    uint16_t inodeNumber = 0;
    char inodeFilename[15] = { 0 };

    if (inodeIsDirectory(inode) == 0) {
        return 0;
    }

    while (blockNumber != 0) {
        v6_read_block(blockNumber, blockData, 1);
        for (size_t i = 0; i < 32; i++) {
            memcpy(&inodeNumber, &blockData[i * 16], 2);
            memcpy(inodeFilename, &blockData[(i * 16) + 2], 14);

            if (inodeNumber > 0) {
                if (strncmp(filename, inodeFilename, 14) == 0) {
                    return inodeNumber;
                }
            }
        }
        blockNumber = getNextAllocatedBlockNumber(NULL);
    }

    return 0;
}

static void convertBytesToSuperblock(uint8_t *data, Superblock *sb) {
    // I'm not sure if this notation is more or less readable than
    // just using a bunch of "memcpy"s...
    sb->isize = (uint16_t *) &data[0];
    sb->fsize = (uint16_t *) &data[2];
    sb->nfree = (uint16_t *) &data[4];
    // Copy the 100 word (200 byte) free array.
    memcpy(sb->free, &data[6], 200);
    sb->ninode = (uint16_t *) &data[206];
    // Copy the 100 word (200 byte) inode array.
    memcpy(sb->inode, &data[208], 200);
    sb->flock = data[408];
    sb->ilock = data[409];
    sb->fmod = data[410];
    // Copy the 4 byte system time.
    memcpy(sb->time, &data[411], 4);
}

static void convertSuperblockToBytes(Superblock* sb, uint8_t *data) {
    memcpy(&data[0], &sb->isize, 2);
    memcpy(&data[2], &sb->fsize, 2);
    memcpy(&data[4], &sb->nfree, 2);
    memcpy(&data[6], sb->free, 200);
    memcpy(&data[206], &sb->ninode, 2);
    memcpy(&data[208], sb->inode, 200);
    memcpy(&data[408], &sb->flock, 1);
    memcpy(&data[409], &sb->ilock, 1);
    memcpy(&data[410], &sb->fmod, 1);
    memcpy(&data[411], sb->time, 4);
}

static void convertBytesToInode(uint8_t *data, Inode *inode) {
    memcpy(&inode->flags, &data[0], 2);
    memcpy(&inode->nlinks, &data[2], 1);
    memcpy(&inode->uid, &data[3], 1);
    memcpy(&inode->gid, &data[4], 1);
    memcpy(&inode->size0, &data[5], 1);
    memcpy(&inode->size1, &data[6], 2);
    memcpy(inode->addr, &data[8], 16);
    memcpy(inode->actime, &data[24], 4);
    memcpy(inode->modtime, &data[28], 4);
}

static void convertInodeToBytes(Inode *inode, uint8_t *data) {
    memcpy(&data[0], &inode->flags, 2);
    memcpy(&data[2], &inode->nlinks, 1);
    memcpy(&data[3], &inode->uid, 1);
    memcpy(&data[4], &inode->gid, 1);
    memcpy(&data[5], &inode->size0, 1);
    memcpy(&data[6], &inode->size1, 2);
    memcpy(&data[8], inode->addr, 16);
    memcpy(&data[24], inode->actime, 4);
    memcpy(&data[28], inode->modtime, 4);
}

static uint16_t inodeIsDirectory(Inode *inode) {
    if ((inode->flags & FLAG_FILE_TYPE) == FILE_TYPE_DIRECTORY) {
        return 1;
    }
    return 0;
}

static size_t getBlockAddress(uint16_t blockNumber) {
    return blockNumber * 512U;
}

static uint32_t getFileSize(Inode *inode) {
    uint32_t fileSize = 0;

    if (inode->flags & FLAG_FILE_SIZE_MSB) {
        fileSize |= 1UL << 25;
    }

    fileSize |= (inode->size0 << 16) | inode->size1;
}

static void setFileSize(Inode *inode, uint32_t fileSize) {
    if (fileSize & (1UL << 25)) {
        inode->flags |= FLAG_FILE_SIZE_MSB;
    }

    inode->size0 = (uint8_t) ((fileSize >> 16) & 0xFF);
    inode->size1 = (uint16_t) fileSize;
}

/*
 * Returns a new inode
 */
static Inode* getNewInode(Superblock *sb){
    uint16_t newInodeNumber;
    Inode *newInode;
    if(sb->ninode == 0){
        repopulateInodeList(sb);
    }

    if(sb->ninode > 0) {
        sb->ninode--;
        newInodeNumber = sb->inode[sb->ninode];
    }
    newInode = loadInode(&sb, newInodeNumber);
    saveInode(&sb, newInodeNumber, newInode);
    return newInode;
}