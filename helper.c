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

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
struct ext2_inode *ino_table;
unsigned char block_bitmap[128];
unsigned char inode_bitmap[32];
struct path_lnk* p;
char* new_dir;
char dir_flag;

/*
 * Utility functions
 */
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
    struct path_lnk* new = NULL;
    struct path_lnk* cur = NULL;
    int count, index;
   
    p = malloc(sizeof(struct path_lnk));
    memset(p->name,'\0',256);
    p->name[0] = '/';
    p->next = NULL;
    path = path + 1;
    count = strlen(path);
    cur = p;
    while (count > 0){
        new = malloc(sizeof(struct path_lnk));
        memset(new->name,'\0',256);
        char* ptr = strchr(path, '/');
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
    if (new == NULL)
        new_dir = "/";
    else
        new_dir = new->name;
    for (struct path_lnk* i = p; i != NULL; i = i->next){
        printf("%s ",i->name);
    }
    puts("");
}

void destroy_list(){
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

void set_bitmap(unsigned char* ptr, int index,char type){
    unsigned char *b = ptr;
    int i, j;
    i = index / 8;
    j = index % 8;
    if (type == '1')
        *(b + i) = *(b+i) | (1 << j );
    else if(type == '0')
        *(b+i) = *(b+i) & ~(1 << j);
}

int chk_source_path(char* source_path, char* target_path){
    char * f_name = strrchr(source_path,'/');
    char new[255];
    strcpy(new,target_path);

    if (strrchr(new,'/') - new == strlen(new) - 1){
        if (f_name == NULL)
            strcat(new,source_path);
        else
            strcat(new,f_name+1);
    }
    else{
        if (f_name == NULL){
            strcat(new,"/");
            strcat(new,source_path);
        }
        else
            strcat(new,f_name);
    }
    construct_path_linkedlst(new);
    int root_block = ino_table[1].i_block[0];
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + (1024* root_block));
    int check = ftree_visit(dir, 2, p->next, "cp");
    destroy_list();
    if (! (check == -EEXIST))
        return 0;
    else
        return -EEXIST;
}

int ftree_visit(struct ext2_dir_entry * dir, unsigned short p_inode ,struct path_lnk* p, char* type){
    struct ext2_dir_entry * new;
    struct ext2_dir_entry * cur = dir;
    
    int count = (int)cur->rec_len; 
    int size = ino_table[cur->inode - 1].i_size;
      
    printf("layer %d,%d,%s\n",count,size,p->name);
    while ( count <= size ){
        char name[cur->name_len+1];
        memset(name, '\0', cur->name_len+1);
        strncpy(name, cur->name, cur->name_len);
        printf("%s,%d\n",name,count);
        //only cares if we can find a match in the file names
        if (strcmp(name,p->name) == 0){
            
            // reached end of path with an existing file, for both mkdir and cp case return EEXIST
            if (cur->file_type == EXT2_FT_REG_FILE || cur->file_type == EXT2_FT_SYMLINK){
                if (p->next != NULL)
                    return -ENOENT;
                if ( strcmp(type,"mkdir")==0 || strcmp(type,"cp")==0 || strcmp(type,"ln_l")==0){
                    fprintf(stderr,"%s: Already exists\n", name);
                    return -EEXIST;
                }
                else if (strcmp(type,"ln_s")==0){
                    return cur->inode;
                }
            }
            // recursively dive deeper for directories until we reach end of path
            else if (cur->file_type == EXT2_FT_DIR){
                if (p->next == NULL){
                    if (strcmp(type,"mkdir") == 0){
                        return -EEXIST;
                    }
                    else if (strcmp(type,"cp") == 0 || strcmp(type,"ln_l") == 0){
                        dir_flag = 'd';
                        return cur->inode;
                    }
                    else if (strcmp(type,"ln_s") == 0){
                        printf("ln: hard link refering to a dir\n");
                        return -EISDIR;
                    }
                }
                //iterate all 15 pointers in i_block array and recursively search for path
                for (int index = 0; index < 13; index++){
                    int block_num = ino_table[cur->inode-1].i_block[index];
                    if ( block_num != 0 ){
                        new = (struct ext2_dir_entry *)(disk + (1024* block_num));
                        return ftree_visit(new, cur->inode,p->next,type);
                    }
                }
            }   
        }
        if (count == size)
            break;
        cur = (struct ext2_dir_entry *)((char *)cur + (cur->rec_len));
        count += (int)cur->rec_len;
    }
    //===finished traversing current layer of directory block and does not find target===============
    // if any component in path (excluding last file) is not found, return error
    if (p->next != NULL){
        printf("%s: not found\n",p->name);
        return -ENOENT;
    }
    //if p.next is null, meaning we reached end of path where no target dir / file is found
    else{
        // in mkdir / cp case, has reached end of path and ensured validity to mkdir, return parent's inode
        if ( strcmp(type,"mkdir")==0 || strcmp(type,"cp")==0 || strcmp(type,"ln_l")==0){
            printf("%s need to be maked under parent inode %d \n", p->name, p_inode);
            new_dir = p->name;
            return p_inode;
        }
        else if (strcmp(type,"ln_s")==0){
            printf("%s source file does not exist %d \n", p->name, p_inode);
            return -ENOENT;
        }
    }
    return -EINVAL;
}

int allocate_block(){
        for(int block = 0; block < 128; block++){
            if (! block_bitmap[block] & 1){
                set_bitmap(disk+(1024 * gd->bg_block_bitmap),block,'1');
                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                gd->bg_free_blocks_count --;
                sb->s_free_blocks_count --;
                return block + 1;
            }
        }
        printf("oops,all blocks seems to be occupied\n");
        return -ENOSPC;
}

int allocate_inode(){
        for(int i = 0; i < 32; i++){
            if (! inode_bitmap[i] & 1){
                gd->bg_free_inodes_count --;
                sb->s_free_inodes_count --;
                printf("will allocate inode #%d\n",i+1);
                set_bitmap(disk+(1024 * gd->bg_inode_bitmap),i,'1');
                construct_bitmap(32, (char *)disk+(1024 * gd->bg_inode_bitmap), 'i');
                return i;
            }
        }
        printf("oops,all blocks seems to be occupied\n");
        return -ENOSPC;
}

void init_inode(unsigned short inode_index, unsigned short size,char type ){
        struct ext2_inode* node = ino_table + inode_index;
        if ( size % 512 != 0 )
            node->i_blocks = size/512 + 1;
        else
            node->i_blocks = size/512;
        memset(node->i_block, 0 , 15);
        node->i_file_acl = 0;
        node->i_dir_acl = 0;
        node->i_faddr = 0;
        node->i_uid = 0;
        node->i_gid = 0;
        node->osd1 = 0;
        node->i_generation = 0;
        node->i_size = size;
        node->i_dtime = 0;
        if (type == 'd'){
            node->i_links_count = 2;
            node->i_mode = EXT2_S_IFDIR;
        }
        else if (type == 'f'){
            node->i_links_count = 1;
            node->i_mode = EXT2_S_IFREG;
        }
        printf("done initializing inode\n");
}

void update_dir_entry(unsigned short inum, unsigned short inode_num,char* name, unsigned char type){
    int count,size;
    struct ext2_dir_entry * dir;
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
                        int block_num = allocate_block();
                        (ino_table+inum-1)->i_block[i] = block_num;
                        dir =(struct ext2_dir_entry *)(disk + (1024* (block_num)) );
                        dir->file_type = type;
                        dir->inode = inode_num;
                        dir->name_len = strlen(name);
                        strncpy(dir->name,name,dir->name_len);
                        
                        dir->rec_len = 1024;       
                    }
                    //there is space in this dir_block, add the new directory to it
                    else{
                        //changing current pointer from end of file to the new dir
                        printf("size%d\n",size);
                        dir->rec_len = size;
                        dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                        dir->file_type = type;
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
                    return;
                }
                dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                count += (int)dir->rec_len;
                printf("end ,count now is %d\n",count);
            }
        }
    }
}

int make_dir(unsigned short inum, char* name){
    struct ext2_dir_entry * dir;
    int count,inode_index,block_num;
    // Allocating and writing to new inode section and new directory entry
    inode_index = allocate_inode();
    init_inode(inode_index, 1024, 'd');
    struct ext2_inode* node = ino_table + inode_index;
    block_num = allocate_block( inode_index );
    //for this assignment we don't need to handle indirect blocks for directories
    for (int idx = 0; idx < 12 ; idx ++){
        if ( node->i_block[idx] == 0){
            node->i_block[idx] = block_num;
            printf("Assigned block #%d to node->i_block.\n",block_num);
            break;
        } 
    }
    //Allocate empty directory entry and writes to it with current dir and parent dir entries
    //cur dir
    dir = (struct ext2_dir_entry *)(disk + (1024* (node->i_block[0])));
    dir->file_type = EXT2_FT_DIR;
    dir->inode = inode_index + 1;
    strncpy(dir->name,".",1);
    dir->name_len = 1;
    dir->rec_len = sizeof(struct ext2_dir_entry) + dir->name_len;
    if (dir->rec_len % 4 != 0){
        dir->rec_len = 4*(dir->rec_len / 4) + 4;
    }
    count = dir->rec_len;
    //parent dir
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

    // Updating parent directory entry (note will make this another helper function)
    ino_table[inum-1].i_links_count++;
    gd->bg_used_dirs_count+=1;
    update_dir_entry(inum, inode_index + 1, name, EXT2_FT_DIR);
    return 0;
}

int copy_file(struct stat* stats, unsigned short parent_inode,char* source_path){
         int fsize = (int)stats->st_size;
         int total_blocks,inode;
         if (fsize % 1024 != 0)
             total_blocks = fsize/1024 + 1;
         else
             total_blocks = fsize/1024;
         if (total_blocks > 12 )
             total_blocks ++;
         int blocks[total_blocks];
         
         printf("total blocks needed for this file:%d\n",total_blocks);
         
         //Preallocate blocks and inode for copying file
        int i_index = allocate_inode();
        inode = i_index + 1;
        //Filling in all needed info of newly allocated inode
        init_inode(i_index, fsize, 'f');
        struct ext2_inode* node = ino_table + i_index;
        //Allocate all blocks needed and store block number into predefined bookmark array
        int allocated = 0;
        while (allocated < total_blocks){
            int block_num = allocate_block(i_index);
            //assigns block number to direct data blocks
            if (allocated < 12){
                node->i_block[allocated] = block_num;        
            }
            //needs to allocate single indirect block and assigns to it
            else{
                struct single_indirect_block* sib;
                if (allocated == 12){
                    node->i_block[allocated] = block_num;
                    sib = (struct single_indirect_block*)(disk + (1024* (block_num)) );
                    sib->blocks[0] = block_num;
                }
                else{
                    int index = allocated % 12;
                    sib->blocks[index] = block_num;
                }
                printf("block allocating with single indirect done\n");
            }
            blocks[allocated] = block_num;
            allocated++;
        }
        printf("done allocating all blocks needed to copy file:\n");
        for (int i = 0 ; i < total_blocks ; i ++){
            printf("[%d] ",blocks[i]);
        }
        puts("");

         //Perform raw data copying using fopen / fread
         FILE *file = fopen( (const char*)source_path, "rb");
         if (file == NULL){
             perror("fopen");
             return -EINVAL;
         }
         fseek(file, 0 , SEEK_SET);
         unsigned char buffer[1024] = { 0 };
         int block_count = 0;
         size_t b_read;
         int total_read = 0;
         //start reading from source file one block at a time and copy to disk image
         while ( (b_read = fread(buffer, 1, 1024, file)) > 0){
             if (block_count == total_blocks)
                 printf("block_count reached total blocks, index error\n");
             total_read += (int)b_read;
             memcpy( (disk + 1024*blocks[block_count]), buffer, 1024);
             memset(buffer, 0, 1024);
             block_count ++;
         }
         printf("finished memory copying with total %d bytes, file size is %d bytes\n",total_read,fsize);
         //update parent directory
         char * f_name = strrchr(source_path,'/');
         if (f_name == NULL)
             f_name = source_path;
         if (dir_flag == 'd')
             update_dir_entry(parent_inode,inode,f_name,EXT2_FT_REG_FILE);
         else
             update_dir_entry(parent_inode,inode,new_dir,EXT2_FT_REG_FILE);
         fclose(file);
         return 0;
 }

int hard_link(unsigned short source_inode,unsigned short parent_inode,char* link_name){
    printf("Starting hard link process\n");
    if (dir_flag == 'd')
        update_dir_entry(parent_inode,source_inode,link_name,EXT2_FT_SYMLINK);
    else
        update_dir_entry(parent_inode,source_inode,new_dir,EXT2_FT_SYMLINK);
    return 0;
}

int sym_link(unsigned short parent_inode, char* path,char* link_name){
    printf("Starting sym link process\n");
    return 0;
}