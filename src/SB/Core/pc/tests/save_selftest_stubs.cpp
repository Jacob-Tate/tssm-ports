// What save_selftest's slice of the game and the platform layer call outside
// that slice: the allocator, the still, the mod and config settings. Each is
// the simplest thing that behaves the same for the save code.
#include <types.h>

#include <stdlib.h>
#include <string.h>

#include "iSaveThumb.h"

U32 gActiveHeap;

void* xMemAlloc(U32 heapID, U32 size, S32 align)
{
    return calloc(1, size != 0 ? size : 1);
}

void* iMemPushTemp(U32 size)
{
    return calloc(1, size != 0 ? size : 1);
}

void iMemPopTemp(void* memory)
{
    free(memory);
}

// xutil.cpp's xUtil_yesno draws on the game's random numbers; the save code
// never calls it.
U32 xrand_GenRandInt32()
{
    return (U32)rand();
}

// No frame to take a still of: saves get none, as with xbox.snapshot off.
void iSaveThumbCapture()
{
}

S32 iSaveThumbHave()
{
    return 0;
}

S32 iSaveThumbWrite(const char*)
{
    return 0;
}

RwTexture* iSaveThumbLoad(const char*, F32*)
{
    return NULL;
}

void iSaveThumbFree(RwTexture*)
{
}

// No mod, and config.ini's defaults.
const char* iFileModName()
{
    return "";
}

const char* iFileModRoot()
{
    return "";
}

const char* iConfigGetString(const char* key, const char* def)
{
    return def;
}
