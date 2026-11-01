/* standalone.c - single-process baseline (same workload, direct function calls).
 * REPLACE core_exec() with your real simulator logic for a fair comparison. */
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include "ipc_defs.h"

static shm_state_t state;                  /* plain memory instead of shared memory */

static void core_exec(long id, FILE *log) {
    state.counter++; state.last_cmd = id;
    fprintf(log, "CORE|INFO|exec cmd %ld\n", id);
}

int main(void) {
    FILE *log = fopen("sim_standalone.log", "w");
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (long i = 1; i <= N_CMDS; i++) core_exec(i, log);
    fclose(log);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    struct rusage r; getrusage(RUSAGE_SELF, &r);
    double cpu = r.ru_utime.tv_sec * 1e3 + r.ru_utime.tv_usec / 1e3
               + r.ru_stime.tv_sec * 1e3 + r.ru_stime.tv_usec / 1e3;
    printf("[standalone] total run: %.2f ms\n", ms);
    printf("[standalone] cpu=%.2f ms  mem=%ld KB  cmds=%ld\n", cpu, r.ru_maxrss, state.counter);
    return 0;
}
