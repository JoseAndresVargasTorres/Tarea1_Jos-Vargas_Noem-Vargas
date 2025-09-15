CC=gcc
CFLAGS=-Wall

all: cliente servidor

cliente: cn.c
	$(CC) $(CFLAGS) -o cliente cn.c -lm

servidor: sn.c
	$(CC) $(CFLAGS) -o servidor sn.c -lm

run-cliente: cliente
	./cliente

run-servidor: servidor
	./servidor

clean:
	rm -f cliente servidor
