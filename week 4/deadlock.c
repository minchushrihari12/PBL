/*
 * deadlock.c  --  Banker's algorithm, deadlock detection, recovery helpers.
 */
#include "deadlock.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ds_init(sys_state_t *s, int np, int nr)
{
    memset(s, 0, sizeof *s);
    s->np = np;
    s->nr = nr;
    for (int i = 0; i < np; i++) s->active[i] = 1;
}

void ds_available(const sys_state_t *s, int av[])
{
    for (int j = 0; j < s->nr; j++) {
        av[j] = s->total[j];
        for (int i = 0; i < s->np; i++) av[j] -= s->alloc[i][j];
    }
}

int ds_need(const sys_state_t *s, int p, int r)
{
    if (!s->active[p]) return 0;
    int n = s->max[p][r] - s->alloc[p][r];
    return n < 0 ? 0 : n;
}

static int vec_le(const int *a, const int *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] > b[i]) return 0;
    return 1;
}

static int has_req(const sys_state_t *s, int p)
{
    for (int j = 0; j < s->nr; j++) if (s->req[p][j] > 0) return 1;
    return 0;
}

void ds_vec_str(char *d, size_t n, const int *v, int c)
{
    size_t o = 0;
    if (!n) return;
    o += snprintf(d + o, n - o, "(");
    for (int i = 0; i < c && o < n; i++)
        o += snprintf(d + o, n - o, i ? ",%d" : "%d", v[i]);
    if (o < n) snprintf(d + o, n - o, ")");
}

static void tr(char *t, size_t n, const char *fmt, ...)
{
    if (!t || !n) return;
    size_t o = strlen(t);
    if (o + 1 >= n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t + o, n - o, fmt, ap);
    va_end(ap);
}

int ds_validate(const sys_state_t *s, char *err, size_t n)
{
    int av[MAXR];
    if (s->np <= 0 || s->nr <= 0) {
        snprintf(err, n, "Create processes and resources first");
        return 0;
    }
    for (int i = 0; i < s->np; i++) {
        if (!s->active[i]) continue;
        for (int j = 0; j < s->nr; j++)
            if (s->alloc[i][j] > s->max[i][j]) {
                snprintf(err, n, "P%d holds more of R%d than its Maximum", i, j);
                return 0;
            }
    }
    ds_available(s, av);
    for (int j = 0; j < s->nr; j++)
        if (av[j] < 0) {
            snprintf(err, n, "Allocated R%d (%d) exceeds Total (%d)",
                     j, s->total[j] - av[j], s->total[j]);
            return 0;
        }
    err[0] = 0;
    return 1;
}

int ds_safety(const sys_state_t *s, int seq[], int *len, char *trace, size_t tsz)
{
    int work[MAXR], fin[MAXP], cnt = 0, nact = 0, progress = 1;

    ds_available(s, work);
    for (int i = 0; i < s->np; i++) {
        fin[i] = !s->active[i];
        nact += s->active[i];
    }
    if (trace && tsz) trace[0] = 0;

    while (progress) {
        progress = 0;
        for (int i = 0; i < s->np; i++) {
            int need[MAXR];
            if (fin[i]) continue;
            for (int j = 0; j < s->nr; j++) need[j] = ds_need(s, i, j);
            if (!vec_le(need, work, s->nr)) continue;

            char a[64], b[64], c[64];
            ds_vec_str(a, sizeof a, need, s->nr);
            ds_vec_str(b, sizeof b, work, s->nr);
            for (int j = 0; j < s->nr; j++) work[j] += s->alloc[i][j];
            ds_vec_str(c, sizeof c, work, s->nr);
            tr(trace, tsz, "P%d: Need %s <= Work %s, so P%d can finish. Work = %s\n",
               i, a, b, i, c);
            fin[i] = 1;
            seq[cnt++] = i;
            progress = 1;
        }
    }
    *len = cnt;
    return cnt == nact;
}

int ds_detect(const sys_state_t *s, int dead[])
{
    int work[MAXR], fin[MAXP], progress = 1, n = 0;

    ds_available(s, work);
    for (int i = 0; i < s->np; i++) {
        int holds = 0;
        for (int j = 0; j < s->nr; j++) holds |= s->alloc[i][j] != 0;
        fin[i] = !s->active[i] || !holds;   /* holds nothing -> cannot be in a cycle */
        dead[i] = 0;
    }
    while (progress) {
        progress = 0;
        for (int i = 0; i < s->np; i++) {
            if (fin[i] || !vec_le(s->req[i], work, s->nr)) continue;
            for (int j = 0; j < s->nr; j++) work[j] += s->alloc[i][j];
            fin[i] = 1;
            progress = 1;
        }
    }
    for (int i = 0; i < s->np; i++)
        if (!fin[i]) { dead[i] = 1; n++; }
    return n;
}

sys_status_t ds_status(const sys_state_t *s)
{
    int dead[MAXP], seq[MAXP], len;
    if (ds_detect(s, dead) > 0) return ST_DEADLOCK;
    return ds_safety(s, seq, &len, NULL, 0) ? ST_SAFE : ST_UNSAFE;
}

/* returns REQ_GRANTED when the request is well-formed (caller continues) */
static req_result_t precheck(const sys_state_t *s, int p, const int r[])
{
    int sum = 0;
    if (p < 0 || p >= s->np || !s->active[p] || has_req(s, p)) return REQ_BAD;
    for (int j = 0; j < s->nr; j++) {
        if (r[j] < 0) return REQ_BAD;
        sum += r[j];
        if (r[j] > ds_need(s, p, j)) return REQ_EXCEEDS_NEED;
    }
    return sum > 0 ? REQ_GRANTED : REQ_BAD;
}

req_result_t ds_request_banker(sys_state_t *s, int p, const int r[])
{
    int av[MAXR], seq[MAXP], len;
    req_result_t pc = precheck(s, p, r);
    if (pc != REQ_GRANTED) return pc;

    ds_available(s, av);
    if (!vec_le(r, av, s->nr)) return REQ_NOT_AVAILABLE;

    for (int j = 0; j < s->nr; j++) s->alloc[p][j] += r[j];      /* pretend */
    if (!ds_safety(s, seq, &len, NULL, 0)) {
        for (int j = 0; j < s->nr; j++) s->alloc[p][j] -= r[j];  /* roll back */
        return REQ_UNSAFE;
    }
    return REQ_GRANTED;
}

req_result_t ds_request_raw(sys_state_t *s, int p, const int r[])
{
    int av[MAXR];
    req_result_t pc = precheck(s, p, r);
    if (pc != REQ_GRANTED) return pc;

    ds_available(s, av);
    if (vec_le(r, av, s->nr)) {
        for (int j = 0; j < s->nr; j++) s->alloc[p][j] += r[j];
        return REQ_GRANTED;
    }
    for (int j = 0; j < s->nr; j++) s->req[p][j] = r[j];          /* block */
    return REQ_BLOCKED;
}

int ds_release(sys_state_t *s, int p, const int r[])
{
    if (p < 0 || p >= s->np || !s->active[p]) return 0;
    for (int j = 0; j < s->nr; j++)
        if (r[j] < 0 || r[j] > s->alloc[p][j]) return 0;
    for (int j = 0; j < s->nr; j++) s->alloc[p][j] -= r[j];
    return 1;
}

void ds_terminate(sys_state_t *s, int p)
{
    for (int j = 0; j < s->nr; j++) s->alloc[p][j] = s->req[p][j] = 0;
    s->active[p] = 0;
}

void ds_preempt(sys_state_t *s, int p)
{
    for (int j = 0; j < s->nr; j++) s->alloc[p][j] = s->req[p][j] = 0;
}

int ds_wake(sys_state_t *s, int granted[])
{
    int n = 0, changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < s->np; i++) {
            int av[MAXR];
            if (!s->active[i] || !has_req(s, i)) continue;
            ds_available(s, av);
            if (!vec_le(s->req[i], av, s->nr)) continue;
            for (int j = 0; j < s->nr; j++) {
                s->alloc[i][j] += s->req[i][j];
                s->req[i][j] = 0;
            }
            granted[n++] = i;
            changed = 1;
        }
    }
    return n;
}

int ds_choose_victim(const sys_state_t *s, const int dead[])
{
    int best = -1, best_cost = 0;
    for (int i = 0; i < s->np; i++) {
        int cost = 0;
        if (!dead[i]) continue;
        for (int j = 0; j < s->nr; j++) cost += s->alloc[i][j];
        if (best < 0 || cost < best_cost) { best = i; best_cost = cost; }
    }
    return best;
}

static void fill(sys_state_t *s, int np, int nr, const int *total,
                 const int *alloc, const int *max, const int *req)
{
    ds_init(s, np, nr);
    for (int j = 0; j < nr; j++) s->total[j] = total[j];
    for (int i = 0; i < np; i++)
        for (int j = 0; j < nr; j++) {
            s->alloc[i][j] = alloc[i * nr + j];
            s->max[i][j]   = max[i * nr + j];
            s->req[i][j]   = req ? req[i * nr + j] : 0;
        }
}

int ds_load_scenario(sys_state_t *s, int n)
{
    if (n == 1) {   /* textbook example: SAFE, sequence P1 P3 P4 P0 P2 */
        static const int t[] = {10, 5, 7};
        static const int a[] = {0,1,0, 2,0,0, 3,0,2, 2,1,1, 0,0,2};
        static const int m[] = {7,5,3, 3,2,2, 9,0,2, 2,2,2, 4,3,3};
        fill(s, 5, 3, t, a, m, NULL);
    } else if (n == 2) {   /* one resource type, UNSAFE but not deadlocked */
        static const int t[] = {12};
        static const int a[] = {5, 2, 3};
        static const int m[] = {10, 4, 9};
        fill(s, 3, 1, t, a, m, NULL);
    } else if (n == 3) {   /* circular wait: P0->P1->P2->P0, DEADLOCK */
        static const int t[] = {1, 1, 1};
        static const int a[] = {1,0,0, 0,1,0, 0,0,1};
        static const int m[] = {1,1,0, 0,1,1, 1,0,1};
        static const int q[] = {0,1,0, 0,0,1, 1,0,0};
        fill(s, 3, 3, t, a, m, q);
    } else {
        return 0;
    }
    return 1;
}
