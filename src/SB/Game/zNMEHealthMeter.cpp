#include "zNMEHealthMeter.h"
#include "zRenderState.h"
#include "xColor.h"
#include "xMathInlines.h"

#include <rwcore.h>

void NMEHealthMeter::MeterData::Render()
{
    _SDRenderState old_rendstat;

    old_rendstat = zRenderStateCurrent();
    if (old_rendstat == SDRS_Unknown)
    {
        old_rendstat = SDRS_Default;
    }
    zRenderState(SDRS_NPCBossMeter);

    F32 xoff;
    S32 i;

    xoff = -0.206f;
    RenderLogo(xoff);

    for (i = 3; i >= 1; i--)
    {
        RenderOuter(xoff, cfginfo.stageHealth[i], i);
        RenderBars(xoff, cfginfo.stageHealth[i], i);
        RenderInner(xoff, status.healthCurr[i], cfginfo.stageHealth[i], i);

        // Both arms are equal in retail too.
        if (i == 3)
        {
            xoff += 0.0135f * cfginfo.stageHealth[i] + 0.04f;
        }
        else
        {
            xoff += 0.0135f * cfginfo.stageHealth[i] + 0.04f;
        }
    }

    zRenderState(old_rendstat);
}

// cBeg/cEnd (our names; not in the PS2 DWARF) hold the stage's gradient ends.
void NMEHealthMeter::MeterData::RenderInner(F32 offset, S32 healthCurr, S32 healthMax, S32 stage)
{
    F32 percentFull = (F32)healthCurr / (F32)healthMax;
    F32 xywh[4] = { 0.04f, -0.44f, 0.0135f, 0.02f };
    F32 uv_beg[2] = { 0.0f, 0.5f };
    F32 uv_end[2] = { 1.0f, 1.0f };
    F32 xStart;
    S32 i;
    xColor_tag cBeg;
    xColor_tag cEnd;

    uv_end[1] = percentFull * (uv_end[1] - uv_beg[1]) + uv_beg[1];

    if (stage == 1)
    {
        cBeg.r = 0xAA;
        cBeg.g = 0xFF;
        cBeg.b = 0;
        cEnd.r = 0;
        cEnd.g = 0xFF;
        cEnd.b = 0;
        xStart = 0.02f;
        xywh[2] = 0.0135f + 0.04f / (2.0f * healthMax);
    }
    else if (stage == 2)
    {
        cBeg.r = 0xFF;
        cBeg.g = 0xAA;
        cBeg.b = 0;
        cEnd.r = 0xAA;
        cEnd.g = 0xFF;
        cEnd.b = 0;
        xStart = 0.02f;
        xywh[2] = 0.0135f + 0.04f / healthMax;
    }
    else
    {
        cBeg.r = 0xFF;
        cBeg.g = 0;
        cBeg.b = 0;
        cEnd.r = 0xFF;
        cEnd.g = 0xAA;
        cEnd.b = 0;
        xStart = 0.04f;
        xywh[2] = 0.0135f + 0.04f / (2.0f * healthMax);
    }

    cEnd.r = cBeg.r + (U8)(percentFull * (cEnd.r - cBeg.r));
    cEnd.g = cBeg.g + (U8)(percentFull * (cEnd.g - cBeg.g));

    if (healthCurr == 0)
    {
        cBeg.r = 0;
        cBeg.g = 0;
        cBeg.b = 0;
        cEnd.r = 0;
        cEnd.g = 0;
        cEnd.b = 0;
    }

    xywh[0] = xStart;
    for (i = 0; i < healthCurr; i++)
    {
        xColor_tag cLeft;
        xColor_tag cRight;
        F32 leftRatio;
        F32 rightRatio;

        leftRatio = (F32)i / (F32)healthMax;
        cLeft.r = cBeg.r + (U8)(leftRatio * (cEnd.r - cBeg.r));
        cLeft.g = cBeg.g + (U8)(leftRatio * (cEnd.g - cBeg.g));
        cLeft.b = cBeg.b + (U8)(leftRatio * (cEnd.b - cBeg.b));
        cLeft.a = 0xFF;

        rightRatio = (F32)(i + 1) / (F32)healthMax;
        cRight.r = cBeg.r + (U8)(rightRatio * (cEnd.r - cBeg.r));
        cRight.g = cBeg.g + (U8)(rightRatio * (cEnd.g - cBeg.g));
        cRight.b = cBeg.b + (U8)(rightRatio * (cEnd.b - cBeg.b));
        cRight.a = 0xFF;

        RenderBar(runtime.rast_inner, offset, xywh, uv_beg, uv_end, cLeft, cRight);
        xywh[0] += xywh[2];
    }
}

// No GC symbols: Render inlines RenderOuter, RenderLogo and RenderBars. RenderBars is named by the
// PS2 build; the other two names are ours, split out to follow retail's stack and constant order.
void NMEHealthMeter::MeterData::RenderOuter(F32 offset, S32 healthMax, S32 stage)
{
    F32 xywh[4] = { 0.0f, -0.41f, 0.0135f * healthMax, 0.08f };
    F32 uv_beg[2] = { 0.0f, 0.0f };
    F32 uv_end[2] = { 1.0f, 1.0f };

    // Both arms are equal in retail too.
    if (stage == 3)
    {
        xywh[0] = 0.04f;
    }
    else
    {
        xywh[0] = 0.04f;
    }
    RenderBar(runtime.rast_outer, offset, xywh, uv_beg, uv_end);
}

void NMEHealthMeter::MeterData::RenderLogo(F32 offset)
{
    F32 xywh[4];
    F32 uv_beg[2] = { 1.0f, 0.0f };
    F32 uv_end[2] = { 0.0f, 1.0f };

    xywh[0] = offset - 0.175f;
    xywh[1] = -0.316f;
    xywh[2] = 0.175f;
    xywh[3] = 0.175f;
    RenderBar(runtime.rast_logo, 0.0f, xywh, uv_beg, uv_end);
}

void NMEHealthMeter::MeterData::RenderBars(F32 offset, S32 healthMax, S32 stage)
{
    F32 xywh[4] = { 0.0f, 0.0f, 0.0f, 0.08f };
    F32 uv_beg[2] = { 0.0f, 0.0f };
    F32 uv_end[2] = { 1.0f, 1.0f };
    F32 uv_beg_rhs[2] = { 1.0f, 0.0f };
    F32 uv_end_rhs[2] = { 0.0f, 1.0f };

    if (stage == 3)
    {
        xywh[0] = 0.0f;
        xywh[1] = -0.41f;
        xywh[2] = 0.04f;
        RenderBar(runtime.rast_solid_bar, offset, xywh, uv_beg_rhs, uv_end_rhs);

        xywh[0] = 0.0135f * healthMax + 0.04f;
        xywh[1] = -0.41f;
        xywh[2] = 0.04f;
        RenderBar(runtime.rast_broken_bar, offset, xywh, uv_beg, uv_end);
    }
    else if (stage == 1)
    {
        xywh[0] = 0.0135f * healthMax + 0.04f;
        xywh[1] = -0.41f;
        xywh[2] = 0.04f;
        RenderBar(runtime.rast_solid_bar, offset, xywh, uv_beg, uv_end);
    }
    else
    {
        xywh[0] = 0.0135f * healthMax + 0.04f;
        xywh[1] = -0.41f;
        xywh[2] = 0.04f;
        RenderBar(runtime.rast_broken_bar, offset, xywh, uv_beg, uv_end);
    }
}

void NMEHealthMeter::MeterData::RenderBar(RwRaster* rast, F32 xoffset, const F32* xywh,
                                          const F32* uv_beg, const F32* uv_end, xColor_tag c1,
                                          xColor_tag c2)
{
    RxObjSpace3DVertex vtxbuf[4];
    RxObjSpace3DVertex* vtx_horz;
    S32 i;
    xColor_tag currColor;
    F32 rat;
    F32 u;

    vtx_horz = vtxbuf;

    for (i = 0; i < 2; i++)
    {
        currColor = (i == 0) ? c1 : c2;

        rat = i;
        u = LERP(1.0f - rat, uv_beg[0], uv_end[0]);

        while (u > 1.0f)
        {
            u -= 1.0f;
        }

        RwIm3DVertexSetPos(&vtx_horz[0], -(xoffset + xywh[0] + rat * xywh[2]) * 1.3f,
                           xywh[1] * 1.74f, 1.0f);
        RwIm3DVertexSetRGBA(&vtx_horz[0], currColor.r, currColor.g, currColor.b, currColor.a);
        vtx_horz[0].u = u;
        vtx_horz[0].v = uv_beg[1];

        RwIm3DVertexSetPos(&vtx_horz[1], -(xoffset + xywh[0] + rat * xywh[2]) * 1.3f,
                           (xywh[1] - xywh[3]) * 1.74f, 1.0f);
        RwIm3DVertexSetRGBA(&vtx_horz[1], currColor.r, currColor.g, currColor.b, currColor.a);
        vtx_horz[1].u = u;
        vtx_horz[1].v = uv_end[1];

        vtx_horz += 2;
    }

    U32 drawflag;
    if (rast)
    {
        drawflag = rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA;
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, rast);
    }
    else
    {
        drawflag = rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA;
    }

    RwIm3DTransform(vtxbuf, 4, NULL, drawflag);
    RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
    RwIm3DEnd();
}
