#include "iWad.h"

OSThread asyncThread;
bool asyncThreadDone = true;
RwLinkList asyncDirtyFrameList;

static U8 ThreadStack[0x2000];

void iAsyncStart(void* (*func)(void*))
{
    rwLinkListInitialize(&asyncDirtyFrameList);
    asyncThreadDone = false;
    OSCreateThread(&asyncThread, func, NULL, ThreadStack + sizeof(ThreadStack), sizeof(ThreadStack),
                   10, 1);
    OSResumeThread(&asyncThread);
}

S32 iAsyncIsDone()
{
    return asyncThreadDone;
}

void iAsyncTerminate()
{
    asyncThreadDone = true;
}

void iAsyncBeginUpdate()
{
    OSDisableInterrupts();
}

void iAsyncEndUpdate()
{
    OSEnableInterrupts();
}

void iAsyncSuspend()
{
    if (!asyncThreadDone && !OSIsThreadSuspended(&asyncThread))
    {
        OSSuspendThread(&asyncThread);
    }
}

void iAsyncResume()
{
    if (!asyncThreadDone && OSIsThreadSuspended(&asyncThread))
    {
        OSResumeThread(&asyncThread);
    }
}
