#ifndef SIMULATION_H
#define SIMULATION_H
/* algo: 1 FIFO, 2 LRU, 3 Optimal, 4 compare all. Returns a JSON string (static buffer). */
const char *run_simulation(const char *refs_str, int frames, int algo);
#endif
