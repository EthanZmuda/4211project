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
#include <string>
#include <vector>
#include <thread>

#include "connection.h"
#include "payload.h"

#define BACKLOG 10	                // how many pending connections queue will hold
#define MAXCLIENTS 128                // max number of clients accepted

int main(int argc, char* argv[]);

class Connection;

typedef struct topic {
    char* name;
    char* retain;
    std::vector<Connection*>* connections;
    std::map<std::string, struct topic*>* subtopics;
} topic_t;

class Server {
    private:
        int server_fd;
        std::map<int, Connection*>* connections = new std::map<int, Connection*>();
        std::map<std::string, topic_t*>* topics = new std::map<std::string, topic_t*>();
        std::thread* accept_thread;
        int poll_topics(std::string topic);
        int free_topics(std::map<std::string, topic_t*>* topics);
        static void accept_loop(Server* server);

    public:
        Server(int server_fd);
        ~Server();
        int create_connection(int client_fd);
        int subscribe_to_topic(int client_fd, char* topic);
        int unsubscribe_from_topic(int client_fd, char* topic);
        int publish_message(payload_t* payload, int retain);
        int get_server_fd() { return server_fd; }
        std::map<int, Connection*>* get_connections() { return connections; }
};

#endif
