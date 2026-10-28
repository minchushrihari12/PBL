/* fifo.c - Member 1 */
#include "algorithms.h"

void fifo(const int *refs, int n, int frames, Result *r) {
    int f[MAX_FRAMES], next = 0;
    for (int j = 0; j < frames; j++) f[j] = -1;
    r->n = n; r->frames = frames; r->hits = 0; r->faults = 0;

    for (int i = 0; i < n; i++) {
        int hit = 0;
        for (int j = 0; j < frames; j++)
            if (f[j] == refs[i]) { hit = 1; break; }

        if (hit) {
            r->hits++;
        } else {
            f[next] = refs[i];            /* oldest slot (or next empty one) */
            next = (next + 1) % frames;
            r->faults++;
        }
        r->fault[i] = !hit;
        for (int j = 0; j < frames; j++) r->snapshot[i][j] = f[j];
    }
}
