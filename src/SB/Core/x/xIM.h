#ifndef XIM_H
#define XIM_H

#include <types.h>
#include "iColor.h"

struct xVec2;
struct xVec3;
struct xMat4x3;

// GC immediate-mode vertex lock (layout and enum order from the Ratatouille GC DWARF, xIM.cpp).
// TSSM's zFire::render passes 3 for the face triangle list and 4 for the floor strip.
enum xIMFormat
{
    XIM_TRISTRIP = 4,
    XIM_TRILIST = 3
};

// total size: 0x18
struct xIMLockContext
{
    void* data;
    xVec3* pos;
    xVec2* uv;
    xColor_tag* color;
    S32 vertCount;
    xIMFormat format;
};

void xIMLock(xIMLockContext& context, xIMFormat format, S32 vertCount);
void xIMUnlock(xIMLockContext& context);
void xIMRender(xIMLockContext& context, const xMat4x3* mat);

#endif
