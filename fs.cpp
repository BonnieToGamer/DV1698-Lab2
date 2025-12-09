#include <iostream>
#include <boost/algorithm/string.hpp>
#include <cstring>
#include "fs.h"

std::vector<uint16_t> FS::find_empty_blocks(const int amount, const std::string& callee) const
{
    std::vector<uint16_t> blocks;

    for (int i = 0; i < BLOCK_SIZE / 2; i++)
    {
        const auto block = fat[i];
        if (block == FAT_FREE)
        {
            blocks.emplace_back(i);
            if (blocks.size() == amount)
                return blocks;
        }
    }

    if (blocks.size() != amount)
        ERROR_C("Could not find enough empty blocks");

    return {}; // return empty to indicate error
}

/**
 * Checks if a dir entry is empty
 * @param entry The entry to check
 * @return true if empty, otherwise has data
 */
bool is_entry_empty(const dir_entry& entry)
{
    return entry.file_name[0] == '\0';
}

bool FS::add_dir_entry(uint8_t* block, const uint16_t block_index, const dir_entry& new_entry,
                       const std::string& callee)
{
    // find empty space
    const auto* entries = reinterpret_cast<dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);
    int index = 0;
    bool found_empty = false;

    for (; index < size; index++)
    {
        const auto& entry = entries[index];
        if (is_entry_empty(entry))
        {
            found_empty = true;
            break;
        }
    }

    if (!found_empty)
    {
        ERROR_C("Could not find empty dir entry in block " << block_index);
        return false;
    }

    // write it there
    std::memcpy(block + index * sizeof(dir_entry), &new_entry, sizeof(dir_entry));

    // write to disk
    if (disk.write(block_index, block) != 0)
    {
        ERROR_C("Could not write block" << block_index << " to disk");
        return false;
    }

    return true;
}

/**
 * Searches for a dir entry with the name entry_name
 * @param block Block to search
 * @param block_index The index of the block
 * @param entry_name Entry name to find
 * @param result The resulting dir entry
 * @param index The index of the resulting dir entry
 * @param callee The caller of the function
 * @return true if success otherwise false
 */
bool find_entry(uint8_t* block, const uint16_t block_index, const std::string& entry_name, dir_entry& result,
                int16_t& index, const std::string& callee)
{
    // find the entry
    const auto* entries = reinterpret_cast<dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);

    for (int i = 0; i < size; i++)
    {
        const auto& entry = entries[i];
        if (std::strcmp(entry.file_name, entry_name.c_str()) == 0)
        {
            std::memcpy(&result, &entry, sizeof(dir_entry));
            index = i;
            return true;
        }
    }

    ERROR_C("Could not find entry with name " << entry_name << " in block " << block_index);

    return false;
}

bool FS::remove_dir_entry(uint8_t* block, const uint16_t block_index, dir_entry remove_entry, const std::string& callee)
{
    dir_entry result{};
    int16_t index = 0;
    if (find_entry(block, block_index, std::string(remove_entry.file_name), result, index, callee))
    {
        if (is_entry_empty(result))
            return false;

        std::memset(block + index * sizeof(dir_entry), 0, sizeof(dir_entry));

        if (disk.write(block_index, block) != 0)
        {
            ERROR_C("Could not write to block " << block_index);
            return false;
        }

        return true;
    }

    return false;
}

/**
 * Splits a path by '/' characters
 * @param path The path to split
 * @return The split path
 */
std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> result;
    boost::split(result, path, boost::is_any_of("/"));

    return result;
}

int16_t FS::navigate_to_dir_block(const std::string& path, std::string& file_name, const std::string& callee)
{
    /*
     * Example paths:
     * [x] file.txt 
     * [x] ./file.txt
     * [x] ../file.txt
     * [x] ./test/file.txt
     * [x] ../test/file.txt
     * The paths should work
     */

    // check if it's just the file. aka no path
    if (path.find('/') == std::string::npos)
    {
        file_name = path;
        if (file_name.size() >= 56)
        {
            ERROR_C("filename" << file_name << " to long");
            return -1;
        }

        return static_cast<int16_t>(current_dir.first_blk);
    }

    std::vector<std::string> split = split_path(path);
    file_name = split.back(); // get the last split element since that's the file
    split.pop_back();

    uint8_t block[BLOCK_SIZE];
    uint16_t current_block_index = current_dir.first_blk;

    if (disk.read(current_block_index, block) != 0)
    {
        ERROR_C("Could not read block " << current_dir.first_blk);
        return -1;
    }

    int16_t index = ROOT_BLOCK;

    for (const auto& dir : split)
    {
        if (dir.size() >= 56)
        {
            ERROR_C("filename" << dir << " to long");
            return -1;
        }

        if (dir == ".")
            continue;

        // we are in root block trying to go back
        if (current_block_index == ROOT_BLOCK && dir == "..")
            continue;

        dir_entry result{};
        if (!find_entry(block, current_block_index, dir, result, index, callee))
            return -1;

        if (result.type != TYPE_DIR && (result.access_rights & EXECUTE) == 0)
        {
            ERROR_C(dir << " is not a dir or we don't have permission");
            return -1;
        }

        current_block_index = result.first_blk;
        index = current_block_index;

        if (disk.read(current_block_index, block) != 0)
        {
            ERROR_C("Could not read block " << current_dir.first_blk);
            return -1;
        }
    }

    return index;
}

bool FS::add_blocks_to_fat(const std::vector<unsigned short int>& blocks, const std::string& callee)
{
    for (int i = 0; i < blocks.size(); i++)
    {
        const auto block = blocks[i];
        if (i < blocks.size() - 1)
            fat[block] = static_cast<int16_t>(blocks[i + 1]);
        else
            fat[block] = FAT_EOF;
    }

    if (!write_fat_to_disk())
    {
        ERROR_C("could not write fat to disk");
        return false;
    }

    return true;
}

bool FS::write_fat_to_disk()
{
    return disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat)) == 0;
}

FS::FS() : fat{}
{
    std::cout << "FS::FS()... Creating file system\n";

    if (disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat)) != 0)
    {
        ERROR("FS", "could not read FAT block");
        return;
    }

    current_dir = {
        .file_name = "",
        .size = 0,
        .first_blk = ROOT_BLOCK,
        .type = TYPE_DIR,
        .access_rights = READ | WRITE
    };
}

FS::~FS()
= default;

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    std::cout << "FS::format()\n";

    std::memset(&fat, 0, sizeof(fat));
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    write_fat_to_disk();

    uint8_t block[BLOCK_SIZE]{};
    if (disk.write(ROOT_BLOCK, block) != 0)
    {
        ERROR("format", "could not write to root block");
        return -1;
    }

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";

    std::vector<std::string> user_input;
    while (true)
    {
        std::string line;
        std::getline(std::cin, line);

        if (line.empty())
            break;

        user_input.emplace_back(line);
    }

    uint32_t size = 0;
    for (const auto& input : user_input)
        size += static_cast<int>(input.size()) + 1;

    const int block_count = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    std::string file_name;
    const int16_t dir_block = navigate_to_dir_block(filepath, file_name, "create");

    if (dir_block == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(dir_block, block) != 0)
    {
        ERROR("create", "could not read block " << dir_block);
        return -1;
    }

    const std::vector<uint16_t> empty_blocks = find_empty_blocks(block_count, "create");

    if (empty_blocks.empty())
        return -1;

    if (!add_blocks_to_fat(empty_blocks, "create"))
        return -1;

    dir_entry new_entry = {
        .file_name = "",
        .size = size,
        .first_blk = empty_blocks[0],
        .type = TYPE_FILE,
        .access_rights = READ | WRITE
    };

    std::strncpy(new_entry.file_name, file_name.c_str(), sizeof(new_entry.file_name) - 1);

    if (!add_dir_entry(block, dir_block, new_entry, "create"))
        return -1;

    int byte_offset = 0;
    int block_offset = 0;

    std::memset(block, 0, BLOCK_SIZE);

    auto flush_block_if_full = [&]()
    {
        if (byte_offset == BLOCK_SIZE)
        {
            disk.write(empty_blocks[block_offset], block);
            byte_offset = 0;
            block_offset++;
            std::memset(block, 0, BLOCK_SIZE);
        }
    };

    for (const auto& input : user_input)
    {
        for (const char c : input)
        {
            block[byte_offset++] = static_cast<uint8_t>(c);
            flush_block_if_full();
        }
        block[byte_offset++] = '\n';
        flush_block_if_full();
    }

    if (byte_offset > 0)
        disk.write(empty_blocks[block_offset], block);

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

    std::string dir_name;
    const int16_t block_index = navigate_to_dir_block(dirpath, dir_name, "mkdir");
    if (block_index == -1)
        return -1;

    const std::vector<uint16_t> result = find_empty_blocks(1, "mkdir");
    if (result.empty())
        return -1;

    add_blocks_to_fat(result, "mkdir");

    dir_entry new_entry = {
        .file_name = "",
        .size = 0,
        .first_blk = result[0],
        .type = TYPE_DIR,
        .access_rights = READ | WRITE | EXECUTE
    };

    std::strncpy(new_entry.file_name, dir_name.c_str(), sizeof(new_entry.file_name) - 1);

    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("mkdir", "could not read block " << block_index);
        return -1;
    }

    if (!add_dir_entry(block, block_index, new_entry, "mkdir"))
        return -1;

    std::memset(block, 0, sizeof(block));

    const dir_entry parent_entry = {
        .file_name = "..",
        .size = 0,
        .first_blk = static_cast<uint16_t>(block_index),
        .type = TYPE_DIR,
        .access_rights = READ | WRITE | EXECUTE
    };

    if (!add_dir_entry(block, result[0], parent_entry, "mkdir"))
        return -1;

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";

    // add last '/' so we make it an actual dir
    if (dirpath.back() != '/')
        dirpath += "/";

    std::string dir_name;
    const int16_t block_index = navigate_to_dir_block(dirpath, dir_name, "mkdir");
    if (block_index == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("cd", "could not read block " << block_index);
        return -1;
    }

    dirpath.pop_back();
    const std::vector<std::string> split = split_path(dirpath);
    dir_name = split.back();

    if (block_index == ROOT_BLOCK)
    {
        current_dir = {
            .file_name = "",
            .size = 0,
            .first_blk = ROOT_BLOCK,
            .type = TYPE_DIR,
            .access_rights = READ | WRITE | EXECUTE
        };

        return 0;
    }

    dir_entry result{};
    int16_t index = -1;
    if (!find_entry(block, block_index, "..", result, index, "cd"))
        return -1;

    if (disk.read(result.first_blk, block) != 0)
    {
        ERROR("cd", "could not read block " << block_index);
        return -1;
    }

    if (!find_entry(block, result.first_blk, dir_name, result, index, "cd"))
        return -1;

    current_dir = result;

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
