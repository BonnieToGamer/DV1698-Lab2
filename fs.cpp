#include <iostream>
#include <boost/algorithm/string.hpp>
#include <cstring>
#include "fs.h"

#include <queue>

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

bool FS::overwrite_dir_entry(uint8_t* block, const uint16_t block_index, const dir_entry& new_entry,
                             const int16_t index,
                             const std::string& callee)
{
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
 * @param print_error If true it prints an error message if entry is not found
 * @return true if success otherwise false
 */
bool find_entry(const uint8_t* block, const uint16_t block_index, const std::string& entry_name, dir_entry& result,
                int16_t& index, const std::string& callee, const bool print_error = true)
{
    // find the entry
    const auto* entries = reinterpret_cast<const dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);

    for (int i = 0; i < size; i++)
    {
        const auto& entry = entries[i];
        if (std::strcmp(entry.file_name, entry_name.c_str()) == 0)
        {
            std::memcpy(&result, &entry, sizeof(dir_entry));
            index = static_cast<int16_t>(i);
            return true;
        }
    }

    if (print_error)
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

int16_t FS::walk_path(const std::string& path, std::string& file_name, const std::string& callee,
                      const bool print_error)
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

    if (file_name.size() >= 56)
    {
        ERROR_C("filename" << file_name << " to long");
        return -1;
    }

    split.pop_back();

    uint8_t block[BLOCK_SIZE];
    uint16_t current_block_index = path.at(0) == '/' ? ROOT_BLOCK : current_dir.first_blk;
    int16_t index = ROOT_BLOCK;

    for (const auto& dir : split)
    {
        if (dir.size() >= 56)
        {
            ERROR_C("filename" << dir << " to long");
            return -1;
        }

        if (disk.read(current_block_index, block) != 0)
        {
            ERROR_C("Could not read block " << current_dir.first_blk);
            return -1;
        }

        if (dir == ".")
            continue;

        // we are in root block trying to go back
        if (current_block_index == ROOT_BLOCK && dir == "..")
            continue;

        dir_entry result{};
        if (!find_entry(block, current_block_index, dir, result, index, callee, print_error))
            return -1;

        if (result.type != TYPE_DIR && (result.access_rights & EXECUTE) == 0)
        {
            ERROR_C(dir << " is not a dir or we don't have permission");
            return -1;
        }

        current_block_index = result.first_blk;
        index = static_cast<int16_t>(current_block_index);
    }

    return index;
}

bool FS::lookup_path(const std::string& path, dir_entry& out, const std::string& callee, const bool print_error)
{
    const std::vector<std::string> split = split_path(path);

    uint8_t block[BLOCK_SIZE];
    uint16_t current_block_index = path.at(0) == '/' ? ROOT_BLOCK : current_dir.first_blk;

    for (int i = 0; i < split.size(); i++)
    {
        const auto& dir = split[i];

        if (dir.size() >= 56)
        {
            ERROR_C("filename" << dir << " to long");
            return false;
        }

        if (disk.read(current_block_index, block) != 0)
        {
            ERROR_C("Could not read block " << current_dir.first_blk);
            return false;
        }

        if (dir == ".")
            continue;

        // we are in root block trying to go back
        if (current_block_index == ROOT_BLOCK && dir == "..")
        {
            if (i == split.size() - 1)
            {
                out = {
                    .file_name = "",
                    .size = 0,
                    .first_blk = ROOT_BLOCK,
                    .type = TYPE_DIR,
                    .access_rights = READ | WRITE | EXECUTE
                };
                return true;
            }
            continue;
        }

        dir_entry entry{};
        int16_t index = -1;
        if (!find_entry(block, current_block_index, dir, entry, index, callee))
            return false;

        // we are on the last space
        if (i == split.size() - 1)
        {
            out = entry;
            return true;
        }

        // we are still traversing
        current_block_index = entry.first_blk;
    }

    if (print_error)
        ERROR_C("could not find the path " << path);

    return false;
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

bool FS::remove_blocks_from_fat(const std::vector<unsigned short int>& blocks, const std::string& callee)
{
    for (const unsigned short block : blocks)
        fat[block] = FAT_FREE;

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

int FS::count_blocks(const uint16_t starter_block) const
{
    auto current_block = static_cast<int16_t>(starter_block);
    int amount = 1;

    while (fat[current_block] != FAT_EOF)
    {
        current_block = fat[current_block];
        amount++;
    }

    return amount;
}

std::vector<uint16_t> FS::get_related_blocks(const uint16_t starter_block) const
{
    auto current_block = static_cast<int16_t>(starter_block);
    std::vector<uint16_t> result;

    do
    {
        result.push_back(current_block);
        current_block = fat[current_block];
    }
    while (current_block != FAT_EOF);

    return result;
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
int FS::create(const std::string& filepath)
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

    const int block_count = (static_cast<int>(size) + BLOCK_SIZE - 1) / BLOCK_SIZE;

    std::string file_name;
    const int16_t block_index = walk_path(filepath, file_name, "create");

    if (block_index == -1)
        return -1;

    if (file_name == "..")
        return ERROR_R("create", "'..' is a reserved name");

    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("create", "could not read block " << block_index);
        return -1;
    }

    dir_entry result_entry{};
    int16_t index;
    if (find_entry(block, block_index, file_name, result_entry, index, "create", false))
    {
        ERROR("create", "there already exists a file or directory with this name");
        return -1;
    }

    if (disk.read(block_index, block) != 0)
    {
        ERROR("create", "could not read block " << block_index);
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

    if (!add_dir_entry(block, block_index, new_entry, "create"))
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
int FS::cat(const std::string& filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";

    std::string file_name;
    const int16_t block_index = walk_path(filepath, file_name, "cat");

    if (block_index == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("cat", "could not read block " << block_index);
        return -1;
    }

    dir_entry result{};
    int16_t index;
    if (!find_entry(block, block_index, file_name, result, index, "cat"))
        return -1;

    const auto blocks = get_related_blocks(result.first_blk);

    int bytes_read = 0;

    for (const auto related_block_index : blocks)
    {
        if (disk.read(related_block_index, block) != 0)
        {
            ERROR("cat", "could not read block " << related_block_index);
            return -1;
        }

        for (const auto byte : block)
        {
            if (bytes_read == static_cast<int>(result.size))
                break;

            std::cout << static_cast<char>(byte);
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
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        ERROR("ls", "could not read block " << current_dir.first_blk);
        return -1;
    }

    const dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);

    // puts lexicographically smallest filename first
    auto cmp = [](const dir_entry& a, const dir_entry& b)
    {
        return std::strcmp(a.file_name, b.file_name) < 0;
    };

    std::priority_queue<dir_entry, std::vector<dir_entry>, decltype(cmp)> pq(cmp);

    for (int i = 0; i < size; i++)
    {
        const dir_entry entry = entries[i];
        if (is_entry_empty(entry))
            continue;

        pq.push(entry);
    }

    auto check_access = [](const uint8_t access_rights, const uint8_t right, const char right_str, std::string& str)
    {
        if ((access_rights & right) == right)
            str += right_str;
        else
            str += '-';
    };

    std::cout << "name\t type\t accessrights\t size\n";

    while (!pq.empty())
    {
        const auto entry = pq.top();
        pq.pop();

        std::string access_str;
        check_access(entry.access_rights, READ, 'r', access_str);
        check_access(entry.access_rights, WRITE, 'w', access_str);
        check_access(entry.access_rights, EXECUTE, 'x', access_str);

        std::cout
            << entry.file_name << "\t "
            << (entry.type == TYPE_DIR ? "dir" : "file") << "\t "
            << access_str << "\t "
            << (entry.type == TYPE_DIR ? "-" : std::to_string(entry.size)) << "\n";
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(const std::string& source_path, const std::string& dest_path)
{
    std::cout << "FS::cp(" << source_path << "," << dest_path << ")\n";

    uint8_t block[BLOCK_SIZE];

    // resolve source file

    std::string source_file_name;
    const int16_t source_parent_index = walk_path(source_path, source_file_name, "cp");

    if (source_parent_index == -1)
        return -1;

    if (disk.read(source_parent_index, block) != 0)
        return ERROR_R("cp", "could not read block " << source_parent_index);

    dir_entry source_entry{};
    int16_t index;
    if (!find_entry(block, source_parent_index, source_file_name, source_entry, index, "cp"))
        return -1;

    if (source_entry.type != TYPE_FILE)
        return ERROR_R("cp", "source is not a file");

    // resolve destination parent + base name

    std::string dest_file_name;
    int16_t dest_parent_index = walk_path(dest_path, dest_file_name, "cp", false);
    if (dest_parent_index == -1)
        return -1;

    if (disk.read(dest_parent_index, block) != 0)
        return ERROR_R("cp", "could not read block " << dest_parent_index);

    // does destination exist?

    dir_entry dest_entry{};
    int16_t dest_index;
    const bool exists = find_entry(block, dest_parent_index, dest_file_name, dest_entry, dest_index, "cp", false);

    // file exits with that name, don't overwrite it
    if (exists)
    {
        if (dest_entry.type == TYPE_FILE)
            return ERROR_R("cp", "file or directory with same name already exists");

        dest_parent_index = static_cast<int16_t>(dest_entry.first_blk);
        dest_file_name = source_file_name;

        if (disk.read(dest_parent_index, block) != 0)
            return ERROR_R("cp", "could not read block " << dest_parent_index);

        dir_entry existing{};
        int16_t existing_index;
        if (find_entry(block, dest_parent_index, source_file_name, existing, existing_index, "cp", false))
            return ERROR_R("cp", "file or directory with same name already in directory");
    }

    // allocate new blocks for the file

    const int amount_of_blocks = count_blocks(source_entry.first_blk);
    const std::vector<uint16_t> empty_blocks = find_empty_blocks(amount_of_blocks, "cp");

    if (empty_blocks.empty())
        return -1;

    // prepare new entry
    dir_entry new_entry = {
        .file_name = "",
        .size = source_entry.size,
        .first_blk = empty_blocks.front(),
        .type = TYPE_FILE,
        .access_rights = source_entry.access_rights,
    };

    std::strncpy(new_entry.file_name, dest_file_name.c_str(), sizeof(new_entry.file_name) - 1);

    // add new entry
    if (!add_dir_entry(block, dest_parent_index, new_entry, "cp"))
        return -1;

    add_blocks_to_fat(empty_blocks, "cp");

    // copy data blocks

    const std::vector<uint16_t> source_blocks = get_related_blocks(source_entry.first_blk);

    // copy data
    for (int i = 0; i < amount_of_blocks; i++)
    {
        if (disk.read(source_blocks[i], block) != 0)
            return ERROR_R("cp", "could not read block " << source_blocks[i]);

        if (disk.write(empty_blocks[i], block) != 0)
            return ERROR_R("cp", "could not write to block " << empty_blocks[i]);
    }

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(const std::string& source_path, const std::string& dest_path)
{
    std::cout << "FS::mv(" << source_path << "," << dest_path << ")\n";

    std::string source_file_name;
    const int16_t source_parent_index = walk_path(source_path, source_file_name, "cp");

    if (source_parent_index == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(source_parent_index, block) != 0)
        return ERROR_R("cp", "could not read block " << source_parent_index);

    dir_entry source_entry{};
    int16_t index;
    if (!find_entry(block, source_parent_index, source_file_name, source_entry, index, "cp"))
        return -1;

    if ((source_entry.access_rights & READ & WRITE) != (READ & WRITE))
        return ERROR_R("cp", "cannot read source");

    std::string dest_file_name;
    int16_t dest_parent_index = walk_path(dest_path, dest_file_name, "cp", false);
    if (dest_parent_index == -1)
        return -1;

    if (disk.read(dest_parent_index, block) != 0)
        return ERROR_R("mv", "could not read block " << dest_parent_index);

    dir_entry dest_entry{};
    int16_t dest_index;
    const bool exists = find_entry(block, dest_parent_index, dest_file_name, dest_entry, dest_index, "cp", false);

    // file exits with that name, don't overwrite it
    if (exists)
    {
        if (dest_entry.type == TYPE_FILE)
            return ERROR_R("mv", "file or directory with same name already exists");

        // it exists but is a directory
        // so we need to use the source file name
        dest_parent_index = static_cast<int16_t>(dest_entry.first_blk);
        dest_file_name = source_file_name;

        if (disk.read(dest_parent_index, block) != 0)
            return ERROR_R("mv", "could not read block " << dest_parent_index);

        dir_entry existing{};
        int16_t existing_index;
        if (find_entry(block, dest_parent_index, source_file_name, existing, existing_index, "cp", false))
            return ERROR_R("mv", "file or directory with same name already in directory");
    }

    std::strncpy(source_entry.file_name, dest_file_name.c_str(), sizeof(source_entry.file_name) - 1);

    // add new entry
    if (!add_dir_entry(block, dest_parent_index, source_entry, "mv"))
        return -1;

    // remove old entry
    if (disk.read(source_parent_index, block) != 0)
        return ERROR_R("mv", "could not read block " << source_parent_index);

    if (!overwrite_dir_entry(block, source_parent_index, {}, index, "mv"))
        return -1;

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(const std::string& filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";

    std::string name;
    const int16_t parent = walk_path(filepath, name, "rm");

    if (parent == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(parent, block) != 0)
        return ERROR_R("rm", "could not read block " << parent);

    dir_entry entry{};
    int16_t index;
    if (!find_entry(block, parent, name, entry, index, "rm"))
        return -1;

    if ((entry.access_rights & WRITE) != WRITE)
        return ERROR_R("rm", "no permission to delete");

    constexpr dir_entry empty{};
    overwrite_dir_entry(block, parent, empty, index, "rm");

    const std::vector<uint16_t> blocks = get_related_blocks(entry.first_blk);

    if (blocks.empty())
        return -1;

    if (!remove_blocks_from_fat(blocks, "rm"))
        return -1;

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(const std::string& filepath1, const std::string& filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

    uint8_t block[BLOCK_SIZE];

    // get the first file

    std::string file_name_1;
    const int16_t parent_1 = walk_path(filepath1, file_name_1, "append");

    if (parent_1 == -1)
        return -1;

    if (disk.read(parent_1, block) != 0)
        return ERROR_R("append", "could not read block " << parent_1);

    // check that it exists
    dir_entry file_entry_1{};
    int16_t file_index_1;
    if (!find_entry(block, parent_1, file_name_1, file_entry_1, file_index_1, "append"))
        return -1;

    // check it's a file
    if (file_entry_1.type != TYPE_FILE)
        return ERROR_R("append", file_name_1 << " is not a file");

    // get the second file
    std::string file_name_2;
    const int16_t parent_2 = walk_path(filepath2, file_name_2, "append");

    if (parent_2 == -1)
        return -1;

    if (disk.read(parent_2, block) != 0)
        return ERROR_R("append", "could not read block " << parent_2);

    dir_entry file_entry_2{};
    int16_t file_index_2;
    if (!find_entry(block, parent_2, file_name_2, file_entry_2, file_index_2, "append"))
        return -1;

    if (file_entry_2.type != TYPE_FILE)
        return ERROR_R("append", file_name_2 << " is not a file");

    const std::vector<uint16_t> file_1_blocks = get_related_blocks(file_entry_1.first_blk);
    std::vector<uint16_t> file_2_blocks = get_related_blocks(file_entry_2.first_blk);

    const int final_size = static_cast<int>(file_entry_1.size + file_entry_2.size);
    const int final_block_size = (final_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int new_blocks_size = final_block_size - static_cast<int>(file_2_blocks.size());

    if (new_blocks_size > 0)
    {
        const std::vector<uint16_t> new_blocks = find_empty_blocks(new_blocks_size, "append");

        if (new_blocks.empty())
            return -1;

        file_2_blocks.insert(file_2_blocks.end(), new_blocks.begin(), new_blocks.end());
    }

    int current_block_index = static_cast<int>(file_entry_2.size) / BLOCK_SIZE;
    int current_write_byte_offset = static_cast<int>(file_entry_2.size) % BLOCK_SIZE;

    file_entry_2.size = final_size;
    if (!overwrite_dir_entry(block, parent_2, file_entry_2, file_index_2, "append"))
        return -1;

    uint8_t write_block[BLOCK_SIZE];

    if (disk.read(file_2_blocks[current_block_index], write_block) != 0)
        return ERROR_R("append", "could not read block " << file_2_blocks[current_block_index]);

    for (const auto read_block : file_1_blocks)
    {
        if (disk.read(read_block, block) != 0)
            return ERROR_R("append", "could not read block " << read_block);

        for (const auto byte : block)
        {
            write_block[current_write_byte_offset++] = byte;

            if (current_write_byte_offset == BLOCK_SIZE)
            {
                if (disk.write(file_2_blocks[current_block_index], write_block) != 0)
                    return ERROR_R("append", "could not write block " << file_2_blocks[current_block_index]);

                current_write_byte_offset = 0;
                current_block_index++;

                if (current_block_index > file_2_blocks.size() - 1)
                    break;

                if (disk.read(file_2_blocks[current_block_index], write_block) != 0)
                    return ERROR_R("append", "could not read block " << file_2_blocks[current_block_index]);
            }
        }
    }

    add_blocks_to_fat(file_2_blocks, "append");

    return 0;
}

// mkdir <dirpath> creates a new subdirectory with the name <dirpath>
// in the current directory
int FS::mkdir(const std::string& dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";

    std::string dir_name;
    const int16_t block_index = walk_path(dirpath, dir_name, "mkdir");
    if (block_index == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("mkdir", "could not read block " << block_index);
        return -1;
    }

    dir_entry result_entry{};
    int16_t index;
    if (find_entry(block, block_index, dir_name, result_entry, index, "mkdir", false))
    {
        ERROR("mkdir", "there already exists a file or directory with this name");
        return -1;
    }

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

    if (disk.read(block_index, block) != 0)
    {
        ERROR("mkdir", "could not read block " << block_index);
        return -1;
    }

    if (!add_dir_entry(block, block_index, new_entry, "mkdir"))
        return -1;

    std::memset(block, 0, sizeof(block));

    // get permissions of parent
    if (!lookup_path("..", result_entry, "mkdir"))
        return -1;

    const dir_entry parent_entry = {
        .file_name = "..",
        .size = 0,
        .first_blk = static_cast<uint16_t>(block_index),
        .type = TYPE_DIR,
        .access_rights = result_entry.access_rights
    };

    if (!add_dir_entry(block, result[0], parent_entry, "mkdir"))
        return -1;

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";

    // normalize folders
    if (dirpath.back() == '/')
        dirpath.pop_back();

    dir_entry result{};
    if (!lookup_path(dirpath, result, "cd"))
        return -1;

    if (result.type == TYPE_FILE)
        return ERROR_R("cd", "cannot cd into a file");

    current_dir = result;

    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << "FS::pwd()\n";

    uint16_t current_block = current_dir.first_blk;
    std::string path;

    uint8_t block[BLOCK_SIZE];

    while (current_block != ROOT_BLOCK)
    {
        if (disk.read(current_block, block) != 0)
            return ERROR_R("pwd", "could not read block " << current_block);

        dir_entry parent_entry{};
        int16_t index;
        if (!find_entry(block, current_block, "..", parent_entry, index, "pwd", false))
            break; // probably on root block

        uint16_t parent_block = parent_entry.first_blk;

        if (disk.read(parent_block, block) != 0)
            return ERROR_R("pwd", "could not read block " << parent_block);

        dir_entry name_entry{};
        bool found_name = false;

        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++)
        {
            const dir_entry* entry = reinterpret_cast<dir_entry*>(block) + i;
            if (entry->first_blk == current_block && !is_entry_empty(*entry))
            {
                name_entry = *entry;
                found_name = true;
                break;
            }
        }

        if (!found_name)
            return ERROR_R("pwd", "could not find directory name in parent");

        path.insert(0, std::string(name_entry.file_name) + "/");

        current_block = parent_block;
    }

    path.insert(0, "/");

    std::cout << path << std::endl;

    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(const std::string& access_rights, const std::string& filepath)
{
    std::cout << "FS::chmod(" << access_rights << "," << filepath << ")\n";

    int value;

    try
    {
        value = std::stoi(access_rights);
    }
    catch (const std::invalid_argument&)
    {
        return ERROR_R("chmod", "the access rights is not a number");
    }
    catch (const std::out_of_range&)
    {
        return ERROR_R("chmod", "the access rights are too big");
    }

    if (value > (READ | WRITE | EXECUTE))
        return ERROR_R("chmod", "the access rights are too big");

    std::string name;
    const int16_t parent = walk_path(filepath, name, "chmod");

    if (parent == -1)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (disk.read(parent, block) != 0)
        return ERROR_R("chmod", "could not read block " << parent);

    dir_entry entry{};
    int16_t index;
    if (!find_entry(block, parent, name, entry, index, "chmod"))
        return -1;

    entry.access_rights = value;

    if (!overwrite_dir_entry(block, parent, entry, index, "chmod"))
        return -1;

    return 0;
}
