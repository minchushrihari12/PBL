CC = gcc
CFLAGS = -Wall -Wextra -O2
ALGOS = fifo.c lru.c optimal.c

all: core logger mqclient server

core: core.c simulation.c page.c $(ALGOS) *.h
	$(CC) $(CFLAGS) core.c simulation.c page.c $(ALGOS) -o core -lrt
logger: logger.c protocol.h
	$(CC) $(CFLAGS) logger.c -o logger -lrt
mqclient: mqclient.c protocol.h
	$(CC) $(CFLAGS) mqclient.c -o mqclient -lrt
server: server.c page.c *.h
	$(CC) $(CFLAGS) server.c page.c -o server
test: test.c simulation.c $(ALGOS) *.h
	$(CC) $(CFLAGS) test.c simulation.c $(ALGOS) -o test && ./test
clean:
	rm -f core logger mqclient server test simulation.log
