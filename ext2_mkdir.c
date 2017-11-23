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

int main(int argc, char **argv) {
    //argument validity checks
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path to directory>\n", argv[0]);
        exit(1);
    }
    
    char * path = (char*)argv[2];
    
    if (path[0] != '/'){
        fprintf(stderr, "%s: <absolute path to directory> should include root '/' \n", argv[2]);
        exit(1);
    }
    //mapping memory onto disk and construct reference data structures
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, DISK_BLOCK * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    construct_path_linkedlst(path);
    if ( (strcmp(p->name,"/"))==0 && p->next==NULL){
        printf("%s : %s Root directory cannot be created\n",argv[0],p->name);
        exit(1);
    }
    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc *)(disk + (1024*2) );
    construct_bitmap(DISK_BLOCK, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
    construct_bitmap(sb->s_inodes_count, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
    printf("\n");
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    int result;
    for (int i = 0;i<128;i++){
        printf("%u ",block_bitmap[i]);
    }
    for (int i_idx = 0; i_idx < 15; i_idx++){
        int block_num = ino_table[1].i_block[i_idx];
        if (  block_num != 0){
            printf("root block %d\n",block_num);
            struct ext2_dir_entry * root = (struct ext2_dir_entry *)(disk + (1024* (block_num))) ;
            result = ftree_visit(root, 2 ,p->next,"mkdir");
        }
    }
    if (result == -EEXIST){
        printf("%s : Cannot create directory, %s already exists\n",argv[0],path);
        exit(1);
    }
    else if (result == -ENOENT){
        printf("%s : Invalid path %s\n",argv[0],path);
        exit(1);
    }
    //no error given, return is the parent directory i_node of dir to make
    else{
        printf("now calling make_dir\n");
        make_dir(result, new_dir);
    }
    printf("=================================================================\n");
        for (int i = 0; i < 32 ; i++){
        if ( (i == 1 || i > 10) && inode_bitmap[i] & 1){
            char type;
            if (S_ISREG(ino_table[i].i_mode))
                type = 'f';
            else if (S_ISDIR(ino_table[i].i_mode))
                type = 'd';
            else if (S_ISLNK(ino_table[i].i_mode))
                type = 'l';
            printf("[%d] type: %c size: %d links: %d blocks: %d\n", i+1, type, ino_table[i].i_size,ino_table[i].i_links_count, ino_table[i].i_blocks);
            printf("[%d] Blocks: ", i+1);
            for (int j = 0 ; j < 12 ; j++){
               if (ino_table[i].i_block[j] != 0)
                   printf("%d ",ino_table[i].i_block[j]);
            }
            puts("");
        }
    }
        printf ("\nDirectory Blocks:\n");
    for (int i = 0; i < 32 ; i++){
        if ( (i == 1 || i > 10) && (inode_bitmap[i] & 1) && S_ISDIR(ino_table[i].i_mode)){
                for (int j = 0 ; j < 12 ; j++){
                   if (ino_table[i].i_block[j] != 0){
                       printf("   DIR BLOCK NUM: %d (for inode %d)\n", ino_table[i].i_block[j], i+1);
                       struct ext2_dir_entry * dir = (struct ext2_dir_entry *)(disk + (1024* ino_table[i].i_block[j]) );
                       int count = (int)dir->rec_len; 
                       while ( count <= ino_table[i].i_size ){
                           char type;
                           char names[dir->name_len+1];
                           memset(names, '\0', dir->name_len+1);
                           strncpy(names, dir->name, dir->name_len);
                           if (dir->file_type == EXT2_FT_REG_FILE)
                               type = 'f';
                           else if (dir->file_type == EXT2_FT_DIR)
                               type = 'd';
                           else if (dir->file_type == EXT2_FT_SYMLINK)
                               type = 'l';
                               printf("Inode: %d rec_len: %d name_len: %d type= %c name= %s \n", dir->inode,dir->rec_len,dir->name_len,type,names);                         
                               if (count == ino_table[i].i_size)
                                   break;
                               dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                                   count += (int)dir->rec_len;
                       }
                   }
                }
        }
    
}
    
    
    
    
    
    
    
        puts("");
    
    
    
    destroy_list();
    return 0;
}
