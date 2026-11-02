/* TEST STUB for the UI process (message queues). Replace with the real ui.c. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <mqueue.h>
#include <sys/mman.h>
#include "../ctrl_util.h"

int main(void) {
    int s = ctrl_connect(PROC_UI);
    ctrl_wait(s, MSG_START);

    mqd_t cmd = mq_open(MQ_CMD,   O_WRONLY);
    mqd_t rep = mq_open(MQ_REPLY, O_RDONLY);
    int sfd = shm_open(SHM_NAME, O_RDONLY, 0666);
    shm_state_t *st = mmap(NULL, SHM_SIZE, PROT_READ, MAP_SHARED, sfd, 0);
    if (cmd == (mqd_t)-1 || rep == (mqd_t)-1 || st == MAP_FAILED) { perror("ui open"); return 1; }

    double total = 0; int ok = 0;
    char out[MQ_MSG_SZ], in[MQ_MSG_SZ];
    for (int i = 1; i <= N_CMDS; i++) {
        snprintf(out, sizeof out, "CMD %d", i);
        double t = now_us();
        mq_send(cmd, out, strlen(out) + 1, 0);
        ssize_t n = mq_receive(rep, in, sizeof in, NULL);
        total += now_us() - t;
        if (n > 0 && strncmp(in, "OK", 2) == 0) ok++;
    }
    printf("UI: %d/%d replies ok, shm counter=%ld\n", ok, N_CMDS, st->counter);
    fflush(stdout);

    ctrl_done(s, PROC_UI);
    ctrl_wait(s, MSG_SHUTDOWN);
    ctrl_metrics(s, PROC_UI, total / N_CMDS);
    return 0;
}
