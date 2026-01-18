#include <iostream>
#include <cstdint>
#include "disk.h"

#ifndef __FS_H__
#define __FS_H__

#define ROOT_BLOCK 0
#define FAT_BLOCK 1
#define FAT_FREE 0
#define FAT_EOF -1
#define FAT_ENTRIES BLOCK_SIZE / 2

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
    int16_t fat[BLOCK_SIZE/2];

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

public:
    FS();
    ~FS();
    // formats the disk, i.e., creates an empty file system
    int format();
    // create <filepath> creates a new file on the disk, the data content is
    // written on the following rows (ended with an empty row)
    int create(std::string filepath);
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
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>

#include "disk.h"

#ifndef __FS_H__
#define __FS_H__

#define ROOT_BLOCK 0
#define FAT_BLOCK 1
#define FAT_FREE 0
#define FAT_EOF -1

#define TYPE_FILE 0
#define TYPE_DIR 1
#define READ 0x04
#define WRITE 0x02
#define EXECUTE 0x01

#define ERROR(callee, message) std::cout << "[FS::" << callee << "] Error: " << message << "\n"
#define ERROR_C(message) std::cout << "[FS::" << callee << "] Error: " << message << "\n"
#define ERROR_R(callee, message) \
(ERROR(callee, message), -1)

struct dir_entry
{
    char file_name[56]; // name of the file / sub-directory
    uint32_t size; // size of the file in bytes
    uint16_t first_blk; // index in the FAT for the first block of the file
    uint8_t type; // directory (1) or file (0)
    uint8_t access_rights; // read (0x04), write (0x02), execute (0x01)
};

class FS
{
private:
    Disk disk;
    // size of a FAT entry is 2 bytes
    int16_t fat[BLOCK_SIZE / 2];

    dir_entry current_dir{};

    /**
     * Find an amount of empty blocks
     * @param amount Amount of blocks to find
     * @param callee The caller of the function
     * @return The found blocks
     */
    std::vector<uint16_t> find_empty_blocks(int amount, const std::string& callee) const;

    /**
     * Adds a new dir entry to block
     * @param block Block to add to
     * @param block_index The index of the block
     * @param new_entry Entry to add
     * @param callee The caller of the function
     * @return true if success otherwise false
     */
    bool add_dir_entry(uint8_t* block, uint16_t block_index, const dir_entry& new_entry, const std::string& callee);

    /**
     * Overwrites a dir_entry at index
     * @param block Block to write to
     * @param block_index The index of the block
     * @param new_entry Entry to overwrite
     * @param index The index of the entry
     * @param callee The caller of the function
     * @return true if success otherwise false
     */
    bool overwrite_dir_entry(uint8_t* block, uint16_t block_index, const dir_entry& new_entry, int16_t index, const std::string& callee);
    
    /**
     * Removes a dir entry from a block
     * @param block Block to remove from
     * @param block_index The index of the block
     * @param remove_entry Entry to remove
     * @param callee The caller of the function
     * @return true if success otherwise false
     */
    bool remove_dir_entry(uint8_t* block, uint16_t block_index, dir_entry remove_entry, const std::string& callee);

    /**
     * Resolves a path up to its parent directory.
     * @param path The path to navigate
     * @param file_name The resulting file name
     * @param callee The caller of the function
     * @param print_error If an error should be printed when path isn't found
     * @return The block that has the end path. -1 if failure
     * @note This function does NOT resolve the final component; it only navigates
     * through all parent directories. Used for operations like create, remove,
     * or mkdir where the parent directory must be located.
     */
    int16_t walk_path(const std::string& path, std::string& file_name, const std::string& callee, bool print_error = true);

    /**
     * Resolves a path fully and returns the directory entry of the final component
     * Used for operations like cd or stat where the actual directory entry
     * of the final component is needed.
     * @param path The path to navigate
     * @param out The resulting dir_entry
     * @param callee The caller of the function
     * @param print_error If an error should be printed when entry isn't found
     * @return true if success otherwise false
     * @note
     * This differs from walk_path:
     * - walk_path stops at the parent directory ("/a/b")
     * - lookup_path resolves the final component ("c") and returns its metadata
     */
    bool lookup_path(const std::string& path, dir_entry& out, const std::string& callee, bool print_error = true);

    /**
     * Adds a vector of blocks to the fat.
     * @param blocks The blocks to add
     * @param callee The caller of the function
     * @return true if success otherwise false
     */
    bool add_blocks_to_fat(const std::vector<unsigned short int>& blocks, const std::string& callee);

    /**
     * Write's the current FAT to disk
     * @return Status, true if success otherwise false
     * @note Should be called every time fat is written to
     */
    bool write_fat_to_disk();

    /**
     * Counts how many blocks are connected to the starter block
     * @param starter_block The block to start from
     * @return The amount of blocks connected to the starter block
     */
    int count_blocks(uint16_t starter_block) const;


    /**
     * Gets all related blocks from FAT
     * @param starter_block The block to start from
     * @return The resulting connected blocks
     */
    std::vector<uint16_t> get_related_blocks(uint16_t starter_block) const;


    void free_blocks(int16_t start_block);
    bool is_directory_empty(uint16_t dir_block_index);

public:
    FS();
    ~FS();
    // formats the disk, i.e., creates an empty file system
    int format();
    // create <filepath> creates a new file on the disk, the data content is
    // written on the following rows (ended with an empty row)
    int create(const std::string& filepath);
    // cat <filepath> reads the content of a file and prints it on the screen
    int cat(const std::string& filepath);
    // ls lists the content in the current directory (files and sub-directories)
    int ls();

    // cp <sourcepath> <destpath> makes an exact copy of the file
    // <sourcepath> to a new file <destpath>
    int cp(const std::string& source_path, const std::string& dest_path);
    // mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
    // or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
    int mv(const std::string& source_path, const std::string& dest_path);
    // rm <filepath> removes / deletes the file <filepath>
    int rm(const std::string& filepath);
    // append <filepath1> <filepath2> appends the contents of file <filepath1> to
    // the end of file <filepath2>. The file <filepath1> is unchanged.
    int append(const std::string& filepath1, const std::string& filepath2);

    // mkdir <dirpath> creates a new sub-directory with the name <dirpath>
    // in the current directory
    int mkdir(const std::string& dirpath);
    // cd <dirpath> changes the current (working) directory to the directory named <dirpath>
    int cd(std::string dirpath);
    // pwd prints the full path, i.e., from the root directory, to the current
    // directory, including the current directory name
    int pwd();

    // chmod <accessrights> <filepath> changes the access rights for the
    // file <filepath> to <accessrights>.
    int chmod(const std::string& access_rights, const std::string& filepath);
};

#endif // __FS_H__
