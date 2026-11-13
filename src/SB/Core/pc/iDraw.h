#ifndef IDRAW_H
#define IDRAW_H

// gc/iDraw.h's interface; unchanged except that it is the PC layer's copy
// (gc/iDraw.h is identical today but includes nothing GameCube-specific only
// by luck of what it declares, so it is kept as an adapted file).
#include <types.h>

extern U32 lastSetFBMSK;

void iDrawSetFBMSK(U32 abgr, U32 setMask, bool plusFrame2);
void iDrawSetDisplayOffset(F32 x, F32 y);
void iDrawBegin();
void iDrawEnd();
void iXFBreset();
void* iXFBalloc(U32 size);

#endif
