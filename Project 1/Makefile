CC=gcc
CFLAGS=-I.
OBJ = webserver.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

webserver: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)