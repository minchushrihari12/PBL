/* algorithms.h - shared contract. Do NOT change after day 1. */
#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#define MAX_REFS   100
#define MAX_FRAMES 10

typedef struct {
    int n, frames;
    int snapshot[MAX_REFS][MAX_FRAMES]; /* frame contents after each request, -1 = empty */
    int fault[MAX_REFS];                /* 1 = page fault, 0 = hit */
    int hits, faults;
} Result;

void fifo(const int *refs, int n, int frames, Result *r);     /* Member 1 */
void lru(const int *refs, int n, int frames, Result *r);      /* Member 1 */
void optimal(const int *refs, int n, int frames, Result *r);  /* Member 2 */

#endif
