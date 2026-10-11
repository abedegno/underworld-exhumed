/* Native two-player presence transport. No game objects or save data cross this API. */
#ifndef UW_COOP_H
#define UW_COOP_H
#include <stddef.h>
#include <stdint.h>

#define COOP_PACKET_SIZE 32
#define COOP_PROTOCOL_VERSION 1
#define COOP_SEND_MS 50
#define COOP_TIMEOUT_MS 10000
#define COOP_RETRY_MS 1000

enum coop_role { COOP_OFF, COOP_HOST, COOP_JOIN };
enum coop_status { COOP_DISABLED, COOP_WAITING, COOP_CONNECTING, COOP_HANDSHAKE,
                   COOP_CONNECTED, COOP_REJECTED, COOP_ERROR };
enum coop_message { COOP_HELLO = 1, COOP_WELCOME, COOP_STATE, COOP_REJECT };
enum coop_rejection { COOP_WRONG_VERSION = 1, COOP_WRONG_GAME, COOP_FULL };

/* Game headers use pack(1) to match DOS files. Restore a fixed native ABI for
   this interface whether it is included before or after compat.h. */
#pragma pack(push, 8)
struct coop_pose {
    uint16_t level, x, y;       /* level is 1-based; x/y are 1/256 tiles in a 64x64 grid */
    int16_t z;
    uint16_t facing;            /* one full turn is 65536 */
    uint8_t health, max_health, active;
};
struct coop_packet {
    uint8_t type, game, reason;
    uint32_t sequence;
    struct coop_pose pose;
};
struct coop_session {
    enum coop_role role;
    enum coop_status status;
    uint8_t game, reason;
    uint32_t address;           /* network byte order */
    uint16_t port;
    intptr_t listener, peer;
    int sockets_started, connecting, welcomed, have_remote;
    uint64_t last_receive, last_send, retry_at, write_since;
    uint32_t sequence, remote_sequence;
    uint8_t incoming[COOP_PACKET_SIZE], outgoing[COOP_PACKET_SIZE];
    size_t received, sent, queued;
    struct coop_pose remote;
    char error[160];
};
#pragma pack(pop)

int coop_encode(uint8_t out[COOP_PACKET_SIZE], const struct coop_packet *packet);
/* Returns a rejection reason for a valid header with incompatible version/game,
   -1 for malformed data, or 0 on success. Expected game is 1 or 2. */
int coop_decode(struct coop_packet *packet, const uint8_t *data, size_t size, uint8_t game);
/* HOST endpoint is a port; JOIN endpoint is a numeric IPv4 address followed by :port. */
int coop_endpoint(enum coop_role role, const char *endpoint, uint32_t *address, uint16_t *port);
int coop_open(struct coop_session *s, enum coop_role role, uint8_t game, const char *endpoint);
/* Single game thread only. Work is bounded and sockets never block. now is monotonic ms. */
void coop_tick(struct coop_session *s, const struct coop_pose *local, uint64_t now);
void coop_close(struct coop_session *s);
const char *coop_status_text(enum coop_status status);
#endif
