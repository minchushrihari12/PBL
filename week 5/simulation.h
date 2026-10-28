#ifndef SIMULATION_H
#define SIMULATION_H
#include "algorithms.h"

/* Validates the input and runs the chosen algorithm(s).
 * algo: 1 FIFO, 2 LRU, 3 Optimal, 4 compare all.
 * Returns NULL on success (fills refs, *n, results[], names[], *count),
 * or an error message string. results/names must have room for 3 entries. */
const char *run_all(const char *refs_str, int frames, int algo,
                    int *refs, int *n, Result *results, const char **names, int *count);

/* Same thing, but returns a JSON string (static buffer). Used by test.c. */
const char *run_simulation(const char *refs_str, int frames, int algo);
#endif
