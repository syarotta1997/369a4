#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include <string.h>
unsigned char *disk;


void printbitmap(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=0;i<size/8;i++)
    {
        for (j=0;j<8;j++)
        {
            byte = (b[i] >> j) & 1;
            printf("%u", byte);
        }
        printf(" ");
    }
    printf("\n");
}

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);
    // Group descriptor locates at block 2 of the disk
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + (1024*2) );
    printf("Block group:\n");
    printf("    block bitmap: %d\n",gd->bg_block_bitmap );
    printf("    inode bitmap: %d\n", gd->bg_inode_bitmap );
    printf("    inode table: %d\n",gd->bg_inode_table);
    printf("    free blocks: %d\n",gd->bg_free_blocks_count);
    printf("    free inodes: %d\n",gd->bg_free_inodes_count);
    printf("    used_dirs: %d\n",gd->bg_used_dirs_count);
    char * b_bitmap = (char *)disk+(1024 * gd->bg_block_bitmap);
    printf ("Block bitmap: ");
    printbitmap(sb->s_blocks_count, b_bitmap);
    char * i_bitmap = (char *)disk+(1024 * gd->bg_inode_bitmap);
    printf ("Inode bitmap: ");
    printbitmap(sb->s_inodes_count, i_bitmap);
    printf ("\n");
    struct ext2_inode *ino = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    for (int i = 0; i < sb->s_inodes_count ; i++){
        if ( (i == 1 || i > 10) && ino[i].i_size > 0){
            char type;
            if (S_ISREG(ino[i].i_mode))
                type = 'f';
            else if (S_ISDIR(ino[i].i_mode))
                type = 'd';
            else if (S_ISLNK(ino[i].i_mode))
                type = 'l';
            printf("[%d] type: %c size: %d links: %d blocks: %d\n", i+1, type, ino[i].i_size,ino[i].i_links_count, ino[i].i_blocks);
            printf("[%d] Blocks: ", i+1);
            for (int j = 0 ; j < 12 ; j++){
               if (ino[i].i_block[j] != 0)
                   printf("%d ",ino[i].i_block[j]);
            }
            puts("");
        }
    }
    printf ("\nDirectory Blocks:\n");
    for (int i = 0; i < sb->s_inodes_count ; i++){
        if ( (i == 1 || i > 10) && ino[i].i_size > 0 && S_ISDIR(ino[i].i_mode)){
                for (int j = 0 ; j < 12 ; j++){
                   if (ino[i].i_block[j] != 0){
                       printf("   DIR BLOCK NUM: %d (for inode %d)\n", ino[i].i_block[j], i+1);
                       struct ext2_dir_entry * dir = (struct ext2_dir_entry *)(disk + (1024* ino[i].i_block[j]) );
                       int count = (int)dir->rec_len; 
                       while ( count <= ino[i].i_size ){
                           char type;
                           char name[dir->name_len+1];
                           memset(name, '\0', dir->name_len+1);
                           strncpy(name, dir->name, dir->name_len);
                           if (dir->file_type == EXT2_FT_REG_FILE)
                               type = 'f';
                           else if (dir->file_type == EXT2_FT_DIR)
                               type = 'd';
                           else if (dir->file_type == EXT2_FT_SYMLINK)
                               type = 'l';
                               printf("Inode: %d rec_len: %d name_len: %d type= %c name= %s \n", dir->inode,dir->rec_len,dir->name_len,type,name);                         
                               if (count == ino[i].i_size)
                                   break;
                               dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                                   count += (int)dir->rec_len;
                       }
                   }
                }
        }
    
}
    puts("");
    return 0;
}
