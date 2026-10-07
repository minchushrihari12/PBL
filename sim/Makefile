CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-format-truncation -O2
LDLIBS  = -lrt
BINS    = logger core_process ui_process

all: $(BINS)

%: %.c ipc_defs.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(BINS)

# remove leftover queues from a crashed run / older build
clean-queues:
	rm -f /dev/mqueue/sim_cmd /dev/mqueue/sim_res /dev/mqueue/sim_log /dev/mqueue/sim_log_disk
