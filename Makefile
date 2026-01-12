CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pthread

all: ipc_sender ipc_receiver

ipc_sender: ipc_sender.c ipc_common.h
	$(CC) $(CFLAGS) -o $@ ipc_sender.c

ipc_receiver: ipc_receiver.c ipc_common.h
	$(CC) $(CFLAGS) -o $@ ipc_receiver.c

clean:
	rm -f ipc_sender ipc_receiver

.PHONY: all clean
