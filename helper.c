#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"

void construct_bitmap(size_t const size, void const * const ptr, char type){
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j,index;
    index = 0;
    for (i=0;i<size/8;i++){
        for (j=0;j<8;j++){
            byte = (b[i] >> j) & 1;
            if (type == 'b')
                block_bitmap[index] = byte;
            else if (type == 'i')
                inode_bitmap[index] = byte;
            index++;
        }
    }
    printf("\n");
}

void* walk_path(unsigned char* disk, char* path){
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + (1024*2) );
    char * b_bitmap = (char *)disk+(1024 * gd->bg_block_bitmap);
    char * i_bitmap = (char *)disk+(1024 * gd->bg_inode_bitmap);
    construct_bitmap(DISK_BLOCK, b_bitmap, 'b');
    construct_bitmap(sb->s_inodes_count, i_bitmap, 'i');
    for (int i = 0; i < 128; i++){
        printf("%u ",block_bitmap[i]);
    }
    printf("\n");
    for (int i = 0; i < 32; i++){
        printf("%u ",inode_bitmap[i]);
    }
    printf("\n");
    return 0;
    struct ext2_inode *ino = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    for (int i = 1; i < sb->s_inodes_count ; i++){
        if ( (i == 1 || i > 10) && (inode_bitmap[i] & 1)){
            
            
        }
    }
}
/* 
 * A helper function that takes an absolute path as an argument and construct
 * a linked list with each node containing the name of a component between 2 slashes
 * (i.e. file name / directory name). In this way the trailing slashes will be handled and this list
 * will aid in comparing names while traversing inodes in disk image.
 * This function assumes all paths are absolute, which will start with root directory "/"
 * 
 * e.g.          construct_path_linkedlst("/usr/bin/csc369")
 *                '/' -> ''usr   ->  'bin' -> '369'
 */
void construct_path_linkedlst(char* path){
    struct path_lnk* p = malloc(sizeof(struct path_lnk));
    memset(p->name,'\0',256);
    p->name[0] = '/';
    p->next = NULL;
    path = path + 1;
    int count = strlen(path)-1;
    struct path_lnk* cur = p;
    while (count > 0){
        struct path_lnk* new = malloc(sizeof(struct path_lnk));
        memset(new->name,'\0',256);
        char* ptr = strchr(path, '/');
        int index = (int)(ptr - path);
        strncpy(new->name,path,index);
        new->next = NULL;
        cur->next = new;
        cur = cur->next;
        path = path + index + 1;
        count -= strlen(new->name) + 1;
    }
    for (struct path_lnk* i = p; i != NULL; i = i->next){
        printf("%s\n",i->name);
    }
}
/*
 * A function that cleans all malloc-ed struct path lnks in this program
 */
void destroy_list(struct path_lnk* p){
    struct path_lnk* cur = p;
    while (cur != NULL){
        struct path_lnk* to_free = cur;
        cur = cur->next;
        free(to_free);
    }
    printf("path link list destroyed\n");
}
