#include "client.h"

int main(int argc, char** argv) {

    const char *hostname = "76.17.252.142";
    if(argc >= 2){
        hostname = argv[1];
    }

    printf("Host: %s\n", hostname);

    int ret;
    struct addrinfo* serv_addr;                        // filled by getaddrinfo
    ret = getaddrinfo(hostname, PORT, NULL, &serv_addr);
    assert(ret == 0);

    printf("Got addrinfo\n");

    int sockfd = socket(serv_addr->ai_family,          // create a socket with the appropriate params
                        serv_addr->ai_socktype,        // to the server
                        serv_addr->ai_protocol);

    ret = connect(sockfd,                              // connect the socket to the server so that 
                  serv_addr->ai_addr,                  // writes will send over the network
                  serv_addr->ai_addrlen);
    assert(ret != -1);

    printf("Connected\n");

    char* buf = (char*) malloc(256*sizeof(char));
    read(sockfd, buf, 256);
    printf("%s\n", buf);


    freeaddrinfo(serv_addr);
    close(sockfd);

    return 0;
}