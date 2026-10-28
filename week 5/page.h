#ifndef PAGE_H
#define PAGE_H
#include "algorithms.h"

/* Builds the whole web page as HTML (no JavaScript needed). Returns a static buffer.
 * - Blank form:   page_render("7 0 1 2 0 3 0 4 2 3", 3, 4, NULL, NULL, 0, NULL, NULL, 0)
 * - Error:        pass the error message, count = 0
 * - With results: pass refs/n and results/names/count from run_all() */
const char *page_render(const char *refs_str, int frames, int algo, const char *error,
                        const int *refs, int n, const Result *results, const char **names, int count);
#endif
