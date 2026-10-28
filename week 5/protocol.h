/* protocol.h - message formats shared by core, client and logger. Do not change after day 1. */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define REQ_Q      "/pr_req"        /* clients -> core   */
#define LOG_Q      "/pr_log"        /* core    -> logger */
#define REPLY_FMT  "/pr_reply_%d"   /* core    -> client (one queue per client, named by its PID) */
#define REPLY_PREFIX "/pr_reply_"

#define MSG_SIZE   4096             /* every queue uses this message size */
#define MAX_MSGS   10

typedef struct {                    /* client -> core */
    char reply_q[64];
    int  frames;
    int  algo;
    char refs[1024];
} Request;

#define CHUNK_DATA 4000
typedef struct {                    /* core -> client; JSON is sent in chunks */
    int  seq;
    int  last;                      /* 1 on the final chunk */
    int  len;
    char data[CHUNK_DATA];
} Chunk;

typedef struct {                    /* core -> logger */
    char text[256];
} LogMsg;

#endif
