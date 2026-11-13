#include "zParPTank.h"

#include <rpptank.h>
#include <types.h>

#include "xClimate.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xPtankPool.h"
#include "xRand.h"
#include "xstransvc.h"
#include "xVec3.h"
#include "xVec3Inlines.h"

#include "zGame.h"
#include "zGlobals.h"
#include "zGameState.h"
#include "zMain.h"
#include "zRenderState.h"

struct BubbleData
{
    xVec3 vel;
    F32 life;
};

extern RwCamera* sGameScreenTransCam;

const U32 gPTankDisable = 0;
static U32 sNumPTanks;
static zParPTank sPTank[7];
static zParPTank* sSparklePTank;
static zParPTank* sBubblePTank;
static zParPTank* sMenuBubblePTank;
static zParPTank* sSnowPTank;
static zParPTank* sSteamPTank;
static zParPTank* sEffectsParticlePTank;
static const RwV2d SparkleSize = { 0.3f, 0.3f };
static F32 sSparkleAnimTime;
static BubbleData* sSparkleData;
static BubbleData* sBubbleData;
static BubbleData* sMenuBubbleData;

namespace
{
    // total size: 0x30
    struct snow_particle_data
    {
        xVec3 loc;
        F32 size;
        xVec3 vel;
        F32 life;
        F32 u;
        F32 pad[3];
    };

    static snow_particle_data* snow_particles;
    static ptank_pool__pos_color_size_uv2 snow_pool;
} // namespace

static void zParPTankSpawnBubbles(xVec3* pos, xVec3* vel, U32 count, F32 scale, zParPTank* zp);

// No GC symbol: inlined into zSceneRenderPostFX and dead-stripped.
inline void zParPTankUpdate(F32 dt)
{
    S32 paused = zGameIsPaused();
    zParPTank* zp = sPTank;

    for (U32 i = 0; i < sNumPTanks; i++, zp++)
    {
        if ((!paused || (zp->flags & 0x1)) && zp->update)
        {
            zp->update(zp, dt);
        }
    }
}

static void zParPTankSparkleCreate(zParPTank* zp, U32 max_particles, zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(0xFF1FF776, NULL); // "partex0"
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwRGBA defaultColor = { 0xFF, 0xFF, 0xFF, 0xFF };

    zp->ptank = RpPTankAtomicCreate(
        zp->max_particles, rpPTANKDFLAGPOSITION | rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY, 0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpPTankAtomicSetConstantColor(zp->ptank, &defaultColor);
    RpPTankAtomicSetConstantSize(zp->ptank, &SparkleSize);
    RpPTankAtomicSetTexture(zp->ptank, tex);

    RpPTankAtomicSetBlendModes(zp->ptank, rwBLENDSRCALPHA, rwBLENDONE);
    RpPTankAtomicSetVertexAlpha(zp->ptank, TRUE);

    sSparkleAnimTime = 0.0f;
}

static void zParPTankSparkleUpdate(zParPTank* zp, F32 dt)
{
    sSparkleAnimTime += dt;

    if (!(sSparkleAnimTime >= 2.0f * SECS_PER_VBLANK))
    {
        return;
    }

    RpPTankLockStruct plock;
    RpPTankLockStruct uvlock;

    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    UPtr plock_base = (UPtr)plock.data;
    UPtr uvlock_base = (UPtr)uvlock.data;

    for (U32 i = 0; i < zp->num_particles; i++)
    {
        RwTexCoords* uv = (RwTexCoords*)uvlock.data;
        uv[0].u += 0.125f;
        uv[1].u += 0.125f;

        if (uv[0].u >= 1.0f)
        {
            *(RwV3d*)(plock_base + i * plock.stride) =
                *(RwV3d*)(plock_base + (zp->num_particles - 1) * plock.stride);

            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            i--;
            zp->num_particles--;
            uvlock.data -= uvlock.stride;
        }

        uvlock.data += uvlock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RpPTankAtomicSetActiveParticlesCount(zp->ptank, zp->num_particles);

    sSparkleAnimTime -= 2.0f * SECS_PER_VBLANK;
}

static const RwRGBA bubble_color = { 0x80, 0x80, 0x80, 0xFF };

static inline void zParPTankBubbleCreate(zParPTank* zp, U32 max_particles,
                                         zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;

    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(0xFF1FF776, NULL); // "partex0"
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);

    zp->ptank = RpPTankAtomicCreate(zp->max_particles,
                                    rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                        rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY,
                                    0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpPTankAtomicSetTexture(zp->ptank, tex);

    RpPTankAtomicSetBlendModes(zp->ptank, rwBLENDSRCALPHA, rwBLENDONE);
    RpPTankAtomicSetVertexAlpha(zp->ptank, TRUE);

    sBubbleData = (BubbleData*)RwMalloc(zp->max_particles * sizeof(BubbleData), 0);
}

static inline void zParPTankMenuBubbleCreate(zParPTank* zp, U32 max_particles,
                                             zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;

    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(0xFF1FF776, NULL); // "partex0"
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);

    zp->ptank = RpPTankAtomicCreate(zp->max_particles,
                                    rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                        rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY,
                                    0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpPTankAtomicSetTexture(zp->ptank, tex);

    RpPTankAtomicSetBlendModes(zp->ptank, rwBLENDSRCALPHA, rwBLENDONE);
    RpPTankAtomicSetVertexAlpha(zp->ptank, TRUE);

    sMenuBubbleData = (BubbleData*)RwMalloc(zp->max_particles * sizeof(BubbleData), 0);
}

static void zParPTankBubbleUpdate(zParPTank* zp, F32 dt)
{
    RpPTankLockStruct plock;
    RpPTankLockStruct clock;
    RpPTankLockStruct slock;
    RpPTankLockStruct uvlock;

    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &clock, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &slock, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    UPtr plock_base = (UPtr)plock.data;
    UPtr clock_base = (UPtr)clock.data;
    UPtr slock_base = (UPtr)slock.data;
    UPtr uvlock_base = (UPtr)uvlock.data;

    F32 damp = xpow(0.95f, dt * VBLANKS_PER_SEC);

    BubbleData* base_xp = zp == sBubblePTank ? sBubbleData : sMenuBubbleData;
    BubbleData* xp = base_xp;

    for (U32 i = 0; i < zp->num_particles; i++)
    {
        xp->life -= dt;

        xVec3* pos = (xVec3*)plock.data;
        pos->x += xp->vel.x * dt;
        pos->y += xp->vel.y * dt;
        pos->z += xp->vel.z * dt;

        xp->vel.x *= damp;
        xp->vel.y += 3.0f * dt;
        xp->vel.y *= damp;
        xp->vel.z *= damp;

        RwTexCoords* uv = (RwTexCoords*)uvlock.data;
        if (xp->life > 0.21875f)
        {
            uv[0].u = -(0.125f * (U32)((8.0f * xp->life) / 1.75f) - 1.0f);
            uv[1].u = 0.125f + uv[0].u;
        }

        F32 life = xp->life > 0.0f ? xp->life : 0.0f;
        RwRGBA* color = (RwRGBA*)clock.data;
        // Retail's 0x3FC99999 is 0.9f * 1.75f (1.575f would be 0x3FC9999A): the fade-in threshold
        // is 90% of the bubble life, as zParPTankSnowUpdate's fadein_life = 0.9f * snow_life.
        if (life > 0.9f * 1.75f)
        {
            color->alpha = 255.0f * ((1.75f - life) / 0.175f);
        }
        else if (life < 0.7f)
        {
            color->alpha = 255.0f * (life / 0.7f);
        }
        else
        {
            color->alpha = 0xFF;
        }

        if ((xp->life < 1.2f && xp->life > 0.5f && xurand() > 0.96f) != 0 || xp->life < 0.0f)
        {
            *pos = *(xVec3*)(plock_base + (zp->num_particles - 1) * plock.stride);
            *color = *(RwRGBA*)(clock_base + (zp->num_particles - 1) * clock.stride);
            *(RwV2d*)slock.data = *(RwV2d*)(slock_base + (zp->num_particles - 1) * slock.stride);

            RwTexCoords* uv = (RwTexCoords*)uvlock.data;
            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            xp->vel = base_xp[zp->num_particles - 1].vel;
            xp->life = base_xp[zp->num_particles - 1].life;

            i--;
            xp--;
            zp->num_particles--;
            plock.data -= plock.stride;
            clock.data -= clock.stride;
            slock.data -= slock.stride;
            uvlock.data -= uvlock.stride;
        }

        xp++;
        plock.data += plock.stride;
        clock.data += clock.stride;
        slock.data += slock.stride;
        uvlock.data += uvlock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RpPTankAtomicSetActiveParticlesCount(zp->ptank, zp->num_particles);
}

static void zParPTankSpawnBubbles(xVec3* pos, xVec3* vel, U32 count, F32 scale, zParPTank* zp)
{
    if (!xglobals->___player_ent_dont_use_directly->model ||
        !xglobals->___player_ent_dont_use_directly->model->Mat)
    {
        return;
    }

    if (count > zp->max_particles - zp->num_particles)
    {
        count = zp->max_particles - zp->num_particles;
    }

    if (count == 0)
    {
        return;
    }

    BubbleData* base_xp = zp == sBubblePTank ? sBubbleData : sMenuBubbleData;

    RpPTankLockStruct plock;
    RpPTankLockStruct clock;
    RpPTankLockStruct slock;
    RpPTankLockStruct uvlock;

    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    if (!plock.data)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &clock, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
    if (!clock.data)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &slock, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);
    if (!slock.data)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);
    if (!uvlock.data)
    {
        return;
    }

    UPtr plock_base = (UPtr)plock.data;
    UPtr clock_base = (UPtr)clock.data;
    UPtr slock_base = (UPtr)slock.data;
    UPtr uvlock_base = (UPtr)uvlock.data;
    xVec3* ref_pos = pos;
    RwCamera* camera = RwCameraGetCurrentCamera();
    if (gGameState == eGameState_Play && camera)
    {
        ref_pos = (xVec3*)&RwFrameGetMatrix(RwCameraGetFrame(camera))->pos;
    }

    for (U32 i = 0; i < count; i++)
    {
        if (!sGameScreenTransCam && ref_pos && xVec3Dist2(&pos[i], ref_pos) > 5625.0f)
        {
            continue;
        }

        *(xVec3*)(plock_base + zp->num_particles * plock.stride) = pos[i];
        base_xp[zp->num_particles].vel = vel[i];
        base_xp[zp->num_particles].life = 1.75f;

        RwTexCoords* uv = (RwTexCoords*)(uvlock_base + zp->num_particles * uvlock.stride);
        uv[0].u = 0.0f;
        uv[0].v = 0.625f;
        uv[1].u = 0.125f + uv[0].u;
        uv[1].v = 0.75f;

        RwV2d* size = (RwV2d*)(slock_base + zp->num_particles * slock.stride);
        size->x = size->y = scale * (0.15f * xurand() + 0.1f);

        *(RwRGBA*)(clock_base + zp->num_particles * clock.stride) = bubble_color;
        zp->num_particles++;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RpPTankAtomicSetActiveParticlesCount(zp->ptank, zp->num_particles);
}

void zParPTankSpawnBubbles(xVec3* pos, xVec3* vel, U32 count, F32 scale)
{
    if (zGameIsPaused())
    {
        return;
    }

    zParPTankSpawnBubbles(pos, vel, count, scale, sBubblePTank);
}

S32 zParPTankBubblesAvailable()
{
    return sBubblePTank->max_particles - sBubblePTank->num_particles;
}

void zParPTankSpawnMenuBubbles(xVec3* pos, xVec3* vel, U32 count)
{
    zParPTankSpawnBubbles(pos, vel, count, 1.0f, sMenuBubblePTank);
}

static void zParPTankSnowUpdate(zParPTank* zp, F32 dt)
{
    snow_particle_data* end = snow_particles + zp->num_particles;
    F32 fadein_life = 0.9f * snow_life;
    F32 fadeout_life = 0.4f * snow_life;
    F32 ilife = 1.0f / snow_life;
    F32 ifadein = 2550.0f * ilife;
    F32 ifadeout = 637.5f * ilife;
    snow_pool.reset();
    snow_particle_data* it = snow_particles;

    while (it != end)
    {
        it->life -= dt;
        if (it->life <= 0.0f)
        {
            end--;
            *it = *end;
            continue;
        }

        it->loc += it->vel * dt;

        // not a DWARF local: the frustum-test sphere (named as in Ratatouille's culling code)
        RwSphere worldsph;
        worldsph.center.x = it->loc.x;
        worldsph.center.y = it->loc.y;
        worldsph.center.z = it->loc.z;
        worldsph.radius = it->size;

        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
        {
            snow_pool.next();
            if (!snow_pool.valid())
            {
                end = it;
                break;
            }

            *(U32*)snow_pool.color = 0xFFFFFFFF;

            if (it->life > fadein_life)
            {
                snow_pool.color->a = ifadein * (snow_life - it->life) + 0.5f;
            }
            else if (it->life < fadeout_life)
            {
                snow_pool.color->a = it->life * ifadeout + 0.5f;
            }
            else
            {
                snow_pool.color->a = 0xFF;
            }

            *snow_pool.pos = it->loc;
            snow_pool.size->assign(it->size);
            snow_pool.uv[0].assign(it->u, 0.875f);
            snow_pool.uv[1].assign(0.125f + snow_pool.uv[0].x, 0.125f + snow_pool.uv[0].y);
        }

        it++;
    }

    snow_pool.flush();
    zp->num_particles = end - snow_particles;
}

void zParPTankSpawnSnow(xVec3* pos, xVec3* vel, U32 count)
{
    if (zGameIsPaused())
    {
        return;
    }

    // not a DWARF local: retail loads sSnowPTank once
    zParPTank* zp = sSnowPTank;
    const U32 old_size = zp->num_particles;
    zp->num_particles += count;

    if (zp->num_particles > zp->max_particles)
    {
        zp->num_particles = zp->max_particles;
    }

    snow_particle_data* it = snow_particles + old_size;
    snow_particle_data* end = it + (zp->num_particles - old_size);

    for (; it != end; it++)
    {
        it->loc = *pos;
        it->vel = *vel;
        it->life = snow_life;
        it->size = 0.15f * xurand() + 0.15f;
        it->u = 0.125f * ((xrand_RandomInt32() >> 13) & 0x7);

        pos++;
        vel++;
    }
}

static const RwV2d steam_size = { 0.4f, 0.4f };
static F32 sSteamAnimTime;

static void zParPTankSteamCreate(zParPTank* zp, U32 max_particles, zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(0x682D9088, NULL); // "particle_cloud"
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwRGBA defaultColor = { 0xFF, 0xFF, 0xFF, 0xFF };

    zp->ptank = RpPTankAtomicCreate(
        zp->max_particles, rpPTANKDFLAGPOSITION | rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY, 0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpPTankAtomicSetConstantColor(zp->ptank, &defaultColor);
    RpPTankAtomicSetConstantSize(zp->ptank, &steam_size);
    RpPTankAtomicSetTexture(zp->ptank, tex);

    RpPTankAtomicSetBlendModes(zp->ptank, rwBLENDSRCALPHA, rwBLENDONE);
    RpPTankAtomicSetVertexAlpha(zp->ptank, TRUE);

    sSteamAnimTime = 0.0f;
}

static void zParPTankSteamUpdate(zParPTank* zp, F32 dt)
{
    RpPTankAtomicSetConstantSize(zp->ptank, &steam_size);

    sSteamAnimTime += dt;

    if (!(sSteamAnimTime >= 2.0f * SECS_PER_VBLANK))
    {
        return;
    }

    RpPTankLockStruct plock;
    RpPTankLockStruct uvlock;

    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    UPtr plock_base = (UPtr)plock.data;
    UPtr uvlock_base = (UPtr)uvlock.data;

    for (U32 i = 0; i < zp->num_particles; i++)
    {
        RwTexCoords* uv = (RwTexCoords*)uvlock.data;
        uv[0].u += 0.125f;
        uv[1].u += 0.125f;

        if (uv[0].u >= 1.0f)
        {
            *(RwV3d*)(plock_base + i * plock.stride) =
                *(RwV3d*)(plock_base + (zp->num_particles - 1) * plock.stride);

            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            i--;
            zp->num_particles--;
            uvlock.data -= uvlock.stride;
        }

        uvlock.data += uvlock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RpPTankAtomicSetActiveParticlesCount(zp->ptank, zp->num_particles);
}

static const RwV2d EffectsParticlesSize = { 0.1f, 0.1f };
static F32 sEffectsParticleAnimTime;
static F32 SparkleDamp = 0.35f;
static F32 SparkleLife = 2.0f;

static void zParPTankEffectsParticleCreate(zParPTank* zp, U32 max_particles,
                                           zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(0xFF1FF776, NULL); // "partex0"
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    // not in the DWARF: the constant color is passed by address to RpPTankAtomicSetConstantColor.
    RwRGBA defaultColor = { 0xFF, 0xFF, 0xFF, 0xFF };

    zp->ptank = RpPTankAtomicCreate(
        zp->max_particles, rpPTANKDFLAGPOSITION | rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY, 0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpPTankAtomicSetConstantColor(zp->ptank, &defaultColor);
    RpPTankAtomicSetConstantSize(zp->ptank, &EffectsParticlesSize);
    RpPTankAtomicSetTexture(zp->ptank, tex);

    RpPTankAtomicSetBlendModes(zp->ptank, rwBLENDSRCALPHA, rwBLENDONE);
    RpPTankAtomicSetVertexAlpha(zp->ptank, TRUE);

    sSparkleData = (BubbleData*)RwMalloc(zp->max_particles * sizeof(BubbleData), 0);
    sEffectsParticleAnimTime = 0.0f;
}

static void zParPTankEffectsParticleUpdate(zParPTank* zp, F32 dt)
{
    sSparkleAnimTime += dt;

    if (!(sSparkleAnimTime >= 2.0f * SECS_PER_VBLANK))
    {
        return;
    }

    if (zp->num_particles == 0)
    {
        return;
    }

    RpPTankLockStruct plock;
    RpPTankLockStruct uvlock;

    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    UPtr plock_base = (UPtr)plock.data;
    UPtr uvlock_base = (UPtr)uvlock.data;

    BubbleData* base_xp = sSparkleData;
    BubbleData* xp = base_xp;
    F32 damp = xpow(SparkleDamp, dt * VBLANKS_PER_SEC);

    for (U32 i = 0; i < zp->num_particles; i++)
    {
        RwTexCoords* uv = (RwTexCoords*)uvlock.data;

        // NOTE: retail always moves the first particle (plock_base, not plock.data).
        xVec3* pos = (xVec3*)plock_base;

        xp->life -= dt;

        pos->x += xp->vel.x * dt;
        pos->y += xp->vel.y * dt;
        pos->z += xp->vel.z * dt;

        xp->vel.x *= damp;
        xp->vel.y *= damp;
        xp->vel.z *= damp;

        uv[0].u += 0.125f;
        uv[1].u += 0.125f;

        if (uv[0].u >= 1.0f || xp->life < 0.0f)
        {
            *(RwV3d*)(plock_base + i * plock.stride) =
                *(RwV3d*)(plock_base + (zp->num_particles - 1) * plock.stride);

            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            xp->vel = base_xp[zp->num_particles - 1].vel;
            xp->life = base_xp[zp->num_particles - 1].life;

            i--;
            xp--;
            zp->num_particles--;
            uvlock.data -= uvlock.stride;
            plock.data -= plock.stride;
        }

        xp++;
        uvlock.data += uvlock.stride;
        plock.data += plock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RpPTankAtomicSetActiveParticlesCount(zp->ptank, zp->num_particles);

    sSparkleAnimTime -= 2.0f * SECS_PER_VBLANK;
}

void zParPTankSpawnEffectsParticles(xVec3* pos, xVec3* vel, U32 count)
{
    zParPTank* zp = sEffectsParticlePTank;

    if (count > zp->max_particles - zp->num_particles)
    {
        count = zp->max_particles - zp->num_particles;
    }

    if (count == 0)
    {
        return;
    }

    RpPTankLockStruct posLock;
    RpPTankLockStruct vtx2TexCoordsLock;

    RpPTankAtomicLock(zp->ptank, &posLock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    if (posLock.data == NULL)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &vtx2TexCoordsLock, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);
    if (vtx2TexCoordsLock.data == NULL)
    {
        return;
    }

    UPtr poslock_base = (UPtr)posLock.data;
    UPtr uvlock_base = (UPtr)vtx2TexCoordsLock.data;
    xVec3* ref_pos = pos;
    RwCamera* camera = RwCameraGetCurrentCamera();
    if (gGameState == eGameState_Play && camera)
    {
        ref_pos = (xVec3*)&RwFrameGetMatrix(RwCameraGetFrame(camera))->pos;
    }

    for (U32 i = 0; i < count; i++)
    {
        if (!sGameScreenTransCam && ref_pos && xVec3Dist2(&pos[i], ref_pos) > 900.0f)
        {
            continue;
        }

        *(xVec3*)(poslock_base + zp->num_particles * posLock.stride) = pos[i];
        sSparkleData[zp->num_particles].vel = vel[i];
        sSparkleData[zp->num_particles].life = SparkleLife;

        RwTexCoords* uv =
            (RwTexCoords*)(uvlock_base + zp->num_particles * vtx2TexCoordsLock.stride);
        uv[0].u = 0.0f;
        uv[0].v = 0.5f;
        uv[1].u = 0.125f + uv[0].u;
        uv[1].v = 0.125f + uv[0].v;
        zp->num_particles++;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RpPTankAtomicSetActiveParticlesCount(zp->ptank, zp->num_particles);
}

// No DWARF has this helper (inlined in both PS2 builds), so the local is not attested; BfBB
// returns the expression directly. Through a local, zSceneInit gets retail's register choice.
static inline zParPTank* zParPTankAdd()
{
    zParPTank* zp = &sPTank[sNumPTanks++];
    return zp;
}

void zParPTankInit()
{
    sNumPTanks = 0;

    sSparklePTank = zParPTankAdd();
    zParPTankSparkleCreate(sSparklePTank, 0x80, zParPTankSparkleUpdate);

    sMenuBubblePTank = zParPTankAdd();
    zParPTankMenuBubbleCreate(sMenuBubblePTank, 0x10, zParPTankBubbleUpdate);
    sMenuBubblePTank->flags |= 0x3;

    sBubblePTank = zParPTankAdd();
    zParPTankBubbleCreate(sBubblePTank, 0x300, zParPTankBubbleUpdate);

    sSteamPTank = zParPTankAdd();
    zParPTankSteamCreate(sSteamPTank, 0x80, zParPTankSteamUpdate);

    sEffectsParticlePTank = zParPTankAdd();
    zParPTankEffectsParticleCreate(sEffectsParticlePTank, 0x20, zParPTankEffectsParticleUpdate);
    sEffectsParticlePTank->flags |= 0x3;
}

// No GC symbol: inlined into zSceneInit through zParPTankSceneEnter.
static inline void zParPTankSnowCreate(zParPTank* zp, U32 max_particles,
                                       zParPTankUpdateCallback update)
{
    zp->flags = 0;
    zp->update = update;
    zp->ptank = NULL;
    zp->num_particles = 0;
    zp->max_particles = max_particles;

    snow_pool.rs.texture = (RwTexture*)xSTFindAsset(0xFF1FF776, NULL); // "partex0"
    snow_pool.rs.src_blend = 5;
    snow_pool.rs.dst_blend = 2;
    snow_pool.rs.flags = 0;
    snow_particles =
        (snow_particle_data*)xMemAllocSize(zp->max_particles * sizeof(snow_particle_data));
}

// No GC symbol: inlined into zSceneInit.
inline void zParPTankSceneEnter()
{
    sSnowPTank = zParPTankAdd();
    zParPTankSnowCreate(sSnowPTank, 0x400, zParPTankSnowUpdate);
}

// No GC symbol: inlined into zSceneExit.
inline void zParPTankExit()
{
    zParPTank* zp = sPTank;

    for (U32 i = 0; i < sNumPTanks; i++, zp++)
    {
        if (zp->ptank != NULL)
        {
            RwFrame* frame = RpAtomicGetFrame(zp->ptank);
            RpAtomicSetFrame(zp->ptank, NULL);
            _rwFrameSyncDirty();
            RwFrameDestroy(frame);
            RpPTankAtomicDestroy(zp->ptank);
        }
    }

    if (sBubbleData != NULL)
    {
        RwFree(sBubbleData);
    }
    sBubbleData = NULL;

    if (sMenuBubbleData != NULL)
    {
        RwFree(sMenuBubbleData);
    }
    sMenuBubbleData = NULL;

    if (sSparkleData != NULL)
    {
        RwFree(sSparkleData);
    }
    sSparkleData = NULL;
}

// No GC symbol: inlined into zSceneRenderPostFX and dead-stripped.
inline void zParPTankRender()
{
    zParPTank* zp = sPTank;
    S32 renders = 0;
    S32 paused = zGameIsPaused();

    for (U32 i = 0; i < sNumPTanks; i++, zp++)
    {
        if ((!paused || (zp->flags & 0x2)) && zp->ptank && zp->num_particles)
        {
            if (renders <= 0)
            {
                zRenderState(SDRS_ZParPTank);
            }

            RpAtomicRender(zp->ptank);
            renders++;
        }
    }
}
