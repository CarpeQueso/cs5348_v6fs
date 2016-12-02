#include "v6fs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>


FILE *v6FileSystem = NULL;


static uint16_t v6_alloc(Superblock *sb);
static int8_t v6_free(Superblock *sb, uint16_t blockNumber);
static int8_t v6_read_block(uint16_t blockNumber, void *data, size_t size);
static int8_t v6_write_block(uint16_t blockNumber, void *data, size_t size);
static uint16_t createFile(Superblock *sb, char *filename);
static uint16_t createDirectory(Superblock *sb, char *filename);
static uint16_t getTerminalInodeNumber(Superblock *sb, char *filename);
static Inode* loadInode(Superblock *sb, uint16_t inodeNumber);
static int8_t saveInode(Superblock *sb, uint16_t inodeNumber, Inode *inode);
static uint16_t getNewInodeNumber(Superblock *sb);
static int8_t freeInode(Superblock *sb, uint16_t inodeNumber);
static int8_t initInode(Inode *inode);
static int8_t repopulateInodeList(Superblock *sb);
static int8_t addAllocatedBlockToInode(Superblock *sb, Inode *inode, uint16_t numBytes, uint16_t blockNumber);
static int8_t convertInodeToLargeFile(Superblock *sb, Inode *inode);
static uint16_t getNextAllocatedBlockNumber(Inode *inode);
static int8_t addDirectoryEntry(Superblock *sb, Inode *inode, char *filename, uint16_t inodeNumber);
static int8_t removeDirectoryEntry(Inode *inode, char *filename);
static uint16_t findDirectoryEntry(Inode *inode, char *filename);
static uint16_t getBlockNumberAtIndex(Inode *inode, uint16_t index);
static int8_t setBlockNumberAtIndex(Superblock *sb, Inode *inode, uint16_t blockNumber, uint16_t index);
static uint16_t getSinglyIndirectBlockNumberAtIndex(Inode *inode, uint16_t index);
static void convertBytesToSuperblock(uint8_t *data, Superblock *sb);
static void convertSuperblockToBytes(Superblock* sb, uint8_t *data);
static void convertBytesToInode(uint8_t *data, Inode *inode);
static void convertInodeToBytes(Inode *inode, uint8_t *data);
static uint16_t inodeIsDirectory(Inode *inode);
static uint16_t inodeIsLargeFile(Inode *inode);
static size_t getBlockAddress(uint16_t blockNumber);
static uint32_t getFileSize(Inode *inode);
static void setFileSize(Inode *inode, uint32_t fileSize);


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
        blockNumber = v6_alloc(sb);
        if (blockNumber == 0) {
            return E_ALLOCATE_FAILURE;
        }
        size_t numBytes = fread(data, 1, BLOCK_SIZE, f);
        v6_write_block(blockNumber, data, 1);
        addAllocatedBlockToInode(NULL, inode, (uint16_t) numBytes, blockNumber);
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
    uint16_t inodeNumber;

    inodeNumber = getTerminalInodeNumber(sb, v6Filename);

    if (inodeNumber == 0) {
        return E_NO_SUCH_FILE;
    }

    freeInode(sb, inodeNumber);

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
 *
 * Returns 0 if a block could not be allocated.
 */
static uint16_t v6_alloc(Superblock *sb) {
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
            return 0;
        }

        sb->nfree = blockData[0];
        memcpy(sb->free, &blockData[1], sb->nfree * 2);

        free(blockData);
    }

    return freeBlockNumber;
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

/*
 * Returns a new inode
 */
static uint16_t getNewInodeNumber(Superblock *sb){
    uint16_t newInodeNumber = 0;

    if(sb->ninode == 0){
        repopulateInodeList(sb);
    }

    if(sb->ninode > 0) {
        sb->ninode--;
        newInodeNumber = sb->inode[sb->ninode];
    }

    return newInodeNumber;
}

static int8_t freeInode(Superblock *sb, uint16_t inodeNumber) {
    if (inodeNumber < 1 || inodeNumber > sb->isize * 16) {
        return E_INVALID_INODE_NUMBER;
    }

    size_t blockIndex = 0;
    Inode *inode = loadInode(sb, inodeNumber);
    uint16_t nextAllocatedBlockNumber = getNextAllocatedBlockNumber(inode);

    // Free i-node data
    while (nextAllocatedBlockNumber != 0) {
        v6_free(sb, nextAllocatedBlockNumber);

        nextAllocatedBlockNumber = getNextAllocatedBlockNumber(NULL);
    }

    if (inodeIsLargeFile(inode)) {
        uint16_t singlyIndirectBlockNumber;
        for (size_t i = 0; i < MAX_SINGLY_INDIRECT_BLOCKS_PER_INODE; i++) {
            singlyIndirectBlockNumber = getSinglyIndirectBlockNumberAtIndex(inode, (uint16_t) i);
            if (singlyIndirectBlockNumber != 0) {
                v6_free(sb, singlyIndirectBlockNumber);
            }
        }

        if (inode->addr[7] != 0) {
            v6_free(sb, inode->addr[7]);
        }
    }

    // Deallocate i-node
    initInode(inode);
    saveInode(sb, inodeNumber, inode);

    return 0;
}

/*
 * Adds the block to the first available position.
 * This function will create indirect blocks as necessary.
 */
static int8_t addAllocatedBlockToInode(Superblock *sb, Inode *inode, uint16_t numBytes, uint16_t blockNumber) {
    size_t addrIndex = 0;

    if (inodeIsLargeFile(inode)) {
        uint16_t index = 0;
        uint16_t blockNumberAtIndex = 0;

        while (index < MAX_BLOCKS_PER_INODE) {
            blockNumberAtIndex = getBlockNumberAtIndex(inode, index);
            if (blockNumberAtIndex == 0) {
                setBlockNumberAtIndex(sb, inode, blockNumber, index);
                break;
            }
            index++;
        }
    } else {
        while (addrIndex < 8) {
            if (inode->addr[addrIndex] == 0) {
                inode->addr[addrIndex] = blockNumber;
                break;
            }
            addrIndex++;
        }
        if (addrIndex == 8) {
            // Could not add to i-node. The i-node is too small.
            convertInodeToLargeFile(sb, inode);
            return addAllocatedBlockToInode(sb, inode, numBytes, blockNumber);
        }
    }

    uint32_t inodeSize = getFileSize(inode);
    setFileSize(inode, inodeSize + numBytes);

    return 0;
}

// TODO: Use setBlockNumberAtIndex function
static int8_t convertInodeToLargeFile(Superblock *sb, Inode *inode) {
    // The block numbers that are initially stored in inode->addr[0-7]
    uint16_t smallFileBlockNumbers[8];
    uint16_t newIndirectBlockNumber;
    uint16_t indirectBlockData[256] = { 0 };

    if (inodeIsLargeFile(inode)) {
        // i-node is already a large file. Bam, done.
        return 0;
    }

    newIndirectBlockNumber = v6_alloc(sb);

    if (newIndirectBlockNumber == 0) {
        return E_ALLOCATE_FAILURE;
    }

    for (size_t i = 0; i < 8; i++) {
        smallFileBlockNumbers[i] = inode->addr[i];
        inode->addr[i] = 0;
    }

    memcpy(indirectBlockData, smallFileBlockNumbers, 8 * sizeof(uint16_t));
    v6_write_block(newIndirectBlockNumber, indirectBlockData, 2);
    inode->addr[0] = newIndirectBlockNumber;
    inode->flags |= FLAG_LARGE_FILE;

    return 0;
}

/*
 *
 */
static uint16_t getNextAllocatedBlockNumber(Inode *inode) {
    static Inode *persistentInode;
    static uint16_t isLargeFile;
    static uint16_t blockIndex;

    if (inode == NULL) {
        if (persistentInode == NULL) {
            // No i-node was ever given.
            return 0;
        }
    } else {
        // Set static references and start from beginning of i-node
        persistentInode = inode;
        isLargeFile = inodeIsLargeFile(inode);
        blockIndex = 0;
    }

    uint16_t blockNumber;

    if (isLargeFile) {
        while (blockIndex < MAX_BLOCKS_PER_INODE) {
            blockNumber = getBlockNumberAtIndex(persistentInode, blockIndex);
            blockIndex += 1;
            if (blockNumber != 0) {
                return blockNumber;
            }
        }
    } else {
        if (blockIndex >= 8) {
            return 0;
        }

        while (blockIndex < 8) {
            uint16_t blockNumber = getBlockNumberAtIndex(persistentInode, blockIndex);
            blockIndex += 1;
            if (blockNumber != 0) {
                // We have a valid block number
                return blockNumber;
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

    newBlockNumber = v6_alloc(sb);

    if (newBlockNumber == 0) {
        return E_ALLOCATE_FAILURE;
    }

    memcpy(&newBlockData[0], &inodeNumber, 2);
    memcpy(&newBlockData[2], inodeFilename, 14);
    v6_write_block(newBlockNumber, newBlockData, 1);
    addAllocatedBlockToInode(NULL, inode, 512, newBlockNumber);

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

static uint16_t getBlockNumberAtIndex(Inode *inode, uint16_t index) {
    // The block number to return.
    uint16_t blockNumber = 0;

    if (inodeIsLargeFile(inode)) {
        size_t addrIndex = index / 256U;

        if (index >= MAX_BLOCKS_PER_INODE) {
            return 0;
        }

        if (addrIndex < 7) {
            if (inode->addr[addrIndex] == 0) {
                return 0;
            }

            uint16_t singlyIndirectBlockNumber = inode->addr[addrIndex];
            uint16_t singlyIndirectBlockData[256];
            size_t indexInSinglyIndirectBlock = index % 256U;
            v6_read_block(singlyIndirectBlockNumber, singlyIndirectBlockData, 2);

            blockNumber = singlyIndirectBlockData[indexInSinglyIndirectBlock];
        } else {
            addrIndex = 7;
            if (inode->addr[addrIndex] == 0) {
                return 0;
            }

            // Maybe using 7 explicitly and not addrIndex is better?
            uint16_t doublyIndirectBlockNumber = inode->addr[addrIndex];
            uint16_t doublyIndirectBlockData[256];
            size_t indexInDoublyIndirectBlock = index / 256U - 7U;

            v6_read_block(doublyIndirectBlockNumber, doublyIndirectBlockData, 2);

            uint16_t singlyIndirectBlockNumber = doublyIndirectBlockData[indexInDoublyIndirectBlock];
            uint16_t singlyIndirectBlockData[256];
            size_t indexInSinglyIndirectBlock = index % 256U;

            if (singlyIndirectBlockNumber == 0) {
                return 0;
            }

            v6_read_block(singlyIndirectBlockNumber, singlyIndirectBlockData, 2);

            blockNumber = singlyIndirectBlockData[indexInSinglyIndirectBlock];
        }
    } else {
        if (index > 7) {
            return 0;
        }
        blockNumber = inode->addr[index];
    }

    return blockNumber;
}

static int8_t setBlockNumberAtIndex(Superblock *sb, Inode *inode, uint16_t blockNumber, uint16_t index) {
    if (inodeIsLargeFile(inode)) {
        size_t addrIndex = index / 256U;

        if (index >= MAX_BLOCKS_PER_INODE) {
            return E_INVALID_INDEX;
        }

        if (addrIndex < 7) {
            uint16_t singlyIndirectBlockData[256] = { 0 };

            if (inode->addr[addrIndex] == 0) {
                // Allocate a singly indirect block
                uint16_t newSinglyIndirectBlockNumber = v6_alloc(sb);

                if (newSinglyIndirectBlockNumber == 0) {
                    return E_ALLOCATE_FAILURE;
                }

                v6_write_block(newSinglyIndirectBlockNumber, singlyIndirectBlockData, 2);
                inode->addr[addrIndex] = newSinglyIndirectBlockNumber;
            }

            uint16_t singlyIndirectBlockNumber = inode->addr[addrIndex];
            size_t indexInSinglyIndirectBlock = index % 256U;

            v6_read_block(singlyIndirectBlockNumber, singlyIndirectBlockData, 2);
            singlyIndirectBlockData[indexInSinglyIndirectBlock] = blockNumber;
            v6_write_block(singlyIndirectBlockNumber, singlyIndirectBlockData, 2);
        } else {
            addrIndex = 7;
            uint16_t doublyIndirectBlockData[256] = { 0 };
            uint16_t singlyIndirectBlockData[256] = { 0 };

            if (inode->addr[addrIndex] == 0) {
                uint16_t newDoublyIndirectBlockNumber = v6_alloc(sb);

                if (newDoublyIndirectBlockNumber == 0) {
                    return E_ALLOCATE_FAILURE;
                }

                v6_write_block(newDoublyIndirectBlockNumber, doublyIndirectBlockData, 2);
                inode->addr[addrIndex] = newDoublyIndirectBlockNumber;
            }

            uint16_t doublyIndirectBlockNumber = inode->addr[addrIndex];
            size_t indexInDoublyIndirectBlock = index / 256U - 7U;

            v6_read_block(doublyIndirectBlockNumber, doublyIndirectBlockData, 2);

            if (doublyIndirectBlockData[indexInDoublyIndirectBlock] == 0) {
                uint16_t newSinglyIndirectBlockNumber = v6_alloc(sb);

                if (newSinglyIndirectBlockNumber == 0) {
                    return E_ALLOCATE_FAILURE;
                }

                v6_write_block(newSinglyIndirectBlockNumber, singlyIndirectBlockData, 2);

                doublyIndirectBlockData[indexInDoublyIndirectBlock] = newSinglyIndirectBlockNumber;
                v6_write_block(doublyIndirectBlockNumber, doublyIndirectBlockData, 2);
            }

            uint16_t singlyIndirectBlockNumber = doublyIndirectBlockData[indexInDoublyIndirectBlock];
            size_t indexInSinglyIndirectBlock = index % 256U;

            v6_read_block(singlyIndirectBlockNumber, singlyIndirectBlockData, 2);
            singlyIndirectBlockData[indexInSinglyIndirectBlock] = blockNumber;
            v6_write_block(singlyIndirectBlockNumber, singlyIndirectBlockData, 2);
        }
    } else {
        if (index > 7) {
            return E_INVALID_INDEX;
        }
        inode->addr[index] = blockNumber;
    }

    return 0;
}

static uint16_t getSinglyIndirectBlockNumberAtIndex(Inode *inode, uint16_t index) {
    if (inodeIsLargeFile(inode)) {
        if (index >= MAX_SINGLY_INDIRECT_BLOCKS_PER_INODE) {
            return 0;
        }

        if (index < 7) {
            return inode->addr[index];
        } else {
            if (inode->addr[7] == 0) {
                return 0;
            } else {
                uint16_t doublyIndirectBlockData[256];
                v6_read_block(inode->addr[7], doublyIndirectBlockData, 2);
                return doublyIndirectBlockData[index - 7];
            }
        }
    } else {
        // I-node is not a large file. There are no singly indirect blocks.
        return 0;
    }
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

static uint16_t inodeIsLargeFile(Inode *inode) {
    if (inode->flags & FLAG_LARGE_FILE) {
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