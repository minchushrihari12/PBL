#include <stdio.h>
#include <string.h>
#include "scheduler.h"

static void seterr(char *err, size_t len, const char *msg) {
    if (err && len) { strncpy(err, msg, len - 1); err[len - 1] = '\0'; }
}

static void emit(sched_log_fn log, const char *fmt, int a, int b) {
    char buf[TEXT_LEN];
    if (!log) return;
    snprintf(buf, sizeof buf, fmt, a, b);
    log(buf);
}

/* Add one time unit [t, t+1) to the Gantt chart, merging with the last block
   when it is the same process and touches it. */
static int add_seg(sched_result_t *r, int pid, int t) {
    if (r->nseg > 0) {
        seg_t *l = &r->segs[r->nseg - 1];
        if (l->pid == pid && l->end == t) { l->end = t + 1; return 0; }
    }
    if (r->nseg >= MAX_SEGS) return -1;
    r->segs[r->nseg].pid = pid;
    r->segs[r->nseg].start = t;
    r->segs[r->nseg].end = t + 1;
    r->nseg++;
    return 0;
}

/* Is process i better than process j for this algorithm? */
static int better(algo_t algo, const proc_t *p, const int *rem, int i, int j, int run) {
    long ki, kj;
    switch (algo) {
    case ALGO_FCFS:    ki = p[i].arrival;  kj = p[j].arrival;  break;
    case ALGO_SJF_NP:  ki = p[i].burst;    kj = p[j].burst;    break;
    case ALGO_SJF_P:   ki = rem[i];        kj = rem[j];        break;
    case ALGO_PRIO_NP:
    case ALGO_PRIO_P:  ki = p[i].priority; kj = p[j].priority; break;
    default:           ki = kj = 0;                            break;
    }
    if (ki != kj) return ki < kj;
    if (i == run) return 1;                      /* tie: keep the running one */
    if (j == run) return 0;
    if (p[i].arrival != p[j].arrival) return p[i].arrival < p[j].arrival;
    return p[i].pid < p[j].pid;
}

int run_schedule(const proc_t *p, int n, algo_t algo, int quantum,
                 sched_result_t *out, sched_log_fn log,
                 char *err, size_t errlen)
{
    int rem[MAX_PROCS], first[MAX_PROCS], comp[MAX_PROCS];
    char started[MAX_PROCS], queued[MAX_PROCS];
    int rq[MAX_PROCS + 1], qh = 0, qt = 0, qn = 0;   /* Round Robin queue */
    int t = 0, done = 0, busy = 0, run = -1, last = -2, slice = 0;
    int nonpre = (algo == ALGO_FCFS || algo == ALGO_SJF_NP || algo == ALGO_PRIO_NP);
    int i, j;

    /* ---- validate input ---- */
    if (!p || !out) { seterr(err, errlen, "internal error: null pointer"); return -1; }
    if (n < 1)          { seterr(err, errlen, "no processes to schedule"); return -1; }
    if (n > MAX_PROCS)  { seterr(err, errlen, "too many processes (max 100)"); return -1; }
    if (algo < ALGO_FCFS || algo > ALGO_RR) { seterr(err, errlen, "unknown algorithm"); return -1; }
    if (algo == ALGO_RR && quantum < 1) { seterr(err, errlen, "time quantum must be at least 1"); return -1; }
    for (i = 0; i < n; i++) {
        if (p[i].burst < 1)   { seterr(err, errlen, "burst time must be at least 1"); return -1; }
        if (p[i].arrival < 0) { seterr(err, errlen, "arrival time cannot be negative"); return -1; }
        for (j = i + 1; j < n; j++)
            if (p[i].pid == p[j].pid) { seterr(err, errlen, "duplicate process ID"); return -1; }
    }

    memset(out, 0, sizeof *out);
    for (i = 0; i < n; i++) { rem[i] = p[i].burst; first[i] = -1; comp[i] = -1; started[i] = 0; queued[i] = 0; }

    /* ---- simulate one time unit at a time ---- */
    while (done < n) {
        int pick = -1;

        if (algo == ALGO_RR) {
            /* queue newly arrived processes (by arrival, then pid) */
            for (;;) {
                int b = -1;
                for (i = 0; i < n; i++)
                    if (!queued[i] && p[i].arrival <= t &&
                        (b == -1 || p[i].arrival < p[b].arrival ||
                         (p[i].arrival == p[b].arrival && p[i].pid < p[b].pid))) b = i;
                if (b == -1) break;
                queued[b] = 1; rq[qt] = b; qt = (qt + 1) % (MAX_PROCS + 1); qn++;
            }
            if (run == -1 && qn > 0) { run = rq[qh]; qh = (qh + 1) % (MAX_PROCS + 1); qn--; slice = 0; }
            pick = run;
        } else if (nonpre && run != -1) {
            pick = run;                                   /* non-preemptive: keep running */
        } else {
            for (i = 0; i < n; i++)
                if (p[i].arrival <= t && rem[i] > 0 &&
                    (pick == -1 || better(algo, p, rem, i, pick, run))) pick = i;
        }

        /* log events */
        if (last >= 0 && last != pick && rem[last] > 0) emit(log, "t=%d P%d preempted", t, p[last].pid);
        if (pick >= 0 && !started[pick]) {
            started[pick] = 1; first[pick] = t;
            emit(log, "t=%d P%d started", t, p[pick].pid);
        }
        if (pick == -1 && last != -1) emit(log, "t=%d CPU idle", t, 0);

        if (add_seg(out, pick >= 0 ? p[pick].pid : IDLE_PID, t) != 0) {
            seterr(err, errlen, "Gantt chart too long (too many blocks)");
            return -1;
        }
        if (pick >= 0) { rem[pick]--; busy++; }
        t++;
        last = pick;

        if (pick >= 0 && rem[pick] == 0) {
            comp[pick] = t; done++;
            emit(log, "t=%d P%d finished", t, p[pick].pid);
        }

        if (algo == ALGO_RR) {
            /* arrivals during this slice go in BEFORE the preempted process */
            for (;;) {
                int b = -1;
                for (i = 0; i < n; i++)
                    if (!queued[i] && p[i].arrival <= t &&
                        (b == -1 || p[i].arrival < p[b].arrival ||
                         (p[i].arrival == p[b].arrival && p[i].pid < p[b].pid))) b = i;
                if (b == -1) break;
                queued[b] = 1; rq[qt] = b; qt = (qt + 1) % (MAX_PROCS + 1); qn++;
            }
            if (pick >= 0) {
                slice++;
                if (rem[pick] == 0) run = -1;
                else if (slice >= quantum) {
                    rq[qt] = pick; qt = (qt + 1) % (MAX_PROCS + 1); qn++;
                    run = -1;
                } else run = pick;
            }
        } else {
            run = (pick >= 0 && rem[pick] > 0) ? pick : -1;
        }
    }

    /* ---- metrics, in input order ---- */
    {
        double sw = 0, st = 0;
        for (i = 0; i < n; i++) {
            int tat = comp[i] - p[i].arrival;
            out->m[i].pid = p[i].pid;
            out->m[i].tat = tat;
            out->m[i].wt  = tat - p[i].burst;
            out->m[i].rt  = first[i] - p[i].arrival;
            sw += out->m[i].wt; st += tat;
        }
        out->nm = n;
        out->avg_wt = sw / n;
        out->avg_tat = st / n;
        out->total_time = t;
        out->cpu_util = t > 0 ? 100.0 * busy / t : 0.0;
    }
    return 0;
}
