#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#include "ipc_defs.h"

/* Fill one log_msg_t and send it. Returns 0 on success, -1 on failure. */
static int send_log(mqd_t q, log_level_t level, const char *text)
{
    log_msg_t msg;
    memset(&msg, 0, sizeof(msg));             /* always clear the struct first */
    msg.level = level;
    msg.timestamp = time(NULL);               /* current time */
    strncpy(msg.text, text, TEXT_LEN - 1);    /* copy at most TEXT_LEN-1 chars */

    if (mq_send(q, (const char *)&msg, sizeof(msg), 0) == -1) {
        perror("fake_core: mq_send");
        return -1;
    }
    return 0;
}

int main(void)
{
    ipc_print_info("fake_core");

    /* Open the log queue for sending only (macro from ipc_defs.h) */
    mqd_t q = IPC_OPEN_LOG(O_WRONLY);

    char text[TEXT_LEN];

    /* 1000 INFO messages: "test message 0" ... "test message 999" */
    for (int i = 0; i < 1000; i++) {
        snprintf(text, sizeof(text), "test message %d", i);
        if (send_log(q, LOG_INFO, text) == -1) { mq_close(q); return 1; }
    }

    /* 5 ERROR messages */
    for (int i = 0; i < 5; i++) {
        snprintf(text, sizeof(text), "test error %d", i);
        if (send_log(q, LOG_ERROR, text) == -1) { mq_close(q); return 1; }
    }

    /* Tell the Logger to stop */
    if (send_log(q, LOG_QUIT, "quit") == -1) { mq_close(q); return 1; }

    mq_close(q);
    printf("fake_core: sent 1000 INFO, 5 ERROR, 1 QUIT\n");
    return 0;
}
