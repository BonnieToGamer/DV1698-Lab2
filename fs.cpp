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
    return disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat));
}

int FS::add_blocks_to_fat(const std::vector<int16_t>& blocks)
{
    // add the blocks to the FAT
    for (int i = 0; i < blocks.size(); i++)
        fat[blocks[i]] = i == blocks.size() - 1 ? FAT_EOF : blocks[i + 1];
    
    // write FAT to disk
    return write_fat_to_disk();
}

void FS::get_blocks_from_fat(std::vector<int16_t>& blocks, const uint16_t starting_block) const
{
    uint16_t current_block_index = starting_block;
    
    while (true)
    {
        blocks.emplace_back(current_block_index);

        if (fat[current_block_index] == FAT_EOF)
            break;
        
        current_block_index = fat[current_block_index];
    }
}

void FS::find_empty_blocks(std::vector<int16_t>& blocks, const int amount) const
{
    for (int i = 0; i < FAT_ENTRIES; i++)
    {
        if (fat[i] != FAT_FREE)
            continue;

        blocks.emplace_back(i);

        // check if we have enough blocks
        if (blocks.size() == amount)
            break;
    }
}

int FS::write_new_file_descriptor(const dir_entry& new_entry, const int16_t block_index, const std::string& callee)
{
    uint8_t block[BLOCK_SIZE] = {};
    if (disk.read(block_index, block) != 0)
    {
        std::cout << "[" << callee << "] Error: could not read directory block\n";
        return -1;
    }

    bool space_available = false;

    // find empty part in the block to write to
    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        const auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        // check if file already exists
        if (std::string(entry->file_name) == new_entry.file_name)
        {
            std::cout << "[" << callee << "] Error: file with that name already exists\n";
            return -1;
        }
        
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
        std::cout << "[" << callee << "] Error: no space available in current directory" << std::endl;
        return -1;
    }
    
    return 0;
}

void FS::pad_left(std::string& string, const int padding)
{
    if (padding <= string.length()) {
        // Already long enough, do nothing
        return;
    }

    // Calculate how many characters to add
    const int to_add = padding - static_cast<int>(string.length());

    // Prepend spaces
    string = std::string(to_add, ' ') + string;
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
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK]  = FAT_EOF;

    write_fat_to_disk();
    
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(const std::string& filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";

    // get user input until empty newline
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
    std::vector<int16_t> blocks;
    find_empty_blocks(blocks, block_size);
    
    if (blocks.size() != block_size)
    {
        std::cout << "[FS::create] Error: not enough free space to create file" << std::endl;
        return -1;
    }
    
    // write metadata to current dir
    dir_entry new_entry = {
        .file_name = "",
        .size = static_cast<uint32_t>(size),
        .first_blk = static_cast<uint16_t>(blocks[0]),
        .type = TYPE_FILE,
        .access_rights = READ | WRITE
    };

    strncpy(new_entry.file_name, filepath.c_str(), sizeof(new_entry.file_name) - 1);
    if (write_new_file_descriptor(new_entry, static_cast<int16_t>(current_dir.first_blk), "FS::create") != 0)
        return -1;
    
    if (add_blocks_to_fat(blocks) != 0)
    {
        std::cout << "[FS::create] Error: could not write FAT to disk\n";
        return -1;
    }
    
    // write data to the blocks
    int byte_offset = 0;
    int block_offset = 0;
    uint8_t block[BLOCK_SIZE] = {};

    auto try_flush_block = [&]() {
        if (byte_offset == BLOCK_SIZE) {
            disk.write(blocks[block_offset], block);
            byte_offset = 0;
            block_offset++;
            memset(block, 0, BLOCK_SIZE);
        }
    };
    
    for (auto& input : user_input)
    {
        // loop through each character
        for (const char& c : input)
        {
            block[byte_offset++] = c;
            try_flush_block();
        }

        block[byte_offset++] = '\n';
        try_flush_block();
    }

    // only write if we have a partial block
    if (byte_offset > 0)
        disk.write(blocks[block_offset], block);
    
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";

    uint8_t block[BLOCK_SIZE];

    if (disk.read(current_dir.first_blk, block) != 0) {
        std::cout << "[FS::ls] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    dir_entry file_entry{};
    
    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        // empty file descriptor
        if (entry->file_name[0] == '\0')
            continue;

        if (std::string(entry->file_name) == filepath)
        {
            memcpy(&file_entry, entry, sizeof(dir_entry));
            break;
        }
    }

    if (file_entry.file_name[0] == '\0')
    {
        std::cout << "[FS::cat] Error: no file with that name";
        return -1;
    }

    std::vector<int16_t> blocks;
    get_blocks_from_fat(blocks, file_entry.first_blk);

    int bytes_read = 0;

    for (const auto block_index : blocks)
    {
        if (disk.read(block_index, block) != 0)
        {
            std::cout << "[FS::cat] Error reading block nr " << block_index << "\n";
            return -1;
        }
        
        for (const auto byte : block)
        {
            if (bytes_read == file_entry.size)
                break;

            std::cout << byte;
            bytes_read++;
        }
    }

    std::cout << std::endl;
    
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
    std::cout << "FS::ls()\n";
    uint8_t block[BLOCK_SIZE];

    if (disk.read(current_dir.first_blk, block) != 0) {
        std::cout << "[FS::ls] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    std::cout << "name\tsize\n";

    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        const auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        // empty file descriptor
        if (entry->file_name[0] == '\0')
            continue;

        std::cout << entry->file_name << "\t\t" << entry->size << "\n";
    }
    
    std::cout << std::flush;
    
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::cp] Error: cannot read block nr " << current_dir.first_blk << "\n";
        return -1;
    }

    dir_entry source_entry{};
    bool no_duplicate_name = true;
    
    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        // empty file descriptor
        if (entry->file_name[0] == '\0')
            continue;

        if (std::string(entry->file_name) == destpath)
        {
            no_duplicate_name = false;
            break;
        }

        if (std::string(entry->file_name) == sourcepath)
            memcpy(&source_entry, entry, sizeof(dir_entry));
    }

    if (no_duplicate_name == false)
    {
        std::cout << "[FS::cp] Error: there already exists a file with that name\n";
        return -1;
    }

    if (source_entry.file_name[0] == '\0')
    {
        std::cout << "[FS::cp] Error: source file does not exist\n";
        return -1;
    }

    dir_entry destination = source_entry;
    memset(&destination.file_name, 0, sizeof(destination.file_name));

    strncpy(destination.file_name, destpath.c_str(), sizeof(destination.file_name) - 1);

    // get the source files block indices
    std::vector<int16_t> blocks;
    get_blocks_from_fat(blocks, source_entry.first_blk);

    // find that amount of new blocks
    std::vector<int16_t> new_blocks;
    find_empty_blocks(new_blocks, static_cast<int>(blocks.size()));

    if (new_blocks.size() != blocks.size())
    {
        std::cout << "[FS::cp] Error: not enough free space to copy file\n";
        return -1;
    }

    // assign new blocks in FAT
    destination.first_blk = new_blocks[0];

    if (add_blocks_to_fat(new_blocks) != 0)
    {
        std::cout << "[FS::cp] Error: could not write FAT to disk\n";
        return -1;
    }

    if (write_new_file_descriptor(destination, static_cast<int16_t>(current_dir.first_blk), "FS::cp") != 0)
        return -1;

    // we know blocks and new_blocks are the same size
    // so the following operations are completely safe
    for (int i = 0; i < blocks.size(); i++)
    {
        if (disk.read(blocks[i], block) != 0)
        {
            std::cout << "[FS::cp] Error: could not read block nr " << blocks[i] << "\n";
            return -1;
        }

        if (disk.write(new_blocks[i], block) != 0)
        {
            std::cout << "[FS::cp] Error: could not write block nr " << blocks[i] << "\n";
            return -1;
        }
    }
    
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