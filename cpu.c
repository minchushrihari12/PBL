#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

#define MEM_SIZE    256
#define STACK_SIZE  64
#define QUEUE_SIZE  64

/* Instruction word: opcode (8 bits) | register (8 bits) | operand (16 bits) */
enum { OP_HALT, OP_LOADI, OP_ADD, OP_SUB, OP_PUSH, OP_POP, OP_ENQ, OP_DEQ,
       OP_STORE, OP_LOAD, OP_JMP, OP_JNZ, OP_NOP, OP_COUNT };

static const char *OPNAME[OP_COUNT] = { "HALT", "LOADI", "ADD", "SUB", "PUSH", "POP",
    "ENQ", "DEQ", "STORE", "LOAD", "JMP", "JNZ", "NOP" };
static const int OPARGS[OP_COUNT] = { 0, 2, 2, 2, 1, 1, 1, 1, 2, 2, 1, 2, 0 };

static uint32_t prog[MEM_SIZE], data[MEM_SIZE], stk[STACK_SIZE], que[QUEUE_SIZE];
static uint32_t regs[NUM_REGS], pc, ir, sp, flags;
static int qhead, qcount, loaded, halted, nprog;
static char last_desc[TEXT_LEN];

static void seterr(char *err, size_t len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void seterr(char *err, size_t len, const char *fmt, ...) {
    va_list ap;
    if (!err || !len) return;
    va_start(ap, fmt);
    vsnprintf(err, len, fmt, ap);
    va_end(ap);
}

static int parse_reg(const char *s, int *r) {
    char *end; long v;
    if ((s[0] != 'R' && s[0] != 'r') || !isdigit((unsigned char)s[1])) return -1;
    v = strtol(s + 1, &end, 10);
    if (*end != '\0' || v < 0 || v >= NUM_REGS) return -1;
    *r = (int)v;
    return 0;
}

static int parse_num(const char *s, long lo, long hi, long *v) {
    char *end;
    *v = strtol(s, &end, 0);
    if (end == s || *end != '\0' || *v < lo || *v > hi) return -1;
    return 0;
}

static void strip_comma(char *s) {
    size_t n = strlen(s);
    if (n && s[n - 1] == ',') s[n - 1] = '\0';
}

int cpu_load_file(const char *filename, char *err, size_t errlen) {
    FILE *f;
    char line[128];
    uint32_t tmp[MEM_SIZE];
    int n = 0, lineno = 0;

    if (!filename || !*filename) { seterr(err, errlen, "no file name given"); return -1; }
    f = fopen(filename, "r");
    if (!f) { seterr(err, errlen, "cannot open file '%s'", filename); return -1; }

    while (fgets(line, sizeof line, f)) {
        char m[16] = "", a[32] = "", b[32] = "", *sc;
        int cnt, op, r = 0, r2 = 0;
        long v = 0;
        uint32_t imm = 0;
        size_t i;

        lineno++;
        sc = strchr(line, ';');
        if (sc) *sc = '\0';
        cnt = sscanf(line, "%15s %31s %31s", m, a, b);
        if (cnt <= 0) continue;
        strip_comma(a);
        for (i = 0; m[i]; i++) m[i] = (char)toupper((unsigned char)m[i]);
        for (op = 0; op < OP_COUNT; op++) if (!strcmp(m, OPNAME[op])) break;
        if (op == OP_COUNT) { seterr(err, errlen, "line %d: unknown instruction '%s'", lineno, m); fclose(f); return -1; }
        if (cnt - 1 != OPARGS[op]) { seterr(err, errlen, "line %d: %s needs %d operand(s)", lineno, m, OPARGS[op]); fclose(f); return -1; }
        if (n >= MEM_SIZE) { seterr(err, errlen, "program too long (max %d instructions)", MEM_SIZE); fclose(f); return -1; }

        switch (op) {
        case OP_LOADI:
            if (parse_reg(a, &r) || parse_num(b, 0, 65535, &v)) goto bad;
            imm = (uint32_t)v; break;
        case OP_ADD: case OP_SUB:
            if (parse_reg(a, &r) || parse_reg(b, &r2)) goto bad;
            imm = (uint32_t)r2; break;
        case OP_PUSH: case OP_POP: case OP_ENQ: case OP_DEQ:
            if (parse_reg(a, &r)) goto bad;
            break;
        case OP_STORE: case OP_LOAD: case OP_JNZ:
            if (parse_reg(a, &r) || parse_num(b, 0, MEM_SIZE - 1, &v)) goto bad;
            imm = (uint32_t)v; break;
        case OP_JMP:
            if (parse_num(a, 0, MEM_SIZE - 1, &v)) goto bad;
            imm = (uint32_t)v; break;
        default: break;
        }
        tmp[n++] = ((uint32_t)op << 24) | ((uint32_t)r << 16) | (imm & 0xFFFF);
        continue;
    bad:
        seterr(err, errlen, "line %d: bad operand for %s", lineno, m);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (n == 0) { seterr(err, errlen, "program is empty"); return -1; }
    memcpy(prog, tmp, (size_t)n * sizeof tmp[0]);
    nprog = n;
    loaded = 1;
    cpu_reset();
    return 0;
}

void cpu_reset(void) {
    memset(regs, 0, sizeof regs);
    memset(data, 0, sizeof data);
    memset(stk, 0, sizeof stk);
    memset(que, 0, sizeof que);
    pc = ir = sp = flags = 0;
    qhead = qcount = 0;
    halted = 0;
    last_desc[0] = '\0';
}

const char *cpu_last_desc(void) { return last_desc; }
int cpu_program_size(void) { return nprog; }

void cpu_get_state(uint32_t *p, uint32_t *i, uint32_t r[NUM_REGS], uint32_t *s, uint32_t *f) {
    int k;
    *p = pc; *i = ir; *s = sp; *f = flags;
    for (k = 0; k < NUM_REGS; k++) r[k] = regs[k];
}

static void describe(uint32_t addr, uint32_t w, char *buf, size_t len) {
    unsigned op = w >> 24, r = (w >> 16) & 0xFF, imm = w & 0xFFFF;
    switch (op) {
    case OP_HALT: case OP_NOP: snprintf(buf, len, "PC=%u %s", addr, OPNAME[op]); break;
    case OP_LOADI: case OP_STORE: case OP_LOAD: case OP_JNZ:
        snprintf(buf, len, "PC=%u %s R%u,%u", addr, OPNAME[op], r, imm); break;
    case OP_ADD: case OP_SUB:
        snprintf(buf, len, "PC=%u %s R%u,R%u", addr, OPNAME[op], r, imm); break;
    case OP_JMP: snprintf(buf, len, "PC=%u JMP %u", addr, imm); break;
    default:     snprintf(buf, len, "PC=%u %s R%u", addr, OPNAME[op], r); break;
    }
}

cpu_status_t cpu_step(char *err, size_t errlen) {
    uint32_t addr, w, imm;
    unsigned op, r;

    last_desc[0] = '\0';
    if (!loaded) { seterr(err, errlen, "no program loaded"); return CPU_ERROR; }
    if (halted) return CPU_HALTED;
    if (pc >= (uint32_t)nprog) { seterr(err, errlen, "PC=%u ran past the end of the program (missing HALT?)", pc); return CPU_ERROR; }

    addr = pc;
    ir = prog[pc];
    pc++;
    w = ir;
    op = w >> 24; r = (w >> 16) & 0xFF; imm = w & 0xFFFF;
    describe(addr, w, last_desc, sizeof last_desc);

    if (op >= OP_COUNT) { seterr(err, errlen, "invalid opcode %u at PC=%u", op, addr); return CPU_ERROR; }

    switch (op) {
    case OP_HALT: halted = 1; return CPU_HALTED;
    case OP_NOP:  break;
    case OP_LOADI: regs[r] = imm; flags = (regs[r] == 0); break;
    case OP_ADD:   regs[r] += regs[imm]; flags = (regs[r] == 0); break;
    case OP_SUB:   regs[r] -= regs[imm]; flags = (regs[r] == 0); break;
    case OP_PUSH:
        if (sp >= STACK_SIZE) { seterr(err, errlen, "stack overflow at PC=%u", addr); return CPU_ERROR; }
        stk[sp++] = regs[r]; break;
    case OP_POP:
        if (sp == 0) { seterr(err, errlen, "stack underflow at PC=%u", addr); return CPU_ERROR; }
        regs[r] = stk[--sp]; break;
    case OP_ENQ:
        if (qcount >= QUEUE_SIZE) { seterr(err, errlen, "queue full at PC=%u", addr); return CPU_ERROR; }
        que[(qhead + qcount) % QUEUE_SIZE] = regs[r]; qcount++; break;
    case OP_DEQ:
        if (qcount == 0) { seterr(err, errlen, "queue empty at PC=%u", addr); return CPU_ERROR; }
        regs[r] = que[qhead]; qhead = (qhead + 1) % QUEUE_SIZE; qcount--; break;
    case OP_STORE: data[imm] = regs[r]; break;
    case OP_LOAD:  regs[r] = data[imm]; break;
    case OP_JMP:   pc = imm; break;
    case OP_JNZ:   if (regs[r] != 0) pc = imm; break;
    }
    return CPU_OK;
}
