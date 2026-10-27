#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"

const char *algorithm_name(Algorithm algorithm) {
    switch (algorithm) {
        case FIFO: return "FIFO";
        case LRU: return "LRU";
        case OPTIMAL: return "Optimal";
        default: return "Unknown";
    }
}

static int find_page(const int *frames, int count, int page) {
    for (int i = 0; i < count; i++)
        if (frames[i] == page) return i;
    return -1;
}

void run_simulation(Algorithm algorithm, const int *pages, int page_count,
                    int frame_count, SimulationResult *result) {
    int *frames = malloc((size_t)frame_count * sizeof(int));
    int *last_used = calloc((size_t)frame_count, sizeof(int));
    if (!frames || !last_used) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(frames); free(last_used);
        exit(EXIT_FAILURE);
    }

    result->algorithm = algorithm;
    result->frame_count = frame_count;
    result->reference_count = page_count;
    result->frame_states = malloc((size_t)page_count * frame_count * sizeof(int));
    result->is_hit = calloc((size_t)page_count, sizeof(int));
    if (!result->frame_states || !result->is_hit) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(frames); free(last_used);
        free_result(result);
        exit(EXIT_FAILURE);
    }

    for (int j = 0; j < frame_count; j++) frames[j] = -1;
    result->hits = result->faults = 0;
    int fifo_next = 0;

    for (int i = 0; i < page_count; i++) {
        int slot = find_page(frames, frame_count, pages[i]);
        if (slot >= 0) {
            result->is_hit[i] = 1;
            result->hits++;
            last_used[slot] = i;
        } else {
            result->faults++;
            int empty = find_page(frames, frame_count, -1);
            if (empty >= 0) slot = empty;
            else if (algorithm == FIFO) {
                slot = fifo_next;
            } else if (algorithm == LRU) {
                slot = 0;
                for (int j = 1; j < frame_count; j++)
                    if (last_used[j] < last_used[slot]) slot = j;
            } else {
                int farthest = -1;
                slot = 0;
                for (int j = 0; j < frame_count; j++) {
                    int next_use = page_count + 1;
                    for (int k = i + 1; k < page_count; k++) {
                        if (pages[k] == frames[j]) { next_use = k; break; }
                    }
                    if (next_use > farthest) {
                        farthest = next_use;
                        slot = j;
                    }
                }
            }
            frames[slot] = pages[i];
            last_used[slot] = i;
            if (algorithm == FIFO) fifo_next = (slot + 1) % frame_count;
        }

        for (int j = 0; j < frame_count; j++)
            result->frame_states[i * frame_count + j] = frames[j];
    }

    free(frames);
    free(last_used);
}

void free_result(SimulationResult *result) {
    if (!result) return;
    free(result->frame_states);
    free(result->is_hit);
    result->frame_states = NULL;
    result->is_hit = NULL;
}
