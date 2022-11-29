#include "connection.h"

// function to disconnect client
int Connection::disconnect_client() {
    printf("Disconnecting client %d\n", client_fd);
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "DISC");
    send_to_client(&payload); // send disconnect request to client

    int tries = 0;
    while (!disconnected) { // wait for client to return DISC_ACK, which will set disconnected to 1
        if (tries >= 10) {
            printf("Client did not send DISC_ACK\n");
            cleanup = 1; // if client does not respond, set cleanup to 1 anyway and force shut down
            return 1;
        }

        usleep(100000); // sleep for 100ms, this is the timeout
        tries++;
    }

    cleanup = 1; // set cleanup to 1, which will cause the listen thread to exit
    return 0;
}

// function to add topic to client's subscription list
int Connection::add_topic(char* topic) {
    for (unsigned long i = 0; i < topics->size(); i++) {
        if (strncmp(topics->at(i), topic, TOPIC_SIZE) == 0) {
            return 1;
        }
    }
    topics->push_back(strdup(topic));
    return 0;
}

// function to remove topic from client's subscription list
int Connection::remove_topic(char* topic) {
    for (unsigned long i = 0; i < topics->size(); i++) {
        if (strncmp(topic, topics->at(i), TOPIC_SIZE) == 0) {
            topics->erase(topics->begin() + i);
            return 0;
        }
    }
    return 1;
}

// function to list topics in client's subscription list
int Connection::list_topics() {
    payload_t payload = {0};
    snprintf(payload.req, REQ_SIZE, "LIST");
    snprintf(payload.msg, MSG_SIZE, "%s", topics->at(0));
    for (unsigned long i = 1; i < topics->size(); i++) {
        char* temp_msg = strdup(payload.msg);
        snprintf(payload.msg, MSG_SIZE, "%s, %s", temp_msg, topics->at(i));
        free(temp_msg);
    }
    send_to_client(&payload);
    return 0;
}

// function to listen for messages to client
// this is run in a separate thread with no hangup
void Connection::listen_loop(Connection* connection) {
    payload_t payload = {0};
    while (!connection->cleanup) { // while cleanup is not set, keep listening for messages
        int nread = read(connection->get_client_fd(), &payload, PACKET_SIZE); // read message from client, nonblocking

        if (nread > 0) printf("Read %d bytes from client %d: %s\n", nread, connection->get_client_fd(),  payload.req);
        if (nread != PACKET_SIZE) continue; // if message is not the correct size, ignore it

        if (strncmp(payload.req, "PUB", REQ_SIZE) == 0) { // if message is a PUB, send it to all clients subscribed to the topic
            printf("Received PUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->publish_message(&payload, 0);
        }
        if (strncmp(payload.req, "PUBRET", REQ_SIZE) == 0) { // if message is a PUBRET, send it to all clients subscribed to the topic and retain the message
            printf("Received PUBRET from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->publish_message(&payload, 1);
        }
        else if (strncmp(payload.req, "SUB", REQ_SIZE) == 0) { // if message is a SUB, add the topic to the client's subscription list
            printf("Received SUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->subscribe_to_topic(connection->get_client_fd(), payload.topic);
        }
        else if (strncmp(payload.req, "UNSUB", REQ_SIZE) == 0) { // if message is a UNSUB, remove the topic from the client's subscription list
            printf("Received UNSUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->get_server()->unsubscribe_from_topic(connection->get_client_fd(), payload.topic);
        }
        else if (strncmp(payload.req, "LIST", REQ_SIZE) == 0) { // if message is a LIST, send the client a list of all topics they are subscribed to
            printf("Received SUB from client %d to topic %s, processing\n", connection->get_client_fd(), payload.topic);
            connection->list_topics();
        }
        else if (strncmp(payload.req, "DISC", REQ_SIZE) == 0) { // if message is a DISC, send the client a DISC_ACK and set cleanup to 1, ending the listening thread's loop
            printf("Received DISC from client %d, sending DISC_ACK\n", connection->get_client_fd());
            snprintf(payload.req, REQ_SIZE, "DISC_ACK");
            connection->send_to_client(&payload);
            connection->cleanup = 1;
            break;
        }
        else if (strncmp(payload.req, "DISC_ACK", REQ_SIZE) == 0) { // if message is a DISC_ACK, set disconnected to 1, ending the disconnect_client() loop
            printf("Received DISC_ACK from client %d\n", connection->get_client_fd());
            connection->disconnected = 1;
            break;
        }
        else { // something went horribly wrong
            printf("Received unknown request from client %d: %s\n", connection->get_client_fd(), payload.req);
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