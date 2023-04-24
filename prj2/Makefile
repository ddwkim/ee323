CC = gcc
CFLAGS = -Wall -g
OBJECT = proxy.o utils.o handler.o
TARGET = proxy

all: $(TARGET)

utils.o: utils.c utils.h common.h
	$(CC) $(CFLAGS) -o $@ -c utils.c

handler.o: handler.c common.h utils.h handler.h
	$(CC) $(CFLAGS) -o $@ -c handler.c

proxy.o: proxy.c common.h utils.h handler.h
	$(CC) $(CFLAGS) -o $@ -c proxy.c

$(TARGET): $(OBJECT)
	$(CC) $(CFLAGS) -o $@ $(OBJECT)

clean:
	rm -f $(TARGET) $(OBJECT)