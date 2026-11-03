/* UI process (message queues + 2 threads) - MENU VERSION.
 * Owner: Student 1.  Input thread -> mq_send(MQ_CMD); display thread <- mq_receive(MQ_REPLY).
 * Interactive: numbered menu.   Scripted (used by tests): UI_AUTO=1 ./integrator
 * Student 1: read this file, understand it, and extend it (colours, history, validation...). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include "ctrl_util.h"
#include "sim_state.h"

#define MAX_SEQ 4096

static mqd_t q_cmd, q_rep;
static sim_state_t *st;
static double t_sent[MAX_SEQ];
static atomic_int sent, received, errors_seen, input_done;
static double lat_total;                      /* written by display thread only */

/* ---------- display of shared state (read from shared memory under lock) ---------- */
static void draw_state(void) {
    char stk[160] = "", que[160] = "", mem[200] = "";
    pthread_mutex_lock(&st->lock);
    for (int i = 0; i < st->sp; i++)
        snprintf(stk + strlen(stk), sizeof stk - strlen(stk), "%ld ", st->stack[i]);
    for (int i = 0; i < st->qcount; i++)
        snprintf(que + strlen(que), sizeof que - strlen(que), "%ld ",
                 st->queue[(st->qhead + i) % QUEUE_MAX]);
    int shown = 0;
    for (int i = 0; i < MEM_WORDS && shown < 8; i++)
        if (st->mem[i]) { snprintf(mem + strlen(mem), sizeof mem - strlen(mem), "[%d]=%ld ", i, st->mem[i]); shown++; }
    printf("\n  +------------------- SIMULATOR STATE -------------------\n");
    printf("  | CPU     pc=%d  acc=%ld\n", st->cpu.pc, st->cpu.acc);
    printf("  | STACK   [ %s]  top-last  (%d/%d)\n", stk, st->sp, STACK_MAX);
    printf("  | QUEUE   front-> [ %s]  (%d/%d)\n", que, st->qcount, QUEUE_MAX);
    printf("  | MEMORY  %s%s\n", shown ? mem : "(all zero)", shown == 8 ? "..." : "");
    printf("  | commands executed=%ld  errors=%ld\n", st->cmds_done, st->errors);
    printf("  +--------------------------------------------------------\n\n");
    pthread_mutex_unlock(&st->lock);
    fflush(stdout);
}

/* ---------- sending commands ---------- */
static void send_cmd(const char *text) {
    int seq = atomic_fetch_add(&sent, 1) + 1;
    if (seq >= MAX_SEQ) { fprintf(stderr, "too many commands\n"); return; }
    char buf[MQ_MSG_SZ];
    snprintf(buf, sizeof buf, "%d %s", seq, text);
    printf("-> %s\n", buf); fflush(stdout);      /* print BEFORE sending so the reply never appears first */
    t_sent[seq] = now_us();
    mq_send(q_cmd, buf, strlen(buf) + 1, 0);
}

/* wait (max 2 s) until every sent command has been answered, so output stays tidy */
static void wait_replies(void) {
    for (int i = 0; i < 2000 && atomic_load(&received) < atomic_load(&sent); i++) usleep(1000);
}

static void run_script(void) {
    static const char *script[] = {
        "PUSH 1", "PUSH 2", "PUSH 3", "PUSH 4", "PUSH 5", "POP", "POP",
        "ENQ 10", "ENQ 11", "ENQ 12", "ENQ 13", "ENQ 14", "DEQ", "DEQ",
        "LOAD 0 5", "STEP", "STEP", "STEP", "BAD", NULL };
    for (int i = 0; script[i]; i++) send_cmd(script[i]);
}

/* ---------- menu ---------- */
static void print_menu(void) {
    puts("===================== SIMULATOR =====================");
    puts("  1. Push to stack        5. Load memory (addr, value)");
    puts("  2. Pop from stack       6. Step CPU");
    puts("  3. Enqueue              7. Show state");
    puts("  4. Dequeue              8. Run demo script");
    puts("  0. Quit");
    printf("Choice: "); fflush(stdout);
}

static int ask_long(const char *prompt, long *out) {
    char line[64], *end;
    printf("%s", prompt); fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return 0;
    *out = strtol(line, &end, 10);
    if (end == line) { puts("  Invalid number."); return 0; }
    return 1;
}

static void *input_thread(void *arg) {
    (void)arg;
    if (getenv("UI_AUTO")) {
        run_script();
    } else {
        char line[64], cmd[64];
        for (;;) {
            print_menu();
            if (!fgets(line, sizeof line, stdin)) break;       /* EOF = quit */
            char *end; long c = strtol(line, &end, 10);
            if (end == line) { puts("  Invalid choice."); continue; }
            long a, b;
            switch (c) {
            case 0: goto done;
            case 1: if (ask_long("  Value to push: ", &a))  { snprintf(cmd, sizeof cmd, "PUSH %ld", a); send_cmd(cmd); } break;
            case 2: send_cmd("POP"); break;
            case 3: if (ask_long("  Value to enqueue: ", &a)) { snprintf(cmd, sizeof cmd, "ENQ %ld", a); send_cmd(cmd); } break;
            case 4: send_cmd("DEQ"); break;
            case 5: if (ask_long("  Memory address (0-63): ", &a) && ask_long("  Value: ", &b)) {
                        snprintf(cmd, sizeof cmd, "LOAD %ld %ld", a, b); send_cmd(cmd); } break;
            case 6: send_cmd("STEP"); break;
            case 7: draw_state(); break;
            case 8: run_script(); break;
            default: puts("  Invalid choice."); break;
            }
            wait_replies();
        }
    }
done:
    atomic_store(&input_done, 1);
    return NULL;
}

/* ---------- reply display thread ---------- */
static void *display_thread(void *arg) {
    (void)arg;
    char buf[MQ_MSG_SZ];
    for (;;) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        ssize_t n = mq_timedreceive(q_rep, buf, sizeof buf, NULL, &ts);
        if (n < 0) {
            if (errno == ETIMEDOUT) {
                if (atomic_load(&input_done) && atomic_load(&received) >= atomic_load(&sent)) break;
                continue;
            }
            break;
        }
        int seq = 0; sscanf(buf, "%d", &seq);
        if (seq > 0 && seq < MAX_SEQ) lat_total += now_us() - t_sent[seq];
        if (strstr(buf, " ERR")) atomic_fetch_add(&errors_seen, 1);
        printf("  <- %s\n", buf); fflush(stdout);
        atomic_fetch_add(&received, 1);
        if (atomic_load(&input_done) && atomic_load(&received) >= atomic_load(&sent)) break;
    }
    return NULL;
}

int main(void) {
    int s = ctrl_connect(PROC_UI);
    ctrl_wait(s, MSG_START);

    q_cmd = mq_open(MQ_CMD,   O_WRONLY);
    q_rep = mq_open(MQ_REPLY, O_RDONLY);
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    st = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (q_cmd == (mqd_t)-1 || q_rep == (mqd_t)-1 || st == MAP_FAILED) { perror("ui open"); return 1; }

    pthread_t ti, td;
    pthread_create(&td, NULL, display_thread, NULL);
    pthread_create(&ti, NULL, input_thread, NULL);
    pthread_join(ti, NULL);
    pthread_join(td, NULL);

    int r = atomic_load(&received);
    printf("UI: replies %d/%d, errors=%d\n", r, atomic_load(&sent), atomic_load(&errors_seen));
    pthread_mutex_lock(&st->lock);
    printf("UI: snapshot pc=%d acc=%ld sp=%d qcount=%d mem0=%ld\n",
           st->cpu.pc, st->cpu.acc, st->sp, st->qcount, st->mem[0]);
    pthread_mutex_unlock(&st->lock);
    fflush(stdout);

    ctrl_done(s, PROC_UI);
    ctrl_wait(s, MSG_SHUTDOWN);
    ctrl_metrics(s, PROC_UI, r ? lat_total / r : 0);
    return 0;
}
