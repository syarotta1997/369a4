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
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
struct ext2_inode *ino_table;
unsigned char block_bitmap[128];
unsigned char inode_bitmap[32];
struct path_lnk* p;
char* new_dir;

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

void set_bitmap(void const * const ptr, int index,char type){
    unsigned char *b = (unsigned char*) ptr;
    int i, j;
    i = index / 8;
    j = index % 8;
    if (type == '1')
        *(b + i) = *(b+i) | (1 << j );
    else if(type == '0')
        *(b+i) = *(b+i) & ~(1 << j);
}

int ftree_visit(struct ext2_dir_entry * dir, unsigned short p_inode ,struct path_lnk* p){
    struct ext2_dir_entry * new;
    struct ext2_dir_entry * cur = dir;
    
    int result;
    int count = (int)cur->rec_len; 
    int size = ino_table[cur->inode - 1].i_size;
      
    printf("%d,%d,%s\n",count,size,p->name);
    
    while ( count <= size ){

        if (cur->file_type == EXT2_FT_DIR){
            char name[cur->name_len+1];
            memset(name, '\0', cur->name_len+1);
            strncpy(name, cur->name, cur->name_len);
            printf("%s,%d\n",name,count);
            if (strcmp(name,p->name) == 0){
                if (p->next == NULL){
                    return -EEXIST;
                }
                //iterate all 15 pointers in i_block array and recursively search for path
                for (int index = 0; index < 15; index++){
                    if (ino_table[cur->inode-1].i_block[index] != 0 ){
                        new = (struct ext2_dir_entry *)(disk + (1024* ino_table[cur->inode-1].i_block[index]));
                        return ftree_visit(new, cur->inode,p->next);
                    }
                }
                
                
            }   
        }
        printf("current count:%d total size:%d\n",count, size);
        if (count == size)
            break;

        cur = (struct ext2_dir_entry *)((char *)cur + (cur->rec_len));
        count += (int)cur->rec_len;
    }
    //===finished traversing current layer of directory block and does not find target directory
    // if any component in path is not found, return error
    if (p->next != NULL){
        printf("%s: not found\n",p->name);
        return -ENOENT;
    }
    
    else{//makes the directory
        printf("%s need to be maked\n", p->name);
        printf("%d \n",p_inode);
        return p_inode;
    }
}

int allocate_block(int inode_num){
        for(int block = 0; block < 128; block++){
            if (! block_bitmap[block] & 1){
                printf("will allocate block #%d\n",block);
                set_bitmap((char *)disk+(1024 * gd->bg_block_bitmap),block,'1');
                construct_bitmap(DISK_BLOCK, (char *)disk+(1024 * gd->bg_block_bitmap), 'b');
                    for (int i = 0; i < 128; i++){
                        printf("%u ",block_bitmap[i]);
                    }
                set_bitmap((char *)disk+(1024 * gd->bg_block_bitmap),block,'0');
                construct_bitmap(DISK_BLOCK, (char *)disk+(1024 * gd->bg_block_bitmap), 'b');
                for (int i = 0; i < 13 ; i ++){
                    if ( (ino_table+inode_num)->i_block[i] == 0){
                        (ino_table+inode_num)->i_block[i] = block;
                        return block;
                    } 
                }

            }
        }
        printf("oops,all blocks seems to be occupied\n");
        return EINVAL;
}

int make_dir(unsigned short inum, char* name){
    struct ext2_dir_entry * dir;
    struct ext2_inode* node;
    int count,size,inode_num,block_num;

    // Allocating and writing to new inode section and new directory entry
    for (int i = 11 ; i < 32 ; i ++){
        if (! inode_bitmap[i] & 1){
            inode_num = i;
            block_num = allocate_block(inode_num);
            node = ino_table + i;
            
             printf("will allocate inode #%d\n",i+1);
             set_bitmap((char *)disk+(1024 * gd->bg_inode_bitmap),i,'1');
            construct_bitmap(32, (char *)disk+(1024 * gd->bg_inode_bitmap), 'i');
                for (int i = 0; i < 32;i++){
                    printf("%u ",inode_bitmap[i]);
                }
            set_bitmap((char *)disk+(1024 *  gd->bg_inode_bitmap),i,'0');
            construct_bitmap(32, (char *)disk+(1024 * gd->bg_inode_bitmap), 'i');      
            node->i_blocks = 2;
            node->i_file_acl = 0;
            node->i_dir_acl = 0;
            node->i_faddr = 0;
            node->i_uid = 0;
            node->i_gid = 0;
            node->osd1 = 0;
            node->i_generation = 0;
            node->i_size = 1024;
            node->i_links_count = 2;
            node->i_mode = EXT2_S_IFDIR;
            printf("done initializing inode\n");
            //Allocate empty directory and writes to it with current dir and parent dir entries
            dir = (struct ext2_dir_entry *)(disk + (1024* node->i_block[0]) );
            dir->file_type = EXT2_FT_DIR;
            dir->inode = inode_num;
            strcpy(dir->name,".");
            dir->name_len = 1;
            dir->rec_len = sizeof(struct ext2_dir_entry) + dir->name_len;
            if (dir->rec_len % 4 != 0){
                dir->rec_len = 4*(dir->rec_len / 4) + 4;
            }
            dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
            dir->file_type = EXT2_FT_DIR;
            dir->inode = inum;
            strcpy(dir->name,"..");
            dir->name_len = 2;
            dir->rec_len = 1024 - (sizeof(struct ext2_dir_entry) + dir->name_len);       
            if (dir->rec_len % 4 != 0){
                dir->rec_len =4*(dir->rec_len / 4) + 4;
            }
            printf("DONE init new dir entry\n");
            break;
        }
    }
    // Updating parent directory entry
    int new_size = sizeof(struct ext2_dir_entry) + strlen(name);
            if (new_size % 4 != 0){
                new_size =4*(new_size) + 4;
            }
    for (int i = 12 ; i > 0 ; i -- ){
        if (ino_table[inum-1].i_block[i-1] != 0){
            int dir_block_num = ino_table[inum-1].i_block[i-1];
            dir = (struct ext2_dir_entry *)(disk + (1024* dir_block_num) );
            count = dir->rec_len;
            
            while (count < 1024){
                if (count + (int)dir->rec_len == 1024){
                    size = sizeof(struct ext2_dir_entry)+dir->name_len;
                        if (size % 4 != 0){
                            size =4*(size / 4) + 4;
                        }
                    if (count + size + new_size > 1024){
                        //allocate new block
                    }
                    //there is space in this dir_block, add the new directory to it
                    else{
                        //changing current pointer from end of file to the new dir
                        dir->rec_len = size;
                        dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                        dir->file_type = EXT2_FT_DIR;
                        dir->inode = inode_num;
                        strcpy(dir->name,name);
                        dir->name_len = strlen(name);
                        dir->rec_len = count + size;       
                        if (dir->rec_len % 4 != 0){
                            dir->rec_len =4*(dir->rec_len / 4) + 4;
                        }
                        
                    }
                    //done updating, no point in looping
                    break;
                }
                dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                    count += (int)dir->rec_len;
                    
            }
        }
    }
    //updating links in directory blocks
        for (int i = 14; i >= 0; i --){
            if ( ino_table[inum-1].i_block[i] != 0){
                
//                dir->file_type = EXT2_FT_DIR;
            }
        }
    return 0;
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
        if ( (count - strlen(new->name) + 1) <= 0)
            new_dir = new->name;
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
    printf("new to be make:%s\n",new_dir);
}

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
    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    gd = (struct ext2_group_desc *)(disk + (1024*2) );
    construct_bitmap(DISK_BLOCK, (char *)disk+(1024 * gd->bg_block_bitmap), 'b');
    construct_bitmap(sb->s_inodes_count, (char *)disk+(1024 * gd->bg_inode_bitmap), 'i');
    for (int i = 0; i < 128; i++){
        printf("%u ",block_bitmap[i]);
    }
//    printf("\n");
//    for (int i = 0; i < 32; i++){
//        printf("%u ",inode_bitmap[i]);
//    }
    printf("\n");
    ino_table = (struct ext2_inode *)(disk + 1024*(gd->bg_inode_table));
    int result;
    for (int i_idx = 0; i_idx < 15; i_idx++){
        if ( ino_table[1].i_block[i_idx] != 0){
            struct ext2_dir_entry * root = (struct ext2_dir_entry *)(disk + (1024* ino_table[1].i_block[i_idx]) );
            result = ftree_visit(root, 2 ,p->next);
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
        make_dir(result, new_dir);
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
