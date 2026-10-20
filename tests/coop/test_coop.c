#include "../../coop/coop.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_platform.h"

static const struct coop_pose pose1 = {3, 1024, 2048, -8, 65535, 45, 60, 1};
static const struct coop_pose pose2 = {3, 3072, 4096, 88, 32768, 20, 30, 1};
static uint64_t now = 1000;
static void pause_briefly(void) { test_pause(); }

static void protocol(void)
{
    uint8_t data[COOP_PACKET_SIZE], copy[COOP_PACKET_SIZE];
    struct coop_packet p = {COOP_STATE, 1, 0, 0x12345678, {0}}, got;
    size_t i;
    p.pose = pose1;
    assert(!coop_encode(data, &p));
    assert(!memcmp(data, "UWCP\1\3\1\0\x12\x34\x56\x78\0\3\4\0\10\0\xff\xf8\xff\xff\x2d\x3c\1", 25));
    assert(!coop_decode(&got, data, sizeof data, 1));
    assert(got.sequence == p.sequence && got.pose.z == -8 && got.pose.facing == 65535);
    assert(coop_decode(&got, data, sizeof data, 2) == COOP_WRONG_GAME);
    assert(coop_decode(&got, data, sizeof data - 1, 1) == -1);
    memcpy(copy, data, sizeof data); copy[4]++;
    assert(coop_decode(&got, copy, sizeof copy, 1) == COOP_WRONG_VERSION);
    for (i = 25; i < sizeof data; i++) {
        memcpy(copy, data, sizeof data); copy[i] = 1;
        assert(coop_decode(&got, copy, sizeof copy, 1) == -1);
    }
    memcpy(copy, data, sizeof data); copy[14] = 0x40;
    assert(coop_decode(&got, copy, sizeof copy, 1) == -1); /* x outside map */
    memcpy(copy, data, sizeof data); copy[22] = 61;
    assert(coop_decode(&got, copy, sizeof copy, 1) == -1); /* invalid health */
    memcpy(copy, data, sizeof data); copy[24] = 2;
    assert(coop_decode(&got, copy, sizeof copy, 1) == -1);
    p.pose.level = 10; assert(coop_encode(data, &p) == -1);
    p.game = 2; p.pose.level = 80; assert(!coop_encode(data, &p));
    p.pose.level = 81; assert(coop_encode(data, &p) == -1);
    memset(&p.pose, 0, sizeof p.pose); assert(!coop_encode(data, &p));
    assert(!coop_decode(&got, data, sizeof data, 2) && !got.pose.active);
    p.type = COOP_REJECT; p.game = 1; p.reason = COOP_WRONG_GAME;
    assert(!coop_encode(data, &p));
    assert(!coop_decode(&got, data, sizeof data, 2) && got.reason == COOP_WRONG_GAME);
    /* Fixed seed malformed-frame smoke fuzz: exercise decoding without out-of-bounds reads. */
    srand(7);
    for (i = 0; i < 20000; i++) {
        size_t j;
        for (j = 0; j < sizeof data; j++) data[j] = (uint8_t)rand();
        coop_decode(&got, data, i % (COOP_PACKET_SIZE + 1), 1);
    }
}

static unsigned free_port(void)
{
    test_socket fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    test_socklen n = sizeof addr;
    assert(test_valid(fd));
    memset(&addr, 0, sizeof addr); addr.sin_family = AF_INET;
    /* INADDR_LOOPBACK is not exposed by strict POSIX headers on macOS. */
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(!bind(fd, (struct sockaddr *)&addr, sizeof addr));
    assert(!getsockname(fd, (struct sockaddr *)&addr, &n)); test_close(fd);
    return ntohs(addr.sin_port);
}
static void endpoints(void)
{
    uint32_t addr; uint16_t port;
    const char *bad[] = {"", "0", "-1", "+2", "65536", "999999999999999999999999999", "3x", " 3", "3:4"};
    size_t i;
    assert(!coop_endpoint(COOP_HOST, "65535", &addr, &port) && port == 65535);
    assert(!coop_endpoint(COOP_JOIN, "127.0.0.1:27860", &addr, &port) && port == 27860);
    for (i = 0; i < sizeof bad / sizeof *bad; i++) assert(coop_endpoint(COOP_HOST, bad[i], &addr, &port));
    assert(coop_endpoint(COOP_JOIN, "localhost:27860", &addr, &port));
    assert(coop_endpoint(COOP_JOIN, "256.1.1.1:27860", &addr, &port));
    assert(coop_endpoint(COOP_JOIN, "0.0.0.0:27860", &addr, &port));
    assert(coop_endpoint(COOP_JOIN, "127.0.0.1:", &addr, &port));
    assert(coop_endpoint(COOP_JOIN, "127.0.0.1:9:9", &addr, &port));
}
static unsigned host(struct coop_session *h, int game)
{
    char port[8]; unsigned p = free_port(); snprintf(port, sizeof port, "%u", p);
    assert(!coop_open(h, COOP_HOST, (uint8_t)game, port)); return p;
}
static void join(struct coop_session *c, int game, unsigned port)
{
    char endpoint[40]; snprintf(endpoint, sizeof endpoint, "127.0.0.1:%u", port);
    assert(!coop_open(c, COOP_JOIN, (uint8_t)game, endpoint));
}
static void pump(struct coop_session *h, struct coop_session *c, int frames)
{
    while (frames--) {
        now += 10; coop_tick(h, &pose1, now); coop_tick(c, &pose2, now); pause_briefly();
    }
}
static void presence(int game)
{
    struct coop_session h, c, third, duplicate;
    struct coop_pose inactive = {0};
    int i;
    char port[8]; unsigned p = host(&h, game);
    snprintf(port, sizeof port, "%u", p);
    assert(coop_open(&duplicate, COOP_HOST, (uint8_t)game, port));
    coop_close(&duplicate);
    join(&c, game, p); pump(&h, &c, 100);
    assert(h.status == COOP_CONNECTED && c.status == COOP_CONNECTED);
    assert(h.have_remote && c.have_remote && h.remote.x == pose2.x && c.remote.x == pose1.x);
    join(&third, game, p); pump(&h, &third, 20); pump(&h, &c, 20);
    assert(h.remote.x == pose2.x); coop_close(&third);
    /* Keep sending the inactive pose while waiting for TCP delivery. A single
       host tick can still see the previous pose, especially on macOS. */
    for (i = 0; i < 100 && (!h.have_remote || h.remote.active); i++) {
        now += 10; coop_tick(&c, &inactive, now); coop_tick(&h, &pose1, now); pause_briefly();
    }
    assert(h.have_remote && !h.remote.active); /* menus/death remove the marker */
    coop_close(&c);
    for (i = 0; i < 100 && h.peer != -1; i++) { coop_tick(&h, &pose1, ++now); pause_briefly(); }
    assert(h.status == COOP_WAITING && !h.have_remote);
    join(&c, game, p); pump(&h, &c, 100);
    assert(h.have_remote && c.have_remote); /* new connection resets sequence */
    now += COOP_TIMEOUT_MS + 1; coop_tick(&h, &pose1, now);
    assert(h.status == COOP_WAITING && !h.have_remote);
    pump(&h, &c, 200); assert(h.have_remote && c.have_remote); /* timeout and automatic reconnect */
    coop_close(&h); coop_close(&c);
    assert(h.peer == -1 && h.listener == -1 && h.status == COOP_DISABLED);
    assert(!coop_open(&h, COOP_OFF, 1, NULL)); coop_tick(&h, &pose1, ++now); coop_close(&h);
}
static void mismatch(void)
{
    struct coop_session h, c;
    unsigned p = host(&h, 1); join(&c, 2, p); pump(&h, &c, 100);
    assert(c.status == COOP_REJECTED && c.reason == COOP_WRONG_GAME && !c.have_remote);
    assert(h.status == COOP_WAITING && !h.have_remote);
    coop_close(&c); join(&c, 1, p); pump(&h, &c, 100);
    assert(h.have_remote && c.have_remote); coop_close(&h); coop_close(&c);
}
static void host_restart(void)
{
    struct coop_session h, c;
    unsigned p = free_port();
    int i;
    char port[8]; snprintf(port, sizeof port, "%u", p);
    join(&c, 1, p);
    /* Joining before the host exists stays nonblocking and retries. */
    coop_tick(&c, &pose2, ++now); pause_briefly(); coop_tick(&c, &pose2, ++now);
    assert(!c.have_remote);
    assert(!coop_open(&h, COOP_HOST, 1, port)); pump(&h, &c, 200);
    assert(h.have_remote && c.have_remote);
    coop_close(&h);
    for (i = 0; i < 100 && c.peer != -1; i++) { coop_tick(&c, &pose2, ++now); pause_briefly(); }
    assert(!c.have_remote);
    assert(!coop_open(&h, COOP_HOST, 1, port)); pump(&h, &c, 200);
    assert(h.have_remote && c.have_remote);
    coop_close(&h); coop_close(&c);
}
static test_socket raw_peer(unsigned port)
{
    test_socket fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    assert(test_valid(fd)); memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    assert(inet_pton(AF_INET, "127.0.0.1", &a.sin_addr) == 1);
    test_timeout(fd);
    assert(!connect(fd, (struct sockaddr *)&a, sizeof a)); return fd;
}
static void fragmentation(void)
{
    struct coop_session h;
    struct coop_packet p = {COOP_HELLO, 1, 0, 0, {0}}, got;
    uint8_t data[COOP_PACKET_SIZE], reply[COOP_PACKET_SIZE];
    unsigned port = host(&h, 1); test_socket fd = raw_peer(port); int i;
    assert(!coop_encode(data, &p));
    for (i = 0; i < COOP_PACKET_SIZE; i++) {
        assert(send(fd, (const char *)data + i, 1, 0) == 1);
        coop_tick(&h, &pose1, ++now); pause_briefly();
    }
    for (i = 0; i < 100 && h.status != COOP_CONNECTED; i++) { coop_tick(&h, &pose1, ++now); pause_briefly(); }
    assert(recv(fd, (char *)reply, sizeof reply, MSG_WAITALL) == sizeof reply);
    assert(!coop_decode(&got, reply, sizeof reply, 1) && got.type == COOP_WELCOME);
    p.type = COOP_STATE; p.sequence = 1; p.pose = pose2; assert(!coop_encode(data, &p));
    assert(send(fd, (const char *)data, 7, 0) == 7); coop_tick(&h, &pose1, ++now); assert(!h.have_remote);
    assert(send(fd, (const char *)data + 7, sizeof data - 7, 0) == sizeof data - 7);
    for (i = 0; i < 100 && !h.have_remote; i++) { coop_tick(&h, &pose1, ++now); pause_briefly(); }
    assert(h.have_remote && h.remote.x == pose2.x);
    assert(send(fd, (const char *)data, sizeof data, 0) == sizeof data); /* duplicate sequence rejected */
    for (i = 0; i < 100 && h.peer != -1; i++) { coop_tick(&h, &pose1, ++now); pause_briefly(); }
    assert(!h.have_remote && h.status == COOP_WAITING); test_close(fd);
    fd = raw_peer(port); p.type = COOP_HELLO; assert(!coop_encode(data, &p)); data[4]++;
    assert(send(fd, (const char *)data, sizeof data, 0) == sizeof data);
    for (i = 0; i < 100; i++) { coop_tick(&h, &pose1, ++now); pause_briefly(); }
    assert(recv(fd, (char *)reply, sizeof reply, MSG_WAITALL) == sizeof reply);
    assert(!coop_decode(&got, reply, sizeof reply, 1) && got.reason == COOP_WRONG_VERSION);
    test_close(fd); coop_close(&h);
}
int main(void)
{
    test_sockets_start();
    protocol(); endpoints(); presence(1); presence(2); mismatch(); host_restart(); fragmentation();
    test_sockets_stop();
    puts("co-op protocol, UW1/UW2 loopback, reconnect, timeout, capacity and fragmentation: PASS");
    return 0;
}
