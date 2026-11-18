#ifndef XFXHIGHDYNAMICRANGE_H
#define XFXHIGHDYNAMICRANGE_H

#include "xFX.h"
#include "xGlobals.h"
#include "iDraw.h"
#include "xModelBlur.h"
#include "xFX.h"

struct xFXHighDynamicRangeConfiguration
{
    S32 glow;
    S32 darken;
    S32 downsamples;
    F32 overbrighten;
    F32 overbrighten_decay;
    bool blur_faster;
};

struct downsample_pass_info
{
    xVec2* offset;
    S32 resamples;
};

struct interpolator
{
    S32 value;
    F32 start;
    F32 end;
    F32 current;
    F32 t;
    F32 trate;

    void set(S32 v)
    {
        value = v;
        start = current = end = v;
        t = trate = 0.0f;
    }

    void update(F32 dt)
    {
        if (trate == 0.0f)
        {
            return;
        }

        t += trate * dt;
        if (t >= 1.0f)
        {
            t = 0.0f;
            start = current = end;
            trate = 0.0f;
        }
        else
        {
            current = 0.5f + ((1.0f - t) * start + t * end);
        }

        value = (S32)(0.5f + current);
    }
};

extern U8 HDR_brightening;

inline void xFXHighDynamicRangeBrighten(bool bright)
{
    if (HDR_brightening != bright)
    {
        HDR_brightening = bright;
        iDrawSetFBMSK(bright ? 0x00000000 : 0xFF000000, 0xFFFFFFFF, true);
    }
}

void xFXHighDynamicRangeSetBackgroundDefault(S32 glow, S32 darken, bool apply);
void xFXHighDynamicRangeReset();
void xFXHighDynamicRangeSceneExit();
void xFXHighDynamicRangeSceneEnter();
void xFXHighDynamicRangeGetConfiguration(xFXHighDynamicRangeConfiguration& configuration);
void xFXHighDynamicRangeSetConfiguration(const xFXHighDynamicRangeConfiguration& configuration);
S32 xFXHighDynamicRangeGetBackgroundGlow();
void xFXHighDynamicRangeSetGlowClamp(S32 glow_min, bool dest_test);
void xFXHighDynamicRangeResetGlowClamp();
bool xFXHighDynamicRangeEnable(bool tmpEnable);
void xFXHighDynamicRangeRender(RwCamera* camera);
void xFXHighDynamicRangeStartRender();
void xFXHighDynamicRangeUpdate(F32 dt);
U32 xFXHighDynamicRangeBrightenPtank(RpAtomic* ptank, bool bright);
void xFXHighDynamicRangeRestorePtank(RpAtomic* ptank, U32 oldtest);
void xFXHighDynamicRangeTunePalette(RwRaster* raster, F32 base_alpha, bool additive);

#ifdef PLATFORM_PC
// PC-only: the Xbox release's glow on the title and pause menus (zUIMgr.cpp). Whether the menus
// glow ([extras] glow = xbox, video.hdr_glow on), and the menus' glow pass, in place of
// xFXHighDynamicRangeRender at the end of the menu's elements (the Xbox's 0x12f220).
bool xFXHighDynamicRangeMenuGlow();
void xFXHighDynamicRangeRenderMenu(RwCamera* camera);
#endif

#endif
