#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"

extern unsigned char *disk;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;
extern struct ext2_inode *ino_table;
extern unsigned char block_bitmap[128];
extern unsigned char inode_bitmap[32];
extern struct path_lnk* p;
extern char* new_dir;

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

void destroy_list(struct path_lnk* p){
    struct path_lnk* cur = p;
    while (cur != NULL){
        struct path_lnk* to_free = cur;
        cur = cur->next;
        free(to_free);
    }
    printf("path link list destroyed\n");
}

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
                    int block_num = ino_table[cur->inode-1].i_block[index];
                    if ( block_num != 0 ){
                        new = (struct ext2_dir_entry *)(disk + (1024* block_num));
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

int allocate_block(int inode_idx){
        for(int block = 0; block < 128; block++){
            if (! block_bitmap[block] & 1){
                printf("will allocate block #%d\n",block+1);
                set_bitmap((char *)disk+(1024 * gd->bg_block_bitmap),block,'1');
                construct_bitmap(DISK_BLOCK, (char *)disk+(1024 * gd->bg_block_bitmap), 'b');
                for (int i = 0; i < 13 ; i ++){
                    if ( (ino_table+inode_idx)->i_block[i] == 0){
                        (ino_table+inode_idx)->i_block[i] = block+1;
                        return block+1;
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
            inode_num = i + 1;
            allocate_block(inode_num - 1);
            node = ino_table + i;
            printf("will allocate inode #%d\n",inode_num);
            set_bitmap((char *)disk+(1024 * gd->bg_inode_bitmap),i,'1');
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
            dir = (struct ext2_dir_entry *)(disk + (1024* (node->i_block[0])));
            dir->file_type = EXT2_FT_DIR;
            dir->inode = inode_num;
            strcnpy(dir->name,".",1);
            dir->name_len = 1;
            dir->rec_len = sizeof(struct ext2_dir_entry) + dir->name_len;
            if (dir->rec_len % 4 != 0){
                dir->rec_len = 4*(dir->rec_len / 4) + 4;
            }
            count = dir->rec_len;
            dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
            dir->file_type = EXT2_FT_DIR;
            dir->inode = inum;
            strncpy(dir->name,"..",2);
            dir->name_len = 2;
            dir->rec_len = 1024 - count;       
            if (dir->rec_len % 4 != 0){
                dir->rec_len =4*(dir->rec_len / 4) + 4;
            }
            printf("DONE init new dir entry\n");
            break;
        }
    }
    // Updating parent directory entry
    ino_table[inum-1].i_links_count++;
    int new_size = sizeof(struct ext2_dir_entry) + strlen(name);
            if (new_size % 4 != 0){
                new_size =4*(new_size) + 4;
            }
    for (int i = 12 ; i > 0 ; i -- ){
        if (ino_table[inum-1].i_block[i-1] != 0){
            int dir_block_num = ino_table[inum-1].i_block[i-1];
            printf("locate parent dir block num at %d\n",dir_block_num);
            dir = (struct ext2_dir_entry *)(disk + (1024* (dir_block_num)) );
            count = dir->rec_len;
            printf("begin %d with rec_len %d \n",dir->inode,count);
            
            
            while (count <= 1024){
                //reached at end pointer of this current block
                if (count == 1024){
                    
                    size = sizeof(struct ext2_dir_entry)+dir->name_len;
                    
                        if (size % 4 != 0){
                            size =4*(size / 4) + 4;
                        }
                    printf("reached end block inode %d,size %d,new size %d\n",dir->inode,size,new_size);
                    count = dir->rec_len - size;
                    //no space, need to allocate new block
                    if ( count - new_size < 0){
                        printf("allocate needed\n");
                        //allocate new block
                        block_num = allocate_block(inum - 1);
                        dir =(struct ext2_dir_entry *)(disk + (1024* (block_num)) );
                        dir->file_type = EXT2_FT_DIR;
                        dir->inode = inode_num;
                        dir->name_len = strlen(name);
                        strcnpy(dir->name,name,dir->name_len);
                        
                        dir->rec_len = 1024;       
                    }
                    //there is space in this dir_block, add the new directory to it
                    else{
                        //changing current pointer from end of file to the new dir
                        printf("size%d\n",size);
                        dir->rec_len = size;
                        dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                        dir->file_type = EXT2_FT_DIR;
                        dir->inode = inode_num;
                        dir->name_len = strlen(name);
                        strncpy(dir->name,name,dir->name_len);
                        
                        dir->rec_len = count;       
                        if (dir->rec_len % 4 != 0){
                            dir->rec_len =4*(dir->rec_len / 4) + 4;
                        }
                        
                    }
                    //done updating, no point in looping
                    printf("done updating parent dir, added inode %s,%d\n",dir->name,dir->rec_len);
                    break;
                }
                dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                count += (int)dir->rec_len;
                printf("end ,count now is %d\n",count);
            }
        }
    }
    return 0;
}
