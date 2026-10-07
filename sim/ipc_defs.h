#ifndef IPC_DEFS_H
#define IPC_DEFS_H

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>

/* ---- Queue names (POSIX message queues must start with '/') ---- */
#define Q_CMD  "/sim_cmd"   /* UI   -> Core   : commands        */
#define Q_RES  "/sim_res"   /* Core -> UI     : state / results */
#define Q_LOG  "/sim_log"   /* Core -> Logger : log events      */

/* ---- Limits (Linux default mq_maxmsg is 10 without root) ---- */
#define Q_MAXMSG        10
#define IPC_TIMEOUT_SEC 5      /* max wait when a queue is full/empty (Core/Logger) */

/* ---- Simulator sizes ---- */
#define NUM_REGS     8
#define TEXT_LEN     128
#define MEMORY_SIZE  256       /* data memory words        */
#define MAX_PROGRAM  100       /* max instructions         */

/* How the Core's CPU registers are exposed through res_msg_t.regs[] */
#define REG_ACC 0
#define REG_MAR 1
#define REG_MDR 2

/* ---- UI -> Core ---- */
typedef enum {
    CMD_LOAD = 1,   /* load program, filename in text */
    CMD_RUN,
    CMD_STEP,
    CMD_RESET,
    CMD_QUIT
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    char       text[TEXT_LEN];   /* filename for CMD_LOAD, else unused */
} cmd_msg_t;

/* ---- Core -> UI ---- (exactly ONE response per command) */
typedef enum {
    RES_STATE = 1,  /* normal state update  */
    RES_HALTED,     /* program finished     */
    RES_ERROR       /* something went wrong */
} res_type_t;

typedef struct {
    res_type_t type;
    uint32_t   pc;                 /* program counter      */
    uint32_t   ir;                 /* instruction register */
    uint32_t   regs[NUM_REGS];     /* regs[0]=ACC [1]=MAR [2]=MDR */
    uint32_t   sp;                 /* unused (0)           */
    uint32_t   flags;              /* 1 = CPU running, 0 = halted */
    char       text[TEXT_LEN];     /* error / info text    */
} res_msg_t;

/* ---- Core -> Logger ---- */
typedef enum {
    LOG_INFO = 1,
    LOG_ERROR,
    LOG_QUIT        /* tells Logger to flush and exit */
} log_level_t;

typedef struct {
    log_level_t level;
    time_t      timestamp;
    char        text[TEXT_LEN];
} log_msg_t;

/* ---- Shared helpers ----
 * Every process opens the queues with O_CREAT and identical attributes, so
 * the start-up ORDER of Logger / Core / UI does not matter. */
static inline mqd_t ipc_open_queue(const char *name, int access, long msgsize)
{
    struct mq_attr a;
    memset(&a, 0, sizeof(a));
    a.mq_maxmsg  = Q_MAXMSG;
    a.mq_msgsize = msgsize;
    return mq_open(name, access | O_CREAT, 0666, &a);
}

/* send that gives up after 'sec' seconds instead of blocking forever */
static inline int ipc_send_timed(mqd_t q, const void *msg, size_t len, int sec)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += sec;
    while (mq_timedsend(q, (const char *)msg, len, 0, &ts) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static inline ssize_t ipc_recv_timed(mqd_t q, void *buf, size_t len, int sec)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += sec;
    for (;;) {
        ssize_t n = mq_timedreceive(q, (char *)buf, len, NULL, &ts);
        if (n == -1 && errno == EINTR) continue;
        return n;
    }
}

#endif /* IPC_DEFS_H */
