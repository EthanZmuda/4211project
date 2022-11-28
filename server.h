#ifndef SERVER_H
#define SERVER_H

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
#include <map>
#include <vector>
#include <thread>

#include "connection.h"
#include "payload.h"

#define BACKLOG 10	                // how many pending connections queue will hold
#define MAXCLIENTS 128                // max number of clients accepted

int main(int argc, char* argv[]);

class Connection;

class Server {
    private:
        int server_fd;
        std::map<int, Connection*>* connections = new std::map<int, Connection*>();
        std::vector<Connection*>* weather = new std::vector<Connection*>();
        std::vector<Connection*>* news = new std::vector<Connection*>();
        std::vector<Connection*>* health = new std::vector<Connection*>();
        std::vector<Connection*>* security = new std::vector<Connection*>();
        std::thread* accept_thread;
        static void accept_loop(Server* server);

    public:
        Server(int server_fd);
        ~Server();
        int create_connection(int client_fd);
        int subscribe_to_topic(int client_fd, char* topic);
        int publish_message(payload_t* payload);
        int get_server_fd() { return server_fd; }
        std::map<int, Connection*>* get_connections() { return connections; }
};

#endif
