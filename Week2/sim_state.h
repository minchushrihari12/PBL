/* sim_state.h - layout of the shared memory owned by CORE.
 * Core owner: extend this (e.g. process table, page table) but keep it <= SHM_SIZE.
 * UI reads it for display (always take st->lock first). */
#ifndef SIM_STATE_H
#define SIM_STATE_H
#include <pthread.h>
#include "ipc_defs.h"

#define MEM_WORDS 64
#define STACK_MAX 16
#define QUEUE_MAX 16

typedef struct {
    pthread_mutex_t lock;          /* process-shared, initialised by Core */
    long cmds_done, errors;
    struct { int pc; long acc; } cpu;
    long mem[MEM_WORDS];
    long stack[STACK_MAX]; int sp;
    long queue[QUEUE_MAX]; int qhead, qcount;
} sim_state_t;

_Static_assert(sizeof(sim_state_t) <= SHM_SIZE, "sim_state_t too big for SHM_SIZE");
#endif
