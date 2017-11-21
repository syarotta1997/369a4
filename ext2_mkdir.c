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
char block_bitmap[129];
char inode_bitmap[33];

void construct_bitmap(size_t const size, void const * const ptr, char type){
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j,index;
    index = 0;
    for (i=0;i<size/8;i++){
        for (j=0;j<8;j++){
            byte = (b[i] >> j) & 1;
            printf("%u",byte);
            if (type == 'b')
                block_bitmap[index] = (char)byte;
            else if (type == 'i')
                inode_bitmap[index] = (char)byte;
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
        printf("%c ",block_bitmap[i]);
    }
    printf("\n");
    for (int i = 0; i < 32; i++){
        printf("%c ",inode_bitmap[i]);
    }
    printf("\n");
    return 0;
//    struct ext2_inode *ino = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
//    for (int i = 1; i < sb->s_inodes_count ; i++){
//        if ((i == 1 || i > 10))

}

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path to directory>\n", argv[0]);
        exit(1);
    }
    char * path = argv[2];
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, DISK_BLOCK * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    memset(block_bitmap, '\0', 129);
    memset(inode_bitmap, '\0', 33);
    walk_path(disk,path);
    
    return 0;
}
