#ifndef UI_H
#define UI_H
#include "core.h"

int ui_read_input(int **pages, int *count, int *frames, int *choice);
void ui_print_result(const SimulationResult *result, const int *pages,
                     int count, int frame_count);
void ui_print_comparison(const SimulationResult results[3],
                         const int selected[3], int reference_count);
#endif
