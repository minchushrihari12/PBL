# Week 4: Multi-Process Simulator & IPC (C / POSIX)

A simulator split into **three independent processes** (UI, Core, Logger) plus an **Integrator** that launches and supervises them. Each process talks over a different POSIX IPC mechanism.

**Demo flow:** `UI -> Core -> CPU / Memory / Stack / Queue -> Logger`

## Team

| Role | Name | Component | IPC | Files |
|------|------|-----------|-----|-------|
| Student 1 | Reyan | UI process | Message queues | `ui.c` |
| Student 2 | Vignesh | Core process | Shared memory | `core.c`, `sim_state.h` |
| Student 3 | Ananya | Logging process | Pipes (FIFO) | `logger.c` |
| Team Leader | Minchu | Integration, testing, benchmarking | Sockets | `integrator.c`, `ipc_defs.h`, `ctrl_util.h`, `standalone.c`, `Makefile`, tests, docs |

---

## 1. Architecture

```
              +------------- Integrator (UNIX domain sockets) -------------+
              |  creates IPC objects, launches the 3 processes,            |
              |  READY / START / DONE / SHUTDOWN control, collects metrics |
              +----------^------------------^------------------^----------+
                         | socket           | socket           | socket
  +--------+   mq: CMD   +----------------+   FIFO: log lines   +----------+
  |   UI   | ----------> |      Core      | ------------------> |  Logger  |
  |        | <---------- |  (shared mem)  |                     |          |
  +--------+  mq: REPLY  | CPU/Mem/Stk/Q  |                     +----------+
       ^                 +----------------+
       |                         |
       +---- reads state snapshot from shared memory (under lock)
```

### Processes

| Process | Responsibility | Threads |
|---------|----------------|---------|
| **UI** | Numbered menu, sends commands, shows replies, draws the state box from shared memory | Input thread + display thread |
| **Core** | Executes commands on the CPU, memory, stack and queue held in shared memory; logs each command; replies | Single thread (polls for shutdown with a 100 ms receive timeout) |
| **Logger** | Reads log lines from the pipe, timestamps them, writes `sim.log`, counts errors | Single thread (`poll` on pipe and control socket) |
| **Integrator** | Creates all IPC objects, launches the processes, runs the control protocol, gathers metrics, cleans up | Single thread (`poll`) |

### Communication links

| Link | Mechanism | Direction | Message format |
|------|-----------|-----------|----------------|
| UI to Core | POSIX message queue `/sim_cmd` | one way | `<seq> <VERB> [args]`, e.g. `3 PUSH 5` |
| Core to UI | POSIX message queue `/sim_reply` | one way | `<seq> OK|ERR <detail>`, e.g. `3 OK pushed 5` |
| Core state | POSIX shared memory `/sim_shm` | Core writes, UI reads | `sim_state_t` struct (see `sim_state.h`) guarded by a process-shared mutex |
| Core to Logger | Named pipe (FIFO) `/tmp/sim_log.fifo` | one way | text lines, `CORE|INFO|<text>` or `CORE|ERROR|<text>` |
| Integrator to all | UNIX domain stream socket `/tmp/sim_ctrl.sock` | both ways | fixed-size `ctrl_msg_t` struct |

### Supported commands

`PUSH n`, `POP`, `ENQ n`, `DEQ`, `LOAD addr val`, `STEP`. Invalid input or a full or empty stack or queue returns `ERR <reason>` and is logged as `ERROR`.

### Control protocol (socket)

```
Integrator                      UI / Core / Logger
    |  <------- connect + READY --------  |   (each process, after mapping its IPC)
    |  -------------- START ------------> |   (only when all 3 are READY)
    |           ... simulation ...        |
    |  <------------- DONE -------------- |   (UI, when the demo ends)
    |  ------------ SHUTDOWN -----------> |
    |  <----------- METRICS ------------- |   (each process: latency, CPU, memory)
```

**Startup ordering rule:** the FIFO is opened only *after* `START`. Opening a FIFO blocks until both ends exist, so opening it earlier would deadlock the READY handshake.

---

## 2. Why each IPC mechanism

### UI: POSIX message queues
The UI sends discrete commands and gets discrete replies. A message queue keeps message boundaries, blocks the reader until data arrives, and needs no manual locking, so it fits a request/response pattern. Two queues (one per direction) avoid mixing commands and replies. Its limits are a fixed maximum message size and a small queue depth, which are fine for short text commands.

*Owner notes (Student 1):* [add 2 to 3 lines in your own words]

### Core: POSIX shared memory
CPU registers, memory, the stack and the queue are one large piece of state that several processes need to read. Shared memory lets the UI read a snapshot without the data being copied through the kernel for every update, so it is the fastest option here. It has no built-in synchronisation, so the struct holds a **process-shared mutex**, initialised by Core before it announces READY so no process ever sees an uninitialised lock.

*Owner notes (Student 2):* [add 2 to 3 lines in your own words]

### Logger: pipes (named FIFO)
Logging is a one-way, ordered stream of text that is appended and never queried. A pipe matches that exactly and decouples the Logger from Core: Core just writes a line and moves on. A named FIFO is used because the processes are launched independently rather than forked from each other. The Logger reassembles lines that arrive split across `read()` calls.

*Owner notes (Student 3):* [add 2 to 3 lines in your own words]

### Integrator: sockets (UNIX domain)
Control traffic needs two-way, connection-oriented communication. A stream socket gives that, and when a process dies its socket closes, so the Integrator detects crashes immediately and can abort cleanly instead of hanging. The same channel carries metrics back for benchmarking.

---

## 3. Files

All files are in one folder.

| File | Owner | Purpose |
|------|-------|---------|
| `ipc_defs.h` | Integrator | shared names, sizes, message structs |
| `ctrl_util.h` | Integrator | helper functions for talking to the Integrator |
| `integrator.c` | Integrator | launcher, socket server, metrics, cleanup |
| `standalone.c` | Integrator | single-process baseline for the benchmark |
| `ui.c` | Student 1 | UI process (menu, 2 threads, message queues) |
| `core.c`, `sim_state.h` | Student 2 | Core process and shared-memory layout |
| `logger.c` | Student 3 | Logger process (FIFO) |
| `ui_stub.c`, `core_stub.c`, `logger_stub.c` | Integrator | minimal reference stubs used for IPC tests and the benchmark |
| `test_starters.sh` | Integrator | 9 system tests on the real components |
| `test_ipc.sh` | Integrator | 8 IPC tests on the stubs |
| `bench.sh` | Integrator | standalone vs multi-process benchmark |
| `benchmark_results.txt` | Integrator | latest benchmark output |
| `Makefile`, `.gitignore`, `TEAM_GUIDE.md` | Integrator | build, ignore rules, team contract |

---

## 4. Build and run

Requires Linux with GCC. Link flags: `-lrt -pthread`.

```bash
make clean
make demo             # builds the real components and starts the menu
make test-starters    # 9 automated system tests
make test             # 8 IPC tests using the stubs
make bench            # standalone vs multi-process benchmark
```

> `make demo` / `make test-starters` build the real `ui`, `core`, `logger`. `make test` / `make bench` build the stubs into the same binary names, so always use the `make` targets rather than compiling by hand.

### Using the menu

```
===================== SIMULATOR =====================
  1. Push to stack        5. Load memory (addr, value)
  2. Pop from stack       6. Step CPU
  3. Enqueue              7. Show state
  4. Dequeue              8. Run demo script
  0. Quit
```

Option 7 draws the CPU, stack, queue and memory straight from shared memory. After a run, `sim.log` holds one timestamped line per command.

Scripted (non-interactive) run: `UI_AUTO=1 ./integrator`

---

## 5. Testing

### System tests (`make test-starters`): 9 tests

A scripted run of 19 commands (pushes, pops, enqueues, dequeues, a memory load, three CPU steps and one deliberately invalid command).

| Test | Checks |
|------|--------|
| T1 | Integrator exits cleanly, no hang |
| T2 | All 3 processes connect over the socket |
| T3 | UI to Core (message queue): 19/19 replies, 1 expected error |
| T4 | Core state in shared memory seen by UI (`pc=3 acc=5 sp=3 qcount=3 mem0=5`) |
| T5 | Core to Logger (pipe): 19 log lines written |
| T6 | The error is logged exactly once |
| T7 | Logger summary matches (`19 lines, 1 errors`) |
| T8 | Metrics received from all 3 processes |
| T9 | No leftover IPC objects after exit |

### IPC tests with stubs (`make test`): 8 tests
1000 commands through the minimal stubs: socket connect, every command answered, shared counter equals 1000, 1000 ordered log lines, metrics from all 3, no leftover objects, clean exit.

### Robustness
The Integrator uses timeouts (10 s to connect, 60 s for the run) and aborts all processes if one dies or never connects. Stale IPC objects from a crashed run are removed at startup. The full 3-process run was repeated 20 times with no failures.

---

## 6. Benchmark: standalone vs multi-process

**Setup:** 1000 commands, 10 runs each, GitHub Codespaces (2 vCPUs), reference stubs with a trivial workload. Timing uses `CLOCK_MONOTONIC`; CPU and memory come from `getrusage`.

| Metric | Standalone | Multi-process |
|--------|-----------|---------------|
| Total execution time (avg of 10) | 0.74 ms | 35.23 ms |
| Avg IPC round trip (UI to Core and back) | n/a | 34.81 us |
| CPU time | 1.27 ms | 37.5 ms (UI 10.76, Core 17.96, Logger 8.77) |
| Peak memory | 1408 KB | about 4480 KB (UI 1664, Core 1536, Logger 1280) |

Raw output is in `benchmark_results.txt`.

## 7. Performance analysis

The standalone simulator averaged **0.74 ms** for the 1000-command workload and the three-process simulator averaged **35.23 ms**, about 48 times slower. The difference is almost entirely IPC cost. The average UI to Core message-queue round trip was **34.81 us**, and 1000 round trips at that rate come to about 34.8 ms, which accounts for nearly the whole multi-process run. Each command costs two message-queue transfers (a system call and kernel copy each), a shared-memory update, a pipe write to the Logger, and two context switches between processes. Standalone does the same work as a plain function call, which costs well under a microsecond.

CPU use rose from **1.27 ms** standalone to **37.5 ms** across the three processes. Core is the busiest because it handles every command, updates shared state and writes every log line. Peak memory rose from **1408 KB** to about **4480 KB** in total, because each process carries its own runtime and library overhead, so memory scales with the process count rather than with the data.

The overhead is a fixed cost of about 35 us per command, not a percentage of the work. In this benchmark the simulated work is trivial, so the overhead dominates; with a heavier simulator the relative cost would shrink. Codespaces provides only 2 vCPUs, which makes context switches costlier than on a typical machine. In return, separating the system gives isolation (a Logger crash does not take down Core), a clean split of responsibilities, and components that can be developed and tested independently. The trade-off is slower execution for better structure.

*Note:* the numbers above come from the reference stubs. After the real simulator logic is added to Core, copy it into `standalone.c` and rerun `make bench` so both builds do the same work.

---

## 8. Deliverables checklist

| Deliverable | Where |
|-------------|-------|
| Three-process working simulator | `make demo` |
| IPC architecture diagram | Section 1 |
| Selected IPC mechanism and justification | Section 2 |
| Working inter-process communication | `make test-starters` |
| Individual component per member | `ui.c`, `core.c`, `logger.c` (each from its owner's account) |
| Integration by Team Leader | `integrator.c`, `ipc_defs.h`, `ctrl_util.h` |
| IPC test cases | `test_starters.sh`, `test_ipc.sh` |
| Standalone vs multi-process benchmark | Section 6, `benchmark_results.txt` |
| Performance analysis | Section 7 |
| Updated GitHub documentation | this README, `TEAM_GUIDE.md` |

## 9. Interface contract (for contributors)

1. Include `ctrl_util.h`. Start with `ctrl_connect(PROC_x)`, then `ctrl_wait(s, MSG_START)`.
2. Only **open** the message queues, shared memory and FIFO. The Integrator creates them.
3. Open the FIFO only after `MSG_START`.
4. Core writes the log line **before** sending the reply.
5. UI calls `ctrl_done()` at the end. Every process, on `MSG_SHUTDOWN`, calls `ctrl_metrics()` and exits.
6. Only the Integrator edits `ipc_defs.h`, `ctrl_util.h` and `integrator.c`.
