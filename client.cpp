#include "client.h"
#include <errno.h>

Client* client;
int connected = 0;
int disconnected = 0;   // set to 1 when client disconnects
int cleanup = 0;        // set to 1 when client disconnects and all threads have exited

// function to handle SIGINT and SIGTERM
void sig_handler(int s) {
    printf("Caught signal, disconnecting\n");
	if (client->disconnect_from_server()) cleanup = 1;  // if client is connected, disconnect
                                                        // if the disconnect fails, force shut down anyway
}

// function to handle connecting to server on a certain port
int Client::connect_to_server(const char* hostname, const char* port) {
    struct addrinfo* server_addr;
    if (getaddrinfo(hostname, port, NULL, &server_addr)) {  // get address info for server
        freeaddrinfo(server_addr);
        printf("failed to get addrinfo\n");
        return 1;
    }

    int server_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol); // create socket

    if (server_fd == 0) { // check if socket creation failed
        freeaddrinfo(server_addr);
        printf("failed to create socket\n");
        return 1;
    }

	fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK); // set socket to non-blocking

    connect(server_fd, server_addr->ai_addr, server_addr->ai_addrlen); // connect to server

    // as the socket is non-blocking, we need to poll it to see if the connection is successful

    struct pollfd pfd = {server_fd, POLLOUT, 0}; // create pollfd struct

    if (poll(&pfd, 1, 1000) == -1) { // poll socket for 1 second
        freeaddrinfo(server_addr);
        printf("Poll failed\n");
        return -1;
    }

    if ((pfd.revents & POLLOUT) != POLLOUT) { // check if socket is ready for writing
        freeaddrinfo(server_addr);
        printf("Socket did not open for writing\n");
        return 1;
    }

    int optval = 0;
    socklen_t socklen = 4;
    getsockopt(server_fd, SOL_SOCKET, SO_ERROR, &optval, &socklen); // check if socket has an error

    if (optval) { // if socket has an error, print error and return
        freeaddrinfo(server_addr);
        printf("Socket had an error\n");
        close(server_fd);
        return 1;
    }

    freeaddrinfo(server_addr);

    printf("Connecting to server\n");
    
    this->sock_fd = server_fd;
    this->listen_thread = new std::thread(listen_loop, this);
    
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "CONN");
    
    send_to_server(&payload); // send connection request to server

    int tries = 0;
    while (!connected) { // wait for server to return CONN_ACK, which will set connected to 1. If server does not respond, force shut down
        if (tries < 0) {
            printf("Did not receive CONN_ACK, failed to connect to server\n");
            cleanup = 1;
            return 1;
        }
        usleep(100000); // sleep for 100ms, this is the timeout
        tries++;
    }
    return 0;
}

// function to handle disconnecting from server
int Client::disconnect_from_server() { 
    printf("Disconnecting from server\n");
    
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "DISC");

    send_to_server(&payload); // send disconnect request to server

    int tries = 0;
    while (!disconnected) {
        if (tries >= 10) {
            printf("Failed to disconnect from server\n");
            return 1;
        }
        usleep(100000); // sleep for 100ms, this is the timeout
        tries++;
    }
    return 0;
}

// function to handle processing an input string from the user
int Client::process_string(const char* str) {
    char* str2 = strdup(str);
    char* token = strtok((char*) str2, " ");
    if (strncmp(token, "PUB", strnlen(token, REQ_SIZE)) == 0) { // if the first token is PUB, send a PUB request to the server
        token = strtok(NULL, " ");
        if (!token) { // if there is no second token, print an error
            printf("Invalid PUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "PUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        snprintf(payload.msg, MSG_SIZE, "%s", str+5+strnlen(token, TOPIC_SIZE));
        if (!(token = strtok(NULL, " "))) { // if there is no message, print an error
            printf("Invalid PUB request\n");
            free(str2);
            return 1;
        }
        send_to_server(&payload);
    }
    else if (strncmp(token, "PUBRET", strnlen(token, REQ_SIZE)) == 0) { // if the first token is PUBRET, send a PUBRET request to the server
        token = strtok(NULL, " ");
        if (!token) { // if there is no second token, print an error
            printf("Invalid PUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "PUBRET");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        snprintf(payload.msg, MSG_SIZE, "%s", str+8+strnlen(token, TOPIC_SIZE));
        if (!(token = strtok(NULL, " "))) { // if there is no message, print an error
            printf("Invalid PUBRET request\n");
            free(str2);
            return 1;
        }
        send_to_server(&payload);
    }
    else if (strncmp(token, "SUB", strnlen(token, REQ_SIZE)) == 0) { // if the first token is SUB, send a SUB request to the server
        token = strtok(NULL, " ");
        if (!token) { // if there is no second token, print an error
            printf("Invalid SUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "SUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        send_to_server(&payload);
    }
    else if (strncmp(token, "UNSUB", strnlen(token, REQ_SIZE)) == 0) { // if the first token is UNSUB, send a UNSUB request to the server
        token = strtok(NULL, " ");
        if (!token) { // if there is no second token, print an error
            printf("Invalid UNSUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "UNSUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        send_to_server(&payload);
    }
    else if (strncmp(token, "LIST", strnlen(token, REQ_SIZE)) == 0) { // if the first token is LIST, send a LIST request to the server
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "LIST");
        send_to_server(&payload);
    }
    else if (strncmp(token, "DISCONNECT", strnlen(token, REQ_SIZE)) == 0) { // if the first token is DISCONNECT (or DISC), disconnect from the server
        if (client->disconnect_from_server()) cleanup = 1;
    }
    else {
        printf("Invalid request\n");
        free(str2);
        return 1;
    }
    free(str2);
    return 0;
}

// function to handle listening for messages from the server
// this function is run in a separate thread
void Client::listen_loop(Client* client) {
    payload_t payload = {0};
    printf("Listening\n");
    while (!cleanup) { // while the client is not shutting down
        int nread = read(client->get_server_fd(), &payload, PACKET_SIZE); // read a packet from the server

        if (nread != PACKET_SIZE) continue; // if the packet is not the correct size, ignore it

        if (strncmp(payload.req, "PUB", REQ_SIZE) == 0 || strncmp(payload.req, "PUBRET", REQ_SIZE) == 0) { // if the packet is a PUB or PUBRET packet, print the message
            printf("%s: %s\n", payload.topic, payload.msg);
        }
        else if (strncmp(payload.req, "LIST", REQ_SIZE) == 0) { // if the packet is a LIST packet, print the list of topics
            printf("Subscribed topics: %s\n", payload.msg);
        }
        else if (strncmp(payload.req, "DISC", REQ_SIZE) == 0) { // if the packet is a DISC packet, disconnect from the server
            printf("Received DISC, sending DISC_ACK\n");

            snprintf(payload.req, REQ_SIZE, "DISC_ACK");

            usleep(100000);

            client->send_to_server(&payload); // send a DISC_ACK packet to the server

            disconnected = 1;   // set the disconnected flag to true
            cleanup = 1;        // set the cleanup flag to true
            break;
        }
        else if (strncmp(payload.req, "CONN_ACK", REQ_SIZE) == 0) { // if the packet is a CONN_ACK packet, print a message and set the connected flag to true
            printf("Received CONN_ACK\n");
            connected = 1;
        }
        else if (strncmp(payload.req, "DISC_ACK", REQ_SIZE) == 0) { // if the packet is a DISC_ACK packet, print a message and set the disconnected and cleanup flags to true
            printf("Received DISC_ACK\n");
            disconnected = 1;
            cleanup = 1;
            break;
        }
    }
}

Client::Client() {}

Client::~Client() {
    close(get_server_fd());
    if (listen_thread) listen_thread->join();
}

int main(int argc, char* argv[]) {
    if(argc < 3){
        printf("Correct usage:\n./client <hostname> <port>\n");
		return 1;
    }
    const char* hostname = argv[1];
    const char* port = argv[2];

    // set up signal handelling for SIGINT and SIGTERM
    struct sigaction my_sa = {};
	sigemptyset(&my_sa.sa_mask);
	my_sa.sa_handler = sig_handler;
	sigaction(SIGINT, &my_sa, NULL);
	sigaction(SIGTERM, &my_sa, NULL);

    client = new Client(); // create a new client object

    printf("Host: %s Port: %s\n", hostname, port);

    if (client->connect_to_server(hostname, port)) {
        delete client;
        return 1;
    }

    std::string input;

    while (!cleanup) { // while the client is not shutting down, constantly poll the user for input
        struct pollfd pfd = {0, POLLIN, 0};

        poll(&pfd, 1, 0); // I used poll so that it's nonblocking, and the cleanup flag can be checked

        if ((pfd.revents & POLLIN) == POLLIN) {
            std::getline(std::cin, input);
            client->process_string(input.c_str());
        }
    };

    printf("Shutting down\n");

    delete client;

    return 0;
}