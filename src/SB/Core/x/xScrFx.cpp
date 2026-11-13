#include "xScrFx.h"
#include "xScrPulseGlare.h"
#include "xScreenFade.h"
#include "zGame.h"
#include "zEntPlayerOOBState.h"
#include "xGroup.h"
#include "xstransvc.h"
#include "xString.h"

#include <float.h>
#include "xCam.h"

struct xScrFxLensFlareElement : xDynAsset
{
    U32 textureID;
    RwTexture* texture;
    F32 position;
    F32 width;
    F32 height;
    F32 uv[2][2];
    U32 color;
    U32 mode;
};

struct xScrFxLensFlareSourceAsset : xDynAsset
{
    U32 object;
    U32 elementID;
    xScrFxLensFlareElement** elementArray;
    U32 numElements;
    F32 size;
    F32 intensity;
    F32 coneAngle;
    U32 distanceAttenuationMode;
};

// Dynamic "location" and "pointer" assets that ObjRecurse hands to the lens flare callbacks
// (types 2 and 3). pointer_asset follows the PS2 DWARF; location_asset is the same position-only
// asset zTalkBox.cpp uses.
struct location_asset : xDynAsset
{
    xVec3 loc;
};

struct pointer_asset : xDynAsset
{
    xVec3 loc;
    F32 yaw;
    F32 pitch;
    F32 roll;
};

struct xScrFxLensFlareSource
{
    U32 flags;
    xVec3* pos;
    xVec3* at;
    xScrFxLensFlareSourceAsset* asset;
};

struct xScrFxLensFlareParams
{
    F32 near_dist;
    F32 far_dist;
    F32 near_dist2;
    F32 far_dist2;
};

struct _xFadeData
{
    U8 active;
    U8 hold_at_dest;
    xColor_tag src;
    xColor_tag dest;
    F32 time_passed;
    F32 time_total;
    xColor_tag current_color;
};

struct xGlare
{
    S32 flags;
    xVec3 pos;
    F32 intensity;
    F32 intensityFadeRate;
    F32 lifetime;
    F32 size;
    RwRGBAReal col;
    RwRaster* raster;
};

static _xFadeData mFade;
U8 g_debugRenderSafeArea = FALSE;
static F32 mLetterboxO;
static F32 mLetterboxTO;
static F32 sLetterBoxSize;
static U8 sLetterBoxAlpha = 255;
xVec3 sFullScreenGlareDir = { -0.3f, 1.0f, 0.4f };
F32 sFullScreenGlareIntensity = 0.5f;
RwRGBA sFullScreenGlareColor = { 255, 255, 255, 64 };
S32 sFullScreenGlareEnabled = 0;
U32 sFullScreenGlareTextureID = 0x9F681A2B; // xStrHash("fx_streak2")
RwTexture* sFullScreenGlareTexturePtr = NULL;
xGlare sGlare[10];
cameraFX sCameraFX[10];

// No symbol exists for this helper and its original name is unknown. The PS2 line table puts
// both calls on one source line, and the target keeps the frame in a register across
// _rwFrameSyncDirty, so the frame was passed to an inlined function.
inline void DestroyFlareFrame(RwFrame* frame)
{
    _rwFrameSyncDirty();
    RwFrameDestroy(frame);
}

inline cameraFX* xCameraFXAlloc()
{
    S32 i;
    cameraFX* f;

    for (i = 0; i < 10; i++)
    {
        f = &sCameraFX[i];

        if (f->flags == 0)
        {
            f->flags = 0x1;
            f->elapsedTime = 0.0f;
            f->maxTime = 0.0f;

            return f;
        }
    }

    return NULL;
}

static inline void xScrFXGlareUpdate(F32 dt)
{
    S32 i;

    for (i = 0; i < 10; i++)
    {
        if (sGlare[i].flags != 0)
        {
            sGlare[i].lifetime -= dt;
            sGlare[i].intensity -= dt * sGlare[i].intensityFadeRate;

            if (sGlare[i].lifetime <= 0.0f)
            {
                sGlare[i].flags = 0;
            }
        }
    }
}

static inline U8 InterpCol(F32 t, U8 s, U8 d)
{
    U8 uval;
    F32 val = d - s;
    val *= t;
    val += s;
    uval = (U8)val;
    return uval;
}

static inline void xScrFxFadeUpdate(F32 seconds)
{
    F32 percent;

    if (zGameIsPaused())
    {
        return;
    }

    if (!mFade.active)
    {
        return;
    }

    mFade.time_passed += seconds;

    if (mFade.time_passed >= mFade.time_total)
    {
        if (!mFade.hold_at_dest)
        {
            mFade.active = 0;
        }
        else
        {
            mFade.current_color = mFade.dest;
        }
    }
    else
    {
        percent = mFade.time_passed / mFade.time_total;

        mFade.current_color.r = InterpCol(percent, mFade.src.r, mFade.dest.r);
        mFade.current_color.g = InterpCol(percent, mFade.src.g, mFade.dest.g);
        mFade.current_color.b = InterpCol(percent, mFade.src.b, mFade.dest.b);
        mFade.current_color.a = InterpCol(percent, mFade.src.a, mFade.dest.a);
    }
}

inline void xScrFXGlareInit()
{
    S32 i;

    for (i = 0; i < 10; i++)
    {
        sGlare[i].flags = 0;
    }
}

inline void xCameraFXInit()
{
    S32 i;

    memset(sCameraFX, 0, sizeof(sCameraFX));

    for (i = 0; i < 10; i++)
    {
        sCameraFX[i].flags = 0;
    }
}

void xScrFxInit()
{
    memset(&mFade, 0, sizeof(mFade));
    xScrFXGlareInit();
    iScrFxInit();
    xCameraFXInit();
}

void xScrFxReset()
{
    xScrFXGlareInit();
}

void xScrFxUpdate(RwCamera* cam, F32 dt)
{
    iScrFxBegin();
    xScrFxFadeUpdate(dt);
    xScreenFade::update_all(dt);
    xScrFXGlareUpdate(dt);
    xScrFX_PulseGlaresUpdate(dt);
    iScrFxEnd();
}

static inline void xScrFxFadeRender()
{
    if (!mFade.active)
    {
        return;
    }

    RwVideoMode video_mode;
    RwEngineGetVideoModeInfo(&video_mode, RwEngineGetCurrentVideoMode());
    iScrFxDrawBox(0.0f, 0.0f, video_mode.width, video_mode.height, mFade.current_color.r,
                  mFade.current_color.g, mFade.current_color.b, mFade.current_color.a, true);
}

void xScrFxRender(RwCamera* cam)
{
    iScrFxBegin();
    xScreenFade::render_all();
    xScrFxFadeRender();
    oob_state::fx_render();

    if (g_debugRenderSafeArea)
    {
        xScrFxDrawSafeArea();
    }

    iScrFxEnd();
}

void xScrFxDrawScreenSizeRectangle()
{
    RwVideoMode video_mode;
    RwEngineGetVideoModeInfo(&video_mode, RwEngineGetCurrentVideoMode());
    iScrFxDrawBox(0.0f, 0.0f, video_mode.width, video_mode.height, 0, 0, 0, 255, false);
}

void xScrFxFade(const xColor_tag* base, const xColor_tag* dest, F32 seconds, S32 hold)
{
    if (seconds < 0.0f)
    {
        seconds = 0.0f;
    }

    mFade.active = 1;
    mFade.src = *base;
    mFade.dest = *dest;
    mFade.time_passed = 0.0f;
    mFade.time_total = seconds;
    mFade.hold_at_dest = hold != 0;
}

void xScrFxStopFade()
{
    mFade.active = 0;
}

U8 xScrFxIsFading()
{
    return mFade.active;
}

void xScrFxLetterboxReset()
{
    mLetterboxO = 0.0f;
    mLetterboxTO = 0.0f;
}

void xScrFxLetterBoxSetSize(F32 size)
{
    sLetterBoxSize = size / 480.0f * FB_YRES;
}

void xScrFxLetterBoxSetAlpha(U8 alpha)
{
    sLetterBoxAlpha = alpha;
}

void xScrFxLetterbox(S32 enable)
{
    if (zMenuRunning())
    {
        enable = 0;
    }

    if (enable)
    {
        mLetterboxTO = sLetterBoxSize;
    }
    else
    {
        mLetterboxTO = 0.0f;
    }
}

void xScrFxRenderLetterBox(RwCamera* cam)
{
    F32 y_offset;

    if (xglobals->sceneCur->enableDrawing)
    {
        if (mLetterboxO < mLetterboxTO)
        {
            mLetterboxO += 100.0f * xglobals->update_dt;

            if (mLetterboxO > mLetterboxTO)
            {
                mLetterboxO = mLetterboxTO;
            }
        }
        else if (mLetterboxO > mLetterboxTO)
        {
            mLetterboxO -= 100.0f * xglobals->update_dt;

            if (mLetterboxO < mLetterboxTO)
            {
                mLetterboxO = mLetterboxTO;
            }
        }

        if (mLetterboxO > 0.0f)
        {
            y_offset = mLetterboxO;
            RwVideoMode video_mode;
            RwEngineGetVideoModeInfo(&video_mode, RwEngineGetCurrentVideoMode());
            iScrFxDrawBox(0.0f, 0.0f, video_mode.width, y_offset, 0, 0, 0, sLetterBoxAlpha, false);
            iScrFxDrawBox(0.0f, video_mode.height - y_offset, video_mode.width, video_mode.height,
                          0, 0, 0, sLetterBoxAlpha, false);
        }
    }
}

// Global, not inline (PS2 DWARF, BfBB xScrFx.h): inlined into xScrFxRender, but its stripped
// copy here interns 0.85f where retail's .sdata2 has it.
void xScrFxDrawSafeArea()
{
    RwVideoMode videoMode;
    RwEngineGetVideoModeInfo(&videoMode, RwEngineGetCurrentVideoMode());

    U8 bHiDef = FALSE;

    if (videoMode.flags & (rwVIDEOMODE_XBOX_WIDESCREEN | rwVIDEOMODE_XBOX_PROGRESSIVE))
    {
        bHiDef = TRUE;
    }

    F32 fSafeArea = 0.85f;

    if (bHiDef)
    {
        fSafeArea = 0.9f;
    }

    U32 screenWidth = videoMode.width;
    U32 screenHeight = videoMode.height;
    U32 xMargin = (screenWidth - (U32)(screenWidth * fSafeArea)) / 2;
    U32 yMargin = (screenHeight - (U32)(screenHeight * fSafeArea)) / 2;

    S32 top = yMargin;
    S32 bottom = screenHeight - yMargin;
    S32 left = xMargin;
    S32 right = screenWidth - xMargin;

    F32 width = 2.0f;
    iScrFxDrawBox(left - width, top - width, right + width, top, 255, 0, 0, 255, false);
    iScrFxDrawBox(left - width, bottom, right + width, bottom + width, 255, 0, 0, 255, false);
    iScrFxDrawBox(left - width, top, left, bottom, 255, 0, 0, 255, false);
    iScrFxDrawBox(right, top, right + width, bottom, 255, 0, 0, 255, false);
}

void xScrFXGlareDisable(S32 id)
{
    sGlare[id].flags = 0;
}

S32 xScrFXGlareAdd(const xVec3* pos, F32 life, F32 intensity, F32 size, F32 r, F32 g, F32 b, F32 a,
                   RwRaster* raster)
{
    S32 i;

    for (i = 0; i < 10; i++)
    {
        if (sGlare[i].flags == 0)
        {
            sGlare[i].lifetime = life;
            sGlare[i].intensity = intensity;
            sGlare[i].pos = *pos;
            sGlare[i].flags = 0x1;
            sGlare[i].col.alpha = a;
            sGlare[i].col.red = r;
            sGlare[i].col.green = g;
            sGlare[i].col.blue = b;
            sGlare[i].size = size;
            sGlare[i].intensityFadeRate = sGlare[i].intensity / sGlare[i].lifetime;

            if (!raster)
            {
                RwTexture* texturePtr = (RwTexture*)xSTFindAsset(xStrHash("particle_shine"), NULL);

                if (texturePtr)
                {
                    sGlare[i].raster = RwTextureGetRaster(texturePtr);
                }
                else
                {
                    sGlare[i].raster = NULL;
                }
            }
            else
            {
                sGlare[i].raster = raster;
            }

            return i;
        }
    }

    return -1;
}

void xScrFXGlareChange(S32 id, const xVec3* pos, F32 life, F32 intensity)
{
    sGlare[id].pos = *pos;

    if (life >= 0.0f)
    {
        sGlare[id].lifetime = life;
    }

    if (intensity >= 0.0f)
    {
        sGlare[id].intensity = intensity;
    }
}

U8 xScrFXGlareActive(S32 id)
{
    return sGlare[id].flags != 0;
}

void xScrFXFullScreenGlareRender()
{
    if (!sFullScreenGlareEnabled)
    {
        return;
    }

    xMat4x3 mat;
    iCamGetViewMatrix(xglobals->screen->icam, &mat);

    xVec3 v;
    xVec3Copy(&v, &mat.at);
    xVec3Normalize(&v, &v);

    xVec3 glareDir;
    xVec3Normalize(&glareDir, &sFullScreenGlareDir);

    F32 dp = glareDir.dot(v);

    F32 d = dp;
    d = 0.5f + d;
    d *= d * d * d;
    d *= sFullScreenGlareIntensity;

    if (d == 0.0f)
    {
        return;
    }

    RwRGBA color;
    color.red = sFullScreenGlareColor.red;
    color.green = sFullScreenGlareColor.green;
    color.blue = sFullScreenGlareColor.blue;

    F32 alpha = sFullScreenGlareColor.alpha * d;
    if (alpha > 255.0f)
    {
        alpha = 255.0f;
    }
    color.alpha = alpha;

    if (!sFullScreenGlareTexturePtr)
    {
        sFullScreenGlareTexturePtr = (RwTexture*)xSTFindAsset(sFullScreenGlareTextureID, NULL);
    }

    if (sFullScreenGlareTexturePtr)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER,
                         (void*)RwTextureGetRaster(sFullScreenGlareTexturePtr));
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

    xScrFxDrawBox(0.0f, 0.0f, FB_XRES, FB_YRES, color.red, color.green, color.blue, color.alpha, dp,
                  0.0f);
}

void xScrFXGlareRender(const xMat4x3& mat)
{
    S32 i;

    for (i = 0; i < 10; i++)
    {
        xGlare* g = &sGlare[i];

        if (g->flags != 0)
        {
            xVec3 w;
            xVec3 h;

            h = mat.right;
            w = mat.up;
            xVec3Normalize(&h, &h);
            xVec3Normalize(&w, &w);
            h *= 0.5f * g->size;
            w *= 0.5f * g->size;

            xVec3 v;
            xVec3Copy(&v, &mat.at);
            xVec3Normalize(&v, &v);

            xVec3 glareDir;
            glareDir.x = g->pos.x - mat.pos.x;
            glareDir.y = g->pos.y - mat.pos.y;
            glareDir.z = g->pos.z - mat.pos.z;
            xVec3Normalize(&glareDir, &glareDir);

            F32 dot = glareDir.dot(v);
            F32 val = (1.0f + dot) / 2.0f;
            val *= val * val * val;

            F32 glareDirLen = xVec3Length(&glareDir);
            if (glareDirLen != 0.0f)
            {
                val *= g->intensity / glareDirLen;

                if (val != 0.0f)
                {
                    static RxObjSpace3DVertex sStripVert[4];

                    xVec3 toCam = xglobals->cam->mat.pos - g->pos;
                    toCam.normalize();
                    toCam *= 0.5f;
                    g->pos += toCam;

                    RxObjSpace3DVertex* vert = &sStripVert[0];
                    RwIm3DVertexSetPos(vert, g->pos.x - w.x - h.x, g->pos.y - w.y - h.y,
                                       g->pos.z - w.z - h.z);
                    RwIm3DVertexSetUV(vert, 0.0f, 0.0f);
                    RwIm3DVertexSetRGBA(vert, g->col.red, g->col.green, g->col.blue,
                                        g->col.alpha * val);
                    vert++;

                    RwIm3DVertexSetPos(vert, g->pos.x - w.x + h.x, g->pos.y - w.y + h.y,
                                       g->pos.z - w.z + h.z);
                    RwIm3DVertexSetUV(vert, 0.0f, 1.0f);
                    RwIm3DVertexSetRGBA(vert, g->col.red, g->col.green, g->col.blue,
                                        g->col.alpha * val);
                    vert++;

                    RwIm3DVertexSetPos(vert, g->pos.x + w.x - h.x, g->pos.y + w.y - h.y,
                                       g->pos.z + w.z - h.z);
                    RwIm3DVertexSetUV(vert, 1.0f, 0.0f);
                    RwIm3DVertexSetRGBA(vert, g->col.red, g->col.green, g->col.blue,
                                        g->col.alpha * val);
                    vert++;

                    RwIm3DVertexSetPos(vert, g->pos.x + w.x + h.x, g->pos.y + w.y + h.y,
                                       g->pos.z + w.z + h.z);
                    RwIm3DVertexSetUV(vert, 1.0f, 1.0f);
                    RwIm3DVertexSetRGBA(vert, g->col.red, g->col.green, g->col.blue,
                                        g->col.alpha * val);

                    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g->raster);

                    if (RwIm3DTransform(sStripVert, 4, NULL,
                                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
                    {
                        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                        RwIm3DEnd();
                    }
                }
            }
        }
    }
}

void xScrFxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, U8 red, U8 green, U8 blue, U8 alpha, F32 ushift,
                   F32 vshift)
{
    F32 oocameraNearClipPlane = RwIm2DGetNearScreenZ();

    if (alpha == 0)
    {
        return;
    }

    static RwIm2DVertex v[4];
    static RwImVertexIndex indices[4] = { 0, 2, 1, 3 };

    RwIm2DVertexSetScreenX(&v[0], x1);
    RwIm2DVertexSetScreenY(&v[0], y1);
    RwIm2DVertexSetScreenX(&v[1], x2);
    RwIm2DVertexSetScreenY(&v[1], y1);
    RwIm2DVertexSetScreenX(&v[2], x1);
    RwIm2DVertexSetScreenY(&v[2], y2);
    RwIm2DVertexSetScreenX(&v[3], x2);
    RwIm2DVertexSetScreenY(&v[3], y2);

    RwIm2DVertexSetScreenZ(&v[0], oocameraNearClipPlane);
    RwIm2DVertexSetScreenZ(&v[1], oocameraNearClipPlane);
    RwIm2DVertexSetScreenZ(&v[2], oocameraNearClipPlane);
    RwIm2DVertexSetScreenZ(&v[3], oocameraNearClipPlane);

    RwIm2DVertexSetIntRGBA(&v[0], red, green, blue, alpha);
    RwIm2DVertexSetIntRGBA(&v[1], red, green, blue, alpha);
    RwIm2DVertexSetIntRGBA(&v[2], red, green, blue, alpha);
    RwIm2DVertexSetIntRGBA(&v[3], red, green, blue, alpha);

    RwIm2DVertexSetU(&v[0], ushift, 0);
    RwIm2DVertexSetV(&v[0], vshift, 0);
    RwIm2DVertexSetU(&v[1], 1.0f + ushift, 0);
    RwIm2DVertexSetV(&v[1], vshift, 0);
    RwIm2DVertexSetU(&v[2], ushift, 0);
    RwIm2DVertexSetV(&v[2], 1.0f + vshift, 0);
    RwIm2DVertexSetU(&v[3], 1.0f + ushift, 0);
    RwIm2DVertexSetV(&v[3], 1.0f + vshift, 0);

    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, v, 4, indices, 4);
}

// The camera FX and lens flare data sit with their functions: retail's .bss/.sdata place
// xScrFXGlareRender's sStripVert and xScrFxDrawBox's v/indices before sCameraFXMatOld and
// sLensFlareParams (local statics take their slot when their function is parsed).
static xMat4x3 sCameraFXMatOld;
void xCameraFXZoomUpdate(cameraFX* f, F32 dt, const xMat4x3*, xMat4x3* m);
void xCameraFXShakeUpdate(cameraFX* f, F32 dt, const xMat4x3*, xMat4x3* m);

cameraFXTableEntry sCameraFXTable[3] = { { CAMERAFX_TYPE_NONE, NULL, NULL },
                                         { CAMERAFX_TYPE_ZOOM, xCameraFXZoomUpdate, NULL },
                                         { CAMERAFX_TYPE_SHAKE, xCameraFXShakeUpdate, NULL } };

namespace
{
    U8 anycameraFXActive;
}

static const F32 SHAKE_FOREVER = FLT_MAX;

void xCameraFXBegin(const xMat4x3& mat)
{
    xMat4x3Identity(&sCameraFXMatOld);
    xMat4x3Copy(&sCameraFXMatOld, &mat);
}

void xCameraFXKill()
{
    S32 i;

    for (i = 0; i < 10; i++)
    {
        if (sCameraFX[i].flags & 0x1)
        {
            sCameraFX[i].flags |= 0x2;
        }
    }
}

void xCameraFXZoomUpdate(cameraFX* f, F32 dt, const xMat4x3*, xMat4x3* m)
{
    switch (f->zoom.mode)
    {
    case CAMERAFX_ZOOM_MODE_0:
    {
        f->zoom.velCur += f->zoom.accel * dt;
        f->zoom.distanceCur += f->zoom.velCur * dt;

        if (f->zoom.distanceCur >= f->zoom.distance)
        {
            f->zoom.distanceCur = f->zoom.distance;
            f->zoom.mode = CAMERAFX_ZOOM_MODE_2;
            f->zoom.holdTimeCur = 0.0f;
        }

        xMat4x3MoveLocalAt(m, f->zoom.distanceCur);

        break;
    }
    case CAMERAFX_ZOOM_MODE_2:
    {
        f->zoom.holdTimeCur += dt;

        if (f->zoom.holdTimeCur > f->zoom.holdTime)
        {
            f->zoom.mode = CAMERAFX_ZOOM_MODE_1;
            f->zoom.distanceCur = f->zoom.distance;
            f->zoom.velCur = f->zoom.vel;
        }

        xMat4x3MoveLocalAt(m, f->zoom.distance);

        break;
    }
    case CAMERAFX_ZOOM_MODE_1:
    {
        f->zoom.velCur += f->zoom.accel * dt;
        f->zoom.distanceCur -= f->zoom.velCur * dt;

        if (f->zoom.distanceCur <= 0.0f)
        {
            f->zoom.distanceCur = 0.0f;
            f->zoom.mode = CAMERAFX_ZOOM_MODE_3;
            f->flags |= 0x2;
        }

        xMat4x3MoveLocalAt(m, f->zoom.distanceCur);

        break;
    }
    case CAMERAFX_ZOOM_MODE_3:
    {
        break;
    }
    }
}

cameraFX* xCameraFXShakeForever(F32 magnitude, F32 cycleMax, F32 rotate_magnitude, F32 radius,
                                const xVec3* epicenter, const xVec3* player, bool shakeY)
{
    return xCameraFXShake(SHAKE_FOREVER, magnitude, cycleMax, rotate_magnitude, radius, epicenter,
                          player, shakeY);
}

void xCameraFXShakeEnd(cameraFX* fx, F32 fadeOutTime)
{
    fx->maxTime = fadeOutTime;
    fx->elapsedTime = 0.0f;
    fx->shake.dampenRate = (1.0f / fadeOutTime);
}

cameraFX* xCameraFXShake(F32 maxTime, F32 magnitude, F32 cycleMax, F32 rotate_magnitude, F32 radius,
                         const xVec3* epicenter, const xVec3* player, bool shakeY)
{
    cameraFX* f = xCameraFXAlloc();

    if (!f)
    {
        return NULL;
    }

    f->type = CAMERAFX_TYPE_SHAKE;
    f->maxTime = maxTime;
    f->shake.magnitude = magnitude;

    if (shakeY)
    {
        f->shake.dir.x = 0.0f;
        f->shake.dir.y = 1.0f;
    }
    else
    {
        f->shake.dir.x = 1.0f;
        f->shake.dir.y = 0.0f;
    }

    f->shake.cycleMax = cycleMax;
    f->shake.cycleTime = 0.0f;
    f->shake.dampen = 0.0f;
    f->shake.dampenRate = 1.0f / maxTime;
    f->shake.rotate_magnitude = rotate_magnitude;
    f->shake.radius = radius;
    f->shake.epicenterP = epicenter;

    if (f->shake.epicenterP)
    {
        f->shake.epicenter = *f->shake.epicenterP;
    }

    f->shake.player = player;

    return f;
}

void xCameraFXShakeUpdate(cameraFX* f, F32 dt, const xMat4x3*, xMat4x3* m)
{
    F32 x, y, scale, noise, s;
    xVec3 e;

    f->shake.cycleTime += dt;

    while (f->shake.cycleTime > f->shake.cycleMax)
    {
        f->shake.dir.x = -f->shake.dir.x;
        f->shake.dir.y = -f->shake.dir.y;
        f->shake.cycleTime -= f->shake.cycleMax;
    }

    scale = 1.0f;

    if (f->maxTime != SHAKE_FOREVER)
    {
        scale = f->shake.dampenRate * (f->maxTime - f->elapsedTime);
    }

    noise = 0.1f * (xurand() - 0.5f);

    if (f->shake.radius > 0.0f && f->shake.epicenterP)
    {
        xVec3 d;

        if (!f->shake.player)
        {
            xVec3Sub(&d, &sCameraFXMatOld.pos, &f->shake.epicenter);
        }
        else
        {
            xVec3Sub(&d, f->shake.player, &f->shake.epicenter);
        }

        s = d.length();

        if (s > f->shake.radius)
        {
            scale = 0.0f;
        }
        else
        {
            s = 1.0f - s / f->shake.radius;
            scale *= s * s;
        }
    }

    x = (f->shake.magnitude + noise) * f->shake.dir.x * scale / f->shake.cycleMax *
        f->shake.cycleTime * isin(f->shake.cycleTime / f->shake.cycleMax * 3.1415927f);

    noise = 0.1f * (xurand() - 0.5f);

    y = (f->shake.magnitude + noise) * f->shake.dir.y * scale / f->shake.cycleMax *
        f->shake.cycleTime * isin(f->shake.cycleTime / f->shake.cycleMax * 3.1415927f);

    xMat4x3MoveLocalRight(m, x);
    xMat4x3MoveLocalUp(m, y);

    xMat3x3GetEuler(m, &e);

    e.z += f->shake.cycleTime / f->shake.cycleMax * 0.63661975f * 0.1f * scale *
           f->shake.rotate_magnitude;

    xMat3x3Euler(m, &e);
}

void xCameraFXUpdate(xMat4x3& mat, RwCamera* icam, F32 dt)
{
    S32 i;

    if (zGameIsPaused())
    {
        return;
    }

    anycameraFXActive = 0;

    for (i = 0; i < 10; i++)
    {
        if (sCameraFX[i].flags & 0x1)
        {
            sCameraFX[i].elapsedTime += dt;
            anycameraFXActive = 1;

            if ((sCameraFX[i].maxTime > 0.0f && sCameraFX[i].elapsedTime > sCameraFX[i].maxTime) ||
                sCameraFX[i].flags & 0x2)
            {
                sCameraFX[i].flags = 0;

                if (sCameraFXTable[sCameraFX[i].type].funcKill)
                {
                    sCameraFXTable[sCameraFX[i].type].funcKill(&sCameraFX[i]);
                }
            }
            else if (sCameraFXTable[sCameraFX[i].type].func)
            {
                sCameraFXTable[sCameraFX[i].type].func(&sCameraFX[i], dt, &sCameraFXMatOld, &mat);
            }
        }
    }

    iCameraUpdatePos(icam, &mat);
}

void xCameraFXEnd(xMat4x3& mat, RwCamera* icam)
{
    mat = sCameraFXMatOld;
    iCameraUpdatePos(icam, &sCameraFXMatOld);
}

static xScrFxLensFlareSource* sFlareSource;
static S32 sNumFlareSources;
static xScrFxLensFlareParams sDefaultLensFlareParams = { 40.0f, 80.0f, 0.0f, 0.0f };
static xScrFxLensFlareParams* sLensFlareParams = &sDefaultLensFlareParams;
static iScrFxLensFlare sFlare[128];
static S32 sNumFlares;
RwTexture* gxFlareTexture;
RwFrame* gxFlareFrame;
S32 dumpFlares;

static S32 ObjRecurse(U32 assetID, void (*callback)(void*, U32, void*), void* data)
{
    S32 count;
    xBase* base;
    xGroup* group;
    U32 numItems;
    U32 i;
    U32 memberID;
    U32 size;
    void* obj;
    xDynAsset* da;

    if (!assetID)
    {
        return 0;
    }

    count = 0;
    base = zSceneFindObject(assetID);

    if (base)
    {
        if (base->baseType == eBaseTypeGroup)
        {
            group = (xGroup*)base;
            numItems = xGroupGetCount(group);

            for (i = 0; i < numItems; i++)
            {
                memberID = xGroupGetItem(group, i);
                count += ObjRecurse(memberID, callback, data);
            }
        }
        else if (base->baseFlags & 0x20)
        {
            if (callback)
            {
                callback(base, 0, data);
            }

            count++;
        }
    }
    else
    {
        obj = xSTFindAsset(assetID, &size);

        if (obj)
        {
            if (size == sizeof(xVec3))
            {
                if (callback)
                {
                    callback(obj, 1, data);
                }

                count++;
            }

            da = (xDynAsset*)obj;

            if (da->type == xStrHash("location"))
            {
                if (callback)
                {
                    callback(obj, 2, data);
                }

                count++;
            }
            else if (da->type == xStrHash("pointer"))
            {
                if (callback)
                {
                    callback(obj, 3, data);
                }

                count++;
            }
            else if (da->type == xStrHash("effect:Lens Flare Element"))
            {
                if (callback)
                {
                    callback(obj, 4, data);
                }

                count++;
            }
        }
    }

    return count;
}

static void LensFlareElementCB(void* obj, U32, void* data)
{
    xScrFxLensFlareSourceAsset* asset = ((xScrFxLensFlareSource*)data)->asset;
    asset->elementArray[asset->numElements] = (xScrFxLensFlareElement*)obj;
    asset->numElements++;
}

static void LensFlareSourceCB(void* obj, U32 type, void* data)
{
    xScrFxLensFlareSource* source = &sFlareSource[sNumFlareSources];

    memset(source, 0, sizeof(xScrFxLensFlareSource));
    source->asset = (xScrFxLensFlareSourceAsset*)data;

    switch (type)
    {
    case 2:
        source->pos = &((location_asset*)obj)->loc;
        break;
    case 3:
        source->pos = &((pointer_asset*)obj)->loc;
        source->at = &((pointer_asset*)obj)->loc;
        break;
    case 1:
        source->pos = (xVec3*)obj;
        break;
    case 0:
        source->pos = &xEntGetFrame((xEnt*)obj)->pos;
        source->at = &xEntGetFrame((xEnt*)obj)->at;
        break;
    default:
        return;
    }

    sNumFlareSources++;

    if (!source->asset->elementArray)
    {
        U32 elementID = source->asset->elementID;
        S32 numElements = ObjRecurse(elementID, NULL, NULL);

        source->asset->elementArray = (xScrFxLensFlareElement**)xMemAlloc(
            gActiveHeap, numElements * sizeof(xScrFxLensFlareElement*), 0);
        ObjRecurse(elementID, LensFlareElementCB, source);

        U32 texID = 0;

        for (S32 i = 0; i < (S32)source->asset->numElements; i++)
        {
            xScrFxLensFlareElement* element = source->asset->elementArray[i];

            if (element->textureID)
            {
                if (!texID)
                {
                    texID = element->textureID;
                }
                else if (element->textureID != texID)
                {
                    element->textureID = 0;
                }
            }
        }

        for (S32 i = 0; i < (S32)source->asset->numElements; i++)
        {
            xScrFxLensFlareElement* element = source->asset->elementArray[i];

            if (!element->texture)
            {
                RwTexture* tex = (RwTexture*)xSTFindAsset(element->textureID, NULL);
                element->texture = tex;

                RwRaster* raster = RwTextureGetRaster(element->texture);
                if (!(RwRasterGetFormat(raster) & rwRASTERFORMATPAL8) ||
                    !(RwRasterGetFormat(raster) & rwRASTERFORMAT8888))
                {
                    S32 width = RwRasterGetWidth(raster);
                    S32 height = RwRasterGetHeight(raster);
                    RwRaster* newRaster;

                    RwImage* image = RwImageCreate(width, height, 32);
                    RwImageAllocatePixels(image);
                    RwImageSetFromRaster(image, raster);

                    S32 format = RwRasterGetFormat(raster) | RwRasterGetType(raster);
                    format &= ~(RwRasterGetFormat(raster) & rwRASTERFORMATPIXELFORMATMASK);
                    format |= rwRASTERFORMAT8888 | rwRASTERFORMATPAL8;

                    newRaster = RwRasterCreate(width, height, 0, format);

                    if (newRaster)
                    {
                        RwRasterSetFromImage(newRaster, image);
                        RwTextureSetRaster(tex, newRaster);
                        RwRasterDestroy(raster);
                    }

                    RwImageDestroy(image);
                }
            }
        }
    }
}

static inline void xScrFxLensFlare_Add(const xVec3* pos, const xVec3* at, F32 intensity,
                                       xScrFxLensFlareSource* source)
{
    iScrFxLensFlare* flare;

    if (sNumFlares < 128 && source)
    {
        flare = &sFlare[sNumFlares];
        flare->pos = *pos;

        if (at)
        {
            flare->flags |= 0x1;
            flare->at = *at;
        }

        flare->intensity = intensity;
        flare->source = source;
        sNumFlares++;
    }
}

static inline void xScrFxLensFlare_Add(xScrFxLensFlareSource* source)
{
    xScrFxLensFlare_Add(source->pos, source->at, source->asset->intensity, source);
}

void xScrFxLensFlare_SceneEnter()
{
    S32 numDyns = xSTAssetCountByType('DYNA');
    S32 i;
    xDynAsset* dyn;

    sNumFlareSources = 0;
    gxFlareFrame = RwFrameCreate();
    sLensFlareParams = &sDefaultLensFlareParams;

    for (i = 0; i < numDyns; i++)
    {
        dyn = (xDynAsset*)xSTFindAssetByType('DYNA', i, NULL);

        if (dyn->type == 0xB7BA72D5) // xStrHash("effect:Lens Flare Params")
        {
            sLensFlareParams = (xScrFxLensFlareParams*)(dyn + 1);
        }
    }

    sLensFlareParams->near_dist2 = sLensFlareParams->near_dist * sLensFlareParams->near_dist;
    sLensFlareParams->far_dist2 = sLensFlareParams->far_dist * sLensFlareParams->far_dist;

    {
        S32 numSources = 0;

        for (S32 i = 0; i < numDyns; i++)
        {
            xDynAsset* dyn = (xDynAsset*)xSTFindAssetByType('DYNA', i, NULL);

            if (dyn->type == 0xA072A4DA) // xStrHash("effect:Lens Flare Source")
            {
                if (dyn->version != 3)
                {
                    xSTAssetName(dyn->id);
                }
                else
                {
                    numSources +=
                        ObjRecurse(((xScrFxLensFlareSourceAsset*)dyn)->object, NULL, NULL);
                }
            }
        }

        if (numSources)
        {
            sFlareSource = (xScrFxLensFlareSource*)xMemAlloc(
                gActiveHeap, numSources * sizeof(xScrFxLensFlareSource), 0);

            for (S32 i = 0; i < numDyns; i++)
            {
                xDynAsset* dyn = (xDynAsset*)xSTFindAssetByType('DYNA', i, NULL);

                if (dyn->type == 0xA072A4DA && dyn->version == 3)
                {
                    ObjRecurse(((xScrFxLensFlareSourceAsset*)dyn)->object, LensFlareSourceCB, dyn);
                }
            }
        }
    }

    iScrFxLensFlare_Reset();

    sNumFlares = 0;
    for (i = 0; i < sNumFlareSources; i++)
    {
        xScrFxLensFlare_Add(&sFlareSource[i]);
    }
}

void xScrFxLensFlare_SceneExit()
{
    if (gxFlareFrame)
    {
        DestroyFlareFrame(gxFlareFrame);
        gxFlareFrame = NULL;
    }
}

// The PS2 build's iScrFxLensFlare_Render passes the flare itself as flr; the GameCube one
// passes flare->source, which this function still reads as an iScrFxLensFlare.
static void xScrFxLensFlare_RenderElements(RwRaster*, RwFrame* frame, U8 vCol, void* flr)
{
    RxObjSpace3DVertex verts[4];
    RwImVertexIndex indices[4] = { 0, 1, 3, 2 };
    RxObjSpace3DVertex qverts[9];
    RwImVertexIndex qindices[10] = { 4, 0, 1, 2, 5, 8, 7, 6, 3, 0 };
    RwCamera* camera = RwCameraGetCurrentCamera();
    const RwV2d* vw = RwCameraGetViewWindow(camera);
    RwMatrix invMtx;
    RwMatrix transform;
    RwMatrix tmp;
    RwMatrix* matrix = RwFrameGetLTM(RwCameraGetFrame(camera));
    RwV3d v;
    F32 length;
    RwV3d position;
    RwV3d FlarePosition;
    xScrFxLensFlareElement* ep;
    iScrFxLensFlare* flare;
    xScrFxLensFlareSource* source;
    S32 i;

    if (!gxFlareTexture)
    {
        return;
    }

    RwMatrixInvert(&invMtx, matrix);
    RwMatrixMultiply(&tmp, RwFrameGetLTM(frame), &invMtx);

    FlarePosition.x = tmp.pos.x;
    FlarePosition.y = tmp.pos.y;
    FlarePosition.z = tmp.pos.z;
    if (FlarePosition.z <= 0.0f)
    {
        return;
    }

    RwV3dScaleMacro(&FlarePosition, &FlarePosition, 1.0f / FlarePosition.z);
    v.x = FlarePosition.x;
    v.y = FlarePosition.y;
    v.z = 0.0f;

    length = RwV3dLength(&v);
    if (length > 0.0f)
    {
        RwV3dScaleMacro(&v, &v, 1.0f / length);
    }

    RwIm3DVertexSetPos(&verts[0], -vw->x, vw->y, 1.0f);
    RwIm3DVertexSetPos(&verts[1], -vw->x, -vw->y, 1.0f);
    RwIm3DVertexSetPos(&verts[2], vw->x, -vw->y, 1.0f);
    RwIm3DVertexSetPos(&verts[3], vw->x, vw->y, 1.0f);

    RwIm3DVertexSetPos(&qverts[0], -vw->x, -vw->y, 1.0f);
    RwIm3DVertexSetPos(&qverts[1], 0.0f, -vw->y, 1.0f);
    RwIm3DVertexSetPos(&qverts[2], vw->x, -vw->y, 1.0f);
    RwIm3DVertexSetPos(&qverts[3], -vw->x, 0.0f, 1.0f);
    RwIm3DVertexSetPos(&qverts[4], 0.0f, 0.0f, 1.0f);
    RwIm3DVertexSetPos(&qverts[5], vw->x, 0.0f, 1.0f);
    RwIm3DVertexSetPos(&qverts[6], -vw->x, vw->y, 1.0f);
    RwIm3DVertexSetPos(&qverts[7], 0.0f, vw->y, 1.0f);
    RwIm3DVertexSetPos(&qverts[8], vw->x, vw->y, 1.0f);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)RwTextureGetRaster(gxFlareTexture));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

    flare = (iScrFxLensFlare*)flr;
    source = (xScrFxLensFlareSource*)flare->source;
    vCol = vCol * flare->attenuatedIntensity;

    for (i = 0; i < (S32)source->asset->numElements; i++)
    {
        ep = source->asset->elementArray[i];
        if (!ep)
        {
            continue;
        }

        if ((U8)ep->mode == 0)
        {
            RwIm3DVertexSetUV(&verts[0], ep->uv[1][0], ep->uv[0][1]);
            RwIm3DVertexSetUV(&verts[1], ep->uv[1][0], ep->uv[1][1]);
            RwIm3DVertexSetUV(&verts[2], ep->uv[0][0], ep->uv[1][1]);
            RwIm3DVertexSetUV(&verts[3], ep->uv[0][0], ep->uv[0][1]);

            U8 r = ((ep->color & 0xFF) * vCol) >> 8;
            U8 g = (((ep->color >> 8) & 0xFF) * vCol) >> 8;
            U8 b = (((ep->color >> 16) & 0xFF) * vCol) >> 8;

            RwIm3DVertexSetRGBA(&verts[0], r, g, b, 255);
            RwIm3DVertexSetRGBA(&verts[1], r, g, b, 255);
            RwIm3DVertexSetRGBA(&verts[2], r, g, b, 255);
            RwIm3DVertexSetRGBA(&verts[3], r, g, b, 255);

            position.x = v.x * (ep->position * length);
            position.y = v.y * (ep->position * length);
            position.z = v.z * (ep->position * length);

            RwV3d flareSize;
            flareSize.x = ep->width;
            flareSize.y = ep->height;
            flareSize.z = 1.0f;

            RwMatrixScale(&tmp, &flareSize, rwCOMBINEREPLACE);
            RwMatrixTranslate(&tmp, &position, rwCOMBINEPOSTCONCAT);
            RwMatrixMultiply(&transform, &tmp, matrix);

            RwIm3DTransform(verts, 4, &transform,
                            rwIM3D_VERTEXUV | rwIM3D_ALLOPAQUE | rwIM3D_VERTEXXYZ |
                                rwIM3D_VERTEXRGBA);
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, indices, 4);
            RwIm3DEnd();
        }
        else if ((U8)ep->mode == 1)
        {
            F32 uadj = 0.5f / RwRasterGetWidth(RwTextureGetRaster(gxFlareTexture));
            F32 vadj = 0.5f / RwRasterGetHeight(RwTextureGetRaster(gxFlareTexture));

            RwIm3DVertexSetUV(&qverts[0], ep->uv[0][0] + uadj, ep->uv[0][1] + vadj);
            RwIm3DVertexSetUV(&qverts[1], ep->uv[1][0] - uadj, ep->uv[0][1] + vadj);
            RwIm3DVertexSetUV(&qverts[2], ep->uv[0][0] + uadj, ep->uv[0][1] + vadj);
            RwIm3DVertexSetUV(&qverts[3], ep->uv[0][0] + uadj, ep->uv[1][1] - vadj);
            RwIm3DVertexSetUV(&qverts[4], ep->uv[1][0] - uadj, ep->uv[1][1] - vadj);
            RwIm3DVertexSetUV(&qverts[5], ep->uv[0][0] + uadj, ep->uv[1][1] - vadj);
            RwIm3DVertexSetUV(&qverts[6], ep->uv[0][0] + uadj, ep->uv[0][1] - vadj);
            RwIm3DVertexSetUV(&qverts[7], ep->uv[1][0] - uadj, ep->uv[0][1] - vadj);
            RwIm3DVertexSetUV(&qverts[8], ep->uv[0][0] + uadj, ep->uv[0][1] + vadj);

            U8 r = ((ep->color & 0xFF) * vCol) >> 8;
            U8 g = (((ep->color >> 8) & 0xFF) * vCol) >> 8;
            U8 b = (((ep->color >> 16) & 0xFF) * vCol) >> 8;

            for (S32 j = 0; j < 9; j++)
            {
                RwIm3DVertexSetRGBA(&qverts[j], r, g, b, 255);
            }

            position.x = v.x * (ep->position * length);
            position.y = v.y * (ep->position * length);
            position.z = v.z * (ep->position * length);

            RwV3d flareSize;
            flareSize.x = ep->width;
            flareSize.y = ep->height;
            flareSize.z = 1.0f;

            RwMatrixScale(&tmp, &flareSize, rwCOMBINEREPLACE);
            RwMatrixTranslate(&tmp, &position, rwCOMBINEPOSTCONCAT);
            RwMatrixMultiply(&transform, &tmp, matrix);

            RwIm3DTransform(qverts, 9, &transform,
                            rwIM3D_VERTEXUV | rwIM3D_ALLOPAQUE | rwIM3D_VERTEXXYZ |
                                rwIM3D_VERTEXRGBA);
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRIFAN, qindices, 10);
            RwIm3DEnd();
        }
    }

    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
}

// Debug dump of one flare (a static function in Ratatouille); the printing is compiled out on
// GC. The pointer guard is a reconstruction: retail's dump loops stay plain mtctr/bdnz loops,
// so the body was not empty.
static inline void dumpFlare(iScrFxLensFlare* flare)
{
    if (!flare)
    {
        return;
    }
}

void xScrFxLensFlare_Render(RwCamera* cam)
{
    iScrFxLensFlare* flare;
    RwMatrix* cammat = RwFrameGetMatrix(RwCameraGetFrame(globalCamera));
    xVec3* campos = (xVec3*)RwMatrixGetPos(cammat);
    xVec3* cam_at = (xVec3*)RwMatrixGetAt(cammat);
    S32 i;

    flare = sFlare;
    for (i = 0; i < sNumFlares; i++, flare++)
    {
        flare->dist2 = xVec3Dist2(&flare->pos, campos);
    }

    if (dumpFlares)
    {
        for (i = 0; i < sNumFlares; i++)
        {
            dumpFlare(&sFlare[i]);
        }
    }

    if (dumpFlares)
    {
        for (i = 0; i < sNumFlares; i++)
        {
            dumpFlare(&sFlare[i]);
        }
    }

    flare = sFlare;
    for (i = 0; i < sNumFlares; i++, flare++)
    {
        xScrFxLensFlareSource* source = (xScrFxLensFlareSource*)flare->source;
        F32 intensity = flare->intensity;

        if (intensity == 0.0f)
        {
            continue;
        }

        if (source->asset->distanceAttenuationMode == 1)
        {
            if (flare->dist2 >= sLensFlareParams->far_dist2)
            {
                continue;
            }

            if (flare->dist2 > sLensFlareParams->near_dist2)
            {
                intensity *= (sLensFlareParams->far_dist2 - flare->dist2) /
                             (sLensFlareParams->far_dist2 - sLensFlareParams->near_dist2);
            }
        }

        xVec3 v1;
        xVec3 v2;

        if (flare->flags & 0x1)
        {
            v1 = flare->at;
            xVec3Sub(&v2, campos, &flare->pos);
        }
        else
        {
            v1 = *cam_at;
            xVec3Inv(&v1, &v1);
            xVec3Sub(&v2, campos, &flare->pos);
        }

        xVec3Normalize(&v1, &v1);
        xVec3Normalize(&v2, &v2);

        F32 dot = xVec3Dot(&v1, &v2);
        F32 inner = PI * source->asset->coneAngle / 180.0f;
        F32 outer = 1.25f * inner;

        inner = icos(inner);
        outer = icos(outer);

        if (dot < outer)
        {
            continue;
        }

        if (dot >= outer && dot < inner)
        {
            intensity *= (dot - outer) / (inner - outer);
        }

        flare->attenuatedIntensity = CLAMP(intensity, 0.0f, 1.0f);

        gxFlareTexture = source->asset->elementArray[0]->texture;
        iScrFxLensFlare_Render(cam, &sFlare[i], xScrFxLensFlare_RenderElements);
        gxFlareTexture = NULL;
    }

    iScrFxLensFlare_Reset();

    sNumFlares = 0;
    for (i = 0; i < sNumFlareSources; i++)
    {
        xScrFxLensFlare_Add(&sFlareSource[i]);
    }
}
