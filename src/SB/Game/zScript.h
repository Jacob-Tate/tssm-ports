#ifndef ZSCRIPT_H
#define ZSCRIPT_H

#include <types.h>
#include "xBase.h"

struct xScene;

struct xScriptEventAsset
{
    F32 time; // 0x0
    U32 widget; // 0x4
    U32 paramEvent; // 0x8
    F32 param[4]; // 0xC
    U32 paramWidget; // 0x1C
};

struct xScriptAsset : xBaseAsset
{
    F32 scaleFactor; // 0x8
    U32 eventCount; // 0xC
    U8 loop; // 0x10
    U8 pad[3];
};

struct _zScript : xBase
{
    xScriptAsset* tasset; // 0x10
    xScriptEventAsset* current; // 0x14
    U8 state; // 0x18
    U8 pad[3];
    F32 time; // 0x1C
    F32 scaleFactor; // 0x20
};

#define ZSCRIPT_STATE_READY (0)
#define ZSCRIPT_STATE_RUNNING (1)

void zScriptInit(void* b, void* tasset);
void zScriptReset(_zScript* t);
void zScriptUpdate(xBase* to, xScene* sc, F32 dt);
void zScriptEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32);

#endif
