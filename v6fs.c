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
static Inode* loadInode(Superblock *sb, uint16_t inodeNumber);
static int8_t saveInode(Superblock *sb, uint16_t inodeNumber, Inode *inode);
static int8_t initInode(Inode *inode);
static int8_t repopulateInodeList(Superblock *sb);
static void convertBytesToSuperblock(uint8_t *data, Superblock *sb);
static void convertSuperblockToBytes(Superblock* sb, uint8_t *data);
static void convertBytesToInode(uint8_t *data, Inode *inode);
static void convertInodeToBytes(Inode *inode, uint8_t *data);
static size_t getBlockAddress(uint16_t blockNumber);


int8_t v6_loadfs(char *v6FileSystemName, Superblock *sb) {
    size_t sbSize = sizeof(Superblock);
    // Array to store raw superblock data before it is assigned.
    uint8_t sbBytes[BLOCK_SIZE];
    int8_t blockReadSuccess;

    v6FileSystem = fopen(v6FileSystemName, "r+");

    if (v6FileSystem == NULL) {
        v6FileSystem = fopen(v6FileSystemName, "w+");
        if (v6FileSystem == NULL) {
            return E_FILE_OPEN_FAILURE;
        }
    }

    blockReadSuccess = v6_read_block(1, sbBytes, 1);

    if (blockReadSuccess != 0) {
        return blockReadSuccess;
    }

    // Allocate sb and set values.
    sb = malloc(sbSize);

    convertBytesToSuperblock(sbBytes, sb);

    return 0;
}

int8_t v6_initfs(uint16_t numBlocks, uint16_t numInodes, Superblock *sb) {
    uint8_t block[BLOCK_SIZE] = { 0 };
    // The number of blocks required to hold the designated number of inodes.
    uint16_t numInodeBlocks;
    // Total number of data blocks, including those that will be used for free list blocks.
    uint16_t totalDataBlocks;
    uint16_t numFreeListBlocks, numFreeBlocks;
    uint16_t firstDataBlockNumber, firstFreeBlockNumber;

    if (v6FileSystem == NULL) {
        return E_FILE_SYSTEM_NULL;
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
    repopulateInodeList(sb);
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

    // Init root i-node
    Inode *inode = loadInode(sb, 1);
    uint16_t newBlockNumber;
    uint8_t rootDirectoryData[512];
    uint8_t firstRootEntry[] = { 0x01, 0x00, '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t secondRootEntry[] = { 0x00, 0x00, '.', '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    memcpy(rootDirectoryData, firstRootEntry, 16);
    memcpy(&rootDirectoryData[16], secondRootEntry, 16);

    v6_alloc(sb, &newBlockNumber);
    inode->addr[0] = newBlockNumber;
    v6_write_block(newBlockNumber, rootDirectoryData, 1);

    saveInode(sb, inode, 1);

    return 0;
}

int8_t v6_cpin(Superblock *sb, char *externalFilename, char *v6Filename) {


    return 0;
}

int8_t v6_cpout(Superblock *sb, char *v6Filename, char *externalFilename) {


    return 0;
}

int8_t v6_mkdir(Superblock *sb, char *v6DirectoryName) {


    return 0;
}

int8_t v6_rm(Superblock *sb, char *v6Filename) {


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

    if (inodeNumber > sb->isize) {
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

static size_t getBlockAddress(uint16_t blockNumber) {
    return blockNumber * 512U;
}
