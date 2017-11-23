#ifndef __HEALPER_H__
#define __HEALPER_H__

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

//Self defined helper functions and structs
struct path_lnk{
    char name[255];
    struct path_lnk* next;
};
// extern variables
extern unsigned char *disk;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;
extern struct ext2_inode *ino_table;
extern unsigned char block_bitmap[128];
extern unsigned char inode_bitmap[32];
extern struct path_lnk* p;
extern char* new_dir;
//function declarations
void construct_path_linkedlst(char* path);
void construct_bitmap(size_t const size, void const * const ptr, char type);
void set_bitmap(unsigned char* ptr, int index,char type);
int ftree_visit(struct ext2_dir_entry * dir, unsigned short p_inode ,struct path_lnk* p, char type);
int allocate_block(int inode_idx);
int make_dir(unsigned short inum, char* name);
void destroy_list();
#endif // __SIM_H 
