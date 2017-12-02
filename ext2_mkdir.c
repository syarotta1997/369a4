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

int main(int argc, char **argv) {
    //argument validity checks
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path to directory>\n", argv[0]);
        return ENOENT;
    }
    //mapping memory onto disk and construct reference data structures
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        return ENOENT;
    }
    //Constructs all data structures for reference and return error if no space
    char * path = (char*)argv[2];
    construct_path_linkedlst(path);
    if ( (strcmp(p->name,"/"))==0 && p->next==NULL){
        fprintf(stderr,"%s : %s Root directory cannot be created\n",argv[0],p->name);
        return ENOENT;
    }
    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc *)(disk + (1024*2) );
    if (gd->bg_free_blocks_count ==0 || gd->bg_free_inodes_count == 0)
        return ENOSPC;
    construct_bitmap( 128 , (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
    construct_bitmap(sb->s_inodes_count, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    //construct root directory for file walk and perform make dir operation
    int root_block,result;
    root_block = ino_table[1].i_block[0];
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + (1024* root_block));
    result = ftree_visit(dir, 2, p->next, "mkdir");
    if (result < 0)
        return -result;
    else{
        int stat = make_dir(result);
        destroy_list();
        if (stat < 0)
            return -stat;
        else
            return 0;
    }
}
