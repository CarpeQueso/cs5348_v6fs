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
static void convertBytesToSuperblock(uint8_t *data, Superblock *sb);
static void convertSuperblockToBytes(Superblock* sb, uint8_t *data);
static size_t getBlockAddress(uint16_t blockNumber);


int8_t v6_loadfs(char *v6FileSystemName, Superblock *sb) {
    size_t sbSize = sizeof(Superblock);
    // Array to store raw superblock data before it is assigned.
    uint8_t sbBytes[BLOCK_SIZE];
    int8_t blockReadSuccess;

    v6FileSystem = fopen(v6FileSystemName, "w+");

    if (v6FileSystem == NULL) {
        return E_FILE_OPEN_FAILURE;
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

int8_t v6_initfs(uint32_t numBlocks, uint32_t numInodes, Superblock *sb) {
    if (v6FileSystem == NULL) {
        return E_FILE_SYSTEM_NULL;
    }

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

    free(sb);
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

static size_t getBlockAddress(uint16_t blockNumber) {
    return blockNumber * 512U;
}
