#include <iostream>
#include <sys/mman.h>

struct block{
    size_t size;
    block* next;
    bool is_free;
};

/*
mmap(
    void *address,
    
    size_t length/size,
    
    int prot, = access permisssions, read/write etc
    
    int flags, = map_shared (changes shared with other processes)
    map_private (creates a private copy on write mapping)
    map_anonomus (mapping is not backed by any file, the fd argument should be -1)
    
    int fd, = file descriptor
    off_t offset = starting point in the file from which the mapping begins
    
    )
*/

//creating a memory pool so that all blocks are contagious in memory
//enabling coalesce to actually merge adjacent free blocks
char* pool_start = nullptr;
char* pool_current = nullptr;

void init_pool(){
    pool_start = (char*)mmap(NULL, 1024*1024, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    pool_current = pool_start;
}

block* free_list = nullptr;

//block splitting
void block_split(block* b, size_t req_size){
    block* leftover = (block*)((char*)(b+1) + req_size);
    size_t ori_size = b->size;
    block* old_next = b->next;
    b->size = req_size;
    b->is_free = false;

    leftover->size = ori_size - req_size - sizeof(block);
    leftover->is_free = true;
    leftover->next = old_next;
    b->next = leftover;
}

void* mymalloc(size_t size){
    const size_t METADATA = sizeof(block);
    size = (size + sizeof(void*)-1) & ~(sizeof(void*)-1);
    size_t total_size = METADATA + size;
    block* current = free_list;
    block* insert_pos = free_list;

    while(current != NULL && !(current->is_free && current->size >= size)){
        current = current->next;
    }
    if(current != NULL){
        if(current->size >= size + sizeof(block) + sizeof(void*)){
            block_split(current, size);
        }
        else{
            current->is_free = false;
        }
        return current+1;
    }
    else{
        block* meta = (block*) pool_current;
        pool_current += total_size;
        meta->size = size;
        meta->is_free = false;

        if(free_list == nullptr || meta < free_list){
            meta->next = free_list;
            free_list = meta;
        }
        else{
            block* insert_pos = free_list;
            while(insert_pos->next != nullptr && insert_pos->next < meta){
                insert_pos = insert_pos->next;
            }
            meta->next = insert_pos->next;
            insert_pos->next = meta;
        }
        return (block*) meta+1;
    }
}

void coalesce(block* ptr){
    if(!ptr) return;
    if(ptr->next && ptr->next->is_free && (char*)ptr + sizeof(block) + ptr->size == (char*)ptr->next){
        ptr->size += sizeof(block) + ptr->next->size;
        ptr->next = ptr->next->next;
    }

    block* current = free_list;
    while(current && current->next != ptr){
        current = current->next;
    }
    if(current && current->next == ptr && current->is_free && (char*)current + sizeof(block) + current->size == (char*)ptr){
        current->size += sizeof(block) + ptr->size;
        current->next = ptr->next;
    }
}

void myfree(void* ptr){
    if(!ptr) return;
    block* meta = (block*)ptr - 1;
    meta->is_free = true;
    coalesce(meta);
}