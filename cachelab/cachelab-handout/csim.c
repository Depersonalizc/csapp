#include "cachelab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char* argv[])
{
    FILE* fp;
    char* trace_path;
    int verbose = 0, print_cache = 0;
    unsigned int s = 0, E = 0, b = 0;

    // parsing
    char* arg;
    for (int i = 1; i < argc; i++) {
        arg = argv[i];
        if (arg[0] == '-') {
            /* option */
            switch(arg[1]) {
                case 'h':
                    csimHelper();
                    return 0;
                case 'v':
                    verbose = 1;
                    break;
                case 'c':
                    print_cache = 1;
                    break;
                case 's':
                    if (++i < argc) {
                        arg = argv[i];
                        s = atoi(arg);
                    }
                    break;
                case 'E':
                    if (++i < argc) {
                        arg = argv[i];
                        E = atoi(arg);
                    }
                    break;
                case 'b':
                    if (++i < argc) {
                        arg = argv[i];
                        b = atoi(arg);
                    }
                    break;
                case 't':
                    if (++i < argc)
                        trace_path = argv[i];
                    break;
                default:
                    printf("./csim: invalid option -- '%s'\n", arg);
                    csimHelper();
                    return 1;
            }
        }
    } // parsing done

    // DEBUG
    printf("[s=%u, E=%u, b=%u, t:%s]\n", s, E, b, trace_path);

    if ( !(s && E && b && trace_path) ) {
        printf("./csim: Missing required command line argument\n");
        csimHelper();
        return 2;
    }
    if (s > MAX_ADDR_BITS || b > MAX_ADDR_BITS || (s + b) > MAX_ADDR_BITS) {
        printf("./csim: Invalid cache parameters (s = %u, b = %u)\n", s, b);
        return 3;
    }
    if ( (fp = fopen(trace_path, "r")) == NULL ) {
        printf("%s: No such file or directory\n", trace_path);
        return 4;
    }

    unsigned long addr;
    addr_id_t id;
    cache_t* cache = initCache(E, s, b);
    int status_0 = CACHEBLK_NIL;
    int status_1 = CACHEBLK_NIL;

    char* end_p;
    char line_buf[MAX_TRACELINE_LEN];
    while (fgets(line_buf, MAX_TRACELINE_LEN, fp)) {
        if (line_buf[0] != ' ')
            continue;

        end_p = &line_buf[strcspn(line_buf, ",")];
        addr  = strtoul(&line_buf[3], &end_p, 16);
        cacheDecodeAddr(cache, addr, &id);
        status_1 = CACHEBLK_NIL;

        switch (line_buf[1]) {
            case 'S':
                cacheStore(cache, &id, &status_0);
                break;
            case 'L':
                cacheLoad(cache, &id, &status_0);
                break;
            case 'M':
                cacheModify(cache, &id, &status_0, &status_1);
                break;
            default:
                break;
        }

        if (verbose) {
            /* print trace */
            line_buf[strcspn(line_buf, "\n")] = 0;
            printf("%s", &line_buf[1]);
            // printf("%0*lx\t", (int)((MAX_ADDR_BITS + 3) / 4 ), addr);
            // printf("tag: %0*lx\t", (int)((MAX_ADDR_BITS - (s+b) + 3) / 4), id.tbits);
            // printf("set: %0*lx\t", (int)((s + 3) / 4), id.sbits);
            // printf("bbits: %0*lx\n", (int)((b + 3) / 4), id.bbits);

            /* print status (hit/miss) */
            switch (status_0) {
                case CACHEBLK_HIT:
                    printf(" hit");
                    break;
                case CACHEBLK_MISS_FREE:
                    printf(" miss");
                    break;
                case CACHEBLK_MISS_EVICT:
                    printf(" miss eviction");
                    break;
                default:
                    break;
            }
            switch (status_1) {
                case CACHEBLK_HIT:
                    printf(" hit");
                    break;
                case CACHEBLK_MISS_FREE:
                    printf(" miss");
                    break;
                case CACHEBLK_MISS_EVICT:
                    printf(" miss eviction");
                    break;
                default:
                    break;
            }
            printf("\n");

            /* print cache */
            if (print_cache) {
                for (int i = 0; i < cache->S; i++) {
                    printf(
                        "Cache set %d/%d (MRU=%d , LRU=%d):\n",
                        i, 
                        cache->S - 1, 
                        cacheGetMRU(cache, i)->idx, 
                        cacheGetLRU(cache, i)->idx
                    );
                    cblock_t* blk = cache->blks[i];
                    for (int j = 0; j < cache->E; j++, blk++) {
                        printf("- blk %d/%d:\t", j, cache->E - 1);
                        printf("v=%d | ", blk->valid);
                        printf("d=%d | ", blk->dirty);
                        printf("tag=%0*lx\n", (int)((MAX_ADDR_BITS - (s+b) + 3) / 4), blk->tag);
                    }
                }
            }
        }
    }
    fclose(fp);
    freeCache(cache);
    printSummary(cache->hits, cache->misses, cache->evictions);
    return 0;
}
