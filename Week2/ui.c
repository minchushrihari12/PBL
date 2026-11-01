/* STARTER - UI process (message queues + 2 threads).
 * Owner: Student 1.  Input thread -> mq_send(MQ_CMD); display thread <- mq_receive(MQ_REPLY).
 * Commands: PUSH n | POP | ENQ n | DEQ | LOAD addr val | STEP | show | quit
 * Run interactive:  ./integrator        Run scripted:  UI_AUTO=1 ./integrator
 * TODO (yours): nicer display (ncurses/ASCII boxes), more commands, input validation. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdatomic.h>
#include <strings.h>
#include <sys/mman.h>
#include "ctrl_util.h"
#include "sim_state.h"

#define MAX_SEQ 4096

static mqd_t q_cmd, q_rep;
static sim_state_t *st;
static double t_sent[MAX_SEQ];
static atomic_int sent, received, errors_seen, input_done;
static double lat_total;                      /* written by display thread only */

static void print_snapshot(void) {
    pthread_mutex_lock(&st->lock);
    printf("  [state] cpu pc=%d acc=%ld | stack sp=%d top=%ld | queue count=%d | cmds=%ld errors=%ld\n",
           st->cpu.pc, st->cpu.acc, st->sp, st->sp ? st->stack[st->sp - 1] : -1L,
           st->qcount, st->cmds_done, st->errors);
    pthread_mutex_unlock(&st->lock);
    fflush(stdout);
}

static void send_cmd(const char *text) {
    int seq = atomic_fetch_add(&sent, 1) + 1;
    if (seq >= MAX_SEQ) { fprintf(stderr, "too many commands\n"); return; }
    char buf[MQ_MSG_SZ];
    snprintf(buf, sizeof buf, "%d %s", seq, text);
    t_sent[seq] = now_us();
    mq_send(q_cmd, buf, strlen(buf) + 1, 0);
    printf("-> %s\n", buf); fflush(stdout);
}

static void *input_thread(void *arg) {
    (void)arg;
    if (getenv("UI_AUTO")) {
        static const char *script[] = {
            "PUSH 1", "PUSH 2", "PUSH 3", "PUSH 4", "PUSH 5", "POP", "POP",
            "ENQ 10", "ENQ 11", "ENQ 12", "ENQ 13", "ENQ 14", "DEQ", "DEQ",
            "LOAD 0 5", "STEP", "STEP", "STEP", "BAD", NULL };
        for (int i = 0; script[i]; i++) send_cmd(script[i]);
    } else {
        char line[128];
        puts("Commands: PUSH n | POP | ENQ n | DEQ | LOAD addr val | STEP | show | quit");
        while (printf("> "), fflush(stdout), fgets(line, sizeof line, stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!*line) continue;
            if (!strcasecmp(line, "quit")) break;
            if (!strcasecmp(line, "show")) { print_snapshot(); continue; }
            send_cmd(line);
        }
    }
    atomic_store(&input_done, 1);
    return NULL;
}

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
