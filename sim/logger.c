#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include "ipc_defs.h"

#define DISK_QUEUE "/sim_log_disk"   /* internal: receiver -> writer (logger only) */
#define LOG_FILE   "simulator.log"

typedef struct {
    int  quit;                       /* 1 = flush and exit */
    char line[TEXT_LEN + 64];
} disk_msg_t;

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static const char *level_name(log_level_t lvl)
{
    switch (lvl) {
        case LOG_INFO:  return "INFO";
        case LOG_ERROR: return "ERROR";
        case LOG_QUIT:  return "QUIT";
        default:        return "UNKNOWN";
    }
}

/* ---------------- Child: disk writer ---------------- */
static void writer_process(void)
{
    signal(SIGINT, SIG_IGN);         /* parent controls shutdown */
    signal(SIGTERM, SIG_IGN);

    mqd_t dq = mq_open(DISK_QUEUE, O_RDONLY);
    if (dq == (mqd_t)-1) { perror("writer: mq_open " DISK_QUEUE); _exit(1); }

    FILE *logfile = fopen(LOG_FILE, "a");
    if (logfile == NULL) { perror("writer: fopen " LOG_FILE); _exit(1); }

    disk_msg_t m;
    for (;;)
    {
        ssize_t n = mq_receive(dq, (char *)&m, sizeof(m), NULL);
        if (n == -1)
        {
            if (errno == EINTR) continue;
            perror("writer: mq_receive");
            break;
        }
        if (m.quit) break;           /* everything queued earlier is already written */

        fputs(m.line, logfile);
        fflush(logfile);
    }

    fflush(logfile);
    fclose(logfile);
    mq_close(dq);
    _exit(0);
}

/* ---------------- Parent: receiver ---------------- */
int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;   /* no SA_RESTART: mq_receive returns EINTR */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Queue Core sends to (create if Core hasn't started yet) */
    mqd_t inq = ipc_open_queue(Q_LOG, O_RDONLY, sizeof(log_msg_t));
    if (inq == (mqd_t)-1) { perror("mq_open " Q_LOG); return 1; }

    /* Internal queue to the writer; create BEFORE fork */
    struct mq_attr d_attr;
    memset(&d_attr, 0, sizeof(d_attr));
    d_attr.mq_maxmsg  = Q_MAXMSG;
    d_attr.mq_msgsize = sizeof(disk_msg_t);
    mq_unlink(DISK_QUEUE);           /* remove stale queue from a crashed run */
    mqd_t dq = mq_open(DISK_QUEUE, O_CREAT | O_WRONLY, 0600, &d_attr);
    if (dq == (mqd_t)-1) { perror("mq_open " DISK_QUEUE); return 1; }

    pid_t pid = fork();
    if (pid == -1) { perror("fork"); return 1; }

    if (pid == 0)
    {
        mq_close(inq);               /* child doesn't need these */
        mq_close(dq);
        writer_process();            /* never returns */
    }

    printf("Logger Process Started (receiver pid %d, writer pid %d)\n",
           getpid(), pid);
    printf("Waiting for messages on %s...\n", Q_LOG);

    log_msg_t msg;
    while (running)
    {
        ssize_t n = mq_receive(inq, (char *)&msg, sizeof(msg), NULL);
        if (n == -1)
        {
            if (errno == EINTR) continue;   /* loop re-checks 'running' */
            perror("mq_receive");
            break;
        }

        msg.text[TEXT_LEN - 1] = '\0';

        /* Use the timestamp Core attached to the event */
        struct tm tm_info;
        char ts[32];
        localtime_r(&msg.timestamp, &tm_info);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

        disk_msg_t out;
        memset(&out, 0, sizeof(out));
        snprintf(out.line, sizeof(out.line), "[%s] [%s] %s\n",
                 ts, level_name(msg.level), msg.text);

        printf("%s", out.line);
        fflush(stdout);

        /* Blocks if the writer is behind -> no log lines dropped in a burst */
        while (mq_send(dq, (const char *)&out, sizeof(out), 0) == -1)
        {
            if (errno == EINTR) continue;
            perror("mq_send to writer");
            running = 0;
            break;
        }

        if (msg.level == LOG_QUIT)      /* logged first, then stop */
            break;
    }

    printf("\nLogger shutting down, flushing remaining messages...\n");

    /* Writer finishes everything queued before this marker, then exits */
    disk_msg_t quit;
    memset(&quit, 0, sizeof(quit));
    quit.quit = 1;
    while (mq_send(dq, (const char *)&quit, sizeof(quit), 0) == -1
           && errno == EINTR)
        ;

    waitpid(pid, NULL, 0);           /* reap child, no zombie */

    mq_close(inq);
    mq_close(dq);
    mq_unlink(DISK_QUEUE);           /* internal queue only; Core/UI own /sim_log */
    return 0;
}
