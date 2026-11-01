CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LIBS    = -lrt -pthread

all: integrator standalone

integrator: integrator.c ipc_defs.h
	$(CC) $(CFLAGS) -o $@ integrator.c $(LIBS)

standalone: standalone.c ipc_defs.h
	$(CC) $(CFLAGS) -o $@ standalone.c $(LIBS)

# test stubs (lets you test the integrator before teammates finish)
stubs: all
	$(CC) $(CFLAGS) -o ui     stubs/ui_stub.c     $(LIBS)
	$(CC) $(CFLAGS) -o core   stubs/core_stub.c   $(LIBS)
	$(CC) $(CFLAGS) -o logger stubs/logger_stub.c $(LIBS)

# starter templates for the teammates (full working UI/Core/Logger with the right IPC wired up)
starters: all
	$(CC) $(CFLAGS) -I. -o ui     starters/ui.c     $(LIBS)
	$(CC) $(CFLAGS) -I. -o core   starters/core.c   $(LIBS)
	$(CC) $(CFLAGS) -I. -o logger starters/logger.c $(LIBS)

demo: starters
	./integrator

test-starters: starters
	bash tests/test_starters.sh

# real teammate code: put ui.c, core.c, logger.c in this folder (edit if they have more files)
real: all
	$(CC) $(CFLAGS) -o ui     ui.c     $(LIBS)
	$(CC) $(CFLAGS) -o core   core.c   $(LIBS)
	$(CC) $(CFLAGS) -o logger logger.c $(LIBS)

test: stubs
	bash tests/test_ipc.sh

bench: stubs
	bash tests/bench.sh 10

clean:
	rm -f integrator standalone ui core logger *.log
	-rm -f /dev/shm/sim_shm /dev/mqueue/sim_cmd /dev/mqueue/sim_reply /tmp/sim_log.fifo /tmp/sim_ctrl.sock
