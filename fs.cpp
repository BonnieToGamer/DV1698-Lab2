#include <iostream>
#include <cstring>
#include <vector>
#include "fs.h"



FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";

    int result = disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat));

    if (result == -1) return;

    uint8_t block[BLOCK_SIZE];
    result = disk.read(ROOT_BLOCK, block);

    if (result == -1) return;

    memcpy(&current_dir, block, sizeof(dir_entry));

    current_dir.first_blk = ROOT_BLOCK;
    current_dir.type = TYPE_DIR;
    strcpy(current_dir.file_name, "/");
    current_dir.size = 0;
    current_dir.access_rights = READ | WRITE;
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    std::cout << "FS::format()\n";

    uint8_t temp_arr[BLOCK_SIZE] = {}; // zero-initialize it

    // dir that points to previous block (directory)
    constexpr dir_entry dir {
        .file_name = "..",
        .size = 0,
        .first_blk = ROOT_BLOCK,
        .type = TYPE_DIR,
        .access_rights = READ | WRITE
    };

    // copy to temp_arr
    memcpy(temp_arr, &dir, sizeof(dir_entry));
    
    disk.write(ROOT_BLOCK, temp_arr);

    memset(fat, 0, sizeof(fat));
    fat[ROOT_BLOCK] = EOF;
    fat[FAT_BLOCK]  = EOF;
    
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(&fat));

    current_dir = dir;

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout << "FS::ls()\n";
    uint8_t block[BLOCK_SIZE];

    if (disk.read(current_dir.first_blk, block) != 0) {
        std::cout << "Error reading dir block" << std::endl;
        return -1;
    }

    dir_entry* entries = reinterpret_cast<dir_entry*>(block);
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    std::cout << "DEBUG: Checking " << max_entries << " entries in block " 
              << current_dir.first_blk << std::endl;

    bool found_entries = false;
    int count = 0;

    for (int i = 0; i < max_entries; i++) {
        dir_entry& entry = entries[i];

        if (entry.file_name[0] == '\0') {
            continue;
        }

        std::cout << "DEBUG: Found entry " << i << ": " << entry.file_name 
                  << " type: " << (int)entry.type << std::endl;

        if (strcmp(entry.file_name, "..") == 0 && current_dir.first_blk == ROOT_BLOCK) {
            continue;
        }

        if (!found_entries) {
            std::cout << "name\tsize\n";
            found_entries = true;
        }

        std::cout << entry.file_name << "\t" << entry.size << std::endl;
        count++;
    }

    std::cout << "DEBUG: Total files shown: " << count << std::endl;

    if (!found_entries) {
        std::cout << "Directory is empty" << std::endl;
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    
    /*
    ======= LÄS NUVARANDE KATALOG BLOCK =======

    ifall current_dir.first_blk inte är block 0, root dir, så kommer read misslyckas för vi kommer inte lyckas hitta filen
    */
    uint8_t block[BLOCK_SIZE];
    if (disk.read(current_dir.first_blk, block) != 0) {
        std::cout << "Error reading dir\n";
        return -1;
    }

    //gör om blocket till en array ac directory entries
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

    if (file.type != TYPE_FILE) {
        std::cout << "cant remove directories";
        return -1;
    }

    /*
    ====== FRIGÖR BLOCK UTEFTER FAT KEDJAN =======
    */

    int block2 = file.first_blk;

    //ifall block2 == FAT_EOF så har de ingen fil data, då kan vi hoppa över block rensningen
    while (block2 != FAT_EOF)
    {
        int next_block = fat[block2];

        //nollar blockets data, på disken genom att skriva över det
        uint8_t empty[BLOCK_SIZE] = {};
        disk.write(block2, empty);

        //här markerar vi fat som free, enligt instruktionerna
        fat[block2] = FAT_FREE;
        
        //gå till nästa block i kedjan
        block2 = next_block;
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
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
