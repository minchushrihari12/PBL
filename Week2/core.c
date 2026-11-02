/* STARTER - Core process (shared memory).
 * Owner: Student 2.  Receives commands on MQ_CMD, executes them on CPU/Memory/Stack/Queue
 * kept in shared memory, logs to the FIFO, replies on MQ_REPLY.
 * TODO (yours): plug in your real simulator logic (e.g. Week 3 scheduler) inside exec_cmd(),
 * add more instructions, add a worker thread if you need one. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/mman.h>
#include "ctrl_util.h"
#include "sim_state.h"

static sim_state_t *st;

/* returns 1 = ok, 0 = error. Fills detail. Holds st->lock while touching state. */
static int exec_cmd(const char *verb, long a, long b, int nargs, char *d, size_t dl) {
    int ok = 1;
    pthread_mutex_lock(&st->lock);
    if (!strcmp(verb, "PUSH") && nargs >= 1) {
        if (st->sp >= STACK_MAX) { ok = 0; snprintf(d, dl, "stack full"); }
        else { st->stack[st->sp++] = a; snprintf(d, dl, "pushed %ld", a); }
    } else if (!strcmp(verb, "POP")) {
        if (st->sp == 0) { ok = 0; snprintf(d, dl, "stack empty"); }
        else { snprintf(d, dl, "popped %ld", st->stack[--st->sp]); }
    } else if (!strcmp(verb, "ENQ") && nargs >= 1) {
        if (st->qcount >= QUEUE_MAX) { ok = 0; snprintf(d, dl, "queue full"); }
        else { st->queue[(st->qhead + st->qcount++) % QUEUE_MAX] = a; snprintf(d, dl, "enqueued %ld", a); }
    } else if (!strcmp(verb, "DEQ")) {
        if (st->qcount == 0) { ok = 0; snprintf(d, dl, "queue empty"); }
        else { long v = st->queue[st->qhead]; st->qhead = (st->qhead + 1) % QUEUE_MAX; st->qcount--;
               snprintf(d, dl, "dequeued %ld", v); }
    } else if (!strcmp(verb, "LOAD") && nargs >= 2) {
        if (a < 0 || a >= MEM_WORDS) { ok = 0; snprintf(d, dl, "bad address %ld", a); }
        else { st->mem[a] = b; snprintf(d, dl, "mem[%ld]=%ld", a, b); }
    } else if (!strcmp(verb, "STEP")) {
        st->cpu.acc += st->mem[st->cpu.pc];
        st->cpu.pc = (st->cpu.pc + 1) % MEM_WORDS;
        snprintf(d, dl, "pc=%d acc=%ld", st->cpu.pc, st->cpu.acc);
    } else { ok = 0; snprintf(d, dl, "unknown command"); }
    st->cmds_done++; if (!ok) st->errors++;
    pthread_mutex_unlock(&st->lock);
    return ok;
}

int main(void) {
    /* map + initialise shared state BEFORE announcing READY so UI never sees an uninitialised mutex */
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    st = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st == MAP_FAILED) { perror("core shm"); return 1; }
    memset(st, 0, sizeof *st);
    pthread_mutexattr_t at; pthread_mutexattr_init(&at);
    pthread_mutexattr_setpshared(&at, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&st->lock, &at);

    int s = ctrl_connect(PROC_CORE);
    ctrl_wait(s, MSG_START);

    mqd_t q_cmd = mq_open(MQ_CMD,   O_RDONLY);
    mqd_t q_rep = mq_open(MQ_REPLY, O_WRONLY);
    if (q_cmd == (mqd_t)-1 || q_rep == (mqd_t)-1) { perror("core mq"); return 1; }
    int lf = open(LOG_FIFO, O_WRONLY);          /* blocks until Logger opens read end */

    char buf[MQ_MSG_SZ], verb[16], detail[96], line[160], reply[160];
    for (;;) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        ssize_t n = mq_timedreceive(q_cmd, buf, sizeof buf, NULL, &ts);
        if (n < 0) {
            if (errno == ETIMEDOUT) { if (ctrl_shutdown_pending(s)) break; continue; }
            perror("core recv"); break;
        }
        int seq = 0; long a = 0, b = 0; verb[0] = 0;
        int r = sscanf(buf, "%d %15s %ld %ld", &seq, verb, &a, &b);
        int ok = exec_cmd(verb, a, b, r - 2, detail, sizeof detail);

        int len = snprintf(line, sizeof line, "CORE|%s|%s -> %s\n", ok ? "INFO" : "ERROR", verb, detail);
        (void)!write(lf, line, len);            /* log BEFORE replying */

        snprintf(reply, sizeof reply, "%d %s %s", seq, ok ? "OK" : "ERR", detail);
        mq_send(q_rep, reply, strlen(reply) + 1, 0);
    }
    close(lf);                                  /* gives Logger EOF */
    ctrl_metrics(s, PROC_CORE, 0);
    return 0;
}
