# Team Guide – Week 4 Multi-Process Simulator

## Who does what
| Person | Files | IPC | Must do |
|--------|-------|-----|---------|
| Student 1 (UI) | `starters/ui.c` | Message queues | display, input, threads |
| Student 2 (Core) | `starters/core.c`, `sim_state.h` | Shared memory | CPU/Memory/Stack/Queue logic |
| Student 3 (Logger) | `starters/logger.c` | Pipe (FIFO) | execution + error logging |
| Integrator | `integrator.c`, `ipc_defs.h`, `ctrl_util.h`, `standalone.c`, `tests/`, docs | Sockets | integrate, test, benchmark |

The starters are **working templates**: the IPC is already wired and tested end to end. Each owner must read their file,
understand it, and extend/rewrite it as their own component (the assignment requires individual implementation).
Each `TODO` comment at the top of a starter lists what is left for you.

## Quick start
```bash
make starters      # build everything
make demo          # interactive: type PUSH 5, ENQ 3, STEP, show, quit
make test-starters # 9 automated system tests
make test          # 8 IPC tests with the minimal stubs
make bench         # standalone vs multi-process -> benchmark_results.txt
```
Only your own file changes: edit `starters/ui.c` (etc.), run `make starters && make demo`.

## Rules everyone follows (the interface contract)
1. Include `ctrl_util.h`; start with `ctrl_connect(PROC_x)` then `ctrl_wait(s, MSG_START)`.
2. Only **open** the mq / shm / FIFO. The Integrator creates them.
3. Open the FIFO only **after** START.
4. Core logs **before** replying. Log line format: `CORE|INFO|<text>` or `CORE|ERROR|<text>`.
5. UI sends `ctrl_done()` at the end. Everyone: on `MSG_SHUTDOWN` call `ctrl_metrics()` and exit.
6. Message format UI→Core: `<seq> <VERB> [args]`; Core→UI: `<seq> OK|ERR <detail>`.
7. Compile with `-lrt -pthread`.

## Deliverables checklist
- [ ] Three-process simulator working (`make test-starters` all PASS)
- [ ] IPC architecture diagram (README.md)
- [ ] IPC choice + justification (README.md table; each owner adds 2–3 lines on why *their* IPC)
- [ ] Each member's own component committed from their own GitHub account
- [ ] IPC test cases (`tests/`)
- [ ] Benchmark: `make bench`, paste `benchmark_results.txt` into README
- [ ] Performance analysis paragraph (total time, IPC latency, CPU, memory; standalone vs multi-process)
- [ ] Demo flow: UI → Core → CPU/Memory/Stack/Queue → Logger (`make demo`)

## Tip for a fair benchmark
Put your real simulator logic into `standalone.c: core_exec()` so the standalone and multi-process runs do the same work.
