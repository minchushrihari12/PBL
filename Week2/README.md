# Week 4 – Multi-Process Simulator & IPC (C / POSIX)

**Flow:** UI → Core → CPU / Memory / Stack / Queue → Logger, supervised by the Integrator.

## Architecture

```
             ┌────────── Integrator (UNIX domain sockets) ──────────┐
             │ creates IPC objects, launches procs, READY/START/    │
             │ DONE/SHUTDOWN control, collects metrics, cleanup     │
             └───────▲──────────────────▲─────────────────▲─────────┘
                     │ socket           │ socket          │ socket
  ┌──────┐  mq: CMD   ┌────────────────┐  FIFO: log lines  ┌────────┐
  │  UI  │──────────► │  Core (shm)    │ ────────────────► │ Logger │
  │      │◄────────── │ CPU/Mem/Stk/Q  │                   │        │
  └──────┘  mq: REPLY └────────────────┘                   └────────┘
```

| Link | IPC | Owner | Why |
|------|-----|-------|-----|
| UI ↔ Core | POSIX message queues | UI | discrete commands/replies, built-in blocking + message boundaries, no manual locking |
| Core state | POSIX shared memory (+ mutex/semaphore) | Core | fastest option for large shared state; UI can read a snapshot without copying through the kernel |
| Core → Logger | Named pipe (FIFO) | Logger | one-way ordered byte stream, perfect for append-only logs; Logger is decoupled |
| Integrator ↔ all | UNIX domain sockets | Integrator | bidirectional, connection-oriented; detects crashes (EOF); good for control + metrics |

## Files

| File | Owner | Purpose |
|------|-------|---------|
| `ipc_defs.h` | Integrator | shared names, sizes, message structs |
| `ctrl_util.h` | Integrator | 5 helper functions for talking to the Integrator |
| `integrator.c` | Integrator | launcher + socket server + metrics |
| `standalone.c` | Integrator | single-process baseline for benchmark |
| `starters/ui.c` | Student 1 | UI process, mq + 2 threads (starter template) |
| `starters/core.c`, `sim_state.h` | Student 2 | Core process, shared memory (starter template) |
| `starters/logger.c` | Student 3 | Logger process, FIFO (starter template) |
| `stubs/*` | Integrator | minimal stubs for testing the integrator alone |
| `TEAM_GUIDE.md` | all | roles, contract, checklist |
| `tests/test_ipc.sh` | Integrator | 8 IPC test cases |
| `tests/bench.sh` | Integrator | standalone vs multi-process benchmark |

## Contract for each process

1. `s = ctrl_connect(PROC_x)` → `ctrl_wait(s, MSG_START)`
2. Only **open** the mq / shm / FIFO (the Integrator creates them).
3. Open the FIFO **after** START (otherwise open() blocks and deadlocks startup).
4. Core: write the log line **before** sending the reply.
5. UI: `ctrl_done(s, PROC_UI)` when the demo finishes.
6. All: on `MSG_SHUTDOWN` → `ctrl_metrics(...)` → exit. Use `ctrl_shutdown_pending(s)` in loops.
7. Link with `-lrt -pthread`.

## Build & run

```bash
make starters   # integrator + starter UI/Core/Logger
make demo       # interactive run
make test-starters
make stubs      # minimal stubs instead
make test       # IPC test cases
make bench      # benchmark (10 runs) -> benchmark_results.txt

make real       # when ui.c / core.c / logger.c are added
make clean
```

## Benchmark & analysis
Paste `benchmark_results.txt` here and explain: total time, IPC round-trip latency, CPU per process, memory per process, and the overhead of separation vs the standalone build.
