CC = gcc
all: server client
server: sender.c
	$(CC) sender.c -o sender -w
client: receiver.c
	$(CC) receiver.c -o receiver -w
clean:
	rm -f sender receiver n_*
