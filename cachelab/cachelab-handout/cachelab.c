/*
 * cachelab.c - Cache Lab helper functions
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "cachelab.h"
#include <time.h>

trans_func_t func_list[MAX_TRANS_FUNCS];
int func_counter = 0; 


/* 
 * csimHelper - Print helper message of the csim program. 
 */
void csimHelper() {
    puts(
        "Usage: ./csim [-hvc] -s <num> -E <num> -b <num> -t <file>\n"
        "Options:\n"
        "  -h         Print this help message.\n"
        "  -v         Optional verbose flag.\n"
        "  -c         Optional cache printing flag.\n"
        "  -s <num>   Number of set index bits.\n"
        "  -E <num>   Number of lines per set.\n"
        "  -b <num>   Number of block offset bits.\n"
        "  -t <file>  Trace file.\n"
        "\n"
        "Examples:\n"
        "  linux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n"
        "  linux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace"
    );
}

cache_t* initCache(unsigned int E, unsigned int s, unsigned int b) {
    /* Init cache parameters */
    unsigned int sb = s + b;
    unsigned int S = 1 << s;
    unsigned int B = 1 << b;

    cache_t* cache = (cache_t*)malloc(sizeof(cache_t));
    *(unsigned int*)&cache->E = E;
    *(unsigned int*)&cache->s = s;
    *(unsigned int*)&cache->b = b;
    *(unsigned int*)&cache->S = S;
    *(unsigned int*)&cache->B = B;
    *(unsigned int*)&cache->t = MAX_ADDR_BITS - sb;

    unsigned long amask, bmask, tmask, smask;
    amask =   (1ul << MAX_ADDR_BITS) - 1;
    bmask =   (1ul << b ) - 1;
    tmask = ~((1ul << sb) - 1) & amask;
    smask = ~( bmask | tmask ) & amask;
    *(unsigned long*)&cache->bmask = bmask;
    *(unsigned long*)&cache->tmask = tmask;
    *(unsigned long*)&cache->smask = smask;
    cache->hits = cache->misses = cache->evictions = 0;
    
    /* Allocate memory for cachelines */
    cache->blks  = (cblock_t**)malloc(S * sizeof(cblock_t*));  // S x E cache blocks
    cache->heads = (cblock_t**)malloc(S * sizeof(cblock_t*));  // MRU heads for sets
    cache->tails = (cblock_t**)malloc(S * sizeof(cblock_t*));  // LRU tails for sets
    for (int i = 0; i < S; i++) {
        cblock_t* cset = (cblock_t*)malloc(E * sizeof(cblock_t));
        cblock_t* blk = cset;
        for (int j = 0; j < E; j++, blk++) {
            blk->tag = 0ul;
            blk->data = (char*)malloc(B);
            blk->prev = blk - 1;
            blk->next = blk + 1;
            blk->valid = 0;
            // blk->prev = j? blk - 1 : NULL;
            // blk->next = j < E-1? blk + 1 : NULL;
            
            blk->idx = j; // DEBUGGING
        }
        --blk;  // tail block
        cset->prev = NULL;
        blk->next  = NULL;
        cache->blks [i] = cset;
        cache->heads[i] = cset;
        cache->tails[i] = blk;
    }

    return cache;
}

void freeCache(cache_t* cache) {
    for (int i = 0; i < cache->S; i++) {
        cblock_t* cset = cache->blks[i];
        for (int j = 0; j < cache->E; j++)
            free(cset[j].data);
        free(cset);
    }
    free(cache->blks);
    free(cache->heads);
    free(cache->tails);
    free(cache);
}

void cacheDecodeAddr(cache_t* cache, unsigned long addr, addr_id_t* id) {
    id->tbits = (addr & cache->tmask) >> (cache->s + cache->b);
    id->sbits = (addr & cache->smask) >> (cache->b);
    id->bbits = (addr & cache->bmask);
}

/* Find the block specified by set and tag and
 * return the block index; Return -1 on miss.
 * // To invalidate a block, set vbit = 0 and move block to tail.
 */  
cblock_t* cacheFindBlk(cache_t* cache, unsigned long set, unsigned long tag) {
    /* Linear search from MRU --> LRU for matching tag */
    for (cblock_t* blk = cache->heads[set]; blk; blk = blk->next) {
        if (!blk->valid)  // miss. Found free block : Tail is free block
            return NULL;
        if (tag == blk->tag) // hit.
            return blk;
    }
    return NULL;  // miss. No free block : Tail is LRU
}

cblock_t* cacheGetLRU(cache_t* cache, unsigned long set) {
    return cache->tails[set];
}

cblock_t* cacheGetMRU(cache_t* cache, unsigned long set) {
    return cache->heads[set];
}

/* Move `blk` to the head of cacheset `set`
 * Set valid bit, optionally dirty bit
 * Modify `heads` and `tails` if needed 
 */
void cacheUseBlk(cache_t* cache, unsigned long set, cblock_t* blk, int dirty) {
    cblock_t* old_head = cache->heads[set];
    cblock_t* prev_blk = blk->prev;
    cblock_t* next_blk = blk->next;

    blk->valid = 1;
    blk->dirty = dirty;

    if (prev_blk == NULL) return;  // blk is already head.
    
    // blk is not head.
    if (next_blk == NULL)  // blk is tail, prev becomes the new tail of the set
        cache->tails[set] = prev_blk;
    else  // blk is not tail, update next_blk's prev
        next_blk->prev = prev_blk;

    // move blk to head
    prev_blk->next = next_blk;
    old_head->prev = blk;
    blk->next = old_head;
    blk->prev = NULL;
    cache->heads[set] = blk;
}


void cacheStore(cache_t* cache, addr_id_t* id, int* status) {
    // missing args: - data, - length
    unsigned long set = id->sbits;
    unsigned long tag = id->tbits;
    cblock_t* blk = cacheFindBlk(cache, set, tag);
    if (blk) {
        /* Hit: Store data to cache block directly */
        *status = CACHEBLK_HIT;
        ++cache->hits;
    } else {
        /* Miss: Copy block from memory to LRU / free
                 cacheline, then store data to cache */
        blk = cacheGetLRU(cache, set);  // LRU policy
        *status = CACHEBLK_MISS_FREE;
        if (blk->valid) {
            *status = CACHEBLK_MISS_EVICT;
            ++cache->evictions;
            // TODO: If blk dirty, write back to MEM
        }
        blk->tag = tag;  // Update the tag
        ++cache->misses;
    }
    cacheUseBlk(cache, set, blk, 1);  // Move block to head (MRU), set valid & dirty
}

void cacheLoad(cache_t* cache, addr_id_t* id, int* status) {
    unsigned long set = id->sbits;
    unsigned long tag = id->tbits;
    cblock_t* blk = cacheFindBlk(cache, set, tag);
    if (blk) {
        /* Hit: Load data from cache block directly */
        *status = CACHEBLK_HIT;
        ++cache->hits;
    } else {
        /* Miss: Copy block from memory to LRU / free
                 cacheline, then load data from cache */
        blk = cacheGetLRU(cache, set);  // LRU policy
        *status = CACHEBLK_MISS_FREE;
        if (blk->valid) {
            *status = CACHEBLK_MISS_EVICT;
            ++cache->evictions;
            // TODO: If blk dirty, write back to MEM
        }
        blk->tag = tag;  // Update the tag
        ++cache->misses;
    }
    cacheUseBlk(cache, set, blk, 0);  // Move block to head (MRU), dirty = 0
}

void cacheModify(cache_t* cache, addr_id_t* id, int* status_0, int* status_1) {
    cacheLoad(cache, id, status_0);
    cacheStore(cache, id, status_1);
}


/* 
 * printSummary - Summarize the cache simulation statistics. Student cache simulators
 *                must call this function in order to be properly autograded. 
 */
void printSummary(int hits, int misses, int evictions)
{
    printf("hits:%d misses:%d evictions:%d\n", hits, misses, evictions);
    FILE* output_fp = fopen(".csim_results", "w");
    assert(output_fp);
    fprintf(output_fp, "%d %d %d\n", hits, misses, evictions);
    fclose(output_fp);
}

/* 
 * initMatrix - Initialize the given matrix 
 */
void initMatrix(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;
    srand(time(NULL));
    for (i = 0; i < N; i++){
        for (j = 0; j < M; j++){
            // A[i][j] = i+j;  /* The matrix created this way is symmetric */
            A[i][j]=rand();
            B[j][i]=rand();
        }
    }
}

void randMatrix(int M, int N, int A[N][M]) {
    int i, j;
    srand(time(NULL));
    for (i = 0; i < N; i++){
        for (j = 0; j < M; j++){
            // A[i][j] = i+j;  /* The matrix created this way is symmetric */
            A[i][j]=rand();
        }
    }
}

/* 
 * correctTrans - baseline transpose function used to evaluate correctness 
 */
void correctTrans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;
    for (i = 0; i < N; i++){
        for (j = 0; j < M; j++){
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    
}


/* 
 * registerTransFunction - Add the given trans function into your list
 *     of functions to be tested
 */
void registerTransFunction(void (*trans)(int M, int N, int[N][M], int[M][N]), 
                           char* desc)
{
    func_list[func_counter].func_ptr = trans;
    func_list[func_counter].description = desc;
    func_list[func_counter].correct = 0;
    func_list[func_counter].num_hits = 0;
    func_list[func_counter].num_misses = 0;
    func_list[func_counter].num_evictions =0;
    func_counter++;
}
