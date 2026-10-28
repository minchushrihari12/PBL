/* logger.c - Logger process. Reads log messages from a queue and appends them to simulation.log */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <mqueue.h>
#include "protocol.h"

static volatile sig_atomic_t running = 1;
static void stop(int s) { (void)s; running = 0; }

int main(void) {
    signal(SIGINT, stop); signal(SIGTERM, stop);
    mq_unlink(LOG_Q);
    struct mq_attr attr = {0};
    attr.mq_maxmsg = MAX_MSGS; attr.mq_msgsize = MSG_SIZE;
    mqd_t q = mq_open(LOG_Q, O_CREAT | O_RDONLY, 0666, &attr);
    if (q == (mqd_t)-1) { perror("logger: mq_open"); return 1; }
    FILE *f = fopen("simulation.log", "a");
    if (!f) { perror("logger: simulation.log"); return 1; }
    printf("logger: writing to simulation.log\n");
    fflush(stdout);

    char buf[MSG_SIZE];
    while (running) {
        struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_sec += 1;
        if (mq_timedreceive(q, buf, MSG_SIZE, NULL, &t) == -1) {
            if (errno == ETIMEDOUT || errno == EINTR) continue;
            perror("logger: mq_receive"); break;
        }
        LogMsg *m = (LogMsg *)buf;
        m->text[sizeof m->text - 1] = 0;
        time_t now = time(NULL); char ts[32];
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] %s\n", ts, m->text);
        fflush(f);
    }
    fclose(f); mq_close(q); mq_unlink(LOG_Q);
    printf("logger: stopped\n");
    return 0;
}
