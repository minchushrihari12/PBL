# Week 3 – CPU Scheduling Simulator (C + POSIX message queues)

Everything is written in C. The only IPC used is **POSIX message queues**.
The program is four separate processes:

```
                 Q_REQ_FCFS_RR  ──►  fcfs_rr_worker  (Teammate 1)
   scheduler ──► Q_REQ_SJF      ──►  sjf_worker      (Teammate 2)
   (Integrator)  Q_REQ_PRIO     ──►  prio_worker     (Teammate 3)
        ▲                                   │
        └──────────────  Q_RES  ◄───────────┘
```

`scheduler` shows the menu, sends a `Request` (processes + algorithm +
quantum) to the right worker and prints the `Response` (Gantt chart, per
process WT/TAT/RT, averages, CPU utilization).

## Who uploads what

| File | Owner | Contents |
|---|---|---|
| `common.h`, `common.c` | Integrator | shared structs, queue helpers, metrics, worker loop |
| `main.c` | Integrator | menu, input, random generation, test cases, Gantt/table output, comparison table |
| `Makefile` | Integrator | build rules |
| `fcfs_rr_worker.c` | Teammate 1 | FCFS and Round Robin |
| `sjf_worker.c` | Teammate 2 | SJF non-preemptive and preemptive (SRTF) |
| `prio_worker.c` | Teammate 3 | Priority non-preemptive and preemptive |
| `sample_output.txt` | Integrator | output of `make sample` (3 test cases x 6 algorithms) |
| `REPORT.md` | everyone | analysis (each teammate fills in their part) |

## Build and run

POSIX message queues exist on **Linux only** (they do not exist on macOS), so
build and run in the GitHub Codespace / Docker Linux environment.

```
make            # builds scheduler + 3 workers
./scheduler     # interactive menu
make sample     # writes sample_output.txt
make clean
```

Run `./scheduler` from the folder that contains the worker executables.

## Rules for the algorithm files (read before coding)

* Priority convention: **lower number = higher priority**.
* Your function gets a `Response` that is already filled in. Do only this:
  1. call `add_segment(res, pid, start, end)` for each stretch of CPU time in
     time order from time 0 (use `IDLE_PID` for idle time);
  2. set `first_run` and `completion` for every process.
* Do **not** compute WT/TAT/RT/averages/utilization – `finalize_metrics()` does it.
* Tie-breaks: earlier arrival, then input order; a preemptive algorithm keeps
  the running process on a tie.
* Do not edit `common.h` – ask the Integrator.

## Limits

Up to 10 processes, arrival 0–100, burst 1–30, priority 1–99, quantum 1–20.
