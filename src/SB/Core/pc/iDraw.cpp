#include "iDraw.h"

#include <stddef.h>
#include <stdlib.h>

// PC iDraw: TSSM's interface (gc/iDraw.cpp) on treedome's pc/iDraw.cpp.
//
// iDrawSetFBMSK is the frame-buffer write mask, named after the PS2's GS
// register (a SET bit means DO NOT WRITE). The GameCube reduces it to two GX
// switches, colour and alpha; the port forwards it to the COLORWRITEMASK
// render state treedome added to the librw fork (D3DRS_COLORWRITEENABLE on
// D3D9), per channel. TSSM's version also merges the new mask with the last
// one under `setMask`, so only the bits being set change; that is kept.
//
// The console reduction is coarser than the per-channel one here: GX only
// switches alpha when the byte is 0 or 0xFF and colour as a whole. Every caller
// passes one of the two extremes per channel (treedome measured BfBB's), so
// the two agree; a partial mask would differ.
//
// iDrawSetDisplayOffset moves the PAL picture with the video interface's
// origin, and the XFB pair lends out the console's second frame buffer as
// scratch memory (the movie player uses it). Neither has a host meaning; see
// each.

// Defined in rw/renderstate.cpp, which owns librw; the values match librw's
// ColorWriteMask.
void rwSetColorWriteMask(U32 mask);

#define IDRAW_WRITE_RED 1
#define IDRAW_WRITE_GREEN 2
#define IDRAW_WRITE_BLUE 4
#define IDRAW_WRITE_ALPHA 8

U32 lastSetFBMSK;

void iDrawSetFBMSK(U32 abgr, U32 setMask, bool plusFrame2)
{
    abgr = (abgr & setMask) | (lastSetFBMSK & ~setMask);
    lastSetFBMSK = abgr;

    U32 mask = 0;

    if ((abgr & 0x000000FF) == 0)
    {
        mask |= IDRAW_WRITE_RED;
    }
    if ((abgr & 0x0000FF00) == 0)
    {
        mask |= IDRAW_WRITE_GREEN;
    }
    if ((abgr & 0x00FF0000) == 0)
    {
        mask |= IDRAW_WRITE_BLUE;
    }
    if ((abgr & 0xFF000000) == 0)
    {
        mask |= IDRAW_WRITE_ALPHA;
    }

    rwSetColorWriteMask(mask);
}

// PAL only on the console (it nudges the VI origin). No-op on a host.
void iDrawSetDisplayOffset(F32 x, F32 y)
{
}

void iDrawBegin()
{
}

void iDrawEnd()
{
}

// The console hands out the idle external frame buffer (FB_XRES * FB_YRES * 2
// bytes) as scratch memory. A host has no such buffer to borrow, so this is a
// heap block of the size a 640x480 XFB would have, allocated once and reused
// the same way: iXFBreset rewinds, iXFBalloc bumps, and 0xBEEEEEEF says "full"
// exactly as retail does.
#define IXFB_SIZE (640 * 528 * 2)

static unsigned char* sXFB;
static unsigned char* sXFBAddr;

void iXFBreset()
{
    if (sXFB == NULL)
    {
        sXFB = (unsigned char*)malloc(IXFB_SIZE + 31);
    }
    sXFBAddr = (unsigned char*)(((size_t)sXFB + 31) & ~(size_t)31);
}

void* iXFBalloc(U32 size)
{
    if (sXFBAddr == NULL)
    {
        iXFBreset();
    }

    unsigned char* addr = sXFBAddr;
    U32 rounded = (size + 31) & ~31;

    if (sXFB == NULL || addr + rounded > sXFB + IXFB_SIZE + 31)
    {
        return (void*)0xBEEEEEEF;
    }

    sXFBAddr = addr + rounded;
    return addr;
}
