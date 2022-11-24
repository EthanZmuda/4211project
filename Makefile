CFLAGS = -Wall -g
CC     = g++ $(CFLAGS)

all : server client

server.o : server.cpp server.h
	$(CC) -c $<

client.o : client.cpp client.h
	$(CC) -c $<

server : server.o
	$(CC) -o $@ $^

client : client.o
	$(CC) -pthread -o $@ $^

clean:
	rm -rf *.o server client