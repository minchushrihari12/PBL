# Page Replacement Simulator (Week 5)

FIFO, LRU and Optimal page replacement in C. The processes talk through POSIX message queues,
and a website shows the results.

## How it works
1. The website calls `/run` on `server.py`.
2. `server.py` runs `mqclient`, which sends a request to the **core** process on the `/pr_req` queue.
3. **core** runs the algorithms and sends the JSON result back on the client's reply queue (in chunks).
4. **core** also sends a log line to the **logger** process, which writes `simulation.log`.

## Files and owners
| File | Owner |
|---|---|
| `algorithms.h` | Shared contract, do not change |
| `fifo.c`, `lru.c` | Member 1 |
| `optimal.c`, `test.c` | Member 2 |
| `web/index.html`, `web/style.css`, `web/app.js` | Member 3 |
| `protocol.h`, `simulation.h`, `simulation.c`, `core.c`, `logger.c`, `mqclient.c`, `server.py`, `Makefile`, `run.sh`, `Dockerfile` | Team leader |

## Run (Linux only; use Docker on Mac)
    docker build -t pr-sim .
    docker run --rm -p 8000:8000 pr-sim
Then open http://localhost:8000. On Linux or Codespaces you can run `./run.sh` instead.

## Test the algorithms
    make test

Sample check: `7 0 1 2 0 3 0 4 2 3`, 3 frames gives FIFO 9 faults, LRU 8, Optimal 6.
