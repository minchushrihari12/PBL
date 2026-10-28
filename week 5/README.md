# Page Replacement Simulator

FIFO, LRU and Optimal in C, compiled to WebAssembly, shown on a website.

| File | Owner |
|---|---|
| `algorithms.h` | Shared contract, do not change |
| `fifo.c`, `lru.c` | Member 1 |
| `optimal.c`, `test.c` | Member 2 |
| `index.html`, `style.css`, `app.js` | Member 3 |
| `main_wasm.c`, `build.sh`, `sample.json` | Team leader |

## Test the C core (no browser)
    gcc test.c main_wasm.c fifo.c lru.c optimal.c -o test && ./test

## Build and run the website
    ./build.sh                    # needs Emscripten
    python3 -m http.server 8000   # open http://localhost:8000

Browsers block .wasm on file://, so always use the local server.
Without sim.js the page falls back to sample.json (dev mode).

## Sample check
`7 0 1 2 0 3 0 4 2 3`, 3 frames: FIFO 9 faults, LRU 8, Optimal 6.
