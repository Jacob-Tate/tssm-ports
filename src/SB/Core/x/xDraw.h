#ifndef XDRAW_H
#define XDRAW_H

#include "iColor.h"
#include "xMath3.h"

void xDrawSetColor(iColor_tag);
// No GC symbol: an empty inline in release builds (as in BfBB's xDraw.h).
inline void xDrawLine(const xVec3* a, const xVec3* b)
{
}
inline void xDrawSphere2(const xSphere*, U32)
{
}

inline void xDrawOBB(const xBox*, const xMat4x3*)
{
}

inline void xDrawBox(const xBox*)
{
}

#endif
