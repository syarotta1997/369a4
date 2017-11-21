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
struct ext2_inode *ino_table;
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
    struct ext2_dir_entry * new;
    struct ext2_dir_entry * cur = dir;
    struct ext2_dir_entry * parent;
    int result;
    int count = (int)cur->rec_len; 
    int size = ino_table[cur->inode - 1].i_size;
      
    printf("%d,%d,%s\n",count,size,p->name);
    
    while ( count <= size ){

        if (cur->file_type == EXT2_FT_DIR){
            char name[cur->name_len+1];
            memset(name, '\0', cur->name_len+1);
            strncpy(name, cur->name, cur->name_len);
            printf("%s\n",name);
            if (strcmp(name,p->name) == 0){
                if (p->next == NULL){
                    return EEXIST;
                }
                //iterate all 15 pointers in i_block array and recursively search for path
                for (int index = 0; index < 15; index++){
                    if (ino_table[cur->inode-1].i_block[index] != 0 ){
                        new = (struct ext2_dir_entry *)(disk + (1024* ino_table[cur->inode-1].i_block[index]));
                        result = ftree_visit(new, p->next);
                    }
                }
                return result;
            }   
        }
        if (count == size)
            break;

        cur = (struct ext2_dir_entry *)((char *)cur + (cur->rec_len));
        count += (int)cur->rec_len;
    }
    //===finished traversing current layer of directory block and does not find target directory
    // if any component in path is not found, return error
    if (p->next != NULL){
        printf("%s: not found\n",p->name);
        return ENOENT;
    }
    else{//makes the directory
        printf("%s need to be maked\n", p->name);
            char names[dir->name_len+1];
            memset(names, '\0', dir->name_len+1);
            strncpy(names, dir->name, dir->name_len);
            printf("%s\n",names);
        
    }
}



/*
 * A helper function that goes through paths from the root directory and returns 
 */
void* walk_path(){
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
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    int result;
    for (int i_idx = 0; i_idx < 15; i_idx++){
        if ( ino_table[1].i_block[i_idx] != 0){
            struct ext2_dir_entry * root = (struct ext2_dir_entry *)(disk + (1024* ino_table[1].i_block[i_idx]) );
            result = ftree_visit(root, p->next);
        }
    }
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
    p = malloc(sizeof(struct path_lnk));
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
        int index;
        if (ptr == NULL)
            index = strlen(path);
        else
            index = (int)(ptr - path);
        strncpy(new->name,path,index);
        new->next = NULL;
        cur->next = new;
        cur = cur->next;
        path = path + index + 1;
        count -= strlen(new->name) + 1;
    }
    for (struct path_lnk* i = p; i != NULL; i = i->next){
        if (strcmp(p->name,"/")!=0)
            printf("%s/",i->name);
        else
            printf("%s",i->name);
    }
    puts("");
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
    construct_path_linkedlst(path);
    if ( (strcmp(p->name,"/"))==0 && p->next==NULL){
        printf("%s : %s Root directory cannot be created\n",argv[0],p->name);
        exit(1);
    }
    int result = walk_path();
    if (result == EEXIST){
        printf("%s : Cannot create directory, %s already exists\n",argv[0],path);
        exit(1);
    }
    if (result == ENOENT){
        printf("%s : Invalid path %s\n",argv[0],path);
        exit(1);
    }
    //Free all allocated memories
    struct path_lnk* cur = p;
    while (cur != NULL){
        struct path_lnk* to_free = cur;
        cur = cur->next;
        free(to_free);
    }
    return 0;
}
