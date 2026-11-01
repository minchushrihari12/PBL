CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS  = -lrt

all: scheduler fcfs_rr_worker sjf_worker prio_worker

scheduler: main.c common.c common.h
	$(CC) $(CFLAGS) -o $@ main.c common.c $(LDLIBS)

fcfs_rr_worker: fcfs_rr_worker.c common.c common.h
	$(CC) $(CFLAGS) -o $@ fcfs_rr_worker.c common.c $(LDLIBS)

sjf_worker: sjf_worker.c common.c common.h
	$(CC) $(CFLAGS) -o $@ sjf_worker.c common.c $(LDLIBS)

prio_worker: prio_worker.c common.c common.h
	$(CC) $(CFLAGS) -o $@ prio_worker.c common.c $(LDLIBS)

run: all
	./scheduler

# create the sample output for the report (3 test cases x 6 algorithms)
sample: all
	./scheduler --demo > sample_output.txt
	@echo "Wrote sample_output.txt"

clean:
	rm -f scheduler fcfs_rr_worker sjf_worker prio_worker
