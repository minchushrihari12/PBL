/*
 * fcfs_rr_worker.c  --  OWNER: Teammate 1
 * Algorithms: FCFS and Round Robin (configurable quantum).
 *
 * This is a separate program.  It waits on its request queue, runs the
 * requested algorithm and sends the result back (all done by worker_run()).
 * You only write the algorithms below.
 */
#include <stdio.h>
#include "common.h"

/* ------------------------------------------------------------------ */
/* FCFS: run processes in order of arrival (ties: input order).        */
/* ------------------------------------------------------------------ */
static void fcfs(Response *res)
{
    int n = res->n;
    int done[MAX_P] = {0};
    int time = 0;

    for (int count = 0; count < n; count++) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (done[i])
                continue;
            if (idx == -1 || res->procs[i].arrival < res->procs[idx].arrival)
                idx = i;
        }
        Process *p = &res->procs[idx];

        if (p->arrival > time) {                 /* CPU idle until it arrives */
            add_segment(res, IDLE_PID, time, p->arrival);
            time = p->arrival;
        }
        p->first_run = time;
        add_segment(res, p->pid, time, time + p->burst);
        time += p->burst;
        p->remaining = 0;
        p->completion = time;
        done[idx] = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Round Robin                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    int items[MAX_P];
    int head;
    int count;
} Queue;

static void q_push(Queue *q, int v)
{
    q->items[(q->head + q->count) % MAX_P] = v;
    q->count++;
}

static int q_pop(Queue *q)
{
    int v = q->items[q->head];
    q->head = (q->head + 1) % MAX_P;
    q->count--;
    return v;
}

/* move every process that has arrived by 'time' into the ready queue */
static void admit(const Response *res, const int *order, int *next,
                  int time, Queue *q)
{
    while (*next < res->n && res->procs[order[*next]].arrival <= time) {
        q_push(q, order[*next]);
        (*next)++;
    }
}

static void round_robin(const Request *req, Response *res)
{
    int n = res->n;
    int quantum = req->quantum > 0 ? req->quantum : 1;
    int order[MAX_P];

    /* process indexes sorted by arrival time (stable insertion sort) */
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && res->procs[order[j]].arrival > res->procs[key].arrival) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    Queue ready = { {0}, 0, 0 };
    int next = 0, time = 0, finished = 0;

    admit(res, order, &next, time, &ready);

    while (finished < n) {
        if (ready.count == 0) {                  /* nothing ready: CPU idle */
            int t = res->procs[order[next]].arrival;
            add_segment(res, IDLE_PID, time, t);
            time = t;
            admit(res, order, &next, time, &ready);
            continue;
        }

        int idx = q_pop(&ready);
        Process *p = &res->procs[idx];
        if (p->first_run == -1)
            p->first_run = time;

        int run = p->remaining < quantum ? p->remaining : quantum;
        add_segment(res, p->pid, time, time + run);
        time += run;
        p->remaining -= run;

        /* processes that arrived during this slice go in BEFORE the
         * preempted process is put back at the tail */
        admit(res, order, &next, time, &ready);

        if (p->remaining > 0) {
            q_push(&ready, idx);
        } else {
            p->completion = time;
            finished++;
        }
    }
}

/* ------------------------------------------------------------------ */
static void schedule(const Request *req, Response *res)
{
    switch (req->algo) {
    case ALGO_FCFS: fcfs(res);              break;
    case ALGO_RR:   round_robin(req, res);  break;
    default:
        fprintf(stderr, "fcfs_rr_worker: unsupported algorithm %d\n", req->algo);
    }
}

int main(void)
{
    return worker_run(Q_REQ_FCFS_RR, schedule);
}
