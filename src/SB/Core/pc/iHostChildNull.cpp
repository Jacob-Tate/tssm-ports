// The child-process half of iHost.h on every host but Windows: not available.
// Local co-op starts a second copy of the game, which Android and the Switch
// cannot (one process per app), and on desktop POSIX it is not built yet.

#include "iHost.h"

iHostChild* iHostChildCreate()
{
    return NULL;
}

const char* iHostChildPipeValue(const iHostChild*)
{
    return "";
}

bool iHostChildSend(iHostChild*, const char*, size_t)
{
    return false;
}

bool iHostChildStart(iHostChild*, const char*, const char*)
{
    return false;
}

U32 iHostChildId(const iHostChild*)
{
    return 0;
}

S32 iHostChildState(iHostChild*, U32*)
{
    return -1;
}

bool iHostChildWait(iHostChild*, U32)
{
    return false;
}

void iHostChildClosePipe(iHostChild*)
{
}

void iHostChildKill(iHostChild*)
{
}

void iHostChildFree(iHostChild*)
{
}

bool iHostExePath(char*, size_t)
{
    return false;
}

bool iHostEnvironment(void (*)(void*, const char*), void*)
{
    return false;
}

bool iHostParentPipeOpen(const char*)
{
    return false;
}

S32 iHostParentRead(char*, size_t, U32)
{
    return -1;
}
