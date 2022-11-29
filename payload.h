#ifndef PAYLOAD_H
#define PAYLOAD_H

#define PACKET_SIZE 1024
#define REQ_SIZE 128
#define TOPIC_SIZE 128
#define MSG_SIZE 768      // 1024 - 128 - 128

// This is the payload that will be sent between the client and server
typedef struct {
    char req[128]; // The request type
    char topic[128]; // The topic
    char msg[768]; // The message
} payload_t;

#endif