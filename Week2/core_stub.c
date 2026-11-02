/* TEST STUB for the Core process (shared memory). Replace with the real core.c. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/mman.h>
#include "../ctrl_util.h"

int main(void) {
    int s = ctrl_connect(PROC_CORE);
    ctrl_wait(s, MSG_START);

    mqd_t cmd = mq_open(MQ_CMD,   O_RDONLY);
    mqd_t rep = mq_open(MQ_REPLY, O_WRONLY);
    int sfd = shm_open(SHM_NAME, O_RDWR, 0666);
    shm_state_t *st = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
    if (cmd == (mqd_t)-1 || rep == (mqd_t)-1 || st == MAP_FAILED) { perror("core open"); return 1; }
    int lf = open(LOG_FIFO, O_WRONLY);          /* blocks until Logger opens read end */

    char buf[MQ_MSG_SZ], line[128], reply[32];
    for (;;) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        ssize_t n = mq_timedreceive(cmd, buf, sizeof buf, NULL, &ts);
        if (n < 0) {
            if (errno == ETIMEDOUT) { if (ctrl_shutdown_pending(s)) break; continue; }
            perror("core recv"); break;
        }
        long id = 0; sscanf(buf, "CMD %ld", &id);
        st->counter++; st->last_cmd = id;       /* update shared state */

        int len = snprintf(line, sizeof line, "CORE|INFO|exec cmd %ld\n", id);
        (void)!write(lf, line, len);            /* log BEFORE replying */

        snprintf(reply, sizeof reply, "OK %ld", id);
        mq_send(rep, reply, strlen(reply) + 1, 0);
    }
    close(lf);                                  /* gives Logger EOF */
    ctrl_metrics(s, PROC_CORE, 0);
    return 0;
}
