/* lru.c - Member 1 */
#include "algorithms.h"

void lru(const int *refs, int n, int frames, Result *r) {
    int f[MAX_FRAMES], last[MAX_FRAMES];
    for (int j = 0; j < frames; j++) { f[j] = -1; last[j] = -1; }
    r->n = n; r->frames = frames; r->hits = 0; r->faults = 0;

    for (int i = 0; i < n; i++) {
        int pos = -1;
        for (int j = 0; j < frames; j++)
            if (f[j] == refs[i]) { pos = j; break; }

        if (pos >= 0) {
            last[pos] = i;                /* hit: refresh usage time */
            r->hits++;
        } else {
            int victim = -1;
            for (int j = 0; j < frames; j++)   /* use an empty frame first */
                if (f[j] == -1) { victim = j; break; }
            if (victim < 0) {                  /* else evict least recently used */
                victim = 0;
                for (int j = 1; j < frames; j++)
                    if (last[j] < last[victim]) victim = j;
            }
            f[victim] = refs[i];
            last[victim] = i;
            r->faults++;
        }
        r->fault[i] = (pos < 0);
        for (int j = 0; j < frames; j++) r->snapshot[i][j] = f[j];
    }
}
