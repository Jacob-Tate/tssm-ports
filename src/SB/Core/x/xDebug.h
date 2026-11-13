#ifndef XDEBUG_H
#define XDEBUG_H

#include <types.h>

extern U32 gFrameCount;
extern U32 gSceneFrameCount;
void xprintf(const char* msg, ...);
S32 xDebugModeAdd(char* mode, void(*debugFunc));
void xDebugInit();
void xDebugUpdate();
void xDebugExit();
void xDebugTimestampScreen();

void xDebugUpdate();

void xDrawTextInit(U32, U32);
void xDrawTextRender();

// Heavy Iron's assert macro, compiled out in the final build ((void)0 is assumed). It still counts
// as a statement and costs 3 in GC/2.6's inline-size estimate, so it can decide whether a function
// is inlined. Add one only where that is needed to match; use a real condition only where
// Ratatouille has the xASSERT in the same function, otherwise xASSERT_PLACEHOLDER().
#define xASSERT(cond) ((void)0)

// Heavy Iron's validate macro, compiled out like xASSERT; use it only where Ratatouille has the
// xVALIDATE in the same function.
#define xVALIDATE(cond) ((void)0)

// An assert retail compiled out whose condition is unknown. Same expansion as xASSERT.
#define xASSERT_PLACEHOLDER() ((void)0)

// xFAIL: an assert that always fails. Same expansion as xASSERT.
#define xFAIL() ((void)0)

#endif
