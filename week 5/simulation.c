/* simulation.c - Team leader (integration)
 * Parses and validates input, runs FIFO / LRU / Optimal. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "simulation.h"

#define BUF_SIZE 131072
#define MAX_PAGE 9999

static char out[BUF_SIZE];
static int  pos;

static void put(const char *fmt, ...) {
    if (pos >= BUF_SIZE - 1) return;
    va_list ap; va_start(ap, fmt);
    pos += vsnprintf(out + pos, BUF_SIZE - pos, fmt, ap);
    va_end(ap);
    if (pos > BUF_SIZE - 1) pos = BUF_SIZE - 1;
}

/* returns count, or -1 (empty), -2 (bad char), -3 (too many), -4 (too big) */
static int parse_refs(const char *s, int *refs) {
    int n = 0;
    while (*s) {
        if (isspace((unsigned char)*s) || *s == ',') { s++; continue; }
        if (!isdigit((unsigned char)*s)) return -2;     /* letters, '-', symbols */
        long v = 0;
        while (isdigit((unsigned char)*s)) {
            v = v * 10 + (*s - '0');
            if (v > MAX_PAGE) return -4;
            s++;
        }
        if (n >= MAX_REFS) return -3;
        refs[n++] = (int)v;
    }
    return n == 0 ? -1 : n;
}

const char *run_all(const char *refs_str, int frames, int algo,
                    int *refs, int *n, Result *results, const char **names, int *count) {
    int c = parse_refs(refs_str, refs);
    if (c == -1) return "Enter at least one page number.";
    if (c == -2) return "Pages must be non-negative whole numbers separated by spaces or commas.";
    if (c == -3) return "Too many pages. Maximum is 100.";
    if (c == -4) return "Page numbers can be at most 9999.";
    if (frames < 1) return "Number of frames must be at least 1.";
    if (frames > MAX_FRAMES) return "Number of frames can be at most 10.";
    if (algo < 1 || algo > 4) return "Pick an algorithm.";

    *n = c;
    *count = 0;
    for (int a = 1; a <= 3; a++) {
        if (algo != 4 && algo != a) continue;
        Result *r = &results[*count];
        if (a == 1)      { fifo(refs, c, frames, r);    names[*count] = "FIFO"; }
        else if (a == 2) { lru(refs, c, frames, r);     names[*count] = "LRU"; }
        else             { optimal(refs, c, frames, r); names[*count] = "Optimal"; }
        (*count)++;
    }
    return NULL;
}

static void write_result(const char *name, const Result *r, const int *refs) {
    put("{\"name\":\"%s\",\"hits\":%d,\"faults\":%d,\"steps\":[", name, r->hits, r->faults);
    for (int i = 0; i < r->n; i++) {
        put("%s{\"page\":%d,\"frames\":[", i ? "," : "", refs[i]);
        for (int j = 0; j < r->frames; j++)
            put("%s%d", j ? "," : "", r->snapshot[i][j]);
        put("],\"fault\":%s}", r->fault[i] ? "true" : "false");
    }
    put("]}");
}

const char *run_simulation(const char *refs_str, int frames, int algo) {
    static Result results[3];
    const char *names[3];
    int refs[MAX_REFS], n, count;

    const char *err = run_all(refs_str, frames, algo, refs, &n, results, names, &count);
    pos = 0;
    if (err) { put("{\"ok\":false,\"error\":\"%s\"}", err); return out; }

    put("{\"ok\":true,\"frames\":%d,\"total\":%d,\"results\":[", frames, n);
    for (int i = 0; i < count; i++) {
        if (i) put(",");
        write_result(names[i], &results[i], refs);
    }
    put("]}");
    return out;
}
