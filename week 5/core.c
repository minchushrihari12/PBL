/* core.c - Core process. Waits for requests on a message queue, runs the algorithms,
 * sends the JSON result back on the client's reply queue, and tells the logger. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <mqueue.h>
#include "protocol.h"
#include "simulation.h"

static volatile sig_atomic_t running = 1;
static void stop(int s) { (void)s; running = 0; }

static void send_log(const char *text) {
    mqd_t q = mq_open(LOG_Q, O_WRONLY | O_NONBLOCK);
    if (q == (mqd_t)-1) return;                       /* logger not running: ignore */
    LogMsg m; memset(&m, 0, sizeof m);
    snprintf(m.text, sizeof m.text, "%s", text);
    mq_send(q, (char *)&m, sizeof m, 0);
    mq_close(q);
}

static void send_reply(const char *qname, const char *json) {
    mqd_t q = mq_open(qname, O_WRONLY);
    if (q == (mqd_t)-1) return;
    size_t total = strlen(json), off = 0;
    int seq = 0;
    do {
        Chunk c; memset(&c, 0, sizeof c);
        size_t n = total - off > CHUNK_DATA ? CHUNK_DATA : total - off;
        c.seq = seq++; c.len = (int)n; c.last = (off + n >= total);
        memcpy(c.data, json + off, n);
        struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_sec += 2;
        if (mq_timedsend(q, (char *)&c, sizeof c, 0, &t) == -1) break;  /* client gone */
        off += n;
    } while (off < total);
    mq_close(q);
}

int main(void) {
    signal(SIGINT, stop); signal(SIGTERM, stop);
    mq_unlink(REQ_Q);
    struct mq_attr attr = {0};
    attr.mq_maxmsg = MAX_MSGS; attr.mq_msgsize = MSG_SIZE;
    mqd_t req = mq_open(REQ_Q, O_CREAT | O_RDONLY, 0666, &attr);
    if (req == (mqd_t)-1) { perror("core: mq_open"); return 1; }
    printf("core: waiting for requests on %s\n", REQ_Q);
    fflush(stdout);

    char buf[MSG_SIZE];
    while (running) {
        struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_sec += 1;
        if (mq_timedreceive(req, buf, MSG_SIZE, NULL, &t) == -1) {
            if (errno == ETIMEDOUT || errno == EINTR) continue;
            perror("core: mq_receive"); break;
        }
        Request *rq = (Request *)buf;
        rq->reply_q[sizeof rq->reply_q - 1] = 0;
        rq->refs[sizeof rq->refs - 1] = 0;
        /* accept only our own reply queue names */
        if (strncmp(rq->reply_q, REPLY_PREFIX, strlen(REPLY_PREFIX)) != 0 || strchr(rq->reply_q + 1, '/'))
            continue;

        const char *json = run_simulation(rq->refs, rq->frames, rq->algo);
        send_reply(rq->reply_q, json);

        char line[256];
        snprintf(line, sizeof line, "algo=%d frames=%d refs=\"%.120s\" -> %s",
                 rq->algo, rq->frames, rq->refs, strstr(json, "\"ok\":true") ? "ok" : "invalid input");
        send_log(line);
    }
    mq_close(req); mq_unlink(REQ_Q);
    printf("core: stopped\n");
    return 0;
}
