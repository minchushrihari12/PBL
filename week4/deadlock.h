/*
 * deadlock.h  --  pure algorithm layer (no IPC, no I/O).  (Owner: Core)
 */
#ifndef DEADLOCK_H
#define DEADLOCK_H

#include <stddef.h>

#define MAXP 10
#define MAXR 10

typedef struct {
    int np, nr;
    int total[MAXR];          /* total instances of each resource      */
    int alloc[MAXP][MAXR];    /* Allocation matrix                     */
    int max[MAXP][MAXR];      /* Maximum requirement matrix            */
    int req[MAXP][MAXR];      /* pending (blocked) requests            */
    int active[MAXP];         /* 0 = terminated                        */
} sys_state_t;

typedef enum { ST_SAFE, ST_UNSAFE, ST_DEADLOCK } sys_status_t;

typedef enum {
    REQ_GRANTED = 0,
    REQ_BAD,                  /* bad pid / empty / already blocked     */
    REQ_EXCEEDS_NEED,         /* request > Need (exceeds max claim)    */
    REQ_NOT_AVAILABLE,        /* request > Available                   */
    REQ_UNSAFE,               /* Banker's: would leave an unsafe state */
    REQ_BLOCKED               /* raw mode: process now waits           */
} req_result_t;

void ds_init(sys_state_t *s, int np, int nr);
void ds_available(const sys_state_t *s, int av[]);
int  ds_need(const sys_state_t *s, int p, int r);
int  ds_validate(const sys_state_t *s, char *err, size_t n);   /* 1 = ok */
void ds_vec_str(char *dst, size_t n, const int *v, int count); /* "(3,3,2)" */

/* Banker's safety algorithm. Returns 1 if safe. trace (optional) gets
 * one line per step, '\n' separated. */
int  ds_safety(const sys_state_t *s, int seq[], int *len, char *trace, size_t tsz);

/* Deadlock detection (Allocation + pending Request). dead[i]=1 for
 * deadlocked processes. Returns how many. */
int  ds_detect(const sys_state_t *s, int dead[]);

req_result_t ds_request_banker(sys_state_t *s, int p, const int r[]);
req_result_t ds_request_raw(sys_state_t *s, int p, const int r[]);

int  ds_release(sys_state_t *s, int p, const int r[]);        /* 1 = ok */
void ds_terminate(sys_state_t *s, int p);                     /* free all */
void ds_preempt(sys_state_t *s, int p);                       /* free all, rollback request */
int  ds_wake(sys_state_t *s, int granted[]);                  /* grant pending requests */
int  ds_choose_victim(const sys_state_t *s, const int dead[]);/* min-cost victim */
sys_status_t ds_status(const sys_state_t *s);
int  ds_load_scenario(sys_state_t *s, int n);                 /* 1 safe, 2 unsafe, 3 deadlock */

#endif
