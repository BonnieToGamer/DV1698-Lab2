#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <cctype>
#include <algorithm>
#include "fs.h"

/**
 * Convert access_rights bitmask to "rwx" style string
 * @param rights The rights to convert
 * @return The string of rights formatted
 */
static std::string rights_to_string(uint8_t rights)
{
    std::string s = "---";
    if (rights & READ) s[0] = 'r';
    if (rights & WRITE) s[1] = 'w';
    if (rights & EXECUTE) s[2] = 'x';
    return s;
}

/**
 * Splits a path into individual strings
 * @param path The path to split
 * @return The split path
 */
static std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> components;
    std::string current;

    for (char c : path)
    {
        if (c == '/')
        {
            if (!current.empty())
            {
                components.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty())
        components.push_back(current);

    return components;
}

/**
 * Creates the folders '.' and '..' for a given directory.
 * @param current_block The block of the current directory
 * @param previous_block The block of the parent directory
 * @param block The block array to write to
 * @return Success status, 0 - success. 1 - failure
 */
static int create_navigation_folders(const uint16_t current_block, const uint16_t previous_block, uint8_t block[])
{
    // "." entry – directory itself
    dir_entry current_entry{};
    std::memset(&current_entry, 0, sizeof(current_entry));
    std::strcpy(current_entry.file_name, ".");
    current_entry.size = 0;
    current_entry.first_blk = current_block;
    current_entry.type = TYPE_DIR;
    current_entry.access_rights = READ | WRITE | EXECUTE;

    // ".." entry – parent directory
    dir_entry previous_entry{};
    std::memset(&previous_entry, 0, sizeof(previous_entry));
    std::strcpy(previous_entry.file_name, "..");
    previous_entry.size = 0;
    previous_entry.first_blk = previous_block;
    previous_entry.type = TYPE_DIR;
    previous_entry.access_rights = READ | WRITE | EXECUTE;

    std::memcpy(block, &current_entry, sizeof(dir_entry));
    std::memcpy(block + sizeof(dir_entry), &previous_entry, sizeof(dir_entry));

    return 0;
}

int FS::write_fat_to_disk()
{
    return disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
}

int FS::add_blocks_to_fat(const std::vector<int16_t>& blocks)
{
    for (size_t i = 0; i < blocks.size(); i++)
        fat[blocks[i]] = (i == blocks.size() - 1) ? FAT_EOF : blocks[i + 1];

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

        if (static_cast<int>(blocks.size()) == amount)
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

    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        if (entry->file_name[0] == '\0')
        {
            // free slot: but we must first check duplicate name
        }
        else
        {
            if (std::string(entry->file_name) == new_entry.file_name)
            {
                std::cout << "[" << callee << "] Error: file with that name already exists\n";
                return -1;
            }
            continue;
        }

        std::memcpy(block + i, &new_entry, sizeof(dir_entry));
        if (disk.write(block_index, block) != 0)
        {
            std::cout << "[" << callee << "] Error: could not write directory block\n";
            return -1;
        }

        space_available = true;
        break;
    }

    if (!space_available)
    {
        std::cout << "[" << callee << "] Error: no space available in current directory" << std::endl;
        return -1;
    }

    return 0;
}

int FS::find_entry_in_dir(uint16_t dir_block, const std::string& name, dir_entry& result)
{
    uint8_t block[BLOCK_SIZE];
    if (disk.read(dir_block, block) != 0)
        return -1;

    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        if (entry->file_name[0] == '\0')
            continue;

        if (std::string(entry->file_name) == name)
        {
            std::memcpy(&result, entry, sizeof(dir_entry));
            return 0;
        }
    }

    return -1;
}

int FS::navigate_to_dir(const std::string& path, dir_entry& result_dir)
{
    if (path.empty())
    {
        std::memcpy(&result_dir, &current_dir, sizeof(dir_entry));
        return 0;
    }

    dir_entry working_dir;
    bool is_absolute = (path[0] == '/');

    if (is_absolute)
    {
        uint8_t block[BLOCK_SIZE];
        if (disk.read(ROOT_BLOCK, block) != 0)
            return -1;

        auto* entries = reinterpret_cast<dir_entry*>(block);
        std::memcpy(&working_dir, &entries[0], sizeof(dir_entry));
    }
    else
    {
        std::memcpy(&working_dir, &current_dir, sizeof(dir_entry));
    }

    std::vector<std::string> components = split_path(path);

    for (const auto& component : components)
    {
        if (component == ".")
            continue;

        dir_entry next_dir;
        if (find_entry_in_dir(working_dir.first_blk, component, next_dir) != 0)
            return -1;

        if (next_dir.type != TYPE_DIR)
            return -1;

        std::memcpy(&working_dir, &next_dir, sizeof(dir_entry));
    }

    std::memcpy(&result_dir, &working_dir, sizeof(dir_entry));
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

    auto* entries = reinterpret_cast<dir_entry*>(block);
    std::memcpy(&current_dir, &entries[0], sizeof(dir_entry));
}

FS::~FS()
{
}

// ------------------------------------------------------------------
// format
// ------------------------------------------------------------------
int FS::format()
{
    std::cout << "FS::format()\n";

    uint8_t temp_arr[BLOCK_SIZE] = {};
    disk.write(ROOT_BLOCK, temp_arr);

    std::memset(fat, FAT_FREE, sizeof(fat));
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    write_fat_to_disk();

    auto* entries = reinterpret_cast<dir_entry*>(temp_arr);
    std::memcpy(&current_dir, &entries[0], sizeof(dir_entry));

    return 0;
}

// ------------------------------------------------------------------
// create
// ------------------------------------------------------------------
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

    int size = 0;
    for (const auto& input : user_input)
        size += static_cast<int>(input.size());
    size += static_cast<int>(user_input.size());

    const int block_count = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    std::vector<int16_t> blocks;
    find_empty_blocks(blocks, block_count);

    if (static_cast<int>(blocks.size()) != block_count)
    {
        std::cout << "[FS::create] Error: not enough free space to create file" << std::endl;
        return -1;
    }

    dir_entry new_entry{};
    std::memset(&new_entry, 0, sizeof(new_entry));
    std::strncpy(new_entry.file_name, filepath.c_str(), sizeof(new_entry.file_name) - 1);
    new_entry.size = static_cast<uint32_t>(size);
    new_entry.first_blk = static_cast<uint16_t>(blocks[0]);
    new_entry.type = TYPE_FILE;
    new_entry.access_rights = READ | WRITE;

    if (write_new_file_descriptor(new_entry, static_cast<int16_t>(current_dir.first_blk), "FS::create") != 0)
        return -1;

    if (add_blocks_to_fat(blocks) != 0)
    {
        std::cout << "[FS::create] Error: could not write FAT to disk\n";
        return -1;
    }

    int byte_offset = 0;
    int block_offset = 0;
    uint8_t block[BLOCK_SIZE] = {};

    auto flush_block_if_full = [&]()
    {
        if (byte_offset == BLOCK_SIZE)
        {
            disk.write(blocks[block_offset], block);
            byte_offset = 0;
            block_offset++;
            std::memset(block, 0, BLOCK_SIZE);
        }
    };

    for (const auto& input : user_input)
    {
        for (char c : input)
        {
            block[byte_offset++] = static_cast<uint8_t>(c);
            flush_block_if_full();
        }
        block[byte_offset++] = '\n';
        flush_block_if_full();
    }

    if (byte_offset > 0)
        disk.write(blocks[block_offset], block);

    return 0;
}

// ------------------------------------------------------------------
// cat
// ------------------------------------------------------------------
int FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";

    uint8_t block[BLOCK_SIZE];

    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::ls] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    dir_entry file_entry{};
    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        if (entry->file_name[0] == '\0')
            continue;

        if (std::string(entry->file_name) == filepath)
        {
            std::memcpy(&file_entry, entry, sizeof(dir_entry));
            break;
        }
    }

    if (file_entry.file_name[0] == '\0')
    {
        std::cout << filepath << ": does not exist\n";
        return -1;
    }

    if (file_entry.type != TYPE_FILE)
    {
        std::cout << filepath << ": not a file\n";
        return -1;
    }

    if (!(file_entry.access_rights & READ))
    {
        std::cout << "[FS::cat] Error: No read permission for file '" << filepath << "'\n";
        return -1;
    }

    std::vector<int16_t> blocks;
    get_blocks_from_fat(blocks, file_entry.first_blk);

    int bytes_read = 0;

    for (auto block_index : blocks)
    {
        if (disk.read(block_index, block) != 0)
        {
            std::cout << "[FS::cat] Error reading block nr " << block_index << "\n";
            return -1;
        }

        for (auto byte : block)
        {
            if (bytes_read == static_cast<int>(file_entry.size))
                break;

            std::cout << static_cast<char>(byte);
            bytes_read++;
        }
    }

    std::cout << std::endl;

    return 0;
}

// ------------------------------------------------------------------
// ls (now with accessrights)
// ------------------------------------------------------------------
int FS::ls()
{
    uint8_t block[BLOCK_SIZE];

    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::ls] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    std::cout << "name\t type\t accessrights\t size\n";

    std::vector<std::string> dir_strs;
    std::vector<std::string> file_strs;

    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        const auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        if (entry->file_name[0] == '\0')
            continue;

        std::string name(entry->file_name);
        if (name == "." || name == "..")
            continue; // hide navigation entries

        std::string type_str = (entry->type == TYPE_DIR) ? "dir" : "file";
        std::string rights_str = rights_to_string(entry->access_rights);

        std::string result = name + "\t ";
        result += type_str + "\t ";
        result += rights_str + "\t ";

        if (entry->type == TYPE_DIR)
        {
            result += "-";
            dir_strs.emplace_back(result);
        }

        else
        {
            result += std::to_string(entry->size);
            file_strs.emplace_back(result);
        }
    }

    for (const auto& str : dir_strs)
        std::cout << str << "\n";
    for (const auto& str : file_strs)
        std::cout << str << "\n";

    std::cout << std::flush;
    return 0;
}

// ------------------------------------------------------------------
// cp
// ------------------------------------------------------------------
int FS::cp(std::string sourcepath, std::string destpath)
{
    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::cp] Error: cannot read current dir block\n";
        return -1;
    }

    dir_entry source_entry{};
    bool source_found = false;

    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        auto* entry = reinterpret_cast<dir_entry*>(&block[i]);
        if (entry->file_name[0] == '\0')
            continue;

        if (std::string(entry->file_name) == sourcepath)
        {
            source_entry = *entry;
            source_found = true;
            break;
        }
    }

    if (!source_found)
    {
        std::cout << "[FS::cp] Error: source file does not exist\n";
        return -1;
    }

    if (source_entry.type != TYPE_FILE)
    {
        std::cout << "[FS::cp] Error: can only copy files\n";
        return -1;
    }

    bool dest_is_dir = false;
    dir_entry dest_dir{};
    uint16_t target_dir_block = 0;
    std::string dest_filename;

    if (navigate_to_dir(destpath, dest_dir) == 0)
    {
        dest_is_dir = true;
        target_dir_block = dest_dir.first_blk;
        dest_filename = sourcepath;
    }
    else
    {
        std::string parent_path;
        std::string name;

        auto pos = destpath.rfind('/');
        if (pos == std::string::npos)
        {
            parent_path = "";
            name = destpath;
        }
        else
        {
            if (pos == 0)
                parent_path = "/";
            else
                parent_path = destpath.substr(0, pos);
            name = destpath.substr(pos + 1);
        }

        dir_entry parent_dir{};
        if (parent_path.empty())
        {
            std::memcpy(&parent_dir, &current_dir, sizeof(dir_entry));
        }
        else
        {
            if (navigate_to_dir(parent_path, parent_dir) != 0)
            {
                std::cout << "[FS::cp] Error: destination parent directory not found\n";
                return -1;
            }
        }

        target_dir_block = parent_dir.first_blk;
        dest_filename = name;

        uint8_t dir_block[BLOCK_SIZE];
        if (disk.read(target_dir_block, dir_block) != 0)
        {
            std::cout << "[FS::cp] Error: cannot read destination directory\n";
            return -1;
        }

        bool name_exists = false;
        dir_entry existing{};
        for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
        {
            auto* entry = reinterpret_cast<dir_entry*>(&dir_block[i]);
            if (entry->file_name[0] == '\0')
                continue;

            if (std::string(entry->file_name) == dest_filename)
            {
                name_exists = true;
                existing = *entry;
                break;
            }
        }

        if (name_exists)
        {
            if (existing.type == TYPE_DIR)
            {
                dest_is_dir = true;
                dest_dir = existing;
                target_dir_block = existing.first_blk;
                dest_filename = sourcepath;
            }
            else
            {
                std::cout << "[FS::cp] Error: destination file already exists\n";
                return -1;
            }
        }
    }

    std::vector<int16_t> src_blocks;
    get_blocks_from_fat(src_blocks, source_entry.first_blk);

    std::vector<int16_t> new_blocks;
    find_empty_blocks(new_blocks, static_cast<int>(src_blocks.size()));

    if (new_blocks.size() != src_blocks.size())
    {
        std::cout << "[FS::cp] Error: not enough free space to copy file\n";
        return -1;
    }

    if (add_blocks_to_fat(new_blocks) != 0)
    {
        std::cout << "[FS::cp] Error: could not write FAT to disk\n";
        return -1;
    }

    uint8_t buf[BLOCK_SIZE];
    for (size_t i = 0; i < src_blocks.size(); ++i)
    {
        if (disk.read(src_blocks[i], buf) != 0)
        {
            std::cout << "[FS::cp] Error: could not read block " << src_blocks[i] << "\n";
            return -1;
        }
        if (disk.write(new_blocks[i], buf) != 0)
        {
            std::cout << "[FS::cp] Error: could not write block " << new_blocks[i] << "\n";
            return -1;
        }
    }

    dir_entry destination = source_entry;
    destination.first_blk = new_blocks[0];
    std::memset(destination.file_name, 0, sizeof(destination.file_name));
    std::strncpy(destination.file_name, dest_filename.c_str(), sizeof(destination.file_name) - 1);

    if (write_new_file_descriptor(destination, static_cast<int16_t>(target_dir_block), "FS::cp") != 0)
        return -1;

    return 0;
}

// ------------------------------------------------------------------
// mv
// ------------------------------------------------------------------
int FS::mv(std::string sourcepath, std::string destpath)
{
    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::mv] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    int source_id = -1;
    dir_entry source_entry{};

    for (int i = 0; i < max_entries; i++)
    {
        if (entries[i].file_name[0] == '\0')
            continue;

        if (std::strcmp(entries[i].file_name, sourcepath.c_str()) == 0)
        {
            source_id = i;
            std::memcpy(&source_entry, &entries[i], sizeof(dir_entry));

            if (source_entry.type != TYPE_FILE)
            {
                std::cout << "[FS::mv] Error: can not move directories with mv" << std::endl;
                return -1;
            }

            if (!(source_entry.access_rights & WRITE))
            {
                std::cout << "[FS::mv] Error: no write permission for " << sourcepath << std::endl;
                return -1;
            }
            break;
        }
    }

    if (source_id == -1)
    {
        std::cout << "[FS::mv] Error: Source file not found" << std::endl;
        return -1;
    }

    bool dest_is_dir = false;
    dir_entry dest_dir{};

    if (navigate_to_dir(destpath, dest_dir) == 0)
    {
        dest_is_dir = true;
    }
    else
    {
        for (int i = 0; i < max_entries; i++)
        {
            if (entries[i].file_name[0] == '\0')
                continue;

            if (std::strcmp(entries[i].file_name, destpath.c_str()) == 0)
            {
                if (entries[i].type == TYPE_DIR)
                {
                    dest_is_dir = true;
                    dest_dir = entries[i];
                }
                else
                {
                    std::cout << "[FS::mv] Error: Destination file " << destpath << " already exists " << std::endl;
                    return -1;
                }
                break;
            }
        }
    }

    if (dest_is_dir)
    {
        if (write_new_file_descriptor(source_entry, dest_dir.first_blk, "FS::mv") != 0)
            return -1;

        if (disk.read(current_dir.first_blk, block) != 0)
        {
            std::cout << "[FS::mv] Error: cannot read current dir block" << std::endl;
            return -1;
        }
        entries = reinterpret_cast<dir_entry*>(block);

        for (int i = 0; i < max_entries; i++)
        {
            if (entries[i].file_name[0] != '\0' &&
                std::strcmp(entries[i].file_name, sourcepath.c_str()) == 0)
            {
                std::memset(&entries[i], 0, sizeof(dir_entry));
                break;
            }
        }

        if (disk.write(current_dir.first_blk, block) != 0)
        {
            std::cout << "[FS::mv] Error: Could not update source directory" << std::endl;
            return -1;
        }
    }
    else
    {
        if (destpath.length() >= sizeof(entries[source_id].file_name))
        {
            std::cout << "[FS::mv] Error: Destination filename too long" << std::endl;
            return -1;
        }

        std::memset(entries[source_id].file_name, 0, sizeof(entries[source_id].file_name));
        std::strncpy(entries[source_id].file_name, destpath.c_str(),
                     sizeof(entries[source_id].file_name) - 1);

        if (disk.write(current_dir.first_blk, block) != 0)
        {
            std::cout << "[FS::mv] Error: Could not write dir to disk" << std::endl;
            return -1;
        }
    }

    return 0;
}

// ------------------------------------------------------------------
// rm
// ------------------------------------------------------------------
int FS::rm(std::string filepath)
{
    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::rm] Error: could not read directory\n";
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    int entry_id = -1;
    for (int i = 0; i < max_entries; i++)
    {
        if (entries[i].file_name[0] == '\0')
            continue;

        if (std::strcmp(entries[i].file_name, filepath.c_str()) == 0)
        {
            if (entries[i].type != TYPE_FILE)
            {
                std::cout << "[FS::rm] Error: Cannot remove directories with rm" << std::endl;
                return -1;
            }

            if (!(entries[i].access_rights & WRITE))
            {
                std::cout << "[FS::rm] Error: No write permission for " << filepath << std::endl;
                return -1;
            }

            entry_id = i;
            break;
        }
    }

    if (entry_id == -1)
    {
        std::cout << "couldnt find:" << filepath << "\n";
        return -1;
    }

    dir_entry& file = entries[entry_id];
    int16_t current_block_index = file.first_blk;

    while (current_block_index != FAT_EOF)
    {
        if (current_block_index >= FAT_ENTRIES || current_block_index < 0)
        {
            std::cout << "[FS::rm] Error: Invalid FAT entry " << current_block_index
                << " - corrupted filesystem" << std::endl;
            return -1;
        }

        int16_t next_block = fat[current_block_index];

        uint8_t empty[BLOCK_SIZE] = {0};
        disk.write(current_block_index, empty);

        fat[current_block_index] = FAT_FREE;

        current_block_index = next_block;
    }

    std::memset(&entries[entry_id], 0, sizeof(dir_entry));
    disk.write(current_dir.first_blk, block);

    write_fat_to_disk();

    return 0;
}

// ------------------------------------------------------------------
// append
// ------------------------------------------------------------------
int FS::append(std::string filepath1, std::string filepath2)
{
    if (filepath1.empty() || filepath2.empty()) return -1;

    uint8_t dir_block[BLOCK_SIZE] = {};
    disk.read(current_dir.first_blk, dir_block);

    auto* dirEntries = reinterpret_cast<dir_entry*>(dir_block);
    const int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    dir_entry* file_entry1 = nullptr;
    dir_entry* file_entry2 = nullptr;

    for (int i = 0; i < max_entries; ++i)
    {
        if (dirEntries[i].file_name[0] == '\0')
            continue;

        if (std::string(dirEntries[i].file_name) == filepath1)
            file_entry1 = &dirEntries[i];
        else if (std::string(dirEntries[i].file_name) == filepath2)
            file_entry2 = &dirEntries[i];

        if (file_entry1 && file_entry2) break;
    }

    if (!file_entry1 || !file_entry2) return -1;
    if (file_entry1->type != TYPE_FILE || file_entry2->type != TYPE_FILE) return -1;

    if (!(file_entry1->access_rights & READ))
    {
        std::cout << "[FS::append] Error: No read permission for file '" << filepath1 << "'" << std::endl;
        return -1;
    }

    if (!(file_entry2->access_rights & WRITE))
    {
        std::cout << "[FS::append] Error: No write permission for file '" << filepath2 << "'" << std::endl;
        return -1;
    }

    uint8_t fatbuf[BLOCK_SIZE];
    if (disk.read(FAT_BLOCK, fatbuf) == -1) return -1;
    std::memcpy(fat, fatbuf, sizeof(fat));

    uint16_t last_blk2 = file_entry2->first_blk;
    uint32_t size2 = file_entry2->size;

    while (fat[last_blk2] != FAT_EOF)
        last_blk2 = fat[last_blk2];

    uint32_t lastblk_offset = size2 % BLOCK_SIZE;
    uint8_t buf2[BLOCK_SIZE];

    if (lastblk_offset == 0)
    {
        int new_blk = -1;
        for (int i = FAT_BLOCK + 1; i < FAT_ENTRIES; ++i)
        {
            if (fat[i] == FAT_FREE)
            {
                new_blk = i;
                break;
            }
        }
        if (new_blk < 0)
        {
            std::cout << "[FS::append] Error: no free blocks\n";
            return -1;
        }

        fat[last_blk2] = static_cast<uint16_t>(new_blk);
        fat[new_blk] = FAT_EOF;
        last_blk2 = static_cast<uint16_t>(new_blk);

        std::memset(buf2, 0, BLOCK_SIZE);
        lastblk_offset = 0;
    }
    else
    {
        if (disk.read(last_blk2, buf2) == -1)
            return -1;
    }

    uint16_t blk1 = file_entry1->first_blk;
    uint32_t bytes_left1 = file_entry1->size;
    uint8_t buf1[BLOCK_SIZE];

    while (bytes_left1 > 0)
    {
        if (disk.read(blk1, buf1) == -1)
            return -1;

        uint32_t bytes_from_this_block = std::min<uint32_t>(bytes_left1, BLOCK_SIZE);
        uint32_t src_offset = 0;

        while (bytes_from_this_block > 0)
        {
            if (lastblk_offset == BLOCK_SIZE)
            {
                if (disk.write(last_blk2, buf2) == -1)
                    return -1;

                int new_blk = -1;
                for (int i = FAT_BLOCK + 1; i < FAT_ENTRIES; ++i)
                {
                    if (fat[i] == FAT_FREE)
                    {
                        new_blk = i;
                        break;
                    }
                }
                if (new_blk < 0)
                {
                    std::cout << "[FS::append] Error: no free blocks\n";
                    return -1;
                }

                fat[last_blk2] = static_cast<uint16_t>(new_blk);
                fat[new_blk] = FAT_EOF;
                last_blk2 = static_cast<uint16_t>(new_blk);

                std::memset(buf2, 0, BLOCK_SIZE);
                lastblk_offset = 0;
            }

            uint32_t space = BLOCK_SIZE - lastblk_offset;
            uint32_t chunk = std::min(space, bytes_from_this_block);

            std::memcpy(buf2 + lastblk_offset, buf1 + src_offset, chunk);

            lastblk_offset += chunk;
            src_offset += chunk;
            bytes_from_this_block -= chunk;
            bytes_left1 -= chunk;
        }

        if (fat[blk1] == FAT_EOF)
            break;
        blk1 = fat[blk1];
    }

    if (disk.write(last_blk2, buf2) == -1)
        return -1;

    file_entry2->size += file_entry1->size;

    if (disk.write(current_dir.first_blk, dir_block) == -1)
        return -1;

    write_fat_to_disk();

    return 0;
}

// ------------------------------------------------------------------
// mkdir
// ------------------------------------------------------------------
int FS::mkdir(std::string dirpath)
{
    std::vector<std::string> components = split_path(dirpath);

    if (components.empty())
    {
        std::cout << "[FS::mkdir] Error: Invalid path" << std::endl;
        return -1;
    }

    std::string new_dir_name = components.back();

    if (new_dir_name.length() >= sizeof(dir_entry().file_name))
    {
        std::cout << "[FS::mkdir] Error: Directory name too long" << std::endl;
        return -1;
    }

    if (new_dir_name == ".." || new_dir_name == ".")
    {
        std::cout << "[FS::mkdir] Error: Can not create dir with reserved name: " << new_dir_name << std::endl;
        return -1;
    }

    dir_entry parent_dir{};
    if (components.size() == 1 && dirpath[0] != '/')
    {
        std::memcpy(&parent_dir, &current_dir, sizeof(dir_entry));
    }
    else
    {
        std::string parent_path;
        if (dirpath[0] == '/')
            parent_path = "/";

        for (size_t i = 0; i < components.size() - 1; i++)
        {
            if (!parent_path.empty() && parent_path.back() != '/')
                parent_path += "/";
            parent_path += components[i];
        }

        if (navigate_to_dir(parent_path, parent_dir) != 0)
        {
            std::cout << "[FS::mkdir] Error: Parent directory not found" << std::endl;
            return -1;
        }
    }

    uint8_t dir_block[BLOCK_SIZE];
    if (disk.read(parent_dir.first_blk, dir_block) != 0)
    {
        std::cout << "[FS::mkdir] Error: Can not read parent dir" << std::endl;
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(dir_block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    int free_space = -1;
    for (int i = 0; i < max_entries; i++)
    {
        if (entries[i].file_name[0] == '\0')
        {
            if (free_space == -1) free_space = i;
            continue;
        }

        if (std::strcmp(entries[i].file_name, new_dir_name.c_str()) == 0)
        {
            std::cout << "[FS::mkdir] Error: directory or file: " << new_dir_name << " already exists" << std::endl;
            return -1;
        }
    }

    if (free_space == -1)
    {
        std::cout << "[FS::mkdir] Error: directory is full" << std::endl;
        return -1;
    }

    int16_t new_dir_block_idx = -1;
    for (int i = 0; i < FAT_ENTRIES; i++)
    {
        if (fat[i] == FAT_FREE && i != ROOT_BLOCK && i != FAT_BLOCK)
        {
            new_dir_block_idx = i;
            break;
        }
    }

    if (new_dir_block_idx == -1)
    {
        std::cout << "[FS::mkdir] Error: No free blocks for new directory" << std::endl;
        return -1;
    }

    uint8_t new_dir_data[BLOCK_SIZE] = {0};
    create_navigation_folders(static_cast<uint16_t>(new_dir_block_idx),
                              parent_dir.first_blk,
                              new_dir_data);

    disk.write(new_dir_block_idx, new_dir_data);

    fat[new_dir_block_idx] = FAT_EOF;
    write_fat_to_disk();

    dir_entry new_dir_entry{};
    std::memset(&new_dir_entry, 0, sizeof(new_dir_entry));
    std::strcpy(new_dir_entry.file_name, new_dir_name.c_str());
    new_dir_entry.size = 0;
    new_dir_entry.first_blk = static_cast<uint16_t>(new_dir_block_idx);
    new_dir_entry.type = TYPE_DIR;
    new_dir_entry.access_rights = READ | WRITE | EXECUTE;

    std::memcpy(&entries[free_space], &new_dir_entry, sizeof(dir_entry));
    disk.write(parent_dir.first_blk, dir_block);

    return 0;
}

// ------------------------------------------------------------------
// cd
// ------------------------------------------------------------------
int FS::cd(std::string dirpath)
{
    if (dirpath.empty())
    {
        std::cout << "[FS::cd] Error: Directory path cannot be empty" << std::endl;
        return -1;
    }

    dir_entry target_dir;
    if (navigate_to_dir(dirpath, target_dir) != 0)
    {
        std::cout << "[FS::cd] Error: Directory '" << dirpath << "' not found" << std::endl;
        return -1;
    }

    if (!(target_dir.access_rights & EXECUTE))
    {
        std::cout << "[FS::cd] Error: No execute permission for directory '" << dirpath << "'" << std::endl;
        return -1;
    }

    std::memcpy(&current_dir, &target_dir, sizeof(dir_entry));
    return 0;
}

// ------------------------------------------------------------------
// pwd
// ------------------------------------------------------------------
int FS::pwd()
{
    if (current_dir.first_blk == ROOT_BLOCK)
    {
        std::cout << "/" << std::endl;
        return 0;
    }

    std::vector<std::string> path_parts;

    uint16_t child_block = current_dir.first_blk;

    while (true)
    {
        if (child_block == ROOT_BLOCK)
            break;

        uint8_t child_buf[BLOCK_SIZE];
        if (disk.read(child_block, child_buf) != 0)
        {
            std::cout << "[FS::pwd] Error: Cannot read dir block " << child_block << std::endl;
            return -1;
        }

        auto* child_entries = reinterpret_cast<dir_entry*>(child_buf);

        if (std::strcmp(child_entries[1].file_name, "..") != 0)
        {
            std::cout << "[FS::pwd] Error: invalid directory structure\n";
            return -1;
        }

        uint16_t parent_block = child_entries[1].first_blk;

        uint8_t parent_buf[BLOCK_SIZE];
        if (disk.read(parent_block, parent_buf) != 0)
        {
            std::cout << "[FS::pwd] Error: Cannot read parent dir\n";
            return -1;
        }

        auto* parent_entries = reinterpret_cast<dir_entry*>(parent_buf);
        int max_entries = BLOCK_SIZE / sizeof(dir_entry);

        std::string dirname;
        for (int i = 0; i < max_entries; ++i)
        {
            if (parent_entries[i].file_name[0] == '\0')
                continue;

            std::string n(parent_entries[i].file_name);
            if (n == "." || n == "..")
                continue;

            if (parent_entries[i].first_blk == child_block)
            {
                dirname = n;
                break;
            }
        }

        if (!dirname.empty())
            path_parts.push_back(dirname);

        if (parent_block == ROOT_BLOCK)
            break;

        child_block = parent_block;
    }

    std::cout << '/';
    for (int i = static_cast<int>(path_parts.size()) - 1; i >= 0; --i)
    {
        std::cout << path_parts[i];
        if (i != 0)
            std::cout << "/";
    }
    std::cout << std::endl;

    return 0;
}

// ------------------------------------------------------------------
// chmod
// ------------------------------------------------------------------
int FS::chmod(std::string accessrights, std::string filepath)
{
    if (filepath.empty()) return -1;

    uint8_t block[BLOCK_SIZE] = {};
    disk.read(current_dir.first_blk, block);

    auto* dirEntries = reinterpret_cast<dir_entry*>(block);
    const int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    dir_entry* file_entry = nullptr;
    for (int i = 0; i < max_entries; i++)
    {
        if (dirEntries[i].file_name[0] == '\0')
            continue;

        if (std::string(dirEntries[i].file_name) == filepath)
        {
            file_entry = &dirEntries[i];
            break;
        }
    }

    if (!file_entry)
    {
        std::cout << "not found" << std::endl;
        return -1;
    }

    uint8_t rights = 0;

    if (!accessrights.empty() && std::isdigit(static_cast<unsigned char>(accessrights[0])))
    {
        int num = accessrights[0] - '0';
        if (num & 4) rights |= READ;
        if (num & 2) rights |= WRITE;
        if (num & 1) rights |= EXECUTE;
    }
    else
    {
        for (char c : accessrights)
        {
            if (c == 'r') rights |= READ;
            if (c == 'w') rights |= WRITE;
            if (c == 'x') rights |= EXECUTE;
        }
    }

    file_entry->access_rights = rights;
    disk.write(current_dir.first_blk, block);

    return 0;
}
