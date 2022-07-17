/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include <stdlib.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    
}

// void transpose_block(int M, int N, int A[N][M], int B[M][N],
//                        int si, int sj, int block_h, int block_w) 
// {
//     /* Block with top-left corner (i, j)*/
//     for (int i = si; i < si + block_h; i++) {
//         for (int j = sj; j < sj + block_w; j++)
//         {
//             B[j][i] = A[i][j];
//         }
//     }
// }

void swap(int* a, int* b) {
    int tmp = *b;
    *b = *a;
    *a = tmp;
}

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    // return;
    if (M == 32 && N == 32) {
        /* Transpose off-diag blocks, store in B */
        for (int si = 0; si < 32; si += MM_BSIZE) {
            for (int sj = 0; sj < 32; sj += MM_BSIZE) {
                if (si == sj) continue; 
                /* Transpose block */
                for (int i = si; i < si + MM_BSIZE; i++) {
                    for (int j = sj; j < sj + MM_BSIZE; j++) {
                        B[j][i] = A[i][j];
                    }
                }
            }
        }
        /* Transpose diag blocks, store in mismatched blocks in B */
        // 0 <-> 16, 8 <-> 24
        for (int si = 0; si < 32; si += MM_BSIZE) {
            int ti = 32 - MM_BSIZE - si;
            for (int i = 0; i < MM_BSIZE; i++)
                for (int j = 0; j < MM_BSIZE; j++)
                    B[ti+j][ti+i] = A[si+i][si+j];
        }
        /* Swap mismatched blocks of B */
        // 0 <-> 16, 8 <-> 24
        for (int si = 32 / 2; si < 32; si += MM_BSIZE) {
            int ti = 32 - MM_BSIZE - si;
            for (int i = 0; i < MM_BSIZE; i++)
                for (int j = 0; j < MM_BSIZE; j++)
                    swap(&B[si+i][si+j], &B[ti+i][ti+j]);                    
        }

    
    } else if (M == 64 && N == 64) {
        /* Transpose off-diag blocks */

        /* current A block (si, sj) and next A block (nsi, nsj)
         * current B block (sj, si) and next B block (nsj, nsi) */
        int si = 0, sj = 0, nsi, nsj;
        // for (nsi = 0; nsi < 64; nsi += MM_BSIZE) {
        //     for (nsj = 0; nsj < 64; nsj += MM_BSIZE) 
        for (nsj = 0; nsj < 64; nsj += MM_BSIZE) {
            for (nsi = 0; nsi < 64; nsi += MM_BSIZE) 
            {
                // if (nsi == 0 && nsj == 0)
                //     nsj = MM_BSIZE;  // init nsi, nsj
                if (nsi == 0 && nsj == 0)
                    nsi = MM_BSIZE;  // init nsi, nsj
                
                if (
                // 1 || 
                si != sj) {
                    /* Transpose 4 mini-blocks */

                    /* mini-block #0
                    * i = si..si+3, j = sj..sj+3 */
                    for (int i = si; i < si + MM_MINIBSIZE; i++)
                        for (int j = sj; j < sj + MM_MINIBSIZE; j++)
                            B[j][i] = A[i][j];
                    // for (int i = 0; i < MM_MINIBSIZE; i++)
                    //     for (int j = 0; j < MM_MINIBSIZE; j++)
                    //         B[sj + j][si + i] = A[si + i][sj + j];

                    /* mini-block #1 (STAGE ANSWER TO MINI-BLOCK #0 OF NEXT B BLOCK)
                    * i = si..si+3, j = sj+4..sj+7 */
                    for (int i = 0; i < MM_MINIBSIZE; i++)
                        for (int j = 0; j < MM_MINIBSIZE; j++)
                            B[nsj + j][nsi + i] = A[si + i][sj+MM_MINIBSIZE + j];

                    /* mini-block #2
                    * i = si+4..si+7, j = sj..sj+3 */
                    for (int i = si + MM_MINIBSIZE; i < si + MM_BSIZE; i++)
                        for (int j = sj; j < sj + MM_MINIBSIZE; j++)
                            B[j][i] = A[i][j];
                    // for (int i = 0; i < MM_MINIBSIZE; i++)
                    //     for (int j = 0; j < MM_MINIBSIZE; j++)
                    //         B[sj + j][si+MM_MINIBSIZE + i] = A[si+MM_MINIBSIZE + i][sj + j];

                    /* mini-block #3
                    * i = si+4..si+7, j = sj+4..sj+7 */
                    for (int i = si + MM_MINIBSIZE; i < si + MM_BSIZE; i++)
                        for (int j = sj + MM_MINIBSIZE; j < sj + MM_BSIZE; j++)
                            B[j][i] = A[i][j];
                    // for (int i = 0; i < MM_MINIBSIZE; i++)
                    //     for (int j = 0; j < MM_MINIBSIZE; j++)
                    //         B[sj+MM_MINIBSIZE + j][si+MM_MINIBSIZE + i] = A[si+MM_MINIBSIZE + i][sj+MM_MINIBSIZE + j];

                    /* Move staged mini-block #1 
                    * from  next B block (nsj, nsi)
                    * to current B block (sj+MM_MINIBSIZE, si) */
                    for (int i = 0; i < MM_MINIBSIZE; i++)
                        for (int j = 0; j < MM_MINIBSIZE; j++)
                            B[sj+MM_MINIBSIZE + i][si + j] = B[nsj + i][nsi + j];
                }
                /* Update si, sj */
                si = nsi;
                sj = nsj;
            }
        }
        /* Transpose diag blocks */
        // 0 <-> 16, 8 <-> 24
        // for (int si = 0; si < 64; si += MM_BLOCKSIZE_64) {
        //     int ti = 64 - MM_BLOCKSIZE_64 - si;
        //     for (int i = 0; i < MM_BLOCKSIZE_64; i++)
        //         for (int j = 0; j < MM_BLOCKSIZE_64; j++)
        //             B[ti+j][ti+i] = A[si+i][si+j];
        // }
        // /* Swap */
        // // 0 <-> 16, 8 <-> 24
        // for (int si = 64 / 2; si < 64; si += MM_BLOCKSIZE_64) {
        //     int ti = 64 - MM_BLOCKSIZE_64 - si;
        //     for (int i = 0; i < MM_BLOCKSIZE_64; i++)
        //         for (int j = 0; j < MM_BLOCKSIZE_64; j++)
        //             swap(&B[si+i][si+j], &B[ti+i][ti+j]);                    
        // }
    } else {
        trans(M, N, A, B);
    }


}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 


/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

