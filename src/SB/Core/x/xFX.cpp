#include "xFX.h"

#include "zFX.h"
#include "zRenderState.h"
#include "zParEmitter.h"
#include "zSurface.h"

#include "iDraw.h"
#include "iFX.h"
#include "iParMgr.h"
#include "xFXHighDynamicRange.h"
#include "xScrFx.h"

#include <rpworld.h>
#include <rpmatfx.h>
#include <rpptank.h>
#include <rpskin.h>
#include "xRand.h"

/* boot.HIP texture IDs */
#define ID_gloss_edge 0xB8C2351E
#define ID_rainbowfilm_smooth32 0x741B0566
#define ID_aura2 0x8074A95F /* "aura2" */

/* bubble entity IDs (xStrHash of the entity names) */
#define ID_bubble_buddy 0x50F6F3DE /* "bubble buddy" */
#define ID_bubble_missile 0x7D47072A /* "bubble missile" */
#define ID_bubble_helmet 0x3BB1E87D /* "bubble helmet" */
#define ID_bubble_bowling_ball 0xC6F600D7 /* "bubble bowling ball" */
#define ID_bubble_shoeL 0x79A50039 /* "bubble shoeL" */
#define ID_bubble_shoeR 0x79A5003F /* "bubble shoeR" */

struct _tagFirework
{
    S32 state;
    F32 timer;
    xVec3 vel;
    xVec3 pos;
    F32 fuel;
};

struct _xFXAura
{
    xVec3 pos;
    xColor_tag color;
    F32 size;
};

struct _xFXAuraAngle
{
    F32 angle;
    F32 cc;
    F32 ss;
};

#define FIREWORK_COUNT 10
#define STREAK_COUNT 10
#define SHINE_COUNT 2
#define AURA_COUNT 80

RpAtomicCallBackRender gAtomicRenderCallBack = NULL;
F32 EnvMapShininess = 1.0f;

RpLight* MainLight = NULL;

// GameCube-only table that no code references any more (retail .bss fx_atomics, 0x100 bytes).
// fxAtomic is from the Ratatouille GC DWARF.
struct fxAtomic
{
    RpAtomic* atomic;
    F32* originalUVs;
};

static fxAtomic fx_atomics[32];
static U32 num_fx_atomics = 0;
static RwFrame* g_matfx_frame;

U32 xfx_initted = 0;

xFXRing ringlist[RING_COUNT];
static U32 Im3DBufferPos;

// { pass1, pass2, pass3, padding, pass1_alpha, pass2_alpha, pass3_alpha, pass1_fbmsk,
//   fresnel_map, fresnel_map_coeff, env_map, env_map_coeff }
static xFXBubbleParams defaultBFX[1] = {
    1, 1, 1, 0, 0, 0, 192, 0xFFFFFFFF, ID_gloss_edge, 0.75f, ID_rainbowfilm_smooth32, 0.5f,
};

static U32 bfx_curr = 0;
static xFXBubbleParams* BFX = defaultBFX;

static RxPipeline* xFXanimUVPipeline;
F32 xFXanimUVRotMat0[2] = { 1.0f, 0.0f };
F32 xFXanimUVRotMat1[2] = { 0.0f, 1.0f };
F32 xFXanimUVTrans[2] = { 0.0f, 0.0f };
F32 xFXanimUVScale[2] = { 1.0f, 1.0f };
F32 xFXanimUV2PRotMat0[2] = { 1.0f, 0.0f };
F32 xFXanimUV2PRotMat1[2] = { 0.0f, 1.0f };
F32 xFXanimUV2PTrans[2] = { 0.0f, 0.0f };
F32 xFXanimUV2PScale[2] = { 1.0f, 1.0f };
RwTexture* xFXanimUV2PTexture;

static RwMatrix gFXUVs[2];

namespace xFX
{
    // Per-triangle segment counts of the last xFXRenderProximityFade call: alpha_count0 is the
    // predicted count, alpha_count1 the number of blended triangles actually produced.
    static U8 alpha_count0[300];
    static U8 alpha_count1[300];
} // namespace xFX

static _tagFirework sFirework[FIREWORK_COUNT];
static zParEmitter* sFireworkTrailEmit;
static zParEmitter* sFirework1Emit;
static zParEmitter* sFirework2Emit;
static U32 sFireworkSoundID;
static U32 sFireworkLaunchSoundID;

xFXStreak sStreakList[STREAK_COUNT];
xFXShine sShineList[SHINE_COUNT];

static void LightResetFrame(RpLight* light);
static void DrawRing(xFXRing* m);
inline RwRaster* xFXFindRaster(U32 id);
inline void xFXRingInit();

RpMaterial* MaterialSetEnvMap2(RpMaterial* material, void* data);
static RpMaterial* MaterialSetShininess(RpMaterial* material, void*);

static RpMaterial* MaterialDisableMatFX(RpMaterial* material, void*);

void xFXInit()
{
    RpLight* light;
    RwFrame* frame;

    if (!xfx_initted)
    {
        xfx_initted = 1;

        g_matfx_frame = RwFrameCreate();

        light = RpLightCreate(rpLIGHTDIRECTIONAL);

        if (light)
        {
            frame = RwFrameCreate();

            if (frame)
            {
                RpLightSetFrame(light, frame);
                LightResetFrame(light);

                MainLight = light;
            }
            else
            {
                RpLightDestroy(light);
            }
        }

        xFXanimUVCreate();
        xFXAuraInit();

        xFXRingInit();
    }
}

inline void xFXRingInit()
{
    xFXRing* ringdata = &ringlist[0];

    for (S32 i = 0; i < RING_COUNT; i++, ringdata++)
    {
        ringdata->time = -1.0f;
        ringdata->rast_ring = xFXFindRaster(ringdata->texture);
    }
}

static void DrawRing(xFXRing* m)
{
    xVec3 a;
    xVec3 b;
    xVec3 c;
    xVec3 d;
    F32 dt;
    F32 radius;
    F32 height;
    F32 tilt;
    F32 oradius;
    xVec3* center;
    RxObjSpace3DVertex* Im3DBuffer;
    RxObjSpace3DVertex* imv;
    _SDRenderState old_rendState;
    F32 oour;
    S32 i;
    U8 red;
    U8 green;
    U8 blue;
    U8 alpha;
    F32 u;

    if (m->time <= 0.0f)
    {
        return;
    }

    if (m->rast_ring == NULL)
    {
        return;
    }

    dt = m->time / m->lifetime;

    radius = m->ring_radius + m->ring_radius_delta * dt;
    height = m->ring_height + m->ring_height_delta * dt;
    tilt = m->ring_tilt + m->ring_tilt_delta * dt;

    oradius = radius + height * icos(tilt);
    height = height * isin(tilt);

    center = &m->pos;
    Im3DBuffer = gRenderBuffer.m_vertex;

    a.x = center->x;
    a.y = center->y;
    a.z = center->z + radius;
    c.x = center->x;
    c.y = center->y + height;
    c.z = center->z + oradius;

    old_rendState = zRenderStateCurrent();
    zRenderState(SDRS_PatStunRings);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)m->rast_ring);

    oour = (F32)m->u_repeat / (F32)m->ring_segs;

    for (i = 1; i <= m->ring_segs; i++)
    {
        if (Im3DBufferPos > 474)
        {
            if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
                RwIm3DEnd();
            }

            Im3DBufferPos = 0;
        }

        imv = Im3DBuffer + Im3DBufferPos;

        if (i == m->ring_segs)
        {
            tilt = 0.0f;
        }
        else
        {
            tilt = (2 * PI) * i / m->ring_segs;
        }

        F32 sinval = isin(tilt);
        F32 cosval = icos(tilt);

        b.x = center->x + radius * sinval;
        b.y = center->y;
        b.z = center->z + radius * cosval;
        d.x = center->x + oradius * sinval;
        d.y = center->y + height;
        d.z = center->z + oradius * cosval;

        F32 color[4];
        color[0] = m->ring_color.r - m->ring_color.r * dt;
        color[1] = m->ring_color.g - m->ring_color.g * dt;
        color[2] = m->ring_color.b - m->ring_color.b * dt;
        color[3] = m->ring_color.a - m->ring_color.a * dt;

        if (color[0] < 0.0f)
            color[0] = 0.0f;
        if (color[1] < 0.0f)
            color[1] = 0.0f;
        if (color[2] < 0.0f)
            color[2] = 0.0f;
        if (color[3] < 0.0f)
            color[3] = 0.0f;

        red = color[0];
        green = color[1];
        blue = color[2];
        alpha = color[3] * (1.0f - dt);

        u = i * oour;

        RwIm3DVertexSetPos(&imv[0], a.x, a.y, a.z);
        RwIm3DVertexSetPos(&imv[1], c.x, c.y, c.z);
        RwIm3DVertexSetPos(&imv[2], b.x, b.y, b.z);
        RwIm3DVertexSetPos(&imv[3], c.x, c.y, c.z);
        RwIm3DVertexSetPos(&imv[4], b.x, b.y, b.z);
        RwIm3DVertexSetPos(&imv[5], d.x, d.y, d.z);

        imv[0].u = u;
        imv[1].u = u;
        imv[2].u = u + oour;
        imv[3].u = u;
        imv[4].u = u + oour;
        imv[5].u = u + oour;

        imv[0].v = 0.0f;
        imv[1].v = 0.999f;
        imv[2].v = 0.0f;
        imv[3].v = 0.999f;
        imv[4].v = 0.0f;
        imv[5].v = 0.999f;

        RwIm3DVertexSetRGBA(&imv[0], red, green, blue, alpha);
        RwIm3DVertexSetRGBA(&imv[1], red, green, blue, alpha);
        RwIm3DVertexSetRGBA(&imv[2], red, green, blue, alpha);
        RwIm3DVertexSetRGBA(&imv[3], red, green, blue, alpha);
        RwIm3DVertexSetRGBA(&imv[4], red, green, blue, alpha);
        RwIm3DVertexSetRGBA(&imv[5], red, green, blue, alpha);

        Im3DBufferPos += 6;

        a = b;
        c = d;
    }

    if (Im3DBufferPos != 0)
    {
        if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL,
                            rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
            RwIm3DEnd();
        }

        Im3DBufferPos = 0;
    }

    zRenderState(old_rendState);
}

xFXRing* xFXRingCreate(const xVec3* pos, const xFXRing* params)
{
    xFXRing* ring = &ringlist[0];

    if (!pos || !params)
    {
        return NULL;
    }

    for (S32 i = 0; i < RING_COUNT; i++, ring++)
    {
        if (ring->time <= 0.0f)
        {
            memcpy(ring, params, sizeof(xFXRing));

            ring->rast_ring = xFXFindRaster(ring->texture);

            ring->time = 0.001f;
            ring->pos = *pos;
            ring->ring_radius_delta *= 1.0f / ring->lifetime;
            ring->ring_height_delta *= 1.0f / ring->lifetime;
            ring->ring_tilt_delta *= 1.0f / ring->lifetime;

            return ring;
        }
    }

    return NULL;
}

void xFXRingRender()
{
    S32 i;
    xFXRing* ring = &ringlist[0];

    for (i = 0; i < RING_COUNT; i++, ring++)
    {
        if (ring->time > 0.0f)
        {
            DrawRing(ring);
        }
    }
}

static inline RpMaterial* MaterialSetEnvMap(RpMaterial* material, void* data)
{
    RwTexture* texture;
    RwFrame* frame;

    if (data == NULL)
    {
        return NULL;
    }

    if (material->texture)
    {
        texture = (RwTexture*)data;

        if (texture)
        {
            if (gFXSurfaceFlags & 0x10)
            {
                if (xglobals->screen->icam)
                {
                    frame = (RwFrame*)xglobals->screen->icam->object.object.parent;
                }
                else
                {
                    frame = (RwFrame*)MainLight->object.object.parent;
                }
            }
            else
            {
                frame = (RwFrame*)MainLight->object.object.parent;
            }

            RpMatFXMaterialSetEffects(material, rpMATFXEFFECTENVMAP);
            RpMatFXMaterialSetupEnvMap(material, texture, frame, FALSE, 1.0f);
        }
        else
        {
            RpMatFXMaterialSetEffects(material, rpMATFXEFFECTNULL);
        }
    }

    return material;
}

static inline RpMaterial* MaterialSetBumpMap(RpMaterial* material, void* data)
{
    RwTexture* texture;
    RwFrame* frame;

    if (data == NULL)
    {
        return NULL;
    }

    if (material->texture)
    {
        texture = (RwTexture*)data;

        if (texture)
        {
            frame = (RwFrame*)MainLight->object.object.parent;
            RpMatFXMaterialSetEffects(material, rpMATFXEFFECTBUMPMAP);
            RpMatFXMaterialSetupBumpMap(material, texture, frame, 1.0f);
        }
        else
        {
            RpMatFXMaterialSetEffects(material, rpMATFXEFFECTNULL);
        }
    }

    return material;
}

static inline RpMaterial* MaterialSetBumpEnvMap(RpMaterial* material, RwTexture* envMap,
                                                F32 envCooef, RwTexture* bumpMap, F32 bumpCooef)
{
    RwFrame* frame;

    if (envMap == NULL || bumpMap == NULL)
    {
        return NULL;
    }

    RpMatFXMaterialSetEffects(material, rpMATFXEFFECTBUMPENVMAP);

    if (gFXSurfaceFlags & 0x10)
    {
        frame = (RwFrame*)xglobals->screen->icam->object.object.parent;
    }
    else
    {
        frame = (RwFrame*)MainLight->object.object.parent;
    }

    RpMatFXMaterialSetupEnvMap(material, envMap, frame, TRUE, envCooef);
    RpMatFXMaterialSetupBumpMap(material, bumpMap, (RwFrame*)MainLight->object.object.parent,
                                bumpCooef);

    return material;
}

void xFX_SceneEnter(RpWorld* world)
{
    S32 i;
    S32 num = RpWorldGetNumMaterials(world);

    for (i = 0; i < num; i++)
    {
        xSurface* sp = zSurfaceGetSurface(i);
        zSurfaceProps* pp = (zSurfaceProps*)sp->moprops;

        if (pp && pp->asset)
        {
            zSurfMatFX* fxp = &pp->asset->matfx;

            if (fxp->flags)
            {
                if (fxp->flags == 0x10)
                {
                    fxp->flags |= 0x1;
                }

                RpMaterial* mp = RpWorldGetMaterial(world, i);

                if (RpMaterialGetTexture(mp))
                {
                    gFXSurfaceFlags = fxp->flags;

                    if (fxp->flags & 0x1)
                    {
                        RwTexture* env = (RwTexture*)xSTFindAsset(fxp->envmapID, NULL);

                        if (!env)
                        {
                            continue;
                        }

                        MaterialSetEnvMap(mp, env);
                        RpMatFXMaterialSetEnvMapCoefficient(mp, 0.5f * fxp->shininess);
                    }

                    if (fxp->flags & 0x2)
                    {
                        RwTexture* bump = (RwTexture*)xSTFindAsset(fxp->bumpmapID, NULL);

                        if (!bump)
                        {
                            continue;
                        }

                        MaterialSetBumpMap(mp, bump);
                        RpMatFXMaterialSetBumpMapCoefficient(mp, fxp->bumpiness);
                    }

                    if (fxp->flags & 0x4)
                    {
                        RwTexture* env = (RwTexture*)xSTFindAsset(fxp->envmapID, NULL);
                        RwTexture* bump = (RwTexture*)xSTFindAsset(fxp->bumpmapID, NULL);

                        if (!env || !bump)
                        {
                            continue;
                        }

                        MaterialSetBumpEnvMap(mp, env, fxp->shininess, bump, fxp->bumpiness);
                    }
                }
            }
        }
    }

    xScene* sc = xglobals->sceneCur;

    for (i = 0; i < sc->num_act_ents; i++)
    {
        xEnt* ent = sc->act_ents[i];

        if (!gAtomicRenderCallBack && ent->model)
        {
            RpAtomicCallBackRender tmp = RpAtomicGetRenderCallBack(ent->model->Data);

            RpAtomicSetRenderCallBack(ent->model->Data, NULL);

            gAtomicRenderCallBack = RpAtomicGetRenderCallBack(ent->model->Data);

            RpAtomicSetRenderCallBack(ent->model->Data, tmp);
        }

        if (ent->model)
        {
            U32 bubble;

            bubble = 0;
            bubble |= (ent->id == ID_bubble_buddy);
            bubble |= (ent->id == ID_bubble_missile);
            bubble |= (ent->id == ID_bubble_helmet);
            bubble |= (ent->id == ID_bubble_bowling_ball);
            bubble |= (ent->id == ID_bubble_shoeL);
            bubble |= (ent->id == ID_bubble_shoeR);

            if (bubble)
            {
                xSTAssetName(ent->id);

                RpAtomicSetRenderCallBack(ent->model->Data, xFXBubbleRender);
            }
        }
    }

    num_fx_atomics = 0;
}

void xFX_SceneExit(RpWorld*)
{
}

// No GC symbol, but static rather than inline: its stripped copy interns 45.0f where retail's
// .sdata2 has it (before DrawRing's literals).
static void LightResetFrame(RpLight* light)
{
    RwV3d v1 = { 1.0f, 0.0f, 0.0f };
    RwV3d v2 = { 0.0f, 1.0f, 0.0f };

    RwFrame* frame = RpLightGetFrame(light);

    RwFrameRotate(frame, &v1, 45.0f, rwCOMBINEREPLACE);
    RwFrameRotate(frame, &v2, 45.0f, rwCOMBINEPOSTCONCAT);
}

// No GC symbol (always inlined into xFXUpdate); global functions in the PS2 build.
static inline void xFXRingUpdate(F32 dt)
{
    xFXRing* ring = &ringlist[0];

    if ((F32)iabs(dt) < 0.001f)
    {
        return;
    }

    for (S32 i = 0; i < RING_COUNT; i++, ring++)
    {
        if (ring->time <= 0.0f)
        {
            continue;
        }

        F32 lifetime = ring->lifetime;

        if (lifetime < dt)
        {
            lifetime = dt;
        }

        ring->time += dt;

        if (ring->time / lifetime > 1.0f)
        {
            ring->time = 0.0f;

            if (ring->parent)
            {
                *ring->parent = NULL;
            }

            ring->parent = NULL;
        }
    }
}

void xFXUpdate(F32 dt)
{
    xFXRingUpdate(dt);
    xFXRibbon::update_all(dt);
    xFXAuraUpdate(dt);
}

static RpMaterial* MaterialDisableMatFX(RpMaterial* material, void*)
{
    RpMatFXMaterialSetEffects(material, rpMATFXEFFECTNULL);
    return material;
}

RpAtomic* AtomicDisableMatFX(RpAtomic* atomic)
{
    RpMatFXAtomicEnableEffects(atomic);

    RpGeometry* geometry = RpAtomicGetGeometry(atomic);

    if (geometry)
    {
        RpGeometryForAllMaterials(geometry, MaterialDisableMatFX, NULL);
    }

    return atomic;
}

static RpMaterial* MaterialSetShininess(RpMaterial* material, void*)
{
    RpMatFXMaterialFlags flags = RpMatFXMaterialGetEffects(material);

    if (flags == rpMATFXEFFECTENVMAP || flags == rpMATFXEFFECTBUMPENVMAP)
    {
        RpMatFXMaterialSetEnvMapCoefficient(material, EnvMapShininess);
    }

    return material;
}

// useEnvMap is a constant flag, as in Ratatouille's version of this function: 'if (useEnvMap)'
// stops textureName from being propagated into the strcmp call, as in retail. The trailing
// '!useEnvMap' block is reconstructed dead code from the same source; it compiles to nothing.
RpMaterial* MaterialSetEnvMap2(RpMaterial* material, void* data)
{
    RwTexture* matTexture;
    char* textureName;
    S32 useEnvMap = TRUE;
    RwTexture* texture;
    RwFrame* frame;

    matTexture = RpMaterialGetTexture(material);

    if (matTexture != NULL)
    {
        texture = (RwTexture*)data;
        textureName = RwTextureGetName(texture);

        if (useEnvMap)
        {
            if (rwstrcmp(textureName, "spec3") == 0)
            {
                frame = RwCameraGetFrame(xglobals->screen->icam);
            }
            else
            {
                frame = RpLightGetFrame(MainLight);
            }

            RpMatFXMaterialSetEffects(material, rpMATFXEFFECTENVMAP);
            RpMatFXMaterialSetupEnvMap(material, texture, frame, FALSE, EnvMapShininess);
        }
    }

    if (!useEnvMap)
    {
        RpMatFXMaterialSetEffects(material, rpMATFXEFFECTNULL);
    }

    return material;
}

// No GC symbol (always inlined into xFXAtomicEnvMapSetup).
static inline RpAtomic* AtomicSetShininess(RpAtomic* atomic, void* data)
{
    RpGeometry* geometry = RpAtomicGetGeometry(atomic);

    if (geometry)
    {
        RpGeometryForAllMaterials(geometry, MaterialSetShininess, data);
    }

    return atomic;
}

// No GC symbol (always inlined into xFXAtomicEnvMapSetup).
static inline RpAtomic* AtomicSetEnvMap(RpAtomic* atomic, void* data)
{
    RpMatFXAtomicEnableEffects(atomic);

    RpGeometry* geometry = RpAtomicGetGeometry(atomic);

    if (geometry)
    {
        RpGeometryForAllMaterials(geometry, MaterialSetEnvMap2, data);
    }

    return atomic;
}

RpAtomic* xFXBubbleRender(RpAtomic* atomic)
{
    RwCullMode cmode;
    xFXBubbleParams* bp;

    bp = &BFX[bfx_curr];

    RwRenderStateGet(rwRENDERSTATECULLMODE, &cmode);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    iDrawSetFBMSK(bp->pass1_fbmsk, 0x00FFFFFF, true);
    iModelSetMaterialAlpha(atomic, bp->pass1_alpha);

    if (bp->pass1)
    {
        AtomicDisableMatFX(atomic);
        gAtomicRenderCallBack(atomic);
    }

    iDrawSetFBMSK(0, 0x00FFFFFF, true);
    iModelSetMaterialAlpha(atomic, bp->pass2_alpha);

    if (bp->pass2)
    {
        gFXSurfaceFlags = 0x10;
        xFXAtomicEnvMapSetup(atomic, bp->fresnel_map, bp->fresnel_map_coeff);
        gFXSurfaceFlags = 0;
        gAtomicRenderCallBack(atomic);
    }

    iModelSetMaterialAlpha(atomic, bp->pass3_alpha);

    if (bp->pass3)
    {
        AtomicDisableMatFX(atomic);
        gFXSurfaceFlags = 0x10;
        xFXAtomicEnvMapSetup(atomic, bp->env_map, bp->env_map_coeff);
        gFXSurfaceFlags = 0;
        gAtomicRenderCallBack(atomic);
    }

    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)cmode);

    return atomic;
}

RpAtomic* xFXAtomicEnvMapSetup(RpAtomic* atomic, U32 envmapID, F32 shininess)
{
    RwTexture* env;
    F32 tmp;
    RpSkin* skin;

    env = (RwTexture*)xSTFindAsset(envmapID, NULL);

    if (env)
    {
        AtomicSetEnvMap(atomic, env);

        tmp = EnvMapShininess;
        EnvMapShininess = shininess;
        AtomicSetShininess(atomic, NULL);
        EnvMapShininess = tmp;

        skin = RpSkinGeometryGetSkin(RpAtomicGetGeometry(atomic));

        if (skin)
        {
            RpSkinAtomicSetType(atomic, rpSKINTYPEMATFX);
        }

        return atomic;
    }

    return NULL;
}

inline U32 xFXanimUVCreate()
{
    if (xFXanimUVPipeline == NULL)
    {
        xFXanimUVPipeline = iFXanimUVCreatePipe();
    }

    return xFXanimUVPipeline != NULL;
}

void xFXanimUV2PSetTexture(RwTexture* texture)
{
    xFXanimUV2PTexture = texture;
}

RpAtomic* xFXanimUVAtomicSetup(RpAtomic* atomic, bool dualPass)
{
    RpGeometry* geom;
    S32 numMaterials;
    S32 i;
    RpMaterial* material;
    F32 rx;
    F32 ry;
    F32 ux;
    F32 uy;
    F32 py;

    if (atomic != NULL)
    {
        geom = RpAtomicGetGeometry(atomic);

        if (geom != NULL)
        {
            numMaterials = geom->matList.numMaterials;

            for (i = 0; i < numMaterials; i++)
            {
                material = geom->matList.materials[i];

                if (material == NULL)
                {
                    continue;
                }

                RwMatrixSetIdentity(&gFXUVs[0]);

                rx = xFXanimUVRotMat0[0] * xFXanimUVScale[0];
                ry = xFXanimUVRotMat0[1] * xFXanimUVScale[1];
                ux = xFXanimUVRotMat1[0] * xFXanimUVScale[0];
                uy = xFXanimUVRotMat1[1] * xFXanimUVScale[1];
                py = xFXanimUVTrans[1];

                gFXUVs[0].right.x = rx;
                gFXUVs[0].right.y = ry;
                gFXUVs[0].up.x = ux;
                gFXUVs[0].up.y = uy;
                gFXUVs[0].pos.x = xFXanimUVTrans[0];
                gFXUVs[0].pos.y = py;

                if (dualPass)
                {
                    if (RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTDUALUVTRANSFORM)
                    {
                        return NULL;
                    }

                    RwMatrixSetIdentity(&gFXUVs[1]);

                    rx = xFXanimUV2PRotMat0[0] * xFXanimUV2PScale[0];
                    ry = xFXanimUV2PRotMat0[1] * xFXanimUV2PScale[1];
                    ux = xFXanimUV2PRotMat1[0] * xFXanimUV2PScale[0];
                    uy = xFXanimUV2PRotMat1[1] * xFXanimUV2PScale[1];
                    py = xFXanimUV2PTrans[1];

                    gFXUVs[1].right.x = rx;
                    gFXUVs[1].right.y = ry;
                    gFXUVs[1].up.x = ux;
                    gFXUVs[1].up.y = uy;
                    gFXUVs[1].pos.x = xFXanimUV2PTrans[0];
                    gFXUVs[1].pos.y = py;

                    RpMatFXMaterialSetupDualTexture(material, xFXanimUV2PTexture, rwBLENDSRCALPHA,
                                                    rwBLENDINVSRCALPHA);
                    RpMatFXMaterialSetUVTransformMatrices(material, &gFXUVs[0], &gFXUVs[1]);
                }
                else
                {
                    if (RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTUVTRANSFORM)
                    {
                        return NULL;
                    }

                    RpMatFXMaterialSetEffects(material, rpMATFXEFFECTUVTRANSFORM);
                    RpMatFXMaterialSetUVTransformMatrices(material, &gFXUVs[0], NULL);
                }
            }
        }
    }

    return atomic;
}

namespace xFX
{
    struct vert_data
    {
        xVec3 loc;
        xVec3 norm;
        RwRGBA color;
        RwTexCoords uv;
        F32 depth;
    };

    struct tri_data
    {
        vert_data vert[3];

        void init(const xVec3* loc, const xVec3* norm, const RwTexCoords* uv, const F32* depth,
                  const U16* index)
        {
            for (S32 i = 0; i < 3; i++)
            {
                U16 vi = index[i];
                vert_data& v = vert[i];

                v.loc = loc[vi];
                v.norm = norm[vi];
                v.color.red = v.color.green = v.color.blue = v.color.alpha = 255;
                v.uv = uv[vi];
                v.depth = depth[vi];
            }
        }
    };

    inline void lerp(F32& v, F32 frac, F32 v0, F32 v1)
    {
        v = v0 + frac * (v1 - v0);
    }

    inline void lerp(U8& v, F32 frac, U8 v0, U8 v1)
    {
        v = 0.5f + ((F32)v0 + frac * ((F32)v1 - (F32)v0));
    }

    inline void lerp(xVec3& v, F32 frac, const xVec3& v0, const xVec3& v1)
    {
        lerp(v.x, frac, v0.x, v1.x);
        lerp(v.y, frac, v0.y, v1.y);
        lerp(v.z, frac, v0.z, v1.z);
    }

    inline void lerp(RwRGBA& v, F32 frac, RwRGBA v0, RwRGBA v1)
    {
        lerp(v.red, frac, v0.red, v1.red);
        lerp(v.green, frac, v0.green, v1.green);
        lerp(v.blue, frac, v0.blue, v1.blue);
        lerp(v.alpha, frac, v0.alpha, v1.alpha);
    }

    inline void lerp(RwTexCoords& v, F32 frac, const RwTexCoords& v0, const RwTexCoords& v1)
    {
        lerp(v.u, frac, v0.u, v1.u);
        lerp(v.v, frac, v0.v, v1.v);
    }

    inline void lerp(vert_data& v, F32 frac, const vert_data& v0, const vert_data& v1)
    {
        lerp(v.loc, frac, v0.loc, v1.loc);
        lerp(v.norm, frac, v0.norm, v1.norm);
        lerp(v.color, frac, v0.color, v1.color);
        lerp(v.uv, frac, v0.uv, v1.uv);
    }

    inline void set_vert(RxObjSpace3DVertex& vert, const xVec3& loc, const xVec3& norm,
                         const RwTexCoords& uv, U8 alpha)
    {
        RwIm3DVertexSetPos(&vert, loc.x, loc.y, loc.z);
        RwIm3DVertexSetNormal(&vert, norm.x, norm.y, norm.z);
        RwIm3DVertexSetRGBA(&vert, 255, 255, 255, alpha);
        RwIm3DVertexSetUV(&vert, uv.u, uv.v);
    }

    inline void set_vert(RxObjSpace3DVertex& vert, const vert_data& vd)
    {
        U8 alpha;
        F32 a = 255.0f * vd.depth;

        if (a < 0.0f)
        {
            alpha = 0;
        }
        else if (a >= 255.0f)
        {
            alpha = 255;
        }
        else
        {
            alpha = 0.5f + a;
        }

        RwIm3DVertexSetPos(&vert, vd.loc.x, vd.loc.y, vd.loc.z);
        RwIm3DVertexSetNormal(&vert, vd.norm.x, vd.norm.y, vd.norm.z);
        RwIm3DVertexSetRGBA(&vert, 255, 255, 255, alpha);
        RwIm3DVertexSetUV(&vert, vd.uv.u, vd.uv.v);
    }

    inline void push_triangle(RxObjSpace3DVertex*& vert, const tri_data& tri)
    {
        for (S32 i = 0; i < 3; i++)
        {
            set_vert(*vert, tri.vert[i]);
            vert++;
        }
    }

    // Splits a triangle that crosses the given depth into three triangles that each lie
    // entirely on one side of it.
    inline S32 clip_triangle(tri_data* out, const tri_data& in, F32 depth)
    {
        U16 flags = 0;
        U8 i0;
        U8 i1;
        U8 i2;

        flags |= (in.vert[0].depth < depth) ? 0x1 : 0x8;
        flags |= (in.vert[1].depth < depth) ? 0x2 : 0x10;
        flags |= (in.vert[2].depth < depth) ? 0x4 : 0x20;

        switch (flags)
        {
        case 0x0e:
        case 0x31:
            i0 = 0;
            i1 = 1;
            i2 = 2;
            break;
        case 0x15:
        case 0x2a:
            i0 = 1;
            i1 = 2;
            i2 = 0;
            break;
        case 0x1c:
        case 0x23:
            i0 = 2;
            i1 = 0;
            i2 = 1;
            break;
        default:
            out[0] = in;
            return 1;
        }

        F32 d0 = in.vert[i0].depth;
        F32 d1 = in.vert[i1].depth;
        F32 d2 = in.vert[i2].depth;

        out[0].vert[0] = in.vert[i0];

        lerp(out[0].vert[1], (depth - d0) / (d1 - d0), in.vert[i0], in.vert[i1]);
        lerp(out[0].vert[2], (depth - d0) / (d2 - d0), in.vert[i0], in.vert[i2]);

        out[0].vert[2].depth = depth;
        out[0].vert[1].depth = depth;

        out[1].vert[0] = out[0].vert[1];
        out[1].vert[1] = in.vert[i1];
        out[1].vert[2] = in.vert[i2];

        out[2].vert[0] = in.vert[i2];
        out[2].vert[1] = out[0].vert[2];
        out[2].vert[2] = out[0].vert[1];

        return 3;
    }

    // Flushes the immediate-mode vertex buffer once it is nearly full (or always, if flush).
    inline void refresh_vert_buffer(RxObjSpace3DVertex*& vert, bool flush)
    {
        S32 count = vert - gRenderBuffer.m_vertex;

        if (!flush && count < 492)
        {
            return;
        }

        if (count == 0)
        {
            return;
        }

        vert = gRenderBuffer.m_vertex;

        if (RwIm3DTransform(vert, count, NULL,
                            rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
            RwIm3DEnd();
        }
    }

    // Counts the triangles each triangle splits into when clipped against depth 0 and 1.
    inline U32 count_alpha_triangles(const RpTriangle* tri, const F32* depth, size_t size)
    {
        static const U8 segments[43] = { 0, 1, 3, 0, 1, 2, 4, 0, 3, 4, 3, 0, 0, 0, 0,
                                         0, 1, 2, 4, 0, 2, 1, 2, 0, 4, 2, 1, 0, 0, 0,
                                         0, 0, 3, 4, 3, 0, 4, 2, 1, 0, 3, 1, 0 };

        size_t i = 0;
        U32 total = 0;
        const RpTriangle* end = tri + size;

        while (tri != end)
        {
            U32 flags = 0;
            F32 d0 = depth[tri->vertIndex[0]];
            F32 d1 = depth[tri->vertIndex[1]];
            F32 d2 = depth[tri->vertIndex[2]];

            if (d0 < 0.0f)
            {
                // behind the fade range: contributes nothing
            }
            else if (d0 > 1.0f)
            {
                flags |= 0x2;
            }
            else
            {
                flags |= 0x1;
            }

            if (d1 < 0.0f)
            {
                // behind the fade range: contributes nothing
            }
            else if (d1 > 1.0f)
            {
                flags |= 0x8;
            }
            else
            {
                flags |= 0x4;
            }

            if (d2 < 0.0f)
            {
                // behind the fade range: contributes nothing
            }
            else if (d2 > 1.0f)
            {
                flags |= 0x20;
            }
            else
            {
                flags |= 0x10;
            }

            alpha_count0[i] = segments[flags];
            total += segments[flags];

            i++;
            tri++;
        }

        return total;
    }

    // Sorts the alpha triangles back to front.
    inline void depth_sort(U16* index, const tri_data* tri, size_t size)
    {
        for (U32 i = 0; i < size; i++)
        {
            U16& e0 = index[i];
            F32 d0 = tri[e0].vert[0].depth;

            for (U32 j = i + 1; j < size; j++)
            {
                if (tri[index[j]].vert[0].depth > d0)
                {
                    U16 temp = e0;

                    e0 = index[j];
                    index[j] = temp;
                }
            }
        }
    }
} // namespace xFX

void xFXRenderProximityFade(const xModelInstance& model, F32 near_dist, F32 far_dist)
{
    using namespace xFX;

    S32 curtriindex;
    RpGeometry* geom;
    RwRaster* raster;
    RpTriangle* tri;
    RwFrame* frame;
    RwTexCoords* uv;
    S32 vert_total;
    xVec3* vert;
    xVec3* normal;
    U8* alpha;
    F32* depth;

    memset(alpha_count0, 0, sizeof(alpha_count0));
    memset(alpha_count1, 0, sizeof(alpha_count1));

    geom = model.Data->geometry;
    raster = geom->matList.materials[0]->texture->raster;
    tri = geom->triangles;
    frame = RwCameraGetFrame(RwCameraGetCurrentCamera());
    uv = geom->texCoords[0];
    vert_total = geom->numVertices;

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)raster);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

    vert = (xVec3*)xMemPushTemp(vert_total * sizeof(xVec3));
    normal = (xVec3*)xMemPushTemp(vert_total * sizeof(xVec3));

    iModelVertEval(model.Data, 0, vert_total, model.Mat, NULL, vert);
    iModelNormalEval(normal, *model.Data, model.Mat, 0, -1, NULL);

    alpha = (U8*)xMemPushTemp(vert_total * sizeof(U8));
    depth = (F32*)xMemPushTemp(vert_total * sizeof(F32));

    xMat4x3& cm = *(xMat4x3*)RwFrameGetLTM(frame);
    xVec3 ov;
    F32 zfrac;
    S32 i;
    F32 a;
    RxObjSpace3DVertex* out_vert;
    S32 tri_total;
    U16* alpha_tri_index;
    tri_data* alpha_tri;
    U32 alpha_tri_total;
    // not in the DWARF: the number of alpha triangles produced so far.
    U32 alpha_tri_used;
    tri_data tri_buffer[2][3];
    tri_data cur_tri;
    RpTriangle* end;

    if (near_dist >= far_dist)
    {
        zfrac = 1.0f;
    }
    else
    {
        zfrac = 1.0f / (far_dist - near_dist);
    }

    for (i = 0; i < vert_total; i++)
    {
        xMat4x3Tolocal(&ov, &cm, &vert[i]);

        depth[i] = zfrac * (ov.z - near_dist);

        a = 255.0f * depth[i];

        if (a < 0.0f)
        {
            alpha[i] = 0;
        }
        else if (a >= 255.0f)
        {
            alpha[i] = 255;
        }
        else
        {
            alpha[i] = 0.5f + a;
        }
    }

    out_vert = gRenderBuffer.m_vertex;

    alpha_tri_index = NULL;
    alpha_tri = NULL;
    alpha_tri_used = 0;

    tri_total = geom->numTriangles;

    alpha_tri_total = count_alpha_triangles(tri, depth, tri_total);

    if (alpha_tri_total == 0)
    {
        // vert_total is never negative, so this never skips the render. Both the GC and PS2
        // builds branch straight into the shared cleanup below.
        if (vert_total < 0 && depth[0] < 0.0f)
        {
            goto cleanup;
        }
    }
    else
    {
        alpha_tri_index = (U16*)xMemPushTemp(alpha_tri_total * sizeof(U16));
        alpha_tri = (tri_data*)xMemPushTemp(alpha_tri_total * sizeof(tri_data));
    }

    end = tri + tri_total;

    curtriindex = 0;

    while (tri != end)
    {
        U16 vi[3];
        F32 d0;
        F32 d1;
        F32 d2;
        U32 flags;
        S32 i;
        S32 j;

        refresh_vert_buffer(out_vert, false);

        vi[0] = tri->vertIndex[0];
        vi[1] = tri->vertIndex[1];
        vi[2] = tri->vertIndex[2];

        flags = 0;

        d0 = depth[vi[0]];
        d1 = depth[vi[1]];
        d2 = depth[vi[2]];

        if (d0 < 0.0f)
        {
            flags |= 0x1;
        }
        else if (d0 > 1.0f)
        {
            flags |= 0x40;
        }
        else
        {
            flags |= 0x8;
        }

        if (d1 < 0.0f)
        {
            flags |= 0x2;
        }
        else if (d1 > 1.0f)
        {
            flags |= 0x80;
        }
        else
        {
            flags |= 0x10;
        }

        if (d2 < 0.0f)
        {
            flags |= 0x4;
        }
        else if (d2 > 1.0f)
        {
            flags |= 0x100;
        }
        else
        {
            flags |= 0x20;
        }

        if ((flags & 0x38) == 0x38 || (flags & 0x1c0) == 0x1c0)
        {
            // entirely inside the fade range, or entirely beyond it
            if (alpha[vi[0]])
            {
                set_vert(*out_vert++, vert[vi[0]], normal[vi[0]], uv[vi[0]], alpha[vi[0]]);
                set_vert(*out_vert++, vert[vi[1]], normal[vi[1]], uv[vi[1]], alpha[vi[1]]);
                set_vert(*out_vert++, vert[vi[2]], normal[vi[2]], uv[vi[2]], alpha[vi[2]]);
            }
        }
        else if ((flags & 0x7) != 0x7)
        {
            cur_tri.init(vert, normal, uv, depth, vi);

            S32 size1 = clip_triangle(tri_buffer[0], cur_tri, 0.0f);

            for (i = 0; i < size1; i++)
            {
                if (tri_buffer[0][i].vert[0].depth <= 0.0f &&
                    tri_buffer[0][i].vert[1].depth <= 0.0f &&
                    tri_buffer[0][i].vert[2].depth <= 0.0f)
                {
                    continue;
                }

                // not in the DWARF: the clipped triangle count.
                S32 size2 = clip_triangle(tri_buffer[1], tri_buffer[0][i], 1.0f);

                for (j = 0; j < size2; j++)
                {
                    if (tri_buffer[1][j].vert[0].depth >= 1.0f &&
                        tri_buffer[1][j].vert[1].depth >= 1.0f &&
                        tri_buffer[1][j].vert[2].depth >= 1.0f)
                    {
                        push_triangle(out_vert, tri_buffer[1][j]);
                    }
                    else
                    {
                        alpha_count1[curtriindex]++;

                        alpha_tri_index[alpha_tri_used] = alpha_tri_used;
                        alpha_tri[alpha_tri_used] = tri_buffer[1][j];
                        alpha_tri_used++;
                    }
                }
            }
        }

        tri++;
        curtriindex++;
    }

    depth_sort(alpha_tri_index, alpha_tri, alpha_tri_used);

    for (U32 i = 0; i < alpha_tri_used; i++)
    {
        refresh_vert_buffer(out_vert, false);
        push_triangle(out_vert, alpha_tri[alpha_tri_index[i]]);
    }

    refresh_vert_buffer(out_vert, true);

    if (alpha_tri_total)
    {
        xMemPopTemp(alpha_tri);
        xMemPopTemp(alpha_tri_index);
    }

cleanup:
    xMemPopTemp(depth);
    xMemPopTemp(alpha);
    xMemPopTemp(normal);
    xMemPopTemp(vert);
}

void xFXFireworksInit(const char* fireworksTrailEmitter, const char* fireworksEmitter1,
                      const char* fireworksEmitter2, const char* fireworksSound,
                      const char* fireworksLaunchSound)
{
    S32 i;

    sFireworkTrailEmit = zParEmitterFind(fireworksTrailEmitter);
    sFirework1Emit = zParEmitterFind(fireworksEmitter1);
    sFirework2Emit = zParEmitterFind(fireworksEmitter2);
    sFireworkSoundID = xStrHash(fireworksSound);
    sFireworkLaunchSoundID = xStrHash(fireworksLaunchSound);

    memset(sFirework, 0, sizeof(sFirework));

    for (i = 0; i < FIREWORK_COUNT; i++)
    {
        sFirework[i].state = 0;
    }
}

void xFXFireworksLaunch(F32 countdownTime, const xVec3* pos, F32 fuelTime)
{
    S32 i;

    for (i = 0; i < FIREWORK_COUNT; i++)
    {
        _tagFirework& f = sFirework[i];

        if (f.state == 0)
        {
            f.state = 1;
            f.timer = countdownTime;
            f.pos = *pos;
            f.fuel = fuelTime;
            return;
        }
    }
}

void xFXFireworksUpdate(F32 dt)
{
    for (S32 i = 0; i < FIREWORK_COUNT; i++)
    {
        if (sFirework[i].state == 0)
        {
            continue;
        }

        if (sFirework[i].state == 1)
        {
            sFirework[i].timer -= dt;
            if (sFirework[i].timer <= 0.0f)
            {
                sFirework[i].vel.x = 13.0f * xurand() + -6.5f;
                sFirework[i].vel.y = 0.0f;
                sFirework[i].vel.z = 13.0f * xurand() + -6.5f;
                sFirework[i].state = 2;

                if (sFireworkLaunchSoundID != 0)
                {
                    xSndMgrPlay(xSndMgrGetSoundGroup(sFireworkLaunchSoundID), 0, &sFirework[i].pos,
                                NULL, NULL, NULL, NULL);
                }
            }
        }
        else
        {
            sFirework[i].fuel -= dt;
            if (sFirework[i].fuel > 0.0f)
            {
                sFirework[i].vel.y += 15.0f * dt;
            }

            xVec3AddScaled(&sFirework[i].pos, &sFirework[i].vel, dt);
            xParEmitterCustomSettings trail_info;
            trail_info.custom_flags = 0x100;
            trail_info.pos = sFirework[i].pos;
            xParEmitterEmitCustom(sFireworkTrailEmit, dt, &trail_info);

            if (sFirework[i].fuel <= 0.0f)
            {
                sFirework[i].state = 0;
                sFirework[i].timer = 0.0f;

                zParEmitter* femit = sFirework1Emit;
                if (xurand() < 0.75f)
                {
                    femit = sFirework2Emit;
                }

                xParEmitterCustomSettings xplo_info;
                xplo_info.custom_flags = 0xD00;
                xplo_info.pos = sFirework[i].pos;

                if (femit != NULL)
                {
                    xplo_info.color_birth[0].set(127.0f * xurand() + 128.0f, 75.0f, 0.0f, 0);
                    xplo_info.color_birth[1].set(127.0f * xurand() + 128.0f, 75.0f, 0.0f, 0);
                    xplo_info.color_birth[2].set(127.0f * xurand() + 128.0f, 75.0f, 0.0f, 0);
                    xplo_info.color_birth[3].set(255.0f, 0.0f, 1.0f, 0);
                    memcpy(xplo_info.color_death, xplo_info.color_birth,
                           sizeof(xplo_info.color_birth));
                    xplo_info.color_death[3].set(0.0f, 0.0f, 1.0f, 0);
                    xParEmitterEmitCustom(femit, dt, &xplo_info);
                }

                xScrFXGlareAdd(&sFirework[i].pos, 0.5f * xurand() + 0.5f, 0.3f * xurand() + 0.1f,
                               5.0f * xurand() + 2.0f, 0.5f * xurand() + 0.5f,
                               0.5f * xurand() + 0.5f, 0.5f * xurand() + 0.5f,
                               0.4f * xurand() + 0.1f, NULL);

                sFirework[i].pos.y = xEntGetPos(xglobals->___player_ent_dont_use_directly)->y;

                if (sFireworkSoundID != 0)
                {
                    xSndMgrPlay(xSndMgrGetSoundGroup(sFireworkSoundID), 0, &sFirework[i].pos, NULL,
                                NULL, NULL, NULL);
                }
            }
        }
    }
}

// Dead-stripped placeholder, not recovered code: retail's @stringBase0 has an unreferenced
// "update frame\n" between "fx_streak2" and "spec3" (BfBB's xFX has the same string). Name and
// body are unknown.
static const char* __deadstripped_xFX()
{
    return "update frame\n";
}

void xFXStreakInit()
{
    S32 i;

    for (i = 0; i < STREAK_COUNT; i++)
    {
        memset(&sStreakList[i], 0, sizeof(xFXStreak));
        sStreakList[i].flags = 0;
        sStreakList[i].head = 0;
    }
}

void xFXStreakUpdate(F32 dt)
{
    S32 i;
    // Not in the PS2 DWARF; retail addresses every field through this one pointer.
    xFXStreak* s;

    for (i = 0; i < STREAK_COUNT; i++)
    {
        s = &sStreakList[i];

        if (s->flags == 0)
        {
            continue;
        }

        s->elapsed += dt;
        s->lifetime += dt;

        for (S32 j = 0; j < 50; j++)
        {
            xFXStreakElem* e = &s->elem[j];

            if (s->flags & 0x4)
            {
                xVec3 diff = e->p[0] - e->p[1];
                diff *= 0.5f * (s->alphaFadeRate * dt);

                e->p[0] -= diff;
                e->p[1] += diff;
            }

            e->a -= s->alphaFadeRate * dt;

            if (e->a < 0.01f)
            {
                e->a = 0.0f;
                e->flag = 0;
            }
        }

        if (s->flags & 0x2)
        {
            S32 done = 1;

            for (S32 j = 0; j < 50; j++)
            {
                if (s->elem[j].flag == 1)
                {
                    done = 0;
                    break;
                }
            }

            if (done)
            {
                s->flags = 0;
            }
        }
    }
}

void xFXStreakRender()
{
    xFXStreakElem* e1;
    S32 streak;
    S32 count;
    S32 j;
    // s and e are not in the PS2 DWARF; retail addresses the fields through them.
    xFXStreak* s;
    xFXStreakElem* e;
    static RxObjSpace3DVertex sStripVert[4];

    for (streak = 0; streak < STREAK_COUNT; streak++)
    {
        s = &sStreakList[streak];

        if (s->flags == 0)
        {
            continue;
        }

        j = s->head;
        count = 49;

        while (count > 0)
        {
            e = &s->elem[j];

            // j - 1 > 0 (not >= 0): from j == 1 the previous element is taken as elem[49]
            if (j - 1 > 0)
            {
                e1 = &s->elem[j - 1];
            }
            else
            {
                e1 = &s->elem[49];
            }

            if (e->flag == 0 || e1->flag == 0)
            {
                break;
            }

            // Not in the PS2 DWARF (constant-propagated, as in the other sStripVert renderers).
            RwIm3DVertex* vert = sStripVert;
            // Not in the PS2 DWARF either: retail addresses the positions as
            // elem + 12 * vertex index, which only a vertex-indexed p[] expression gives
            // (constant indices fold to plain offsets).
            S32 i = 0;

            RwIm3DVertexSetPos(vert, e->p[i].x, e->p[i].y, e->p[i].z);
            RwIm3DVertexSetUV(vert, 0.0f, 0.0f);
            RwIm3DVertexSetRGBA(vert, s->color_a.r, s->color_a.g, s->color_a.b,
                                (U8)(255.0f * e->a));
            vert++;
            i++;

            RwIm3DVertexSetPos(vert, e->p[i].x, e->p[i].y, e->p[i].z);
            RwIm3DVertexSetUV(vert, 0.0f, 1.0f);
            RwIm3DVertexSetRGBA(vert, s->color_b.r, s->color_b.g, s->color_b.b,
                                (U8)(255.0f * e->a));
            vert++;
            i++;

            RwIm3DVertexSetPos(vert, e1->p[i - 2].x, e1->p[i - 2].y, e1->p[i - 2].z);
            RwIm3DVertexSetUV(vert, 1.0f, 0.0f);
            RwIm3DVertexSetRGBA(vert, s->color_a.r, s->color_a.g, s->color_a.b,
                                (U8)(255.0f * e1->a));
            vert++;
            i++;

            RwIm3DVertexSetPos(vert, e1->p[i - 2].x, e1->p[i - 2].y, e1->p[i - 2].z);
            RwIm3DVertexSetUV(vert, 1.0f, 1.0f);
            RwIm3DVertexSetRGBA(vert, s->color_b.r, s->color_b.g, s->color_b.b,
                                (U8)(255.0f * e1->a));
            vert++;
            i++;

            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s->textureRasterPtr);

            if (RwIm3DTransform(sStripVert, 4, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                RwIm3DEnd();
            }

            j--;
            count--;

            if (j < 0)
            {
                j = 49;
            }
        }
    }
}

U32 xFXStreakStart(F32 frequency, F32 alphaFadeRate, F32 alphaStart, U32 textureID,
                   const xColor_tag* edge_a, const xColor_tag* edge_b, S32 taper)
{
    U32 i;
    S32 j;
    // Not in the PS2 DWARF; retail addresses every field through this one pointer.
    xFXStreak* s;

    for (i = 0; i < STREAK_COUNT; i++)
    {
        s = &sStreakList[i];

        if (s->flags == 0)
        {
            s->flags = 0x1;

            if (taper != 0)
            {
                s->flags |= 0x4;
            }

            s->frequency = frequency;
            s->alphaFadeRate = alphaFadeRate;
            s->alphaStart = alphaStart;
            s->head = 0;
            s->elapsed = 0.0f;
            s->lifetime = 0.0f;
            s->textureRasterPtr = NULL;
            s->texturePtr = NULL;

            for (j = 0; j < 50; j++)
            {
                s->elem[j].flag = 0;
            }

            if (edge_a != NULL)
            {
                s->color_a = *edge_a;
            }
            else
            {
                s->color_a.a = 255;
                s->color_a.b = 255;
                s->color_a.g = 255;
                s->color_a.r = 255;
            }

            if (edge_b != NULL)
            {
                s->color_b = *edge_b;
            }
            else
            {
                s->color_b.a = 255;
                s->color_b.b = 255;
                s->color_b.g = 255;
                s->color_b.r = 255;
            }

            if (textureID == 0)
            {
                textureID = xStrHash("fx_streak1");
            }

            s->texturePtr = (RwTexture*)xSTFindAsset(textureID, NULL);

            if (s->texturePtr != NULL)
            {
                s->textureRasterPtr = s->texturePtr->raster;
            }

            return i;
        }
    }

    return STREAK_COUNT;
}

void xFXStreakStop(U32 id)
{
    xFXStreak* s;

    if (id == STREAK_COUNT)
    {
        return;
    }

    s = &sStreakList[id];

    // (!s->flags) & 0x1 by precedence: a zero test, not the active-bit test it reads as
    if (!s->flags & 0x1)
    {
        return;
    }

    s->flags = s->flags & ~0x1;
    s->flags = s->flags | 0x2;
}

void xFXStreakUpdate(U32 id, const xVec3* a, const xVec3* b)
{
    xFXStreak* s;

    if (id == STREAK_COUNT)
    {
        return;
    }

    s = &sStreakList[id];

    if (s->elapsed > s->frequency)
    {
        s->elapsed -= s->frequency;
        s->head++;

        if (s->head >= 50)
        {
            s->head = 0;
        }
    }

    s->elem[s->head].p[0] = *a;
    s->elem[s->head].p[1] = *b;
    s->elem[s->head].flag = 1;
    s->elem[s->head].a = s->alphaStart;
}

// Dead-stripped placeholder, not recovered code: retail's @stringBase0 has an unreferenced
// "fx_streak2" right after "fx_streak1" (xFXRibbon::load_default_config). Name and body are
// unknown.
static const char* __deadstripped2_xFX()
{
    return "fx_streak2";
}

void xFXShineInit()
{
    S32 i;

    for (i = 0; i < SHINE_COUNT; i++)
    {
        memset(&sShineList[i], 0, sizeof(xFXShine));
        sShineList[i].flags = 0;
    }
}

void xFXShineUpdate(F32 dt)
{
    S32 i;
    S32 j;
    xFXShineElem* e;
    xFXShine* s;

    for (i = 0; i < SHINE_COUNT; i++)
    {
        s = &sShineList[i];

        if (s->flags == 0)
        {
            continue;
        }

        s->elapsed += dt;
        s->lifetime += dt;
        s->rotateZ += s->rotateSpeed * dt;

        if (s->lifetimeMax && !(s->flags & 0x2) && s->lifetime > s->lifetimeMax)
        {
            s->flags |= 0x2;
        }

        if (s->ppos != NULL)
        {
            s->pos = *s->ppos;
        }

        for (j = 0; j < 100; j++)
        {
            e = &s->elem[j];

            if (e->flag == 0 && s->elapsed >= s->frequency && !(s->flags & 0x2))
            {
                e->vel.x = s->spd * (2.0f * xurand() - 1.0f);
                e->vel.y = s->spd * (2.0f * xurand() - 1.0f);
                e->vel.z = s->spd * (2.0f * xurand() - 1.0f);

                e->p = e->vel;

                e->cola.r = s->color_a.r;
                e->cola.g = s->color_a.g;
                e->cola.b = s->color_a.b;
                e->colb.r = s->color_b.r;
                e->colb.g = s->color_b.g;
                e->colb.b = s->color_b.b;

                e->a = 1.0f;
                e->lifetime = 0.0f;
                e->flag = 1;

                s->elapsed -= s->frequency;
            }

            if (e->flag != 0)
            {
                e->p += e->vel * dt;
                e->lifetime += dt;

                if (e->lifetime < s->lifetimeElemMax)
                {
                    e->a = isin(3.1415927f * (e->lifetime / s->lifetimeElemMax));
                }
                else
                {
                    e->a = 0.0f;
                    e->flag = 0;
                }
            }
        }

        if (s->flags & 0x2)
        {
            S32 done = 1;
            S32 j;

            for (j = 0; j < 100; j++)
            {
                if (s->elem[j].flag & 0x1)
                {
                    done = 0;
                    break;
                }
            }

            if (done)
            {
                s->flags = 0;
            }
        }
    }
}

// Each vertex is filled through 'vert' and then 'vert++' (the PS2 line table has four lines
// per vertex).
void xFXShineRender()
{
    xFXShineElem* e;
    S32 shine;
    S32 j;
    RxObjSpace3DVertex* vert;
    RwFrame* frame;
    xVec3 v;
    xVec3 w;
    // not in the DWARF: the scaled width vector is used for two vertices.
    xVec3 w2;
    F32 uoff;
    xMat3x3 mat;
    xFXShine* s;
    static RxObjSpace3DVertex blah[4];

    for (shine = 0; shine < SHINE_COUNT; shine++)
    {
        s = &sShineList[shine];

        if (s->flags == 0)
        {
            continue;
        }

        for (j = 0; j < 100; j++)
        {
            e = &s->elem[j];

            if (e->flag == 0)
            {
                continue;
            }

            vert = blah;

            frame = NULL;
            if (xglobals->screen->icam != NULL)
            {
                frame = RwCameraGetFrame(xglobals->screen->icam);
            }

            v.x = e->p.x;
            v.y = e->p.y;
            v.z = e->p.z;

            w.x = 0.5f * (frame->ltm.right.x * s->width);
            w.y = 0.5f * (frame->ltm.right.y * s->width);
            w.z = 0.5f * (frame->ltm.right.z * s->width);

            w2 = w * 1.2f;

            uoff = e->lifetime * s->spd;

            // Retail passes the RwFrame itself (not its ltm) as the 3x3 matrix.
            xMat3x3Transpose(&mat, (xMat3x3*)frame);
            xMat3x3LMulVec(&v, &mat, &v);

            RwIm3DVertexSetPos(vert, s->pos.x, s->pos.y, s->pos.z);
            RwIm3DVertexSetUV(vert, uoff, 0.0f);
            RwIm3DVertexSetRGBA(vert, e->cola.r, e->cola.g, e->cola.b, 0);
            vert++;

            RwIm3DVertexSetPos(vert, s->pos.x, s->pos.y, s->pos.z);
            RwIm3DVertexSetUV(vert, uoff, 1.0f);
            RwIm3DVertexSetRGBA(vert, e->cola.r, e->cola.g, e->cola.b, 0);
            vert++;

            RwIm3DVertexSetPos(vert, s->pos.x + v.x - w.x, s->pos.y + v.y - w.y,
                               s->pos.z + v.z - w.z);
            RwIm3DVertexSetUV(vert, 1.0f + uoff, 0.0f);
            RwIm3DVertexSetRGBA(vert, e->cola.r, e->cola.g, e->cola.b, (U8)(255.0f * e->a));
            vert++;

            RwIm3DVertexSetPos(vert, w.x + (s->pos.x + v.x), w.y + (s->pos.y + v.y),
                               w.z + (s->pos.z + v.z));
            RwIm3DVertexSetUV(vert, 1.0f + uoff, 1.0f);
            RwIm3DVertexSetRGBA(vert, e->cola.r, e->cola.g, e->cola.b, (U8)(255.0f * e->a));
            vert++;

            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s->textureRasterPtr);

            if (RwIm3DTransform(blah, 4, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                RwIm3DEnd();
            }

            vert = blah;

            RwIm3DVertexSetPos(vert, s->pos.x, s->pos.y, s->pos.z);
            RwIm3DVertexSetUV(vert, uoff, 0.0f);
            RwIm3DVertexSetRGBA(vert, e->colb.r, e->colb.g, e->colb.b, 0);
            vert++;

            // u is 0.0 here, not uoff as in the first strip
            RwIm3DVertexSetPos(vert, s->pos.x, s->pos.y, s->pos.z);
            RwIm3DVertexSetUV(vert, 0.0f, 1.0f);
            RwIm3DVertexSetRGBA(vert, e->colb.r, e->colb.g, e->colb.b, 0);
            vert++;

            RwIm3DVertexSetPos(vert, s->pos.x + v.x - w2.x, s->pos.y + v.y - w2.y,
                               s->pos.z + v.z - w2.z);
            RwIm3DVertexSetUV(vert, 1.0f, 0.0f);
            RwIm3DVertexSetRGBA(vert, e->colb.r, e->colb.g, e->colb.b, (U8)(255.0f * e->a));
            vert++;

            RwIm3DVertexSetPos(vert, w2.x + (s->pos.x + v.x), w2.y + (s->pos.y + v.y),
                               w2.z + (s->pos.z + v.z));
            RwIm3DVertexSetUV(vert, 1.0f, 1.0f);
            RwIm3DVertexSetRGBA(vert, e->colb.r, e->colb.g, e->colb.b, (U8)(255.0f * e->a));
            vert++;

            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s->textureRasterPtr);

            if (RwIm3DTransform(blah, 4, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                RwIm3DEnd();
            }
        }
    }
}

tier_queue_allocator xFXRibbon::joint_alloc;
xFXRibbon::activity_data* xFXRibbon::activities[32];
S32 xFXRibbon::activities_used;
bool xFXRibbon::need_sort;

static F32 sAuraPulse[2];
static F32 sAuraPulseAng[2];
// +angle and -angle
static _xFXAuraAngle sAuraAngle[2];
static RwTexture* gAuraTex;
static _xFXAura sAura[AURA_COUNT];
static U32 sAuraCount;
static RpAtomic* sAuraPTank;

void xFXRibbon::create(S32)
{
    texture_offset = 0.0f;
    texture_increment_scale = 1.0f;
    act = NULL;
    cfg = NULL;
}

void xFXRibbon::clear()
{
    if (act == NULL)
    {
        return;
    }

    act->joints.clear();
    deactivate();
}

void xFXRibbon::load_default_config(config_type& cfg)
{
    cfg.life_time = 1.0f;
    cfg.blend_src = rwBLENDSRCALPHA;
    cfg.blend_dst = rwBLENDINVSRCALPHA;
    cfg.pivot = 0.5f;
    cfg.flags = 0;
    cfg.raster = xFXFindRaster(xStrHash("fx_streak1"));

    static const curve_node default_curve[2] = {
        { 0.0f, { 0xFF, 0xFF, 0xFF, 0xFF }, 1.0f },
        { 1.0f, { 0xFF, 0xFF, 0xFF, 0xFF }, 1.0f },
    };

    cfg.curve = default_curve;
    cfg.curve_size = 2;
}

void xFXRibbon::set_config(const config_type* cfg)
{
    this->cfg = cfg;

    if (act != NULL)
    {
        act->ilife = 1.0f / cfg->life_time;
        act->mlife = 1000.0f * cfg->life_time;
        need_sort = true;
    }
}

void xFXRibbon::insert(const xVec3& loc, const xVec3& norm, F32 scale, F32 alpha, S32 flags)
{
    activate();

    if (act == NULL)
    {
        return;
    }

    while (act->joints.front_full() && !act->joints.empty())
    {
        act->joints.pop_back();
    }

    if (act->joints.front_full())
    {
        return;
    }

    act->joints.push_front();

    joint_data& joint = act->joints.front();
    joint.flags = flags & 0x1;
    joint.loc = loc;
    joint.norm = norm;
    joint.born = act->mtime;
    joint.scale = scale;
    joint.alpha = alpha;
}

void xFXRibbon::update(F32 dt)
{
    if (act == NULL)
    {
        return;
    }

    act->mtime += (U32)(1000.0f * dt);

    while (!act->joints.empty())
    {
        joint_data& joint = act->joints.back();

        if (act->mtime - joint.born < act->mlife)
        {
            break;
        }

        joint.born = act->mtime - act->mlife;

        if (act->joints.size() == 1)
        {
            act->joints.clear();
            break;
        }

        if (act->mtime - (*(act->joints.end() - 2)).born < act->mlife)
        {
            break;
        }

        act->joints.pop_back();
    }

    // Retail bug, reproduced: deactivate() returns at once when act is NULL, so an emptied
    // ribbon is never deactivated here (the PS2 build has the same dead branch).
    if (act == NULL)
    {
        deactivate();
    }
}

// Builds the joint normal: dir crossed with the axis it is least aligned with, rotated by
// orient around dir.
// Retail bug, reproduced: in the x-axis arm norm.y uses dir.z * (a * dir.y) where the math
// wants dir.y * (a * dir.x).
void xFXRibbon::get_normal(xVec3& norm, const xVec3& dir, F32 orient)
{
    F32 a = isin(orient);
    F32 b = icos(orient);
    F32 ax = xabs(dir.x);
    F32 ay = xabs(dir.y);
    F32 az = xabs(dir.z);

    if (ax < ay && ax < az)
    {
        norm.x = -a * (dir.y * dir.y + dir.z * dir.z);
        norm.y = dir.z * (a * dir.y) + b * dir.z;
        norm.z = dir.z * (a * dir.x) - b * dir.y;
        norm *= 1.0f / xsqrt(dir.y * dir.y + dir.z * dir.z);
    }
    else if (ay < az)
    {
        norm.x = dir.y * (a * dir.x) - b * dir.z;
        norm.y = -a * (dir.x * dir.x + dir.z * dir.z);
        norm.z = dir.z * (a * dir.y) + b * dir.x;
        norm *= 1.0f / xsqrt(dir.x * dir.x + dir.z * dir.z);
    }
    else
    {
        norm.x = dir.z * (a * dir.x) + b * dir.y;
        norm.y = dir.z * (a * dir.y) - b * dir.x;
        norm.z = -a * (dir.x * dir.x + dir.y * dir.y);
        norm *= 1.0f / xsqrt(dir.x * dir.x + dir.y * dir.y);
    }
}

void xFXRibbon::refresh_joint(joint_data& joint, const tier_queue<joint_data>::iterator& it)
{
    if (act == NULL)
    {
        return;
    }

    if (!(joint.flags & 0x20000))
    {
        return;
    }

    joint.flags &= ~0x20000;

    xVec3* p[2];

    if (it == act->joints.begin())
    {
        p[0] = &joint.loc;
    }
    else
    {
        p[0] = &(it - 1)->loc;
    }

    if (it + 1 == act->joints.end())
    {
        p[1] = &joint.loc;
    }
    else
    {
        p[1] = &(it + 1)->loc;
    }

    xVec3 offset = *p[0] - *p[1];

    if (xfeq0(offset.length2()))
    {
        joint.norm.x = icos(joint.orient);
        joint.norm.y = isin(joint.orient);
        joint.norm.z = 0.0f;
    }
    else
    {
        xVec3 dir = offset * (1.0f / offset.length());
        get_normal(joint.norm, dir, joint.orient);
    }
}

namespace
{
    inline void lerp(U8& v, F32 frac, U8 v0, U8 v1)
    {
        v = 0.5f + ((F32)v0 + ((F32)v1 - (F32)v0) * frac);
    }

    inline void lerp(F32& v, F32 frac, F32 v0, F32 v1)
    {
        v = v0 + (v1 - v0) * frac;
    }
} // namespace

inline void xFXRibbon::eval_joint(const joint_data& joint, xColor_tag& color, F32& width)
{
    if (act == NULL)
    {
        return;
    }

    F32 frac = act->ilife * get_age(joint);

    if (frac > 1.0f)
    {
        frac = 1.0f;
    }

    const curve_node* curve = cfg->curve;

    while (act->curve_index > 0)
    {
        if (frac >= curve[act->curve_index].time && frac <= curve[act->curve_index + 1].time)
        {
            break;
        }

        act->curve_index--;
    }

    const curve_node& node0 = curve[act->curve_index];
    const curve_node& node1 = curve[act->curve_index + 1];
    F32 subfrac = (1.0f / (node1.time - node0.time)) * (frac - node0.time);

    lerp(color.r, subfrac, node0.color.r, node1.color.r);
    lerp(color.g, subfrac, node0.color.g, node1.color.g);
    lerp(color.b, subfrac, node0.color.b, node1.color.b);

    F32 alpha;
    lerp(alpha, subfrac, node0.color.a, node1.color.a);
    color.a = alpha * joint.alpha + 0.5f;

    lerp(width, subfrac, node0.scale, node1.scale);
    width *= joint.scale;
}

static inline void set_vert(RxObjSpace3DVertex& vert, F32 x, F32 y, F32 z, xColor_tag color)
{
    RwIm3DVertexSetPos(&vert, x, y, z);
    RwIm3DVertexSetRGBA(&vert, color.r, color.g, color.b, color.a);
}

void xFXRibbon::render_strip(RxObjSpace3DVertex* verts, tier_queue<joint_data>::iterator first,
                             size_t size)
{
    if (act == NULL)
    {
        return;
    }

    RxObjSpace3DVertex* v = verts;
    tier_queue<joint_data>::iterator it = first;
    tier_queue<joint_data>::iterator last = first - size;

    while (!(it == last))
    {
        it -= 1;

        joint_data& joint = *it;

        refresh_joint(joint, it);

        xColor_tag color;
        F32 width;

        color.r = 0;
        color.g = 0;
        color.b = 0;
        color.a = 0;
        width = 0.0f;

        eval_joint(joint, color, width);

        F32 off1 = cfg->pivot * width;
        F32 off2 = (cfg->pivot - 1.0f) * width;
        F32 cx = joint.loc.x;
        F32 cy = joint.loc.y;
        F32 cz = joint.loc.z;
        F32 nx = joint.norm.x;
        F32 ny = joint.norm.y;
        F32 nz = joint.norm.z;

        set_vert(v[0], cx + off1 * nx, cy + off1 * ny, cz + off1 * nz, color);
        set_vert(v[1], cx + off2 * nx, cy + off2 * ny, cz + off2 * nz, color);

        v += 2;
    }

    RxObjSpace3DVertex* endv = v;
    F32 ulookup[2] = { 0.0f, 1.0f };
    v = verts;

    switch (cfg->flags & 0x3)
    {
    case 0:
    {
        S32 back = first.it & 1;

        while (v != endv)
        {
            F32 u = ulookup[back];
            back ^= 1;

            RwIm3DVertexSetUV(&v[0], u, 0.0f);
            RwIm3DVertexSetUV(&v[1], u, 1.0f);

            v += 2;
        }

        break;
    }
    case 1:
    {
        F32 du = -texture_increment_scale / size;
        F32 u = 1.0f - texture_offset;

        while (v != endv)
        {
            RwIm3DVertexSetUV(&v[0], u, 0.0f);
            RwIm3DVertexSetUV(&v[1], u, 1.0f);

            u += du;
            v += 2;
        }

        break;
    }
    case 2:
    {
        S32 back = first.it & 1;

        while (v != endv)
        {
            F32 u = ulookup[back];
            back ^= 1;

            RwIm3DVertexSetUV(&v[0], 0.0f, u);
            RwIm3DVertexSetUV(&v[1], 1.0f, u);

            v += 2;
        }

        break;
    }
    case 3:
    {
        F32 du = -texture_increment_scale / size;
        F32 u = 1.0f - texture_offset;

        while (v != endv)
        {
            RwIm3DVertexSetUV(&v[0], 0.0f, u);
            RwIm3DVertexSetUV(&v[1], 1.0f, u);

            u += du;
            v += 2;
        }

        break;
    }
    }

    if (RwIm3DTransform(verts, v - verts, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }
}

inline void xFXRibbon::render()
{
    if (act == NULL)
    {
        return;
    }

    RxObjSpace3DVertex* verts = gRenderBuffer.m_vertex;

    xFXHighDynamicRangeBrighten((cfg->flags & 0x4) != 0);

    act->curve_index = cfg->curve_size - 2;

    S32 it = act->joints.size();

    while (it > 1)
    {
        S32 subsize = it > 240 ? 240 : it;
        S32 next_it = it - subsize + 1;
        S32 break_it = it - 1;

        while (break_it >= next_it)
        {
            break_it--;

            if (act->joints[break_it].flags & 0x1)
            {
                next_it = break_it + 1;
                subsize = it - next_it;
                break;
            }
        }

        if (subsize > 1)
        {
            render_strip(verts, act->joints.begin() + it, subsize);
        }

        it = next_it;
    }
}

// qsort hands compare_ribbons pointers to entries of xFXRibbon::activities.
inline const xFXRibbon::config_type* get_ribbon_config(const void* e)
{
    return (*(const xFXRibbon::activity_data* const*)e)->owner->cfg;
}

S32 xFXRibbon::compare_ribbons(const void* e1, const void* e2)
{
    if (e1 == e2)
    {
        return 0;
    }

    if (e1 == NULL)
    {
        return 1;
    }

    if (e2 == NULL)
    {
        return -1;
    }

    if (get_ribbon_config(e1)->raster < get_ribbon_config(e2)->raster)
    {
        return -1;
    }

    if (get_ribbon_config(e1)->raster > get_ribbon_config(e2)->raster)
    {
        return 1;
    }

    if (get_ribbon_config(e1)->blend_src < get_ribbon_config(e2)->blend_src)
    {
        return -1;
    }

    if (get_ribbon_config(e1)->blend_src > get_ribbon_config(e2)->blend_src)
    {
        return 1;
    }

    if (get_ribbon_config(e1)->blend_dst < get_ribbon_config(e2)->blend_dst)
    {
        return -1;
    }

    return get_ribbon_config(e1)->blend_dst > get_ribbon_config(e2)->blend_dst;
}

void xFXRibbon::scene_enter()
{
    joint_alloc.init(sizeof(joint_data), 32, 32);
    activities_used = 0;

    activity_data** act = activities;
    activity_data** end_act = act + 32;

    while (act != end_act)
    {
        *act = (activity_data*)xMemAllocSize(sizeof(activity_data));
        (*act)->position = act;
        (*act)->joints.init(joint_alloc);
        ++act;
    }
}

void xFXRibbon::update_all(F32 dt)
{
    activity_data** act = activities;
    activity_data** end_act = act + activities_used;

    while (act != end_act)
    {
        (*act)->owner->update(dt);
        ++act;
    }
}

inline void xFXRibbon::sort_active_ribbons()
{
    if (!need_sort || activities_used <= 0)
    {
        return;
    }

    qsort(activities, activities_used, sizeof(activity_data*), compare_ribbons);

    activity_data** act = activities;
    activity_data** end_act = act + activities_used;

    while (act != end_act)
    {
        (*act)->position = act;
        ++act;
    }

    need_sort = false;
}

void xFXRibbon::render_all()
{
    if (activities_used <= 0)
    {
        return;
    }

    sort_active_ribbons();

    zRenderState(SDRS_Ribbon);

    RwRaster* raster = (RwRaster*)-1;
    U32 blend_src = rwBLENDSRCALPHA;
    U32 blend_dst = rwBLENDINVSRCALPHA;
    activity_data** act = activities;
    activity_data** end_act = act + activities_used;

    while (act != end_act)
    {
        xFXRibbon* ribbon = (*act)->owner;

        if (ribbon->cfg->blend_src != blend_src)
        {
            blend_src = ribbon->cfg->blend_src;
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)blend_src);
        }

        if (ribbon->cfg->blend_dst != blend_dst)
        {
            blend_dst = ribbon->cfg->blend_dst;
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)blend_dst);
        }

        if (ribbon->cfg->raster != raster)
        {
            raster = ribbon->cfg->raster;
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }

        ribbon->render();
        ++act;
    }

    if (blend_src != rwBLENDSRCALPHA)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    }

    if (blend_dst != rwBLENDINVSRCALPHA)
    {
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    }

    xFXHighDynamicRangeBrighten(false);
}

// The colour is copied as one aligned word, not as an xColor_tag (the PS2 build uses lw/sw here
// too). The 'pos' temporary is not in the PS2 DWARF; retail reuses it for both particles.
static void xFXAuraPTankRender()
{
    S32 activeCount;
    RpPTankLockStruct poslock;
    RpPTankLockStruct sizelock;
    RpPTankLockStruct rotlock;
    RpPTankLockStruct collock;
    U32 i;
    _xFXAura* ap;
    xMat4x3* cammat;
    F32 nearclip;
    F32 scaleX;
    F32 scaleY;
    F32 rotA;
    F32 rotB;

    if (sAuraPTank == NULL || sAuraCount == 0)
    {
        return;
    }

    activeCount = 0;

    RpPTankAtomicLock(sAuraPTank, &poslock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(sAuraPTank, &sizelock, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(sAuraPTank, &rotlock, rpPTANKLFLAG2DROTATE, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(sAuraPTank, &collock, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);

    ap = sAura;
    cammat = (xMat4x3*)RwFrameGetMatrix(RwCameraGetFrame(RwCameraGetCurrentCamera()));
    nearclip = RwCameraGetNearClipPlane(RwCameraGetCurrentCamera()) + 0.1f;
    scaleX = 1.0f + sAuraPulse[0];
    scaleY = 1.0f + sAuraPulse[1];
    rotA = 3.926991f - sAuraAngle[0].angle;
    rotB = 0.7853982f - sAuraAngle[1].angle;

    if (rotA > PI)
    {
        rotA -= 2 * PI;
    }

    if (rotB > PI)
    {
        rotB -= 2 * PI;
    }

    for (i = 0; i < sAuraCount; i++, ap++)
    {
        F32 width;
        F32 height;
        xVec3 myat;
        F32 depth;
        F32 camdist;
        F32 at_offset;

        xVec3Sub(&myat, &ap->pos, &cammat->pos);
        camdist = xVec3Normalize(&myat, &myat);

        if (camdist < nearclip)
        {
            continue;
        }

        depth = -2.0f + camdist;

        if (depth <= nearclip)
        {
            depth = nearclip;
        }

        at_offset = depth - camdist;
        activeCount += 2;

        xVec3 pos;
        xVec3SMul(&pos, &myat, at_offset);
        xVec3Add(&pos, &ap->pos, &pos);

        height = 0.70710677f * ap->size * (depth / camdist);
        width = height * scaleX;
        height *= scaleY;

        ((xVec3*)poslock.data)->x = pos.x;
        ((xVec3*)poslock.data)->y = pos.y;
        ((xVec3*)poslock.data)->z = pos.z;
        poslock.data += poslock.stride;
        ((xVec3*)poslock.data)->x = pos.x;
        ((xVec3*)poslock.data)->y = pos.y;
        ((xVec3*)poslock.data)->z = pos.z;
        poslock.data += poslock.stride;

        ((RwV2d*)sizelock.data)->x = width;
        ((RwV2d*)sizelock.data)->y = height;
        sizelock.data += sizelock.stride;
        ((RwV2d*)sizelock.data)->x = width;
        ((RwV2d*)sizelock.data)->y = height;
        sizelock.data += sizelock.stride;

        *(F32*)rotlock.data = rotA;
        rotlock.data += rotlock.stride;
        *(F32*)rotlock.data = rotB;
        rotlock.data += rotlock.stride;

        *(U32*)collock.data = *(U32*)&ap->color;
        collock.data += collock.stride;
        *(U32*)collock.data = *(U32*)&ap->color;
        collock.data += collock.stride;
    }

    RPATOMICPTANKPLUGINDATA(sAuraPTank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(sAuraPTank)->actPCount = activeCount;

    RpPTankAtomicUnlock(sAuraPTank);
    RpAtomicRender(sAuraPTank);
}

void xFXAuraAdd(xVec3* pos, xColor_tag* color, F32 size, bool doTrivialCull)
{
    if (sAuraCount >= AURA_COUNT)
    {
        return;
    }

    if (doTrivialCull)
    {
        xMat4x3* camMat = &globals.cam->mat;
        xVec3 camToAura = *pos - camMat->pos;

        if (camToAura.length2() > 10000.0f)
        {
            return;
        }

        if (camToAura.dot(camMat->at) <= 0.0f)
        {
            return;
        }
    }

    _xFXAura* ap = &sAura[sAuraCount];
    sAuraCount++;

    ap->pos = *pos;
    ap->color = *color;
    ap->size = size;
}

// No GC symbol: a global function (as in the PS2 build and BfBB) auto-inlined into xFXUpdate.
// Its stripped copy here interns 2.3f and PI where retail's .sdata2 has them.
void xFXAuraUpdate(F32 dt)
{
    if (gAuraTex == NULL)
    {
        return;
    }

    sAuraPulseAng[0] += 2.0f * dt;
    sAuraPulseAng[1] += 2.3f * dt;

    if (sAuraPulseAng[0] > 2 * PI)
    {
        sAuraPulseAng[0] -= 2 * PI;
    }

    if (sAuraPulseAng[1] > 2 * PI)
    {
        sAuraPulseAng[1] -= 2 * PI;
    }

    sAuraPulse[0] = 0.1f * isin(sAuraPulseAng[0]);
    sAuraPulse[1] = 0.1f * isin(sAuraPulseAng[1]);

    sAuraAngle[0].angle += 0.5f * dt;

    if (sAuraAngle[0].angle > PI)
    {
        sAuraAngle[0].angle -= 2 * PI;
    }

    sAuraAngle[0].cc = icos(sAuraAngle[0].angle);
    sAuraAngle[0].ss = isin(sAuraAngle[0].angle);
    sAuraAngle[1].angle = -sAuraAngle[0].angle;
    sAuraAngle[1].cc = sAuraAngle[0].cc;
    sAuraAngle[1].ss = -sAuraAngle[0].ss;

    sAuraCount = 0;
}

void xFXAuraClearCount()
{
    sAuraCount = 0;
}

inline void xFXAuraInit()
{
    if (gAuraTex == NULL)
    {
        gAuraTex = (RwTexture*)xSTFindAsset(ID_aura2, NULL);
    }

    memset(sAura, 0, sizeof(sAura));
    sAuraCount = 0;
}

void xFXAuraRender()
{
    S32 fogstate;

    if (gAuraTex != NULL && sAuraPTank != NULL)
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fogstate);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

        xFXAuraPTankRender();

        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)fogstate);
    }
}

// No GC symbol (always inlined into xFX_SceneSetup).
static inline void xFXAuraPTankInit()
{
    RpAtomic* ptank;
    RwFrame* frame;

    ptank = RpPTankAtomicCreate(AURA_COUNT * 2,
                                rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                    rpPTANKDFLAG2DROTATE | rpPTANKDFLAGCNSVTX2TEXCOORDS |
                                    rpPTANKDFLAGARRAY,
                                rpPTANKDFLAGNONE);

    if (ptank == NULL)
    {
        return;
    }

    frame = RwFrameCreate();

    if (frame == NULL)
    {
        RpPTankAtomicDestroy(ptank);
        return;
    }

    RwMatrixSetIdentity(RwFrameGetMatrix(frame));
    RpAtomicSetFrame(ptank, frame);

    RPATOMICPTANKPLUGINDATA(ptank)->publicData.vertexAlphaBlend = TRUE;
    RPATOMICPTANKPLUGINDATA(ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(ptank)->actPCount = 0;

    RpMaterialSetTexture(ptank->geometry->matList.materials[0], gAuraTex);

    RPATOMICPTANKPLUGINDATA(ptank)->publicData.srcBlend = rwBLENDSRCALPHA;
    RPATOMICPTANKPLUGINDATA(ptank)->publicData.dstBlend = rwBLENDONE;
    RPATOMICPTANKPLUGINDATA(ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;

    RwTexCoords uv[2] = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    memcpy(RPATOMICPTANKPLUGINDATA(ptank)->publicData.cUV, uv, sizeof(uv));
    RPATOMICPTANKPLUGINDATA(ptank)->instFlags |= rpPTANKIFLAGCNSVTX2TEXCOORDS;

    sAuraPTank = ptank;
}

void xFX_SceneSetup()
{
    gAuraTex = (RwTexture*)xSTFindAsset(ID_aura2, NULL);
    xFXAuraPTankInit();
}

inline RwRaster* xFXFindRaster(U32 id)
{
    if (id == 0)
    {
        return NULL;
    }

    RwTexture* texture = (RwTexture*)xSTFindAsset(id, NULL);

    if (texture == NULL)
    {
        return NULL;
    }

    return texture->raster;
}

// Detaches and destroys the frame of a PTank atomic, then destroys the atomic.
inline void xFXDestroyPTank(RpAtomic* ptank)
{
    RwFrame* frame = RpAtomicGetFrame(ptank);
    RpAtomicSetFrame(ptank, NULL);

    if (frame != NULL)
    {
        _rwFrameSyncDirty();
        RwFrameDestroy(frame);
    }

    RpPTankAtomicDestroy(ptank);
}

void xFX_SceneShutdown()
{
    if (sAuraPTank != NULL)
    {
        xFXDestroyPTank(sAuraPTank);
        sAuraPTank = NULL;
    }
}
