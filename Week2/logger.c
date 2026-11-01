/* STARTER - Logger process (pipe / FIFO).
 * Owner: Student 3.  Reads log lines from LOG_FIFO, timestamps them, writes sim.log,
 * counts ERROR lines. Handles partial reads (a line can arrive split across read() calls).
 * TODO (yours): log levels/filtering, log rotation, separate error.log, a writer thread. */
#define _GNU_SOURCE
#include <fcntl.h>
#include "ctrl_util.h"

static FILE *out;
static char acc[1024]; static size_t alen;
static long lines, errs;

static void emit(const char *line) {
    char ts[32]; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    fprintf(out, "[%s] %s\n", ts, line);
    lines++;
    if (strstr(line, "|ERROR|")) errs++;
}

static void feed(const char *d, ssize_t n) {
    for (ssize_t i = 0; i < n; i++) {
        if (d[i] == '\n') { acc[alen] = 0; emit(acc); alen = 0; }
        else if (alen < sizeof acc - 1) acc[alen++] = d[i];
    }
}

int main(void) {
    int s = ctrl_connect(PROC_LOGGER);
    ctrl_wait(s, MSG_START);

    int ff = open(LOG_FIFO, O_RDONLY);          /* blocks until Core opens write end */
    out = fopen("sim.log", "w");
    if (ff < 0 || !out) { perror("logger open"); return 1; }

    struct pollfd pf[2] = { { ff, POLLIN, 0 }, { s, POLLIN, 0 } };
    char buf[4096]; int shutting = 0;
    for (;;) {
        int r = poll(pf, 2, shutting ? 1000 : -1);
        if (r <= 0) break;
        if (pf[0].revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(ff, buf, sizeof buf);
            if (n <= 0) break;                  /* EOF: Core closed the pipe */
            feed(buf, n);
        }
        if (!shutting && (pf[1].revents & (POLLIN | POLLHUP))) {
            ctrl_msg_t m;
            if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.type == MSG_SHUTDOWN) {
                shutting = 1; pf[1].fd = -1;    /* keep draining the pipe until EOF */
            }
        }
    }
    fclose(out);
    printf("LOGGER: %ld lines, %ld errors\n", lines, errs); fflush(stdout);
    ctrl_metrics(s, PROC_LOGGER, 0);
    return 0;
}
