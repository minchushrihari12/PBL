#ifndef IPC_DEFS_H
#define IPC_DEFS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>      /* O_RDONLY, O_WRONLY, O_CREAT */
#include <mqueue.h>     /* POSIX message queues        */

/* Version: bump this number whenever the file changes.
   Every program prints it at start-up so mismatches are easy to spot. */
#define IPC_VERSION 2

/* ---- Queue names (POSIX message queues must start with '/') ---- */
#define Q_CMD  "/sim_cmd"   /* UI   -> Core   : commands            */
#define Q_RES  "/sim_res"   /* Core -> UI     : state and results   */
#define Q_LOG  "/sim_log"   /* Core -> Logger : log events          */

/* ---- Queue limits ---- */
#define Q_MAXMSG   10       /* Linux default limit without root */

/* ---- Sizes: CHANGE NUM_REGS to match your simulator ---- */
#define NUM_REGS   8
#define TEXT_LEN   128
#define IDLE_PID   (-1)     /* pid used in a Gantt segment when the CPU is idle */

/* ---- Scheduling algorithms (Week 3) ---- */
typedef enum {
    ALGO_FCFS = 1,
    ALGO_SJF_NP,        /* SJF non-preemptive       */
    ALGO_SJF_P,         /* SJF preemptive (SRTF)    */
    ALGO_PRIO_NP,       /* Priority non-preemptive  */
    ALGO_PRIO_P,        /* Priority preemptive      */
    ALGO_RR             /* Round Robin (uses quantum) */
} algo_t;

/* =====================  UI -> Core  (queue Q_CMD)  ===================== */
typedef enum {
    /* CPU simulator commands (Weeks 1-2) */
    CMD_LOAD = 1,       /* load program, filename in text */
    CMD_RUN,
    CMD_STEP,
    CMD_RESET,
    CMD_QUIT,
    /* Scheduling commands (Week 3) */
    CMD_CLEAR_PROCESSES,/* forget all processes added so far       */
    CMD_ADD_PROCESS,    /* add one process: proc_id/arrival/burst/priority */
    CMD_SET_ALGO,       /* choose algorithm: algo                  */
    CMD_SET_QUANTUM,    /* Round Robin time quantum: quantum       */
    CMD_RUN_SCHEDULE    /* run the chosen algorithm on all processes */
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    char       text[TEXT_LEN];   /* filename for CMD_LOAD, else unused */
    /* used by CMD_ADD_PROCESS */
    int32_t    proc_id;
    int32_t    arrival;
    int32_t    burst;
    int32_t    priority;
    /* used by CMD_SET_ALGO / CMD_SET_QUANTUM */
    algo_t     algo;
    int32_t    quantum;
} cmd_msg_t;

/* =====================  Core -> UI  (queue Q_RES)  ===================== */
typedef enum {
    RES_STATE = 1,      /* CPU state update (pc, ir, regs...)          */
    RES_HALTED,         /* CPU program finished                        */
    RES_ERROR,          /* something went wrong, see text              */
    RES_GANTT,          /* one Gantt segment: pid, start, end          */
    RES_METRIC,         /* one process's results: pid, wt, tat, rt     */
    RES_SUMMARY,        /* averages: avg_wt, avg_tat, cpu_util         */
    RES_SCHED_DONE      /* last message of a schedule run              */
} res_type_t;

typedef struct {
    res_type_t type;
    /* CPU state (RES_STATE) */
    uint32_t   pc;
    uint32_t   ir;
    uint32_t   regs[NUM_REGS];
    uint32_t   sp;
    uint32_t   flags;
    /* Gantt segment (RES_GANTT): pid is IDLE_PID when CPU was idle */
    int32_t    pid;
    int32_t    start;
    int32_t    end;
    /* Per-process metrics (RES_METRIC) uses pid plus these */
    int32_t    wt;              /* waiting time    */
    int32_t    tat;             /* turnaround time */
    int32_t    rt;              /* response time   */
    /* Summary (RES_SUMMARY) */
    double     avg_wt;
    double     avg_tat;
    double     cpu_util;        /* percent, 0 to 100 */
    char       text[TEXT_LEN];  /* error / info text */
} res_msg_t;

/* =====================  Core -> Logger  (queue Q_LOG)  ================= */
typedef enum {
    LOG_INFO = 1,
    LOG_ERROR,
    LOG_QUIT            /* tells Logger to flush and exit */
} log_level_t;

typedef struct {
    log_level_t level;
    time_t      timestamp;
    char        text[TEXT_LEN];  /* e.g. "t=5 P2 started", "t=8 P2 finished" */
} log_msg_t;

/* =====================  Shared message-queue helpers  ================== */
/* Everyone uses these, so queue sizes can never disagree between programs.
   Do NOT write your own open_q(); use the macros below.
   Compile with:  gcc file.c -o file -lrt -pthread                          */

/* Opens (and creates, if needed) a queue. Exits with a message on failure. */
static inline mqd_t ipc_open_q(const char *name, int flags, size_t msgsize) {
    struct mq_attr a = {0};
    a.mq_maxmsg  = Q_MAXMSG;
    a.mq_msgsize = (long)msgsize;
    mqd_t q = mq_open(name, flags | O_CREAT, 0644, &a);
    if (q == (mqd_t)-1) { perror("mq_open"); exit(1); }
    return q;
}

/* flags = O_RDONLY (receive only) or O_WRONLY (send only) */
#define IPC_OPEN_CMD(flags)  ipc_open_q(Q_CMD, (flags), sizeof(cmd_msg_t))
#define IPC_OPEN_RES(flags)  ipc_open_q(Q_RES, (flags), sizeof(res_msg_t))
#define IPC_OPEN_LOG(flags)  ipc_open_q(Q_LOG, (flags), sizeof(log_msg_t))

/* Call once at start-up of every program. All three must print the same
   version and the same sizes, otherwise someone has an old header. */
static inline void ipc_print_info(const char *who) {
    printf("[%s] IPC_VERSION=%d  sizeof cmd=%zu res=%zu log=%zu\n", who,
           IPC_VERSION, sizeof(cmd_msg_t), sizeof(res_msg_t), sizeof(log_msg_t));
}

#endif /* IPC_DEFS_H */
