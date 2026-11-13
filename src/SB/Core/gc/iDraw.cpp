#include "iDraw.h"

#include "xMath.h"
#include "zGame.h"

#include <dolphin/gx.h>
#include <dolphin/vi.h>

// FIXME: These should be in a RW header somewhere
extern GXRenderModeObj* _RwDlRenderMode;
extern "C" {
void RwGameCubeGetXFBs(void*, void*);
}

extern _tagGameVidMode gVidMode;
extern U32 FB_XRES;
extern U32 FB_YRES;

struct GCmemXFB
{
    U8* base;
    U8* addr;
    U8* max;
};

U32 lastSetFBMSK;

static GCmemXFB xfb;
static void* xfbDisp;
static void* xfbCopy;

void iDrawSetFBMSK(U32 abgr, U32 setMask, bool plusFrame2)
{
    abgr = (abgr & setMask) | (lastSetFBMSK & ~setMask);
    lastSetFBMSK = abgr;

    if (setMask & 0xFF000000)
    {
        U8 alpha = abgr >> 24;

        if (alpha == 0)
        {
            GXSetAlphaUpdate(GX_TRUE);
        }
        else if (alpha == 0xFF)
        {
            GXSetAlphaUpdate(GX_FALSE);
        }
    }

    if (setMask & 0x00FFFFFF)
    {
        U32 color = abgr & 0x00FFFFFF;

        if (color == 0)
        {
            GXSetColorUpdate(GX_TRUE);
        }
        else
        {
            GXSetColorUpdate(GX_FALSE);
        }
    }
}

void iDrawSetDisplayOffset(F32 x, F32 y)
{
    if (gVidMode == eGameVidModePAL)
    {
        F32 nPosX = x * FB_XRES + 40.0f;
        F32 nPosY = y * FB_YRES + 23.0f;

        nPosX = CLAMP(nPosX, 0.0f, 80.0f);
        nPosY = CLAMP(nPosY, 0.0f, 551.0f);

        _RwDlRenderMode->viXOrigin = nPosX;
        _RwDlRenderMode->viYOrigin = nPosY;
        VIConfigure(_RwDlRenderMode);
    }
}

void iDrawBegin()
{
}

void iDrawEnd()
{
}

void iXFBreset()
{
    RwGameCubeGetXFBs(&xfbDisp, &xfbCopy);

    xfb.base = (U8*)xfbCopy;
    xfb.max = xfb.base + FB_XRES * FB_YRES * 2;
    xfb.base = (U8*)(((U32)xfb.base + 31) & ~31);
    xfb.addr = xfb.base;
}

void* iXFBalloc(U32 size)
{
    U8* addr = xfb.addr;

    if (addr + ((size + 31) & ~31) > xfb.max)
    {
        return (void*)0xBEEEEEEF;
    }

    xfb.addr = addr + ((size + 31) & ~31);
    return addr;
}
