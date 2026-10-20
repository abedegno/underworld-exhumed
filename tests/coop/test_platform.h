/* Platform helpers for the same transport/adapter tests on Unix and Windows. */
#ifndef COOP_TEST_PLATFORM_H
#define COOP_TEST_PLATFORM_H
#include <assert.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <timeapi.h>
/* The game's compat.h supplies its own DOS keyword; Windows headers are done. */
#undef pascal
typedef SOCKET test_socket;
typedef int test_socklen;
#define test_close closesocket
#define test_valid(fd) ((fd) != INVALID_SOCKET)
static inline void test_sockets_start(void)
{
    WSADATA data;
    assert(!WSAStartup(MAKEWORD(2, 2), &data));
#ifdef SHARED_TEST_FAST_TIMER
    timeBeginPeriod(1);
#endif
}
static inline void test_sockets_stop(void) {
#ifdef SHARED_TEST_FAST_TIMER
    timeEndPeriod(1);
#endif
    WSACleanup();
}
static inline void test_pause(void) { Sleep(1); }
static inline void test_timeout(test_socket fd)
{
    DWORD milliseconds = 1000;
    assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&milliseconds, sizeof milliseconds));
}
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
typedef int test_socket;
typedef socklen_t test_socklen;
#define test_close close
#define test_valid(fd) ((fd) >= 0)
static inline void test_sockets_start(void) { }
static inline void test_sockets_stop(void) { }
static inline void test_pause(void) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static inline void test_timeout(test_socket fd)
{
    struct timeval t = {1, 0};
    assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t));
}
#endif
#endif
