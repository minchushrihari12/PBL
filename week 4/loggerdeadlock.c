/*
 * logger.c  --  Logger process.  (Owner: Logger teammate)
 *
 * Reads log_msg_t from KEY_LOG and appends timestamped lines to
 * deadlock.log (and echoes them to the terminal).
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ipc_defs.h"

#define LOG_FILE "deadlock.log"

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

int main(void)
{
    static const char *lvl[] = { "INFO ", "WARN ", "ERROR" };
    struct sigaction sa;
    FILE *f = fopen(LOG_FILE, "a");
    int q;

    if (!f) { perror("[logger] fopen"); return 1; }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    q = ipc_open(KEY_LOG);
    printf("[logger] writing to %s\n", LOG_FILE);
    fflush(stdout);

    while (running) {
        log_msg_t m;
        ssize_t n = msgrcv(q, &m, MSG_SIZE(log_msg_t), 0, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) {   /* core restarted: queue was recreated */
                sleep(1);
                q = ipc_open(KEY_LOG);
                continue;
            }
            perror("[logger] msgrcv");
            break;
        }
        m.source[sizeof m.source - 1] = 0;
        m.text[LOG_LEN - 1] = 0;

        char ts[24];
        time_t now = time(NULL);
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
        int l = (m.level >= 0 && m.level <= 2) ? m.level : 0;

        fprintf(f, "[%s] [%s] [%-4s] %s\n", ts, lvl[l], m.source, m.text);
        fflush(f);
        printf("[%s] [%s] [%-4s] %s\n", ts, lvl[l], m.source, m.text);
        fflush(stdout);
    }

    fclose(f);
    printf("[logger] stopped\n");
    return 0;
}
