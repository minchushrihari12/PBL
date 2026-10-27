#ifndef CORE_H
#define CORE_H

typedef enum {
    FIFO = 0,
    LRU = 1,
    OPTIMAL = 2
} Algorithm;

typedef struct {
    Algorithm algorithm;
    int frame_count;
    int reference_count;
    int *frame_states; /* row-major: reference_count x frame_count */
    int *is_hit;       /* 1 = hit, 0 = fault */
    int hits;
    int faults;
} SimulationResult;

void run_simulation(Algorithm algorithm, const int *pages, int page_count,
                    int frame_count, SimulationResult *result);
void free_result(SimulationResult *result);
const char *algorithm_name(Algorithm algorithm);

#endif
