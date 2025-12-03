#include <iostream>
#include <cstring>
#include <vector>
#include "fs.h"

int FS::create_navigation_folders(const uint16_t current_block, const uint16_t previous_block, uint8_t block[])
{
    // dir that points to current block (directory)
    const dir_entry current_entry {
        .file_name = ".",
        .size = 0,
        .first_blk = current_block,
        .type = TYPE_DIR,
        .access_rights = READ | WRITE
    };


    // dir that points to previous block (directory)
    const dir_entry previous_entry {
        .file_name = "..",
        .size = 0,
        .first_blk = previous_block,
        .type = TYPE_DIR,
        .access_rights = READ | WRITE
    };

    memcpy(block, &current_entry, sizeof(dir_entry));
    memcpy(block + sizeof(dir_entry), &previous_entry, sizeof(dir_entry));

    return 0;
}

int FS::write_fat_to_disk()
{
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat));
    return 0;
}

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";

    int result = disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat));

    if (result == -1) return;

    uint8_t block[BLOCK_SIZE];
    result = disk.read(ROOT_BLOCK, block);

    if (result == -1) return;

    memcpy(&current_dir, block, sizeof(dir_entry));
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    std::cout << "FS::format()\n";

    uint8_t temp_arr[BLOCK_SIZE] = {}; // zero-initialize it

    create_navigation_folders(ROOT_BLOCK, ROOT_BLOCK, temp_arr);
    
    disk.write(ROOT_BLOCK, temp_arr);

    memset(fat, FAT_FREE, sizeof(fat));
    fat[ROOT_BLOCK] = EOF;
    fat[FAT_BLOCK]  = EOF;

    write_fat_to_disk();
    
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(const std::string& filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";

    // get user input until empty newline (std::cin?)
    std::vector<std::string> user_input;
    while (true)
    {
        std::string line;
        std::getline(std::cin, line);
        
        if (line.empty())
            break;
        
        user_input.emplace_back(line);
    }
    
    // calculate how many blocks are needed
    int size = 0;
    for (const auto& input : user_input)
        size += static_cast<int>(input.size());

    const int block_size = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    
    // find that many blocks
    std::vector<uint16_t> blocks;
    
    for (int i = FAT_BLOCK + 1; i < sizeof(fat); i++)
    {
        if (fat[i] == FAT_FREE)
        {
            blocks.emplace_back(i);
            if (blocks.size() == block_size)
                break;
        }
    }
    
    // write metadata to current dir
    {
        dir_entry new_entry = {
            .file_name = "",
            .size = static_cast<uint32_t>(block_size),
            .first_blk = blocks[0],
            .type = TYPE_FILE,
            .access_rights = READ | WRITE
        };

        strncpy(new_entry.file_name, filepath.c_str(), sizeof(new_entry.file_name) - 1);

        uint8_t block[BLOCK_SIZE] = {};
        disk.read(current_dir.first_blk, block);

        bool space_available = false;

        // find empty part in the block to write to
        for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
        {
            const auto* entry = reinterpret_cast<dir_entry*>(&block[i]);
            if (entry->file_name[0] == '\0') // empty part of block
            {
                // write new entry to block
                memcpy(block + i, &new_entry, sizeof(dir_entry));
                disk.write(current_dir.first_blk, block);
                
                space_available = true;
                break;
            }
        }

        if (space_available == false)
        {
            std::cout << "[FS::create] Error: no space available in current directory";
            return -1;
        }
    }
    
    // add the blocks to the FAT
    for (int i = 0; i < blocks.size(); i++)
        fat[blocks[i]] = i == blocks.size() - 1 ? EOF : blocks[i + 1];
    
    // write FAT to disk
    write_fat_to_disk();
    
    // write data to the blocks
    int byte_offset = 0;
    int block_offset = 0;
    uint8_t block[BLOCK_SIZE] = {};
    
    for (auto& input : user_input)
    {
        // loop through each character
        for (const char& c : input)
        {
            block[byte_offset] = c;
            byte_offset++;

            if (byte_offset == BLOCK_SIZE)
            {
                disk.write(blocks[block_offset], block);
                byte_offset = 0;
                block_offset++;
            }
        }
    }

    disk.write(blocks[block_offset], block);
    
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
    std::cout << "FS::ls()\n";
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
