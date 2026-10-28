#!/bin/sh
# Compile the C core to WebAssembly. Needs Emscripten (emcc) installed and activated.
emcc main_wasm.c fifo.c lru.c optimal.c -O2 -o sim.js \
  -sEXPORTED_FUNCTIONS=_run_simulation \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -sMODULARIZE=1 -sEXPORT_NAME=createSim
echo "Built sim.js + sim.wasm. Now run: python3 -m http.server 8000"
