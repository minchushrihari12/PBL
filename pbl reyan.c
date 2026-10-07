/*
 * UI process: menu for the user.
 *   writes commands to /sim_cmd (UI -> Core)
 *   reads  results  from /sim_res (Core -> UI)
 * The UI never touches the logger; the Core does that.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ipc_defs.h"

#define RESPONSE_TIMEOUT_SEC 30      /* long enough for INPUT typed in Core's terminal */

static void print_state(const res_msg_t *r)
{
    const char *kind = r->type == RES_ERROR  ? "ERROR"  :
                       r->type == RES_HALTED ? "HALTED" : "STATE";

    printf("\n========================================\n");
    printf("RESULT: %s\n%s\n", kind, r->text);
    printf("----------------------------------------\n");
    printf("PC  : %u\n", r->pc);
    printf("IR  : %u  (index of last fetched instruction)\n", r->ir);
    printf("ACC : %d\n", (int32_t)r->regs[REG_ACC]);
    printf("MAR : %u\n", r->regs[REG_MAR]);
    printf("MDR : %d\n", (int32_t)r->regs[REG_MDR]);
    printf("CPU : %s\n", r->flags ? "RUNNING" : "HALTED");
    printf("========================================\n");
}

/* Throw away late/stale responses so replies never get out of step. */
static void drain_stale(mqd_t q)
{
    struct mq_attr a;
    res_msg_t junk;
    while (mq_getattr(q, &a) == 0 && a.mq_curmsgs > 0)
        if (mq_receive(q, (char *)&junk, sizeof(junk), NULL) == -1)
            break;
}

static int read_line(char *buf, size_t n)
{
    if (!fgets(buf, (int)n, stdin)) return -1;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

int main(void)
{
    mqd_t q_cmd = ipc_open_queue(Q_CMD, O_WRONLY, sizeof(cmd_msg_t));
    mqd_t q_res = ipc_open_queue(Q_RES, O_RDONLY, sizeof(res_msg_t));
    if (q_cmd == (mqd_t)-1 || q_res == (mqd_t)-1) {
        perror("[UI] mq_open");
        fprintf(stderr, "[UI] If this follows a code change, run: make clean-queues\n");
        return 1;
    }

    for (;;) {
        char line[TEXT_LEN];
        cmd_msg_t cmd;
        res_msg_t res;

        memset(&cmd, 0, sizeof(cmd));
        memset(&res, 0, sizeof(res));

        printf("\n=========== CPU IPC SIMULATOR ===========\n");
        printf("1. Load Program\n2. Execute Next Instruction\n3. Run Program\n");
        printf("4. Reset CPU\n5. Quit\n");
        printf("=========================================\nEnter choice: ");
        fflush(stdout);

        if (read_line(line, sizeof(line)) == -1) {      /* EOF: behave like Quit */
            cmd.type = CMD_QUIT;
        } else {
            char *end;
            long choice = strtol(line, &end, 10);
            if (end == line) { printf("Invalid choice.\n"); continue; }

            switch (choice) {
            case 1:
                cmd.type = CMD_LOAD;
                printf("Enter program filename: ");
                fflush(stdout);
                if (read_line(cmd.text, sizeof(cmd.text)) == -1 || cmd.text[0] == '\0') {
                    printf("No filename given.\n");
                    continue;
                }
                break;
            case 2: cmd.type = CMD_STEP;  break;
            case 3: cmd.type = CMD_RUN;   break;
            case 4: cmd.type = CMD_RESET; break;
            case 5: cmd.type = CMD_QUIT;  break;
            default: printf("Invalid choice.\n"); continue;
            }
        }

        drain_stale(q_res);

        if (ipc_send_timed(q_cmd, &cmd, sizeof(cmd), IPC_TIMEOUT_SEC) == -1) {
            fprintf(stderr, "[UI] Could not send command (is the Core running?): %s\n",
                    strerror(errno));
            continue;
        }

        if (ipc_recv_timed(q_res, &res, sizeof(res), RESPONSE_TIMEOUT_SEC) == -1) {
            if (errno == ETIMEDOUT)
                fprintf(stderr, "[UI] No response from Core within %d s. Is core_process running?\n",
                        RESPONSE_TIMEOUT_SEC);
            else
                perror("[UI] mq_receive");
            if (cmd.type == CMD_QUIT) break;
            continue;
        }

        print_state(&res);
        if (cmd.type == CMD_QUIT) break;
    }

    mq_close(q_cmd);
    mq_close(q_res);
    printf("[UI] Exiting.\n");
    return 0;
}
