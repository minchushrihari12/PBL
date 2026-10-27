# Page Replacement Algorithm Simulator (C)

Implements FIFO, LRU, and Optimal page replacement and compares page hits, page faults, and ratios.

## Files
- `main.c` — application entry point and orchestration
- `ui.c`, `ui.h` — console input, validation, and output tables
- `core.c`, `core.h` — algorithm implementations and simulation state
- `logger.c`, `logger.h` — timestamped activity log (`simulation.log`)
- `Makefile` — native build commands
- `Dockerfile` — reproducible container build/run

## Run in GitHub Codespaces (Linux)
```sh
make
./page_replacement
```

Or:
```sh
make run
```

## Run with Docker
Build:
```sh
docker build -t page-replacement .
```
Run interactively so stdin works:
```sh
docker run --rm -it page-replacement
```

## Run on macOS
Install Xcode Command Line Tools if `cc` is unavailable:
```sh
xcode-select --install
```
Then run `make` and `./page_replacement`.

## Example input
```text
7 0 1 2 0 3 0 4 2 3
3
4
```

Input lines are: reference string, frame count, algorithm choice (1 FIFO, 2 LRU, 3 Optimal, 4 compare all).

## Windows teammates
The easiest shared route is GitHub Codespaces: open the same repository and run `make`. No Python is required. If compiling locally on Windows, use WSL or a C compiler such as MinGW-w64 and run `mingw32-make` if Make is installed.

## Notes
- Page numbers are non-negative integers; `-1` is reserved internally to represent an empty frame.
- Reference string limit: 10,000 pages; frame count limit: 1,000.
- The program is console-based, which is explicitly allowed by the project specification.
