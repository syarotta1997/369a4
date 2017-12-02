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
int num_fixed;

/*===============================================
 *                                      Utility Functions
 ================================================*/
//Constructs a linked list of file names given a path
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
//A function that frees the path linked list
void destroy_list(){
    struct path_lnk* cur = p;
    while (cur != NULL){
        struct path_lnk* to_free = cur;
        cur = cur->next;
        free(to_free);
    }
    printf("path link list destroyed\n");
}
//Updates the bitmap based on disk memory
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
//Set specific bit on the disk memory
void set_bitmap(unsigned char* ptr, int index,char type){
    unsigned char *b = ptr;
    int i, j;
    i = index / 8;
    j = index % 8;
    if (type == '1')
        *(b + i) = *(b+i) | (1 << j );
    else if(type == '0')
        *(b+i) = *(b+i) ^ (1 << j);
}
//The major function that looks for a specific file given the path and returns either an inode number
//based on the type of requesting function, or a negative errno macro
int ftree_visit(struct ext2_dir_entry * dir, unsigned short p_inode ,struct path_lnk* p, char* type){
    struct ext2_dir_entry * new;
    struct ext2_dir_entry * cur = dir;
    struct path_lnk* new_p;
    //skips invalid inodes 
    if (cur->inode == 0)
        cur = (struct ext2_dir_entry *)((char *)cur + cur->rec_len);
    int count = (int)cur->rec_len; 
    int size = ino_table[cur->inode - 1].i_size;
    int offset = cur->rec_len;   
    printf("============== layer [ %s ],inode : %d,size : %d\n\n",dir->name,count,size);
    while ( count <= size ){
        char name[cur->name_len+1];
        memset(name, '\0', cur->name_len+1);
        strncpy(name, cur->name, cur->name_len);
        int actual_size = sizeof(struct ext2_dir_entry) + cur->name_len;
        if (actual_size % 4 != 0)
            actual_size =4*(actual_size/4) + 4;
        printf(" %s -- current at %s,  %d    %d   , %d,   count %d\n",dir->name,name,cur->inode,actual_size,cur->rec_len,count);
        //only cares if we can find a match in the file names
        if (strcmp(name,p->name) == 0){
            // reached end of path with an existing file, for both mkdir and cp case return EEXIST
            if (cur->file_type == EXT2_FT_REG_FILE || cur->file_type == EXT2_FT_SYMLINK){
                //reached a file with path not finished-> the given path is invalid
                if (p->next != NULL)
                    return -ENOENT;
                if ( strcmp(type,"mkdir")==0 || strcmp(type,"cp")==0 || strcmp(type,"ln_l")==0)
                    return -EEXIST;
                else if (strcmp(type,"ln_s") == 0)
                    return cur->inode;
                else if (strcmp(type,"rm") == 0){
                    new_name = p->name;
                    return p_inode;
                }
                //in restore case, first check if inode is valid, then checks the inode's blocks
                else if (strcmp(type,"restore") == 0){
                    if (inode_bitmap[cur->inode - 1] == 0){
                        int status = check_blocks(cur->inode);
                        if (status == IN_USE)
                            return -ENOENT;
                        else{
                            new_name = p->name;
                            return p_inode;
                        }
                    }
                    else{
                        return -ENOENT;
                    }
                }
            }      
            // recursively dive deeper for directories until we reach end of path
            //we are also putting a restriction not to dive back to parent directory or current directory
            else if ( (cur->file_type == EXT2_FT_DIR) && (cur->inode != dir->inode) && (cur->inode != p_inode)){
                //check first to see it it's the end of path and handles cases based on function type
                if (p->next == NULL){
                    if (strcmp(type,"mkdir") == 0)
                        return -EEXIST;
                    else if (strcmp(type,"cp") == 0 || strcmp(type,"ln_l") == 0){
                        //indicating we are making a new file under this directory
                        //but also have to forward-check if the file exists, so make a new component in path link list
                        //and continue checking
                        dir_flag = 'd';
                        new_p = malloc(sizeof(struct path_lnk));
                        memset(new_p->name,'\0',256);
                        strcpy(new_p->name,new_name);
                        new_p->next = NULL;
                        p->next = new_p;
                    }
                    else if (strcmp(type,"ln_s") == 0 || strcmp(type,"rm") == 0 || strcmp(type,"restore") == 0)
                        return -EISDIR;
                }
                //deep iteration search: iterate all direct blocks and recursively search for path
                int result;
                for (int index = 0; index < 13; index++){
                    int block_num = ino_table[cur->inode-1].i_block[index];
                    if ( block_num != 0 ){
                        new = (struct ext2_dir_entry *)(disk + (1024* block_num));
                        result =  ftree_visit(new, cur->inode,p->next,type);
                        if (result > 0 )
                            return result;
                    }
                }
                return result;
            }   
        }
        //for restore case, we are also checking the gaps between directory entries
        if ((cur->rec_len != actual_size) && (strcmp(type, "restore") == 0)){
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
    if (p->next != NULL)
        return -ENOENT;
    //Case 2: Reached end of path where no target dir / file is found
    else{
        // file to be maked is not found in disk, can continue making it 
        if ( strcmp(type,"mkdir") == 0 || strcmp(type,"cp") == 0 || strcmp(type,"ln_l") == 0)
            return p_inode;
        // file to be linked or removed or restored not found in disk
        else if (strcmp(type,"ln_s") == 0 || strcmp(type,"rm") == 0 || strcmp(type,"restore") == 0)
            return -ENOENT;
    }
    //technically shouldn't reach here as all type cases are handled above
    return -EINVAL;
}
//A functions that traverses all valid directory entries and checks for status of each file/link/directory
void check_all(struct ext2_dir_entry * dir, unsigned short p_inode){
    struct ext2_dir_entry * new;
    struct ext2_dir_entry * cur = dir;
    struct ext2_inode* cur_inode;
    if (cur->inode == 0)
        cur = (struct ext2_dir_entry *)((char *)cur + cur->rec_len);
    int count = cur->rec_len; 
    printf("============== layer [ %d ]==p+inode%d==============\n\n",dir->inode,p_inode);
    while ( count <= 1024 ){
            printf(" -- current at inode[%d]  rec_len: %d  %s\n",cur->inode,cur->rec_len,cur->name);
            cur_inode = (struct ext2_inode*) (ino_table+cur->inode-1);       
            num_fixed += check_mode(cur_inode, cur);
            num_fixed += check_inode(cur->inode);
            num_fixed += check_dtime(cur->inode);
            num_fixed += check_data(cur->inode);
            // recursively dive deeper for directories until we reach end of path
            if ( (cur->file_type == EXT2_FT_DIR) && (cur->inode != dir->inode) && (cur->inode != p_inode) ){
                char name[cur->name_len+1];
                memset(name,'\0',cur->name_len);
                strncpy(name,cur->name,cur->name_len);
                    //deep iteration search: iterate all direct blocks and recursively search for path
                    for (int index = 0; index < 13; index++){
                        int block_num = ino_table[cur->inode-1].i_block[index];
                        if ( block_num != 0 ){
                            new = (struct ext2_dir_entry *)(disk + (1024* block_num));
                            if (new->inode != 0)
                                check_all(new,dir->inode);
                        }
                    } 
            }   
            if (count == 1024)
                break;
        //prevents seg fault at count == size
        cur = (struct ext2_dir_entry *)((char *)cur + cur->rec_len);
        count += cur->rec_len;
    }
}
//Allocates a block and sets the bit in the disk img, return error if no space is available
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
        return -ENOSPC;
}
//Free all data blocks of an inode but unsetting bitmap and updates super block, group block free block counts
void free_blocks(int inode){
    //handles single indirect block if there is any
    if ((ino_table+inode-1)->i_block[12] != 0){
        struct single_indirect_block* sib = (struct single_indirect_block*)(disk + (1024* (ino_table+inode-1)->i_block[12]) );
        for (int i = 0 ; i < 256; i ++){
            if (sib->blocks[i] != 0){
                set_bitmap(disk+(1024 * gd->bg_block_bitmap),sib->blocks[i] - 1,'0');
                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                sb->s_free_blocks_count++;
                gd->bg_free_blocks_count ++;
            }
        }
    }
    for (int i = 0 ; i < 12 ; i ++){
        if ((ino_table+inode-1)->i_block[i] != 0){
                set_bitmap(disk+(1024 * gd->bg_block_bitmap),(ino_table+inode-1)->i_block[i] - 1,'0');
                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
            sb->s_free_blocks_count++;
            gd->bg_free_blocks_count ++;
        }
    }
}
//Reallocates all previously allocated blocks for an inode and updates data fields
void reallocate_blocks(int inode){
        if ((ino_table+inode-1)->i_block[12] != 0){
        struct single_indirect_block* sib = (struct single_indirect_block*)(disk + (1024* (ino_table+inode-1)->i_block[12]) );
        for (int i = 0 ; i < 256; i ++){
            if (sib->blocks[i] != 0){
                set_bitmap(disk+(1024 * gd->bg_block_bitmap),sib->blocks[i] - 1,'1');
                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                sb->s_free_blocks_count--;
                gd->bg_free_blocks_count --;
            }
            
        }
    }
    for (int i = 0 ; i < 12 ; i ++){
        if ((ino_table+inode-1)->i_block[i] != 0){
                set_bitmap(disk+(1024 * gd->bg_block_bitmap),(ino_table+inode-1)->i_block[i] - 1,'1');
                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count --;
        }
    }
}
//Checks if an inode's data blocks are all in use or free
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
//Allocates an free inode and sets bits, return error if no available inode is present
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
        return -ENOSPC;
}
//Initialize inode fields based on given type
void init_inode(unsigned short inode_index, unsigned short size,char type ){
        struct ext2_inode* node = ino_table + inode_index;
        if ( size % 512 != 0 )
            node->i_blocks = size/512 + 1;
        else
            node->i_blocks = size/512;
        memset(node->i_block, 0 , 15*sizeof(int));
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
}
//updates the new directory entry based on given arguments and sets necessary fields
int update_dir_entry(unsigned short inum, unsigned short inode_num,char* name, unsigned char type){
    int count,size;
    struct ext2_dir_entry * dir;
    int new_size = sizeof(struct ext2_dir_entry) + strlen(name);
    if (new_size % 4 != 0)
        new_size =4*(new_size) + 4;
    //we are only writing to the end block of the last free directory entry block
    for (int i = 12 ; i > 0 ; i -- ){
        if (ino_table[inum-1].i_block[i-1] != 0){
            int dir_block_num = ino_table[inum-1].i_block[i-1];
            dir = (struct ext2_dir_entry *)(disk + (1024* (dir_block_num)) );
            count = dir->rec_len;
            while (count <= 1024){
                //reached at end pointer of this current block
                if (count == 1024){       
                    size = sizeof(struct ext2_dir_entry)+dir->name_len;
                    if (size % 4 != 0)
                        size =4*(size / 4) + 4;
                    count = dir->rec_len - size;
                    //no space, need to allocate new block then updates directory
                    if ( count - new_size < 0){
                        //allocate new block and increase directory size
                        int block_num = allocate_block();
                        if (block_num < 0)
                            return block_num;
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
                        dir->rec_len = size;
                        dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                        dir->file_type = type;
                        dir->inode = inode_num;
                        dir->name_len = strlen(name);
                        strncpy(dir->name,name,dir->name_len);
                        dir->rec_len = count;       
                        if (dir->rec_len % 4 != 0)
                            dir->rec_len =4*(dir->rec_len / 4) + 4;
                    }
                    return 0;
                }
                dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                count += (int)dir->rec_len;
                printf("end ,count now is %d\n",count);
            }
        }
    }
    //all non-gap data blocks are in use
    return -ENOSPC;
}

/*==================================================
 *                                            EXT2 functions
 *==================================================*/
int make_dir(unsigned short inum){
    int count,inode_index,block_num;
    // Allocating and writing to new inode section and new directory entry
    inode_index = allocate_inode();
    if (inode_index < 0)
        return inode_index;
    init_inode(inode_index, 1024, 'd');
    struct ext2_inode* node = ino_table + inode_index;
    block_num = allocate_block( inode_index );
    if (block_num < 0)
        return block_num;
    //try to assign block to next available slot in block
    for (int idx = 0; idx < 12 ; idx ++){
        if (node->i_block[idx] == 0){
            node->i_block[idx] = block_num;
            break;
        } 
    }
    //Allocate empty directory entry and writes to it with current dir and parent dir entries
    struct ext2_dir_entry * dir = (struct ext2_dir_entry *)(disk + (1024* (node->i_block[0])));
    dir->file_type = EXT2_FT_DIR;
    dir->inode = inode_index + 1;
    strncpy(dir->name,".",1);
    dir->name_len = 1;
    dir->rec_len = sizeof(struct ext2_dir_entry) + dir->name_len;
    if (dir->rec_len % 4 != 0)
        dir->rec_len = 4*(dir->rec_len / 4) + 4;
    count = dir->rec_len;
    //parent dir
    dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
    dir->file_type = EXT2_FT_DIR;
    dir->inode = inum;
    (ino_table+inum-1)->i_links_count++;
    strncpy(dir->name,"..",2);
    dir->name_len = 2;
    dir->rec_len = 1024 - count;       
    if (dir->rec_len % 4 != 0)
        dir->rec_len =4*(dir->rec_len / 4) + 4;
    // Updating parent directory entry (note will make this another helper function)
    gd->bg_used_dirs_count+=1;
    int update = update_dir_entry(inum, inode_index + 1, new_dir, EXT2_FT_DIR);
    if (update < 0)
        return update;
    else
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
         //Preallocate blocks and inode for copying file
        int i_index = allocate_inode();
        if (i_index < 0)
            return i_index;
        inode = i_index + 1;
        //Filling in all needed info of newly allocated inode
        init_inode(i_index, fsize, 'f');
        struct ext2_inode* node = ino_table + i_index;
        //Allocate all blocks needed and store block number into predefined bookmark array
        int allocated = 0;
        struct single_indirect_block* sib;
        while (allocated < total_blocks){
            int block_num = allocate_block(i_index);
            if (block_num < 0)
                return block_num;
            //assigns block number to direct data blocks
            if (allocated < 12){
                node->i_block[allocated] = block_num;        
            }
            //needs to allocate single indirect block and assigns to it
            else{
                if (allocated == 12){
                    node->i_block[allocated] = block_num;
                    sib = (struct single_indirect_block*)(disk + (1024* (block_num)) );
                    sib->blocks[0] = block_num;
                }
                else{
                    int index = allocated % 12;
                    sib->blocks[index] = block_num;
                }
            }
            blocks[allocated] = block_num;
            allocated++;
        }
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
             total_read += (int)b_read;
             memcpy( (disk + 1024*blocks[block_count]), buffer, 1024);
             memset(buffer, 0, 1024);
             block_count ++;
         }
         fclose(file);
         //update parent directory
         int update;
         if (dir_flag == 'd'){
             update = update_dir_entry(parent_inode,inode,new_name,EXT2_FT_REG_FILE);
         }
         else{
             update = update_dir_entry(parent_inode,inode,new_dir,EXT2_FT_REG_FILE);
         }
         if (update < 0)
             return update;
         else
             return 0;
 }

int hard_link(unsigned short source_inode,unsigned short parent_inode){
    int update;
    if (dir_flag == 'd')
        update = update_dir_entry(parent_inode,source_inode,new_name,EXT2_FT_REG_FILE);
    else
        update = update_dir_entry(parent_inode,source_inode,new_dir,EXT2_FT_REG_FILE);
    if (update < 0)
        return update;
    else{
        (ino_table+source_inode-1)->i_links_count++;
        return 0;
    }
}

int sym_link(unsigned short parent_inode, char* path){
    int num_blocks,inode;
    int path_len = strlen(path) + 1;
    if (path_len%1024 != 0)
        num_blocks = path_len/4 + 1;
    else
        num_blocks = path_len/4;
    int blocks[num_blocks];
    //Preallocate blocks and inode for copying file
        int i_index = allocate_inode();
        if (i_index < 0)
            return i_index;
        inode = i_index + 1;
        //Filling in all needed info of newly allocated inode
        init_inode(i_index, path_len, 'l');
        struct ext2_inode* node = ino_table + i_index;
        //Allocate all blocks needed and store block number into predefined bookmark array
        int allocated = 0;
        struct single_indirect_block* sib;
        while (allocated < num_blocks){
            int block_num = allocate_block(i_index);
            if (block_num < 0)
                return block_num;
            //assigns block number to direct data blocks
            if (allocated < 12){
                node->i_block[allocated] = block_num;        
            }
            //needs to allocate single indirect block and assigns to it
            else{
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
        char name[dir->name_len+1];
        memset(name, '\0', dir->name_len+1);
        strncpy(name, dir->name, dir->name_len);
                
        while (count < 1024){

            
            //handles case where deleting first entry in dir_entry
            {
            if ( strcmp(name,f_name) == 0){
                if ((ino_table + dir->inode - 1)->i_links_count == 1){
                    set_bitmap(disk+(1024 * gd->bg_inode_bitmap),dir->inode - 1,'0');
                    construct_bitmap(32, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
                    sb->s_free_inodes_count++;
                    gd->bg_free_inodes_count++;
                    (ino_table + dir->inode - 1)->i_dtime = (int)time(NULL);
                    free_blocks(dir->inode);    
                }
                else
                    (ino_table + dir->inode - 1)->i_links_count --;
                dir->inode = 0;
                // the only entry in the block
                if (dir->rec_len == 1024){
                    set_bitmap(disk+(1024 * gd->bg_block_bitmap),block - 1,'0');
                    construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                    sb->s_free_blocks_count++;
                    gd->bg_free_blocks_count++;
                    (ino_table+parent_inode-1)->i_size-= 1024;
                }
                printf("first entry rmed\n");
                return 0;
            }
            }
            //checks the next entry and update reclen if found match
             next = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                         int actual_size = sizeof(struct ext2_dir_entry) + dir->name_len;
            if (actual_size % 4 != 0){
                actual_size =4*(actual_size/4) + 4;
            }
             char next_name[next->name_len+1];
             memset(next_name, '\0', next->name_len+1);
             strncpy(next_name, next->name, next->name_len);
             if (strcmp(next_name,f_name) == 0){
                 if ((ino_table + next->inode - 1)->i_links_count == 1){
                     set_bitmap(disk+(1024 * gd->bg_inode_bitmap),next->inode - 1,'0');
                     construct_bitmap(32, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
                     sb->s_free_inodes_count++;
                     gd->bg_free_inodes_count++;
                     (ino_table + next->inode - 1)->i_dtime = (int)time(NULL);
                     free_blocks(next->inode);
                 }
                 else
                     (ino_table + next->inode - 1)->i_links_count --;
                                 // the only entry in the block
                 dir->rec_len += next->rec_len;
                 if (dir->rec_len == 1024){
                    set_bitmap(disk+(1024 * gd->bg_block_bitmap),block - 1,'0');
                    construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                    sb->s_free_blocks_count++;
                    gd->bg_free_blocks_count++;
                    (ino_table+parent_inode-1)->i_size-= 1024;
                }
                 printf("entry rmed\n");
                 // the only entry in the block
                if (dir->rec_len == 1024){
                    set_bitmap(disk+(1024 * gd->bg_block_bitmap),block - 1,'0');
                    construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                    sb->s_free_blocks_count++;
                    gd->bg_free_blocks_count++;
                    (ino_table+parent_inode-1)->i_size-= 1024;
                }
                 return 0;
             }
             
                dir = (struct ext2_dir_entry *)((char *)dir + (dir->rec_len));
                count += (int)dir->rec_len;
        }
    }
    return -EINVAL;
}

int restore_file(unsigned short parent_inode, char* f_name){
    printf("will perform restore on file:%s\n\n",f_name);
    int block,count, gap_count,actual_size,diff;
    struct ext2_dir_entry *dir;
    struct ext2_dir_entry* cur;
    for (int i = 0; i < 12; i++){
        block = ino_table[parent_inode - 1].i_block[i];
        if (block != 0){
            dir = (struct ext2_dir_entry *)(disk + (1024* block));
            cur = dir;
            count = dir->rec_len;
            while (count <= 1024){
                printf("cur->%s  dir->%s\n",cur->name,dir->name);
                actual_size = sizeof(struct ext2_dir_entry) + dir->name_len;
                if (actual_size % 4 != 0){
                    actual_size =4*(actual_size/4) + 4;
                }
                cur = (struct ext2_dir_entry *)((char *)dir + actual_size);
                gap_count = actual_size;
                diff = dir->rec_len;

                while ( gap_count < diff ){

                        char name[cur->name_len+1];
                        memset(name, '\0', cur->name_len+1);
                        strncpy(name, cur->name, cur->name_len);
                        printf("%s %d \n",cur->name,gap_count);
                        if (strcmp(name,f_name) == 0){
                             if (block_bitmap[block-1] == 0){
                                set_bitmap(disk+(1024 * gd->bg_block_bitmap),block - 1,'1');
                                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                                sb->s_free_blocks_count--;
                                gd->bg_free_blocks_count--;
                                (ino_table+parent_inode-1)->i_size+= 1024;
                                (ino_table+parent_inode-1)->i_blocks += 2 ;
                             }
                             //hard links will not be handled as stated in assignment handout
                             set_bitmap(disk+(1024 * gd->bg_inode_bitmap),cur->inode - 1,'1');
                             construct_bitmap(32, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
                             sb->s_free_inodes_count--;
                             gd->bg_free_inodes_count--;
                             dir->rec_len = gap_count;
                             cur->rec_len = diff - gap_count;
                             (ino_table + cur->inode - 1)->i_dtime = 0;
                             reallocate_blocks(cur->inode);
                             printf("entry restored\n");
                             return 0;
                 }
                        actual_size = sizeof(struct ext2_dir_entry) + cur->name_len;
                        if (actual_size % 4 != 0){
                            actual_size =4*(actual_size/4) + 4;
                        }
                        cur = (struct ext2_dir_entry *)((char *)cur + actual_size);
                        gap_count += actual_size;
                }
                if (count == 1024)
                    break;
                dir = (struct ext2_dir_entry *)((char *)dir + dir->rec_len);
                count += dir->rec_len;
            }
        }
    }
    return -EINVAL;
}

int check_free(){
    //[free block count, free inode count, group flag, type flag]
    int flags[2];
    memset(flags,0,2*sizeof(int));
    int total_difference = 0;
    int difference;
    
    for (int i = 0; i < 128; i ++){
        if (block_bitmap[i] == 0)
            flags[0] += 1;
        if ( i < 32 && inode_bitmap[i] == 0)
            flags[1] += 1;
    }
    if (flags[0] != (int) sb->s_free_blocks_count){
        difference = abs(flags[0] - sb->s_free_blocks_count);
        total_difference += difference;
        sb->s_free_blocks_count = flags[0];
        printf("Superblock's free blocks counter was off by %d compared to the bitmap\n",difference);
    }
    if (flags[0] != (int)gd->bg_free_blocks_count){
        difference += abs(flags[0] - gd->bg_free_blocks_count);
        total_difference += difference;
        gd->bg_free_blocks_count = flags[0];
        printf("Block group's free blocks counter was off by %d compared to the bitmap\n",difference);
    }
    if (flags[1] != (int)sb->s_free_inodes_count){
        difference += abs(flags[1] - sb->s_free_inodes_count);
        total_difference += difference;
        sb->s_free_inodes_count = flags[1];
        printf("Superblock's free inodes counter was off by %d compared to the bitmap\n",difference);
    }
    if (flags[1] != (int)gd->bg_free_inodes_count){
        difference += abs(flags[1] - gd->bg_free_inodes_count);
        total_difference += difference;
        gd->bg_free_inodes_count = flags[1];
        printf("Block group's free inodes counter was off by %d compared to the bitmap\n",difference);
    }
    return total_difference; 
}

int check_mode(struct ext2_inode* inode, struct ext2_dir_entry* dir){ 
    if ( (inode->i_mode == EXT2_S_IFREG) && (dir->file_type != EXT2_FT_REG_FILE))
        return 1;
    else if ( (inode->i_mode == EXT2_S_IFDIR) && (dir->file_type != EXT2_FT_DIR) )
        return 1;
    else if ( (inode->i_mode == EXT2_S_IFLNK) && (dir->file_type != EXT2_FT_SYMLINK) )
        return 1;
    return 0;
}

int check_inode(unsigned short inode_num){
    if (inode_bitmap[inode_num-1] != 1){
        set_bitmap(disk+(1024 * gd->bg_inode_bitmap), inode_num - 1,'1');
        construct_bitmap(32, (char *)(disk+(1024 * gd->bg_inode_bitmap)), 'i');
        sb->s_free_inodes_count --;
        gd->bg_free_inodes_count --;
        printf("Fixed: inode [%d] not marked as in-use\n",inode_num);
        return 1;
    }
    return 0;    
}

int check_dtime(unsigned short inode_num){
    struct ext2_inode* inode = (struct ext2_inode*) (ino_table+inode_num-1);
    if (inode->i_dtime != 0){
        inode->i_dtime = 0;
        printf("Fixed: valid inode marked for deletion: [%d]\n",inode_num);
        return 1;
    }
    return 0;
}

int check_data(unsigned short inode_num){
    int fix_count = 0;
    struct ext2_inode* inode = (struct ext2_inode*) (ino_table+inode_num-1);
    if (inode->i_block[12] != 0){
        struct single_indirect_block* sib = (struct single_indirect_block*)(disk + (1024* inode->i_block[12]) );
        for (int i = 0 ; i < 256; i ++){
            if (sib->blocks[i] != 0 && block_bitmap[sib->blocks[i] - 1] == 0){
                    set_bitmap(disk+(1024 * gd->bg_block_bitmap), sib->blocks[i] - 1,'1');
                    construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                    sb->s_free_blocks_count --;
                    gd->bg_free_blocks_count --;
                    fix_count ++;
            }
        }
    }
    for (int i = 0 ; i < 12 ; i ++){
        int block = ( ino_table + inode_num - 1 )->i_block[i];
        if ( block != 0 && block_bitmap[block - 1] == 0){
                set_bitmap(disk+(1024 * gd->bg_block_bitmap), block - 1,'1');
                construct_bitmap(128, (char *)(disk+(1024 * gd->bg_block_bitmap)), 'b');
                sb->s_free_blocks_count --;
                gd->bg_free_blocks_count --;
                fix_count ++;
        }
    }
    if (fix_count > 0){
        printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n",fix_count,inode_num);
        return 1;
    }
    return 0;
}
