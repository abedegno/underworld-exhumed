/* Host-rendered shared-world transport. The client sends input, never game state. */
#ifndef UW_SHARED_H
#define UW_SHARED_H
#include <stdint.h>
#include <stddef.h>
#define SHARED_PIXELS (320 * 200)
#define SHARED_FRAME (SHARED_PIXELS + 768 + 8)
/* Picture prefix: BE width/height; mouse-look, companion-present and shared-scene
   booleans; reserved zero; then RGB palette and indexed pixels. */
#define SHARED_INPUT 272
#define SHARED_SOUND 4100
#define SHARED_HEADER 16
#define SHARED_VERSION 3
#define SHARED_MAX SHARED_FRAME
enum shared_message { SHARED_HELLO=1, SHARED_WELCOME, SHARED_PICTURE, SHARED_KEYS, SHARED_PING, SHARED_AUDIO };
#pragma pack(push, 8)
struct shared_net {
    intptr_t listener, socket;
    int role, game, connecting, ready, started, error;
    uint32_t address, sequence, received_sequence;
    uint16_t port;
    uint64_t retry, last_receive, last_send;
    unsigned char rx[SHARED_HEADER + SHARED_MAX], tx[SHARED_HEADER + SHARED_MAX];
    size_t received, expected, sent, queued;
};
#pragma pack(pop)
int shared_open(struct shared_net *net, int role, int game, const char *endpoint);
void shared_close(struct shared_net *net);
void shared_tick(struct shared_net *net, uint64_t now,
                 void (*message)(int, const unsigned char *, size_t, void *), void *context);
int shared_send(struct shared_net *net, int type, const unsigned char *data, size_t length);
int shared_header(unsigned char out[SHARED_HEADER], int type, int game, uint32_t seq, size_t length);
int shared_check(const unsigned char header[SHARED_HEADER], int game, int role, int ready, size_t *length);
#endif
