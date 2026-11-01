/* integrator.c - Integrator process (sockets).
 * Creates the IPC objects, launches UI/Core/Logger, runs the control socket,
 * collects metrics, shuts everything down, cleans up. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "ipc_defs.h"

static const char *NAMES[NPROC] = { "UI", "Core", "Logger" };

static void cleanup(void) {
    mq_unlink(MQ_CMD); mq_unlink(MQ_REPLY);
    shm_unlink(SHM_NAME); unlink(LOG_FIFO); unlink(CTRL_SOCK);
}

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void abort_all(pid_t *pids, const char *why) {
    fprintf(stderr, "[integrator] ERROR: %s\n", why);
    for (int i = 0; i < NPROC; i++) if (pids[i] > 0) kill(pids[i], SIGTERM);
    for (int i = 0; i < NPROC; i++) if (pids[i] > 0) waitpid(pids[i], NULL, 0);
    cleanup();
    exit(1);
}

int main(void) {
    cleanup();                                   /* remove stale objects from a crashed run */

    /* 1. create IPC objects (owners just open them) */
    struct mq_attr at; memset(&at, 0, sizeof at);
    at.mq_maxmsg = 10; at.mq_msgsize = MQ_MSG_SZ;
    mqd_t q1 = mq_open(MQ_CMD,   O_CREAT | O_RDWR, 0666, &at);
    mqd_t q2 = mq_open(MQ_REPLY, O_CREAT | O_RDWR, 0666, &at);
    if (q1 == (mqd_t)-1 || q2 == (mqd_t)-1) { perror("mq_open"); cleanup(); return 1; }
    mq_close(q1); mq_close(q2);

    int sfd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (sfd < 0 || ftruncate(sfd, SHM_SIZE) < 0) { perror("shm"); cleanup(); return 1; }
    close(sfd);

    if (mkfifo(LOG_FIFO, 0666) < 0) { perror("mkfifo"); cleanup(); return 1; }

    /* 2. control socket */
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un ad; memset(&ad, 0, sizeof ad);
    ad.sun_family = AF_UNIX;
    strncpy(ad.sun_path, CTRL_SOCK, sizeof ad.sun_path - 1);
    if (bind(ls, (struct sockaddr *)&ad, sizeof ad) < 0) { perror("bind"); cleanup(); return 1; }
    listen(ls, NPROC);

    /* 3. launch the three processes */
    const char *prog[NPROC] = { "./ui", "./core", "./logger" };
    pid_t pids[NPROC] = { 0, 0, 0 };
    for (int p = 0; p < NPROC; p++) {
        pids[p] = fork();
        if (pids[p] == 0) { execl(prog[p], prog[p], (char *)NULL); perror(prog[p]); _exit(1); }
    }

    /* 4. wait for READY from all three (10 s timeout) */
    int cfd[NPROC] = { -1, -1, -1 };
    for (int n = 0; n < NPROC; n++) {
        struct pollfd lp = { ls, POLLIN, 0 };
        if (poll(&lp, 1, 10000) <= 0) abort_all(pids, "timeout waiting for processes to connect");
        int c = accept(ls, NULL, NULL);
        ctrl_msg_t m;
        if (read(c, &m, sizeof m) == (ssize_t)sizeof m && m.type == MSG_READY &&
            m.from >= 0 && m.from < NPROC) {
            cfd[m.from] = c;
            printf("[integrator] %s ready (pid %ld)\n", NAMES[m.from], m.pid);
        } else abort_all(pids, "bad READY message");
    }

    /* 5. start */
    double t0 = now_ms();
    ctrl_msg_t go; memset(&go, 0, sizeof go); go.type = MSG_START;
    for (int i = 0; i < NPROC; i++) (void)!write(cfd[i], &go, sizeof go);

    /* 6. wait for UI to report DONE (60 s timeout) */
    int done = 0;
    while (!done) {
        struct pollfd pf[NPROC];
        for (int i = 0; i < NPROC; i++) pf[i] = (struct pollfd){ cfd[i], POLLIN, 0 };
        if (poll(pf, NPROC, 60000) <= 0) abort_all(pids, "timeout waiting for DONE");
        for (int i = 0; i < NPROC; i++) {
            if (!(pf[i].revents & (POLLIN | POLLHUP))) continue;
            ctrl_msg_t m;
            if (read(cfd[i], &m, sizeof m) != (ssize_t)sizeof m) abort_all(pids, "a process died early");
            if (m.type == MSG_DONE) done = 1;
        }
    }
    printf("[integrator] total run: %.2f ms\n", now_ms() - t0);

    /* 7. shutdown + collect metrics */
    ctrl_msg_t bye; memset(&bye, 0, sizeof bye); bye.type = MSG_SHUTDOWN;
    for (int i = 0; i < NPROC; i++) (void)!write(cfd[i], &bye, sizeof bye);
    for (int i = 0; i < NPROC; i++) {
        struct pollfd p = { cfd[i], POLLIN, 0 };
        ctrl_msg_t m;
        if (poll(&p, 1, 3000) > 0 && read(cfd[i], &m, sizeof m) == (ssize_t)sizeof m &&
            m.type == MSG_METRICS)
            printf("[metrics] %-6s ipc_latency=%.2f us  cpu=%.2f ms  mem=%.0f KB\n",
                   NAMES[i], m.v1, m.v3, m.v2);
        close(cfd[i]);
    }
    for (int i = 0; i < NPROC; i++) waitpid(pids[i], NULL, 0);

    close(ls);
    cleanup();
    printf("[integrator] clean exit\n");
    return 0;
}
