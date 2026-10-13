/* core.c : the Core process.
   Receives commands from the UI (queue Q_CMD), runs the CPU and the process
   schedulers, sends results to the UI (Q_RES) and log events to the Logger (Q_LOG).

   Build: gcc -Wall -Wextra core.c cpu.c scheduler.c -o core -lrt -pthread

   Threads:  receiver thread : mq_receive -> puts commands in a buffer
             main thread     : executor, takes commands out and runs them
   This lets RESET or QUIT interrupt a long CPU run. */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ipc_defs.h"
#include "cpu.h"
#include "scheduler.h"

#define CMDQ_SIZE      64
#define RUN_LIMIT      100000   /* stop a CPU RUN after this many instructions */
#define LOG_EACH_INSTR 1        /* 1 = log every CPU instruction, 0 = only start/end */

static mqd_t q_cmd, q_res, q_log;

/* ---- command buffer between the two threads ---- */
static cmd_msg_t cq[CMDQ_SIZE];
static int cq_head, cq_tail, cq_count;
static pthread_mutex_t cq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cq_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  cq_not_full  = PTHREAD_COND_INITIALIZER;
static atomic_int stop_run;     /* set by receiver on RESET/QUIT to stop a long RUN */

/* ---- scheduling state ---- */
static proc_t procs[MAX_PROCS];
static int nprocs;
static algo_t cur_algo = ALGO_FCFS;
static int cur_quantum = 2;
static sched_result_t result;

static int log_dead;            /* set if the Logger stops answering */

/* ------------------------------------------------------------------ */
/* sending helpers                                                    */
/* ------------------------------------------------------------------ */
static void send_log(log_level_t lvl, const char *txt) {
    log_msg_t m;
    struct timespec ts;
    if (log_dead) return;
    memset(&m, 0, sizeof m);
    m.level = lvl;
    m.timestamp = time(NULL);
    strncpy(m.text, txt, TEXT_LEN - 1);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    if (mq_timedsend(q_log, (char *)&m, sizeof m, 0, &ts) == -1) {
        if (errno == ETIMEDOUT) {
            fprintf(stderr, "core: Logger is not reading; logging disabled (start the Logger first)\n");
            log_dead = 1;
        } else perror("core: mq_send log");
    }
}

static void send_res(const res_msg_t *r) {
    if (mq_send(q_res, (const char *)r, sizeof *r, 0) == -1) perror("core: mq_send result");
}

static void send_state(res_type_t type, const char *text) {
    res_msg_t r;
    memset(&r, 0, sizeof r);
    r.type = type;
    cpu_get_state(&r.pc, &r.ir, r.regs, &r.sp, &r.flags);
    if (text) strncpy(r.text, text, TEXT_LEN - 1);
    send_res(&r);
}

static void send_error(const char *text) {
    res_msg_t r;
    memset(&r, 0, sizeof r);
    r.type = RES_ERROR;
    strncpy(r.text, text, TEXT_LEN - 1);
    send_res(&r);
    send_log(LOG_ERROR, text);
}

static void sched_log_cb(const char *text) { send_log(LOG_INFO, text); }

/* ------------------------------------------------------------------ */
/* receiver thread                                                    */
/* ------------------------------------------------------------------ */
static void *receiver_thread(void *arg) {
    cmd_msg_t c;
    (void)arg;
    for (;;) {
        memset(&c, 0, sizeof c);
        if (mq_receive(q_cmd, (char *)&c, sizeof c, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("core: mq_receive");
            memset(&c, 0, sizeof c);
            c.type = CMD_QUIT;                 /* fatal: shut down */
        }
        if (c.type == CMD_RESET || c.type == CMD_QUIT) atomic_store(&stop_run, 1);

        pthread_mutex_lock(&cq_lock);
        while (cq_count == CMDQ_SIZE) pthread_cond_wait(&cq_not_full, &cq_lock);
        cq[cq_tail] = c;
        cq_tail = (cq_tail + 1) % CMDQ_SIZE;
        cq_count++;
        pthread_cond_signal(&cq_not_empty);
        pthread_mutex_unlock(&cq_lock);

        if (c.type == CMD_QUIT) break;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* command handlers                                                   */
/* ------------------------------------------------------------------ */
static void do_add_process(const cmd_msg_t *c) {
    char msg[TEXT_LEN];
    int i;
    if (nprocs >= MAX_PROCS) { send_error("too many processes (max 100)"); return; }
    if (c->burst < 1)        { send_error("burst time must be at least 1"); return; }
    if (c->arrival < 0)      { send_error("arrival time cannot be negative"); return; }
    for (i = 0; i < nprocs; i++)
        if (procs[i].pid == c->proc_id) { send_error("duplicate process ID"); return; }
    procs[nprocs].pid = c->proc_id;
    procs[nprocs].arrival = c->arrival;
    procs[nprocs].burst = c->burst;
    procs[nprocs].priority = c->priority;
    nprocs++;
    snprintf(msg, sizeof msg, "added process P%d (arrival %d, burst %d, priority %d)",
             c->proc_id, c->arrival, c->burst, c->priority);
    send_log(LOG_INFO, msg);
}

static void do_run_schedule(void) {
    char err[TEXT_LEN] = "", msg[TEXT_LEN];
    res_msg_t r;
    int i;

    snprintf(msg, sizeof msg, "running schedule: algorithm %d, %d processes, quantum %d",
             (int)cur_algo, nprocs, cur_quantum);
    send_log(LOG_INFO, msg);
    if (run_schedule(procs, nprocs, cur_algo, cur_quantum, &result, sched_log_cb, err, sizeof err) != 0) {
        send_error(err);
        return;
    }
    for (i = 0; i < result.nseg; i++) {
        memset(&r, 0, sizeof r);
        r.type = RES_GANTT;
        r.pid = result.segs[i].pid; r.start = result.segs[i].start; r.end = result.segs[i].end;
        send_res(&r);
    }
    for (i = 0; i < result.nm; i++) {
        memset(&r, 0, sizeof r);
        r.type = RES_METRIC;
        r.pid = result.m[i].pid; r.wt = result.m[i].wt; r.tat = result.m[i].tat; r.rt = result.m[i].rt;
        send_res(&r);
    }
    memset(&r, 0, sizeof r);
    r.type = RES_SUMMARY;
    r.avg_wt = result.avg_wt; r.avg_tat = result.avg_tat; r.cpu_util = result.cpu_util;
    send_res(&r);
    memset(&r, 0, sizeof r);
    r.type = RES_SCHED_DONE;
    send_res(&r);
    send_log(LOG_INFO, "schedule finished");
}

static void do_cpu_step(void) {
    char err[TEXT_LEN] = "";
    cpu_status_t st = cpu_step(err, sizeof err);
    if (st == CPU_ERROR) { send_error(err); return; }
    if (cpu_last_desc()[0]) send_log(LOG_INFO, cpu_last_desc());
    send_state(st == CPU_HALTED ? RES_HALTED : RES_STATE, st == CPU_HALTED ? "program halted" : NULL);
}

static void do_cpu_run(void) {
    char err[TEXT_LEN] = "", msg[TEXT_LEN];
    int n = 0;
    cpu_status_t st = CPU_OK;

    atomic_store(&stop_run, 0);
    send_log(LOG_INFO, "CPU run started");
    while (n < RUN_LIMIT && !atomic_load(&stop_run)) {
        st = cpu_step(err, sizeof err);
        if (st == CPU_ERROR) break;
        if (cpu_last_desc()[0]) {
            n++;
            if (LOG_EACH_INSTR) send_log(LOG_INFO, cpu_last_desc());
        }
        if (st == CPU_HALTED) break;
    }
    if (st == CPU_ERROR) { send_error(err); return; }
    snprintf(msg, sizeof msg, "%d instructions executed", n);
    send_log(LOG_INFO, msg);
    if (st == CPU_HALTED) send_state(RES_HALTED, msg);
    else if (atomic_load(&stop_run)) send_state(RES_STATE, "run interrupted");
    else send_error("instruction limit reached (possible infinite loop)");
}

/* ------------------------------------------------------------------ */
/* main = executor                                                    */
/* ------------------------------------------------------------------ */
int main(void) {
    pthread_t rt;
    int running = 1;

    ipc_print_info("core");
    q_cmd = IPC_OPEN_CMD(O_RDONLY);
    q_res = IPC_OPEN_RES(O_WRONLY);
    q_log = IPC_OPEN_LOG(O_WRONLY);

    if (pthread_create(&rt, NULL, receiver_thread, NULL) != 0) { perror("pthread_create"); return 1; }
    send_log(LOG_INFO, "core started");

    while (running) {
        cmd_msg_t c;
        char err[TEXT_LEN] = "", msg[TEXT_LEN];

        pthread_mutex_lock(&cq_lock);
        while (cq_count == 0) pthread_cond_wait(&cq_not_empty, &cq_lock);
        c = cq[cq_head];
        cq_head = (cq_head + 1) % CMDQ_SIZE;
        cq_count--;
        pthread_cond_signal(&cq_not_full);
        pthread_mutex_unlock(&cq_lock);

        switch (c.type) {
        case CMD_CLEAR_PROCESSES:
            nprocs = 0;
            send_log(LOG_INFO, "process list cleared");
            break;
        case CMD_ADD_PROCESS:
            do_add_process(&c);
            break;
        case CMD_SET_ALGO:
            if (c.algo < ALGO_FCFS || c.algo > ALGO_RR) { send_error("unknown algorithm"); break; }
            cur_algo = c.algo;
            snprintf(msg, sizeof msg, "algorithm set to %d", (int)cur_algo);
            send_log(LOG_INFO, msg);
            break;
        case CMD_SET_QUANTUM:
            if (c.quantum < 1) { send_error("time quantum must be at least 1"); break; }
            cur_quantum = c.quantum;
            snprintf(msg, sizeof msg, "time quantum set to %d", cur_quantum);
            send_log(LOG_INFO, msg);
            break;
        case CMD_RUN_SCHEDULE:
            do_run_schedule();
            break;
        case CMD_LOAD:
            if (cpu_load_file(c.text, err, sizeof err) != 0) { send_error(err); break; }
            snprintf(msg, sizeof msg, "program loaded: %d instructions", cpu_program_size());
            send_log(LOG_INFO, msg);
            send_state(RES_STATE, msg);
            break;
        case CMD_STEP:
            do_cpu_step();
            break;
        case CMD_RUN:
            do_cpu_run();
            break;
        case CMD_RESET:
            atomic_store(&stop_run, 0);
            cpu_reset();
            send_log(LOG_INFO, "CPU reset");
            send_state(RES_STATE, "CPU reset");
            break;
        case CMD_QUIT:
            send_log(LOG_QUIT, "core shutting down");
            running = 0;
            break;
        default:
            send_error("unknown command");
            break;
        }
    }

    pthread_join(rt, NULL);
    mq_close(q_cmd); mq_close(q_res); mq_close(q_log);
    return 0;
}
