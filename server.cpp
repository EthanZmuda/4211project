#include "server.h"

Server* server;
int cleanup = 0;

// catches SIGINT and SIGTERM and sets cleanup flag, initiating a graceful shutdown
void sig_handler(int s){
	printf("Caught signal, cleaning up server\n");
	cleanup = 1;
}

// function which handles the connecting client
// it runs on main thread, so this can only run for one client at a time
int Server::create_connection(int client_fd) {
	if (connections->size() == MAXCLIENTS) { // if we have reached the max number of clients
		printf("reached maximum connections, dropping\n");
		return 1;
	}

	payload_t payload = {0};
	int tries = 0;
	while (read(client_fd, &payload, PACKET_SIZE) != PACKET_SIZE) { // nonblocking read checking for any input
		if (tries >= 10) {
            printf("Client failed to connect\n");
            return 1;
        }
        usleep(100000); // sleep for 100ms, this is the timeout
        tries++;
	}

	if (strncmp(payload.req, "CONN", REQ_SIZE) == 0) { // if the request is CONN, we can connect the client
		printf("Received CONN, sending CONN_ACK\n");
		snprintf(payload.req, REQ_SIZE, "CONN_ACK");
		write(client_fd, &payload, PACKET_SIZE); // send CONN_ACK to client
	}
	else { // client did not send CONN, so we it is probably not compatible, or there was an error
		printf("Client did not send CONN\n");
		return 1;
	}

	Connection* connection = new Connection(client_fd, this);
	connections->insert(std::pair<int, Connection*>(client_fd, connection)); // add new client to connections map

	printf("Number of connections: %ld\n", connections->size());

	return 0;
}

// function which handles a client subscribing to a topic
// this function is only called from within the Connection class, prompted by a SUB request by the client
int Server::subscribe_to_topic(int client_fd, char* topic) {
	printf("Subscribing client %d to topic %s\n", client_fd, topic);

	std::string topic_str(topic);

	std::vector<std::string>* levels = new std::vector<std::string>(); // vector of topic levels
	if (analyze_topic(std::string(topic_str), levels)) { // analyze the topic and put the levels in the vector
		printf("Topic %s is invalid\n", topic);
		delete levels;
		return 1;
	}

	int create = 1;

	if (std::string(topic).find("+") != std::string::npos
		|| std::string(topic).find("#") != std::string::npos) create = 0;							// if the topic levels contains wildcards, we don't want to create any topics

	std::vector<topic_t*>* topic_structs = new std::vector<topic_t*>(); // vector of topic structs
	std::map<std::string, topic_t*>* cur_topics = topics; // start at the root of the server's topic tree
	if (poll_topics(levels, topic_structs, cur_topics, "", create)) { // poll the levels to see if any topics need to be created, and add the lowest-level topics to the topic_structs vector
		printf("Topic %s is invalid\n", topic);
		delete levels;
		delete topic_structs;
		return 1;
	}

	for (auto it : *topic_structs) { // for each topic in the topic_structs vector, add the client to the topic's subscribers
		if (connections->at(client_fd)->add_topic(it->name)) { // if the client is already subscribed to the topic, we don't want to add it again
			printf("Client %d already subscribed to topic %s\n", client_fd, it->name);
			return 1;
		}
		it->connections->push_back(connections->at(client_fd)); // add the client to the topic's subscribers
		if (it->retain) { // if the topic has a retained message, send it to the client
			printf("Sending retained message to client %d for topic %s\n", client_fd, it->name);
			payload_t payload = {0};
			snprintf(payload.req, REQ_SIZE, "PUBRET");
			snprintf(payload.topic, TOPIC_SIZE, "%s", it->name);
			snprintf(payload.msg, MSG_SIZE, "%s", it->retain);
			connections->at(client_fd)->send_to_client(&payload);
		}
	}

	delete levels;
	delete topic_structs;

	return 0;
}

// function which handles a client unsubscribing from a topic
// this function is only called from within the Connection class, prompted by a UNSUB request by the client
int Server::unsubscribe_from_topic(int client_fd, char* topic) {
	printf("Unsubscribing client %d from topic %s\n", client_fd, topic);

	std::string topic_str(topic);

	std::vector<std::string>* levels = new std::vector<std::string>(); // vector of topic levels
	if (analyze_topic(std::string(topic_str), levels)) { // analyze the topic and put the levels in the vector
		printf("Topic %s is invalid\n", topic);
		delete levels;
		return 1;
	}

	std::vector<topic_t*>* topic_structs = new std::vector<topic_t*>(); // vector of topic structs
	std::map<std::string, topic_t*>* cur_topics = topics; // start at the root of the server's topic tree
	if (poll_topics(levels, topic_structs, cur_topics, "", 0)) { // add the lowest-level topics to the topic_structs vector, no need to create any topics
		printf("Topic %s is invalid\n", topic);
		delete levels;
		delete topic_structs;
		return 1;
	}

	for (auto it : *topic_structs) { // for each topic in the topic_structs vector, remove the client from the topic's subscribers
		if (connections->at(client_fd)->remove_topic(it->name)) { // if the client is not subscribed to the topic, we don't want to remove it
			printf("Client %d not subscribed to topic %s\n", client_fd, topic);
			return 1;
		}

		for (unsigned long i = 0; i < it->connections->size(); i++) { // remove the client from the topic's subscribers
			if (it->connections->at(i) == connections->at(client_fd)) {
				it->connections->erase(it->connections->begin() + i);
				break;
			}
		}
	}

	delete levels;
	delete topic_structs;

	return 0;
}

// function which handles a client publishing a message to a topic
// this function is only called from within the Connection class, prompted by a PUB or PUBRET request by the client
// if the client is publishing a retained message, the retain flag will be set to 1
int Server::publish_message(payload_t* payload, int retain) {
	std::vector<std::string>* levels = new std::vector<std::string>(); // vector of topic levels
	if (analyze_topic(std::string(payload->topic), levels)) { // analyze the topic and put the levels in the vector
		printf("Topic %s is invalid\n", payload->topic);
		delete levels;
		return 1;
	}

	int create = 1;
	if (std::string(payload->topic).find("+") != std::string::npos
		|| std::string(payload->topic).find("#") != std::string::npos) create = 0; // if the topic levels contains wildcards, we don't want to create any topics

	std::vector<topic_t*>* topic_structs = new std::vector<topic_t*>(); // vector of topic structs
	std::map<std::string, topic_t*>* cur_topics = topics; // start at the root of the server's topic tree
	if (poll_topics(levels, topic_structs, cur_topics, "", create)) { // poll the levels to see if any topics need to be created, and add the lowest-level topics to the topic_structs vector
		printf("Topic %s is invalid\n", payload->topic);
		delete levels;
		delete topic_structs;
		return 1;
	}

	for (auto it : *topic_structs) { // for each topic in the topic_structs vector, send the message to the topic's subscribers
		unsigned long size = it->connections->size();
		printf("Publishing to %s topic for %ld client", it->name, size);
		if (size != 1) printf("s");
		printf("\n");

		snprintf(payload->topic, TOPIC_SIZE, "%s", it->name);

		for (auto it2 : *(it->connections)) { // send the message to each subscriber
			it2->send_to_client(payload);
		}

		if (retain) { // if the client is publishing a retained message, set the topic's retain field to the message
			printf("Retaining message for topic %s\n", it->name);
			char** cur_retain = &it->retain; // set the topic's retain field to the message
			if (*cur_retain) free(*cur_retain); // if the topic already had a retained message, free it
			*cur_retain = strdup(payload->msg); // set the topic's retain field to the message
		}
	}

	delete levels;
	delete topic_structs;
	return 0;
}

// function which analyzes a topic and puts the levels in a vector
// requires a topic string and a pointer to a vector of strings for the levels
int Server::analyze_topic(std::string topic, std::vector<std::string>* levels) {
	size_t pos = 0;
	std::string token;
	while ((pos = topic.find("/")) != std::string::npos) { // split the topic into levels by the / character
		token = topic.substr(0, pos);
		levels->push_back(token);
		topic.erase(0, pos + 1);
	}

	levels->push_back(topic); // add the last level to the vector

	for (auto it : *levels) { // check if any of the levels are invalid
		if (it.find("+") != std::string::npos && it.size() != 1) return 1; // if the level contains a + character, it must be the only character in the level
		else if (it.find("#") != std::string::npos) { 
			if (it.size() != 1) return 1; // if the level contains a # character, it must be the only character in the level
			else if (it != levels->back()) return 1; // if the level contains a # character, it must be the last level in the topic
		}
	}

	return 0;
}

// function which polls the topic levels to see if any topics need to be created, and adds the lowest-level topics to the topic_structs vector
// requires a pointer to a vector of topic levels, a pointer to a topic structs vetor, a pointer to the temporary topic map, a name which it would iteratively build, and a flag for whether or not to create topics
int Server::poll_topics(std::vector<std::string>* levels, std::vector<topic_t*>* topic_structs, std::map<std::string, topic_t*>* cur_topics, std::string cur_name, int create) {
	if (levels->at(0) == "+") { // if the level is a wildcard, we need to iterate through all of the topics in the current level of the topic map
		for (auto it : *cur_topics) { 
			if (levels->size() == 1) topic_structs->push_back(it.second); // if the level is the last level in the topic, add the topic to the topic_structs vector
			else { // if the level is not the last level in the topic, recursively call poll_topics with the next level
				std::vector<std::string>* temp_levels = new std::vector<std::string>(); // create a temporary vector of topic levels
				for (unsigned long i = 1; i < levels->size(); i++) { // copy the remaining levels into the temporary vector
					temp_levels->push_back(levels->at(i));
				}
				poll_topics(temp_levels, topic_structs, it.second->subtopics, cur_name + it.first + "/", create); // recursively call poll_topics with the next level
				delete temp_levels;
			}
		}
	}
	else if (levels->at(0) == "#") { // if the level is a wildcard, we need to iterate through all of the topics in the rest of the levels of the topic map
		for (auto it : *cur_topics) {
			topic_structs->push_back(it.second); // add the topic to the topic_structs vector
			poll_topics(levels, topic_structs, it.second->subtopics, cur_name + it.first + "/", create); // recursively call poll_topics with the next subtopics, keeping the wildcard as the last level
		}
	}
	else if (levels->size() > 1) { // if the level is not a wildcard, and there are more levels, recursively call poll_topics with the next level
		if (cur_topics->find(levels->at(0)) == cur_topics->end()) {
			if (create) create_topic(cur_topics, levels->at(0), cur_name + levels->at(0)); // if the topic does not exist, create it if the create flag is set
			else return 1; // else, return as it does not exist and we cannot create it
		}
		cur_topics = cur_topics->at(levels->at(0))->subtopics; // set the current topic map to the next level of the topic map
		std::string new_name = cur_name + levels->at(0) + "/";
		levels->erase(levels->begin());
		poll_topics(levels, topic_structs, cur_topics, new_name, create); // recursively call poll_topics with the next level
	}
	else { // if the level is not a wildcard, and there are no more levels, add the topic to the topic_structs vector
		if (cur_topics->find(levels->at(0)) == cur_topics->end()) {
			if (create) create_topic(cur_topics, levels->at(0), cur_name + levels->at(0)); // if the topic does not exist, create it if the create flag is set
			else return 1; // else, return as it does not exist and we cannot create it
		}
		topic_structs->push_back(cur_topics->at(levels->at(0))); // add the topic to the topic_structs vector
	}
	
	return 0;
}

// function which creates a topic if one is required
// requires a pointer to the current topic map, the name of the topic to be used as the key, and the full leveled name of the topic to be stored for the client
int Server::create_topic(std::map<std::string, topic_t*>* cur_topics, std::string topic, std::string name) {
	printf("Creating topic %s\n", name.c_str());
	topic_t* topic_struct = new topic_t;
	topic_struct->name = strdup(name.c_str());
	topic_struct->connections = new std::vector<Connection*>();
	topic_struct->subtopics = new std::map<std::string, topic_t*>();
	(*cur_topics)[topic] = topic_struct;
	return 0;
}

// function which recursively frees and deletes elements of a topic map
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

// the loop which always runs (nonblocking) to check for new connections
// this is run in a separate thread
void Server::accept_loop(Server* server) {
	printf("server: waiting for connections...\n");

	struct sockaddr_in client_addr;
	int client_fd;
	while(!cleanup) { // while the server is not being cleaned up
		socklen_t clientsize = sizeof client_addr;
		client_fd = accept(server->get_server_fd(), (struct sockaddr*) &client_addr, &clientsize);
		if (client_fd == -1) continue; // if the accept failed, continue. this happens most of the time as the socket is set to nonblocking

		fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK); // set the client socket to nonblocking

		printf("Connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		if (server->create_connection(client_fd)) { // if the connection object could not be created, close the socket
			char buf[PACKET_SIZE] = {};
			snprintf(buf, PACKET_SIZE, "Cannot join server, please try again later\n");
			write(client_fd, buf, PACKET_SIZE);
			close(client_fd);
		};
	}
}

Server::Server(int server_fd) {
	this->accept_thread = new std::thread(accept_loop, this); // create the accept thread
	this->server_fd = server_fd;
}

Server::~Server() {
	accept_thread->join(); // wait for the accept thread to finish
	delete accept_thread;
	if (connections->size() > 0) { // if there are still connections, close them
		for (auto it = connections->begin(); it != connections->end();){
			Connection* conn = it->second;
			if (!conn->get_cleanup()) conn->disconnect_client(); // if the connection is not already being cleaned up, disconnect the client
			printf("Client %d disconnected\n", conn->get_client_fd());
			delete conn;
			it = connections->erase(it);
		}
	}

	free_topics(topics);

	delete connections;
}

// main function, which runs main server loop
int main(int argc, char* argv[]) {
	
    const char* port = "42069"; // default port
    if(argc >= 2){ // if a port is specified, use that
        port = argv[1];
    }
	
	// set up server to catch SIGINT and SIGTERM signals
	struct sigaction my_sa = {};
	sigemptyset(&my_sa.sa_mask);
	my_sa.sa_handler = sig_handler;
	sigaction(SIGINT, &my_sa, NULL);
	sigaction(SIGTERM, &my_sa, NULL);

	int server_fd = socket(AF_INET, SOCK_STREAM, 0); // create the server socket
	if (server_fd == -1) return 1;

	fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK); // set the server socket to nonblocking

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(port));
	server_addr.sin_addr.s_addr = INADDR_ANY;

	int yes = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)); // set the socket to reuse the address (so we can restart the server without waiting for the port to be freed)

	if (bind(server_fd, (struct sockaddr*) &server_addr, sizeof(server_addr))) { // bind the socket to the address
		printf("failed to bind\n");
		close(server_fd);
		return 1;
	}

	if (listen(server_fd, BACKLOG)) { // listen for connections with a backlog set by the BACKLOG constant
		printf("failed to listen\n");
		return 1;
	}

	server = new Server(server_fd); // create the server object
	
	while(!cleanup) { // while the server is not being cleaned up, this is the main loop which acts as a garbage collector for connections
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