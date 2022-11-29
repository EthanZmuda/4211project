#include "server.h"

Server* server;
int cleanup = 0;

void sig_handler(int s){
	printf("Caught signal, cleaning up server\n");
	cleanup = 1;
}

int Server::create_connection(int client_fd) {
	if (connections->size() == MAXCLIENTS) {
		printf("reached maximum connections, dropping\n");
		return 1;
	}

	payload_t payload = {0};
	int tries = 0;
	while (read(client_fd, &payload, PACKET_SIZE) != PACKET_SIZE) {
		if (tries >= 10) {
            printf("Client failed to connect\n");
            return 1;
        }
        usleep(100000);
        tries++;
	}

	if (strncmp(payload.req, "CONN", REQ_SIZE) == 0) {
		printf("Received CONN, sending CONN_ACK\n");
		snprintf(payload.req, REQ_SIZE, "CONN_ACK");
		write(client_fd, &payload, PACKET_SIZE);
	}
	else {
		printf("Client did not send CONN\n");
		return 1;
	}

	Connection* connection = new Connection(client_fd, this);
	connections->insert(std::pair<int, Connection*>(client_fd, connection));

	printf("Number of connections: %ld\n", connections->size());

	return 0;
}

int Server::publish_message(payload_t* payload, int retain) {
	poll_topics(std::string(payload->topic));

	unsigned long size = topics->at(std::string(payload->topic))->connections->size();
	printf("Publishing to %s topic for %ld client", payload->topic, size);
	if (size != 1) printf("s");
	printf("\n");

	for (auto it2 : *(topics->at(std::string(payload->topic))->connections)) {
		it2->send_to_client(payload);
	}

	if (retain) {
		printf("Retaining message for topic %s\n", payload->topic);
		char** cur_retain = &topics->at(std::string(payload->topic))->retain;
		if (*cur_retain) free(*cur_retain);
		*cur_retain = strdup(payload->msg);
	}

	return 0;
}

int Server::subscribe_to_topic(int client_fd, char* topic) {
	if (connections->at(client_fd)->add_topic(topic)) {
        printf("Client %d already subscribed to topic %s\n", client_fd, topic);
		return 1;
	}

	printf("Subscribing client %d to topic %s\n", client_fd, topic);

	std::string topic_str(topic);
	poll_topics(topic_str);
	topic_t* topic_struct = topics->at(topic_str);
	topic_struct->connections->push_back(connections->at(client_fd));

	if (topic_struct->retain) {
		printf("Sending retained message to client %d for topic %s\n", client_fd, topic);
		payload_t payload = {0};
		snprintf(payload.req, REQ_SIZE, "PUBRET");
		snprintf(payload.topic, TOPIC_SIZE, "%s", topic);
		snprintf(payload.msg, MSG_SIZE, "%s", topic_struct->retain);
		connections->at(client_fd)->send_to_client(&payload);
	}

	return 0;
}

int Server::unsubscribe_from_topic(int client_fd, char* topic) {
	if (connections->at(client_fd)->remove_topic(topic)) {
    	printf("Client %d not subscribed to topic %s\n", client_fd, topic);
		return 1;
	}
	std::string topic_str(topic);
	topic_t* topic_struct = topics->at(topic_str);
	
	for (unsigned long i = 0; i < topic_struct->connections->size(); i++) {
        if (topic_struct->connections->at(i) == connections->at(client_fd)) {
            topic_struct->connections->erase(topic_struct->connections->begin() + i);
        }
    }
	return 0;
}

int Server::poll_topics(std::string topic) {
	topic_t* topic_struct;
	if(topics->find(topic) == topics->end()){
		topic_struct = new topic_t;
		topic_struct->name = strdup(topic.c_str());
		topic_struct->connections = new std::vector<Connection*>();
		topic_struct->subtopics = new std::map<std::string, topic_t*>();
		(*topics)[topic] = topic_struct;

		printf("Created new topic: %s\n", topic.c_str());
	}
	return 0;
}

int Server::free_topics(std::map<std::string, topic_t*>* topics) {
	for (auto it = topics->begin(); it != topics->end(); it++) {
		free_topics(it->second->subtopics);
		free(it->second->name);
		delete it->second->connections;
		delete it->second;
	}
	delete topics;
	return 0;
}

void Server::accept_loop(Server* server) {
	printf("server: waiting for connections...\n");

	struct sockaddr_in client_addr;
	int client_fd;
	while(!cleanup) {
		socklen_t clientsize = sizeof client_addr;
		client_fd = accept(server->get_server_fd(), (struct sockaddr*) &client_addr, &clientsize);
		if (client_fd == -1) continue;

		fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK);

		printf("Connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		if (server->create_connection(client_fd)) {
			char buf[PACKET_SIZE] = {};
			snprintf(buf, PACKET_SIZE, "Cannot join server, please try again later\n");
			write(client_fd, buf, PACKET_SIZE);
			close(client_fd);
		};
	}
}

Server::Server(int server_fd) {
	this->accept_thread = new std::thread(accept_loop, this);
	this->server_fd = server_fd;
}

Server::~Server() {
	accept_thread->join();
	delete accept_thread;
	if (connections->size() > 0) {
		for (auto it = connections->begin(); it != connections->end();){
			Connection* conn = it->second;
			if (!conn->get_cleanup()) conn->disconnect_client();
			printf("Client %d disconnected\n", conn->get_client_fd());
			delete conn;
			it = connections->erase(it);
		}
	}

	free_topics(topics);

	delete connections;
}

int main(int argc, char* argv[]) {
	
    const char* port = "42069";
    if(argc >= 2){
        port = argv[1];
    }
	
	struct sigaction my_sa = {};
	sigemptyset(&my_sa.sa_mask);
	my_sa.sa_handler = sig_handler;
	sigaction(SIGINT, &my_sa, NULL);
	sigaction(SIGTERM, &my_sa, NULL);

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) return 1;

	fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(port));
	server_addr.sin_addr.s_addr = INADDR_ANY;

	int yes = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

	if (bind(server_fd, (struct sockaddr*) &server_addr, sizeof(server_addr))) {
		printf("failed to bind\n");
		close(server_fd);
		return 1;
	}

	if (listen(server_fd, BACKLOG)) {
		printf("failed to listen\n");
		return 1;
	}

	server = new Server(server_fd);
	
	while(!cleanup) {
		for (auto it = server->get_connections()->begin(); it != server->get_connections()->end();) {
			int fd = it->first;
			Connection* conn = it->second;
			if (conn->get_cleanup()) {
				printf("Client %d disconnected\n", fd);
				delete conn;
				it = server->get_connections()->erase(it);
				printf("New number of connections: %ld\n", server->get_connections()->size());
			}
			else it++;
		}
	}

	delete server;

	return 0;
}