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
#define DISK_BLOCK 128

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

int main(int argc, char **argv) {
    int symflag = 0;
    char* source_path = "";
    char* link_path = "";
    //argument validity checks
    if(argc < 4 || argc > 5) {
        fprintf(stderr, "Usage: %s <image file name> [-s] <absolute path of file> <absolute path of link>\n", argv[0]);
        return ENOENT;
    }
    else if (argc == 5){
        if (! strcmp(argv[2],"-s") == 0){
            fprintf(stderr, "Usage: %s <image file name> [-s] <absolute path of file> <absolute path of link>\n", argv[0]);
            return ENOENT;
        }
        else{
            symflag = 1;
            source_path = (char*) (argv[3]);
            link_path = (char*) (argv[4]);
        }
    }
    else{
        source_path = (char*)argv[2];
        link_path = (char*)argv[3];
    }
    //mapping memory onto disk and construct reference data structures
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        return ENOENT;
    }
    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc *)(disk + (1024*2));
    if (gd->bg_free_blocks_count ==0 || gd->bg_free_inodes_count == 0)
        return ENOSPC;
    construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
    construct_bitmap(sb->s_inodes_count, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    
    int root_block, result, source_inode;
    root_block = ino_table[1].i_block[0];
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + (1024* root_block));
    
    if (! symflag){
        construct_path_linkedlst(source_path);
        result = ftree_visit(dir, 2, p->next, "ln_s");
        destroy_list();
        if (result < 0)
            return -result;
        else
            source_inode = result;
    }
    //handles ending slash for link_path
    char * f_name = strrchr(source_path,'/');
    if (f_name == NULL)
         f_name = source_path;
    else
        f_name = f_name + 1;
    if ( strrchr(link_path,'/') == (link_path + strlen(link_path) - 1))
        strcat(link_path,f_name);
    construct_path_linkedlst(link_path);
    new_name = f_name;
    result = ftree_visit(dir, 2, p->next, "ln_l");
    if (result < 0)
        return -result;
    else{
        int stat;
        if (symflag)
            stat = sym_link(result, link_path);
        else
            stat = hard_link(source_inode,result);
        destroy_list();
        if (stat < 0)
            return -stat;
        else
            return 0;
    }
}
