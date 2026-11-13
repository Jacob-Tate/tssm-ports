#ifndef IDRAW_H
#define IDRAW_H

#include <types.h>

extern U32 lastSetFBMSK;

void iDrawSetFBMSK(U32 abgr, U32 setMask, bool plusFrame2);
void iDrawSetDisplayOffset(F32 x, F32 y);
void iDrawBegin();
void iDrawEnd();
void iXFBreset();
void* iXFBalloc(U32 size);

#endif
