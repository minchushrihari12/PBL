#ifndef IPC_DEFS_H
#define IPC_DEFS_H

#include <stdint.h>
#include <time.h>

/* ---- Queue names (POSIX message queues must start with '/') ---- */
#define Q_CMD  "/sim_cmd"   /* UI   -> Core   : commands        */
#define Q_RES  "/sim_res"   /* Core -> UI     : state / results */
#define Q_LOG  "/sim_log"   /* Core -> Logger : log events      */

/* ---- Queue limits ---- */
#define Q_MAXMSG   10       /* default Linux limit is 10 without root */

/* ---- Simulator sizes: CHANGE to match your simulator ---- */
#define NUM_REGS   8
#define TEXT_LEN   128

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

/* ---- Core -> UI ---- */
typedef enum {
    RES_STATE = 1,  /* normal state update  */
    RES_HALTED,     /* program finished     */
    RES_ERROR       /* something went wrong */
} res_type_t;

typedef struct {
    res_type_t type;
    uint32_t   pc;                 /* program counter     */
    uint32_t   ir;                 /* instruction register */
    uint32_t   regs[NUM_REGS];     /* general registers   */
    uint32_t   sp;                 /* stack pointer       */
    uint32_t   flags;              /* status flags        */
    char       text[TEXT_LEN];     /* error / info text   */
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
    char        text[TEXT_LEN];    /* e.g. "PC=4 executed ADD R1,R2" */
} log_msg_t;

#endif /* IPC_DEFS_H */
