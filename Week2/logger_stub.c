/* TEST STUB for the Logger process (pipes/FIFO). Replace with the real logger.c. */
#define _GNU_SOURCE
#include <fcntl.h>
#include "../ctrl_util.h"

int main(void) {
    int s = ctrl_connect(PROC_LOGGER);
    ctrl_wait(s, MSG_START);

    int ff = open(LOG_FIFO, O_RDONLY);          /* blocks until Core opens write end */
    FILE *out = fopen("sim.log", "w");
    if (ff < 0 || !out) { perror("logger open"); return 1; }

    struct pollfd pf[2] = { { ff, POLLIN, 0 }, { s, POLLIN, 0 } };
    char buf[4096]; int shutting = 0;
    for (;;) {
        int r = poll(pf, 2, shutting ? 1000 : -1);
        if (r <= 0) break;
        if (pf[0].revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(ff, buf, sizeof buf);
            if (n <= 0) break;                  /* EOF: Core closed the pipe */
            fwrite(buf, 1, n, out);
        }
        if (!shutting && (pf[1].revents & (POLLIN | POLLHUP))) {
            ctrl_msg_t m;
            if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.type == MSG_SHUTDOWN) {
                shutting = 1; pf[1].fd = -1;    /* keep draining pipe until EOF */
            }
        }
    }
    fclose(out);
    ctrl_metrics(s, PROC_LOGGER, 0);
    return 0;
}
