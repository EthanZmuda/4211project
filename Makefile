CFLAGS = -Wall -g
CC     = g++ $(CFLAGS)

all : server client

connection.o : connection.cpp connection.h payload.h
	$(CC) -c $<

server.o : server.cpp server.h payload.h
	$(CC) -c $<

client.o : client.cpp client.h payload.h
	$(CC) -c $<

server : server.o connection.o
	$(CC) -pthread -o $@ $^

client : client.o
	$(CC) -pthread -o $@ $^

clean:
	rm -rf *.o server client