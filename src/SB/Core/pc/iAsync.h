#ifndef IASYNC_H
#define IASYNC_H

#include <types.h>

void iAsyncStart(void* (*func)(void*));
S32 iAsyncIsDone();
void iAsyncTerminate();
void iAsyncBeginUpdate();
void iAsyncEndUpdate();
void iAsyncSuspend();
void iAsyncResume();

#endif
