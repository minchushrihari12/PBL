#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>
#include <time.h>

#include "ipc_defs.h"

#define MEMORY_SIZE 256
#define PROGRAM_SIZE 100

typedef struct {
    int PC;
    int IR;
    int ACC;
    int MAR;
    int MDR;
    int running;
} CPU;

typedef struct {
    char opcode[10];
    int operand;
} Instruction;

static int memory[MEMORY_SIZE];
static Instruction program[PROGRAM_SIZE];
static int programSize = 0;

/* ---------- Utility ---------- */

static void fatal(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void trim_newline(char *s)
{
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n')
        s[n - 1] = '\0';
}

static void initializeCPU(CPU *cpu)
{
    cpu->PC = 0;
    cpu->IR = 0;
    cpu->ACC = 0;
    cpu->MAR = 0;
    cpu->MDR = 0;
    cpu->running = 1;

    memset(memory, 0, sizeof(memory));
    memset(program, 0, sizeof(program));
    programSize = 0;
}

/* ---------- Logger helpers ---------- */

static void send_log(mqd_t qlog, log_level_t level, const char *text)
{
    log_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    msg.level = level;
    msg.timestamp = time(NULL);
    snprintf(msg.text, sizeof(msg.text), "%s", text);

    if (mq_send(qlog, (const char *)&msg, sizeof(msg), 0) == -1)
        perror("mq_send(Q_LOG)");
}

/* ---------- Core -> UI ---------- */

static void send_state(mqd_t qres, CPU *cpu, res_type_t type, const char *text)
{
    res_msg_t res;
    memset(&res, 0, sizeof(res));

    res.type = type;
    res.pc = (uint32_t)cpu->PC;
    res.ir = (uint32_t)cpu->IR;

    /*
     * Your original simulator has ACC, MAR and MDR rather than eight
     * general-purpose registers. We expose them through regs[]:
     *
     * regs[0] = ACC
     * regs[1] = MAR
     * regs[2] = MDR
     */
    res.regs[0] = (uint32_t)cpu->ACC;
    res.regs[1] = (uint32_t)cpu->MAR;
    res.regs[2] = (uint32_t)cpu->MDR;

    res.sp = 0;
    res.flags = cpu->running ? 1u : 0u;

    if (text != NULL)
        snprintf(res.text, sizeof(res.text), "%s", text);

    if (mq_send(qres, (const char *)&res, sizeof(res), 0) == -1)
        perror("mq_send(Q_RES)");
}

/* ---------- Program loading ---------- */

/*
 * Example program file:
 *
 * LOAD 10
 * ADD 11
 * STORE 12
 * OUTPUT
 * HALT
 */
static int loadProgramFromFile(CPU *cpu, const char *filename,
                               char *error_text, size_t error_size)
{
    FILE *fp = fopen(filename, "r");
    char line[256];

    if (fp == NULL) {
        snprintf(error_text, error_size,
                 "Could not open program file: %s", filename);
        return -1;
    }

    memset(program, 0, sizeof(program));
    programSize = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char opcode[10] = {0};
        int operand = 0;
        int fields;

        if (line[0] == '\n' || line[0] == '#')
            continue;

        fields = sscanf(line, "%9s %d", opcode, &operand);
        if (fields < 1)
            continue;

        if (programSize >= PROGRAM_SIZE) {
            fclose(fp);
            snprintf(error_text, error_size,
                     "Program is larger than %d instructions.", PROGRAM_SIZE);
            return -1;
        }

        if (strcmp(opcode, "LOAD") != 0 &&
            strcmp(opcode, "STORE") != 0 &&
            strcmp(opcode, "ADD") != 0 &&
            strcmp(opcode, "SUB") != 0 &&
            strcmp(opcode, "JMP") != 0 &&
            strcmp(opcode, "JZ") != 0 &&
            strcmp(opcode, "INPUT") != 0 &&
            strcmp(opcode, "OUTPUT") != 0 &&
            strcmp(opcode, "HALT") != 0) {

            fclose(fp);
            snprintf(error_text, error_size,
                     "Unknown instruction in file: %s", opcode);
            return -1;
        }

        snprintf(program[programSize].opcode,
                 sizeof(program[programSize].opcode),
                 "%s", opcode);

        if (strcmp(opcode, "LOAD") == 0 ||
            strcmp(opcode, "STORE") == 0 ||
            strcmp(opcode, "ADD") == 0 ||
            strcmp(opcode, "SUB") == 0 ||
            strcmp(opcode, "JMP") == 0 ||
            strcmp(opcode, "JZ") == 0) {

            if (fields != 2) {
                fclose(fp);
                snprintf(error_text, error_size,
                         "%s requires an operand.", opcode);
                return -1;
            }

            program[programSize].operand = operand;
        }

        programSize++;
    }

    fclose(fp);

    if (programSize == 0) {
        snprintf(error_text, error_size, "Program file is empty.");
        return -1;
    }

    cpu->PC = 0;
    cpu->IR = 0;
    cpu->ACC = 0;
    cpu->MAR = 0;
    cpu->MDR = 0;
    cpu->running = 1;

    snprintf(error_text, error_size,
             "Loaded %d instructions from %s", programSize, filename);

    return 0;
}

/* ---------- Instruction execution ---------- */

static int valid_memory_address(int address)
{
    return address >= 0 && address < MEMORY_SIZE;
}

static int valid_program_address(int address)
{
    return address >= 0 && address < programSize;
}

/*
 * Returns:
 *   0 = instruction executed
 *   1 = HALT
 *  -1 = error
 */
static int executeInstruction(CPU *cpu, char *status, size_t status_size,
                              mqd_t qlog)
{
    Instruction *instruction;
    char logtext[TEXT_LEN];

    if (programSize == 0) {
        snprintf(status, status_size, "No program loaded.");
        return -1;
    }

    if (!valid_program_address(cpu->PC)) {
        snprintf(status, status_size, "Program Counter out of range.");
        cpu->running = 0;
        return -1;
    }

    instruction = &program[cpu->PC];
    cpu->IR = cpu->PC;

    snprintf(logtext, sizeof(logtext),
             "PC=%d executing %s %d",
             cpu->PC, instruction->opcode, instruction->operand);
    send_log(qlog, LOG_INFO, logtext);

    if (strcmp(instruction->opcode, "LOAD") == 0) {
        if (!valid_memory_address(instruction->operand)) {
            snprintf(status, status_size, "Invalid LOAD memory address.");
            cpu->running = 0;
            return -1;
        }

        cpu->MAR = instruction->operand;
        cpu->MDR = memory[cpu->MAR];
        cpu->ACC = cpu->MDR;
        cpu->PC++;
    }
    else if (strcmp(instruction->opcode, "STORE") == 0) {
        if (!valid_memory_address(instruction->operand)) {
            snprintf(status, status_size, "Invalid STORE memory address.");
            cpu->running = 0;
            return -1;
        }

        cpu->MAR = instruction->operand;
        cpu->MDR = cpu->ACC;
        memory[cpu->MAR] = cpu->MDR;
        cpu->PC++;
    }
    else if (strcmp(instruction->opcode, "ADD") == 0) {
        if (!valid_memory_address(instruction->operand)) {
            snprintf(status, status_size, "Invalid ADD memory address.");
            cpu->running = 0;
            return -1;
        }

        cpu->MAR = instruction->operand;
        cpu->MDR = memory[cpu->MAR];
        cpu->ACC += cpu->MDR;
        cpu->PC++;
    }
    else if (strcmp(instruction->opcode, "SUB") == 0) {
        if (!valid_memory_address(instruction->operand)) {
            snprintf(status, status_size, "Invalid SUB memory address.");
            cpu->running = 0;
            return -1;
        }

        cpu->MAR = instruction->operand;
        cpu->MDR = memory[cpu->MAR];
        cpu->ACC -= cpu->MDR;
        cpu->PC++;
    }
    else if (strcmp(instruction->opcode, "JMP") == 0) {
        if (!valid_program_address(instruction->operand)) {
            snprintf(status, status_size, "Invalid JMP address.");
            cpu->running = 0;
            return -1;
        }

        cpu->PC = instruction->operand;
    }
    else if (strcmp(instruction->opcode, "JZ") == 0) {
        if (cpu->ACC == 0) {
            if (!valid_program_address(instruction->operand)) {
                snprintf(status, status_size, "Invalid JZ address.");
                cpu->running = 0;
                return -1;
            }
            cpu->PC = instruction->operand;
        } else {
            cpu->PC++;
        }
    }
    else if (strcmp(instruction->opcode, "INPUT") == 0) {
        int value;

        /*
         * This keeps INPUT compatible with your original simulator.
         * The Core temporarily reads the value from the terminal.
         */
        printf("\n[CORE] Enter value for ACC: ");
        fflush(stdout);

        if (scanf("%d", &value) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            snprintf(status, status_size, "Invalid INPUT value.");
            cpu->running = 0;
            return -1;
        }

        cpu->ACC = value;
        cpu->PC++;
    }
    else if (strcmp(instruction->opcode, "OUTPUT") == 0) {
        snprintf(status, status_size, "OUTPUT = %d", cpu->ACC);
        send_log(qlog, LOG_INFO, status);
        cpu->PC++;
        return 0;
    }
    else if (strcmp(instruction->opcode, "HALT") == 0) {
        cpu->running = 0;
        snprintf(status, status_size, "CPU HALTED.");
        send_log(qlog, LOG_INFO, "CPU halted.");
        return 1;
    }
    else {
        snprintf(status, status_size,
                 "Unknown instruction: %s", instruction->opcode);
        cpu->running = 0;
        return -1;
    }

    snprintf(status, status_size,
             "Executed %s. ACC=%d PC=%d",
             instruction->opcode, cpu->ACC, cpu->PC);

    return 0;
}

/* ---------- CORE process ---------- */

static void core_process(void)
{
    mqd_t qcmd;
    mqd_t qres;
    mqd_t qlog;
    CPU cpu;

    qcmd = mq_open(Q_CMD, O_RDONLY);
    qres = mq_open(Q_RES, O_WRONLY);
    qlog = mq_open(Q_LOG, O_WRONLY);

    if (qcmd == (mqd_t)-1 ||
        qres == (mqd_t)-1 ||
        qlog == (mqd_t)-1) {
        fatal("[CORE] mq_open");
    }

    initializeCPU(&cpu);
    send_log(qlog, LOG_INFO, "Core process started.");

    for (;;) {
        cmd_msg_t cmd;
        char status[TEXT_LEN] = {0};

        memset(&cmd, 0, sizeof(cmd));

        if (mq_receive(qcmd, (char *)&cmd, sizeof(cmd), NULL) == -1) {
            perror("[CORE] mq_receive");
            continue;
        }

        switch (cmd.type) {
        case CMD_LOAD:
            if (loadProgramFromFile(&cpu, cmd.text,
                                    status, sizeof(status)) == 0) {
                send_log(qlog, LOG_INFO, status);
                send_state(qres, &cpu, RES_STATE, status);
            } else {
                send_log(qlog, LOG_ERROR, status);
                send_state(qres, &cpu, RES_ERROR, status);
            }
            break;

        case CMD_STEP: {
            int result;

            if (programSize == 0) {
                snprintf(status, sizeof(status), "No program loaded.");
                send_state(qres, &cpu, RES_ERROR, status);
                break;
            }

            if (!cpu.running) {
                snprintf(status, sizeof(status), "CPU is halted.");
                send_state(qres, &cpu, RES_HALTED, status);
                break;
            }

            result = executeInstruction(&cpu, status, sizeof(status), qlog);

            if (result == 1)
                send_state(qres, &cpu, RES_HALTED, status);
            else if (result == -1) {
                send_log(qlog, LOG_ERROR, status);
                send_state(qres, &cpu, RES_ERROR, status);
            } else
                send_state(qres, &cpu, RES_STATE, status);

            break;
        }

        case CMD_RUN: {
            int count = 0;
            int result = 0;

            if (programSize == 0) {
                snprintf(status, sizeof(status), "No program loaded.");
                send_state(qres, &cpu, RES_ERROR, status);
                break;
            }

            cpu.running = 1;

            while (cpu.running) {
                result = executeInstruction(&cpu, status,
                                            sizeof(status), qlog);
                count++;

                if (result != 0)
                    break;

                if (count > 1000) {
                    cpu.running = 0;
                    snprintf(status, sizeof(status),
                             "Execution stopped: possible infinite loop.");
                    send_log(qlog, LOG_ERROR, status);
                    result = -1;
                    break;
                }
            }

            if (result == 1)
                send_state(qres, &cpu, RES_HALTED, status);
            else if (result == -1)
                send_state(qres, &cpu, RES_ERROR, status);
            else
                send_state(qres, &cpu, RES_STATE, status);

            break;
        }

        case CMD_RESET:
            initializeCPU(&cpu);
            snprintf(status, sizeof(status),
                     "CPU and memory reset.");
            send_log(qlog, LOG_INFO, status);
            send_state(qres, &cpu, RES_STATE, status);
            break;

        case CMD_QUIT: {
            log_msg_t quit_log;

            snprintf(status, sizeof(status),
                     "Core shutting down.");
            send_state(qres, &cpu, RES_HALTED, status);

            memset(&quit_log, 0, sizeof(quit_log));
            quit_log.level = LOG_QUIT;
            quit_log.timestamp = time(NULL);
            snprintf(quit_log.text, sizeof(quit_log.text),
                     "Logger shutting down.");

            mq_send(qlog, (const char *)&quit_log,
                    sizeof(quit_log), 0);

            mq_close(qcmd);
            mq_close(qres);
            mq_close(qlog);

            exit(EXIT_SUCCESS);
        }

        default:
            snprintf(status, sizeof(status),
                     "Unknown command received by Core.");
            send_log(qlog, LOG_ERROR, status);
            send_state(qres, &cpu, RES_ERROR, status);
            break;
        }
    }
}

/* ---------- LOGGER process ---------- */

static void logger_process(void)
{
    mqd_t qlog;
    FILE *fp;

    qlog = mq_open(Q_LOG, O_RDONLY);
    if (qlog == (mqd_t)-1)
        fatal("[LOGGER] mq_open");

    fp = fopen("simulator.log", "a");
    if (fp == NULL)
        fatal("[LOGGER] fopen");

    printf("[LOGGER] Started. Writing to simulator.log\n");

    for (;;) {
        log_msg_t msg;
        struct tm *tm_info;
        char timebuf[32];

        memset(&msg, 0, sizeof(msg));

        if (mq_receive(qlog, (char *)&msg,
                       sizeof(msg), NULL) == -1) {
            perror("[LOGGER] mq_receive");
            continue;
        }

        tm_info = localtime(&msg.timestamp);
        if (tm_info != NULL)
            strftime(timebuf, sizeof(timebuf),
                     "%Y-%m-%d %H:%M:%S", tm_info);
        else
            snprintf(timebuf, sizeof(timebuf), "unknown-time");

        if (msg.level == LOG_QUIT) {
            fprintf(fp, "[%s] [QUIT] %s\n",
                    timebuf, msg.text);
            fflush(fp);
            break;
        }

        fprintf(fp, "[%s] [%s] %s\n",
                timebuf,
                msg.level == LOG_ERROR ? "ERROR" : "INFO",
                msg.text);
        fflush(fp);
    }

    fclose(fp);
    mq_close(qlog);

    printf("[LOGGER] Exiting.\n");
    exit(EXIT_SUCCESS);
}

/* ---------- UI process ---------- */

static void print_state(const res_msg_t *res)
{
    printf("\n========================================\n");

    if (res->type == RES_ERROR)
        printf("RESULT: ERROR\n");
    else if (res->type == RES_HALTED)
        printf("RESULT: HALTED\n");
    else
        printf("RESULT: STATE\n");

    printf("%s\n", res->text);
    printf("----------------------------------------\n");
    printf("PC  : %u\n", res->pc);
    printf("IR  : %u\n", res->ir);
    printf("ACC : %d\n", (int32_t)res->regs[0]);
    printf("MAR : %u\n", res->regs[1]);
    printf("MDR : %d\n", (int32_t)res->regs[2]);
    printf("RUNNING FLAG : %u\n", res->flags);
    printf("========================================\n");
}

static void ui_process(void)
{
    mqd_t qcmd;
    mqd_t qres;

    qcmd = mq_open(Q_CMD, O_WRONLY);
    qres = mq_open(Q_RES, O_RDONLY);

    if (qcmd == (mqd_t)-1 || qres == (mqd_t)-1)
        fatal("[UI] mq_open");

    for (;;) {
        int choice;
        cmd_msg_t cmd;
        res_msg_t res;

        memset(&cmd, 0, sizeof(cmd));
        memset(&res, 0, sizeof(res));

        printf("\n");
        printf("=========== TOF CPU IPC SIMULATOR ===========\n");
        printf("1. Load Program\n");
        printf("2. Execute Next Instruction\n");
        printf("3. Run Program\n");
        printf("4. Reset CPU\n");
        printf("5. Quit\n");
        printf("==============================================\n");
        printf("Enter choice: ");
        fflush(stdout);

        if (scanf("%d", &choice) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            printf("Invalid choice.\n");
            continue;
        }

        switch (choice) {
        case 1: {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}

            cmd.type = CMD_LOAD;

            printf("Enter program filename: ");
            fflush(stdout);

            if (fgets(cmd.text, sizeof(cmd.text), stdin) == NULL)
                continue;

            trim_newline(cmd.text);
            break;
        }

        case 2:
            cmd.type = CMD_STEP;
            break;

        case 3:
            cmd.type = CMD_RUN;
            break;

        case 4:
            cmd.type = CMD_RESET;
            break;

        case 5:
            cmd.type = CMD_QUIT;
            break;

        default:
            printf("Invalid choice.\n");
            continue;
        }

        if (mq_send(qcmd, (const char *)&cmd,
                    sizeof(cmd), 0) == -1) {
            perror("[UI] mq_send");
            continue;
        }

        if (mq_receive(qres, (char *)&res,
                       sizeof(res), NULL) == -1) {
            perror("[UI] mq_receive");
            continue;
        }

        print_state(&res);

        if (cmd.type == CMD_QUIT)
            break;
    }

    mq_close(qcmd);
    mq_close(qres);

    printf("[UI] Exiting.\n");
    exit(EXIT_SUCCESS);
}

/* ---------- Queue creation ---------- */

static mqd_t create_queue(const char *name, long message_size)
{
    struct mq_attr attr;
    mqd_t q;

    memset(&attr, 0, sizeof(attr));
    attr.mq_flags = 0;
    attr.mq_maxmsg = Q_MAXMSG;
    attr.mq_msgsize = message_size;
    attr.mq_curmsgs = 0;

    q = mq_open(name, O_CREAT | O_RDWR, 0600, &attr);

    if (q == (mqd_t)-1)
        fatal(name);

    return q;
}

/* ---------- MAIN: creates all 3 processes ---------- */

int main(void)
{
    mqd_t qcmd;
    mqd_t qres;
    mqd_t qlog;

    pid_t ui_pid;
    pid_t core_pid;
    pid_t logger_pid;

    /*
     * Remove queues left by an earlier crashed run.
     */
    mq_unlink(Q_CMD);
    mq_unlink(Q_RES);
    mq_unlink(Q_LOG);

    qcmd = create_queue(Q_CMD, sizeof(cmd_msg_t));
    qres = create_queue(Q_RES, sizeof(res_msg_t));
    qlog = create_queue(Q_LOG, sizeof(log_msg_t));

    /*
     * The children reopen the queues with only the access
     * permissions they actually need.
     */
    mq_close(qcmd);
    mq_close(qres);
    mq_close(qlog);

    logger_pid = fork();
    if (logger_pid < 0)
        fatal("fork logger");

    if (logger_pid == 0)
        logger_process();

    core_pid = fork();
    if (core_pid < 0)
        fatal("fork core");

    if (core_pid == 0)
        core_process();

    ui_pid = fork();
    if (ui_pid < 0)
        fatal("fork ui");

    if (ui_pid == 0)
        ui_process();

    /*
     * Parent/launcher waits until the three processes finish.
     */
    waitpid(ui_pid, NULL, 0);
    waitpid(core_pid, NULL, 0);
    waitpid(logger_pid, NULL, 0);

    mq_unlink(Q_CMD);
    mq_unlink(Q_RES);
    mq_unlink(Q_LOG);

    printf("\n[PARENT] All processes finished. IPC queues removed.\n");

    return 0;
}
