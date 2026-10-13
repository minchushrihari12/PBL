#ifndef SCHEDULER_H
#define SCHEDULER_H

/* Scheduling logic for Core. Pure calculation: no queues, no printing.
   Core calls run_schedule(), then sends the results to the UI as messages.
   Build Core with:  gcc core.c scheduler.c -o core -lrt -pthread          */

#include <stddef.h>
#include "ipc_defs.h"       /* algo_t, IDLE_PID, TEXT_LEN */

#define MAX_PROCS 100
#define MAX_SEGS  1000      /* max Gantt blocks (UI stores up to 1000)     */

/* Rule used everywhere: SMALLER priority number = HIGHER priority.        */

typedef struct { int pid, arrival, burst, priority; } proc_t;    /* input  */
typedef struct { int pid, start, end; }               seg_t;     /* Gantt block; pid = IDLE_PID when idle */
typedef struct { int pid, wt, tat, rt; }              metric_t;  /* per process, same order as input */

typedef struct {
    seg_t    segs[MAX_SEGS];
    int      nseg;
    metric_t m[MAX_PROCS];
    int      nm;
    double   avg_wt, avg_tat, cpu_util;   /* cpu_util in percent */
    int      total_time;
} sched_result_t;

/* Optional: called for each scheduling event ("t=5 P2 started").
   Core passes a function that sends a LOG_INFO message. May be NULL.      */
typedef void (*sched_log_fn)(const char *text);

/* Returns 0 on success. On bad input returns -1 and writes a message in err.
   quantum is only used by ALGO_RR (must be >= 1 for RR).                  */
int run_schedule(const proc_t *procs, int n, algo_t algo, int quantum,
                 sched_result_t *out, sched_log_fn log,
                 char *err, size_t errlen);

#endif /* SCHEDULER_H */
