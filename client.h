#ifndef CLIENT_H
#define CLIENT_H

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
#include <fcntl.h>
#include <poll.h>
#include <thread>
#include <string>
#include <iostream>
#include "payload.h"

int main(int argc, char* argv[]);

// Client class
class Client {
    private:
        int sock_fd;
        std::thread* listen_thread;
        static void listen_loop(Client* client);
    public:
        int connect_to_server(const char* hostname, const char* port);
        int disconnect_from_server();
        int process_string(const char* str);
        inline int send_to_server(payload_t* payload) { return write(sock_fd, payload, PACKET_SIZE); }
        int get_server_fd() { return sock_fd; }
        Client();
        ~Client();
};

#endif
