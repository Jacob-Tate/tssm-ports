#include "xFXHighDynamicRange.h"
#include "xDebug.h"
#include "xMath.h"
#include "iDraw.h"
#include "zRenderState.h"
#include "zMain.h"
#include "zGlobals.h"
#include "zGameState.h"
#include "zMenu.h"

#include <dolphin/gx.h>
#include <dolphin/gx/GXCpu2Efb.h>
#include <dolphin/mtx.h>

#ifdef PLATFORM_PC
#include "iHDRGlow.h"
#include "iScreen.h"
#endif

U8 HDR_brightening;

namespace
{
    S32 background_glow_normal;
    S32 background_darken_normal;
    interpolator background_glow;
    interpolator background_darken;

    namespace tweak
    {
        F32 overbrighten = 3.0f;
        F32 overbrighten_decay = 0.3f;
        S32 downsamples = 4;
        bool blur_faster = true;
    }; // namespace tweak

    bool enabled = true;
} // namespace

S32 hdr_dump_efb;
GXBlendFactor hdr_src = GX_BL_SRCALPHA;
GXBlendFactor hdr_dst = GX_BL_ONE;
GXBlendFactor hdr_down_src = GX_BL_SRCALPHA;
GXBlendFactor hdr_down_dst = GX_BL_ZERO;

void* texData[8];
U16 sizes[9][2] = { { 256, 240 }, { 128, 128 }, { 64, 64 }, { 32, 32 }, { 16, 16 },
                    { 8, 8 },     { 4, 4 },     { 2, 2 },   { 1, 1 } };

namespace
{
    inline void set_vert(RwIm2DVertex& vert, F32 x, F32 y, F32 nsz, xColor_tag c)
    {
        RwIm2DVertexSetScreenX(&vert, x);
        RwIm2DVertexSetScreenY(&vert, y);
        RwIm2DVertexSetScreenZ(&vert, nsz);
        RwIm2DVertexSetU(&vert, 0.0f, 0.0f);
        RwIm2DVertexSetV(&vert, 0.0f, 0.0f);
        RwIm2DVertexSetIntRGBA(&vert, c.r, c.g, c.b, c.a);
    }
} // namespace

void xFXHighDynamicRangeInit()
{
    xFXHighDynamicRangeSetBackgroundDefault(10, 80, false);
    xFXHighDynamicRangeReset();
}

void xFXHighDynamicRangeSceneEnter()
{
}

void xFXHighDynamicRangeSceneExit()
{
}

void xFXHighDynamicRangeReset()
{
    enabled = true;
    background_glow.set(background_glow_normal);
    background_darken.set(background_darken_normal);
}

void xFXHighDynamicRangeStartRender()
{
    if (!enabled)
    {
        return;
    }

    HDR_brightening = FALSE;

    U32 mask = lastSetFBMSK;
    iDrawSetFBMSK(0x00FFFFFF, 0xFFFFFFFF, true);
    RwGameCubeSetAlphaCompare(7, 0, 0, 6, 0);
    _rwDlRenderStateSetZCompLoc(FALSE);
    zRenderState(SDRS_Fill);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);

    F32 nsz = RwIm2DGetNearScreenZ();
    xColor_tag color;
    color.r = 0;
    color.g = 0;
    color.b = 0;
    color.a = MAX(1, background_glow.value);

    RwIm2DVertex verts[4];
    set_vert(verts[0], 0.0f, 0.0f, nsz, color);
    set_vert(verts[1], 0.0f, FB_YRES, nsz, color);
    set_vert(verts[2], FB_XRES, 0.0f, nsz, color);
    set_vert(verts[3], FB_XRES, FB_YRES, nsz, color);
#ifdef PLATFORM_PC
    // PC: the glow's alpha covers the whole screen, not the 4:3 UI box
    // (iScreen.h); boxed, the sides of the picture would get no glow.
    iScreen2DScreenSpaceBegin();
#endif
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, verts, 4);
#ifdef PLATFORM_PC
    iScreen2DScreenSpaceEnd();
#endif

    iDrawSetFBMSK(mask, 0xFFFFFFFF, true);
    zRenderState(SDRS_HDRBeginRender);
}

void xFXHighDynamicRangeRender(RwCamera* camera)
{
    if (!enabled || globals.inLoadingScreen ||
        (!zMenuRunning() && zGameGetOstrich() != eGameOstrich_InScene))
    {
        return;
    }

    zRenderState(SDRS_Fill);

#ifdef PLATFORM_PC
    // PLATFORM_PC: the body below is GX -- copies out of the EFB, a bright pass drawn in the
    // EFB's top-left corner through five TEV stages, and the result added back over the frame --
    // which a host does not have. iHDRGlowRender draws the same passes with render targets.
    iHDRGlowRender(camera, tweak::overbrighten, tweak::downsamples, hdr_down_src, hdr_down_dst,
                   hdr_src, hdr_dst);
    hdr_dump_efb = 0;
#else
    U16 curSizeX = FB_XRES / 2;
    U16 curSizeY = FB_YRES / 2;

    iXFBreset();
    for (S32 i = 0; i < 8; i++)
    {
        texData[i] = iXFBalloc(sizes[i][0] * sizes[i][1] * 4);
    }

    // Half-resolution copy of the frame buffer.
    GXSetTexCopySrc(0, 0, FB_XRES, FB_YRES);
    GXSetTexCopyDst(curSizeX, curSizeY, GX_TF_RGBA8, GX_TRUE);
    GXCopyTex(texData[0], GX_FALSE);
    GXPixModeSync();

    // Save the corner of the frame buffer that the downsample passes draw over.
    void* frame = iXFBalloc(128 * 128 * 4);
    GXSetTexCopySrc(0, 0, 128, 128);
    GXSetTexCopyDst(128, 128, GX_TF_RGBA8, GX_FALSE);
    GXCopyTex(frame, GX_FALSE);
    GXPixModeSync();

    Mtx44 mProjection ALIGNB(16);
    C_MTXOrtho(mProjection, 0.0f, FB_YRES, 0.0f, FB_XRES, 0.0f, 1.0f);
    GXSetProjection(mProjection, GX_ORTHOGRAPHIC);
    GXSetCurrentMtx(GX_PNMTX0);

    Mtx44 mIdentity ALIGNB(16);
    PSMTXIdentity(mIdentity);
    GXLoadPosMtxImm(mIdentity, GX_PNMTX0);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetNumChans(0);
    RwGameCubeSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetCullMode(GX_CULL_NONE);
    GXSetNumTevStages(5);

    GXColor colorize;
    colorize.r = 0x33;
    colorize.g = 0x33;
    colorize.b = 0x33;
    colorize.a = 0xFF;
    F32 overbrighten = tweak::overbrighten;
    F32 alpha = 0.8f * (3.0f * overbrighten) + 0.2f;
    overbrighten *= tweak::overbrighten_decay;
    if (overbrighten < 0.0f)
    {
        overbrighten = 0.0f;
    }
    U8 ualpha = 255.0f * alpha;
    colorize.r = ualpha;
    colorize.g = ualpha;
    colorize.b = ualpha;
    colorize.a = ualpha;
    GXSetTevColor(GX_TEVREG0, colorize);

    for (S32 i = 0; i < 5; i++)
    {
        GXSetTevColorIn((GXTevStageID)i, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_CPREV);
        GXSetTevColorOp((GXTevStageID)i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                        GX_TEVPREV);
        GXSetTevAlphaIn((GXTevStageID)i, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_APREV);
        GXSetTevAlphaOp((GXTevStageID)i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                        GX_TEVPREV);
        GXSetTevOrder((GXTevStageID)i, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    }
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);

    GXSetBlendMode(GX_BM_BLEND, hdr_down_src, hdr_down_dst, GX_LO_SET);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX1, GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX2, GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX3, GX_FALSE, GX_PTIDENTITY);
    GXSetViewport(0.0f, 0.0f, FB_XRES, FB_YRES, 0.0f, 1.0f);
    GXSetScissor(0, 0, FB_XRES, FB_YRES);

    GXTexObj texObject;
    GXInitTexObj(&texObject, texData[0], curSizeX, curSizeY, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXSetAlphaUpdate(GX_TRUE);
    GXPokeAlphaRead(GX_READ_NONE);

    // Blur and downsample into the top left corner, halving the size each pass.
    curSizeX = 128;
    curSizeY = 128;
    for (S32 i = 1; i < tweak::downsamples; i++)
    {
        GXLoadTexObj(&texObject, GX_TEXMAP0);

        // Texture matrices offsetting the sample by a fraction of a texel in each diagonal
        // direction. Retail only enables one texgen and every stage reads GX_TEXCOORD0, so
        // these offsets never reach the TEV.
        // not in the DWARF: GameCube texture-matrix setup, which the PS2 build does not have.
        F32 isizex = 2.5f / curSizeX;
        F32 isizey = 2.5f / curSizeY;
        F32 matrix[2][4] = { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } };

        matrix[0][3] = -isizex;
        matrix[1][3] = -isizey;
        GXLoadTexMtxImm(matrix, GX_TEXMTX0, GX_MTX2x4);

        matrix[0][3] = isizex;
        matrix[1][3] = -isizey;
        GXLoadTexMtxImm(matrix, GX_TEXMTX1, GX_MTX2x4);

        matrix[0][3] = -isizex;
        matrix[1][3] = isizey;
        GXLoadTexMtxImm(matrix, GX_TEXMTX2, GX_MTX2x4);

        matrix[0][3] = isizex;
        matrix[1][3] = isizey;
        GXLoadTexMtxImm(matrix, GX_TEXMTX3, GX_MTX2x4);

        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
        GXPosition2f32(0.0f, 0.0f);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition2f32(0.0f, curSizeY);
        GXTexCoord2f32(0.0f, 1.0f);
        GXPosition2f32(curSizeX, 0.0f);
        GXTexCoord2f32(1.0f, 0.0f);
        GXPosition2f32(curSizeX, curSizeY);
        GXTexCoord2f32(1.0f, 1.0f);
        GXEnd();

        alpha = 0.8f * (3.0f * overbrighten) + 0.2f;
        overbrighten *= tweak::overbrighten_decay;
        if (overbrighten < 0.0f)
        {
            overbrighten = 0.0f;
        }
        ualpha = 255.0f * alpha;
        colorize.r = ualpha;
        colorize.g = ualpha;
        colorize.b = ualpha;
        colorize.a = ualpha;
        GXSetTevColor(GX_TEVREG0, colorize);

        GXSetTexCopySrc(0, 0, curSizeX, curSizeY);
        GXSetTexCopyDst(curSizeX, curSizeY, GX_TF_RGBA8, GX_FALSE);
        GXCopyTex(texData[i], GX_FALSE);
        GXPixModeSync();
        GXInvalidateTexAll();
        GXInitTexObj(&texObject, texData[i], curSizeX, curSizeY, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP,
                     GX_FALSE);

        curSizeX >>= 1;
        curSizeY >>= 1;
        GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_SET);
    }

    GXInitTexObj(&texObject, texData[1], sizes[1][0], sizes[1][1], GX_TF_RGBA8, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXSetViewport(0.0f, 0.0f, FB_XRES, FB_YRES, 0.0f, 1.0f);
    GXSetScissor(0, 0, FB_XRES, FB_YRES);

    // Restore the corner of the frame buffer.
    GXTexObj texObject2;
    GXInitTexObj(&texObject2, frame, 128, 128, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXSetNumTexGens(1);
    GXSetNumTevStages(1);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXLoadTexObj(&texObject2, GX_TEXMAP0);

    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
    GXPosition2f32(0.0f, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition2f32(0.0f, 128.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXPosition2f32(128.0f, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition2f32(128.0f, 128.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXEnd();

    // Add the blurred glow over the whole screen.
    GXSetAlphaUpdate(GX_FALSE);
    GXLoadTexObj(&texObject, GX_TEXMAP0);
    GXSetBlendMode(GX_BM_BLEND, hdr_src, hdr_dst, GX_LO_SET);

    colorize.r = 0x80;
    colorize.g = 0x80;
    colorize.b = 0x80;
    colorize.a = 0xFF;
    GXSetTevColor(GX_TEVREG0, colorize);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
    GXPosition2f32(0.0f, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition2f32(0.0f, FB_YRES);
    GXTexCoord2f32(0.0f, 1.0f);
    GXPosition2f32(FB_XRES, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition2f32(FB_XRES, FB_YRES);
    GXTexCoord2f32(1.0f, 1.0f);
    GXEnd();

    hdr_dump_efb = 0;
#endif
}

U32 xFXHighDynamicRangeBrightenPtank(RpAtomic* ptank, bool bright)
{
    return 0;
}

void xFXHighDynamicRangeRestorePtank(RpAtomic* ptank, U32 oldtest)
{
}

void xFXHighDynamicRangeTunePalette(RwRaster* raster, F32 base_alpha, bool additive)
{
}

void xFXHighDynamicRangeUpdate(F32 dt)
{
    background_glow.update(dt);
    background_darken.update(dt);
}

bool xFXHighDynamicRangeEnable(bool tmpEnable)
{
    return enabled = tmpEnable;
}

S32 xFXHighDynamicRangeGetBackgroundGlow()
{
    return background_glow.value;
}

void xFXHighDynamicRangeSetConfiguration(const xFXHighDynamicRangeConfiguration& configuration)
{
    background_glow.value = configuration.glow;
    background_darken.value = configuration.darken;
    tweak::overbrighten = configuration.overbrighten;
    tweak::overbrighten_decay = configuration.overbrighten_decay;
    tweak::downsamples = configuration.downsamples;
    tweak::blur_faster = configuration.blur_faster;
}

void xFXHighDynamicRangeGetConfiguration(xFXHighDynamicRangeConfiguration& configuration)
{
    configuration.glow = background_glow.value;
    configuration.darken = background_darken.value;
    configuration.overbrighten = tweak::overbrighten;
    configuration.overbrighten_decay = tweak::overbrighten_decay;
    configuration.downsamples = tweak::downsamples;
    configuration.blur_faster = tweak::blur_faster;
}

void xFXHighDynamicRangeSetGlowClamp(S32 glow_min, bool dest_test)
{
    RwGameCubeSetAlphaCompare(7, 0, 1, 6, CLAMP(glow_min, 0, 255));
    _rwDlRenderStateSetZCompLoc(FALSE);
}

void xFXHighDynamicRangeResetGlowClamp()
{
}

void xFXHighDynamicRangeSetBackgroundDefault(S32 glow, S32 darken, bool apply)
{
    background_glow_normal = CLAMP(glow, 0, 255);
    background_darken_normal = CLAMP(darken, 0, 255);

    if (apply)
    {
        background_glow.set(background_glow_normal);
        background_darken.set(background_darken_normal);
    }
}
