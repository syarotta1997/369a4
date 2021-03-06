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

#define FREE 0
#define IN_USE 1

#define SUPERBLOCK 2
#define BLOCK_GROUP 3

#define INODE_FLAG 4
#define BLOCK_FLAG 5

//Self defined helper functions and structs
struct path_lnk{
    char name[255];
    struct path_lnk* next;
};
struct single_indirect_block{
    //assuming block size 1024, each block point is 4 bytes (32 bit / 8)
    // then there are 256 pointer space avilable
    int blocks[256];
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
extern char dir_flag;
extern int num_fixed;
//function declarations for util functions
void construct_path_linkedlst(char* path);
void construct_bitmap(size_t const size, void const * const ptr, char type);
void set_bitmap(unsigned char* ptr, int index,char type);
int ftree_visit(struct ext2_dir_entry * dir, unsigned short p_inode ,struct path_lnk* p, char* type);
int allocate_block();
void free_blocks(int inode);
void reallocate_blocks(int inode);
int check_blocks(int inode);
int allocate_inode();
void destroy_list();
void init_inode(unsigned short inode_index, unsigned short size,char type );
//function declarations for main functions
int make_dir(unsigned short inum);
int copy_file(struct stat* stats, unsigned short parent_inode,char* source_path);
int update_dir_entry(unsigned short inum, unsigned short inode_num,char* name, unsigned char type);
int hard_link(unsigned short source_inode,unsigned short parent_inode);
int sym_link(unsigned short parent_inode, char* path);
int remove_file(unsigned short parent_inode, char* f_name);
int restore_file(unsigned short parent_inode, char* f_name);
int check_free();
int check_mode(struct ext2_inode* inode, struct ext2_dir_entry* dir);
int check_inode(unsigned short inode_num);
int check_dtime(unsigned short inode_num);
int check_data(unsigned short inode_num);
void check_all(struct ext2_dir_entry * dir,unsigned short p_inode);
#endif // __HEALPER_H
