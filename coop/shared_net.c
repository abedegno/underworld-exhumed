#include "shared.h"
#include "coop.h"
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET shared_socket;
#define S_CLOSE closesocket
static int blocked(void) { int e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINPROGRESS; }
static int nonblocking(shared_socket s) { u_long value=1; return ioctlsocket(s,FIONBIO,&value); }
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
typedef int shared_socket;
#define S_CLOSE close
static int blocked(void) { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINPROGRESS || errno==EINTR; }
static int nonblocking(shared_socket s) { int flags=fcntl(s,F_GETFL,0); return flags<0 ? -1 : fcntl(s,F_SETFL,flags|O_NONBLOCK); }
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static void put32(unsigned char *p,uint32_t x) { p[0]=(unsigned char)(x>>24);p[1]=(unsigned char)(x>>16);p[2]=(unsigned char)(x>>8);p[3]=(unsigned char)x; }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
int shared_header(unsigned char *out,int type,int game,uint32_t seq,size_t length)
{
    if ((game!=1 && game!=2) || !seq || length>SHARED_MAX || type<SHARED_HELLO || type>SHARED_AUDIO) return -1;
    if (length!=(type==SHARED_PICTURE ? SHARED_FRAME : type==SHARED_KEYS ? SHARED_INPUT : type==SHARED_AUDIO ? SHARED_SOUND : 0)) return -1;
    memcpy(out,"UWMP",4);out[4]=SHARED_VERSION;out[5]=(unsigned char)type;out[6]=(unsigned char)game;out[7]=0;
    put32(out+8,seq);put32(out+12,(uint32_t)length);return 0;
}
int shared_check(const unsigned char *h,int game,int role,int ready,size_t *length)
{
    unsigned char expected[SHARED_HEADER];
    *length=get32(h+12);
    if (shared_header(expected,h[5],game,get32(h+8),*length) || memcmp(h,expected,SHARED_HEADER)) return -1;
    if (!ready) return h[5]==(role==COOP_HOST ? SHARED_HELLO : SHARED_WELCOME) ? 0 : -1;
    return (h[5]==SHARED_PING || h[5]==(role==COOP_HOST ? SHARED_KEYS : SHARED_PICTURE) ||
            (role==COOP_JOIN && h[5]==SHARED_AUDIO)) ? 0 : -1;
}
static void drop(struct shared_net *n,uint64_t now)
{
    if(n->socket!=-1)S_CLOSE((shared_socket)n->socket);
    n->socket=-1;n->ready=n->connecting=0;n->received=n->sent=n->queued=0;
    n->expected=SHARED_HEADER;n->sequence=n->received_sequence=0;n->retry=now+1000;
}
void shared_close(struct shared_net *n)
{
    drop(n,0);if(n->listener!=-1)S_CLOSE((shared_socket)n->listener);n->listener=-1;
#ifdef _WIN32
    if(n->started)WSACleanup();
#endif
    n->started=0;
}
static int configure(shared_socket s)
{
    int one=1;
    if(nonblocking(s))return -1;
    setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof one);
#ifndef _WIN32
    if(s>=FD_SETSIZE || fcntl(s,F_SETFD,FD_CLOEXEC))return -1;
#ifdef SO_NOSIGPIPE
    setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
#endif
    return 0;
}
int shared_open(struct shared_net *n,int role,int game,const char *endpoint)
{
    struct sockaddr_in a;shared_socket s;int one=1;
    memset(n,0,sizeof *n);n->listener=n->socket=-1;n->role=role;n->game=game;n->expected=SHARED_HEADER;
    if ((game!=1 && game!=2) || coop_endpoint((enum coop_role)role,endpoint,&n->address,&n->port))return -1;
#ifdef _WIN32
    { WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w))return -1; }
#endif
    n->started=1;if(role==COOP_JOIN)return 0;
    s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==(shared_socket)-1)goto fail;n->listener=(intptr_t)s;
    if(configure(s))goto fail;
#ifdef _WIN32
    setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&one,sizeof one);
#else
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
#endif
    memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons(n->port);a.sin_addr.s_addr=n->address;
    if(bind(s,(struct sockaddr*)&a,sizeof a) || listen(s,4))goto fail;return 0;
fail: shared_close(n);n->error=1;return -1;
}
int shared_send(struct shared_net *n,int type,const unsigned char *data,size_t length)
{
    if(n->socket==-1 || n->queued)return 0;
    if(shared_header(n->tx,type,n->game,n->sequence+1,length))return -1;
    if(length)memcpy(n->tx+SHARED_HEADER,data,length);
    n->sequence++;n->queued=SHARED_HEADER+length;n->sent=0;return 1;
}
static void begin(struct shared_net *n,shared_socket s,uint64_t now)
{ n->socket=(intptr_t)s;n->last_receive=n->last_send=now;n->received=0;n->expected=SHARED_HEADER; }
void shared_tick(struct shared_net *n,uint64_t now,void (*message)(int,const unsigned char*,size_t,void*),void *context)
{
    int count;
    if(!n->started)return;
    if(n->listener!=-1) {
        /* Drain at most four incoming connections; a second guest is closed immediately. */
        for(count=0;count<4;count++) {
            shared_socket s=accept((shared_socket)n->listener,NULL,NULL);
            if(s==(shared_socket)-1)break;
            if(n->socket!=-1 || configure(s)) { S_CLOSE(s);continue; }
            begin(n,s,now);
        }
    }
    if(n->role==COOP_JOIN && n->socket==-1 && now>=n->retry) {
        struct sockaddr_in a;shared_socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);int rc;
        n->retry=now+1000;if(s==(shared_socket)-1)return;
        if(configure(s)){S_CLOSE(s);return;}
        memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons(n->port);a.sin_addr.s_addr=n->address;
        rc=connect(s,(struct sockaddr*)&a,sizeof a);
        if(rc && !blocked()){S_CLOSE(s);return;}
        begin(n,s,now);n->connecting=rc!=0;
        if(!n->connecting)shared_send(n,SHARED_HELLO,NULL,0);
    }
    if(n->socket==-1)return;
    if(now-n->last_receive>10000 || (n->queued && now-n->last_send>10000)){drop(n,now);return;}
    if(n->connecting) {
        fd_set writefds,errors;struct timeval wait={0,0};int error=0,rc;
#ifdef _WIN32
        int size=sizeof error;
#else
        socklen_t size=sizeof error;
#endif
        FD_ZERO(&writefds);FD_ZERO(&errors);FD_SET((shared_socket)n->socket,&writefds);FD_SET((shared_socket)n->socket,&errors);
        rc=select((int)n->socket+1,NULL,&writefds,&errors,&wait);
        if(rc<0 || (rc && (getsockopt((shared_socket)n->socket,SOL_SOCKET,SO_ERROR,(char*)&error,&size) || error))) {drop(n,now);return;}
        if(!rc)return;n->connecting=0;shared_send(n,SHARED_HELLO,NULL,0);
    }
    if(n->queued) {
        int sent=send((shared_socket)n->socket,(const char*)n->tx+n->sent,(int)(n->queued-n->sent),MSG_NOSIGNAL);
        if(sent<0 && !blocked()){drop(n,now);return;}
        if(sent==0){drop(n,now);return;}
        if(sent>0){n->sent+=(size_t)sent;n->last_send=now;if(n->sent==n->queued)n->sent=n->queued=0;}
    }
    /* At most two messages per service call; a picture can arrive in arbitrary fragments. */
    for(count=0;count<4;count++) {
        int got=recv((shared_socket)n->socket,(char*)n->rx+n->received,(int)(n->expected-n->received),0);
        if(got<0 && blocked())break;
        if(got<=0){drop(n,now);return;}n->received+=(size_t)got;
        if(n->received==SHARED_HEADER && n->expected==SHARED_HEADER) {
            size_t length;
            if(shared_check(n->rx,n->game,n->role,n->ready,&length) || get32(n->rx+8)<=n->received_sequence){drop(n,now);return;}
            n->expected=SHARED_HEADER+length;
        }
        if(n->received==n->expected) {
            int type=n->rx[5];n->received_sequence=get32(n->rx+8);n->last_receive=now;
            if(!n->ready){
                if(n->role==COOP_HOST && shared_send(n,SHARED_WELCOME,NULL,0)!=1){drop(n,now);return;}
                n->ready=1;
            } else if(type!=SHARED_PING && message)message(type,n->rx+SHARED_HEADER,n->expected-SHARED_HEADER,context);
            n->received=0;n->expected=SHARED_HEADER;
        }
    }
    if(n->ready && !n->queued && now-n->last_send>=1000)shared_send(n,SHARED_PING,NULL,0);
}
