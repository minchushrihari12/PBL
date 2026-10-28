/* main_wasm.c - Team leader (integration)
 * run_simulation(refs_string, frames, algo) -> JSON string
 * algo: 1 = FIFO, 2 = LRU, 3 = Optimal, 4 = compare all
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "algorithms.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

#define BUF_SIZE 131072
#define MAX_PAGE 9999

static char out[BUF_SIZE];
static int  pos;
static Result res;

static void put(const char *fmt, ...) {
    if (pos >= BUF_SIZE - 1) return;
    va_list ap; va_start(ap, fmt);
    pos += vsnprintf(out + pos, BUF_SIZE - pos, fmt, ap);
    va_end(ap);
    if (pos > BUF_SIZE - 1) pos = BUF_SIZE - 1;
}

static const char *error(const char *msg) {
    pos = 0;
    put("{\"ok\":false,\"error\":\"%s\"}", msg);
    return out;
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

EXPORT
const char *run_simulation(const char *refs_str, int frames, int algo) {
    int refs[MAX_REFS];
    int n = parse_refs(refs_str, refs);

    if (n == -1) return error("Enter at least one page number.");
    if (n == -2) return error("Pages must be non-negative whole numbers separated by spaces or commas.");
    if (n == -3) return error("Too many pages. Maximum is 100.");
    if (n == -4) return error("Page numbers can be at most 9999.");
    if (frames < 1) return error("Number of frames must be at least 1.");
    if (frames > MAX_FRAMES) return error("Number of frames can be at most 10.");
    if (algo < 1 || algo > 4) return error("Pick an algorithm.");

    pos = 0;
    put("{\"ok\":true,\"frames\":%d,\"total\":%d,\"results\":[", frames, n);
    int first = 1;
    for (int a = 1; a <= 3; a++) {
        if (algo != 4 && algo != a) continue;
        if (!first) put(",");
        first = 0;
        if (a == 1)      { fifo(refs, n, frames, &res);    write_result("FIFO", &res, refs); }
        else if (a == 2) { lru(refs, n, frames, &res);     write_result("LRU", &res, refs); }
        else             { optimal(refs, n, frames, &res); write_result("Optimal", &res, refs); }
    }
    put("]}");
    return out;
}
