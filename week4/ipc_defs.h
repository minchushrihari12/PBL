/*
 * ipc_defs.h  --  Week 4 Deadlock Simulation System
 * Shared IPC contract between ui, core and logger.  (Owner: Integrator)
 *
 * Transport : System V message queues (msgget/msgsnd/msgrcv).
 *             POSIX mq_* is NOT available on macOS, System V works on
 *             macOS, Linux and WSL.
 *
 *   ui  --KEY_CORE-->  core      text commands   (cmd_msg_t)
 *   ui  <--KEY_UI----  core      JSON replies    (reply_msg_t, chunked)
 *   ui/core --KEY_LOG--> logger  log lines       (log_msg_t)
 *
 * macOS limits a queue to 2048 bytes by default, so every message is
 * kept well below 1 KB and long JSON replies are split into chunks.
 */
#ifndef IPC_DEFS_H
#define IPC_DEFS_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define KEY_CORE 0x44450001   /* ui   -> core   */
#define KEY_UI   0x44450002   /* core -> ui     */
#define KEY_LOG  0x44450003   /* any  -> logger */

#define CMD_LEN   480
#define CHUNK_LEN 800
#define LOG_LEN   200

#define LOG_INFO  0
#define LOG_WARN  1
#define LOG_ERROR 2

typedef struct {            /* ui -> core */
    long mtype;             /* always 1 */
    int  req_id;
    char text[CMD_LEN];     /* e.g. "REQUEST 1 1 0 2" */
} cmd_msg_t;

typedef struct {            /* core -> ui (one JSON document, many chunks) */
    long mtype;             /* always 1 */
    int  req_id;
    int  seq;
    int  last;              /* 1 on the final chunk */
    int  len;
    char data[CHUNK_LEN];
} reply_msg_t;

typedef struct {            /* ui/core -> logger */
    long mtype;             /* always 1 */
    int  level;
    char source[12];
    char text[LOG_LEN];
} log_msg_t;

/* size argument for msgsnd/msgrcv */
#define MSG_SIZE(t) (sizeof(t) - sizeof(long))

static inline int ipc_open(key_t key)
{
    return msgget(key, IPC_CREAT | 0666);
}

/* fire-and-forget log helper; silently drops if logger/queue is unavailable */
static inline void ipc_log(int qid, int level, const char *src, const char *fmt, ...)
{
    log_msg_t m;
    va_list ap;
    memset(&m, 0, sizeof m);
    m.mtype = 1;
    m.level = level;
    strncpy(m.source, src, sizeof m.source - 1);
    va_start(ap, fmt);
    vsnprintf(m.text, LOG_LEN, fmt, ap);
    va_end(ap);
    msgsnd(qid, &m, MSG_SIZE(log_msg_t), IPC_NOWAIT);
}

#endif /* IPC_DEFS_H */
