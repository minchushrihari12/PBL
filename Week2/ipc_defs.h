/* ipc_defs.h - shared interface for all 3 processes. Owned by the Integrator.
 * Do NOT change names/structs without telling the team. */
#ifndef IPC_DEFS_H
#define IPC_DEFS_H

/* UI <-> Core : POSIX message queues */
#define MQ_CMD      "/sim_cmd"        /* UI   -> Core */
#define MQ_REPLY    "/sim_reply"      /* Core -> UI   */
#define MQ_MSG_SZ   256               /* every mq message is <= this, buffers must be >= this */

/* Core : POSIX shared memory (CPU / memory / stack / queue state) */
#define SHM_NAME    "/sim_shm"
#define SHM_SIZE    4096              /* Core's state struct must fit in this */

/* Core -> Logger : named pipe (FIFO) */
#define LOG_FIFO    "/tmp/sim_log.fifo"

/* Integrator <-> everyone : Unix domain socket (control + metrics) */
#define CTRL_SOCK   "/tmp/sim_ctrl.sock"

#define N_CMDS      1000              /* workload size used by stubs, tests, benchmark */

enum { PROC_UI = 0, PROC_CORE, PROC_LOGGER, NPROC };
enum { MSG_READY = 1, MSG_START, MSG_DONE, MSG_SHUTDOWN, MSG_METRICS };

/* fixed-size control message sent over the socket */
typedef struct {
    int    type;     /* MSG_*  */
    int    from;     /* PROC_* */
    long   pid;
    double v1;       /* avg IPC round-trip latency, us (UI only)  */
    double v2;       /* peak memory, KB                            */
    double v3;       /* CPU time (user+sys), ms                    */
} ctrl_msg_t;

/* DEMO shared-memory layout used by the stubs. Core owner replaces this with
 * the real CPU/Memory/Stack/Queue struct (add a process-shared mutex/semaphore). */
typedef struct {
    volatile long counter;
    volatile long last_cmd;
} shm_state_t;

#endif
