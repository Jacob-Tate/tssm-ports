// TSSM's HDR glow as a PC pass. What the console does and how this follows it
// are in iHDRGlow.h; the steps the passes share are in screenpass.h.

#include <rwcore.h>

#include "rw.h"

#include "backend.h"
#include "iGpuOpt.h"
#include "iHDRGlow.h"
#include "screenpass.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// config.ini's video.hdr_glow, pushed down by iSystem.cpp. On unless told
// otherwise, so a target that never calls the setter gets the console's look.
static S32 sEnabled = TRUE;

// The console's numbers. The bright pass and the glow texture are 128x128
// whatever the frame's size, as on the console, so the glow's softness is the
// same fraction of the screen at any render size.
static const RwInt32 kGlowSize = 128;

// TEV stages in the bright pass, each adding texel x C0.
static const S32 kBrightStages = 5;

// The composite's colour, GXSetTevColor(GX_TEVREG0, 0x80808080 | 0xFF): half
// the glow's colour, all of its alpha.
static const U8 kCompositeColor = 0x80;

// The console's half-size copy is 320 wide; the port halves until the picture
// is at most one and a half times that.
static const RwInt32 kHalfWidthMax = 480;

// At most this many halvings: 15360 wide, the largest render width config.ini
// takes, is under 480 after six.
#define HDR_MAX_HALVINGS 6

enum
{
    VIEW_NONE,
    VIEW_MASK,
    VIEW_GLOW
};

static RwRaster* sScreen;
static iScreenPassTarget sHalf[HDR_MAX_HALVINGS];
static S32 sHalvings;
static iScreenPassTarget sBright;
static iScreenPassTarget sGlow;
static S32 sFailed;
static S32 sReported;
static S32 sView = -1;

static void hdrFail(const char* what)
{
    sFailed = 1;
    printf("tssm: the HDR glow is off -- %s\n", what);
    fflush(stdout);
}

// The bright pass's TEV constant, as the console computes it: a float stored
// into a byte, which keeps the low eight bits of the integer (fctiwz, stb).
// Done through S32 here because a float to U8 conversion out of range is
// undefined in C++.
static U8 brightConstant(F32 overbrighten)
{
    F32 alpha = 0.8f * (3.0f * overbrighten) + 0.2f;
    return (U8)(S32)(255.0f * alpha);
}

// The halving chain, sized from the picture. Remade when the render size
// changes (nothing changes it while the game runs today).
static bool makeChain(RwInt32 w, RwInt32 h)
{
    RwInt32 hw = w / 2;
    RwInt32 hh = h / 2;
    S32 n = 0;

    for (;;)
    {
        if (hw < 1)
        {
            hw = 1;
        }
        if (hh < 1)
        {
            hh = 1;
        }

        if (!iScreenPassMakeTarget(&sHalf[n], hw, hh))
        {
            return false;
        }

        n++;
        if (hw <= kHalfWidthMax || n == HDR_MAX_HALVINGS)
        {
            break;
        }

        hw /= 2;
        hh /= 2;
    }

    for (S32 i = n; i < HDR_MAX_HALVINGS; i++)
    {
        iScreenPassDestroyTarget(&sHalf[i]);
    }

    sHalvings = n;
    return iScreenPassMakeTarget(&sBright, kGlowSize, kGlowSize) &&
           iScreenPassMakeTarget(&sGlow, kGlowSize, kGlowSize);
}

// Draw `src` over the whole of target `t`, white, with the blend given.
static void drawInto(iScreenPassTarget* t, RwRaster* src, RwBlendFunction srcBlend,
                     RwBlendFunction dstBlend, U8 c)
{
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)srcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dstBlend);
    iScreenPassQuad(src, 0.0f, 0.0f, (F32)t->width, (F32)t->height, 0.0f, 0.0f, 1.0f, 1.0f, c, c,
                    c, c);
}

void iHDRGlowRender(RwCamera* cam, F32 overbrighten, S32 downsamples, S32 downSrc, S32 downDst,
                    S32 src, S32 dst)
{
    if (!sEnabled || sFailed || cam == NULL || !iScreenPassReady())
    {
        return;
    }

    if (sView < 0)
    {
        const char* view = getenv("TSSM_HDR_VIEW");
        sView = VIEW_NONE;
        if (view != NULL && strcmp(view, "mask") == 0)
        {
            sView = VIEW_MASK;
        }
        else if (view != NULL && strcmp(view, "glow") == 0)
        {
            sView = VIEW_GLOW;
        }
    }

    iScreenPassState saved;
    iScreenPassBegin(&saved);

    // The copy needs the scene closed, and the passes render into their own
    // cameras.
    RwCameraEndUpdate(cam);

    // The mask view draws the picture back into the frame, so it needs a copy;
    // the glow itself only reads it into its first target and can read the
    // frame where it is (iScreenPassReadFrame).
    RwRaster* picture = NULL;
    if (sView == VIEW_MASK ? !iScreenPassCapture(&sScreen) :
                             !iScreenPassReadFrame(&sScreen, &picture))
    {
        hdrFail("the frame could not be copied into a texture");
        RwCameraBeginUpdate(cam);
        iScreenPassEnd(&saved);
        return;
    }

    if (picture == NULL)
    {
        picture = sScreen;
    }

    if (!makeChain(picture->width, picture->height))
    {
        hdrFail("its render targets could not be made");
        RwCameraBeginUpdate(cam);
        iScreenPassEnd(&saved);
        return;
    }

    if (!sReported)
    {
        sReported = 1;
        printf("tssm: HDR glow: %dx%d", (int)picture->width, (int)picture->height);
        for (S32 i = 0; i < sHalvings; i++)
        {
            printf(" -> %dx%d", (int)sHalf[i].width, (int)sHalf[i].height);
        }
        printf(" -> %dx%d, bright scale %d/255 x %d\n", (int)kGlowSize, (int)kGlowSize,
               (int)brightConstant(overbrighten), (int)kBrightStages);
        fflush(stdout);
    }

    // The console refreshes the glow texture only inside its blur loop, which
    // runs downsamples - 1 times.
    if (downsamples > 1)
    {
        // 1. Halve, as GXCopyTex's box filter does, then halve again at
        //    render sizes above the console's.
        //    Each of these targets is drawn over whole, so it is cleared
        //    first: a GPU that draws in tiles then starts it from nothing
        //    instead of reading back what it held.
        RwRaster* from = picture;
        for (S32 i = 0; i < sHalvings; i++)
        {
            if (iGpuOptOn(IGPUOPT_INVALIDATE))
            {
                iScreenPassClear(&sHalf[i]);
            }
            RwCameraBeginUpdate(sHalf[i].camera);
            drawInto(&sHalf[i], from, rwBLENDONE, rwBLENDZERO, 0xFF);
            RwCameraEndUpdate(sHalf[i].camera);
            from = sHalf[i].raster;
        }

        // 2. The TEV stages: min(1, stages x C0 x texel) in all four channels,
        //    as one draw at up to 1 and additive draws for the rest.
        S32 total = kBrightStages * (S32)brightConstant(overbrighten);

        if (iGpuOptOn(IGPUOPT_INVALIDATE))
        {
            iScreenPassClear(&sBright);
        }
        RwCameraBeginUpdate(sBright.camera);
        S32 left = total;
        bool first = true;
        do
        {
            S32 c = left > 255 ? 255 : left;
            drawInto(&sBright, from, rwBLENDONE, first ? rwBLENDZERO : rwBLENDONE, (U8)c);
            left -= c;
            first = false;
        } while (left > 0);
        RwCameraEndUpdate(sBright.camera);

        //    Then the console's blend of that into the EFB corner, which held
        //    the picture; a destination factor other than ZERO (not retail's)
        //    blends with transparent black here instead.
        iScreenPassClear(&sGlow);
        RwCameraBeginUpdate(sGlow.camera);
        drawInto(&sGlow, sBright.raster, iScreenPassGXBlend(downSrc, true),
                 iScreenPassGXBlend(downDst, false), 0xFF);
        RwCameraEndUpdate(sGlow.camera);
    }

    RwCameraBeginUpdate(cam);

    RwRaster* frame = RwCameraGetRaster(cam);
    F32 w = frame != NULL ? (F32)frame->width : (F32)picture->width;
    F32 h = frame != NULL ? (F32)frame->height : (F32)picture->height;

    if (sView == VIEW_MASK)
    {
        // White, then the copy's alpha multiplied in: the mask in grey.
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iScreenPassQuad(NULL, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFF, 0xFF, 0xFF, 0xFF);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCALPHA);
        iScreenPassQuad(sScreen, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFF, 0xFF, 0xFF,
                        0xFF);
        iScreenPassEnd(&saved);
        return;
    }

    if (sView == VIEW_GLOW)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iScreenPassQuad(NULL, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 0xFF);
    }

    // 4. The glow over the frame, alpha writes off (GXSetAlphaUpdate(GX_FALSE));
    //    colour writes as the game left them.
    iScreenPassSetWriteMask(saved.writeMask & ISCREENPASS_WRITE_RGB);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)iScreenPassGXBlend(src, true));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)iScreenPassGXBlend(dst, false));
    iScreenPassQuad(sGlow.raster, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, kCompositeColor,
                    kCompositeColor, kCompositeColor, 0xFF);

    iScreenPassEnd(&saved);
}

void iHDRGlowSetEnabled(S32 enabled)
{
    sEnabled = enabled ? TRUE : FALSE;
}

S32 iHDRGlowEnabled()
{
    return sEnabled;
}
