/*
 * Name: Ethan Olpin
 * Date: 11/18/2020
 * Course: CS 4400
 * 
 * Description: 
 * 
 *  This solution utilizes a explicit free-list implementation.
 *  Every free block has at least two bytes that are designated for a prev and
 *  next pointer.
 * 
 *  My solution creates "chunks" which are 1 or more contiguous pages, these
 *  chunks are tracked in a doubly linked list, the head of each chunk contains
 *  the pointers to next and previous chunks. 
 * 
 *  Other strategies I employed include:
 * 
 *    -Splitting blocks when the free block is not a perfect fit for the
 *     payload size.
 *    -Coalescing free blocks with their neighbors.
 *    -Unmapping the pages for chunks that are not being used
 * 
 *  Additonally I included a best-fit function definition which is not used by the
 *  final solution. The best fit approach improved utilization but had a much larger
 *  adverse effect on throughput. The final solution uses the first-fit approach.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* always use 16-byte alignment */
#define ALIGNMENT 16
#define OVERHEAD 16
#define CHUNK_OVERHEAD 48
#define MIN_FREE_BLOCK_SIZE 32
#define CHUNK_PAD 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

#define PAGE_OFFSET 32

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))
// Given a pointer to a header, get or set its value
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
// Combine a size and alloc bit
#define PACK(size, alloc) ((size) | (alloc))
// Given a header pointer, get the alloc or size
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SIZE(p) (GET(p) & ~0xF)

//Given a payload pointer, get a pointer to the header
#define HEADER(p) (p - 8)
//Given a payload pointer, get a pointer to the footer
#define FOOTER(p) (p + GET_SIZE(HEADER(p)) - OVERHEAD)

//Given a payload pointer, get a pointer to the previous
#define PREV_BLK_PTR(p) (p - GET_SIZE(p - OVERHEAD))
//Given a payload pointer, get a pointer to the next
#define NXT_BLK_PTR(p) ((char*)p + GET_SIZE(HEADER(p)))

typedef struct {
  void* next;
  void* prev;
} list_node;

//____HELPER FUNCTIONS_____
void add_to_free_list(void* ptr);
void alloc_block(void* ptr, size_t size);
void *coalesce(void* ptr);
void extend(void* ptr, size_t new_size);
void *find_free_block_bf(size_t target_size);
void *find_free_block_ff(size_t target_size);
void *init_chunk(size_t payload_size);
size_t is_in_free_chunk(void* ptr);
void remove_chunk(list_node* ptr, size_t chunk_size);
void remove_from_free_list(list_node* ptr);
list_node *get_chunk_header(void* ptr);
//____END HELPER FUNCS_____

//Chunk size in pages
int new_chunk_size = 1;
list_node* chunk_list_head = NULL;
list_node* free_list_head = NULL;

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  new_chunk_size = 1;
  free_list_head = NULL;
  chunk_list_head = NULL;
  chunk_list_head = get_chunk_header(init_chunk(1));
  return 0;
}

/* 
 * mm_malloc - Allocate a block by using bytes from current_avail,
 *     grabbing a new page if necessary.
 */
void *mm_malloc(size_t size)
{
  size_t new_size = ALIGN(size + OVERHEAD);
  void* p = find_free_block_ff(new_size);
  if(p == NULL){
    p = init_chunk(new_size);
  }
  alloc_block(p, new_size);
  return p;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
  void* header = HEADER(ptr); 
  if(!GET_ALLOC(header)){
    return;
  }
  PUT(header, GET_SIZE(header));
  add_to_free_list(ptr);
  void* free_p = coalesce(ptr);
  
  size_t chunk_size = is_in_free_chunk(free_p);
  //Unmap the chunk to save on utilization
  if(chunk_size){
    //free_p must be removed from the free list as we are
    //removing its chunk
    remove_from_free_list(free_p);
    remove_chunk(get_chunk_header(free_p), chunk_size);
  }
}

/*------------------------------BEGINS HELPER DEFINITIONS------------------------*/


//Allocates a new chunk of free memory using mem_map
void *init_chunk(size_t payload_size){
  //Min_free_block_size is added to prevent case where our new chunk's
  //free block is slightly larger than payload size, but not large enough
  //for a following freeblock. 
  size_t chunk_size =
    new_chunk_size * PAGE_ALIGN(payload_size + CHUNK_OVERHEAD + MIN_FREE_BLOCK_SIZE);
  //Frankly not sure why, but I found this to greatly improv my utilization
  //over doubling the chunksize with each call.
  new_chunk_size += 1;
  void* chunk_ptr = mem_map(chunk_size) + 8;
  if(chunk_ptr == NULL){
    return NULL;
  }

  //New chunks are installed as the new chunk_list_head
  ((list_node*)chunk_ptr)->next = chunk_list_head;
  if(chunk_list_head != NULL)
    chunk_list_head->prev = chunk_ptr;
  chunk_list_head = chunk_ptr; 
  chunk_list_head->prev = NULL;
  chunk_ptr += 16;

  //16 bytes for prologue
  PUT(chunk_ptr, 17);
  PUT(chunk_ptr + 8, 16);
  chunk_ptr += 16;
  
  //8 bytes for header
  PUT(chunk_ptr, chunk_size - CHUNK_OVERHEAD);
  //8 bytes for footer
  PUT(chunk_ptr + chunk_size - CHUNK_OVERHEAD - 8, chunk_size - CHUNK_OVERHEAD);
  //8 bytes for terminator block
  PUT(chunk_ptr + chunk_size - CHUNK_OVERHEAD, 1);
  //Return payload pointer and add it to free list
  chunk_ptr += 8;
  add_to_free_list(chunk_ptr);
  return chunk_ptr;
}

//Returns a pointer to the chunkheader for the ptr's chunk
list_node *get_chunk_header(void* ptr){
  void* curr = ptr;
  size_t curr_size;
  
  while(curr){
    //Get the footer of the preceding block
    curr_size = GET_SIZE(curr - 16);
    //Found chunk prologue
    if(curr_size == 16){
      return curr - 40; //Returns start of chunk header
    }
    //Get previous block
    else{
      curr = PREV_BLK_PTR(curr);
    }
  }
  return NULL;
}

//Checks if the payload pointer is a part of an empty chunk
//If so, returns the size of the chunk. Otherwise, returns 0
size_t is_in_free_chunk(void* ptr){
  void* curr = ptr;
  size_t curr_size;
  size_t chunk_size = GET_SIZE(HEADER(ptr)) + CHUNK_OVERHEAD;
  int curr_alloc;
  while(curr){
    curr_size = GET_SIZE(curr - 16);
    //Found chunk prologue
    if(curr_size == 16){
      break;
    }
    curr = PREV_BLK_PTR(curr);
    curr_alloc = GET_ALLOC(HEADER(curr));
    //Found allocated block
    if(curr_alloc){
      return 0;
    }
    //Check next block
    else{
      chunk_size += curr_size;
    }
  }
  curr = NXT_BLK_PTR(ptr);
  while(curr){
    curr_size = GET_SIZE(HEADER(curr));
    curr_alloc = GET_ALLOC(HEADER(curr));
    //Found terminator block
    if(!curr_size && curr_alloc){
      break;
    }
    //Found allocated block
    else if(curr_alloc){
      return 0;
    }
    //Get next block
    else{
      chunk_size += curr_size;
      curr = NXT_BLK_PTR(curr);
    }
  }
  return chunk_size;
}

//Removes a chunk from the doubly linked list of chunks
void remove_chunk(list_node* ptr, size_t chunk_size){
  if(ptr == chunk_list_head){
    chunk_list_head = ptr->next;
    if(chunk_list_head != NULL){
      chunk_list_head->prev = NULL;
    }
  }
  else{
    ((list_node*)ptr->prev)->next = ptr->next;
    if(ptr->next != NULL)
      ((list_node*)ptr->next)->prev = ptr->prev;
  }
  //Returns the true start of the chunk where the padding byte is
  mem_unmap(((void*)ptr - CHUNK_PAD), chunk_size);
}

//Allocates a block at location p of specified size
void alloc_block(void* ptr, size_t size){
  size_t block_size = GET_SIZE(HEADER(ptr));
  //Block to be allocated is exactly the size
  //of the free block.
  if(size == block_size){
    PUT(HEADER(ptr), PACK(block_size, 1));
    remove_from_free_list(ptr);
  }
  //There is a leftover free block that we add to the free list
  else{
    PUT(HEADER(ptr), PACK(size, 1));
    PUT(FOOTER(ptr), size);
    void* remaining_block = ptr + GET_SIZE(HEADER(ptr));
    PUT(HEADER(remaining_block), block_size - size);
    PUT(FOOTER(remaining_block), block_size - size);
    remove_from_free_list(ptr);
    add_to_free_list(remaining_block);
  }
}

//Adds the block at p to the free list
void add_to_free_list(void* ptr){
  ((list_node*)ptr)->next = free_list_head;
  if(free_list_head != NULL)
    free_list_head->prev = ptr;
  free_list_head = ptr; 
  free_list_head->prev = NULL;
}

//Given a free block pointer, removes pointer
//from free list
void remove_from_free_list(list_node* ptr){
  if(ptr == free_list_head){
    free_list_head = ptr->next;
    if(free_list_head != NULL){
      free_list_head->prev = NULL;
    }
  }
  else{
    ((list_node*)ptr->prev)->next = ptr->next;
    if(ptr->next != NULL)
      ((list_node*)ptr->next)->prev = ptr->prev;
  }
}

//Returns the pointer the best-fit block that meets
//target size
void *find_free_block_bf(size_t target_size){
  list_node* temp = free_list_head;
  void* best_fit = NULL;
  while(temp != NULL) {
    size_t temp_size = GET_SIZE(HEADER((void*)temp));
    if(temp_size == target_size){
      //Found a perfect fit
      return (void*)temp;
    }
    else if(temp_size >= target_size + MIN_FREE_BLOCK_SIZE){
      if(best_fit != NULL){
        if(temp_size < GET_SIZE(HEADER(best_fit)))
          //best fit so far
          best_fit = (void*)temp;
      }
      else{
        //first fit we've found
        best_fit = (void*)temp;
      }
    }
    temp = temp->next;
  }
  return best_fit == NULL ? NULL : best_fit;
}

//Returns the first payload pointer for a free block that
//meets the target size
void *find_free_block_ff(size_t target_size){
  list_node* temp = free_list_head;
  while(temp != NULL) {
    size_t temp_size = GET_SIZE(HEADER((void*)temp));
    if(temp_size == target_size || temp_size >= target_size + MIN_FREE_BLOCK_SIZE){
      //Found a perfect fit
      return (void*)temp;
    }
    temp = temp->next;
  }
  return NULL;
}


//Given a pointer to a free block extends it to new_size
void extend(void* ptr, size_t new_size){
  PUT(HEADER(ptr), new_size);
  PUT(FOOTER(ptr), new_size);
}

//Takse a pointer to a free block and checks if it's neighbors
//are also free. If so, joins the current block with one or two
//of it's neighbors and updates the free list accordingly
//
//Returns a pointer to the start of the resulting free block
void* coalesce(void* ptr){
  void* prevh = HEADER(PREV_BLK_PTR(ptr));
  void* nexth = HEADER(NXT_BLK_PTR(ptr));
  int prev_alloc = GET_ALLOC(prevh);
  int next_alloc = GET_ALLOC(nexth);
  size_t curr_size = GET_SIZE(HEADER(ptr));
  size_t prev_size = GET_SIZE(prevh);
  size_t next_size = GET_SIZE(nexth);

  void* result = ptr;
  if(!prev_alloc && !next_alloc){ //Coalesce with both neighboring blocks
    result = prevh + 8;
    remove_from_free_list(ptr);
    remove_from_free_list(nexth + 8);
    extend(result, curr_size + prev_size + next_size);
  }
  else if(!prev_alloc){ //Coalesce with previous block
    result = prevh + 8;
    remove_from_free_list(ptr);
    extend(result, curr_size + prev_size);
  }
  else if(!next_alloc){ //Coalesce with next block
    remove_from_free_list(nexth + 8);
    extend(result, curr_size + next_size);
  }
  return result;
}