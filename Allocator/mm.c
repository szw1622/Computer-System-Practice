/*
 * mm-naive.c - The least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by allocating a
 * new page as needed.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused.
 *
 * Use explicit free list auto coalesce free blocks
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

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize() - 1)) & ~(mem_pagesize() - 1))

// This assumes you have a struct or typedef called "block_header" and "block_footer"
#define OVERHEAD 16

// Given a payload pointer, get the header or footer pointer
#define HDRP(bp) ((char *)(bp)-8)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - OVERHEAD)

// Given a payload pointer, get the next or previous payload pointer
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE((char *)(bp)-OVERHEAD))

// ******These macros assume you are using a size_t for headers and footers ******

// Given a pointer to a header, get or set its value
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

// Combine a size and alloc bit
#define PACK(size, alloc) ((size) | (alloc))

// Given a header pointer, get the alloc or size
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SIZE(p) (GET(p) & ~0xF)

// *******************************************************************************

// ******Recommended helper functions******

/* These functions will provide a high-level recommended structure to your program.
 * Fill them in as needed, and create additional helper functions depending on your design.
 */

/* Set a block to allocated
 * Update block headers/footers as needed
 * Update free list if applicable
 * Split block if applicable
 */
void set_allocated(void *b, size_t size);

/* Request more memory by calling mem_map
 * Initialize the new chunk of memory as applicable
 * Update free list if applicable
 */
void extend(size_t s);

/* Coalesce a free block if applicable
 * Returns pointer to new coalesced block
 */
void *coalesce(void *bp);

typedef struct block_header block_header;
typedef struct block_footer block_footer;
typedef struct list_ptr list_ptr;

struct block_header
{
  size_t size;
};

struct block_footer
{
  size_t size;
};

struct list_ptr
{
  list_ptr *prev;
  list_ptr *next;
};

int mapped;

list_ptr *free_head = NULL;
list_ptr *page_head = NULL;

/* Get first free block.
 */
void *get_first(size_t size);

void add(list_ptr *ptr, int page);

void remove_from_list(list_ptr *ptr, int page);

/* Unmap unused pages.
 */
void unmap_page();

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  free_head = NULL;
  page_head = NULL;
  mapped = 0;
  return 0;
}

/*
 * mm_malloc - Allocate a block by using bytes from current_avail,
 *     grabbing a new page if necessary.
 */
void *mm_malloc(size_t size)
{
  size = ALIGN(size + OVERHEAD);
  // show_pages();
  void *ptr;
  while ((ptr = get_first(size)) == NULL)
  {
    extend(size);
  }

  if (GET_SIZE(HDRP(ptr)) - size <= OVERHEAD + sizeof(list_ptr))
  {
    size = GET_SIZE(HDRP(ptr));
  }

  // printf("Get size: %d\n", size);

  size_t size_remain = GET_SIZE(HDRP(ptr)) - size;
  // printf("Size remain: %d\n", size_remain);

  remove_from_list(ptr, 0);
  set_allocated(HDRP(ptr), PACK(size, 1));

  if (size_remain > 0)
  {
    set_allocated(HDRP(NEXT_BLKP(ptr)), PACK(size_remain, 0));
    // printf("Add remain %d\n", size_remain);
    add(NEXT_BLKP(ptr), 0);
  }
  // printf("Allocated: %p\n", ptr);

  return ptr;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
  // printf("Call free on %p\n", ptr);
  // printf("Current has size %d\n", (GET_SIZE(HDRP(ptr))));
  unmap_page();

  void *prev_block = PREV_BLKP(ptr);
  void *next_block = NEXT_BLKP(ptr);

  set_allocated(HDRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));

  // printf("Pre size: %d\n", *(size_t *)((char *)ptr - OVERHEAD));
  // printf("Pre: %p\n", prev_block);
  // printf("Next: %p\n", next_block);

  if (GET_ALLOC(HDRP(prev_block)) == 0)
  {
    // printf("Merge prev\n");
    // printf("Prev has size %d\n", GET_SIZE(HDRP(prev_block)));

    remove_from_list(prev_block, 0);
    set_allocated(HDRP(prev_block), PACK(GET_SIZE(HDRP(prev_block)) + GET_SIZE(HDRP(ptr)), 0));
    ptr = prev_block;
  }

  if (GET_ALLOC(HDRP(next_block)) == 0)
  {
    // printf("Merge next\n");
    remove_from_list(next_block, 0);
    set_allocated(HDRP(ptr), PACK(GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(next_block)), 0));
  }
  add(ptr, 0);
}

void set_allocated(void *b, size_t size)
{
  *((size_t *)b) = size;
  b = (char *)b + 8;
  *((size_t *)FTRP(b)) = size;
}

void add(list_ptr *ptr, int page)
{
  // printf("Add: %p\n", ptr);

  ptr->next = NULL;
  ptr->prev = NULL;
  if (!page)
  {
    if (free_head == NULL)
    {
      free_head = ptr;
      return;
    }
  }
  else
  {
    if (page_head == NULL)
    {
      page_head = ptr;
      return;
    }
  }

  list_ptr *head = page ? page_head : free_head;
  ptr->next = head;
  head->prev = ptr;
  if (!page)
  {
    free_head = ptr;
  }
  else
  {
    page_head = ptr;
  }
}

void remove_from_list(list_ptr *ptr, int page)
{
  // printf("Remove: %p\n", ptr);
  if (ptr->next == NULL && ptr->prev == NULL)
  {
    if (!page)
    {
      free_head = NULL;
    }
    else
    {
      page_head = NULL;
    }
  }
  else if (ptr->next == NULL)
  {
    ptr->prev->next = NULL;
  }
  else if (ptr->prev == NULL)
  {
    ptr->next->prev = NULL;
    if (!page)
    {
      free_head = ptr->next;
    }
    else
    {
      page_head = ptr->next;
    }
  }
  else
  {
    ptr->prev->next = ptr->next;
    ptr->next->prev = ptr->prev;
  }

  ptr->prev = NULL;
  ptr->next = NULL;
}
void *get_first(size_t size)
{
  list_ptr *current = free_head;

  while (current != NULL && GET_SIZE(HDRP(current)) < size)
  {
    current = current->next;
  }

  return current;
}

void extend(size_t s)
{
  s = PAGE_ALIGN(s + sizeof(list_ptr) + OVERHEAD * 2);
  size_t page_size;

  while (page_size < s)
  {
    page_size = mapped * mem_pagesize();
    mapped = mapped + 6;
  }

  void *ptr = mem_map(page_size);

  int page_header_size = sizeof(list_ptr) + OVERHEAD;
  int page_footer_size = OVERHEAD;
  size_t empty_size = page_size - page_header_size - page_footer_size - 16;
  // printf("Allocated new page on %p\n", ptr);

  *(size_t *)ptr = empty_size;
  ptr = (char *)ptr + 8;

  set_allocated(ptr, PACK(page_header_size, 1));
  list_ptr *list = (char *)ptr + 8;
  add(list, 1);

  void *empty_start = (char *)ptr + page_header_size;

  set_allocated(empty_start, PACK(empty_size, 0));
  add((char *)empty_start + 8, 0);

  set_allocated((char *)ptr + page_size - OVERHEAD - 16, PACK(OVERHEAD, 1));
}

void unmap_page()
{
  list_ptr *current = page_head;
  int page_header_size = sizeof(list_ptr) + OVERHEAD;
  int page_footer_size = OVERHEAD;

  while (current != NULL)
  {
    void *page_header = (char *)current - OVERHEAD;
    size_t empty_size = *(size_t *)page_header;
    void *block = NEXT_BLKP(current);
    if (GET_SIZE(HDRP(NEXT_BLKP(NEXT_BLKP(current)))) == OVERHEAD)
    {
      list_ptr *next = current->next;
      remove_from_list(current, 1);
      remove_from_list(block, 0);
      mem_unmap(page_header, empty_size + page_header_size + page_footer_size + 16);
      current = next;
      continue;
    }
    current = current->next;
  }
}
