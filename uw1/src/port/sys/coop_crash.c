/* Opt-in local integration diagnostics, before Windows' normal crash handler. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static LONG CALLBACK shared_exception(EXCEPTION_POINTERS *event)
{
    void *stack[32];unsigned n,i;
    if(event->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr,"SHARED CRASH base=%llx ip=%llx memory=%llx\n",
        (unsigned long long)(uintptr_t)GetModuleHandleW(NULL),
        (unsigned long long)event->ContextRecord->Rip,
        (unsigned long long)event->ExceptionRecord->ExceptionInformation[1]);
    n=CaptureStackBackTrace(0,32,stack,NULL);
    for(i=0;i<n;i++)fprintf(stderr,"SHARED CRASH stack=%llx\n",(unsigned long long)(uintptr_t)stack[i]);
    fflush(stderr);return EXCEPTION_CONTINUE_SEARCH;
}
void port_shared_crash_install(void)
{if(getenv("UW_COOP_TEST_TRACE"))AddVectoredExceptionHandler(1,shared_exception);}
#else
void port_shared_crash_install(void) {}
#endif
