/* mqclient.c - sends ONE request to the core over a message queue and prints the JSON reply.
 * Usage: ./mqclient "7 0 1 2 0 3 0 4 2 3" 3 4
 * The web server (server.py) runs this program for every request from the website. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <mqueue.h>
#include "protocol.h"

#define MAX_JSON 262144

static int fail(const char *msg) {
    printf("{\"ok\":false,\"error\":\"%s\"}\n", msg);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 4) return fail("Usage: mqclient <refs> <frames> <algo>");

    Request rq; memset(&rq, 0, sizeof rq);
    snprintf(rq.reply_q, sizeof rq.reply_q, REPLY_FMT, (int)getpid());
    snprintf(rq.refs, sizeof rq.refs, "%s", argv[1]);
    rq.frames = atoi(argv[2]);
    rq.algo = atoi(argv[3]);

    mq_unlink(rq.reply_q);
    struct mq_attr attr = {0};
    attr.mq_maxmsg = MAX_MSGS; attr.mq_msgsize = MSG_SIZE;
    mqd_t reply = mq_open(rq.reply_q, O_CREAT | O_RDONLY, 0600, &attr);
    if (reply == (mqd_t)-1) return fail("Could not create reply queue.");

    mqd_t req = mq_open(REQ_Q, O_WRONLY);
    if (req == (mqd_t)-1) { mq_close(reply); mq_unlink(rq.reply_q); return fail("Core process is not running."); }
    if (mq_send(req, (char *)&rq, sizeof rq, 0) == -1) {
        mq_close(reply); mq_unlink(rq.reply_q); return fail("Could not send request.");
    }
    mq_close(req);

    char *json = malloc(MAX_JSON);
    size_t used = 0;
    int done = 0, ok = 1;
    char buf[MSG_SIZE];
    while (!done) {
        struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_sec += 5;
        if (mq_timedreceive(reply, buf, MSG_SIZE, NULL, &t) == -1) { ok = 0; break; }
        Chunk *c = (Chunk *)buf;
        if (c->len < 0 || c->len > CHUNK_DATA || used + c->len >= MAX_JSON) { ok = 0; break; }
        memcpy(json + used, c->data, c->len);
        used += c->len;
        done = c->last;
    }
    mq_close(reply); mq_unlink(rq.reply_q);
    if (!ok) { free(json); return fail("No reply from core (timed out)."); }
    json[used] = 0;
    puts(json);
    free(json);
    return 0;
}
