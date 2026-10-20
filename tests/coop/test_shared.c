#include "../../coop/shared.h"
#include "../../coop/coop.h"
#define SHARED_TEST_FAST_TIMER 1
#include "test_platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static uint64_t clock_ms=1000;
static int pictures,inputs,audio;
static unsigned char picture[SHARED_FRAME],input[SHARED_INPUT],sound[SHARED_SOUND];
static void message(int type,const unsigned char *data,size_t size,void *context)
{
    int role=*(int*)context;
    if(type==SHARED_PICTURE){assert(role==COOP_JOIN && size==SHARED_FRAME && !memcmp(data,picture,size));pictures++;}
    else if(type==SHARED_KEYS){assert(role==COOP_HOST && size==SHARED_INPUT && !memcmp(data,input,size));inputs++;}
    else if(type==SHARED_AUDIO){assert(role==COOP_JOIN && size==SHARED_SOUND && !memcmp(data,sound,size));audio++;}
    else assert(0);
}
static unsigned free_port(void)
{
    test_socket fd=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a;test_socklen size=sizeof a;
    assert(test_valid(fd));memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(fd,(struct sockaddr*)&a,sizeof a));assert(!getsockname(fd,(struct sockaddr*)&a,&size));
    test_close(fd);return ntohs(a.sin_port);
}
static void pump(struct shared_net *host,struct shared_net *client,int count)
{
    int h=COOP_HOST,c=COOP_JOIN;
    while(count--){clock_ms+=2;shared_tick(host,clock_ms,message,&h);shared_tick(client,clock_ms,message,&c);test_pause();}
}
int main(void)
{
    unsigned char header[SHARED_HEADER],bad[SHARED_HEADER];size_t length;int game,i;
    struct shared_net *host=calloc(1,sizeof *host),*client=calloc(1,sizeof *client),*third=calloc(1,sizeof *third);
    assert(host && client && third);test_sockets_start();
    assert(!shared_header(header,SHARED_PICTURE,1,7,SHARED_FRAME));
    assert(!shared_check(header,1,COOP_JOIN,1,&length) && length==SHARED_FRAME);
    assert(shared_check(header,2,COOP_JOIN,1,&length));
    assert(shared_check(header,1,COOP_HOST,1,&length));
    assert(shared_check(header,1,COOP_JOIN,0,&length));
    memcpy(bad,header,sizeof bad);bad[12]=255;assert(shared_check(bad,1,COOP_JOIN,1,&length));
    memcpy(bad,header,sizeof bad);bad[4]++;assert(shared_check(bad,1,COOP_JOIN,1,&length));
    memcpy(bad,header,sizeof bad);bad[4]=2;assert(shared_check(bad,1,COOP_JOIN,1,&length)); /* older clients reject shared scenes */
    memcpy(bad,header,sizeof bad);bad[7]=1;assert(shared_check(bad,1,COOP_JOIN,1,&length));
    assert(shared_header(header,SHARED_KEYS,1,1,SHARED_INPUT-1));
    for(i=0;i<SHARED_FRAME;i++)picture[i]=(unsigned char)(i*73);
    for(i=0;i<SHARED_INPUT;i++)input[i]=(unsigned char)i;
    for(i=0;i<SHARED_SOUND;i++)sound[i]=(unsigned char)(i*11);
    for(game=1;game<=2;game++) {
        char port[16],endpoint[64];unsigned p=free_port();snprintf(port,sizeof port,"%u",p);snprintf(endpoint,sizeof endpoint,"127.0.0.1:%u",p);
        assert(!shared_open(host,COOP_HOST,game,port));assert(!shared_open(client,COOP_JOIN,game,endpoint));
        pump(host,client,200);assert(host->ready && client->ready);
        assert(shared_send(host,SHARED_PICTURE,picture,sizeof picture)==1);
        assert(shared_send(host,SHARED_PICTURE,picture,sizeof picture)==0); /* bounded queue */
        assert(shared_send(client,SHARED_KEYS,input,sizeof input)==1);
        pump(host,client,300);assert(pictures==game && inputs==game);
        assert(shared_send(host,SHARED_AUDIO,sound,sizeof sound)==1);pump(host,client,100);assert(audio==game);
        assert(!shared_open(third,COOP_JOIN,game,endpoint));
        for(i=0;i<100;i++){int c=COOP_JOIN;pump(host,client,1);shared_tick(third,clock_ms,message,&c);}
        assert(host->ready && client->ready && !third->ready);shared_close(third);
        shared_close(client);pump(host,client,1);assert(!host->ready);
        assert(!shared_open(client,COOP_JOIN,game,endpoint));pump(host,client,200);assert(host->ready && client->ready);
        clock_ms+=10001;{int h=COOP_HOST;shared_tick(host,clock_ms,message,&h);}assert(!host->ready);
        pump(host,client,1000);assert(host->ready && client->ready);shared_close(host);shared_close(client);
        /* A presence or other-game executable cannot enter the shared session. */
        assert(!shared_open(host,COOP_HOST,game,port));assert(!shared_open(client,COOP_JOIN,3-game,endpoint));
        pump(host,client,200);assert(!host->ready && !client->ready);shared_close(host);shared_close(client);
    }
    test_sockets_stop();free(host);free(client);free(third);
    puts("shared-world framing, direction/version/game checks, pictures, input, audio, bounded queues, capacity, reconnect and timeout: PASS");
    return 0;
}
