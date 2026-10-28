/* optimal.c - Member 2 */
#include "algorithms.h"

void optimal(const int *refs, int n, int frames, Result *r) {
    int f[MAX_FRAMES];
    for (int j = 0; j < frames; j++) f[j] = -1;
    r->n = n; r->frames = frames; r->hits = 0; r->faults = 0;

    for (int i = 0; i < n; i++) {
        int hit = 0;
        for (int j = 0; j < frames; j++)
            if (f[j] == refs[i]) { hit = 1; break; }

        if (hit) {
            r->hits++;
        } else {
            int victim = -1;
            for (int j = 0; j < frames; j++)
                if (f[j] == -1) { victim = j; break; }
            if (victim < 0) {
                int farthest = -1;
                for (int j = 0; j < frames; j++) {
                    int nxt = n;                     /* n = never used again */
                    for (int k = i + 1; k < n; k++)
                        if (refs[k] == f[j]) { nxt = k; break; }
                    if (nxt > farthest) { farthest = nxt; victim = j; }
                }
            }
            f[victim] = refs[i];
            r->faults++;
        }
        r->fault[i] = !hit;
        for (int j = 0; j < frames; j++) r->snapshot[i][j] = f[j];
    }
}
