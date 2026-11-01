/*
 * common.h  --  OWNER: Integrator
 *
 * Shared definitions for the CPU scheduling simulator.
 * Every process in the project (the main program and the three worker
 * programs) includes this file, so DO NOT change a struct or a constant
 * without telling the whole team.
 *
 * IPC: POSIX message queues only.
 *   main  --(Request)-->  Q_REQ_xxx  -->  worker
 *   main  <--(Response)-- Q_RES      <--  worker
 *
 * WHAT A TEAMMATE'S ALGORITHM FUNCTION MUST DO
 *   The function receives a Response whose procs[] is already filled in
 *   (pid, arrival, burst, priority, remaining = burst, first_run = -1,
 *   completion = -1).  It must:
 *     1. call add_segment(res, pid, start, end) for every stretch of CPU
 *        time, in time order, starting at time 0.  Use IDLE_PID for time
 *        when the CPU has nothing to run (including idle time at the start).
 *     2. set procs[i].first_run  = time the process first got the CPU
 *     3. set procs[i].completion = time the process finished
 *   Waiting/turnaround/response time, averages and CPU utilization are
 *   computed afterwards by finalize_metrics() -- do not compute them.
 *
 * Convention: a LOWER priority number means a HIGHER priority.
 */
#ifndef COMMON_H
#define COMMON_H

#include <mqueue.h>

#define MAX_P     10     /* maximum number of processes            */
#define MAX_SEG   400    /* maximum number of Gantt chart segments */
#define IDLE_PID  (-1)   /* pid used for idle segments             */

#define Q_REQ_FCFS_RR "/sched_req_fcfs_rr"   /* FCFS + Round Robin worker */
#define Q_REQ_SJF     "/sched_req_sjf"       /* SJF worker                */
#define Q_REQ_PRIO    "/sched_req_prio"      /* Priority worker           */
#define Q_RES         "/sched_res"           /* all replies               */

typedef enum {
    ALGO_FCFS = 1,
    ALGO_SJF_NP,
    ALGO_SJF_P,
    ALGO_PRIO_NP,
    ALGO_PRIO_P,
    ALGO_RR,
    ALGO_QUIT            /* tells a worker to exit */
} Algo;

typedef struct {
    int pid;
    int arrival;
    int burst;
    int priority;
    /* filled in by the algorithm / finalize_metrics */
    int remaining;
    int first_run;
    int completion;
    int wt;              /* waiting time    = tat - burst          */
    int tat;             /* turnaround time = completion - arrival */
    int rt;              /* response time   = first_run - arrival  */
} Process;

typedef struct {
    int pid;             /* IDLE_PID for idle */
    int start;
    int end;
} Segment;

/* message: main -> worker */
typedef struct {
    int algo;
    int n;
    int quantum;         /* used by Round Robin only */
    Process procs[MAX_P];
} Request;

/* message: worker -> main */
typedef struct {
    int algo;
    int n;
    int seg_count;
    int total_time;      /* time at which the last process finished */
    double avg_wt;
    double avg_tat;
    double avg_rt;
    double cpu_util;     /* percent */
    Process procs[MAX_P];
    Segment segs[MAX_SEG];
} Response;

/* default Linux limit for a message queue message is 8192 bytes */
_Static_assert(sizeof(Response) <= 8192, "Response too large for a POSIX message");

typedef void (*schedule_fn)(const Request *req, Response *res);

/* ---- helpers implemented in common.c ---- */
const char *algo_name(int algo);
void add_segment(Response *res, int pid, int start, int end);
void init_response(const Request *req, Response *res);
void finalize_metrics(Response *res);
mqd_t create_queue(const char *name, long msgsize);

/* Worker main loop: wait for a Request, run fn, send the Response back.
 * A worker's main() is just:  return worker_run(Q_REQ_xxx, my_schedule);  */
int worker_run(const char *req_queue_name, schedule_fn fn);

#endif

