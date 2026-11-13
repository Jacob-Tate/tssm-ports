#include "iException.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>

// gc/iException.cpp reports through OSReport and stops the console with
// OSPanic. The host equivalent: say it on the console and in a message box,
// then exit -- there is nothing to return to, retail does not return either.
void iExceptionMemCrash(const char* loc, U32 size, const char* arg3)
{
    char message[512];
    snprintf(message, sizeof(message), "Out of memory crash, Loc=%s   Size=%d\n%s", loc,
             (int)size, arg3 != NULL ? arg3 : "");

    printf("tssm: FATAL -- %s\n", message);
    fflush(stdout);
    iHostErrorBox(TSSM_GAME_TITLE, message);
    exit(-5);
}
