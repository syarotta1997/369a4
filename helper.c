#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <time.h>
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
char* new_name;
char dir_flag;

//
//            UTILITY FUNCTIONS
//
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

int ftree_visit(struct ext2_dir_entry * dir, unsigned short p_inode ,struct path_lnk* p, char* type){
    struct ext2_dir_entry * new;
    struct ext2_dir_entry * cur = dir;
    struct path_lnk* new_p;
   
    int count = (int)cur->rec_len; 
    int size = ino_table[cur->inode - 1].i_size;
    int offset = cur->rec_len;
      
    printf("============== layer [ %s ],inode : %d,size : %d\n\n",dir->name,count,size);
    while ( count <= size ){
        char name[cur->name_len+1];
        memset(name, '\0', cur->name_len+1);
        strncpy(name, cur->name, cur->name_len);
        int actual_size = sizeof(struct ext2_dir_entry) + cur->name_len;
        if (actual_size % 4 != 0){
        actual_size =4*(actual_size/4) + 4;
        }
        printf(" %s -- current at %s,  %d    %d   , %d,   count %d\n",dir->name,name,cur->inode,actual_size,cur->rec_len,count);
        //only cares if we can find a match in the file names
        if (strcmp(name,p->name) == 0){
            
            // reached end of path with an existing file, for both mkdir and cp case return EEXIST
            if (cur->file_type == EXT2_FT_REG_FILE || cur->file_type == EXT2_FT_SYMLINK){
                if (p->next != NULL)
                    return -ENOENT;
                if ( strcmp(type,"mkdir")==0 || strcmp(type,"cp")==0 || strcmp(type,"ln_l")==0){
                    printf("%s: Already exists\n", name);
                    return -EEXIST;
                }
                else if (strcmp(type,"ln_s") == 0){
                    return cur->inode;
                }
                else if (strcmp(type,"rm") == 0){
                    new_name = p->name;
                    printf("rm - file name:%s\n",p->name);
                    return p_inode;
                }
                else if (strcmp(type,"restore") == 0){
                    if (inode_bitmap[cur->inode - 1] == 0){
                        int status = check_blocks(cur->inode);
                        if (status == IN_USE){
                            printf("restore found file, but blocks were overwritten\n");
                            return -ENOENT;
                        }
                        else{
                            printf("restore : %s\n",p->name);
                            new_name = p->name;
                            return p_inode;
                        }
                    }
                    else{
                        printf("inode to restore has been overwritten\n");
                        return -ENOENT;
                    }
                }
            }
            // recursively dive deeper for directories until we reach end of path
            else if (cur->file_type == EXT2_FT_DIR){
                //check first to see it it's the end of path and handles cases based on function type
                if (p->next == NULL){
                    if (strcmp(type,"mkdir") == 0){
                        printf("%s: Already exists\n", name);
                        return -EEXIST;
                    }
                    else if (strcmp(type,"cp") == 0 || strcmp(type,"ln_l") == 0){
                        dir_flag = 'd';
                        new_p = malloc(sizeof(struct path_lnk));
                        memset(new_p->name,'\0',256);
                        strcpy(new_p->name,new_name);
                        new_p->next = NULL;
                        p->next = new_p;
                    }
                    else if (strcmp(type,"ln_s") == 0 || strcmp(type,"rm") == 0 || strcmp(type,"restore") == 0){
                        puts("");
                        printf("Directories are not valid inputs for this function\n");
                        return -EISDIR;
                    }
                }
                //deep iteration search: iterate all direct blocks and recursively search for path
                for (int index = 0; index < 13; index++){
                    int block_num = ino_table[cur->inode-1].i_block[index];
                    if ( block_num != 0 ){
                        new = (struct ext2_dir_entry *)(disk + (1024* block_num));
                        return ftree_visit(new, cur->inode,p->next,type);
                    }
                }
            }   
        }
        if ((cur->rec_len != actual_size) && (strcmp(type, "restore") == 0)){
            printf("found possible gap\n");
            offset = actual_size;
        }
        //prevents seg fault at count == size
        if (count == size)
            break;
        cur = (struct ext2_dir_entry *)((char *)cur + offset);
        count += offset;
        offset = cur->rec_len;
        
    }
    //===finished traversing current layer of directory block and does not find target===============
    // Case 1 : Something wrong happened in the middle of given path
    if (p->next != NULL){
        printf("%s: not found\n",p->name);
        return -ENOENT;
    }
    //Case 2: Reached end of path where no target dir / file is found
    else{
        // in mkdir / cp case, has reached end of path and ensured validity to mkdir, return parent's inode
        if ( strcmp(type,"mkdir") == 0 || strcmp(type,"cp") == 0 || strcmp(type,"ln_l") == 0){
            printf("%s need to be maked under parent inode %d \n", p->name, p_inode);
            return p_inode;
        }
        else if (strcmp(type,"ln_s") == 0 || strcmp(type,"rm") == 0 || strcmp(type,"restore") == 0){
            printf("%s source file does not exist %d \n", p->name, p_inode);
            return -ENOENT;
        }
    }
    //technically shouldn't reach here as all type cases are handled above
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

void free_blocks(int inode){
    if ((ino_table+inode-1)->i_block[12] != 0){
        struct single_indirect_block* sib = (struct single_indirect_block*)(disk + (1024* (ino_table+inode-1)->i_block[12]) );
        for (int i = 0 ; i < 256; i ++){
            if (sib->blocks[i] != 0){
                set_bitmap(block_bitmap,sib->blocks[i] - 1,'0');
                sb->s_free_blocks_count++;
                gd->bg_free_blocks_count ++;
            }
            
        }
    }
    for (int i = 0 ; i < 12 ; i ++){
        if ((ino_table+inode-1)->i_block[i] != 0){
            set_bitmap(block_bitmap,(ino_table+inode-1)->i_block[i] - 1,'0');
            sb->s_free_blocks_count++;
            gd->bg_free_blocks_count ++;
        }
    }
}

int check_blocks(int inode){
    if ((ino_table+inode-1)->i_block[12] != 0){
        struct single_indirect_block* sib = (struct single_indirect_block*)(disk + (1024* (ino_table+inode-1)->i_block[12]) );
        for (int i = 0 ; i < 256; i ++){
            if (sib->blocks[i] != 0 && block_bitmap[sib->blocks[i] - 1] == 1)
                    return IN_USE;
        }
    }
    for (int i = 0 ; i < 12 ; i ++){
        int block = (ino_table+inode-1)->i_block[i];
        if ( block != 0 && block_bitmap[block - 1] == 1)
            return IN_USE;
    }
    return FREE;
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
        else if (type == 'l'){
            node->i_links_count = 1;
            node->i_mode = EXT2_S_IFLNK;
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
                        (ino_table+inum-1)->i_size += 1024;
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

//
// MAIN METHOD FUNCTIONS
//
int make_dir(unsigned short inum){
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
    update_dir_entry(inum, inode_index + 1, new_dir, EXT2_FT_DIR);
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
         if (dir_flag == 'd')
             update_dir_entry(parent_inode,inode,new_name,EXT2_FT_REG_FILE);
         else
             update_dir_entry(parent_inode,inode,new_dir,EXT2_FT_REG_FILE);
         fclose(file);
         return 0;
 }

int hard_link(unsigned short source_inode,unsigned short parent_inode){
    printf("Starting hard link process\n");
    if (dir_flag == 'd')
        update_dir_entry(parent_inode,source_inode,new_name,EXT2_FT_SYMLINK);
    else
        update_dir_entry(parent_inode,source_inode,new_dir,EXT2_FT_SYMLINK);
    (ino_table+source_inode-1)->i_links_count++;
    
    return 0;
}

int sym_link(unsigned short parent_inode, char* path){
    printf("Starting sym link process\n");
    int num_blocks,inode;
    int path_len = strlen(path) + 1;
    if (path_len%1024 != 0)
        num_blocks = path_len/4 + 1;
    else
        num_blocks = path_len/4;
    int blocks[num_blocks];
    //Preallocate blocks and inode for copying file
        int i_index = allocate_inode();
        inode = i_index + 1;
        //Filling in all needed info of newly allocated inode
        init_inode(i_index, path_len, 'l');
        struct ext2_inode* node = ino_table + i_index;
        //Allocate all blocks needed and store block number into predefined bookmark array
        int allocated = 0;
        while (allocated < num_blocks){
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
        for (int i = 0 ; i < num_blocks ; i ++){
            printf("[%d] ",blocks[i]);
        }
        char byte;
        int bytes_read = 0;
        int block_count = 0;
        while (bytes_read <= path_len){
            byte = path[bytes_read];
            memcpy( (char*)disk + (1024*blocks[block_count] + bytes_read), &byte, 1);
            bytes_read++;
            if(bytes_read%1024 == 0)
                block_count++;
        }
        
        puts("");
                 printf("finished memory copying with total %d bytes, file size is %d bytes\n",bytes_read,path_len);
         //update parent directory
        if (dir_flag == 'd')
            update_dir_entry(parent_inode,inode,new_name,EXT2_FT_SYMLINK);
        else
            update_dir_entry(parent_inode,inode,new_dir,EXT2_FT_SYMLINK);
        
    return 0;
}

int remove_file(unsigned short parent_inode, char* f_name){
    printf("will perform remove on file:%s\n\n",f_name);
    int block,count;
    struct ext2_dir_entry *dir;
    struct ext2_dir_entry *next;
    for (int i = 0; i < 12; i++){
        block = ino_table[parent_inode - 1].i_block[i];
        dir = (struct ext2_dir_entry *)(disk + (1024* block));
        count = dir->rec_len;
                
        while (count < 1024){
            //handles case where deleting first entry in dir_entry
            {
            if ( strcmp(dir->name,f_name) == 0){
                //handles hard link case
                 //decrease link count by 1,  if reaches 0 after decrement, free inode and block
                // if count == 0, continue, else return
                set_bitmap(inode_bitmap, dir->inode - 1, '0');
                sb->s_free_inodes_count++;
                gd->bg_free_inodes_count++;
                (ino_table + dir->inode - 1)->i_dtime = (int)time(NULL);
                free_blocks(dir->inode);
                dir->inode = 0;
                // the only entry in the block
                if (dir->rec_len == 1024){
                    set_bitmap(block_bitmap, block - 1,'0');
                    sb->s_free_inodes_count++;
                    gd->bg_free_inodes_count++;
                    (ino_table+parent_inode-1)->i_size-= 1024;
                }
                printf("first entry rmed\n");
                return 0;
            }
            }
            //checks the next entry and update reclen if found match
             next = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
             if (strcmp(next->name,f_name) == 0){
                 //handles hard link case
                 //decrease link count by 1,  if reaches 0 after decrement, free inode and block
                 // if count == 0, continue, else return
                 set_bitmap(inode_bitmap, next->inode - 1, '0');
                 sb->s_free_inodes_count++;
                 gd->bg_free_inodes_count++;
                 dir->rec_len += next->rec_len;
                 (ino_table + next->inode - 1)->i_dtime = (int)time(NULL);
                 free_blocks(next->inode);
                 printf("entry rmed\n");
                 return 0;
             }
             if (! (dir->rec_len == 1024)){
                dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                count += (int)dir->rec_len;
             }
        }
    }
    return -EINVAL;
}