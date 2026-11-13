#ifndef ZNMEHEALTHMETER_H
#define ZNMEHEALTHMETER_H

#include <types.h>
#include <rwcore.h>

#include "xColor.h"

namespace NMEHealthMeter
{
    struct Flags
    {
        S8 enable;
        S8 dev_enable;
        S8 unused[2];
    };

    struct CfgInfo
    {
        S32 stageHealth[4];
        char* str_texture;
        F32 tym_fadeIn;
        F32 tym_fadeOut;
    };

    struct Status
    {
        S32 healthCurr[4];
    };

    struct TxtrXtra
    {
        RwTexture* txtr_texture;
        U32 aid_texture;
    };

    struct RunTime
    {
        RwRaster* rast_inner;
        RwRaster* rast_outer;
        RwRaster* rast_solid_bar;
        RwRaster* rast_broken_bar;
        RwRaster* rast_logo;
    };

    struct MeterData
    {
        Flags flags;
        CfgInfo cfginfo;
        Status status;
        TxtrXtra txtrxtra;
        RunTime runtime;

        void RenderBar(RwRaster* rast, F32 xoffset, const F32* xywh, const F32* uv_beg,
                       const F32* uv_end, xColor_tag c1 = g_WHITE, xColor_tag c2 = g_WHITE);
        void RenderOuter(F32 offset, S32 healthMax, S32 stage);
        void RenderLogo(F32 offset);
        void RenderBars(F32 offset, S32 healthMax, S32 stage);
        void RenderInner(F32 offset, S32 healthCurr, S32 healthMax, S32 stage);
        void Render();
    };
} // namespace NMEHealthMeter

#endif
