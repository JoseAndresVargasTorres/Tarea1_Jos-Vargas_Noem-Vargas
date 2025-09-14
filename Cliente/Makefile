CC=gcc
CFLAGS=-Wall

all: cliente servidor

cliente: cn.c
	$(CC) $(CFLAGS) -o cliente cn.c -lm -lpthread

servidor: sn.c
	$(CC) $(CFLAGS) -o servidor sn.c -lm -lpthread

run-cliente: cliente
	./cliente

run-servidor: servidor
	./servidor

clean:
	rm -f cliente servidor
