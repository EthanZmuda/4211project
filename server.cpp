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

int Server::publish_message(payload_t* payload) {
	if (strncmp(payload->topic, "WEATHER", TOPIC_SIZE) == 0) {
		printf("Publishing to WEATHER topic for %ld client", weather->size());
		if (weather->size() != 1) printf("s");
		printf("\n");

		for (Connection* connection : *weather) {
			connection->send_to_client(payload);
		}
	}
	else if (strncmp(payload->topic, "NEWS", TOPIC_SIZE) == 0) {
		printf("Publishing to NEWS topic for %ld client", news->size());
		if (news->size() != 1) printf("s");
		printf("\n");

		for (Connection* connection : *news) {
			connection->send_to_client(payload);
		}
	}
	else if (strncmp(payload->topic, "HEALTH", TOPIC_SIZE) == 0) {
		printf("Publishing to HEALTH topic for %ld client", health->size());
		if (health->size() != 1) printf("s");
		printf("\n");

		for (Connection* connection : *health) {
			connection->send_to_client(payload);
		}
	}
	else if (strncmp(payload->topic, "SECURITY", TOPIC_SIZE) == 0) {
		printf("Publishing to SECURITY topic for %ld client", security->size());
		if (security->size() != 1) printf("s");
		printf("\n");

		for (Connection* connection : *security) {
			connection->send_to_client(payload);
		}
	}
	else {
		printf("Invalid topic\n");
		return 1;
	}
	return 0;
}

int Server::subscribe_to_topic(int client_fd, char* topic) {
	if (strncmp(topic, "WEATHER", TOPIC_SIZE) == 0) {
		weather->push_back(connections->at(client_fd));
	}
	else if (strncmp(topic, "NEWS", TOPIC_SIZE) == 0) {
		news->push_back(connections->at(client_fd));
	}
	else if (strncmp(topic, "HEALTH", TOPIC_SIZE) == 0) {
		health->push_back(connections->at(client_fd));
	}
	else if (strncmp(topic, "SECURITY", TOPIC_SIZE) == 0) {
		security->push_back(connections->at(client_fd));
	}
	else {
		printf("Invalid topic\n");
		return 1;
	}
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
		for (auto it = server->get_connections()->begin(); it != server->get_connections()->end();){
			int fd = it->first;
			Connection* conn = it->second;
			if (conn->get_cleanup()) {
				printf("Client %d disconnected\n", fd);
				delete conn;
				it = server->get_connections()->erase(it);
				printf("New number of connections: %ld\n", server->get_connections()->size());
			}
			else ++it;
		}
	}

	delete server;

	return 0;
}