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
extern char dir_flag;

int main(int argc, char **argv) {
    //argument validity checks
    if(argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path of source file> <absolute path in image disk>\n", argv[0]);
        exit(1);
    }
    char * source_path = (char*)argv[2];
    char * target_path = (char*)argv[3];
    //Path validity checks
    if (target_path[0] != '/'){
        fprintf(stderr, "%s: <absolute path in image disk> should include root '/' \n", argv[2]);
        exit(1);
    }
    struct stat stats;
    if (stat( (const char *)source_path, &stats) == -1) {
        perror("stat");
        exit(ENOENT);
    }
    if ( ! S_ISREG(stats.st_mode)){
        fprintf(stderr,"%s: Source needs to be a regular file.\n",source_path);
        exit(ENOENT);
    }
    //mapping memory onto disk and construct reference data structures
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, DISK_BLOCK * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc *)(disk + (1024*2));
    if (gd->bg_free_blocks_count ==0 || gd->bg_free_inodes_count == 0)
        return ENOSPC;
    construct_bitmap(DISK_BLOCK, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
    construct_bitmap(sb->s_inodes_count, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    printf("\n");
    
    //handles ending slash
    if ( strrchr(target_path,'/') == target_path - 1){
        dir_flag = 'd';
        pad_path(source_path,target_path);
    }
    
    char * f_name = strrchr(source_path,'/');
    if (f_name == NULL)
         f_name = source_path;
        
    construct_path_linkedlst(target_path);
    int root_block,result;
    root_block = ino_table[1].i_block[0];
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + (1024* root_block));
    result = ftree_visit(dir, 2, p->next, "cp");
    if (result < 0)
        return -result;
    else{
        copy_file(&stats, result,source_path,f_name);
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
