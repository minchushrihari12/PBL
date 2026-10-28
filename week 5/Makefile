CC = gcc
CFLAGS = -Wall -Wextra -O2
ALGOS = fifo.c lru.c optimal.c

all: core logger mqclient

core: core.c simulation.c $(ALGOS) *.h
	$(CC) $(CFLAGS) core.c simulation.c $(ALGOS) -o core -lrt
logger: logger.c protocol.h
	$(CC) $(CFLAGS) logger.c -o logger -lrt
mqclient: mqclient.c protocol.h
	$(CC) $(CFLAGS) mqclient.c -o mqclient -lrt
test: test.c simulation.c $(ALGOS) *.h
	$(CC) $(CFLAGS) test.c simulation.c $(ALGOS) -o test && ./test
clean:
	rm -f core logger mqclient test simulation.log
