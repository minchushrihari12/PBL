#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

#define MAX_REFERENCES 10000
#define INPUT_SIZE 65536

int ui_read_input(int **pages, int *count, int *frames, int *choice) {
    char line[INPUT_SIZE];
    int *values = malloc(MAX_REFERENCES * sizeof(int));
    if (!values) return 0;

    printf("=== Page Replacement Algorithm Simulator ===\n");
    printf("Enter page-reference string (space-separated non-negative integers):\n> ");
    if (!fgets(line, sizeof(line), stdin)) { free(values); return 0; }

    int n = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '-' || *p == '+') {
            fprintf(stderr, "Invalid page reference: use non-negative integers only.\n");
            free(values); return 0;
        }
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || v < 0 || v > 2147483647L || (*end && !isspace((unsigned char)*end))) {
            fprintf(stderr, "Invalid page-reference string. Separate integers with spaces.\n");
            free(values); return 0;
        }
        if (n >= MAX_REFERENCES) {
            fprintf(stderr, "Too many references (maximum %d).\n", MAX_REFERENCES);
            free(values); return 0;
        }
        values[n++] = (int)v;
        p = end;
    }
    if (n == 0) {
        fprintf(stderr, "The page-reference string cannot be empty.\n");
        free(values); return 0;
    }

    printf("Enter number of page frames (positive integer):\n> ");
    if (!fgets(line, sizeof(line), stdin)) { free(values); return 0; }
    char *end;
    long f = strtol(line, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (end == line || *end || f <= 0 || f > 1000) {
        fprintf(stderr, "Frame count must be an integer from 1 to 1000.\n");
        free(values); return 0;
    }

    printf("Select algorithm:\n1. FIFO\n2. LRU\n3. Optimal\n4. Compare all\nChoice: ");
    if (!fgets(line, sizeof(line), stdin)) { free(values); return 0; }
    long c = strtol(line, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (end == line || *end || c < 1 || c > 4) {
        fprintf(stderr, "Choose 1, 2, 3, or 4.\n");
        free(values); return 0;
    }

    int *smaller = realloc(values, (size_t)n * sizeof(int));
    if (!smaller) { free(values); return 0; }
    *pages = smaller;
    *count = n;
    *frames = (int)f;
    *choice = (int)c;
    return 1;
}

void ui_print_result(const SimulationResult *r, const int *pages,
                     int count, int frame_count) {
    printf("\n--- %s Simulation ---\n", algorithm_name(r->algorithm));
    printf("%-8s", "Page");
    for (int j = 0; j < frame_count; j++) printf("F%-7d", j + 1);
    printf("%-8s\n", "Status");

    for (int i = 0; i < count; i++) {
        printf("%-8d", pages[i]);
        for (int j = 0; j < frame_count; j++) {
            int value = r->frame_states[i * frame_count + j];
            if (value == -1) printf("%-8s", "-");
            else printf("%-8d", value);
        }
        printf("%-8s\n", r->is_hit[i] ? "Hit" : "Fault");
    }

    printf("\nReferences: %d\nHits: %d\nFaults: %d\n",
           count, r->hits, r->faults);
    printf("Hit ratio: %.2f%%\n", 100.0 * r->hits / count);
    printf("Fault ratio: %.2f%%\n", 100.0 * r->faults / count);
}

void ui_print_comparison(const SimulationResult results[3],
                         const int selected[3], int reference_count) {
    printf("\n=== Algorithm Comparison ===\n");
    printf("%-12s %-10s %-10s %-12s %-12s\n",
           "Algorithm", "Hits", "Faults", "Hit ratio", "Fault ratio");
    for (int i = 0; i < 3; i++) {
        if (!selected[i]) continue;
        printf("%-12s %-10d %-10d %-11.2f%% %-11.2f%%\n",
               algorithm_name((Algorithm)i), results[i].hits, results[i].faults,
               100.0 * results[i].hits / reference_count,
               100.0 * results[i].faults / reference_count);
    }
    int best = -1;
    for (int i = 0; i < 3; i++)
        if (selected[i] && (best < 0 || results[i].faults < results[best].faults))
            best = i;
    if (best >= 0)
        printf("Fewest faults for this input: %s (%d faults)\n",
               algorithm_name((Algorithm)best), results[best].faults);
}
