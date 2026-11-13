#include "zNMESupplement.h"
#include "zNMEParFX.h"
#include "zLightning.h"
#include "zDust.h"
#include "xShadow.h"
#include "xShadowSimple.h"
#include "xCollide.h"
#include "xDebug.h"
#include "xDebugTweak.h"

#include <string.h>
#include "xRand.h"

struct zBossCam_Binary;

// Dynamic "pointer" asset (a position plus yaw/pitch/roll in degrees). Like zGame.cpp and
// zTalkBox.cpp, this file has its own definition.
struct pointer_asset : xDynAsset
{
    xVec3 loc;
    F32 yaw;
    F32 pitch;
    F32 roll;
};

namespace Supple
{
    namespace PlyrSlip
    {
        F32 tym_slippery = 4.0f;
        F32 fac_plyrfric = 0.01f;
    } // namespace PlyrSlip

    namespace Lightning
    {
        U32 rast_lightningMain;
        U32 rast_lightningBranch;

        // No GC symbol (always inlined); the name is ours.
        inline void Setup()
        {
            const char* name = "lightning";
            rast_lightningMain = xStrHash(name);
            rast_lightningBranch = xStrHash(name);
        }
    } // namespace Lightning

    namespace SlipFX
    {
        ExhaustCurve curve_data[3] = {
            { 0.0f, { 0x46, 0xA0, 0xDC, 0x20 }, 0.25f },
            { 0.5f, { 0x46, 0xA0, 0xDC, 0x40 }, 1.0f },
            { 1.0f, { 0x46, 0xA0, 0xDC, 0x00 }, 0.25f },
        };

        static const ExhaustEmitter slipGas = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.42f, 0.28f },
            { 1, 0, 0.1f, -0.1f },
            { 0.2f, 0.2f, 0.2f, 0.2f, 0.0f, 0.2f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 3, 1.0f, 1.5f },
        };
    } // namespace SlipFX
} // namespace Supple

static zMODSupple* g_mod_supple;

// No GC or PS2 symbol (inlined into the slimy hazards' collision code); name is ours. Makes the
// player slip for a while and starts the slip gas effect.
inline void NMEC_PlayerSlip()
{
    globals.player.ForceSlipperyTimer =
        MAX(globals.player.ForceSlipperyTimer, Supple::PlyrSlip::tym_slippery);
    globals.player.ForceSlipperyFriction = Supple::PlyrSlip::fac_plyrfric;
    zMODSupple* supple = g_mod_supple;

    supple->flg_doSlipFX = 1;
    supple->tmr_slipGasEmit = -1.0f;
}

// No GC symbol: inlined into zNMECentral_Startup. Defined here because retail's .bss has its
// mod_supple between g_mod_supple and g_shadCaches.
void zNMESupplement_Startup()
{
    static zMODSupple mod_supple;

    g_mod_supple = &mod_supple;
    zNMECentral_RegisterModule(&mod_supple, 'MSUP');
}

void zMODSupple::ModulePrepUse()
{
}

// Debug tweak callback; does nothing in the retail build.
inline void DBGTWK_KickVisSplash(const tweak_info& tweak)
{
}

void zMODSupple::ScenePrepare()
{
    NMEC_ShadowCacheReset();
    NMEC_SimpShadowCacheReset();
    NMEC_EntShadowCacheReset();
    NMEC_ParabReset();

    NMEC_DustRingCreate();
    NMEC_DustRingReset();

    flg_doSlipFX = 0;
    flg_doJellySting = 0;

    sting.zap_lytnin[0] = NULL;
    sting.zap_lytnin[1] = NULL;
}

void zMODSupple::ScenePostInit()
{
    static const tweak_callback tweak_launch_splash = { (
        void (*)(tweak_info&))DBGTWK_KickVisSplash };

    zNMEParFX_PartyInvite(NPAR_TYP_HEATED, NULL, 0);

    Supple::Lightning::Setup();
}

void zMODSupple::SceneReset()
{
    S32 i;

    NMEC_ShadowCacheReset();
    NMEC_SimpShadowCacheReset();
    NMEC_EntShadowCacheReset();
    NMEC_ParabReset();
    NMEC_DustRingReset();

    for (i = 0; i < 2; i++)
    {
        if (sting.zap_lytnin[i] != NULL)
        {
            zLightningKill(sting.zap_lytnin[i]);
        }
        sting.zap_lytnin[i] = NULL;
    }
}

void zMODSupple::Timestep(F32 dt)
{
    if (flg_doSlipFX && !zGameIsPaused())
    {
        if (globals.player.s->Health < 1 || globals.player.ForceSlipperyTimer <= 0.0f)
        {
            flg_doSlipFX = 0;
        }
        else
        {
            tmr_slipGasEmit = MAX(-1.0f, tmr_slipGasEmit - dt);
            if (tmr_slipGasEmit < 0.0f)
            {
                tmr_slipGasEmit = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

                xVec3 pos = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
                pos.y += 0.35f;
                Supple::SlipFX::slipGas.EmitRing(&pos, &g_NY3, -100000.0f, 1.0f, -1, NULL);
            }
        }
    }

    if (flg_doJellySting)
    {
        StingerUpdate(dt);
    }
}

// No GC symbol (always inlined); the name is ours.
static inline void NMEC_StingerKill(JellySting* sting)
{
    for (S32 i = 0; i < 2; i++)
    {
        if (sting->zap_lytnin[i] != NULL)
        {
            zLightningKill(sting->zap_lytnin[i]);
            sting->zap_lytnin[i] = NULL;
        }
    }
}

// No GC symbol: inlined into its only caller, zNMEGoalJellyAttack::ZapperStart.
void zMODSupple::StingerPrepare()
{
    _tagLightningAdd info;
    xVec3 pos_beg;
    S32 i;
    S32 num_plyrbone;

    if (flg_doJellySting)
    {
        return;
    }

    flg_doJellySting = 1;

    memset(&info, 0, sizeof(_tagLightningAdd));
    NMEC_MakeLightningInfo(NME_LYT_LARVAARC, &info);

    pos_beg = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);

    info.time = 15.0f;
    info.start = &pos_beg;
    info.end = &pos_beg;
    info.segmentsPerMeter = 3.0f;

    for (i = 0; i < 2; i++)
    {
        sting.zap_lytnin[i] = zLightningAdd(&info);
    }

    num_plyrbone = xglobals->___player_ent_dont_use_directly->model->BoneCount;
    sting.idx_curr = (S32)(num_plyrbone * xurand()) + 1;
    sting.idx_curr = CLAMP(sting.idx_curr, 1, num_plyrbone);
}

// idx_base is const so that sting.idx_curr is loaded twice, as retail does (a plain copy is merged
// with the increment's load).
void zMODSupple::StingerUpdate(F32 dt)
{
    S32 num_plyrbone;

    if (zGameIsPaused())
    {
        return;
    }

    if (globals.player.DamageTimer < 0.1f)
    {
        NMEC_StingerKill(&sting);
        flg_doJellySting = 0;
        return;
    }

    static S32 lessismore = 0;
    if (--lessismore > 0)
    {
        return;
    }

    lessismore = 7;

    num_plyrbone = xglobals->___player_ent_dont_use_directly->model->BoneCount;
    const S32 idx_base = sting.idx_curr;
    S32 idx_list[3];
    xMat4x3* mat_list[3];
    S32 i;
    S32 idx;

    sting.idx_curr++;
    if (sting.idx_curr > num_plyrbone)
    {
        sting.idx_curr = 1;
    }

    for (i = 0; i < 3; i++)
    {
        idx = idx_base + (i * 7) % num_plyrbone;
        idx_list[i] = CLAMP(idx, 1, num_plyrbone);
        mat_list[i] = (xMat4x3*)&xglobals->___player_ent_dont_use_directly->model->Mat[idx_list[i]];
    }

    xVec3 pos_pctr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);
    xMat4x3 mat_plyrroot = *(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;

    for (S32 i = 0; i < 2; i++)
    {
        static const xVec3 vec_elechair = { 0.75f, 0.75f, 0.75f };

        xVec3 pos_from = mat_list[i]->pos;
        xMat4x3Toworld(&pos_from, &mat_plyrroot, &pos_from);

        xVec3 pos_dest;
        xVec3 pos_outer = vec_elechair;
        pos_outer.x *= 2.0f * (xurand() - 0.5f);
        pos_outer.y *= 2.0f * (xurand() - 0.5f);
        pos_outer.z *= 2.0f * (xurand() - 0.5f);

        pos_dest = pos_pctr;
        pos_dest += mat_plyrroot.at * pos_outer.z;
        pos_dest += mat_plyrroot.right * pos_outer.x;
        pos_dest += mat_plyrroot.up * pos_outer.y;

        zLightningModifyEndpoints(sting.zap_lytnin[i], &pos_from, &pos_dest);
    }
}

// The pools and their helpers follow StingerUpdate to match retail's .sbss order; zMODSupple's
// scene functions use the helpers through zNMESupplement.h.
static xShadowCache g_shadCaches[4];
static S8 g_shadCachesInUseFlags[4];
static xShadowSimpleCache g_simpShads[64];
static S8 g_simpShadsInUseFlags[64];
static xEntShadow g_entShads[64];
static S8 g_entShadsInUseFlags[64];
static xParabola g_parabPool[256];
static S8 g_parabInUseFlags[256];
static zDustRingEmitter g_dustPool[16];
static S8 g_dustInUseFlags[16];

// Defined here to follow retail's .sbss order; used by zNMETypeBossFrogfish.cpp.
zBossCam_Binary* gzBinaryCam;

// No GC symbol (always inlined); after BfBB's NPCC_ShadowCacheReset.
inline void NMEC_ShadowCacheReset()
{
    for (S32 i = 0; i < 4; i++)
    {
        g_shadCachesInUseFlags[i] = 0;
    }
}

// No GC or PS2 symbol (inlined into HAZBasic::Stuff_Cleanup); after BfBB's
// NPCC_ShadowCacheRelease: hands a complex shadow cache taken from g_shadCaches back to the pool.
inline void NMEC_ShadowCacheRelease(xShadowCache* shadCache)
{
    for (S32 i = 0; i < 4; i++)
    {
        if (shadCache == &g_shadCaches[i])
        {
            g_shadCachesInUseFlags[i] = 0;
        }
    }
}

// No GC symbol (always inlined); see NMEC_ShadowCacheReset.
inline void NMEC_SimpShadowCacheReset()
{
    for (S32 i = 0; i < 64; i++)
    {
        g_simpShadsInUseFlags[i] = 0;
    }
}

// No GC symbol (always inlined); see NMEC_ShadowCacheReset.
inline void NMEC_EntShadowCacheReset()
{
    for (S32 i = 0; i < 64; i++)
    {
        g_entShadsInUseFlags[i] = 0;
    }
}

// No GC symbol (always inlined); see NMEC_ShadowCacheReset.
inline void NMEC_ParabReset()
{
    for (S32 i = 0; i < 256; i++)
    {
        g_parabInUseFlags[i] = 0;
    }
}

// No GC symbol (always inlined); see NMEC_ShadowCacheReset.
inline void NMEC_DustRingCreate()
{
    for (S32 i = 0; i < 16; i++)
    {
        g_dustPool[i].create();
    }
}

// No GC symbol (always inlined); see NMEC_ShadowCacheReset.
inline void NMEC_DustRingReset()
{
    for (S32 i = 0; i < 16; i++)
    {
        g_dustInUseFlags[i] = 0;
    }
}

// No GC symbol (always inlined); the name is ours, after NMEC_SimpShadowCacheReserve: takes a free
// parabola from g_parabPool (zeroed), or NULL when the pool is exhausted.
inline xParabola* NMEC_ParabReserve()
{
    xParabola* da_parab = NULL;
    S32 i;

    for (i = 0; i < 256; i++)
    {
        if (!g_parabInUseFlags[i])
        {
            g_parabInUseFlags[i] = 1;
            da_parab = &g_parabPool[i];
            break;
        }
    }

    if (da_parab != NULL)
    {
        memset(da_parab, 0, sizeof(xParabola));
    }

    return da_parab;
}

// No GC symbol (always inlined); the name is ours, after NMEC_SimpShadowCacheRelease: hands a
// parabola taken from g_parabPool back to the pool.
inline void NMEC_ParabRelease(xParabola* parab)
{
    for (S32 i = 0; i < 256; i++)
    {
        if (parab == &g_parabPool[i])
        {
            g_parabInUseFlags[i] = 0;
        }
    }
}

// No GC symbol (always inlined); the name is ours, after NMEC_SimpShadowCacheRelease: hands an
// entity shadow taken from g_entShads back to the pool.
inline void NMEC_EntShadowCacheRelease(xEntShadow* entShad)
{
    for (S32 i = 0; i < 64; i++)
    {
        if (entShad == &g_entShads[i])
        {
            g_entShadsInUseFlags[i] = 0;
        }
    }
}

// No GC symbol (always inlined); the name is ours, after NMEC_SimpShadowCacheReserve: takes a free
// entity shadow from g_entShads (zeroed), or NULL when the pool is exhausted.
inline xEntShadow* NMEC_EntShadowCacheReserve()
{
    xEntShadow* da_entShad = NULL;
    S32 i;

    for (i = 0; i < 64; i++)
    {
        if (!g_entShadsInUseFlags[i])
        {
            g_entShadsInUseFlags[i] = 1;
            da_entShad = &g_entShads[i];
            break;
        }
    }

    if (da_entShad != NULL)
    {
        memset(da_entShad, 0, sizeof(xEntShadow));
    }

    return da_entShad;
}

// No GC symbol (always inlined); the name is ours, after NMEC_SimpShadowCacheReserve: takes a
// dust ring emitter from g_dustPool (NULL when all are in use).
inline zDustRingEmitter* NMEC_DustRingReserve()
{
    zDustRingEmitter* da_dust = NULL;
    S32 i;

    for (i = 0; i < 16; i++)
    {
        if (!g_dustInUseFlags[i])
        {
            g_dustInUseFlags[i] = 1;
            da_dust = &g_dustPool[i];
            break;
        }
    }

    if (da_dust != NULL)
    {
        memset(da_dust, 0, sizeof(zDustRingEmitter));
    }

    return da_dust;
}

// No GC symbol (always inlined); the name is ours, after NMEC_SimpShadowCacheRelease: hands a
// dust ring emitter taken from g_dustPool back to the pool.
inline void NMEC_DustRingRelease(zDustRingEmitter* dust)
{
    for (S32 i = 0; i < 16; i++)
    {
        if (dust == &g_dustPool[i])
        {
            g_dustInUseFlags[i] = 0;
        }
    }
}

void NMEC_MakeLightningInfo(en_npclyt style, _tagLightningAdd* info)
{
    switch (style)
    {
    case NME_LYT_PLACEHOLDER:
    default:
        info->type = 3;
        info->total_points = 4;
        info->thickness = 0.3f;
        info->color.r = 255;
        info->color.g = 255;
        info->color.b = 255;
        info->color.a = 120;
        info->rand_radius = 1.0f;
        info->arc_height = 0.2f;
        info->rot_radius = 0.5f;
        info->time = 1.0f;
        info->mainTexture = Supple::Lightning::rast_lightningMain;
        info->branchTexture = Supple::Lightning::rast_lightningBranch;
        info->flags = 0;
        info->segmentsPerMeter = 3.0f;
        info->setup_degrees = 90.0f * xurand() + 20.0f;
        info->move_degrees = 12360.0f * xurand() + 1280.0f;
        if (xrand_RandomInt32() & 0x800000)
        {
            info->move_degrees *= -1.0f;
        }
        break;
    case NME_LYT_JELLYPINK:
        info->type = 3;
        info->total_points = 10;
        info->thickness = 0.15f;
        info->color.r = 255;
        info->color.g = 0;
        info->color.b = 255;
        info->color.a = 80;
        info->rand_radius = 1.0f;
        info->arc_height = 0.4f;
        info->rot_radius = 0.5f;
        info->time = 1.0f;
        info->mainTexture = Supple::Lightning::rast_lightningMain;
        info->branchTexture = Supple::Lightning::rast_lightningBranch;
        info->flags = 0;
        info->segmentsPerMeter = 3.0f;
        info->setup_degrees = 90.0f * xurand() + 20.0f;
        info->move_degrees = 12360.0f * xurand() + 1280.0f;
        if (xrand_RandomInt32() & 0x800000)
        {
            info->move_degrees *= -1.0f;
        }
        break;
    case NME_LYT_LARVAARC:
        info->type = 3;
        info->total_points = 10;
        info->thickness = 0.25f;
        info->color.r = 255;
        info->color.g = 0;
        info->color.b = 255;
        info->color.a = 180;
        info->rand_radius = 1.0f;
        info->arc_height = 0.2f;
        info->rot_radius = 0.5f;
        info->time = 1.0f;
        info->mainTexture = Supple::Lightning::rast_lightningMain;
        info->branchTexture = Supple::Lightning::rast_lightningBranch;
        info->flags = 0;
        info->segmentsPerMeter = 3.0f;
        info->setup_degrees = 90.0f * xurand() + 20.0f;
        info->move_degrees = 12360.0f * xurand() + 1280.0f;
        if (xrand_RandomInt32() & 0x800000)
        {
            info->move_degrees *= -1.0f;
        }
        break;
    case NME_LYT_ZAPBOOM:
        info->type = 3;
        info->total_points = 10;
        info->thickness = 0.25f;
        info->color.r = 60;
        info->color.g = 150;
        info->color.b = 255;
        info->color.a = 128;
        info->rand_radius = 1.0f;
        info->arc_height = 0.2f;
        info->rot_radius = 0.5f;
        info->time = 1.0f;
        info->mainTexture = Supple::Lightning::rast_lightningMain;
        info->branchTexture = Supple::Lightning::rast_lightningBranch;
        info->flags = 0;
        info->segmentsPerMeter = 3.0f;
        info->setup_degrees = 90.0f * xurand() + 20.0f;
        info->move_degrees = 12360.0f * xurand() + 1280.0f;
        if (xrand_RandomInt32() & 0x800000)
        {
            info->move_degrees *= -1.0f;
        }
        break;
    case NME_LYT_NEPTUNETRIDENT:
        info->type = 3;
        info->total_points = 16;
        info->thickness = 0.25f;
        info->color.r = 215;
        info->color.g = 220;
        info->color.b = 20;
        info->color.a = 128;
        info->rand_radius = 0.25f;
        info->arc_height = 0.1f;
        info->rot_radius = 0.15f;
        info->time = 1.0f;
        info->mainTexture = Supple::Lightning::rast_lightningMain;
        info->branchTexture = Supple::Lightning::rast_lightningBranch;
        info->flags = 0;
        info->segmentsPerMeter = 5.25f;
        info->setup_degrees = 90.0f * xurand() + 20.0f;
        info->move_degrees = 12360.0f * xurand() + 1280.0f;
        if (xrand_RandomInt32() & 0x800000)
        {
            info->move_degrees *= -1.0f;
        }
        break;
    }
}

// No GC symbol: inlined into zNMEGoalSpawning::PatienceMonitor and HAZMervBomb::AirburstBoomFX.
// The burst type is unused.
void NMEC_BurstBubble(en_npcburst, xVec3* pos_base, F32 rad_pop, S32 flags)
{
    F32 inc_hyt;
    S32 i;
    S32 j;
    F32 hyt;
    F32 rat_hyt;
    F32 rad_cur;
    F32 curang;
    xVec3 pos_emit;

    inc_hyt = (2.0f * rad_pop) / 16.0f;
    for (j = 0; j < 16; j++)
    {
        hyt = j * inc_hyt;
        rat_hyt = 0.5f * hyt / rad_pop;
        rad_cur = rad_pop;
        if (flags & 1)
        {
            rad_cur *= BOWL(rat_hyt);
        }
        else
        {
            rad_cur *= ARCH3(rat_hyt);
        }

        for (i = 0; i < 20; i++)
        {
            curang = (2.0f * PI / 20.0f) * xurand() + (2.0f * PI / 20.0f) * i;
            curang = CLAMP(curang, 0.0f, 2.0f * PI);

            F32 fs = isin(curang);
            F32 fc = icos(curang);

            pos_emit.x = rad_cur * fs + 0.1f * (2.0f * (xurand() - 0.5f));
            pos_emit.y = 0.1f * (2.0f * (xurand() - 0.5f)) + hyt;
            pos_emit.z = rad_cur * fc + 0.1f * (2.0f * (xurand() - 0.5f));
            pos_emit += *pos_base;

            zFX_SpawnBubbleTrail(&pos_emit, 1);
        }
    }
}

void NMEC_MakeASplash(const NMECSplashData* splash)
{
    if (splash->ass_ptr != NULL)
    {
        NMEC_MakeASplash(splash->ass_ptr, splash->typ_splash, splash->rad_splash,
                         splash->tym_duration);
    }
    else
    {
        NMEC_MakeASplash(&splash->pos_splash, &g_Y3, splash->typ_splash, splash->rad_splash,
                         splash->tym_duration);
    }
}

void NMEC_MakeASplash(const pointer_asset* ass_ptr, en_typeSplash typ_splash, F32 radius,
                      F32 tym_duration)
{
    xVec3 pos_splash;
    xVec3 ang;
    xMat3x3 mat_rot;
    xMat4x3 mat_orient;

    pos_splash = ass_ptr->loc;

    ang.x = xDangleClamp(0.017453292f * ass_ptr->yaw);
    ang.y = xDangleClamp(0.017453292f * ass_ptr->pitch);
    ang.z = xDangleClamp(0.017453292f * ass_ptr->roll);
    xMat3x3Euler(&mat_rot, &ang);

    mat_orient.pos = pos_splash;
    mat_orient.up = mat_rot.at;
    mat_orient.at = mat_rot.right;
    mat_orient.right = mat_rot.up;

    NMEC_MakeASplash(&mat_orient, typ_splash, radius, tym_duration);
}

void NMEC_MakeASplash(const xVec3* pos_splash, const xVec3* dir_up, en_typeSplash typ_splash,
                      F32 radius, F32 tym_duration)
{
    xMat4x3 mat_orient;

    mat_orient.pos = *pos_splash;
    if (dir_up == NULL)
    {
        mat_orient.up = g_Y3;
    }
    else
    {
        mat_orient.up = *dir_up;
    }

    mat_orient.at.x = mat_orient.up.y - mat_orient.up.z;
    mat_orient.at.y = mat_orient.up.z - mat_orient.up.x;
    mat_orient.at.z = mat_orient.up.x - mat_orient.up.y;
    xVec3Normalize(&mat_orient.at, &mat_orient.at);
    xVec3Cross(&mat_orient.right, &mat_orient.at, &mat_orient.up);

    NMEC_MakeASplash(&mat_orient, typ_splash, radius, tym_duration);
}

void NMEC_MakeASplash(const xMat4x3* mat_orient, en_typeSplash typ_splash, F32 radius,
                      F32 tym_duration)
{
    F32 rad_splash;
    F32 tym_splash;
    zNMEHazard* haz;
    S32 rc;

    rad_splash = radius;
    if (rad_splash < 0.0f)
    {
        rad_splash = 1.5f;
    }

    tym_splash = tym_duration;
    if (tym_splash < 0.0f)
    {
        tym_splash = 1.0f;
    }

    typ_splash =
        (en_typeSplash)CLAMP((S32)typ_splash, (S32)NMEC_SPLASH_WATER, (S32)NMEC_SPLASH_NUKEGOO);
    en_haztyp hazTypeForSplash[7] = {
        HAZ_TYP_VISSPLASH_WATER, HAZ_TYP_VISSPLASH_CHOC, HAZ_TYP_VISSPLASH_LAVA,
        HAZ_TYP_VISSPLASH_BILE,  HAZ_TYP_VISSPLASH_OIL,  HAZ_TYP_VISSPLASH_SAUCE,
        HAZ_TYP_VISSPLASH_TOXIC,
    };
    en_haztyp hazType = hazTypeForSplash[typ_splash];

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return;
    }

    rc = haz->ConfigHelper(hazType);
    if (!rc)
    {
        haz->Discard();
        return;
    }

    haz->npc_owner = NULL;
    if (0.5f * rad_splash >= 0.0f)
    {
        haz->custdata.typical.rad_min = 0.5f * rad_splash;
    }
    if (rad_splash >= 0.0f)
    {
        haz->custdata.typical.rad_max = rad_splash;
    }
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->TypData_RotMatSet(mat_orient);
    haz->Start(&mat_orient->pos, tym_splash);
}

// No GC symbol: inlined into its callers, the out-of-line copy dead-stripped.
void NMEC_RenderProjTexture(RwRaster* rast, F32 factor, xMat4x3* mat, F32 radius, F32 height,
                            xShadowCache* cache, S32 fillCache, xEnt* ent)
{
    S32 i;
    xEnt* ent_tgt;
    U32 rc;

    if (fillCache)
    {
        xShadowVertical_FillCache(cache, &mat->pos, radius, height, 0.0871557f);
    }

    gShadowObjectRadius = radius;
    xShadowVertical_DrawCache(cache, factor, 0.0f, 1, (RwMatrixTag*)mat, rast);

    for (i = 0; i < cache->entCount; i++)
    {
        ent_tgt = cache->ent[i];
        if (ent_tgt == ent)
        {
            continue;
        }

        if (ent_tgt->baseType == eBaseTypeNPC)
        {
            continue;
        }

        rc = xShadowReceiveShadowSetup(ent_tgt);
        if (!rc)
        {
            continue;
        }

        xShadowReceiveShadow(ent_tgt, factor, 1, (RwMatrixTag*)mat, rast);
    }
}

// No GC or PS2 symbol: BfBB's NPCC_RenderProjTextureFaceCamera, auto-inlined into its only
// caller (loveyTikiRender) and the out-of-line copy dead-stripped.
void NMEC_RenderProjTextureFaceCamera(RwRaster* rast, F32 factor, xVec3* pos, F32 radius,
                                      F32 height, xShadowCache* cache, S32 fillCache, xEnt* ent)
{
    xMat4x3 matrix;

    xVec3Copy(&matrix.pos, pos);
    xVec3Copy(&matrix.right, &globals.oldSkoolCamera.mat.right);

    if (matrix.right.y < -0.0001f || matrix.right.y > 0.0001f)
    {
        matrix.right.y = 0.0f;

        F32 len = xVec3Length(&matrix.right);
        if (len < 0.0001f)
        {
            xVec3Init(&matrix.right, 1.0f, 0.0f, 0.0f);
        }
        else
        {
            xVec3SMulBy(&matrix.right, 1.0f / len);
        }
    }

    xVec3Init(&matrix.at, 0.0f, -1.0f, 0.0f);
    xVec3Init(&matrix.up, -matrix.right.z, 0.0f, matrix.right.x);

    RwMatrixUpdate((RwMatrixTag*)&matrix);

    NMEC_RenderProjTexture(rast, factor, &matrix, radius, height, cache, fillCache, ent);
}

xShadowSimpleCache* NMEC_SimpShadowCacheReserve()
{
    xShadowSimpleCache* da_simpShad = NULL;
    S32 i;

    for (i = 0; i < 64; i++)
    {
        if (!g_simpShadsInUseFlags[i])
        {
            g_simpShadsInUseFlags[i] = 1;
            da_simpShad = &g_simpShads[i];
            break;
        }
    }

    if (da_simpShad != NULL)
    {
        memset(da_simpShad, 0, sizeof(xShadowSimpleCache));
        da_simpShad->flags = 0xB;
    }

    return da_simpShad;
}

void NMEC_SimpShadowCacheRelease(xShadowSimpleCache* entShad)
{
    S32 i;

    for (i = 0; i < 64; i++)
    {
        if (entShad == &g_simpShads[i])
        {
            g_simpShadsInUseFlags[i] = 0;
        }
    }
}
