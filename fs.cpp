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
int
FS::create(std::string filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";
    std::vector<std::string> user_input;
    while(true)
    {
        std::string line;
        std::getline(std::cin, line);
        if(line.empty())
        {
            user_input.push_back("\n");
            break;
        }
        user_input.push_back(line);
    }

    //calculate each line size to get full block size for 
    int size = 0;
    for(const auto& input : user_input)
    {
        size += static_cast<int>(input.size());
    }
    const int block_size_new = (size + BLOCK_SIZE - 1) /BLOCK_SIZE;
    // int size = (static_cast<int>(user_input.size()));
    // const int block_size_new =  (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    // find the needed blocks for 
    std::vector<uint16_t> blocks;
    for(int i = FAT_BLOCK + 1; i < FAT_ENTRIES; i++)
    {
        if(fat[i] == FAT_FREE)
        {
            blocks.emplace_back(i);
            if(blocks.size() == block_size_new)
            {
                break;
            }
        }
    }

    if (blocks.size() != block_size_new)
    {
        std::cout << "[FS::create] Error: Not enough space" << std::endl;
        return -1;
    }
    

    {
        dir_entry new_entry = {
            .file_name = "",
            .size = static_cast<uint32_t>(size),
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
        if(space_available == false)
        {
            std::cout << "[FS::create] Error: no space available in current directory" << std::endl;
            return -1;
        }
    }

    // add blocks to the FAT
    for(int i = 0; i < blocks.size(); i++)
    {
        if(i == (blocks.size() -1))
        {
            fat[blocks[i]] = FAT_EOF;
        }
        else 
        {
            fat[blocks[i]] = blocks[i +1];
        }
    }

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat));

    int byte_offset = 0;
    int block_offset = 0;
    uint8_t block[BLOCK_SIZE] = {};

    //Loop through each byte and store it in block vector
    //Write to disk when block is full
    for(auto& input: user_input)
    {
        for(const char& byte: input)
        {
            block[byte_offset++] = byte;
            if(byte_offset == BLOCK_SIZE)
            {
                disk.write(blocks[block_offset], block);
                byte_offset = 0;
                block_offset++;
                memset(block, 0, BLOCK_SIZE);

            }
        }

        block[byte_offset++] = '\n';
        if(byte_offset == BLOCK_SIZE)
        {
            disk.write(blocks[block_offset], block);
            byte_offset = 0;
            block_offset++;
            memset(block, 0, BLOCK_SIZE);

        }
    }

    if(byte_offset > 0)
    {
        disk.write(blocks[block_offset], block);
    }
    
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
