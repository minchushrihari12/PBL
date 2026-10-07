/*
 * CORE process: owns the CPU, memory and program.
 *   reads  commands  from /sim_cmd  (UI   -> Core)
 *   writes results   to   /sim_res  (Core -> UI)
 *   writes log lines to   /sim_log  (Core -> Logger)
 *
 * Program file format (one item per line, '#' starts a comment):
 *   DATA <addr> <value>      pre-load a memory word
 *   LOAD/STORE/ADD/SUB/JMP/JZ <operand>
 *   INPUT / OUTPUT / HALT
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include "ipc_defs.h"

#define MAX_STEPS_PER_RUN 1000

typedef enum { OP_LOAD, OP_STORE, OP_ADD, OP_SUB, OP_JMP, OP_JZ,
               OP_INPUT, OP_OUTPUT, OP_HALT } opcode_t;

static const struct { const char *name; opcode_t op; int has_operand; } optab[] = {
    {"LOAD",  OP_LOAD,  1}, {"STORE", OP_STORE, 1}, {"ADD",  OP_ADD,  1},
    {"SUB",   OP_SUB,   1}, {"JMP",   OP_JMP,   1}, {"JZ",   OP_JZ,   1},
    {"INPUT", OP_INPUT, 0}, {"OUTPUT",OP_OUTPUT,0}, {"HALT", OP_HALT, 0}
};
#define NUM_OPS ((int)(sizeof(optab) / sizeof(optab[0])))

typedef struct { opcode_t op; int operand; } instr_t;
typedef struct { int addr; int value; }      data_init_t;
typedef struct { int pc, ir, acc, mar, mdr, running; } cpu_t;

static cpu_t       cpu;
static int         memory[MEMORY_SIZE];
static instr_t     program[MAX_PROGRAM];
static int         program_size = 0;
static data_init_t data_init[MEMORY_SIZE];
static int         data_count = 0;
static char        out_buf[TEXT_LEN];          /* OUTPUT values of current command */

static mqd_t q_cmd = (mqd_t)-1, q_res = (mqd_t)-1, q_log = (mqd_t)-1;
static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig) { (void)sig; stop_requested = 1; }

/* ---------- IPC helpers ---------- */

static void send_log(log_level_t level, const char *text)
{
    log_msg_t m;
    memset(&m, 0, sizeof(m));
    m.level = level;
    m.timestamp = time(NULL);
    snprintf(m.text, sizeof(m.text), "%s", text);
    if (ipc_send_timed(q_log, &m, sizeof(m), IPC_TIMEOUT_SEC) == -1)
        fprintf(stderr, "[CORE] log not delivered (is the logger running?): %s\n",
                strerror(errno));
}

static void send_state(res_type_t type, const char *text)
{
    res_msg_t r;
    memset(&r, 0, sizeof(r));
    r.type  = type;
    r.pc    = (uint32_t)cpu.pc;
    r.ir    = (uint32_t)cpu.ir;
    r.regs[REG_ACC] = (uint32_t)cpu.acc;
    r.regs[REG_MAR] = (uint32_t)cpu.mar;
    r.regs[REG_MDR] = (uint32_t)cpu.mdr;
    r.sp    = 0;
    r.flags = cpu.running ? 1u : 0u;
    if (text) snprintf(r.text, sizeof(r.text), "%s", text);
    if (ipc_send_timed(q_res, &r, sizeof(r), IPC_TIMEOUT_SEC) == -1)
        fprintf(stderr, "[CORE] response not delivered (is the UI running?): %s\n",
                strerror(errno));
}

/* ---------- CPU ---------- */

/* Reset registers + memory. The loaded program is kept. */
static void reset_cpu(void)
{
    memset(&cpu, 0, sizeof(cpu));
    memset(memory, 0, sizeof(memory));
    for (int i = 0; i < data_count; i++)
        memory[data_init[i].addr] = data_init[i].value;
    cpu.running = (program_size > 0);
    out_buf[0] = '\0';
}

static int valid_mem(int a)  { return a >= 0 && a < MEMORY_SIZE; }
static int valid_prog(int a) { return a >= 0 && a < program_size; }

/* Parse into temporaries; only commit if the whole file is valid. */
static int load_program(const char *filename, char *err, size_t errsz)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        snprintf(err, errsz, "Cannot open '%s': %s", filename, strerror(errno));
        return -1;
    }

    instr_t     tmp_prog[MAX_PROGRAM];
    data_init_t tmp_data[MEMORY_SIZE];
    int n_prog = 0, n_data = 0, lineno = 0;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        char word[16] = {0};
        int a = 0, b = 0;
        char *hash = strchr(line, '#');
        lineno++;
        if (hash) *hash = '\0';

        int fields = sscanf(line, "%15s %d %d", word, &a, &b);
        if (fields < 1) continue;                       /* blank / comment */
        for (char *p = word; *p; p++) *p = (char)toupper((unsigned char)*p);

        if (strcmp(word, "DATA") == 0) {
            if (fields != 3 || !valid_mem(a) || n_data >= MEMORY_SIZE) {
                snprintf(err, errsz, "Line %d: bad DATA (use: DATA addr value)", lineno);
                fclose(fp); return -1;
            }
            tmp_data[n_data].addr  = a;
            tmp_data[n_data].value = b;
            n_data++;
            continue;
        }

        int idx = -1;
        for (int i = 0; i < NUM_OPS; i++)
            if (strcmp(word, optab[i].name) == 0) { idx = i; break; }
        if (idx < 0) {
            snprintf(err, errsz, "Line %d: unknown instruction '%s'", lineno, word);
            fclose(fp); return -1;
        }
        if (optab[idx].has_operand && fields < 2) {
            snprintf(err, errsz, "Line %d: %s needs an operand", lineno, word);
            fclose(fp); return -1;
        }
        if (n_prog >= MAX_PROGRAM) {
            snprintf(err, errsz, "Program larger than %d instructions", MAX_PROGRAM);
            fclose(fp); return -1;
        }
        tmp_prog[n_prog].op      = optab[idx].op;
        tmp_prog[n_prog].operand = optab[idx].has_operand ? a : 0;
        n_prog++;
    }
    fclose(fp);

    if (n_prog == 0) {
        snprintf(err, errsz, "Program file has no instructions");
        return -1;
    }

    memcpy(program, tmp_prog, (size_t)n_prog * sizeof(instr_t));
    memcpy(data_init, tmp_data, (size_t)n_data * sizeof(data_init_t));
    program_size = n_prog;
    data_count   = n_data;
    reset_cpu();

    snprintf(err, errsz, "Loaded %d instructions, %d data words from %s",
             n_prog, n_data, filename);
    return 0;
}

/* Returns 0 = executed, 1 = halted (HALT or ran off the end), -1 = error */
static int execute_one(char *status, size_t sz)
{
    if (cpu.pc >= program_size && program_size > 0) {
        cpu.running = 0;
        snprintf(status, sz, "Program ended (no HALT). CPU halted.");
        send_log(LOG_INFO, status);
        return 1;
    }
    if (!valid_prog(cpu.pc)) {
        cpu.running = 0;
        snprintf(status, sz, "Program counter out of range.");
        return -1;
    }

    const instr_t *in = &program[cpu.pc];
    char lt[TEXT_LEN];
    cpu.ir = cpu.pc;

    if (optab[in->op].has_operand)
        snprintf(lt, sizeof(lt), "PC=%d executing %s %d", cpu.pc, optab[in->op].name, in->operand);
    else
        snprintf(lt, sizeof(lt), "PC=%d executing %s", cpu.pc, optab[in->op].name);
    send_log(LOG_INFO, lt);

    switch (in->op) {
    case OP_LOAD: case OP_STORE: case OP_ADD: case OP_SUB:
        if (!valid_mem(in->operand)) {
            cpu.running = 0;
            snprintf(status, sz, "Invalid memory address %d in %s.",
                     in->operand, optab[in->op].name);
            return -1;
        }
        cpu.mar = in->operand;
        if (in->op == OP_STORE) {
            cpu.mdr = cpu.acc;
            memory[cpu.mar] = cpu.mdr;
        } else {
            cpu.mdr = memory[cpu.mar];
            if      (in->op == OP_LOAD) cpu.acc = cpu.mdr;
            else if (in->op == OP_ADD)  cpu.acc = (int)((unsigned)cpu.acc + (unsigned)cpu.mdr);
            else                        cpu.acc = (int)((unsigned)cpu.acc - (unsigned)cpu.mdr);
        }
        cpu.pc++;
        break;

    case OP_JMP:
        if (!valid_prog(in->operand)) {
            cpu.running = 0;
            snprintf(status, sz, "Invalid JMP target %d.", in->operand);
            return -1;
        }
        cpu.pc = in->operand;
        break;

    case OP_JZ:
        if (cpu.acc == 0) {
            if (!valid_prog(in->operand)) {
                cpu.running = 0;
                snprintf(status, sz, "Invalid JZ target %d.", in->operand);
                return -1;
            }
            cpu.pc = in->operand;
        } else {
            cpu.pc++;
        }
        break;

    case OP_INPUT: {
        char buf[64];
        char *end;
        long v;
        printf("[CORE] INPUT - enter a value for ACC: ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) {
            cpu.running = 0;
            snprintf(status, sz, "INPUT failed (no data on Core's stdin).");
            return -1;
        }
        v = strtol(buf, &end, 10);
        if (end == buf) {
            cpu.running = 0;
            snprintf(status, sz, "Invalid INPUT value.");
            return -1;
        }
        cpu.acc = (int)v;
        cpu.pc++;
        break;
    }

    case OP_OUTPUT: {
        char item[24];
        snprintf(item, sizeof(item), "%s%d", out_buf[0] ? " " : "", cpu.acc);
        strncat(out_buf, item, sizeof(out_buf) - strlen(out_buf) - 1);
        printf("[CORE] OUTPUT = %d\n", cpu.acc);
        snprintf(lt, sizeof(lt), "OUTPUT = %d", cpu.acc);
        send_log(LOG_INFO, lt);
        cpu.pc++;
        break;
    }

    case OP_HALT:
        cpu.running = 0;
        snprintf(status, sz, "CPU HALTED.");
        send_log(LOG_INFO, "CPU halted.");
        return 1;
    }

    snprintf(status, sz, "Executed %s. ACC=%d PC=%d",
             optab[in->op].name, cpu.acc, cpu.pc);
    return 0;
}

/* Build final text = status + collected OUTPUT values, then reply once. */
static void reply_exec(int result, const char *status)
{
    char text[TEXT_LEN];
    snprintf(text, sizeof(text), "%s%s%s", status,
             out_buf[0] ? " | OUTPUT: " : "", out_buf);

    if (result == -1) {
        send_log(LOG_ERROR, status);
        send_state(RES_ERROR, text);
    } else if (result == 1) {
        send_state(RES_HALTED, text);
    } else {
        send_state(RES_STATE, text);
    }
}

/* ---------- main ---------- */

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;            /* no SA_RESTART: mq_receive -> EINTR */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    q_cmd = ipc_open_queue(Q_CMD, O_RDONLY, sizeof(cmd_msg_t));
    q_res = ipc_open_queue(Q_RES, O_WRONLY, sizeof(res_msg_t));
    q_log = ipc_open_queue(Q_LOG, O_WRONLY, sizeof(log_msg_t));
    if (q_cmd == (mqd_t)-1 || q_res == (mqd_t)-1 || q_log == (mqd_t)-1) {
        perror("[CORE] mq_open");
        fprintf(stderr, "[CORE] If this follows a code change, run: make clean-queues\n");
        return 1;
    }

    reset_cpu();
    printf("=== CORE PROCESS ONLINE (listening on %s) ===\n", Q_CMD);
    send_log(LOG_INFO, "Core process started.");

    while (!stop_requested) {
        cmd_msg_t cmd;
        char status[TEXT_LEN] = {0};

        memset(&cmd, 0, sizeof(cmd));
        if (mq_receive(q_cmd, (char *)&cmd, sizeof(cmd), NULL) == -1) {
            if (errno == EINTR) continue;           /* re-check stop_requested */
            perror("[CORE] mq_receive");
            break;
        }
        cmd.text[TEXT_LEN - 1] = '\0';
        out_buf[0] = '\0';

        if (cmd.type == CMD_QUIT) {
            send_log(LOG_INFO, "Core shutting down.");
            send_state(RES_HALTED, "Core shutting down.");
            break;
        }

        switch (cmd.type) {
        case CMD_LOAD:
            if (load_program(cmd.text, status, sizeof(status)) == 0) {
                send_log(LOG_INFO, status);
                send_state(RES_STATE, status);
            } else {
                send_log(LOG_ERROR, status);
                send_state(RES_ERROR, status);
            }
            break;

        case CMD_STEP:
        case CMD_RUN: {
            if (program_size == 0) {
                send_state(RES_ERROR, "No program loaded.");
                break;
            }
            if (!cpu.running) {
                send_state(RES_HALTED, "CPU is halted. Use Reset to run again.");
                break;
            }

            int result = 0, steps = 0;
            if (cmd.type == CMD_STEP) {
                result = execute_one(status, sizeof(status));
            } else {
                while (cpu.running) {
                    result = execute_one(status, sizeof(status));
                    steps++;
                    if (result != 0) break;
                    if (steps >= MAX_STEPS_PER_RUN) {
                        cpu.running = 0;
                        snprintf(status, sizeof(status),
                                 "Stopped after %d steps: possible infinite loop.", steps);
                        result = -1;
                        break;
                    }
                }
            }
            reply_exec(result, status);
            break;
        }

        case CMD_RESET:
            reset_cpu();
            send_log(LOG_INFO, "CPU and memory reset.");
            send_state(RES_STATE, "CPU and memory reset.");
            break;

        default:
            send_log(LOG_ERROR, "Unknown command received by Core.");
            send_state(RES_ERROR, "Unknown command.");
            break;
        }
    }

    /* Shutdown: tell Logger to flush/exit, then remove the queues. */
    send_log(LOG_QUIT, "Core terminated. Logger shutting down.");

    mq_close(q_cmd);
    mq_close(q_res);
    mq_close(q_log);
    mq_unlink(Q_CMD);
    mq_unlink(Q_RES);
    mq_unlink(Q_LOG);

    printf("=== CORE PROCESS SHUTDOWN ===\n");
    return 0;
}
