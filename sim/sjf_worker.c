/*
 * sjf_worker.c  --  OWNER: Teammate 2
 * Algorithms: SJF non-preemptive and SJF preemptive (SRTF).
 *
 * This is a separate program.  It waits on its request queue, runs the
 * requested algorithm and sends the result back (all done by worker_run()).
 * You only write the algorithms below.
 */
#include <stdio.h>
#include "common.h"

/* earliest arrival among unfinished processes that have not arrived yet;
 * returns -1 if there is none */
static int next_arrival(const Response *res, int time)
{
    int best = -1;
    for (int i = 0; i < res->n; i++) {
        const Process *p = &res->procs[i];
        if (p->remaining > 0 && p->arrival > time &&
            (best == -1 || p->arrival < best))
            best = p->arrival;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* SJF non-preemptive: when the CPU is free, run the arrived process   */
/* with the shortest burst (ties: earlier arrival, then input order).  */
/* ------------------------------------------------------------------ */
static void sjf_nonpreemptive(Response *res)
{
    int n = res->n, time = 0, done = 0;

    while (done < n) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            const Process *p = &res->procs[i];
            if (p->remaining <= 0 || p->arrival > time)
                continue;
            if (idx == -1 ||
                p->burst < res->procs[idx].burst ||
                (p->burst == res->procs[idx].burst &&
                 p->arrival < res->procs[idx].arrival))
                idx = i;
        }

        if (idx == -1) {                         /* nothing has arrived: idle */
            int t = next_arrival(res, time);
            add_segment(res, IDLE_PID, time, t);
            time = t;
            continue;
        }

        Process *p = &res->procs[idx];
        p->first_run = time;
        add_segment(res, p->pid, time, time + p->burst);
        time += p->burst;
        p->remaining = 0;
        p->completion = time;
        done++;
    }
}

/* ------------------------------------------------------------------ */
/* SJF preemptive (SRTF): at every time unit run the arrived process   */
/* with the least remaining time.  On a tie, keep the running process; */
/* otherwise prefer the earlier arrival.                               */
/* ------------------------------------------------------------------ */
static void sjf_preemptive(Response *res)
{
    int n = res->n, time = 0, done = 0, last = -1;

    while (done < n) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            const Process *p = &res->procs[i];
            if (p->remaining <= 0 || p->arrival > time)
                continue;
            if (idx == -1) {
                idx = i;
                continue;
            }
            const Process *b = &res->procs[idx];
            if (p->remaining < b->remaining ||
                (p->remaining == b->remaining &&
                 (i == last || (idx != last && p->arrival < b->arrival))))
                idx = i;
        }

        if (idx == -1) {                         /* idle */
            int t = next_arrival(res, time);
            add_segment(res, IDLE_PID, time, t);
            time = t;
            last = -1;
            continue;
        }

        Process *p = &res->procs[idx];
        if (p->first_run == -1)
            p->first_run = time;
        add_segment(res, p->pid, time, time + 1);
        p->remaining--;
        time++;
        last = idx;
        if (p->remaining == 0) {
            p->completion = time;
            done++;
        }
    }
}

/* ------------------------------------------------------------------ */
static void schedule(const Request *req, Response *res)
{
    switch (req->algo) {
    case ALGO_SJF_NP: sjf_nonpreemptive(res); break;
    case ALGO_SJF_P:  sjf_preemptive(res);    break;
    default:
        fprintf(stderr, "sjf_worker: unsupported algorithm %d\n", req->algo);
    }
}

int main(void)
{
    return worker_run(Q_REQ_SJF, schedule);
}
