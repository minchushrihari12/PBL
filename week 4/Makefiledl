# Week 4 - Deadlock Simulation System (macOS / Linux / WSL)
CC      ?= cc
CFLAGS  ?= -Wall -Wextra -std=gnu99 -O2
BINS     = core logger ui test_deadlock

all: $(BINS)

core: core.c deadlock.c deadlock.h ipc_defs.h
	$(CC) $(CFLAGS) -o $@ core.c deadlock.c

logger: logger.c ipc_defs.h
	$(CC) $(CFLAGS) -o $@ logger.c

ui: ui.c ipc_defs.h
	$(CC) $(CFLAGS) -o $@ ui.c

test_deadlock: test_deadlock.c deadlock.c deadlock.h
	$(CC) $(CFLAGS) -o $@ test_deadlock.c deadlock.c

test: test_deadlock
	./test_deadlock

# remove leftover System V queues if a run crashed
ipcclean:
	-ipcrm -Q 0x44450001
	-ipcrm -Q 0x44450002
	-ipcrm -Q 0x44450003

clean:
	rm -f $(BINS) *.log

.PHONY: all test ipcclean clean
