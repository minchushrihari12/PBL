 /*
 * main.c  --  OWNER: Integrator
 *
 * The user interface and the coordinator of the whole simulator.
 *   - starts the three worker programs (fork + exec)
 *   - reads/generates processes, shows the menu
 *   - sends a Request to the right worker over a POSIX message queue
 *   - receives the Response and prints Gantt chart, table and metrics
 *
 * Run:   ./scheduler            interactive menu
 *        ./scheduler --demo     runs the 3 built-in test cases on all
 *                               algorithms (use it to make sample_output.txt)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "common.h"

static mqd_t q_fcfs_rr = (mqd_t)-1, q_sjf = (mqd_t)-1,
             q_prio = (mqd_t)-1, q_res = (mqd_t)-1;
static pid_t workers[3];
static int   started = 0;

/* ------------------------------------------------------------------ */
/* Worker management                                                   */
/* ------------------------------------------------------------------ */
static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void shutdown_workers(void)
{
    if (!started)
        return;
    started = 0;

    Request quit;
    memset(&quit, 0, sizeof quit);
    quit.algo = ALGO_QUIT;

    mqd_t qs[3] = { q_fcfs_rr, q_sjf, q_prio };
    for (int i = 0; i < 3; i++) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        mq_timedsend(qs[i], (const char *)&quit, sizeof quit, 0, &ts);
    }

    for (int i = 0; i < 3; i++) {
        int exited = 0;
        for (int tries = 0; tries < 20 && !exited; tries++) {
            if (waitpid(workers[i], NULL, WNOHANG) != 0)
                exited = 1;
            else
                sleep_ms(50);
        }
        if (!exited) {
            kill(workers[i], SIGKILL);
            waitpid(workers[i], NULL, 0);
        }
    }

    mq_close(q_fcfs_rr);  mq_unlink(Q_REQ_FCFS_RR);
    mq_close(q_sjf);      mq_unlink(Q_REQ_SJF);
    mq_close(q_prio);     mq_unlink(Q_REQ_PRIO);
    mq_close(q_res);      mq_unlink(Q_RES);
}

static int start_workers(void)
{
    q_fcfs_rr = create_queue(Q_REQ_FCFS_RR, sizeof(Request));
    q_sjf     = create_queue(Q_REQ_SJF,     sizeof(Request));
    q_prio    = create_queue(Q_REQ_PRIO,    sizeof(Request));
    q_res     = create_queue(Q_RES,         sizeof(Response));
    if (q_fcfs_rr == (mqd_t)-1 || q_sjf == (mqd_t)-1 ||
        q_prio == (mqd_t)-1 || q_res == (mqd_t)-1) {
        fprintf(stderr, "Could not create message queues "
                        "(POSIX message queues need Linux).\n");
        return -1;
    }

    const char *exe[3] = { "./fcfs_rr_worker", "./sjf_worker", "./prio_worker" };
    for (int i = 0; i < 3; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }
        if (pid == 0) {
            execl(exe[i], exe[i], (char *)NULL);
            perror(exe[i]);
            _exit(127);
        }
        workers[i] = pid;
    }
    started = 1;
    atexit(shutdown_workers);
    return 0;
}

static mqd_t queue_for(int algo)
{
    switch (algo) {
    case ALGO_FCFS:
    case ALGO_RR:      return q_fcfs_rr;
    case ALGO_SJF_NP:
    case ALGO_SJF_P:   return q_sjf;
    default:           return q_prio;
    }
}

/* send the request to the worker over its queue, wait for the reply */
static int run_algorithm(int algo, Request *req, Response *res)
{
    req->algo = algo;
    if (mq_send(queue_for(algo), (const char *)req, sizeof *req, 0) == -1) {
        perror("mq_send");
        return -1;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    if (mq_timedreceive(q_res, (char *)res, sizeof *res, NULL, &ts) == -1) {
        perror("mq_timedreceive (is the worker running?)");
        return -1;
    }
    if (res->algo != algo) {
        fprintf(stderr, "Unexpected reply for algorithm %d\n", res->algo);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Input helpers                                                       */
/* ------------------------------------------------------------------ */
static int read_int(const char *prompt, int lo, int hi)
{
    char buf[64];
    for (;;) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) {
            printf("\n");
            exit(0);                       /* EOF: leave (atexit cleans up) */
        }
        char *end;
        long v = strtol(buf, &end, 10);
        while (*end == ' ' || *end == '\t')
            end++;
        if (end == buf || (*end != '\n' && *end != '\0')) {
            printf("  Please enter a whole number.\n");
            continue;
        }
        if (v < lo || v > hi) {
            printf("  Value must be between %d and %d.\n", lo, hi);
            continue;
        }
        return (int)v;
    }
}

static void print_input(const Request *req)
{
    printf("\nProcesses (lower priority number = higher priority):\n");
    printf("  %-5s %-8s %-6s %-8s\n", "PID", "Arrival", "Burst", "Priority");
    for (int i = 0; i < req->n; i++)
        printf("  P%-4d %-8d %-6d %-8d\n", req->procs[i].pid,
               req->procs[i].arrival, req->procs[i].burst,
               req->procs[i].priority);
    printf("\n");
}

static void enter_processes(Request *req)
{
    memset(req->procs, 0, sizeof req->procs);
    req->n = read_int("Number of processes (1-10): ", 1, MAX_P);
    for (int i = 0; i < req->n; i++) {
        printf("--- Process %d ---\n", i + 1);
        int id, dup;
        do {
            dup = 0;
            id = read_int("  Process ID (1-99): ", 1, 99);
            for (int j = 0; j < i; j++)
                if (req->procs[j].pid == id) {
                    printf("  That ID is already used.\n");
                    dup = 1;
                }
        } while (dup);
        req->procs[i].pid      = id;
        req->procs[i].arrival  = read_int("  Arrival time (0-100): ", 0, 100);
        req->procs[i].burst    = read_int("  Burst time (1-30): ", 1, 30);
        req->procs[i].priority = read_int("  Priority (1-99, lower = higher): ", 1, 99);
    }
}

static void random_processes(Request *req)
{
    memset(req->procs, 0, sizeof req->procs);
    req->n = read_int("How many random processes (1-10): ", 1, MAX_P);
    for (int i = 0; i < req->n; i++) {
        req->procs[i].pid      = i + 1;
        req->procs[i].arrival  = rand() % 11;
        req->procs[i].burst    = 1 + rand() % 10;
        req->procs[i].priority = 1 + rand() % 5;
    }
}

/* ------------------------------------------------------------------ */
/* Built-in test cases                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *title;
    int n;
    int quantum;
    int d[MAX_P][3];            /* arrival, burst, priority */
} TestCase;

static const TestCase tests[3] = {
    { "Test case 1: staggered arrivals, no idle time", 4, 3,
      { {0, 8, 3}, {1, 4, 1}, {2, 9, 4}, {3, 5, 2} } },
    { "Test case 2: CPU idle at the start and in the middle", 5, 3,
      { {2, 6, 3}, {3, 2, 1}, {4, 4, 2}, {16, 3, 2}, {17, 1, 1} } },
    { "Test case 3: everything arrives at time 0", 5, 3,
      { {0, 10, 3}, {0, 1, 1}, {0, 2, 4}, {0, 1, 5}, {0, 5, 2} } },
};

static void load_test(Request *req, int t)
{
    memset(req->procs, 0, sizeof req->procs);
    req->n = tests[t].n;
    req->quantum = tests[t].quantum;
    for (int i = 0; i < req->n; i++) {
        req->procs[i].pid      = i + 1;
        req->procs[i].arrival  = tests[t].d[i][0];
        req->procs[i].burst    = tests[t].d[i][1];
        req->procs[i].priority = tests[t].d[i][2];
    }
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */
static void print_gantt(const Response *res)
{
    const int PER_ROW = 8;
    printf("Gantt chart:\n");

    for (int s = 0; s < res->seg_count; s += PER_ROW) {
        int e = s + PER_ROW < res->seg_count ? s + PER_ROW : res->seg_count;
        int w[8];
        char label[8][16];

        for (int k = s; k < e; k++) {
            const Segment *g = &res->segs[k];
            if (g->pid == IDLE_PID)
                snprintf(label[k - s], sizeof label[0], "IDLE");
            else
                snprintf(label[k - s], sizeof label[0], "P%d", g->pid);
            int len = (int)strlen(label[k - s]);
            int width = (g->end - g->start) * 2;
            if (width < len + 2) width = len + 2;
            if (width > 14)      width = 14;
            w[k - s] = width;
        }

        for (int pass = 0; pass < 4; pass++) {
            printf("  ");
            for (int k = s; k < e; k++) {
                int width = w[k - s];
                if (pass == 0 || pass == 2) {
                    putchar('+');
                    for (int i = 0; i < width; i++) putchar('-');
                } else if (pass == 1) {
                    int len  = (int)strlen(label[k - s]);
                    int left = (width - len) / 2;
                    printf("|%*s%s%*s", left, "", label[k - s],
                           width - len - left, "");
                } else {
                    printf("%-*d", width + 1, res->segs[k].start);
                }
            }
            if (pass == 3)
                printf("%d\n", res->segs[e - 1].end);
            else
                puts(pass == 1 ? "|" : "+");
        }
    }
}

static void print_results(const Response *res)
{
    printf("\n  %-5s %-4s %-4s %-4s %-4s %-5s %-4s %-4s\n",
           "PID", "AT", "BT", "PR", "CT", "TAT", "WT", "RT");
    int busy = 0;
    for (int i = 0; i < res->n; i++) {
        const Process *p = &res->procs[i];
        printf("  P%-4d %-4d %-4d %-4d %-4d %-5d %-4d %-4d\n", p->pid,
               p->arrival, p->burst, p->priority, p->completion,
               p->tat, p->wt, p->rt);
        busy += p->burst;
    }
    printf("\n  Average waiting time    : %.2f\n", res->avg_wt);
    printf("  Average turnaround time : %.2f\n", res->avg_tat);
    printf("  Average response time   : %.2f\n", res->avg_rt);
    printf("  CPU utilization         : %.2f%%  (busy %d, idle %d, total %d)\n",
           res->cpu_util, busy, res->total_time - busy, res->total_time);

    int bars = (int)(res->cpu_util / 5.0 + 0.5);      /* 20-char bar */
    printf("  CPU usage               : [");
    for (int i = 0; i < 20; i++) putchar(i < bars ? '#' : '.');
    printf("]\n");
}

static int run_and_show(int algo, Request *req)
{
    static Response res;
    if (run_algorithm(algo, req, &res) != 0)
        return -1;

    printf("\n==================== %s", algo_name(algo));
    if (algo == ALGO_RR)
        printf(" (quantum = %d)", req->quantum);
    printf(" ====================\n");
    print_gantt(&res);
    print_results(&res);
    return 0;
}

static void compare_all(Request *req)
{
    static Response res[ALGO_RR + 1];
    for (int a = ALGO_FCFS; a <= ALGO_RR; a++)
        if (run_algorithm(a, req, &res[a]) != 0)
            return;

    printf("\n=========== Comparison (RR quantum = %d) ===========\n", req->quantum);
    printf("  %-26s %-8s %-8s %-8s %-8s\n",
           "Algorithm", "Avg WT", "Avg TAT", "Avg RT", "CPU %");
    for (int a = ALGO_FCFS; a <= ALGO_RR; a++)
        printf("  %-26s %-8.2f %-8.2f %-8.2f %-8.2f\n", algo_name(a),
               res[a].avg_wt, res[a].avg_tat, res[a].avg_rt, res[a].cpu_util);
}

/* ------------------------------------------------------------------ */
static void demo(void)
{
    Request req;
    memset(&req, 0, sizeof req);
    for (int t = 0; t < 3; t++) {
        load_test(&req, t);
        printf("\n\n##########################################################\n");
        printf("# %s\n", tests[t].title);
        printf("##########################################################\n");
        print_input(&req);
        for (int a = ALGO_FCFS; a <= ALGO_RR; a++)
            run_and_show(a, &req);
        compare_all(&req);
    }
}

static void print_menu(void)
{
    printf("\n========== CPU Scheduling Simulator ==========\n");
    printf("  1. Enter processes manually\n");
    printf("  2. Generate random processes\n");
    printf("  3. Load a built-in test case (1-3)\n");
    printf("  4. Show current processes\n");
    printf("  5. Run FCFS\n");
    printf("  6. Run SJF (non-preemptive)\n");
    printf("  7. Run SJF (preemptive)\n");
    printf("  8. Run Priority (non-preemptive)\n");
    printf("  9. Run Priority (preemptive)\n");
    printf(" 10. Run Round Robin\n");
    printf(" 11. Compare all algorithms\n");
    printf("  0. Exit\n");
}

int main(int argc, char **argv)
{
    srand((unsigned)time(NULL));

    if (start_workers() != 0)
        return 1;

    Request req;
    memset(&req, 0, sizeof req);
    req.quantum = 2;

    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        demo();
        return 0;
    }

    for (;;) {
        print_menu();
        int c = read_int("Choice: ", 0, 11);

        if (c == 0)
            break;
        if (c == 1) {
            enter_processes(&req);
            print_input(&req);
        } else if (c == 2) {
            random_processes(&req);
            print_input(&req);
        } else if (c == 3) {
            int t = read_int("Test case (1-3): ", 1, 3);
            load_test(&req, t - 1);
            printf("\n%s\n", tests[t - 1].title);
            print_input(&req);
        } else if (c == 4) {
            if (req.n == 0) printf("\nNo processes yet.\n");
            else            print_input(&req);
        } else {
            if (req.n == 0) {
                printf("\nNo processes yet. Use option 1, 2 or 3 first.\n");
                continue;
            }
            if (c == 11) {
                req.quantum = read_int("Round Robin time quantum (1-20): ", 1, 20);
                compare_all(&req);
            } else {
                int algo = c - 4;                 /* 5..10 -> 1..6 */
                if (algo == ALGO_RR)
                    req.quantum = read_int("Time quantum (1-20): ", 1, 20);
                run_and_show(algo, &req);
            }
        }
    }
    return 0;
}

