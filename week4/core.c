/*
 * core.c  --  Core process.  (Owner: Core teammate)
 *
 * Owns the whole simulation state. Receives text commands from the UI on
 * KEY_CORE, runs the algorithms from deadlock.c and answers on KEY_UI with
 * ONE JSON document (state + message + event lines), split in chunks.
 * Every event is also sent to the logger on KEY_LOG.
 *
 * Commands (all numeric arguments):
 *   RESET
 *   SETUP <np> <nr>
 *   TOTAL <t0..>                      ALLOCALL <np*nr values>   MAXALL <np*nr values>
 *   SCENARIO <1|2|3>                  1 safe, 2 unsafe, 3 deadlock
 *   REQUEST <p> <r0..>                Banker's avoidance
 *   REQUESTRAW <p> <r0..>             no avoidance (blocks if unavailable)
 *   RELEASE <p> <r0..>                TERMINATE <p>
 *   BANKER | DETECT | RECOVER <1|2>   1 terminate victims, 2 preempt victims
 *   STATE
 */
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deadlock.h"
#include "ipc_defs.h"

#define MAX_EV  40
#define EV_LEN  200
#define OUT_LEN 24576

static sys_state_t S;
static int q_core = -1, q_ui = -1, q_log = -1;
static volatile sig_atomic_t running = 1;

/* ---------- per-command result ---------- */
static char events[MAX_EV][EV_LEN];
static int  nev;
static int  ok_flag;
static char msg[EV_LEN];

static void ev(int level, const char *fmt, ...)
{
    char buf[EV_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (nev < MAX_EV) strcpy(events[nev++], buf);
    ipc_log(q_log, level, "CORE", "%s", buf);
}

static void say(int ok, const char *fmt, ...)
{
    va_list ap;
    ok_flag = ok;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    ipc_log(q_log, ok ? LOG_INFO : LOG_WARN, "CORE", "%s", msg);
}

/* ---------- tiny JSON string builder ---------- */
typedef struct { char *p; size_t cap, len; } sb_t;

static void sb_add(sb_t *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (b->len + 1 >= b->cap) return;
    va_start(ap, fmt);
    n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        b->len += (size_t)n;
        if (b->len > b->cap - 1) b->len = b->cap - 1;
    }
}

static void sb_str(sb_t *b, const char *s)
{
    sb_add(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') sb_add(b, "\\%c", c);
        else if (c < 0x20)        sb_add(b, " ");
        else                      sb_add(b, "%c", c);
    }
    sb_add(b, "\"");
}

static void json_ints(sb_t *b, const int *v, int n)
{
    sb_add(b, "[");
    for (int i = 0; i < n; i++) sb_add(b, i ? ",%d" : "%d", v[i]);
    sb_add(b, "]");
}

static void json_matrix(sb_t *b, int (*m)[MAXR], int np, int nr)
{
    sb_add(b, "[");
    for (int i = 0; i < np; i++) {
        if (i) sb_add(b, ",");
        json_ints(b, m[i], nr);
    }
    sb_add(b, "]");
}

static void build_reply(sb_t *b)
{
    int av[MAXR] = {0}, need[MAXP][MAXR] = {{0}}, util[MAXR] = {0};
    int dead[MAXP] = {0}, seq[MAXP], slen = 0, ndead = 0, valid = 1;
    char err[128] = "";
    const char *status = "EMPTY";

    if (S.np > 0) {
        ds_available(&S, av);
        for (int i = 0; i < S.np; i++)
            for (int j = 0; j < S.nr; j++) need[i][j] = ds_need(&S, i, j);
        valid = ds_validate(&S, err, sizeof err);
        if (!valid) {
            status = "INVALID";
        } else if ((ndead = ds_detect(&S, dead)) > 0) {
            status = "DEADLOCK";
        } else if (ds_safety(&S, seq, &slen, NULL, 0)) {
            status = "SAFE";
        } else {
            status = "UNSAFE";
            slen = 0;
        }
        for (int j = 0; j < S.nr; j++)
            util[j] = S.total[j] > 0 ? (S.total[j] - av[j]) * 100 / S.total[j] : 0;
    }

    sb_add(b, "{\"ok\":%d,\"msg\":", ok_flag);
    sb_str(b, msg);
    sb_add(b, ",\"np\":%d,\"nr\":%d,\"total\":", S.np, S.nr);   json_ints(b, S.total, S.nr);
    sb_add(b, ",\"avail\":");   json_ints(b, av, S.nr);
    sb_add(b, ",\"alloc\":");   json_matrix(b, S.alloc, S.np, S.nr);
    sb_add(b, ",\"max\":");     json_matrix(b, S.max, S.np, S.nr);
    sb_add(b, ",\"need\":");    json_matrix(b, need, S.np, S.nr);
    sb_add(b, ",\"req\":");     json_matrix(b, S.req, S.np, S.nr);
    sb_add(b, ",\"active\":");  json_ints(b, S.active, S.np);
    sb_add(b, ",\"util\":");    json_ints(b, util, S.nr);
    sb_add(b, ",\"valid\":%d,\"error\":", valid);   sb_str(b, err);
    sb_add(b, ",\"status\":\"%s\",\"seq\":", status); json_ints(b, seq, slen);
    sb_add(b, ",\"dead\":[");
    for (int i = 0, first = 1; i < S.np; i++)
        if (dead[i]) { sb_add(b, first ? "%d" : ",%d", i); first = 0; }
    sb_add(b, "],\"events\":[");
    for (int i = 0; i < nev; i++) {
        if (i) sb_add(b, ",");
        sb_str(b, events[i]);
    }
    sb_add(b, "]}");
}

/* ---------- reply transport (chunked) ---------- */
static void send_reply(int req_id, const char *s)
{
    size_t n = strlen(s), off = 0;
    int seq = 0;
    do {
        reply_msg_t r;
        size_t c = n - off > CHUNK_LEN ? CHUNK_LEN : n - off;
        memset(&r, 0, sizeof r);
        r.mtype = 1;
        r.req_id = req_id;
        r.seq = seq++;
        r.len = (int)c;
        memcpy(r.data, s + off, c);
        off += c;
        r.last = off >= n;
        while (msgsnd(q_ui, &r, MSG_SIZE(reply_msg_t), 0) < 0)
            if (errno != EINTR || !running) return;
    } while (off < n);
}

/* ---------- command helpers ---------- */
static int need_valid(void)
{
    char err[128];
    if (ds_validate(&S, err, sizeof err)) return 1;
    say(0, "%s", err);
    return 0;
}

static void list_names(char *dst, size_t n, const int *flag)
{
    size_t o = 0;
    dst[0] = 0;
    for (int i = 0; i < S.np && o + 8 < n; i++)
        if (flag[i]) o += snprintf(dst + o, n - o, o ? ", P%d" : "P%d", i);
}

static void do_request(int raw, const int *a, int na)
{
    char vs[64];
    int p;
    if (S.np == 0) { say(0, "Create processes and resources first"); return; }
    if (na != S.nr + 1) { say(0, "Usage: %s <pid> and %d values", raw ? "REQUESTRAW" : "REQUEST", S.nr); return; }
    if (!need_valid()) return;
    p = a[0];
    ds_vec_str(vs, sizeof vs, a + 1, S.nr);

    req_result_t r = raw ? ds_request_raw(&S, p, a + 1) : ds_request_banker(&S, p, a + 1);
    switch (r) {
    case REQ_GRANTED:
        say(1, "P%d request %s granted", p, vs);
        ev(LOG_INFO, raw ? "P%d got %s immediately (resources were available)"
                         : "P%d request %s granted: system stays in a SAFE state", p, vs);
        break;
    case REQ_BLOCKED:
        say(1, "P%d request %s cannot be met: P%d is now BLOCKED", p, vs, p);
        ev(LOG_WARN, "P%d is waiting for %s (added to the pending request matrix)", p, vs);
        break;
    case REQ_NOT_AVAILABLE:
        say(0, "P%d request %s denied: resources not available, P%d must wait", p, vs, p);
        break;
    case REQ_UNSAFE:
        say(0, "P%d request %s denied: granting it would lead to an UNSAFE state", p, vs);
        break;
    case REQ_EXCEEDS_NEED:
        say(0, "P%d request %s exceeds its remaining need (maximum claim violated)", p, vs);
        break;
    default:
        say(0, "Invalid request (bad process, empty request, or process already blocked)");
    }
}

static void do_banker(void)
{
    int seq[MAXP], len;
    char trace[2048], line[160];
    if (!need_valid()) return;
    int safe = ds_safety(&S, seq, &len, trace, sizeof trace);
    char *sp;
    for (char *ln = strtok_r(trace, "\n", &sp); ln; ln = strtok_r(NULL, "\n", &sp))
        ev(LOG_INFO, "%s", ln);
    if (safe) {
        size_t o = 0;
        line[0] = 0;
        for (int i = 0; i < len; i++)
            o += snprintf(line + o, sizeof line - o, i ? " -> P%d" : "P%d", seq[i]);
        say(1, "SAFE state. Safe sequence: %s", line);
    } else {
        ev(LOG_WARN, "No remaining process can have its Need met by Work");
        say(1, "UNSAFE state: no safe sequence exists, deadlock is possible");
    }
}

static void do_detect(void)
{
    int dead[MAXP];
    char names[128];
    if (!need_valid()) return;
    int n = ds_detect(&S, dead);
    if (!n) { say(1, "No deadlock detected"); return; }
    list_names(names, sizeof names, dead);
    ev(LOG_ERROR, "Circular wait found among: %s", names);
    say(1, "DEADLOCK detected: %s", names);
}

static void do_recover(int mode)
{
    int dead[MAXP], g[MAXP], guard = 0, steps = 0;
    if (!need_valid()) return;
    int n = ds_detect(&S, dead);
    if (!n) { say(1, "No deadlock to recover from"); return; }

    while (n > 0 && guard++ <= MAXP) {
        int v = ds_choose_victim(&S, dead), units = 0;
        for (int j = 0; j < S.nr; j++) units += S.alloc[v][j];
        if (mode == 1) {
            ds_terminate(&S, v);
            ev(LOG_WARN, "Terminated P%d (lowest cost victim), released %d units", v, units);
        } else {
            ds_preempt(&S, v);
            ev(LOG_WARN, "Preempted P%d (lowest cost victim): %d units taken back, its request rolled back", v, units);
        }
        steps++;
        int ng = ds_wake(&S, g);
        for (int k = 0; k < ng; k++)
            ev(LOG_INFO, "P%d unblocked: its pending request was granted", g[k]);
        n = ds_detect(&S, dead);
    }
    say(1, "Recovery complete after %d step%s: system is deadlock free",
        steps, steps == 1 ? "" : "s");
}

static void process_cmd(const cmd_msg_t *c)
{
    char line[CMD_LEN], *sp, *cmd;
    int a[MAXP * MAXR + 4], na = 0;

    nev = 0; ok_flag = 1; msg[0] = 0;
    strncpy(line, c->text, sizeof line - 1);
    line[sizeof line - 1] = 0;
    if (strcmp(line, "STATE") != 0)
        ipc_log(q_log, LOG_INFO, "CORE", "received: %.150s", line);

    cmd = strtok_r(line, " ", &sp);
    for (char *t; (t = strtok_r(NULL, " ", &sp)) && na < (int)(sizeof a / sizeof a[0]); )
        a[na++] = atoi(t);

    if (!cmd) {
        say(0, "Empty command");
    } else if (!strcmp(cmd, "STATE")) {
        /* just return the state */
    } else if (!strcmp(cmd, "RESET")) {
        ds_init(&S, 0, 0);
        say(1, "System reset");
    } else if (!strcmp(cmd, "SETUP")) {
        if (na != 2 || a[0] < 1 || a[0] > MAXP || a[1] < 1 || a[1] > MAXR)
            say(0, "SETUP needs 1-%d processes and 1-%d resources", MAXP, MAXR);
        else {
            ds_init(&S, a[0], a[1]);
            say(1, "Created %d processes and %d resource types. Enter the matrices below.", a[0], a[1]);
        }
    } else if (!strcmp(cmd, "SCENARIO")) {
        if (na == 1 && ds_load_scenario(&S, a[0])) {
            static const char *nm[] = {"", "SAFE example (textbook Banker's)", "UNSAFE example",
                                       "DEADLOCK example (circular wait)"};
            say(1, "Loaded scenario: %s", nm[a[0]]);
        } else say(0, "Unknown scenario");
    } else if (S.np == 0 && strcmp(cmd, "REQUEST") && strcmp(cmd, "REQUESTRAW")) {
        say(0, "Create processes and resources first");
    } else if (!strcmp(cmd, "TOTAL")) {
        if (na != S.nr) say(0, "TOTAL needs %d values", S.nr);
        else { for (int j = 0; j < S.nr; j++) S.total[j] = a[j] < 0 ? 0 : a[j]; say(1, "Total resource vector updated"); }
    } else if (!strcmp(cmd, "ALLOCALL") || !strcmp(cmd, "MAXALL")) {
        int is_alloc = cmd[0] == 'A';
        if (na != S.np * S.nr) say(0, "%s needs %d values", cmd, S.np * S.nr);
        else {
            for (int i = 0; i < S.np; i++)
                for (int j = 0; j < S.nr; j++) {
                    int v = a[i * S.nr + j];
                    if (v < 0) v = 0;
                    if (is_alloc) { if (S.active[i]) S.alloc[i][j] = v; }
                    else S.max[i][j] = v;
                }
            say(1, is_alloc ? "Allocation matrix updated" : "Maximum matrix updated");
        }
    } else if (!strcmp(cmd, "REQUEST")) {
        do_request(0, a, na);
    } else if (!strcmp(cmd, "REQUESTRAW")) {
        do_request(1, a, na);
    } else if (!strcmp(cmd, "RELEASE")) {
        if (na == S.nr + 1 && ds_release(&S, a[0], a + 1)) {
            int g[MAXP], ng = ds_wake(&S, g);
            for (int k = 0; k < ng; k++) ev(LOG_INFO, "P%d unblocked: its pending request was granted", g[k]);
            say(1, "P%d released resources", a[0]);
        } else say(0, "Invalid release (more than the process holds?)");
    } else if (!strcmp(cmd, "TERMINATE")) {
        if (na == 1 && a[0] >= 0 && a[0] < S.np && S.active[a[0]]) {
            int g[MAXP];
            ds_terminate(&S, a[0]);
            int ng = ds_wake(&S, g);
            for (int k = 0; k < ng; k++) ev(LOG_INFO, "P%d unblocked: its pending request was granted", g[k]);
            say(1, "P%d finished/terminated and released all its resources", a[0]);
        } else say(0, "Invalid process");
    } else if (!strcmp(cmd, "BANKER")) {
        do_banker();
    } else if (!strcmp(cmd, "DETECT")) {
        do_detect();
    } else if (!strcmp(cmd, "RECOVER")) {
        do_recover(na == 1 && a[0] == 2 ? 2 : 1);
    } else {
        say(0, "Unknown command: %s", cmd);
    }

    {
        static char out[OUT_LEN];
        sb_t b = { out, OUT_LEN, 0 };
        out[0] = 0;
        build_reply(&b);
        send_reply(c->req_id, out);
    }
}

/* ---------- lifecycle ---------- */
static void on_signal(int s) { (void)s; running = 0; }

static int fresh_queue(key_t k)
{
    int id = msgget(k, 0666);
    if (id >= 0) msgctl(id, IPC_RMID, NULL);   /* drop stale queue from a previous run */
    return msgget(k, IPC_CREAT | 0666);
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;               /* no SA_RESTART: msgrcv must be interruptible */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    q_core = fresh_queue(KEY_CORE);
    q_ui   = fresh_queue(KEY_UI);
    q_log  = fresh_queue(KEY_LOG);
    if (q_core < 0 || q_ui < 0 || q_log < 0) { perror("[core] msgget"); return 1; }

    ds_init(&S, 0, 0);
    printf("[core] ready (queues: core=%d ui=%d log=%d)\n", q_core, q_ui, q_log);
    fflush(stdout);
    ipc_log(q_log, LOG_INFO, "CORE", "core process started");

    while (running) {
        cmd_msg_t c;
        ssize_t n = msgrcv(q_core, &c, MSG_SIZE(cmd_msg_t), 0, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[core] msgrcv");
            break;
        }
        c.text[CMD_LEN - 1] = 0;
        process_cmd(&c);
    }

    ipc_log(q_log, LOG_INFO, "CORE", "core process stopping");
    msgctl(q_core, IPC_RMID, NULL);
    msgctl(q_ui, IPC_RMID, NULL);
    msgctl(q_log, IPC_RMID, NULL);
    printf("[core] stopped, queues removed\n");
    return 0;
}
