/*
 * common.c  --  OWNER: Integrator
 * Shared helpers: Gantt segments, metrics, queue creation, worker loop.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "common.h"

const char *algo_name(int algo)
{
    switch (algo) {
    case ALGO_FCFS:    return "FCFS";
    case ALGO_SJF_NP:  return "SJF (Non-preemptive)";
    case ALGO_SJF_P:   return "SJF (Preemptive / SRTF)";
    case ALGO_PRIO_NP: return "Priority (Non-preemptive)";
    case ALGO_PRIO_P:  return "Priority (Preemptive)";
    case ALGO_RR:      return "Round Robin";
    default:           return "Unknown";
    }
}

/* Append [start,end) for pid to the Gantt chart; merges with the previous
 * segment when it is the same process and contiguous. */
void add_segment(Response *res, int pid, int start, int end)
{
    if (end <= start)
        return;
    if (res->seg_count > 0) {
        Segment *last = &res->segs[res->seg_count - 1];
        if (last->pid == pid && last->end == start) {
            last->end = end;
            return;
        }
    }
    if (res->seg_count >= MAX_SEG) {
        fprintf(stderr, "worker: too many Gantt segments (max %d)\n", MAX_SEG);
        return;
    }
    res->segs[res->seg_count].pid   = pid;
    res->segs[res->seg_count].start = start;
    res->segs[res->seg_count].end   = end;
    res->seg_count++;
}

/* Copy the request's processes into the response and reset working fields. */
void init_response(const Request *req, Response *res)
{
    memset(res, 0, sizeof *res);
    res->algo = req->algo;
    res->n    = req->n;
    for (int i = 0; i < req->n; i++) {
        res->procs[i]            = req->procs[i];
        res->procs[i].remaining  = req->procs[i].burst;
        res->procs[i].first_run  = -1;
        res->procs[i].completion = -1;
    }
}

/* Compute WT, TAT, RT, averages and CPU utilization. */
void finalize_metrics(Response *res)
{
    double sum_wt = 0, sum_tat = 0, sum_rt = 0;
    int busy = 0, total = 0;

    for (int i = 0; i < res->n; i++) {
        Process *p = &res->procs[i];
        p->tat = p->completion - p->arrival;
        p->wt  = p->tat - p->burst;
        p->rt  = p->first_run - p->arrival;
        sum_wt  += p->wt;
        sum_tat += p->tat;
        sum_rt  += p->rt;
        busy    += p->burst;
        if (p->completion > total)
            total = p->completion;
    }
    res->total_time = total;
    if (res->n > 0) {
        res->avg_wt  = sum_wt  / res->n;
        res->avg_tat = sum_tat / res->n;
        res->avg_rt  = sum_rt  / res->n;
    }
    res->cpu_util = total > 0 ? 100.0 * busy / total : 0.0;
}

/* Create (or re-create) a message queue. Messages are msgsize bytes. */
mqd_t create_queue(const char *name, long msgsize)
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg  = 4;
    attr.mq_msgsize = msgsize;

    mq_unlink(name);                       /* remove a stale queue */
    mqd_t q = mq_open(name, O_CREAT | O_RDWR, 0600, &attr);
    if (q == (mqd_t)-1)
        perror(name);
    return q;
}

int worker_run(const char *req_queue_name, schedule_fn fn)
{
    mqd_t req_q = mq_open(req_queue_name, O_RDONLY);
    if (req_q == (mqd_t)-1) {
        perror(req_queue_name);
        return 1;
    }
    mqd_t res_q = mq_open(Q_RES, O_WRONLY);
    if (res_q == (mqd_t)-1) {
        perror(Q_RES);
        mq_close(req_q);
        return 1;
    }

    static Request  req;
    static Response res;

    for (;;) {
        ssize_t got = mq_receive(req_q, (char *)&req, sizeof req, NULL);
        if (got == -1) {
            perror("worker mq_receive");
            break;
        }
        if (req.algo == ALGO_QUIT)
            break;

        init_response(&req, &res);
        fn(&req, &res);
        finalize_metrics(&res);

        if (mq_send(res_q, (const char *)&res, sizeof res, 0) == -1) {
            perror("worker mq_send");
            break;
        }
    }

    mq_close(req_q);
    mq_close(res_q);
    return 0;
}

