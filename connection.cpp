#include "connection.h"

int Connection::disconnect_client() {
    printf("Disconnecting client %d\n", client_fd);
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "DISC");
    send_to_client(&payload);

    int tries = 0;
    while (!disconnected) {
        if (tries >= 10) {
            printf("Client did not send DISC_ACK\n");
            cleanup = 1;
            return 1;
        }

        usleep(100000);
        tries++;
    }

    cleanup = 1;
    return 0;
}

int Connection::add_topic(char* topic) {
    for (unsigned long i = 0; i < topics->size(); i++) {
        if (strncmp(topics->at(i), topic, TOPIC_SIZE) == 0) {
            return 1;
        }
    }
    topics->push_back(strdup(topic));
    return 0;
}

int Connection::remove_topic(char* topic) {
    for (unsigned long i = 0; i < topics->size(); i++) {
        if (strncmp(topic, topics->at(i), TOPIC_SIZE) == 0) {
            topics->erase(topics->begin() + i);
            return 0;
        }
    }
    return 1;
}

void Connection::listen_loop(Connection* connection) {
    payload_t payload = {0};
    while (!connection->cleanup) {
        int nread = read(connection->get_client_fd(), &payload, PACKET_SIZE);

        if (nread > 0) printf("Read %d bytes from client %d: %s\n", nread, connection->get_client_fd(),  payload.req);
        if (nread != PACKET_SIZE) continue;

        if (strncmp(payload.req, "PUB", REQ_SIZE) == 0) {
            printf("Received PUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->publish_message(&payload, 0);
        }
        if (strncmp(payload.req, "PUBRET", REQ_SIZE) == 0) {
            printf("Received PUBRET from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->publish_message(&payload, 1);
        }
        else if (strncmp(payload.req, "SUB", REQ_SIZE) == 0) {
            printf("Received SUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->subscribe_to_topic(connection->get_client_fd(), payload.topic);
        }
        else if (strncmp(payload.req, "UNSUB", REQ_SIZE) == 0) {
            printf("Received UNSUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->unsubscribe_from_topic(connection->get_client_fd(), payload.topic);
        }
        else if (strncmp(payload.req, "DISC", REQ_SIZE) == 0) {
            printf("Received DISC from client %d, sending DISC_ACK\n", connection->get_client_fd());
            snprintf(payload.req, REQ_SIZE, "DISC_ACK");
            connection->send_to_client(&payload);
            connection->cleanup = 1;
            break;
        }
        else if (strncmp(payload.req, "DISC_ACK", REQ_SIZE) == 0) {
            printf("Received DISC_ACK from client %d\n", connection->get_client_fd());
            connection->disconnected = 1;
            break;
        }
    }
}

Connection::Connection(int client_fd, Server* server) {
    this->client_fd = client_fd;
	this->listen_thread = new std::thread(listen_loop, this);
    this->server = server;
    printf("Connection %d created\n", client_fd);
}

Connection::~Connection() {
    close(client_fd);
    listen_thread->join();
    delete listen_thread;

    printf("Connection %d closed\n", client_fd);
}