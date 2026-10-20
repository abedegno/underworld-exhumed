/* Host-ABI file IO for the opt-in local campaign driver. Never part of the
   multiplayer wire protocol; original game assets and saves are not its inputs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static int bot_path(char *out,size_t size,const char *name)
{
    const char *home=getenv("UW_COOP_BOT_HOME");
    return home && snprintf(out,size,"%s/%s",home,name)>0;
}
FILE *port_bot_open(const char *name,const char *mode)
{
    char path[1024];FILE *fp;
    if(!bot_path(path,sizeof path,name))return NULL;
    fp=fopen(path,mode);if(fp)setvbuf(fp,NULL,strchr(mode,'w')?_IOFBF:_IONBF,65536);return fp;
}
void port_bot_commit(void)
{
    char from[1024],to[1024];
    if(!bot_path(from,sizeof from,"state.tmp") || !bot_path(to,sizeof to,"state.json"))return;
#ifdef _WIN32
    MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING);
#else
    rename(from,to);
#endif
}
