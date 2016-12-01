#ifndef V6FS_H
#define V6FS_H

#include <stdint.h>
#include <stdio.h>

/*
 * Block size in bytes
 */
#define BLOCK_SIZE                          512

/*
 * I-node flags bits (in octal).
 */

#define FLAG_INODE_ALLOCATED                0100000

/*
 * Both bits used for the four possible file types.
 */
#define FLAG_FILE_TYPE                      0060000

/*
 * Types of files which can exist within the system.
 */
#define FILE_TYPE_PLAIN_FILE                0000000
#define FILE_TYPE_DIRECTORY                 0040000
#define FILE_TYPE_CHAR_SPECIAL_FILE         0020000
#define FILE_TYPE_BLOCK_SPECIAL_FILE        0060000

#define FLAG_LARGE_FILE                     0010000

#define FLAG_SET_USER_ID_ON_EXECUTION       0004000

#define FLAG_SET_GROUP_ID_ON_EXECUTION      0002000

#define FLAG_FILE_SIZE_MSB                  0001000

/*
 * Bits for all three owner permissions.
 */
#define FLAG_OWNER_PERMISSIONS              0000700

/*
 * Individual RWX bits.
 */
#define FLAG_OWNER_READ                     0000400
#define FLAG_OWNER_WRITE                    0000200
#define FLAG_OWNER_EXECUTE                  0000100

/*
 * Bits for all three group permissions.
 */
#define FLAG_GROUP_PERMISSIONS              0000070

/*
 * Individual RWX bits.
 */
#define FLAG_GROUP_READ                     0000040
#define FLAG_GROUP_WRITE                    0000020
#define FLAG_GROUP_EXECUTE                  0000010

/*
 * Bits for all three "other" permissions.
 */
#define FLAG_OTHER_PERMISSIONS              0000007

/*
 * Individual RWX bits.
 */
#define FLAG_OTHER_READ                     0000004
#define FLAG_OTHER_WRITE                    0000002
#define FLAG_OTHER_EXECUTE                  0000001


/*
 * Error codes
 */
#define E_FILE_OPEN_FAILURE                 1
#define E_SEEK_FAILURE                      2
#define E_SUPERBLOCK_READ_ERROR             3
#define E_FILE_SYSTEM_NULL                  4
#define E_BLOCK_READ_FAILURE                5
#define E_BLOCK_WRITE_FAILURE               6


typedef struct Superblock {
    uint16_t isize;
    uint16_t fsize;
    uint16_t nfree;
    uint16_t free[100];
    uint16_t ninode;
    uint16_t inode[100];
    uint8_t flock;
    uint8_t ilock;
    uint8_t fmod;
    uint16_t time[2];
} Superblock;

typedef struct Inode {
    uint16_t flags;
    uint8_t nlinks;
    uint8_t uid;
    uint8_t gid;
    uint8_t size0;
    uint16_t size1;
    uint16_t addr[8];
    uint16_t actime[2];
    uint16_t modtime[2];
} Inode;

/*
 * Declared globally so we can have a consistent place to store between functions.
 */
extern FILE *v6FileSystem;

/*
 * Loads the superblock from the given file system location.
 *
 * sb - the variable to store the newly loaded superblock.
 */
extern int8_t v6_loadfs(char *v6FileSystemName, Superblock *sb);

/*
 * Initializes a new, empty v6 file system.
 *
 * numBlocks - the number of blocks to create in the V6 file system.
 * numInodes - the number of i-nodes contained within this filesystem.
 * sb - where the newly allocated superblock will be stored.
 */
extern int8_t v6_initfs(uint16_t numBlocks, uint16_t numInodes, Superblock *sb);


/*
 * Reads a file from an external file location and writes it to a location within the V6 file system.
 *
 * sb - the superblock that represents the V6 file system.
 * externalFilename - the name of the file to read
 * v6Filename - the name of the file to write in the v6 file system.
 */
extern int8_t v6_cpin(Superblock *sb, char *externalFilename, char *v6Filename);

/*
 * Reads a file from the V6 file system and writes it to an external file.
 *
 * sb - the superblock that represents the V6 file system.
 * v6Filename - the name of the file to read from
 * externalFilename - the name of the file to write
 */
extern int8_t v6_cpout(Superblock *sb, char *v6Filename, char *externalFilename);

/*
 * Creates a new directory with the given name in the V6 file system.
 *
 * sb - the superblock that represents the V6 file system.
 * v6DirectoryName - the name of the directory to be created.
 */
extern int8_t v6_mkdir(Superblock *sb, char *v6DirectoryName);

/*
 * Removes a file in the V6 file system. (TODO: determine if this should also remove directories)
 *
 * sb - the superblock that represents the V6 file system.
 * v6Filename - the name of the file to be removed.
 */
extern int8_t v6_rm(Superblock *sb, char *v6Filename);

/*
 * Exits the program and saves all changes to the superblock back to the V6 file system.
 *
 * sb - the superblock that represents the V6 file system.
 */
extern int8_t v6_quit(Superblock *sb);



#endif
