#include <iostream>
#include <cstring>
#include <vector>
#include "fs.h"

int FS::create_navigation_folders(const uint16_t current_block, const uint16_t previous_block, uint8_t block[])
{
    // dir that points to current block (directory)
    const dir_entry current_entry{
        .file_name = ".",
        .size = 0,
        .first_blk = current_block,
        .type = TYPE_DIR,
        .access_rights = READ | WRITE | EXECUTE
    };


    // dir that points to previous block (directory)
    const dir_entry previous_entry{
        .file_name = "..",
        .size = 0,
        .first_blk = previous_block,
        .type = TYPE_DIR,
        .access_rights = READ | WRITE | EXECUTE
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
    if (padding <= string.length())
    {
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

    disk.write(ROOT_BLOCK, temp_arr);

    memset(fat, FAT_FREE, sizeof(fat));
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    write_fat_to_disk();

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(const std::string& filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";

    if (filepath.size() >= 56)
    {
        std::cout << "[FS::create] Error: file name too long\n";
        return -1;
    }

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
        size += static_cast<int>(input.size()) + 1; // +1 for newlines

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

    auto try_flush_block = [&]()
    {
        if (byte_offset == BLOCK_SIZE)
        {
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

    block[byte_offset++] = '\0';
    try_flush_block();

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

    if (disk.read(current_dir.first_blk, block) != 0)
    {
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
        std::cout << "[FS::cat] Error: no file with that name\n";
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

    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::ls] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    //för test 5 la vi till type utskrift med

    std::cout << "name\tsize\ttype\n";

    for (int i = 0; i < BLOCK_SIZE; i += sizeof(dir_entry))
    {
        const auto* entry = reinterpret_cast<dir_entry*>(&block[i]);

        // empty file descriptor
        if (entry->file_name[0] == '\0' || std::string(entry->file_name) == ".." || std::string(entry->file_name) == ".")
            continue;

        std::cout << entry->file_name << "\t " << entry->size << "\t " << (entry->type == TYPE_DIR ? "dir" : "file") << "\n";
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

    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::mv] Error: cannot read current dir block" << std::endl;
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    int source_id = -1;
    bool does_destination_exist = false;
    dir_entry source_entry;

    for (int i = 0; i < max_entries; i++)
    {
        if (entries[i].file_name[0] == '\0')
        {
            continue;
        }

        if (strcmp(entries[i].file_name, sourcepath.c_str()) == 0)
        {
            source_id = i;
            memcpy(&source_entry, &entries[i], sizeof(dir_entry));

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
        }

        if (strcmp(entries[i].file_name, destpath.c_str()) == 0)
        {
            does_destination_exist = true;
        }
    }

    if (does_destination_exist)
    {
        std::cout << "[FS::mv] Error: Destination file " << destpath << " already exists " << std::endl;
        return -1;
    }

    if (destpath.length() >= 56)
    {
        std::cout << "Error: Destination filename too long" << std::endl;
        return -1;
    }

    uint32_t old_size = entries[source_id].size;
    uint16_t old_first_blk = entries[source_id].first_blk;
    uint8_t old_access_rigths = entries[source_id].access_rights;

    memset(entries[source_id].file_name, 0, sizeof(entries[source_id].file_name));

    strncpy(entries[source_id].file_name, destpath.c_str(), sizeof(entries[source_id].file_name) - 1);

    entries[source_id].size = old_size;
    entries[source_id].first_blk = old_first_blk;
    entries[source_id].access_rights = old_access_rigths;
    entries[source_id].type = TYPE_FILE;

    if (disk.write(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::mv] Error: Could not write dir to disk" << std::endl;
        return -1;
    }

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";

    /*
    ======= LÄS NUVARANDE KATALOG BLOCK =======

    ifall current_dir.first_blk inte är block 0, root dir, så kommer read misslyckas för vi kommer inte lyckas hitta filen
    */
    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0)
    {
        std::cout << "[FS::rm] Error: could not read directory\n";
        return -1;
    }

    //gör om blocket till en array av directory entries
    dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    /*
    ====== LETA UPP ÖNSKAD FIL =======

    alla dir_entry som är tomma har file_name[0] == 0
    vilket innebär att dom är lediga / tomma
    här hoppar vi över tomma filer
    */

    int entry_id = -1;
    for (int i = 0; i < max_entries; i++)
    {
        //när filnamnet matchar exakt den önskade filen så tar vi denns id plats och slutar leta
        if (strcmp(entries[i].file_name, filepath.c_str()) == 0)
        {
            //enkel dubbelkoll så man faktiskt raderar en fil, osäker hur nödvändig för själva uppgiften
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

    //detta är bara en if ifall vi inte hittar önskad fil
    if (entry_id == -1)
    {
        std::cout << "couldnt find:" << filepath << "\n";
        return -1;
    }

    /*
    ====== CHECKA FIL TYP =======

    här vill vi kolla så vi nollar en fil, inte ett directory
    så TYPE_FILE är ok, medans TYPE_DIR inte är ok
    det hade förstört filsystemet
    */

    dir_entry& file = entries[entry_id];

    if (file.type != TYPE_FILE)
    {
        std::cout << "cant remove directories";
        return -1;
    }

    /*
    ====== FRIGÖR BLOCK UTEFTER FAT KEDJAN =======
    */

    uint16_t current_block = file.first_blk;

    //ifall block2 == FAT_EOF så har de ingen fil data, då kan vi hoppa över block rensningen
    while (current_block != FAT_EOF)
    {
        if (current_block >= BLOCK_SIZE / 2)
        {
            std::cout << "[FS::rm] Error: Invalid FAT entry " << current_block << std::endl;
            break;
        }

        uint16_t next_block = fat[current_block];

        //nollar blockets data, på disken genom att skriva över det
        uint8_t empty[BLOCK_SIZE] = {0};
        disk.write(current_block, empty);

        //här markerar vi fat som free, enligt instruktionerna
        fat[current_block] = FAT_FREE;

        //gå till nästa block i kedjan
        current_block = next_block;
    }

    //nollar dir_entryn, file_name[0] = '\0', = 0
    memset(&entries[entry_id], 0, sizeof(dir_entry));

    disk.write(current_dir.first_blk, block);

    //skriver tillbaka fat till disken
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

    if (filepath1.empty() || filepath2.empty()) return -1;

    uint8_t dir_block[BLOCK_SIZE] = {};
    disk.read(ROOT_BLOCK, dir_block);

    auto* dirEntries = reinterpret_cast<dir_entry*>(dir_block);
    const int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    dir_entry* file_entry1 = nullptr;
    dir_entry* file_entry2 = nullptr;

    for (int i = 0; i < max_entries; ++i)
    {
        if (dirEntries[i].file_name[0] == '\0')
            continue;

        if (std::string(dirEntries[i].file_name) == filepath1)
        {
            file_entry1 = &dirEntries[i];
        }

        else if (std::string(dirEntries[i].file_name) == filepath2)
        {
            file_entry2 = &dirEntries[i];
        }

        if (file_entry1 && file_entry2) break;
    }

    if (!file_entry1 || !file_entry2) return -1;

    if (file_entry1->type != TYPE_FILE || file_entry2->type != TYPE_FILE) return -1;


    uint8_t fatbuf[BLOCK_SIZE];
    if (disk.read(FAT_BLOCK, fatbuf) == -1) return -1;

    std::memcpy(fat, fatbuf, sizeof(fat));

    uint16_t last_blk2 = file_entry2->first_blk;
    uint32_t size2 = file_entry2->size;

    while (fat[last_blk2] != FAT_EOF)
    {
        last_blk2 = fat[last_blk2];
    }

    uint32_t lastblk_offset = size2 % BLOCK_SIZE;

    uint8_t buf2[BLOCK_SIZE];

    //If last block is full find a free block.
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
        // Last block is partially filled, preserve the data
        if (disk.read(last_blk2, buf2) == -1)
            return -1;
    }

    // Get the filepath1 data
    uint16_t blk1 = file_entry1->first_blk;
    uint32_t bytes_left1 = file_entry1->size;

    uint8_t buf1[BLOCK_SIZE];

    // write the data blocks to the end of filepath2 until no data left.
    while (bytes_left1 > 0)
    {
        if (disk.read(blk1, buf1) == -1)
            return -1;

        uint32_t bytes_from_this_block = std::min<uint32_t>(bytes_left1, BLOCK_SIZE);
        uint32_t src_offset = 0;

        while (bytes_from_this_block > 0)
        {
            // if current block is full, write it to disk, find a free block and chain it in FAT
            // and continue writing into the new free block.
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

            std::memcpy(buf2 + lastblk_offset,
                        buf1 + src_offset,
                        chunk);

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

    // Write the updated directory to root
    if (disk.write(ROOT_BLOCK, dir_block) == -1)
        return -1;

    // write updated FAT
    write_fat_to_disk();

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";

    if (dirpath.length() >= 56)
    {
        std::cout << "[FS::mkdir] Error: Directory name too long" << std::endl;
        return -1;
    }

    if (dirpath == ".." || dirpath == ".")
    {
        std::cout << "[FS::mkdir] Error: Can not create dir with reserved name: " << dirpath << std::endl;
        return -1;
    }

    uint8_t dir_block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, dir_block) != 0)
    {
        std::cout << "[FS::mkdir] Error: Can not read current dir" << std::endl;
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(dir_block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    int free_space = -1;
    for (int i = 0; i < max_entries; i++)
    {
        if (entries[i].file_name[0] == '\0' && free_space == -1)
        {
            free_space = i;
        }
        else if (entries[i].file_name[0] != '\0' && strcmp(entries[i].file_name, dirpath.c_str()) == 0)
        {
            std::cout << "[FS::mkdir] Error: directory or file: " << dirpath << " already exists" << std::endl;
            return -1;
        }
    }

    if (free_space == -1)
    {
        std::cout << "[FS::mkdir] Error: director is full" << std::endl;
        return -1;
    }

    int16_t new_dir_block = -1;
    for (int i = 0; i < BLOCK_SIZE / 2; i++)
    {
        if (fat[i] == FAT_FREE && i != ROOT_BLOCK && i != FAT_BLOCK)
        {
            new_dir_block = i;
            break;
        }
    }

    if (new_dir_block == -1)
    {
        std::cout << "Error: No free blocks for new directory" << std::endl;
        return -1;
    }

    uint8_t new_dir_data[BLOCK_SIZE] = {0};

    create_navigation_folders(new_dir_block, current_dir.first_blk, new_dir_data);

    disk.write(new_dir_block, new_dir_data);

    fat[new_dir_block] = FAT_EOF;
    write_fat_to_disk();

    dir_entry new_dir_entry;
    strcpy(new_dir_entry.file_name, dirpath.c_str());
    new_dir_entry.size = 0;
    new_dir_entry.first_blk = new_dir_block;
    new_dir_entry.type = TYPE_DIR;
    new_dir_entry.access_rights = READ | WRITE | EXECUTE;

    memcpy(&entries[free_space], &new_dir_entry, sizeof(dir_entry));

    disk.write(current_dir.first_blk, dir_block);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";

    //"cd .."!
    if (dirpath == "..")
    {
        //if sats för ifall vi är root gör nada
        if (current_dir.first_blk == ROOT_BLOCK)
        {
            return 0;
        }

        uint8_t current_dir_block[BLOCK_SIZE];
        if (disk.read(current_dir.first_blk, current_dir_block) != 0)
        {
            std::cout << "[FS::cd] Error: Can not read current dir" << std::endl;
            return -1;
        }

        //ba kollar så .. finns
        dir_entry* entries = reinterpret_cast<dir_entry*>(current_dir_block);
        if (entries[0].file_name[0] == '\0' || strcmp(entries[0].file_name, "..") != 0)
        {
            std::cout << "[FS::cd] Error: Invalid directory struct as .. wasnt found" << std::endl;
            return -1;
        }

        //hämtar parent dir block
        uint16_t parent_block = entries[0].first_blk;

        //dubbelkolla om parent ärr roooooot!
        if (parent_block == ROOT_BLOCK)
        {
            current_dir.first_blk = ROOT_BLOCK;
            current_dir.type == TYPE_DIR;
            strcpy(current_dir.file_name, "unknown");
            current_dir.size = 0;
            current_dir.access_rights = READ | WRITE | EXECUTE;
        }

        return 0;
    }

    //cd till specifik directory
    uint8_t current_dir_block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, current_dir_block) != 0)
    {
        std::cout << "[FS::cd] Error: Can not read current directory" << std::endl;
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(current_dir_block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    bool found = false;
    dir_entry targeted_directory;

    for (int i = 0; i < max_entries; i++)
    {
        if (entries[i].file_name[0] = '\0' && strcmp(entries[i].file_name, dirpath.c_str()) == 0)
        {
            //kontroll för katalog
            if (entries[i].type != TYPE_DIR)
            {
                std::cout << "[FS::cd] Error: " << dirpath << " is not a directory" << std::endl;
                return -1;
            }

            if (!(entries[i].access_rights & EXECUTE))
            {
                std::cout << "[FS::cd] Error: no exe perm for this dir: " << dirpath << std::endl;
                return -1;
            }

            targeted_directory = entries[i];
            found = true;
            break;
        }
    }

    if (!found)
    {
        std::cout << "[FS::cd] Error dir: " << dirpath << " not found" << std::endl;
        return -1;
    }

    current_dir = targeted_directory;

    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << "FS::pwd()\n";

    //för root
    if (current_dir.first_blk == ROOT_BLOCK)
    {
        std::cout << "/" << std::endl;
        return 0;
    }

    //följ .. kedjan för o kunna bygga full path
    std::vector<std::string> path_parts;
    uint16_t current_block = current_dir.first_blk;

    path_parts.push_back(current_dir.file_name);

    //backar i .. tills vi når root
    while (current_block != ROOT_BLOCK)
    {
        uint8_t dir_block[BLOCK_SIZE];
        if (disk.read(current_block, dir_block) != 0)
        {
            std::cout << "[FS::pwd] Error: Cannot read dir block" << current_block << std::endl;
            return -1;
        }

        //first entry är ..
        dir_entry* entries = reinterpret_cast<dir_entry*>(dir_block);
        if (entries[0].file_name[0] == '\0' || strcmp(entries[0].file_name, "..") != 0)
        {
            std::cout << "[FS::pwd] Error: Cannot read dir struct" << std::endl;
            return -1;
        }

        //gå till parent
        current_block = entries[0].first_blk;

        //om inte root, hitta dir namn
        if (current_block != ROOT_BLOCK)
        {
            uint8_t parent_block[BLOCK_SIZE];
            if (disk.read(current_block, parent_block) != 0)
            {
                std::cout << "[FS::pwd] Error: Cannot read parent dir" << std::endl;
                return -1;
            }

            dir_entry* parent_entries = reinterpret_cast<dir_entry*>(parent_block);
            int max_entries = BLOCK_SIZE / sizeof(dir_entry);

            for (int i = 0; i < max_entries; i++)
            {
                if (parent_entries[i].file_name[0] != '\0' && parent_entries[i].first_blk == current_block)
                {
                    path_parts.push_back(parent_entries[i].file_name);
                    break;
                }
            }
        }
    }

    //bygger pathen
    std::cout << '/';
    for (int i = path_parts.size() - 1; i >= 0; i--)
    {
        if (i != path_parts.size() - 1)
        {
            std::cout << "/";
        }
        std::cout << path_parts[i];
    }
    std::cout << std::endl;

    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";

    if (filepath.empty()) return -1;

    // Read the disk
    uint8_t block[BLOCK_SIZE] = {};
    disk.read(ROOT_BLOCK, block);

    //get all the directories
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);
    const int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    // Find the correct file
    dir_entry* file_entry = nullptr;
    for (int i = 0; i < max_entries; i++)
    {
        if (std::string(dirEntries[i].file_name) == filepath)
        {
            file_entry = &dirEntries[i];
        }
    }

    if (file_entry == nullptr)
    {
        std::cout << "not found" << std::endl;
        return -1;
    }


    uint8_t rights = 0;


    // if it is a accessrights is a number char
    // Check if the bit value creates a 1 or a 0,
    // and enable the access rights that is a nonzero value. 
    if (isdigit(stoi(accessrights)))
    {
        int num = accessrights[0] - '0'; // converts char to int
        if (num & 4) rights |= READ;
        if (num & 2) rights |= WRITE;
        if (num & 1) rights |= EXECUTE;
    }
    else
    {
        // get the correct bit from the string "accessrights";
        for (char c : accessrights)
        {
            if (c == 'r') rights |= READ;
            if (c == 'w') rights |= WRITE;
            if (c == 'x') rights |= EXECUTE;
        }
    }

    // change the access_rights
    file_entry->access_rights = rights;

    // write back to the disk.
    disk.write(ROOT_BLOCK, block);

    return 0;
}
