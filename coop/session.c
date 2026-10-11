/* TCP provides ordered frames and reliable handshakes; position updates are bounded to
   one pending frame. This is presence only, not shared-world simulation. */
#include "coop.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#if defined(__EMSCRIPTEN__)
/* Browser sockets need a relay/WebSocket transport, which is not implemented yet. */
int coop_endpoint(enum coop_role role, const char *e, uint32_t *a, uint16_t *p)
{ (void)role; (void)e; (void)a; (void)p; return -1; }
int coop_open(struct coop_session *s, enum coop_role role, uint8_t game, const char *e)
{ (void)role; (void)game; (void)e; memset(s, 0, sizeof *s); s->status = COOP_ERROR;
  snprintf(s->error, sizeof s->error, "co-op presence requires a native build"); return -1; }
void coop_tick(struct coop_session *s, const struct coop_pose *p, uint64_t n) { (void)s; (void)p; (void)n; }
void coop_close(struct coop_session *s) { (void)s; }
#else
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET coop_socket;
#define CLOSE closesocket
static int would_block(void) { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static int nonblock(coop_socket fd) { u_long on = 1; return ioctlsocket(fd, FIONBIO, &on); }
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
typedef int coop_socket;
#define CLOSE close
static int would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
static int nonblock(coop_socket fd) { int f = fcntl(fd, F_GETFL, 0); return f < 0 ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK); }
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int coop_endpoint(enum coop_role role, const char *e, uint32_t *address, uint16_t *port)
{
    const char *digits = e, *sep;
    unsigned long value = 0;
    char ip[16];
    struct in_addr a;
    if (!e || !*e || (role != COOP_HOST && role != COOP_JOIN)) return -1;
    *address = htonl(INADDR_ANY);
    if (role == COOP_JOIN) {
        sep = strchr(e, ':');
        if (!sep || sep == e || (size_t)(sep - e) >= sizeof ip) return -1;
        memcpy(ip, e, (size_t)(sep - e)); ip[sep - e] = 0;
        if (inet_pton(AF_INET, ip, &a) != 1 || a.s_addr == htonl(INADDR_ANY) || a.s_addr == htonl(INADDR_BROADCAST)) return -1;
        *address = a.s_addr; digits = sep + 1;
    }
    if (!*digits) return -1;
    for (; *digits; digits++) {
        if (*digits < '0' || *digits > '9') return -1;
        value = value * 10 + (unsigned)(*digits - '0');
        if (value > 65535) return -1;
    }
    if (!value) return -1;
    *port = (uint16_t)value;
    return 0;
}

static void drop(struct coop_session *s, uint64_t now)
{
    if (s->peer != -1) CLOSE((coop_socket)s->peer);
    s->peer = -1; s->connecting = 0; s->welcomed = 0; s->have_remote = 0;
    s->received = s->sent = s->queued = 0; s->sequence = s->remote_sequence = 0;
    memset(&s->remote, 0, sizeof s->remote);
    s->retry_at = now + COOP_RETRY_MS;
    if (s->status != COOP_REJECTED && s->status != COOP_ERROR)
        s->status = s->role == COOP_HOST ? COOP_WAITING : COOP_CONNECTING;
}

void coop_close(struct coop_session *s)
{
    drop(s, 0);
    if (s->listener != -1) CLOSE((coop_socket)s->listener);
#ifdef _WIN32
    if (s->sockets_started) WSACleanup();
#endif
    s->listener = -1; s->sockets_started = 0; s->role = COOP_OFF; s->status = COOP_DISABLED;
}

static int configure(coop_socket fd)
{
    int on = 1;
#ifndef _WIN32
    if (fd >= FD_SETSIZE) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC)) return -1;
#endif
    if (nonblock(fd) != 0) return -1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    return 0;
}

int coop_open(struct coop_session *s, enum coop_role role, uint8_t game, const char *endpoint)
{
    struct sockaddr_in addr;
    coop_socket fd;
    memset(s, 0, sizeof *s); s->listener = s->peer = -1; s->role = role; s->game = game;
    if (role == COOP_OFF) return 0;
    if ((game != 1 && game != 2) || coop_endpoint(role, endpoint, &s->address, &s->port)) {
        snprintf(s->error, sizeof s->error, "use --coop-host PORT or --coop-join IPV4:PORT (port 1..65535)");
        s->status = COOP_ERROR; return -1;
    }
#ifdef _WIN32
    { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) {
        snprintf(s->error, sizeof s->error, "cannot initialise Winsock"); s->status = COOP_ERROR; return -1;
    } s->sockets_started = 1; }
#endif
    s->status = role == COOP_HOST ? COOP_WAITING : COOP_CONNECTING;
    if (role != COOP_HOST) return 0;
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == (coop_socket)-1) goto failed;
    s->listener = (intptr_t)fd;
    if (configure(fd)) goto failed;
    {
        int on = 1;
#ifdef _WIN32
        setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&on, sizeof on);
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
    }
    memset(&addr, 0, sizeof addr); addr.sin_family = AF_INET; addr.sin_port = htons(s->port); addr.sin_addr.s_addr = s->address;
    /* No SO_REUSEPORT: a second host must fail rather than split the party. */
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 4)) goto failed;
    return 0;
failed:
    snprintf(s->error, sizeof s->error, "cannot host co-op on port %u (socket/bind/listen failed)", s->port);
    coop_close(s); s->status = COOP_ERROR; return -1;
}

static void queue(struct coop_session *s, uint8_t type, const struct coop_pose *local, uint64_t now)
{
    struct coop_packet p;
    memset(&p, 0, sizeof p); p.type = type; p.game = s->game; p.reason = s->reason;
    if (type == COOP_STATE) { p.sequence = ++s->sequence; p.pose = *local; }
    if (coop_encode(s->outgoing, &p)) return;
    s->queued = COOP_PACKET_SIZE; s->sent = 0; s->write_since = now; s->last_send = now;
}

static int flush(struct coop_session *s, uint64_t now)
{
    int n;
    if (!s->queued) return 0;
    n = (int)send((coop_socket)s->peer, (const char *)s->outgoing + s->sent, (int)(s->queued - s->sent), MSG_NOSIGNAL);
    if (n < 0 && would_block()) return 0;
    if (n <= 0) { drop(s, now); return -1; }
    s->sent += (size_t)n;
    if (s->sent == s->queued) { s->sent = s->queued = 0;
        if (s->status == COOP_REJECTED) { drop(s, now); return -1; }
    }
    return 0;
}

static void start_peer(struct coop_session *s, coop_socket fd, uint64_t now)
{
    s->peer = (intptr_t)fd; s->status = COOP_HANDSHAKE; s->last_receive = now; s->last_send = now;
    s->received = s->sent = s->queued = 0;
}

static void dial(struct coop_session *s, uint64_t now)
{
    struct sockaddr_in addr;
    coop_socket fd;
    int rc;
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == (coop_socket)-1) { s->retry_at = now + COOP_RETRY_MS; return; }
    if (configure(fd)) { CLOSE(fd); s->retry_at = now + COOP_RETRY_MS; return; }
    memset(&addr, 0, sizeof addr); addr.sin_family = AF_INET; addr.sin_port = htons(s->port); addr.sin_addr.s_addr = s->address;
    rc = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (rc && !would_block()) { CLOSE(fd); s->retry_at = now + COOP_RETRY_MS; return; }
    start_peer(s, fd, now); s->connecting = rc != 0;
    if (!s->connecting) queue(s, COOP_HELLO, NULL, now);
}

static int finish_dial(struct coop_session *s, uint64_t now)
{
    fd_set writefds, errorfds;
    struct timeval timeout = {0, 0};
    int e = 0, rc;
#ifdef _WIN32
    int size = sizeof e;
#else
    socklen_t size = sizeof e;
#endif
    FD_ZERO(&writefds); FD_ZERO(&errorfds);
    FD_SET((coop_socket)s->peer, &writefds); FD_SET((coop_socket)s->peer, &errorfds);
    rc = select((int)s->peer + 1, NULL, &writefds, &errorfds, &timeout);
    if (rc < 0 || (rc && (getsockopt((coop_socket)s->peer, SOL_SOCKET, SO_ERROR, (char *)&e, &size) || e))) {
        drop(s, now); return -1;
    }
    if (!rc) return 0;
    s->connecting = 0; queue(s, COOP_HELLO, NULL, now); return 1;
}

static int receive_packet(struct coop_session *s, uint64_t now)
{
    struct coop_packet p;
    int rc = coop_decode(&p, s->incoming, COOP_PACKET_SIZE, s->game);
    if (rc > 0 && s->role == COOP_HOST && !s->welcomed && s->incoming[5] == COOP_HELLO) {
        s->reason = (uint8_t)rc; s->status = COOP_REJECTED; queue(s, COOP_REJECT, NULL, now); return 0;
    }
    if (rc) { drop(s, now); return -1; }
    if (!s->welcomed) {
        if (s->role == COOP_HOST && p.type == COOP_HELLO) {
            queue(s, COOP_WELCOME, NULL, now); s->welcomed = 1; s->status = COOP_CONNECTED;
        } else if (s->role == COOP_JOIN && p.type == COOP_WELCOME) {
            s->welcomed = 1; s->status = COOP_CONNECTED;
        } else if (s->role == COOP_JOIN && p.type == COOP_REJECT) {
            s->reason = p.reason; s->status = COOP_REJECTED; drop(s, now); return -1;
        } else { drop(s, now); return -1; }
    } else {
        if (p.type != COOP_STATE || (s->have_remote && p.sequence != s->remote_sequence + 1)) { drop(s, now); return -1; }
        s->remote = p.pose; s->remote_sequence = p.sequence; s->have_remote = 1;
    }
    s->last_receive = now;
    return 0;
}

void coop_tick(struct coop_session *s, const struct coop_pose *local, uint64_t now)
{
    int i;
    if (s->role == COOP_OFF || s->status == COOP_ERROR || (s->status == COOP_REJECTED && s->role == COOP_JOIN)) return;
    /* A rejection on the host closes only that guest and leaves the listener usable. */
    if (s->role == COOP_HOST) {
        for (i = 0; i < 4; i++) {
            coop_socket fd = accept((coop_socket)s->listener, NULL, NULL);
            if (fd == (coop_socket)-1) break;
            if (s->peer != -1 || configure(fd)) { CLOSE(fd); continue; }
            s->reason = 0; start_peer(s, fd, now);
        }
    } else if (s->peer == -1 && now >= s->retry_at) dial(s, now);
    if (s->peer == -1) { if (s->role == COOP_HOST) s->status = COOP_WAITING; return; }
    if (now - s->last_receive >= COOP_TIMEOUT_MS || (s->queued && now - s->write_since >= COOP_TIMEOUT_MS)) {
        if (s->role == COOP_HOST) s->status = COOP_WAITING;
        drop(s, now); return;
    }
    if (s->connecting && finish_dial(s, now) <= 0) return;
    if (flush(s, now)) return;
    if (s->status == COOP_REJECTED) return;
    /* Fixed receive budget avoids starvation by a noisy or malicious peer. */
    for (i = 0; i < 16; i++) {
        int n = (int)recv((coop_socket)s->peer, (char *)s->incoming + s->received, (int)(COOP_PACKET_SIZE - s->received), 0);
        if (n < 0 && would_block()) break;
        if (n <= 0) { drop(s, now); return; }
        s->received += (size_t)n;
        if (s->received == COOP_PACKET_SIZE) {
            s->received = 0;
            if (receive_packet(s, now)) return;
            if (s->queued) break;  /* send the welcome/rejection before reading any state */
        }
    }
    if (s->welcomed && !s->queued && now - s->last_send >= COOP_SEND_MS) queue(s, COOP_STATE, local, now);
    flush(s, now);
}
#endif

const char *coop_status_text(enum coop_status status)
{
    switch (status) {
    case COOP_DISABLED: return "OFF"; case COOP_WAITING: return "WAITING FOR PARTNER";
    case COOP_CONNECTING: return "CONNECTING"; case COOP_HANDSHAKE: return "HANDSHAKE";
    case COOP_CONNECTED: return "CONNECTED"; case COOP_REJECTED: return "INCOMPATIBLE SESSION";
    default: return "ERROR";
    }
}
