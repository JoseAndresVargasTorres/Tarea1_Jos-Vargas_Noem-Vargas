CC=gcc
CFLAGS=-Wall

all: cliente servidor

cliente: c.c
	$(CC) $(CFLAGS) -o cliente c.c -lm

servidor: s.c
	$(CC) $(CFLAGS) -o servidor s.c -lm

run-cliente: cliente
	./cliente

run-servidor: servidor
	./servidor

clean:
	rm -f cliente servidor
