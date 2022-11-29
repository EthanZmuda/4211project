#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <thread>
#include <mutex>
#include <vector>

#include "payload.h"
#include "server.h"

class Server;

class Connection {
    private:
        int client_fd;
        Server* server;
        std::thread* listen_thread;
        std::vector<char*>* topics = new std::vector<char*>();
        static void listen_loop(Connection* connection);
        int disconnected = 0;
        int cleanup = 0;

    public:
        Connection(int client_fd, Server* server);
        ~Connection();
        int disconnect_client();
        int add_topic(char* topic);
        int remove_topic(char* topic);
        inline int send_to_client(payload_t* payload) { return write(client_fd, payload, PACKET_SIZE); }
        int get_client_fd() { return client_fd; }
        int get_cleanup() { return cleanup; }
        Server* get_server() { return server; }
};

#endif