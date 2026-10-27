
#include <assert.h>
#include <stdio.h>
#include "core.h"

static void test_algorithm(Algorithm algorithm,
                          int expected_faults,
                          int expected_hits) {
    int pages[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3};
    int page_count = sizeof(pages) / sizeof(pages[0]);
    int frame_count = 3;

    SimulationResult result = {0};

    run_simulation(algorithm, pages, page_count,
                   frame_count, &result);

    assert(result.faults == expected_faults);
    assert(result.hits == expected_hits);
    assert(result.faults + result.hits == page_count);

    printf("PASS: %s (%d faults, %d hits)\n",
           algorithm_name(algorithm),
           result.faults, result.hits);

    free_result(&result);
}

int main(void) {
    test_algorithm(FIFO, 9, 1);
    test_algorithm(LRU, 8, 2);
    test_algorithm(OPTIMAL, 6, 4);

    puts("All core tests passed.");
    return 0;
}
