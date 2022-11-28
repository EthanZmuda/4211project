#ifndef PAYLOAD_H
#define PAYLOAD_H

#define PACKET_SIZE 1024
#define REQ_SIZE 128
#define TOPIC_SIZE 128
#define MSG_SIZE 768

typedef struct {
    char req[128];
    char topic[128];
    char msg[768];
} payload_t;

#endif