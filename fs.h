#pragma once

#include <iostream>
#include <cstdint>
#include <vector>
#include "disk.h"

#ifndef __FS_H__
#define __FS_H__

#define ROOT_BLOCK 0
#define FAT_BLOCK 1
#define FAT_FREE 0
#define FAT_EOF -1
#define FAT_ENTRIES BLOCK_SIZE/2

#define TYPE_FILE 0
#define TYPE_DIR 1
#define READ 0x04
#define WRITE 0x02
#define EXECUTE 0x01

struct dir_entry {
    char file_name[56]; // name of the file / sub-directory
    uint32_t size; // size of the file in bytes
    uint16_t first_blk; // index in the FAT for the first block of the file
    uint8_t type; // directory (1) or file (0)
    uint8_t access_rights; // read (0x04), write (0x02), execute (0x01)
};

class FS {
private:
    Disk disk;
    // size of a FAT entry is 2 bytes
    int16_t fat[FAT_ENTRIES];

    dir_entry current_dir;
    
    /**
     * Creates the folders '.' and '..' for a given directory.
     * @param current_block The block of the current directory
     * @param previous_block The block of the parent directory
     * @param block The block array to write to
     * @return Success status, 0 - success. 1 - failure
     */
    int create_navigation_folders(uint16_t current_block, uint16_t previous_block, uint8_t block[]);

    /**
     * Writes the current fat to disk.
     * @return Success status, 0 - success. 1 - failure
     */
    int write_fat_to_disk();

    /**
     * Writes the given blocks to the FAT and commits it to disk
     * @param blocks Blocks to write to FAT
     * @return Success status, 0 - success. 1 - failure
     */
    int add_blocks_to_fat(const std::vector<int16_t>& blocks);

    /**
     * Gets all related blocks from a given starting block in order from FAT
     * @param blocks The blocks vector to add to
     * @param starting_block The starting block to look for more blocks in the FAT
     */
    void get_blocks_from_fat(std::vector<int16_t>& blocks, uint16_t starting_block) const;

    /**
     * Finds `amount` of empty blocks from FAT
     * @param blocks The blocks vector add to
     * @param amount The amount of blocks to find
     */
    void find_empty_blocks(std::vector<int16_t>& blocks, int amount) const;

    /**
     * Creates a new file descriptor in an empty space of the given block
     * @param new_entry New entry to write
     * @param block_index The block to write to
     * @param callee The function that called this function
     * @return Status. 0 - success. -1 - error
     */
    int write_new_file_descriptor(const dir_entry& new_entry, int16_t block_index, const std::string& callee);
    
    /**
     * Modifies the given input string so that it is left padded by padding amount
     * @param string The string to pad
     * @param padding The amount of padding
     */
    void pad_left(std::string& string, int padding);

public:
    FS();
    ~FS();
    // formats the disk, i.e., creates an empty file system
    int format();
    // create <filepath> creates a new file on the disk, the data content is
    // written on the following rows (ended with an empty row)
    int create(const std::string& filepath);
    // cat <filepath> reads the content of a file and prints it on the screen
    int cat(std::string filepath);
    // ls lists the content in the current directory (files and sub-directories)
    int ls();

    // cp <sourcepath> <destpath> makes an exact copy of the file
    // <sourcepath> to a new file <destpath>
    int cp(std::string sourcepath, std::string destpath);
    // mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
    // or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
    int mv(std::string sourcepath, std::string destpath);
    // rm <filepath> removes / deletes the file <filepath>
    int rm(std::string filepath);
    // append <filepath1> <filepath2> appends the contents of file <filepath1> to
    // the end of file <filepath2>. The file <filepath1> is unchanged.
    int append(std::string filepath1, std::string filepath2);

    // mkdir <dirpath> creates a new sub-directory with the name <dirpath>
    // in the current directory
    int mkdir(std::string dirpath);
    // cd <dirpath> changes the current (working) directory to the directory named <dirpath>
    int cd(std::string dirpath);
    // pwd prints the full path, i.e., from the root directory, to the current
    // directory, including the current directory name
    int pwd();

    // chmod <accessrights> <filepath> changes the access rights for the
    // file <filepath> to <accessrights>.
    int chmod(std::string accessrights, std::string filepath);
};

#endif // __FS_H__
