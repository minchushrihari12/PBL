
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include "ipc_defs.h"

#define MAX_INSTRUCTIONS 100

// --- Program Storage ---
char program[MAX_INSTRUCTIONS][TEXT_LEN];
int program_size = 0;

// --- CPU Registers & State ---
uint32_t pc = 0;
uint32_t ir = 0;
uint32_t regs[NUM_REGS] = {0};
uint32_t sp = 0;
uint32_t flags = 0;

// --- Helper: Send state to UI (/sim_res) ---
void send_response(mqd_t q_res, res_type_t type, const char *text) {
    res_msg_t res;
    memset(&res, 0, sizeof(res));
    
    res.type = type;
    res.pc = pc;
    res.ir = ir;
    res.sp = sp;
    res.flags = flags;
    memcpy(res.regs, regs, sizeof(regs));
    
    if (text) {
        strncpy(res.text, text, TEXT_LEN - 1);
    }
    mq_send(q_res, (const char *)&res, sizeof(res), 0);
}

// --- Helper: Send log to Logger (/sim_log) ---
void send_log(mqd_t q_log, log_level_t level, const char *text) {
    log_msg_t log;
    memset(&log, 0, sizeof(log));
    
    log.level = level;
    log.timestamp = time(NULL);
    if (text) {
        strncpy(log.text, text, TEXT_LEN - 1);
    }
    mq_send(q_log, (const char *)&log, sizeof(log), 0);
}

// --- Reset CPU registers ---
void reset_cpu() {
    pc = 0;
    ir = 0;
    sp = 0;
    flags = 0;
    memset(regs, 0, sizeof(regs));
}

// --- Load Program File into Memory ---
int load_program(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    program_size = 0;
    while (fgets(program[program_size], TEXT_LEN, f) && program_size < MAX_INSTRUCTIONS) {
        program[program_size][strcspn(program[program_size], "\r\n")] = 0; // Strip newline
        if (strlen(program[program_size]) > 0) {
            program_size++;
        }
    }
    fclose(f);
    return 0;
}

// --- Execute a Single Instruction ---
void execute_instruction(mqd_t q_log, mqd_t q_res) {
    if (pc >= program_size) {
        send_log(q_log, LOG_INFO, "Program end reached / Core Halted.");
        send_response(q_res, RES_HALTED, "End of program.");
        return;
    }

    char *line = program[pc];
    char op[32] = {0};
    int val = 0;
    sscanf(line, "%s %d", op, &val);

    char log_buf[TEXT_LEN];
    snprintf(log_buf, sizeof(log_buf), "Executing PC=%d: %s", pc, line);
    send_log(q_log, LOG_INFO, log_buf);

    if (strcasecmp(op, "PUSH") == 0) {
        if (sp < NUM_REGS) {
            regs[sp++] = val;
        }
    } else if (strcasecmp(op, "POP") == 0) {
        if (sp > 0) {
            sp--;
        }
    } else if (strcasecmp(op, "ADD") == 0) {
        if (sp >= 2) {
            uint32_t b = regs[--sp];
            uint32_t a = regs[--sp];
            regs[sp++] = a + b;
        }
    } else if (strcasecmp(op, "SUB") == 0) {
        if (sp >= 2) {
            uint32_t b = regs[--sp];
            uint32_t a = regs[--sp];
            regs[sp++] = a - b;
        }
    } else if (strcasecmp(op, "STORE") == 0) {
        if (sp > 0 && val >= 0 && val < NUM_REGS) {
            regs[val] = regs[sp - 1];
        }
    } else if (strcasecmp(op, "LOAD") == 0) {
        if (val >= 0 && val < NUM_REGS && sp < NUM_REGS) {
            regs[sp++] = regs[val];
        }
    }

    pc++;
    send_response(q_res, RES_STATE, "Executed 1 instruction.");
}

int main() {
    struct mq_attr attr;

    // Clean up old message queues if present
    mq_unlink(Q_CMD);
    mq_unlink(Q_RES);
    mq_unlink(Q_LOG);

    // Create Command Queue (Read)
    attr.mq_flags = 0;
    attr.mq_maxmsg = Q_MAXMSG;
    attr.mq_msgsize = sizeof(cmd_msg_t);
    attr.mq_curmsgs = 0;
    mqd_t q_cmd = mq_open(Q_CMD, O_RDONLY | O_CREAT, 0666, &attr);

    // Create Response Queue (Write)
    attr.mq_msgsize = sizeof(res_msg_t);
    mqd_t q_res = mq_open(Q_RES, O_WRONLY | O_CREAT, 0666, &attr);

    // Create Log Queue (Write)
    attr.mq_msgsize = sizeof(log_msg_t);
    mqd_t q_log = mq_open(Q_LOG, O_WRONLY | O_CREAT, 0666, &attr);

    if (q_cmd == (mqd_t)-1 || q_res == (mqd_t)-1 || q_log == (mqd_t)-1) {
        perror("mq_open failed");
        return 1;
    }

    printf("=== CORE PROCESS ONLINE (Listening on %s) ===\n", Q_CMD);
    send_log(q_log, LOG_INFO, "Core process initialized.");

    cmd_msg_t cmd;
    int running = 1;

    while (running) {
        if (mq_receive(q_cmd, (char *)&cmd, sizeof(cmd), NULL) >= 0) {
            switch (cmd.type) {
                case CMD_LOAD:
                    if (load_program(cmd.text) == 0) {
                        reset_cpu();
                        send_log(q_log, LOG_INFO, "Program loaded into memory.");
                        send_response(q_res, RES_STATE, "Program loaded successfully.");
                    } else {
                        send_log(q_log, LOG_ERROR, "Failed to load program file.");
                        send_response(q_res, RES_ERROR, "Failed to open file.");
                    }
                    break;

                case CMD_STEP:
                    execute_instruction(q_log, q_res);
                    break;

                case CMD_RUN:
                    while (pc < program_size) {
                        execute_instruction(q_log, q_res);
                    }
                    break;

                case CMD_RESET:
                    reset_cpu();
                    send_log(q_log, LOG_INFO, "CPU state reset.");
                    send_response(q_res, RES_STATE, "CPU reset complete.");
                    break;

                case CMD_QUIT:
                    send_log(q_log, LOG_QUIT, "Core process quitting.");
                    send_response(q_res, RES_HALTED, "Core process terminated.");
                    running = 0;
                    break;

                default:
                    send_response(q_res, RES_ERROR, "Unknown command.");
                    break;
            }
        }
    }

    // Cleanup
    mq_close(q_cmd);
    mq_close(q_res);
    mq_close(q_log);
    mq_unlink(Q_CMD);
    mq_unlink(Q_RES);
    mq_unlink(Q_LOG);

    printf("=== CORE PROCESS SHUTDOWN ===\n");
    return 0;
}