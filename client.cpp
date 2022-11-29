#include "client.h"
#include <errno.h>

Client* client;
int connected = 0;
int disconnected = 0;
int cleanup = 0;

void sig_handler(int s){
    printf("Caught signal, disconnecting\n");
	if (client->disconnect_from_server()) cleanup = 1;
}

int Client::connect_to_server(const char* hostname, const char* port) {
    struct addrinfo* server_addr;
    if (getaddrinfo(hostname, port, NULL, &server_addr)) {
        freeaddrinfo(server_addr);
        printf("failed to get addrinfo\n");
        return 1;
    }

    int server_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol);

    if (server_fd == 0) {
        freeaddrinfo(server_addr);
        printf("failed to create socket\n");
        return 1;
    }

	fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK);

    connect(server_fd, server_addr->ai_addr, server_addr->ai_addrlen);

    struct pollfd pfd = {server_fd, POLLOUT, 0};

    if (poll(&pfd, 1, -1) == -1) {
        freeaddrinfo(server_addr);
        printf("Poll failed\n");
        return -1;
    }

    if ((pfd.revents & POLLOUT) != POLLOUT) {
        freeaddrinfo(server_addr);
        printf("Socket did not open for writing\n");
        return 1;
    }

    int optval = 0;
    socklen_t socklen = 4;
    getsockopt(server_fd, SOL_SOCKET, SO_ERROR, &optval, &socklen);

    if (optval) {
        freeaddrinfo(server_addr);
        printf("Socket had an error\n");
        close(server_fd);
        return 1;
    }

    freeaddrinfo(server_addr);

    printf("Connecting to server\n");
    
    this->server_fd = server_fd;
    this->listen_thread = new std::thread(listen_loop, this);
    
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "CONN");
    
    send_to_server(&payload);

    int tries = 0;
    while (!connected) {
        if (tries < 0) {
            printf("Did not receive CONN_ACK, failed to connect to server\n");
            cleanup = 1;
            return 1;
        }
        usleep(100000);
        tries++;
    }
    return 0;
}

int Client::disconnect_from_server() {
    printf("Disconnecting from server\n");
    
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "DISC");

    send_to_server(&payload);

    int tries = 0;
    while (!disconnected) {
        if (tries >= 10) {
            printf("Failed to disconnect from server\n");
            return 1;
        }
        usleep(100000);
        tries++;
    }
    return 0;
}

int Client::process_string(const char* str) {
    char* str2 = strdup(str);
    char* token = strtok((char*) str2, " ");
    if (strncmp(token, "PUB", strnlen(token, REQ_SIZE)) == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Invalid PUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "PUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        snprintf(payload.msg, MSG_SIZE, "%s", str+5+strnlen(token, TOPIC_SIZE));
        send_to_server(&payload);
    }
    else if (strncmp(token, "PUBRET", strnlen(token, REQ_SIZE)) == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Invalid PUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "PUBRET");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        snprintf(payload.msg, MSG_SIZE, "%s", str+8+strnlen(token, TOPIC_SIZE));
        send_to_server(&payload);
    }
    else if (strncmp(token, "SUB", strnlen(token, REQ_SIZE)) == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Invalid SUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "SUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        send_to_server(&payload);
    }
    else if (strncmp(token, "UNSUB", strnlen(token, REQ_SIZE)) == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Invalid UNSUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "UNSUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        send_to_server(&payload);
    }
    else if (strncmp(token, "UNSUB", strnlen(token, REQ_SIZE)) == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Invalid UNSUB request\n");
            free(str2);
            return 1;
        }
        payload_t payload = {0};
        snprintf(payload.req, REQ_SIZE, "UNSUB");
        snprintf(payload.topic, TOPIC_SIZE, "%s", token);
        send_to_server(&payload);
    }
    free(str2);
    return 0;
}

void Client::listen_loop(Client* client) {
    payload_t payload = {0};
    printf("Listening\n");
    while (!cleanup) {
        int nread = read(client->get_server_fd(), &payload, PACKET_SIZE);

        if (nread != PACKET_SIZE) continue;

        if (strncmp(payload.req, "PUB", REQ_SIZE) == 0 || strncmp(payload.req, "PUBRET", REQ_SIZE) == 0) {
            printf("%s: %s\n", payload.topic, payload.msg);
        }
        else if (strncmp(payload.req, "DISC", REQ_SIZE) == 0) {
            printf("Received DISC, sending DISC_ACK\n");

            snprintf(payload.req, REQ_SIZE, "DISC_ACK");

            usleep(100000);

            client->send_to_server(&payload);

            disconnected = 1;
            cleanup = 1;
            break;
        }
        else if (strncmp(payload.req, "CONN_ACK", REQ_SIZE) == 0) {
            printf("Received CONN_ACK\n");
            connected = 1;
        }
        else if (strncmp(payload.req, "DISC_ACK", REQ_SIZE) == 0) {
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

    const char* hostname = "76.17.252.142";
    const char* port = "42069";
    if(argc >= 3){
        hostname = argv[1];
        port = argv[2];
    }

    struct sigaction my_sa = {};
	sigemptyset(&my_sa.sa_mask);
	my_sa.sa_handler = sig_handler;
	sigaction(SIGINT, &my_sa, NULL);
	sigaction(SIGTERM, &my_sa, NULL);

    client = new Client();

    printf("Host: %s Port: %s\n", hostname, port);

    if (client->connect_to_server(hostname, port)) {
        delete client;
        return 1;
    }

    std::string input;

    while (!cleanup) {
        struct pollfd pfd = {0, POLLIN, 0};

        poll(&pfd, 1, 0);

        if ((pfd.revents & POLLIN) == POLLIN) {
            std::getline(std::cin, input);
            client->process_string(input.c_str());
        }
    };

    printf("Shutting down\n");

    delete client;

    return 0;
}