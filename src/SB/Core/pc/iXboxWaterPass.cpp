// The Xbox water's refraction and reflection targets and passes
// (iXboxWaterPass.h). PC only, compiled with the game units because it draws
// the game's models and calls its render state.

#include "iXboxWaterPass.h"

#include "iDraw.h"
#include "iFrameGrab.h"
#include "iScreen.h"
#include "iXboxWater.h"
#include "iXboxWaterGrid.h"
#include "rw/screenpass.h"

#include "xEnt.h"
#include "xFX.h"
#include "xModel.h"
#include "xSkyDome.h"
#include "zGlobals.h"
#include "zRenderState.h"

#include <rpworld.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // A pair a body (the Xbox has at most 16), and one spare for
    // TSSM_WATER_VERIFY's second draw.
    const S32 kMaxPairs = 17;

    struct Pair
    {
        xFXCameraTexture refract;
        xFXCameraTexture reflect;
    };

    Pair sPairs[kMaxPairs];
    S32 sPairCount;
    S32 sWidth;
    S32 sHeight;

    // The stand-in pair (iXboxWaterHasStandIn): made with the others, kept
    // while they are.
    const S32 kStandInEdge = 16;
    Pair sStandIn;
    bool sHasStandIn;

    iXboxWaterDrawFn sPartnerDraw;

    // The largest target edge: what a phone's or the Switch's GPU is given is
    // kept smaller (4 MB of colour a target at most).
#if defined(__ANDROID__) || defined(__SWITCH__)
    const S32 kMaxEdge = 1024;
#else
    const S32 kMaxEdge = 2048;
#endif

    // TSSM_WATER_RT=<n>: n x n targets whatever the render size (512 is the
    // Xbox's), for comparisons. 0: unset.
    S32 ForcedSize()
    {
        static S32 sForced = -1;

        if (sForced < 0)
        {
            sForced = 0;
            const char* v = getenv("TSSM_WATER_RT");

            if (v != NULL && v[0] != '\0')
            {
                long n = strtol(v, NULL, 10);

                if (n >= 16 && n <= 4096)
                {
                    sForced = (S32)(n - n % 16);
                }
                else
                {
                    printf("tssm: water: TSSM_WATER_RT=%s is not a size from 16 to 4096; "
                           "the targets follow the render size\n",
                           v);
                }
            }
        }

        return sForced;
    }

    void SizeForScreen(S32* w, S32* h)
    {
        S32 forced = ForcedSize();

        if (forced > 0)
        {
            *w = *h = forced;
            return;
        }

        iXboxWaterTargetSize(iScreenWidth(), iScreenHeight(), kMaxEdge, w, h);
    }

    // 0xCAF80: the scene camera's view offset, clip planes, fog distance,
    // projection, view window and placement.
    void CopyCamera(RwCamera* dst, RwCamera* src)
    {
        RwCameraSetViewOffset(dst, RwCameraGetViewOffset(src));
        RwCameraSetNearClipPlane(dst, RwCameraGetNearClipPlane(src));
        RwCameraSetFarClipPlane(dst, RwCameraGetFarClipPlane(src));
        RwCameraSetFogDistance(dst, RwCameraGetFogDistance(src));
        RwCameraSetProjection(dst, RwCameraGetProjection(src));
        RwCameraSetViewWindow(dst, RwCameraGetViewWindow(src));
        RwFrameTransform(RwCameraGetFrame(dst), RwFrameGetMatrix(RwCameraGetFrame(src)),
                         rwCOMBINEREPLACE);
    }

    // The reflection's trick (0x1AD230, 0x1AD3E0): for its draw a model's
    // matrix is the matrix followed by the mirror, and its atomic's world
    // bounding sphere is moved to match. Both are put back afterwards.
    struct Mirrored
    {
        RwMatrix mat;
        RwSphere sphere;
    };

    void MirrorBegin(xEnt* ent, const xMat4x3& flip, Mirrored* saved)
    {
        RwMatrix* mat = ent->model->Mat;
        RpAtomic* atomic = ent->model->Data;

        saved->mat = *mat;
        saved->sphere = atomic->worldBoundingSphere;

        xMat4x3Mul((xMat4x3*)mat, (const xMat4x3*)&saved->mat, &flip);

        const RwV3d& c = atomic->boundingSphere.center;
        atomic->worldBoundingSphere.center.x =
            mat->right.x * c.x + mat->up.x * c.y + mat->at.x * c.z + mat->pos.x;
        atomic->worldBoundingSphere.center.y =
            mat->right.y * c.x + mat->up.y * c.y + mat->at.y * c.z + mat->pos.y;
        atomic->worldBoundingSphere.center.z =
            mat->right.z * c.x + mat->up.z * c.y + mat->at.z * c.z + mat->pos.z;
    }

    void MirrorEnd(xEnt* ent, const Mirrored& saved)
    {
        *ent->model->Mat = saved.mat;
        ent->model->Data->worldBoundingSphere = saved.sphere;
    }

    bool IsDome(const xEnt* ent)
    {
        return ent->render == xSkyDome_EmptyRender;
    }

    // 0x1ACF80 (and 0x1AD230 with a mirror): the list's sky domes, one at a
    // time, under SDRS_WaterSkyDome.
    void RenderDomes(xEnt** ents, S32 count, const xMat4x3* flip)
    {
        S32 drawn = 0;

        for (S32 i = 0; i < count; i++)
        {
            xEnt* ent = ents[i];

            if (!IsDome(ent))
            {
                continue;
            }

            if (drawn == 0)
            {
                zRenderState(SDRS_WaterSkyDome);
                xSkyDome_BeginRenderSingle();
            }

            Mirrored saved;

            if (flip != NULL)
            {
                MirrorBegin(ent, *flip, &saved);
            }

            xSkyDome_RenderSingle(ent);

            if (flip != NULL)
            {
                MirrorEnd(ent, saved);
            }

            drawn++;
        }

        if (drawn > 0)
        {
            xSkyDome_EndRenderSingle();
        }
    }

    // 0x1ACF30 (and 0x1AD3E0 with a mirror): the list's other models, every
    // instance of each. The refraction's turn their first instance's draw bit
    // on first; the reflection's take them as they are.
    void RenderModels(xEnt** ents, S32 count, const xMat4x3* flip)
    {
        for (S32 i = 0; i < count; i++)
        {
            xEnt* ent = ents[i];

            if (IsDome(ent))
            {
                continue;
            }

            Mirrored saved;

            if (flip != NULL)
            {
                MirrorBegin(ent, *flip, &saved);
            }
            else
            {
                ent->model->Flags |= 0x1;
            }

            for (xModelInstance* m = ent->model; m != NULL; m = m->Next)
            {
                xModelRenderSingle(m);
            }

            if (flip != NULL)
            {
                MirrorEnd(ent, saved);
            }
        }
    }

    // The Xbox's xFXCameraTexture::create (0x134240) makes the camera's
    // raster 8888 (raster type 0x505); the GameCube's leaves the format to the
    // renderer, which on the PC takes the screen's, with no alpha. The
    // refraction's alpha is what the surface writes as its own (the glow's
    // mask), so the water's targets get the Xbox's format.
    bool GiveAlpha(xFXCameraTexture& tv, S32 w, S32 h)
    {
        RwRaster* raster = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);

        if (raster == NULL)
        {
            return false;
        }

        RwRaster* old = tv.raster;
        RwCameraSetRaster(tv.cam, raster);
        RwTextureSetRaster(tv.texture, raster);
        tv.raster = raster;
        RwRasterDestroy(old);
        return true;
    }

    bool MakeTarget(xFXCameraTexture& tv, S32 w, S32 h)
    {
        if (!tv.create(w, h, true, 32, 32))
        {
            return false;
        }

        if (!GiveAlpha(tv, w, h))
        {
            tv.destroy();
            return false;
        }

        RwTextureSetFilterMode(tv.texture, rwFILTERLINEAR);
        RwTextureSetAddressing(tv.texture, rwTEXTUREADDRESSCLAMP);
        return true;
    }

    bool MakePair(Pair& p, S32 w, S32 h)
    {
        memset(&p, 0, sizeof(p));

        if (!MakeTarget(p.refract, w, h))
        {
            memset(&p, 0, sizeof(p));
            return false;
        }

        if (!MakeTarget(p.reflect, w, h))
        {
            p.refract.destroy();
            memset(&p, 0, sizeof(p));
            return false;
        }

        return true;
    }

    void FreePair(Pair& p)
    {
        if (p.refract.cam != NULL)
        {
            p.refract.destroy();
        }

        if (p.reflect.cam != NULL)
        {
            p.reflect.destroy();
        }

        // destroy leaves the texture pointer and the z raster's behind.
        memset(&p, 0, sizeof(p));
    }

    RwRaster* RasterOf(const xFXCameraTexture& tv)
    {
        return tv.cam != NULL ? tv.raster : NULL;
    }
} // namespace

void iXboxWaterSetPartnerDraw(iXboxWaterDrawFn draw)
{
    sPartnerDraw = draw;
}

bool iXboxWaterTargetsMake(S32 pairs)
{
    // TSSM_WATER_FAIL=targets: as if the renderer would not make them, for
    // the fallback's test.
    const char* fail = getenv("TSSM_WATER_FAIL");

    if (pairs > 0 && fail != NULL && strstr(fail, "targets") != NULL)
    {
        iXboxWaterTargetsFree();
        return false;
    }

    S32 w;
    S32 h;
    SizeForScreen(&w, &h);

    pairs = pairs < 0 ? 0 : (pairs > kMaxPairs ? kMaxPairs : pairs);

    if (pairs == sPairCount && w == sWidth && h == sHeight)
    {
        return true;
    }

    iXboxWaterTargetsFree();

    for (S32 i = 0; i < pairs; i++)
    {
        if (!MakePair(sPairs[i], w, h))
        {
            printf("tssm: water: the %dx%d refraction and reflection targets could not be "
                   "made (pair %d of %d)\n",
                   (int)w, (int)h, (int)i + 1, (int)pairs);
            iXboxWaterTargetsFree();
            return false;
        }

        sPairCount = i + 1;
    }

    sWidth = w;
    sHeight = h;

    if (pairs > 0)
    {
        // Without it a body out of view draws its own pair, as before.
        sHasStandIn = MakePair(sStandIn, kStandInEdge, kStandInEdge);

        if (!sHasStandIn)
        {
            printf("tssm: water: the %dx%d stand-in targets could not be made; a water body out "
                   "of view draws its own\n",
                   (int)kStandInEdge, (int)kStandInEdge);
        }

        printf("tssm: water: %d pair%s of %dx%d refraction and reflection targets for a %dx%d "
               "render size\n",
               (int)pairs, pairs == 1 ? "" : "s", (int)w, (int)h, (int)iScreenWidth(),
               (int)iScreenHeight());
    }

    return true;
}

void iXboxWaterTargetsFree()
{
    for (S32 i = 0; i < sPairCount; i++)
    {
        FreePair(sPairs[i]);
    }

    if (sHasStandIn)
    {
        FreePair(sStandIn);
        sHasStandIn = false;
    }

    sPairCount = 0;
    sWidth = 0;
    sHeight = 0;
}

S32 iXboxWaterTargetPairs()
{
    return sPairCount;
}

bool iXboxWaterHasStandIn()
{
    return sHasStandIn;
}

void iXboxWaterTargetSizeNow(S32* w, S32* h)
{
    *w = sWidth;
    *h = sHeight;
}

void iXboxWaterTargetsDraw(S32 pair, const iXboxWaterFacades& f, RwCamera* cam)
{
    const bool standIn = pair == iXBOXWATER_PAIR_STANDIN && sHasStandIn;

    if ((!standIn && (pair < 0 || pair >= sPairCount)) || cam == NULL)
    {
        return;
    }

    Pair& p = standIn ? sStandIn : sPairs[pair];

    // The refraction: no mirror, and the player drawn over the list.
    CopyCamera(p.refract.cam, cam);
    p.refract.begin_render(rwCAMERACLEARIMAGE | rwCAMERACLEARZ, lastSetFBMSK, lastSetFBMSK, 0.0f,
                           0.0f);
    RenderDomes(f.refract, f.refractCount, NULL);
    zRenderState(SDRS_OpaqueModels);
    RenderModels(f.refract, f.refractCount, NULL);

    zPlayer* player = xglobals->___player_ent_dont_use_directly;

    if (player != NULL)
    {
        player->Render();
    }

    if (sPartnerDraw != NULL)
    {
        // SDRS_OpaqueModels set afresh, whatever the player's Render left.
        zRenderStateBegin();
        zRenderState(SDRS_OpaqueModels);
        sPartnerDraw();
    }

    p.refract.end_render();

    // The reflection: every model mirrored about the water's height,
    // y' = 2h - y (0x1AD5F5).
    xMat4x3 flip;
    memset(&flip, 0, sizeof(flip));
    flip.right.x = 1.0f;
    flip.up.y = -1.0f;
    flip.at.z = 1.0f;
    flip.pos.y = 2.0f * f.height;

    CopyCamera(p.reflect.cam, cam);
    p.reflect.begin_render(rwCAMERACLEARIMAGE | rwCAMERACLEARZ, lastSetFBMSK, lastSetFBMSK, 0.0f,
                           0.0f);
    RenderDomes(f.reflect, f.reflectCount, &flip);
    zRenderState(SDRS_OpaqueModels);
    RenderModels(f.reflect, f.reflectCount, &flip);
    p.reflect.end_render();
}

RwRaster* iXboxWaterRefractRaster(S32 pair)
{
    return pair >= 0 && pair < sPairCount ? RasterOf(sPairs[pair].refract) : NULL;
}

RwRaster* iXboxWaterReflectRaster(S32 pair)
{
    return pair >= 0 && pair < sPairCount ? RasterOf(sPairs[pair].reflect) : NULL;
}

RwTexture* iXboxWaterRefractTexture(S32 pair)
{
    return pair >= 0 && pair < sPairCount && sPairs[pair].refract.cam != NULL ?
               sPairs[pair].refract.texture :
               NULL;
}

RwTexture* iXboxWaterReflectTexture(S32 pair)
{
    return pair >= 0 && pair < sPairCount && sPairs[pair].reflect.cam != NULL ?
               sPairs[pair].reflect.texture :
               NULL;
}

void iXboxWaterTargetsDebugDraw(S32 pairs, RwRaster* bump)
{
    if (!iScreenPassReady())
    {
        return;
    }

    pairs = pairs > sPairCount ? sPairCount : pairs;

    // Below the cutscenes' letterbox bar (a tenth of the height), which is
    // drawn after the water.
    const F32 sw = iScreenWidthF();
    const F32 sh = iScreenHeightF();
    const F32 gap = 4.0f;
    const F32 top = sh * 0.1f;
    const F32 tw = sw * 0.24f;
    const F32 th = sh * 0.24f;
    const F32 bs = th * 0.5f;

    iScreenScreenSpace scope;
    iScreenPassState saved;
    iScreenPassBegin(&saved);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    iScreenPassSetWriteMask(ISCREENPASS_WRITE_RGB);

    for (S32 i = 0; i < pairs; i++)
    {
        F32 y = top + i * (th + gap);
        F32 x1 = sw - gap;
        F32 x0 = x1 - tw;
        iScreenPassQuad(RasterOf(sPairs[i].reflect), x0, y, x1, y + th, 0.0f, 0.0f, 1.0f, 1.0f, 255,
                        255, 255, 255);
        x1 = x0 - gap;
        x0 = x1 - tw;
        iScreenPassQuad(RasterOf(sPairs[i].refract), x0, y, x1, y + th, 0.0f, 0.0f, 1.0f, 1.0f, 255,
                        255, 255, 255);

        if (i == 0 && bump != NULL)
        {
            iScreenPassQuad(bump, x0 - gap - bs, y, x0 - gap, y + bs, 0.0f, 0.0f, 1.0f, 1.0f, 255,
                            255, 255, 255);
        }
    }

    iScreenPassEnd(&saved);
}

bool iXboxWaterWriteRaster(const char* path, RwRaster* raster)
{
    return iFrameGrabWriteRaster(path, raster) != FALSE;
}

bool iXboxWaterCompareRasters(RwRaster* a, RwRaster* b, S32* differ, S32* maxDiff)
{
    *differ = 0;
    *maxDiff = 0;

    if (a == NULL || b == NULL || RwRasterGetWidth(a) != RwRasterGetWidth(b) ||
        RwRasterGetHeight(a) != RwRasterGetHeight(b) || RwRasterGetDepth(a) != 32 ||
        RwRasterGetDepth(b) != 32)
    {
        return false;
    }

    const S32 w = RwRasterGetWidth(a);
    const S32 h = RwRasterGetHeight(a);
    U8* pa = RwRasterLock(a, 0, rwRASTERLOCKREAD);

    if (pa == NULL)
    {
        return false;
    }

    const S32 strideA = RwRasterGetStride(a);
    U8* copy = (U8*)malloc((size_t)w * 4 * h);

    if (copy != NULL)
    {
        for (S32 y = 0; y < h; y++)
        {
            memcpy(copy + (size_t)y * w * 4, pa + (size_t)y * strideA, (size_t)w * 4);
        }
    }

    RwRasterUnlock(a);

    if (copy == NULL)
    {
        return false;
    }

    U8* pb = RwRasterLock(b, 0, rwRASTERLOCKREAD);

    if (pb == NULL)
    {
        free(copy);
        return false;
    }

    const S32 strideB = RwRasterGetStride(b);

    for (S32 y = 0; y < h; y++)
    {
        const U8* ra = copy + (size_t)y * w * 4;
        const U8* rb = pb + (size_t)y * strideB;

        for (S32 x = 0; x < w * 4; x += 4)
        {
            S32 most = 0;

            for (S32 k = 0; k < 4; k++)
            {
                S32 d = ra[x + k] - rb[x + k];
                d = d < 0 ? -d : d;
                most = d > most ? d : most;
            }

            if (most != 0)
            {
                (*differ)++;
                *maxDiff = most > *maxDiff ? most : *maxDiff;
            }
        }
    }

    RwRasterUnlock(b);
    free(copy);
    return true;
}
