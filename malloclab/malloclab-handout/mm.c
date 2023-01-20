/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "----------",
    "Jamie Chen",
    "ang_chen@brown.edu",
    "", "",
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p)        (*(unsigned int *)(p))
#define PUT(p, val)   (*(unsigned int *)(p) = (val))

#define GET_SIZE(p)   (GET(p) & ~0x7)
#define GET_ALLOC(p)  (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)      ((char *)(bp) - WSIZE)
#define FTRP(bp)      ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute the addr. of previous footer and next header */
#define PREV_FTRP(bp) (HDRP(bp) - WSIZE)
#define NEXT_HDRP(bp) (FTRP(bp) + WSIZE)

/* Given block ptr bp, compute the addr. of previous and next blocks */
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(PREV_FTRP(bp)))
#define NEXT_BLKP(bp) (NEXT_HDRP(bp) + WSIZE)

/* 
 * Given a FREE block ptr bp, compute the address of pred and succ FIELDS
 * See p.862 of CSAPP:3e, free list format
 */
#define GET_PREDP(bp) bp
#define GET_SUCCP(bp) (bp + WSIZE)


#define FIRST_FIT 0
#define NEXT_FIT 1
#define BEST_FIT 2

#define FIT_STRATEGY FIRST_FIT
// #define FIT_STRATEGY NEXT_FIT
// #define FIT_STRATEGY BEST_FIT

static void *heap_listp;

static void *first_fit(size_t size) {
  void *bp = heap_listp;
  while (GET_SIZE(HDRP(bp)) != 0) {
    bp = NEXT_BLKP(bp);
    if (GET_SIZE(HDRP(bp)) >= size && !GET_ALLOC(HDRP(bp)))  // first fit
      return bp;
  }
  return NULL;
}

static void *next_fit(size_t size) {
  return first_fit(size);
  // void *bp = heap_listp;
  // while (GET_SIZE(HDRP(bp)) != 0) {
  //   bp = NEXT_BLKP(bp);
  //   if (GET_SIZE(HDRP(bp)) >= size && !GET_ALLOC(HDRP(bp)))  // first fit
  //     return bp;
  // }
  // return NULL;
}

static void *best_fit(size_t size) {
  void *best = NULL;
  size_t best_diff = -1;
  void *bp = heap_listp;
  while (GET_SIZE(HDRP(bp)) != 0) {
    bp = NEXT_BLKP(bp);
    if (!GET_ALLOC(HDRP(bp))) { // best fit
      size_t bsize = GET_SIZE(HDRP(bp));
      if (bsize > size && bsize - size < best_diff) {
        best = bp;
        best_diff = bsize - size;
      }
    }
  }
  return best;
}

static void place (void *bp, size_t size) {
  size_t bsize = GET_SIZE(HDRP(bp));
  size_t remain = bsize - size;

  if (remain < 2*DSIZE) {  // no split
    PUT(HDRP(bp), PACK(bsize, 1));
    PUT(FTRP(bp), PACK(bsize, 1));
  } else {  // split
    PUT(HDRP(bp), PACK(size, 1));
    PUT(FTRP(bp), PACK(size, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(remain, 0));
    PUT(FTRP(bp), PACK(remain, 0));
  }
}

static void *coalesce(void *bp) {
  size_t size = GET_SIZE(HDRP(bp));

  const void *prev_ftr = PREV_FTRP(bp);
  const void *next_hdr = NEXT_HDRP(bp);

  /* Coalesce with next free block */
  if (!GET_ALLOC(next_hdr)) {
    size += GET_SIZE(next_hdr);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  }

  /* Coalesce with prev free block */
  if (!GET_ALLOC(prev_ftr)) {
    size += GET_SIZE(prev_ftr);
    bp = PREV_BLKP(bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  }

  return bp;
}

static void *extend_heap(size_t size) {
  void *bp;
  size_t newsize = ALIGN(size);

  if ((bp = mem_sbrk(newsize)) == (void *)-1)
    return NULL;

  PUT(HDRP(bp), PACK(size, 0));           // block header (overwrite old epilogue header)
  PUT(FTRP(bp), PACK(size, 0));           // block footer
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // new epilogue header

  /* Possible coalesce with previous free block */
  return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
      return -1;

    PUT(heap_listp, 0);                           // padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));  // prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));  // prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));      // epilogue header
    heap_listp += DSIZE;

    /* Initialize a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE) == NULL)
      return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    void *bp;
    size_t newsize = ALIGN(size + SIZE_T_SIZE);

    /* Ignore spurious allocations */
    if (size == 0)
      return NULL;

    /* Search the free list for fit */
#if FIT_STRATEGY == BEST_FIT
    bp = best_fit(newsize);
#elif FIT_STRATEGY == FIRST_FIT
    bp = first_fit(newsize);
#else
    bp = next_fit(newsize);
#endif
    if (bp != NULL) {
      place(bp, newsize);
      return bp;
    }

    /* No fit found. Request more memory by calling extend_heap */
    bp = extend_heap(MAX(newsize, CHUNKSIZE));
    if (bp != NULL) {
      place(bp, newsize);
      return bp;
    }

    /* Heap extension failed */
    return NULL;
}

/*
 * mm_free - Free a block.
 */
void mm_free(void *ptr)
{
  size_t size = GET_SIZE(HDRP(ptr));
  
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
  coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;

    copySize = MIN(GET_SIZE(HDRP(oldptr)), size);
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}



