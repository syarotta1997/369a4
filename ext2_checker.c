#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "helper.h"

extern unsigned char *disk;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;
extern struct ext2_inode *ino_table;
extern unsigned char block_bitmap[128];
extern unsigned char inode_bitmap[32];
extern struct path_lnk* p;
extern char* new_dir;
extern char* new_name;
extern char dir_flag;
extern int num_fixed;

int main(int argc, char **argv) {
    //argument validity checks
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        return ENOENT;
    }
    system("clear");
    //mapping memory onto disk and construct reference data structures
    {
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        return ENOENT;
    }
    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc *)(disk + (1024*2) );
    construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
    construct_bitmap(sb->s_inodes_count, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    }
    int root_block = ino_table[1].i_block[0];
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + (1024* root_block));
    num_fixed = 0;
    num_fixed += check_free();
    check_all(dir, 2);
    
    if (num_fixed > 0)
        printf("%d file system inconsistencies repaired!\n",num_fixed);
    else
        printf("No file system inconsistencies detected!");

    return 0;
}
