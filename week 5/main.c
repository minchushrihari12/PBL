#include "ui.h"
#include "core.h"
#include "logger.h"
#include <stdlib.h>

int main(void) {
    int *pages = NULL, count = 0, frames = 0, choice = 0;
    SimulationResult results[3];
    int selected[3] = {0, 0, 0};

    logger_init("simulation.log");
    logger_write("Application started.");

    if (!ui_read_input(&pages, &count, &frames, &choice)) {
        logger_write("Input validation failed.");
        logger_close();
        return 1;
    }

    if (choice == 4) selected[0] = selected[1] = selected[2] = 1;
    else selected[choice - 1] = 1;

    for (int i = 0; i < 3; i++) {
        if (selected[i]) {
            run_simulation((Algorithm)i, pages, count, frames, &results[i]);
            ui_print_result(&results[i], pages, count, frames);
            logger_write("Completed simulation for an algorithm.");
        }
    }

    if (choice == 4) ui_print_comparison(results, selected, count);
    for (int i = 0; i < 3; i++) if (selected[i]) free_result(&results[i]);
    free(pages);
    logger_write("Application finished.");
    logger_close();
    return 0;
}
