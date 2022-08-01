/* 
 * cachelab.h - Prototypes for Cache Lab helper functions
 */

#ifndef CACHELAB_TOOLS_H
#define CACHELAB_TOOLS_H

#define MAX_TRANS_FUNCS 100
#define MAX_ADDR_BITS 47
#define MAX_TRACELINE_LEN 32
#define MM_BSIZE 8
#define MM_MINIBSIZE 4
#define MM_BLOCK_H 23
#define MM_BLOCK_W 8

#define CACHEBLK_NIL -1
#define CACHEBLK_HIT 0
#define CACHEBLK_MISS_EVICT 1
#define CACHEBLK_MISS_FREE 2


typedef struct trans_func {
  void (*func_ptr)(int M,int N,int[N][M],int[M][N]);
  char* description;
  char correct;
  unsigned int num_hits;
  unsigned int num_misses;
  unsigned int num_evictions;
} trans_func_t;

typedef struct addr_id {
  unsigned long tbits;
  unsigned long sbits;
  unsigned long bbits;
} addr_id_t;

typedef struct cblock {
  unsigned long tag;
  char* data;
  struct cblock* prev;
  struct cblock* next;
  int valid;
  int dirty;
  int idx; // DEBUGGING
} cblock_t;

typedef struct cache {
  const unsigned int E, S, B;
  const unsigned int t, s, b;
  const unsigned long bmask;
  const unsigned long tmask;
  const unsigned long smask;
  int hits, misses, evictions;
  cblock_t** blks;
  cblock_t** heads;  // MRU
  cblock_t** tails;  // LRU
} cache_t;

/* Print helper message of the csim program */
void csimHelper();

/* Methods: cache_t */
cache_t* initCache(unsigned int E, unsigned int s, unsigned int b);
void freeCache(cache_t* cache);
void cacheDecodeAddr(cache_t* cache, unsigned long addr, addr_id_t* id);
cblock_t* cacheFindBlk(cache_t* cache, unsigned long set, unsigned long tag);
cblock_t* cacheGetLRU(cache_t* cache, unsigned long set);
cblock_t* cacheGetMRU(cache_t* cache, unsigned long set);
void cacheUseBlk(cache_t* cache, unsigned long set, cblock_t* blk, int dirty);

void cacheStore(cache_t* cache, addr_id_t* id, int* status); /* store to address id */
void cacheLoad(cache_t* cache, addr_id_t* id, int* status);  /* load from address id */
void cacheModify(cache_t* cache, addr_id_t* id, int* status_0, int* status_1);


/* 
 * printSummary - This function provides a standard way for your cache
 * simulator * to display its final hit and miss statistics
 */ 
void printSummary(int hits,  /* number of  hits */
				  int misses, /* number of misses */
				  int evictions); /* number of evictions */

/* Fill the matrix with data */
void initMatrix(int M, int N, int A[N][M], int B[M][N]);

/* The baseline trans function that produces correct results. */
void correctTrans(int M, int N, int A[N][M], int B[M][N]);

/* Add the given function to the function list */
void registerTransFunction(
    void (*trans)(int M,int N,int[N][M],int[M][N]), char* desc);


#endif /* CACHELAB_TOOLS_H */
