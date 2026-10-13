CC     = gcc
CFLAGS = -Wall -Wextra -pthread
LIBS   = -lrt
 
all: ui core logger test_scheduler
 
ui: ui.c ipc_defs.h
	$(CC) $(CFLAGS) ui.c -o ui $(LIBS)
 
core: core.c cpu.c cpu.h scheduler.c scheduler.h ipc_defs.h
	$(CC) $(CFLAGS) core.c cpu.c scheduler.c -o core $(LIBS)
 
logger: logger.c ipc_defs.h
	$(CC) $(CFLAGS) logger.c -o logger $(LIBS)
 
test_scheduler: test_scheduler.c scheduler.c scheduler.h ipc_defs.h
	$(CC) $(CFLAGS) test_scheduler.c scheduler.c -o test_scheduler $(LIBS)
 
clean:
	rm -f ui core logger test_scheduler
 
