# Week 4: Interactive Deadlock Simulation System

Three C processes talk through **System V message queues**; the GUI runs in the browser and is served by the UI process.

```
 browser  <--HTTP-->  ui  --KEY_CORE-->  core  (state + algorithms)
                       ^                  |
                       +-----KEY_UI-------+   JSON replies (chunked)
                       |                  |
                       +----KEY_LOG---> logger <---KEY_LOG---+  (core also logs)
```

| Process | Files | Owner |
|---|---|---|
| core | `core.c`, `deadlock.c`, `deadlock.h`, `test_deadlock.c` | Core teammate |
| ui | `ui.c`, `gui/index.html` | UI teammate |
| logger | `logger.c` | Logger teammate |
| integration | `ipc_defs.h`, `Makefile`, `run.sh`, `README.md` | Integrator |

## Run it

```bash
cd week4
./run.sh            # builds, starts core + logger + ui, opens http://127.0.0.1:8085
make test           # algorithm unit tests (no IPC needed)
make ipcclean       # only if a crash left queues behind
```

Start order if you run them by hand in 3 terminals: `./core`, then `./logger`, then `./ui`.

**Mac:** works as is (Xcode command line tools: `xcode-select --install`).
**Windows:** System V queues do not exist in native Windows. Use **WSL2**:
`wsl --install`, open Ubuntu, `sudo apt install build-essential git`, clone the repo *inside* WSL (e.g. `~/pbl`), run `./run.sh`, then open http://localhost:8085 in the Windows browser.

## What the GUI covers (from the brief)

- Create processes and resources, enter Total / Allocation / Maximum, see Available, Need and pending Request matrices
- Resource requests with **Banker's avoidance** (denied if it would be unsafe) or **no avoidance** (process blocks, so deadlock can occur)
- **Run Banker's algorithm** with a step by step trace and the safe sequence
- **Detect deadlock** (Allocation + pending Request) and show the deadlocked processes
- **Resource Allocation Graph** (held = solid, waiting = dashed, deadlocked = red)
- **Recovery** by terminating or preempting the lowest cost victim until no deadlock remains
- Status shown as SAFE / UNSAFE / DEADLOCKED, per-resource utilization, event log, and `deadlock.log` written by the logger
- One click examples: safe (textbook), unsafe, deadlock (circular wait)

## Message protocol (ui -> core, text, numbers only)

```
RESET                         SETUP <np> <nr>            SCENARIO <1|2|3>
TOTAL <nr values>             ALLOCALL <np*nr values>    MAXALL <np*nr values>
REQUEST <p> <nr values>       REQUESTRAW <p> <nr values>
RELEASE <p> <nr values>       TERMINATE <p>
BANKER                        DETECT                     RECOVER <1 terminate|2 preempt>
STATE
```

Core answers every command with one JSON document: `ok`, `msg`, matrices, `status`, `seq`, `dead`, `util`, `events`.
Messages stay under 1 KB because macOS limits a queue to 2048 bytes; replies are split into 800 byte chunks.

## Demo script

1. **Safe example** -> *Run Banker's algorithm* -> safe sequence P1 P3 P4 P0 P2
2. Request `P1: 1 0 2` (granted), then `P0: 0 2 0` (denied, unsafe state) to show avoidance
3. **Unsafe example** -> status UNSAFE but nothing blocked
4. **Deadlock example** -> *Detect deadlock* -> *Recover: terminate* -> watch the graph clear

## Add to git

Put the folder next to your earlier weeks so nothing is overwritten (the old `core.c`, `ui.c`, `Makefile`, `run.sh` live in the repo root):

```bash
git pull
git add week4
git commit -m "Week 4: deadlock simulation system (core, ui, logger, GUI)"
git push
```
