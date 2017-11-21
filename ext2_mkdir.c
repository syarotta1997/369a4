#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"
#define DISK_BLOCK 128

unsigned char *disk;
unsigned char block_bitmap[128];
unsigned char inode_bitmap[32];
struct path_lnk* p;

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
        printf("%s\n",new->name);
        new->next = NULL;
        cur->next = new;
        cur = cur->next;
        path = path + index + 1;
        count -= strlen(new->name) + 1;
    }
    
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
    walk_path(disk,path);
//    free();
//    free();
    return 0;
}
