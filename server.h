#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <semaphore.h>

#define MAXLINE 1024              // max length of line that can be typed for messages
#define MAXNAME 256               // max length of user name for clients
#define MAXPATH 1024              // max length filename paths
#define MAXCLIENTS 256            // max number of clients accepted

#define EOT 4                     // ascii code of typical EOF character
#define DEL 127                   // ascii code of typical backspace key

#define DISCONNECT_SECS 5         // seconds before clients are dropped due to lack of contact

#define PORT "42069"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold

// client_t: data on a client connected to the server
typedef struct {
  char name[MAXPATH];             // name of the client
  int to_client_fd;               // file descriptor to write to to send to client
  int to_server_fd;               // file descriptor to read from to receive from client
} client_t;

// server_t: data pertaining to server operations
typedef struct {
  char server_name[MAXPATH];      // name of server which dictates file names for joining and logging
  int join_fd;                    // file descriptor of join file/FIFO
  int join_ready;                 // flag indicating if a join is available
  int n_clients;                  // number of clients communicating with server
  client_t client[MAXCLIENTS];    // array of clients populated up to n_clients
  int time_sec;                   // ADVANCED: time in seconds since server started
  int log_fd;                     // ADVANCED: file descriptor for log
  sem_t* log_sem;                 // ADVANCED: posix semaphore to control who_t section of log file
} server_t;

void sigchld_handler(int s);
void* get_in_addr(struct sockaddr *sa);
int main(int argc, char* argv[]);

#endif
