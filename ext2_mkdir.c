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
#define DISK_BLOCK 128

unsigned char *disk;
struct ext2_inode *ino;
unsigned char block_bitmap[128];
unsigned char inode_bitmap[32];
struct path_lnk* p;

/*
 * A helper function that constructs the local bitmaps given the pointer to disk
 * based on given size and type, constructs the bitmap for block or inode repectively
 */
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
}

int ftree_visit(struct ext2_dir_entry * dir, struct path_lnk* p){
    
       int count = (int)dir->rec_len; 
       int size = ino[dir->inode].i_size;
       while ( count <= size ){
           if (dir->file_type == EXT2_FT_DIR){
               char name[dir->name_len+1];
               memset(name, '\0', dir->name_len+1);
               strncpy(name, dir->name, dir->name_len);
               
               if (strcmp(name,p->name) == 0){
                   if (p->next == NULL){
                       return EEXIST;
                   }
                   return ftree_visit(dir, p->next);
               }
               
               if (count == size)
                   break;
               dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
               count += (int)dir->rec_len;
           }
       }
       //===finished traversing current layer of directory block and does not find target directory
        // if any component in path is not found, return error
       if (p->next != NULL){
           return ENOENT;
       }
       else{
           printf("%s need to be maked\n", p->name);
       }
      
}

/*
 * A helper function that goes through paths from the root directory and returns 
 */
void* walk_path(unsigned char* disk, struct path_lnk* path){
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
    ino = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    struct ext2_dir_entry * root = (struct ext2_dir_entry *)(disk + (1024* ino[1].i_block[0]) );
    int result = ftree_visit(root, path);
    return result;
    
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
void destroy_list(){
    struct path_lnk* cur = p;
    while (cur != NULL){
        struct path_lnk* to_free = cur;
        cur = cur->next;
        free(to_free);
    }
    printf("path link list destroyed\n");
}

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path to directory>\n", argv[0]);
        exit(1);
    }
    char * path = (char*)argv[2];
    if (path[0] != '/'){
        fprintf(stderr, "%s: <absolute path to directory> should include root '/' \n", argv[2]);
        exit(1);
    }
    
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, DISK_BLOCK * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
//    block_bitmap = malloc(sizeof(char)*128);
//    inode_bitmap = malloc(sizeof(char)*32);
    construct_path_linkedlst(path);
    walk_path(disk,p);
//    free();
//    free();
    destroy_list();
    return 0;
}
