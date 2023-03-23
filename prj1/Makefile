CC = gcc
CFLAGS = -Wall -g

all: client server
client: client.c common.h
	$(CC) $(CFLAGS) -o client client.c
server: server.c common.h
	$(CC) $(CFLAGS) -o server server.c
clean:
	rm -f client server
