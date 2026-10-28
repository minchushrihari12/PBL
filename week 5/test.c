/* test.c - Member 2. Native test, no browser needed.
 * gcc test.c main_wasm.c fifo.c lru.c optimal.c -o test && ./test
 */
#include <stdio.h>
#include <string.h>
#include "algorithms.h"

const char *run_simulation(const char *refs_str, int frames, int algo);

static int failures = 0;

static void check_counts(const char *refs, int frames, int ef, int el, int eo) {
    int arr[MAX_REFS], n = 0, v, off = 0, used;
    while (sscanf(refs + off, "%d%n", &v, &used) == 1) { arr[n++] = v; off += used; }
    static Result r;
    int got[3];
    fifo(arr, n, frames, &r);    got[0] = r.faults;
    lru(arr, n, frames, &r);     got[1] = r.faults;
    optimal(arr, n, frames, &r); got[2] = r.faults;
    int ok = got[0] == ef && got[1] == el && got[2] == eo;
    printf("[%s] \"%s\" frames=%d -> FIFO %d, LRU %d, Optimal %d (expected %d, %d, %d)\n",
           ok ? "PASS" : "FAIL", refs, frames, got[0], got[1], got[2], ef, el, eo);
    if (!ok) failures++;
}

static void check_error(const char *refs, int frames) {
    const char *j = run_simulation(refs, frames, 4);
    int ok = strstr(j, "\"ok\":false") != NULL;
    printf("[%s] invalid input \"%s\", frames=%d -> %.70s\n", ok ? "PASS" : "FAIL", refs, frames, j);
    if (!ok) failures++;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "json") == 0) {   /* print sample JSON for the web dev */
        puts(run_simulation("7 0 1 2 0 3 0 4 2 3", 3, 4));
        return 0;
    }
    check_counts("7 0 1 2 0 3 0 4 2 3", 3, 9, 8, 6);        /* sample from the spec */
    check_counts("1 2 3 4 1 2 5 1 2 3 4 5", 3, 9, 10, 7);   /* classic, shows Belady's anomaly */
    check_counts("1 2 3 4 1 2 5 1 2 3 4 5", 4, 10, 8, 6);
    check_counts("5 5 5 5 5", 3, 1, 1, 1);                  /* all same pages */
    check_counts("1 2 3 1 2 3", 1, 6, 6, 6);                /* one frame */
    check_counts("1 2 3 1 2 3", 5, 3, 3, 3);                /* frames > distinct pages */
    check_error("", 3);                                     /* empty */
    check_error("1 2 x", 3);                                /* non-numeric */
    check_error("1 -2 3", 3);                               /* negative */
    check_error("1 2 3", 0);                                /* frames <= 0 */
    check_error("1 2 3", 11);                               /* too many frames */
    printf(failures ? "\n%d test(s) FAILED\n" : "\nAll tests passed\n", failures);
    return failures != 0;
}
