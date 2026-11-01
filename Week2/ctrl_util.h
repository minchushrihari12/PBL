/* ctrl_util.h - helper for talking to the Integrator over the control socket.
 * Teammates: #include this and use these 5 functions, nothing else needed. */
#ifndef CTRL_UTIL_H
#define CTRL_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/resource.h>
#include "ipc_defs.h"

static inline double now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

/* connect to Integrator and announce READY. Returns socket fd. */
static inline int ctrl_connect(int who) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a; memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, CTRL_SOCK, sizeof a.sun_path - 1);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("ctrl connect"); exit(1); }
    ctrl_msg_t m; memset(&m, 0, sizeof m);
    m.type = MSG_READY; m.from = who; m.pid = getpid();
    (void)!write(s, &m, sizeof m);
    return s;
}

/* block until a message of this type arrives. 0 = ok, -1 = socket closed */
static inline int ctrl_wait(int s, int type) {
    ctrl_msg_t m;
    while (read(s, &m, sizeof m) == (ssize_t)sizeof m)
        if (m.type == type) return 0;
    return -1;
}

/* non-blocking: 1 if MSG_SHUTDOWN arrived (or Integrator vanished), else 0 */
static inline int ctrl_shutdown_pending(int s) {
    struct pollfd p = { s, POLLIN, 0 };
    if (poll(&p, 1, 0) <= 0) return 0;
    ctrl_msg_t m;
    if (read(s, &m, sizeof m) != (ssize_t)sizeof m) return 1;
    return m.type == MSG_SHUTDOWN;
}

/* send MSG_DONE (UI calls this when the demo finishes) */
static inline void ctrl_done(int s, int who) {
    ctrl_msg_t m; memset(&m, 0, sizeof m);
    m.type = MSG_DONE; m.from = who; m.pid = getpid();
    (void)!write(s, &m, sizeof m);
}

/* send metrics. Call this AFTER receiving MSG_SHUTDOWN, just before exit. */
static inline void ctrl_metrics(int s, int who, double avg_ipc_latency_us) {
    struct rusage r; getrusage(RUSAGE_SELF, &r);
    ctrl_msg_t m; memset(&m, 0, sizeof m);
    m.type = MSG_METRICS; m.from = who; m.pid = getpid();
    m.v1 = avg_ipc_latency_us;
    m.v2 = (double)r.ru_maxrss;
    m.v3 = r.ru_utime.tv_sec * 1e3 + r.ru_utime.tv_usec / 1e3
         + r.ru_stime.tv_sec * 1e3 + r.ru_stime.tv_usec / 1e3;
    (void)!write(s, &m, sizeof m);
}

#endif
