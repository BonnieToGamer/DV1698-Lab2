#include <iostream>
#include <boost/algorithm/string.hpp>
#include <cstring>
#include "fs.h"

#include <queue>
/**
 * Finds amounts of blocks in FAT
 * @param amount Amount of of blocks to find
 * @param callee Caller of error function
 * @return A vector of block indices
 * 
 */
std::vector<uint16_t> FS::find_empty_blocks(const int amount, const std::string& callee) const
{
    std::vector<uint16_t> blocks;

    // Go through the fat and finds free blocks
    for (int i = 0; i < BLOCK_SIZE / 2; i++)
    {
        const auto block = fat[i];
        // If entry is marked fat free, add it to list
        if (block == FAT_FREE)
        {
            blocks.emplace_back(i);
            if (blocks.size() == amount)
                // Return the list when weve reached the desired amount
                return blocks;
        }
    }

    // check if the found blocks are enought
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
/**
 * Add new directory entry
 * @param block Pointer to the block data
 * @param block_index The index of the block
 * @param new_entry The entry to be added
 * @param callee Caller of error function
 * @return true if success otherwise false
 */
bool FS::add_dir_entry(uint8_t* block, const uint16_t block_index, const dir_entry& new_entry,
                       const std::string& callee)
{
    // cast block data to dir entries, for iteration
    const auto* entries = reinterpret_cast<dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);
    int index = 0;
    bool found_empty = false;

    // find empty space in the dir block
    for (; index < size; index++)
    {
        const auto& entry = entries[index];
        if (is_entry_empty(entry))
        {
            found_empty = true;
            break;
        }
    }

    // If it wasnt able to find empty dir in block
    if (!found_empty)
    {
        ERROR_C("Could not find empty dir entry in block " << block_index);
        return false;
    }

    // Copy mem of new entry to correct position in block buffer
    std::memcpy(block + index * sizeof(dir_entry), &new_entry, sizeof(dir_entry));

    // Write updated block buffer to disk
    if (disk.write(block_index, block) != 0)
    {
        ERROR_C("Could not write block" << block_index << " to disk");
        return false;
    }

    return true;
}
/**
 * Takes index and overwrites the existing dir
 * @param block Pointer to block
 * @param block_index The index of the block
 * @param entry_name New entry data
 * @param index The index of the block to be overwritten
 * @param callee The caller of the function
 * @return true if success otherwise false
 */
bool FS::overwrite_dir_entry(uint8_t* block, const uint16_t block_index, const dir_entry& new_entry,
                             const int16_t index,
                             const std::string& callee)
{
    // Copy mem of new entry to correct position in block buffer
    std::memcpy(block + index * sizeof(dir_entry), &new_entry, sizeof(dir_entry));

    // Write updated block buffer to disk
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
    // cast block data to dir entries, for iteration
    const auto* entries = reinterpret_cast<const dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);

    // go through the block to find specific entry
    for (int i = 0; i < size; i++)
    {
        const auto& entry = entries[i];
        // compare current entry filename with the name were looking for
        if (std::strcmp(entry.file_name, entry_name.c_str()) == 0)
        {
            // if finally found copy the data to result and store its index
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
    // use find_entry() to find entry and return its index
    if (find_entry(block, block_index, std::string(remove_entry.file_name), result, index, callee))
    {
        // If found entry already is empty we can end the function
        if (is_entry_empty(result))
            return false;
        
        // If the entry is found non empty clear the entry in block buffer by filling memory with zeros
        std::memset(block + index * sizeof(dir_entry), 0, sizeof(dir_entry));

        // Write block buffer back to disk
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
            ERROR_C("filename " << file_name << " to long");
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

    if (!split.empty())
        split.pop_back();

    uint8_t block[BLOCK_SIZE];
    uint16_t current_block_index = path.at(0) == '/' ? ROOT_BLOCK : current_dir.first_blk;
    int16_t index = ROOT_BLOCK;

    for (const auto& dir : split)
    {
        if (dir.empty())
            continue;
        
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
    // Use split path to split path string into dir/file parts
    const std::vector<std::string> split = split_path(path);

    uint8_t block[BLOCK_SIZE];
    // Traverse from root if path is "/", if not start from current directories first block
    uint16_t current_block_index = path.at(0) == '/' ? ROOT_BLOCK : current_dir.first_blk;

    // Go through each part of the split path
    for (int i = 0; i < split.size(); i++)
    {
        const auto& dir = split[i];

        // Make sure file name isnt larger than 56 char
        if (dir.size() >= 56)
        {
            ERROR_C("filename" << dir << " to long");
            return false;
        }

        // Read current dir block to search for next component
        if (disk.read(current_block_index, block) != 0)
        {
            ERROR_C("Could not read block " << current_dir.first_blk);
            return false;
        }

        // skip current dir ref
        if (dir == ".")
            continue;

        // we are in root block trying to go back
        if (current_block_index == ROOT_BLOCK && dir == "..")
        {
            //if .. is the last part of path at root, return root entry
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

        // search for the current split path part within the current block
        dir_entry entry{};
        int16_t index = -1;
        if (!find_entry(block, current_block_index, dir, entry, index, callee))
            return false;

        // we are on the last space, aka reached our target
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

bool FS::resolve_file(uint8_t* block, const std::string& path, dir_entry& out_entry, int16_t& out_index, int16_t& out_parent,
    const uint8_t required_permissions, const std::string& callee)
{
    // walk to parent
    std::string name;
    out_parent = walk_path(path, name, callee);
    if (out_parent < 0)
        return false;

    // read parent block
    if (disk.read(out_parent, block) != 0)
    {
        ERROR_C("could not read block " << out_parent);
        return false;
    }

    // find the entry
    if (!find_entry(block, out_parent, name, out_entry, out_index, callee))
        return false;

    // ensure it's a file
    if (out_entry.type != TYPE_FILE)
    {
        ERROR_C(name << " is not a file");
        return false;
    }

    // check permissions
    if ((out_entry.access_rights & required_permissions) != required_permissions)
    {
        ERROR_C("no permission for " << path);
        return false;
    }

    return true;
}
/**
* Writes the given blocks to the FAT and commits it to disk
* @param blocks Blocks to write to FAT
* @return Success status, 0 - success. 1 - failure
*/
bool FS::add_blocks_to_fat(const std::vector<unsigned short int>& blocks, const std::string& callee)
{
    // go through the block indexes to create chain
    for (int i = 0; i < blocks.size(); i++)
    {
        const auto block = blocks[i];
        // point block to the next block in vector, if it isnt the last one
        if (i < blocks.size() - 1)
            fat[block] = static_cast<int16_t>(blocks[i + 1]);
        else
        // last block gets EOF, end of file, mark
            fat[block] = FAT_EOF;
    }

    // write updated fat table to disk
    if (!write_fat_to_disk())
    {
        ERROR_C("could not write fat to disk");
        return false;
    }

    return true;
}

bool FS::remove_blocks_from_fat(const std::vector<unsigned short int>& blocks, const std::string& callee)
{
    // Set every block in the fat entry to the FREE state
    for (const unsigned short block : blocks)
        fat[block] = FAT_FREE;

    // write updated fat table to disk
    if (!write_fat_to_disk())
    {
        ERROR_C("could not write fat to disk");
        return false;
    }

    return true;
}
/**
* Writes the current fat to disk.
* @return Success status, 0 - success. 1 - failure
*/
bool FS::write_fat_to_disk()
{
    // casts fat array to raw byte pointer for disk writing
    return disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat)) == 0;
}

int FS::count_blocks(const uint16_t starter_block) const
{
    auto current_block = static_cast<int16_t>(starter_block);
    int amount = 1;

    // Go through fat until we hit EOF, in such a way we know when weve reached the end
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

    // Follow chain and store every index we find
    do
    {
        result.push_back(current_block);
        current_block = fat[current_block];
    }
    while (current_block != FAT_EOF);

    return result;
}

//loads fat from disk, thus initializing file system object; -> initial state of current working dir
FS::FS() : fat{}
{   
    //loads fat from block on disk
    if (disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat)) != 0)
    {
        //Fat has to be readable
        ERROR("FS", "could not read FAT block");
        return;
    }

    //initializes current working dir to root, starting user in root
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
    //sets all entries to zero, thus clearing FAT
    std::memset(&fat, 0, sizeof(fat));

    //marks root block and FAT block as end of file
    //so they cant be used or overwritten
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    //write initialized FAT to disk
    write_fat_to_disk();

    //initialize empty block buffer, directory with all zeros
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
    //takes user input lines until empty line
    std::vector<std::string> user_input;
    while (true)
    {
        std::string line;
        std::getline(std::cin, line);

        //as mentioned above, break when reading empty line
        if (line.empty())
            break;

        user_input.emplace_back(line);
    }

    //calculates size of file in bytes
    uint32_t size = 0;
    for (const auto& input : user_input)
        size += static_cast<int>(input.size()) + 1; // +1 for "\n"

    //calculates how many blocks will be needed to store the file
    const int block_count = (static_cast<int>(size) + BLOCK_SIZE - 1) / BLOCK_SIZE;

    //finds parent dir, and name if new file
    std::string file_name;
    const int16_t block_index = walk_path(filepath, file_name, "create");

    //if block index wierd, exit
    if (block_index == -1)
        return -1;

    //dont let users allow naming files ".." "." as they are reserved
    if (file_name == ".." || file_name == ".")
        return ERROR_R("create", "'..' is a reserved name");

    //initialize block and make sure block is readable
    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("create", "could not read block " << block_index);
        return -1;
    }
    //make sure file name is unique, using block from ablove
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

    //get number of free block in the FAT
    const std::vector<uint16_t> empty_blocks = find_empty_blocks(block_count, "create");

    //if no free blocks return with no creation
    if (empty_blocks.empty())
        return -1;

    //link new blocks together in the FAT table
    if (!add_blocks_to_fat(empty_blocks, "create"))
        return -1;

    //create dir entry data for new file
    dir_entry new_entry = {
        .file_name = "",
        .size = size,
        .first_blk = empty_blocks[0],
        .type = TYPE_FILE,
        .access_rights = READ | WRITE
    };

    //copy string of filename into fixed size character array of entry
    std::strncpy(new_entry.file_name, file_name.c_str(), sizeof(new_entry.file_name) - 1);

    // add new files entry into parent dir block
    if (!add_dir_entry(block, block_index, new_entry, "create"))
        return -1;

    //preparation of writing file data to assigned blocks
    int byte_offset = 0;
    int block_offset = 0;
    std::memset(block, 0, BLOCK_SIZE);

    //define helper lambda to flush current buffer to disk when it fills
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

    //go through stored user input and write to the blocks
    for (const auto& input : user_input)
    {
        for (const char c : input)
        {
            block[byte_offset++] = static_cast<uint8_t>(c);
            flush_block_if_full();
        }
        //appends newline char after every string from input vector
        block[byte_offset++] = '\n';
        flush_block_if_full();
    }

    //write last block to disk if theres a partially filled one with remaining data
    if (byte_offset > 0)
        disk.write(empty_blocks[block_offset], block);

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(const std::string& filepath)
{
    uint8_t block[BLOCK_SIZE];
    int16_t block_index, index;
    dir_entry result{};
    
    //resolve the path to file, check for read perms
    //fills result entry with file data
    if (!resolve_file(block, filepath, result, index, block_index, READ, "cat"))
        return -1;

    //gets full list of block indexes of file from FAT chain
    const auto blocks = get_related_blocks(result.first_blk);

    int bytes_read = 0;

    //goes through every block index in file block chain
    for (const auto related_block_index : blocks)
    {
        //read current data block
        if (disk.read(related_block_index, block) != 0)
        {
            ERROR("cat", "could not read block " << related_block_index);
            return -1;
        }

        //goes through every byte in block buff
        for (const auto byte : block)
        {
            //stop print when reached size of file
            if (bytes_read == static_cast<int>(result.size))
                break;
            
            //print byte as char in the console
            std::cout << static_cast<char>(byte);
            bytes_read++;
        }
    }

    std::cout << std::flush;

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
    // read the current directory block
    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        ERROR("ls", "could not read block " << current_dir.first_blk);
        return -1;
    }
    // get the entries and calculate how many directory entries fit into one block
    const dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    constexpr int size = BLOCK_SIZE / sizeof(dir_entry);

    // Lambda function that appends the permission characters
    auto check_access = [](const uint8_t access_rights, const uint8_t right, const char right_str, std::string& str)
    {
        if ((access_rights & right) == right)
            str += right_str;
        else
            str += '-';
    };
    // The header for the directory list
    std::cout << "name\t type\t accessrights\t size\n";

    // iterate over every possible directory entry in the block
    for (int i = 0; i < size; i++)
    {   
    
        const dir_entry entry = entries[i];
        if (is_entry_empty(entry) || std::string(entry.file_name) == "..")
            continue;

        // Checks amd appends the permissions
        std::string access_str;
        check_access(entry.access_rights, READ, 'r', access_str);
        check_access(entry.access_rights, WRITE, 'w', access_str);
        check_access(entry.access_rights, EXECUTE, 'x', access_str);

        // Prints the entry information
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
    uint8_t block[BLOCK_SIZE];
    int16_t source_parent_index, index;
    dir_entry source_entry{};

    // resolve source file
    if (!resolve_file(block, source_path, source_entry, index, source_parent_index, 0, "cp"))
        return -1;

    const std::string source_file_name = source_entry.file_name;

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
    // Resolve the file
    uint8_t block[BLOCK_SIZE];
    dir_entry source_entry{};
    int16_t source_parent_index, index;
    if (!resolve_file(block, source_path, source_entry, index, source_parent_index, READ | WRITE, "mv"))
        return -1;

    // Get the file name
    const std::string source_file_name = source_entry.file_name;
    
    // get the destination parent block index
    std::string dest_file_name;
    int16_t dest_parent_index = walk_path(dest_path, dest_file_name, "cp", false);
    if (dest_parent_index == -1)
        return -1;
    // read the block
    if (disk.read(dest_parent_index, block) != 0)
        return ERROR_R("mv", "could not read block " << dest_parent_index);
    //  check if the name already exists
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
        if (find_entry(block, dest_parent_index, source_file_name, existing, existing_index, "mv", false))
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

    uint8_t block[BLOCK_SIZE];
    int16_t index;
    dir_entry entry{};
    std::string name;
    // get the parent block index
    const int16_t parent = walk_path(filepath, name, "rm");
    if (parent < 0)
        return false;

    // read parent block
    if (disk.read(parent, block) != 0)
        return ERROR_R("rm", "could not read block " << parent);

    // find the entry
    if (!find_entry(block, parent, name, entry, index, "rm"))
        return -1;

    // Check the permissions of the entry
    if ((entry.access_rights & WRITE) != WRITE)
        return ERROR_R("rm", "no permission to delete " << name);
    // Check what type of entry it is.
    // If its a directory, check if its empty. If not empty we do not delete
    // if the directory is empty then continue to delete.
    if (entry.type == TYPE_DIR)
    {
        uint8_t dir_block[BLOCK_SIZE];
        if (disk.read(entry.first_blk, dir_block) != 0)
            return ERROR_R("rm", "could not read block " << parent);

        const auto* entries = reinterpret_cast<const dir_entry*>(dir_block);
        constexpr int size = BLOCK_SIZE / sizeof(dir_entry);

        for (int i = 0; i < size; i++)
        {
            if (!is_entry_empty(entries[i]))
                return ERROR_R("rm", "directory " << name <<  " is not empty");
        }
    }
    
    // Remove the entry 
    constexpr dir_entry empty{};
    overwrite_dir_entry(block, parent, empty, index, "rm");

    const std::vector<uint16_t> blocks = get_related_blocks(entry.first_blk);

    if (blocks.empty())
        return -1;
    // Remove the related blocks from fat
    if (!remove_blocks_from_fat(blocks, "rm"))
        return -1;

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(const std::string& filepath1, const std::string& filepath2)
{
    uint8_t block[BLOCK_SIZE];


    dir_entry file_entry_1{}, file_entry_2{};
    int16_t file_index_1, file_index_2, parent_1, parent_2;
    // resolve the first file
    if (!resolve_file(block, filepath1, file_entry_1, file_index_1, parent_1, READ, "append"))
        return -1;
    // resolve the second file
    if (!resolve_file(block, filepath2, file_entry_2, file_index_2, parent_2, WRITE, "append"))
        return -1;

    // get the both files related blocks
    const std::vector<uint16_t> file_1_blocks = get_related_blocks(file_entry_1.first_blk);
    std::vector<uint16_t> file_2_blocks = get_related_blocks(file_entry_2.first_blk);

    // calculate final size, final block size and the new required block size
    const int final_size = static_cast<int>(file_entry_1.size + file_entry_2.size);
    const int final_block_size = (final_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int new_blocks_size = final_block_size - static_cast<int>(file_2_blocks.size());

    // if more blocks are needed, allocate new blocks
    if (new_blocks_size > 0)
    {
        const std::vector<uint16_t> new_blocks = find_empty_blocks(new_blocks_size, "append");

        if (new_blocks.empty())
            return -1;

        file_2_blocks.insert(file_2_blocks.end(), new_blocks.begin(), new_blocks.end());
    }
    // Find what blocks to start writing into
    int current_block_index = static_cast<int>(file_entry_2.size) / BLOCK_SIZE;
    int current_write_byte_offset = static_cast<int>(file_entry_2.size) % BLOCK_SIZE;
    // Update the destination file size in the directory entry
    file_entry_2.size = final_size;
    if (!overwrite_dir_entry(block, parent_2, file_entry_2, file_index_2, "append"))
        return -1;

    uint8_t write_block[BLOCK_SIZE];
    // Load the blocks to start writing to
    if (disk.read(file_2_blocks[current_block_index], write_block) != 0)
        return ERROR_R("append", "could not read block " << file_2_blocks[current_block_index]);

    // iterate through all the blocks in the source file
    
    for (const auto read_block : file_1_blocks)
    {
        // read one block from source file
        if (disk.read(read_block, block) != 0)
            return ERROR_R("append", "could not read block " << read_block);

        // copy every byte from source block into destination block
        for (const auto byte : block)
        {
            write_block[current_write_byte_offset++] = byte;
            // If destination block becomes full, flush it and move to next block
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
    // add blocks to fat
    add_blocks_to_fat(file_2_blocks, "append");

    return 0;
}

// mkdir <dirpath> creates a new subdirectory with the name <dirpath>
// in the current directory
int FS::mkdir(const std::string& dirpath)
{   
    // find the parent directory block and get the directory name.
    std::string dir_name;
    const int16_t block_index = walk_path(dirpath, dir_name, "mkdir");
    if (block_index == -1)
        return -1;

        // read the parent directory block
    uint8_t block[BLOCK_SIZE];
    if (disk.read(block_index, block) != 0)
    {
        ERROR("mkdir", "could not read block " << block_index);
        return -1;
    }

    // check if there is an entry that already has the same name.
    dir_entry result_entry{};
    int16_t index;
    if (find_entry(block, block_index, dir_name, result_entry, index, "mkdir", false))
    {
        ERROR("mkdir", "there already exists a file or directory with this name");
        return -1;
    }
    // find a free block 
    const std::vector<uint16_t> result = find_empty_blocks(1, "mkdir");
    if (result.empty())
        return -1;
    // add blocks to fat
    add_blocks_to_fat(result, "mkdir");
    // create the new directory entry
    dir_entry new_entry = {
        .file_name = "",
        .size = 0,
        .first_blk = result[0],
        .type = TYPE_DIR,
        .access_rights = READ | WRITE | EXECUTE
    };
    // copy the directory name into the entry
    std::strncpy(new_entry.file_name, dir_name.c_str(), sizeof(new_entry.file_name) - 1);

    // read the parent directory block 
    if (disk.read(block_index, block) != 0)
    {
        ERROR("mkdir", "could not read block " << block_index);
        return -1;
    }

    // insert the new directory entry into the parent dir block
    if (!add_dir_entry(block, block_index, new_entry, "mkdir"))
        return -1;

    // clear the allocated directory block 
    std::memset(block, 0, sizeof(block));

    // get permissions of parent
    if (!lookup_path("..", result_entry, "mkdir"))
        return -1;
    // create the ".." entry for the new directory
    const dir_entry parent_entry = {
        .file_name = "..",
        .size = 0,
        .first_blk = static_cast<uint16_t>(block_index),
        .type = TYPE_DIR,
        .access_rights = result_entry.access_rights
    };
    // add the ".." into the new directory block
    if (!add_dir_entry(block, result[0], parent_entry, "mkdir"))
        return -1;

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    // normalize folders
    if (dirpath.back() == '/')
        dirpath.pop_back();
    

    dir_entry result{};
    // Get the directory entry
    if (!lookup_path(dirpath, result, "cd"))
        return -1;
    // Check if its a directory and not a file.
    if (result.type == TYPE_FILE)
        return ERROR_R("cd", "cannot cd into a file");

    // Update the current working directory
    current_dir = result;

    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{   
    // Start at the current directory's first block.
    uint16_t current_block = current_dir.first_blk;
    std::string path;

    uint8_t block[BLOCK_SIZE];
    // Walk upwards until the root directory block is reached
    while (current_block != ROOT_BLOCK)
    {   
        // Read current directory block to locate its ".." entry
        if (disk.read(current_block, block) != 0)
            return ERROR_R("pwd", "could not read block " << current_block);

        dir_entry parent_entry{};
        int16_t index;
        // Find the ".." to be able to reach the parent directory block.
        if (!find_entry(block, current_block, "..", parent_entry, index, "pwd", false))
            break; // probably on root block

        uint16_t parent_block = parent_entry.first_blk; 
        // read the parent directory block.
        if (disk.read(parent_block, block) != 0)
            return ERROR_R("pwd", "could not read block " << parent_block);

        dir_entry name_entry{};
        bool found_name = false;
        // Loop through parent directory entries until first_blk matches with current block 
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
        // insert the directory name to path.
        path.insert(0, std::string(name_entry.file_name) + "/");
        // Move one level up.
        current_block = parent_block;
    }

    if (!path.empty())
        path.pop_back();
    path.insert(0, "/");

    std::cout << path << std::endl;

    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(const std::string& access_rights, const std::string& filepath)
{
    int value;
    
    // Convert access rights to an int,
    // Checks if its a number or not and if its too big or not.
    try
    {   
        // Convert string to int
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
    // checks if value is too big
    if (value > (READ | WRITE | EXECUTE))
        return ERROR_R("chmod", "the access rights are too big");

    // Get the directory entry
    uint8_t block[BLOCK_SIZE];
    int16_t index, parent;
    dir_entry entry{};
    std::string name;
    if (!resolve_file(block, filepath, entry, index, parent, 0, "chmod"))
        return -1;

    // Update the access rights for the directory entry.
    entry.access_rights = value;
    // Write the directory entry back into the parent directory block on the disk
    if (!overwrite_dir_entry(block, parent, entry, index, "chmod"))
        return -1;

    return 0;
}
