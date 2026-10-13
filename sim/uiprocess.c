/* ui.c : the UI process.
   Shows a text menu, sends commands to Core (queue Q_CMD) and displays
   everything Core sends back (queue Q_RES). It does no scheduling or CPU work.

   Build: gcc -Wall -Wextra ui.c -o ui -lrt -pthread

   Threads:  receiver thread : mq_receive -> prints results
             main thread     : menu and input                              */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ipc_defs.h"

#define MAX_SEGS_UI   1000
#define MAX_PROCS_UI  100

static mqd_t q_cmd, q_res;

/* ---- reply synchronisation: main thread waits until the receiver has printed the reply ---- */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv   = PTHREAD_COND_INITIALIZER;
static int waiting, got_reply;

/* ---- results collected by the receiver thread ---- */
static int g_pid[MAX_SEGS_UI], g_start[MAX_SEGS_UI], g_end[MAX_SEGS_UI], n_gantt;
static int m_pid[MAX_PROCS_UI], m_wt[MAX_PROCS_UI], m_tat[MAX_PROCS_UI], m_rt[MAX_PROCS_UI], n_metric;
static double s_wt, s_tat, s_util;

static int cur_algo = ALGO_FCFS, cur_quantum = 2;

static const char *algo_name(int a) {
    switch (a) {
    case ALGO_FCFS:    return "FCFS";
    case ALGO_SJF_NP:  return "SJF (non-preemptive)";
    case ALGO_SJF_P:   return "SJF (preemptive)";
    case ALGO_PRIO_NP: return "Priority (non-preemptive)";
    case ALGO_PRIO_P:  return "Priority (preemptive)";
    case ALGO_RR:      return "Round Robin";
    default:           return "?";
    }
}

/* ------------------------------------------------------------------ */
/* printing                                                           */
/* ------------------------------------------------------------------ */
static void print_state(const res_msg_t *r) {
    int i;
    printf("\n  PC=%u  IR=0x%08x  SP=%u  FLAGS=%u\n  ", r->pc, r->ir, r->sp, r->flags);
    for (i = 0; i < NUM_REGS; i++) printf("R%d=%u ", i, r->regs[i]);
    printf("\n");
    if (r->text[0]) printf("  (%s)\n", r->text);
}

static void print_schedule(void) {
    int i, w;
    char label[16];

    printf("\nGantt chart (%s):\n", algo_name(cur_algo));
    for (i = 0; i < n_gantt; i++) {
        w = (g_end[i] - g_start[i]) * 2;
        if (w < 5) w = 5;
        if (w > 20) w = 20;
        if (g_pid[i] == IDLE_PID) snprintf(label, sizeof label, "IDLE");
        else snprintf(label, sizeof label, "P%d", g_pid[i]);
        printf("|%-*s", w, label);
    }
    printf("|\n");
    for (i = 0; i < n_gantt; i++) {
        w = (g_end[i] - g_start[i]) * 2;
        if (w < 5) w = 5;
        if (w > 20) w = 20;
        printf("%-*d", w + 1, g_start[i]);
    }
    if (n_gantt > 0) printf("%d", g_end[n_gantt - 1]);
    printf("\n\n  PID   WT   TAT   RT\n");
    for (i = 0; i < n_metric; i++)
        printf("  P%-3d %3d   %3d  %3d\n", m_pid[i], m_wt[i], m_tat[i], m_rt[i]);
    printf("\n  Average WT : %.2f\n  Average TAT: %.2f\n  CPU utilization: %.1f%%\n", s_wt, s_tat, s_util);
}

/* ------------------------------------------------------------------ */
/* receiver thread                                                    */
/* ------------------------------------------------------------------ */
static void reply_done(void) {
    pthread_mutex_lock(&lock);
    if (waiting) { got_reply = 1; pthread_cond_signal(&cv); }
    pthread_mutex_unlock(&lock);
}

static void *receiver_thread(void *arg) {
    res_msg_t r;
    (void)arg;
    while (mq_receive(q_res, (char *)&r, sizeof r, NULL) != -1) {
        switch (r.type) {
        case RES_GANTT:
            if (n_gantt < MAX_SEGS_UI) { g_pid[n_gantt] = r.pid; g_start[n_gantt] = r.start; g_end[n_gantt] = r.end; n_gantt++; }
            break;
        case RES_METRIC:
            if (n_metric < MAX_PROCS_UI) { m_pid[n_metric] = r.pid; m_wt[n_metric] = r.wt; m_tat[n_metric] = r.tat; m_rt[n_metric] = r.rt; n_metric++; }
            break;
        case RES_SUMMARY:
            s_wt = r.avg_wt; s_tat = r.avg_tat; s_util = r.cpu_util;
            break;
        case RES_SCHED_DONE:
            print_schedule();
            n_gantt = 0; n_metric = 0;
            reply_done();
            break;
        case RES_STATE:
            print_state(&r);
            reply_done();
            break;
        case RES_HALTED:
            printf("\n  *** Program finished ***");
            print_state(&r);
            reply_done();
            break;
        case RES_ERROR:
            printf("\n  ERROR from Core: %s\n", r.text);
            reply_done();
            break;
        default:
            printf("\n  (unknown message type %d)\n", (int)r.type);
            break;
        }
        fflush(stdout);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* sending                                                            */
/* ------------------------------------------------------------------ */
/* Send one command. If expect_reply, wait (up to secs) until Core's reply is printed. */
static int send_cmd(cmd_msg_t *c, int expect_reply, int secs) {
    struct timespec ts;
    int ok = 1;

    if (expect_reply) {
        pthread_mutex_lock(&lock);
        waiting = 1; got_reply = 0;
        pthread_mutex_unlock(&lock);
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    if (mq_timedsend(q_cmd, (char *)c, sizeof *c, 0, &ts) == -1) {
        if (errno == ETIMEDOUT) printf("  Core is not responding (is it running?)\n");
        else perror("  mq_send");
        ok = 0;
    }
    if (expect_reply) {
        pthread_mutex_lock(&lock);
        if (ok) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += secs;
            while (!got_reply)
                if (pthread_cond_timedwait(&cv, &lock, &ts) == ETIMEDOUT) break;
            if (!got_reply) printf("  No reply from Core (is it running?)\n");
        }
        waiting = 0;
        pthread_mutex_unlock(&lock);
    }
    return ok;
}

static void simple_cmd(cmd_type_t type, int expect_reply, int secs) {
    cmd_msg_t c;
    memset(&c, 0, sizeof c);
    c.type = type;
    send_cmd(&c, expect_reply, secs);
}

/* ------------------------------------------------------------------ */
/* input helpers                                                      */
/* ------------------------------------------------------------------ */
static int at_eof;

static int read_line(const char *prompt, char *buf, size_t len) {
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    if (!fgets(buf, (int)len, stdin)) { at_eof = 1; return 0; }
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

static int read_int(const char *prompt, int lo, int hi, int *out) {
    char line[64]; int v;
    if (!read_line(prompt, line, sizeof line)) return 0;
    if (sscanf(line, "%d", &v) != 1 || v < lo || v > hi) { printf("  Please enter a number from %d to %d.\n", lo, hi); return 0; }
    *out = v;
    return 1;
}

/* ------------------------------------------------------------------ */
/* menu actions                                                       */
/* ------------------------------------------------------------------ */
static void add_processes(void) {
    int n, i = 1;
    char line[96];
    cmd_msg_t c;

    if (!read_int("Number of processes (1-100): ", 1, 100, &n)) return;
    simple_cmd(CMD_CLEAR_PROCESSES, 0, 0);
    while (i <= n && !at_eof) {
        int id, arr, bur, pri;
        printf("Process %d of %d", i, n);
        if (!read_line(": ID arrival burst priority > ", line, sizeof line)) return;
        if (sscanf(line, "%d %d %d %d", &id, &arr, &bur, &pri) != 4 || arr < 0 || bur < 1) {
            printf("  Enter 4 numbers (arrival >= 0, burst >= 1). Try again.\n");
            continue;
        }
        memset(&c, 0, sizeof c);
        c.type = CMD_ADD_PROCESS; c.proc_id = id; c.arrival = arr; c.burst = bur; c.priority = pri;
        send_cmd(&c, 0, 0);
        i++;
    }
}

static void choose_algorithm(void) {
    int a, q = cur_quantum;
    cmd_msg_t c;
    printf("  1) FCFS\n  2) SJF non-preemptive\n  3) SJF preemptive\n  4) Priority non-preemptive\n  5) Priority preemptive\n  6) Round Robin\n");
    if (!read_int("Algorithm (1-6): ", 1, 6, &a)) return;
    memset(&c, 0, sizeof c);
    c.type = CMD_SET_ALGO; c.algo = (algo_t)a;
    send_cmd(&c, 0, 0);
    cur_algo = a;
    if (a == ALGO_RR) {
        if (!read_int("Time quantum (1-100): ", 1, 100, &q)) return;
        memset(&c, 0, sizeof c);
        c.type = CMD_SET_QUANTUM; c.quantum = q;
        send_cmd(&c, 0, 0);
        cur_quantum = q;
    }
}

static void random_processes(void) {
    int n, i;
    cmd_msg_t c;
    if (!read_int("How many random processes (1-100): ", 1, 100, &n)) return;
    srand((unsigned)time(NULL));
    simple_cmd(CMD_CLEAR_PROCESSES, 0, 0);
    printf("  ID arrival burst priority\n");
    for (i = 1; i <= n; i++) {
        memset(&c, 0, sizeof c);
        c.type = CMD_ADD_PROCESS; c.proc_id = i;
        c.arrival = rand() % 11; c.burst = 1 + rand() % 8; c.priority = 1 + rand() % 5;
        printf("  P%-3d %5d %5d %5d\n", i, c.arrival, c.burst, c.priority);
        send_cmd(&c, 0, 0);
    }
}

static void load_program(void) {
    char name[TEXT_LEN];
    cmd_msg_t c;
    if (!read_line("Program file (e.g. sample.asm): ", name, sizeof name) || !name[0]) return;
    memset(&c, 0, sizeof c);
    c.type = CMD_LOAD;
    strncpy(c.text, name, TEXT_LEN - 1);
    send_cmd(&c, 1, 5);
}

static void print_menu(void) {
    printf("\n===== Simulator (algorithm: %s", algo_name(cur_algo));
    if (cur_algo == ALGO_RR) printf(", quantum %d", cur_quantum);
    printf(") =====\n"
           " 1) Add processes        2) Choose algorithm     3) Run schedule\n"
           " 4) Random processes     5) Load CPU program     6) CPU step\n"
           " 7) CPU run              8) CPU reset            0) Quit\n");
}

int main(void) {
    pthread_t rt;
    int choice;

    ipc_print_info("ui");
    q_cmd = IPC_OPEN_CMD(O_WRONLY);
    q_res = IPC_OPEN_RES(O_RDONLY);
    if (pthread_create(&rt, NULL, receiver_thread, NULL) != 0) { perror("pthread_create"); return 1; }

    for (;;) {
        print_menu();
        if (!read_int("Choice: ", 0, 8, &choice)) { if (at_eof) break; continue; }
        if (choice == 0) break;
        switch (choice) {
        case 1: add_processes(); break;
        case 2: choose_algorithm(); break;
        case 3: simple_cmd(CMD_RUN_SCHEDULE, 1, 10); break;
        case 4: random_processes(); break;
        case 5: load_program(); break;
        case 6: simple_cmd(CMD_STEP, 1, 5); break;
        case 7: simple_cmd(CMD_RUN, 1, 30); break;
        case 8: simple_cmd(CMD_RESET, 1, 5); break;
        }
    }

    simple_cmd(CMD_QUIT, 0, 0);
    pthread_cancel(rt);
    pthread_join(rt, NULL);
    mq_close(q_cmd);
    mq_close(q_res);
    printf("Bye.\n");
    return 0;
}
