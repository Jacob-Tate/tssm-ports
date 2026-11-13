#include "zNMEHazard.h"
#include "zNMEHazardTypeInfo.h"
#include "zNMEParFX.h"
#include "zNMESupport.h"
#include "zNMESupplement.h"
#include "zNMETypeTiki.h"
#include "xordarray.h"
#include "zGame.h"
#include "zGrid.h"
#include "xMathInlines.h"
#include "iModel.h"
#include "xShadowSimple.h"
#include "zEntPlayerFX.h"
#include "zParPTank.h"
#include "xDebug.h"

#include <stdio.h>

extern _tagGameVidMode gVidMode;

// No symbol in either build; name is ours. The duration of one video frame. Retail evaluates it
// before the other operand of a product even when written second (TuneBorn's random timers), as
// an inline function does and the plain conditional expression does not.
static inline F32 FrameTime()
{
    return gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f);
}

enum en_hazfrag
{
    HAZ_FRAG_GENERIC,
    HAZ_FRAG_TURRBOOM,
    HAZ_FRAG_PHLEGM,
    HAZ_FRAG_FLING_DE,
    HAZ_FRAG_FLING_TR,
    HAZ_FRAG_FLING_JK,
    HAZ_FRAG_WATER_DROPS,
    HAZ_FRAG_CHOC_DROPS,
    HAZ_FRAG_LAVA_DROPS,
    HAZ_FRAG_BILE_DROPS,
    HAZ_FRAG_OIL_DROPS,
    HAZ_FRAG_SAUCE_DROPS,
    HAZ_FRAG_TOXIC_DROPS,
    HAZ_FRAG_GOLF_SPLAT,
    HAZ_FRAG_NOMORE,
    HAZ_FRAG_FORCE = 0x7fffffff
};

enum en_hazsnd
{
    HAZ_SND_FLIGHT,
    HAZ_SND_NOMORE
};

static HAZSndTable g_sndTab_CommonExplode[1] = { { { "CommonExplosion", 0, 0 } } };
static HAZSndTable g_sndTab_CommonImpact[1] = { { { "CommonImpact", 0, 0 } } };
static HAZSndTable g_sndTab_CommonImpactWet[1] = { { { "CommonImpactWet", 0, 0 } } };
static HAZSndTable g_sndTab_TurBall[1] = { { { "TurretProjFlight", 2, 0x1002 } } };
static HAZSndTable g_sndTab_TurBoom[1] = { { { "TurretProjImpact", 0, 0 } } };
static HAZSndTable g_sndTab_FlingBall[1] = { { { "FlingerProjFlight", 2, 0x1002 } } };
static HAZSndTable g_sndTab_FlingSplat[1] = { { { "FlingerProjImpact", 0, 0 } } };
static HAZSndTable g_sndTab_PopBall[1] = { { { "PopperProjFlight", 2, 0x1002 } } };
static HAZSndTable g_sndTab_MervBomb[1] = { { { "MervBombProjFlight", 2, 0x1002 } } };
static HAZSndTable g_sndTab_Mervlet[1] = { { { "MervletProjFlight", 0, 0 } } };
static HAZSndTable g_sndTab_Reflect[1] = { { { "PlayerProjReflect", 2, 0x1002 } } };
static HAZSndTable g_sndTab_GolfBall[1] = { { { "DennisProjFlight", 2, 0x1002 } } };
static HAZSndTable g_sndTab_GolfBoom[1] = { { { "DennisProjSizzle", 0, 0 } } };
static HAZSndTable g_sndTab_DenKnife[1] = { { { "DenTooProjFlight", 2, 0x1002 } } };
static HAZSndTable g_sndTab_DenKnifeBoom[1] = { { { "DenTooProjSizzle", 0, 0 } } };

static HAZSndTable* g_hazardSoundTable[HAZ_TYP_NOMORE] = {
    NULL, // HAZ_TYP_UNKNOWN
    g_sndTab_CommonExplode, // HAZ_TYP_EXPLODE
    g_sndTab_CommonImpact, // HAZ_TYP_IMPACT
    g_sndTab_CommonImpactWet, // HAZ_TYP_IMPACTWET
    g_sndTab_CommonExplode, // HAZ_TYP_IMPACTBOOM
    NULL, // HAZ_TYP_DAZED
    NULL, // HAZ_TYP_SPAWNBALL_XX
    NULL, // HAZ_TYP_SPAWNBALL_GG
    NULL, // HAZ_TYP_SPAWNBALL_TR
    NULL, // HAZ_TYP_SPAWNBALL_PT
    NULL, // HAZ_TYP_SPAWNGROW
    NULL, // HAZ_TYP_BUCKOBOOM_V1
    NULL, // HAZ_TYP_TIKIBOOM
    NULL, // HAZ_TYP_SLAMWAVE_V1
    NULL, // HAZ_TYP_SLAMWAVE_V2
    NULL, // HAZ_TYP_SLAMWAVE_V3
    g_sndTab_FlingBall, // HAZ_TYP_FLING_V1
    g_sndTab_FlingBall, // HAZ_TYP_FLING_V2
    g_sndTab_FlingBall, // HAZ_TYP_FLING_V3
    NULL, // HAZ_TYP_FLING_FROGFISH
    g_sndTab_FlingSplat, // HAZ_TYP_SPITPUDDLE_V1
    g_sndTab_FlingSplat, // HAZ_TYP_SPITPUDDLE_V2
    g_sndTab_FlingSplat, // HAZ_TYP_SPITPUDDLE_V3
    NULL, // HAZ_TYP_ICECREAMSPLAT
    NULL, // HAZ_TYP_SPITSTEAM
    NULL, // HAZ_TYP_SPITSPLASH
    g_sndTab_PopBall, // HAZ_TYP_POPPER_V1
    g_sndTab_PopBall, // HAZ_TYP_POPPER_V2
    g_sndTab_PopBall, // HAZ_TYP_POPPER_V3
    g_sndTab_MervBomb, // HAZ_TYP_MERVBOMB_V1
    g_sndTab_MervBomb, // HAZ_TYP_MERVBOMB_V2
    g_sndTab_MervBomb, // HAZ_TYP_MERVBOMB_V3
    g_sndTab_Mervlet, // HAZ_TYP_MERVLET_V1
    g_sndTab_Mervlet, // HAZ_TYP_MERVLET_V2
    g_sndTab_Mervlet, // HAZ_TYP_MERVLET_V3
    NULL, // HAZ_TYP_ZAPBALL_V1
    NULL, // HAZ_TYP_ZAPBALL_V2
    NULL, // HAZ_TYP_ZAPBOOM_V1
    NULL, // HAZ_TYP_ZAPBOOM_V2
    g_sndTab_TurBall, // HAZ_TYP_TURBALL_V1
    g_sndTab_TurBall, // HAZ_TYP_TURBALL_V2
    g_sndTab_TurBall, // HAZ_TYP_TURBALL_V3
    g_sndTab_TurBoom, // HAZ_TYP_TURBOOM
    g_sndTab_GolfBall, // HAZ_TYP_GOLFBALL
    g_sndTab_GolfBoom, // HAZ_TYP_GOLFBOOM
    NULL, // HAZ_TYP_GOLFJUNK_A
    NULL, // HAZ_TYP_GOLFJUNK_B
    NULL, // HAZ_TYP_GOLFJUNK_C
    NULL, // HAZ_TYP_GOLFJUNK_D
    NULL, // HAZ_TYP_GOLFJUNK_E
    g_sndTab_DenKnife, // HAZ_TYP_DENNIS_KNIFE
    g_sndTab_DenKnifeBoom, // HAZ_TYP_DENNIS_KNIFEBOOM
    NULL, // HAZ_TYP_TUNEBORN
    NULL, // HAZ_TYP_TUNEBANG
    NULL, // HAZ_TYP_TUNEBOOM
    NULL, // HAZ_TYP_TUNEFLYT
    NULL, // HAZ_TYP_SWIMWAKE
    NULL, // HAZ_TYP_BOWWAVE
    NULL, // HAZ_TYP_SPLASH_RING
    NULL, // HAZ_TYP_SPLISH_DROP
    NULL, // HAZ_TYP_VISSPLASH_WAVE
    NULL, // HAZ_TYP_VISSPLASH_DROP
    NULL, // HAZ_TYP_VISSPLASH_WATER
    NULL, // HAZ_TYP_VISSPLASH_CHOC
    NULL, // HAZ_TYP_VISSPLASH_LAVA
    NULL, // HAZ_TYP_VISSPLASH_BILE
    NULL, // HAZ_TYP_VISSPLASH_OIL
    NULL, // HAZ_TYP_VISSPLASH_SAUCE
    NULL, // HAZ_TYP_VISSPLASH_TOXIC
    g_sndTab_Reflect, // HAZ_TYP_REFLECT_V1
    NULL, // HAZ_TYP_PLYRBANG_V1
    g_sndTab_CommonExplode, // HAZ_TYP_POWERUP_EXPLOSION_V1
};

static char* g_strz_hazModel[HAZ_TYP_NOMORE] = {
    "fx_generic_ball.dff", // HAZ_TYP_UNKNOWN
    "fx_shockwave.dff", // HAZ_TYP_EXPLODE
    "fx_generic_ball.dff", // HAZ_TYP_IMPACT
    "fx_generic_ball.dff", // HAZ_TYP_IMPACTWET
    "fx_generic_ball.dff", // HAZ_TYP_IMPACTBOOM
    "fx_dazed.dff", // HAZ_TYP_DAZED
    "fx_bucko_spawnball_xx.dff", // HAZ_TYP_SPAWNBALL_XX
    "fx_bucko_spawnball_gg.dff", // HAZ_TYP_SPAWNBALL_GG
    "fx_bucko_spawnball_tr.dff", // HAZ_TYP_SPAWNBALL_TR
    "fx_bucko_spawnball_pt.dff", // HAZ_TYP_SPAWNBALL_PT
    "fx_generic_ball.dff", // HAZ_TYP_SPAWNGROW
    "fx_bucko_boom.dff", // HAZ_TYP_BUCKOBOOM_V1
    "fx_explode.dff", // HAZ_TYP_TIKIBOOM
    "fx_generic_ball.dff", // HAZ_TYP_SLAMWAVE_V1
    "fx_generic_ball.dff", // HAZ_TYP_SLAMWAVE_V2
    "fx_generic_ball.dff", // HAZ_TYP_SLAMWAVE_V3
    "fx_fling_v1_spitball.dff", // HAZ_TYP_FLING_V1
    "fx_fling_v2_spitball.dff", // HAZ_TYP_FLING_V2
    "fx_fling_v3_spitball.dff", // HAZ_TYP_FLING_V3
    "fx_fling_v2_spitball.dff", // HAZ_TYP_FLING_FROGFISH
    "fx_fling_v1_puddle.dff", // HAZ_TYP_SPITPUDDLE_V1
    "fx_fling_v2_puddle.dff", // HAZ_TYP_SPITPUDDLE_V2
    "fx_fling_v3_puddle.dff", // HAZ_TYP_SPITPUDDLE_V3
    "fx_ice_cream_splat.dff", // HAZ_TYP_ICECREAMSPLAT
    "fx_spitsteam.dff", // HAZ_TYP_SPITSTEAM
    "fx_spitsplash.dff", // HAZ_TYP_SPITSPLASH
    "fx_pop_v1_acidball.dff", // HAZ_TYP_POPPER_V1
    "fx_pop_v2_gumball.dff", // HAZ_TYP_POPPER_V2
    "fx_pop_v3_fireball.dff", // HAZ_TYP_POPPER_V3
    "fx_merv_v1_bomb.dff", // HAZ_TYP_MERVBOMB_V1
    "fx_merv_v2_bomb.dff", // HAZ_TYP_MERVBOMB_V2
    "fx_merv_v3_bomb.dff", // HAZ_TYP_MERVBOMB_V3
    "fx_merv_v1_bomblet.dff", // HAZ_TYP_MERVLET_V1
    "fx_merv_v2_bomblet.dff", // HAZ_TYP_MERVLET_V2
    "fx_merv_v3_bomblet.dff", // HAZ_TYP_MERVLET_V3
    "fx_generic_ball.dff", // HAZ_TYP_ZAPBALL_V1
    "fx_generic_ball.dff", // HAZ_TYP_ZAPBALL_V2
    "fx_generic_ball.dff", // HAZ_TYP_ZAPBOOM_V1
    "fx_generic_ball.dff", // HAZ_TYP_ZAPBOOM_V2
    "fx_tur_v1_tinykeg.dff", // HAZ_TYP_TURBALL_V1
    "fx_tur_v2_lavarock.dff", // HAZ_TYP_TURBALL_V2
    "fx_tur_v3_peanut.dff", // HAZ_TYP_TURBALL_V3
    "fx_explode.dff", // HAZ_TYP_TURBOOM
    "fx_denjunk_blob.dff", // HAZ_TYP_GOLFBALL
    "fx_generic_ball.dff", // HAZ_TYP_GOLFBOOM
    "fx_denjunk_bit.dff", // HAZ_TYP_GOLFJUNK_A
    "fx_denjunk_boot.dff", // HAZ_TYP_GOLFJUNK_B
    "fx_denjunk_bot.dff", // HAZ_TYP_GOLFJUNK_C
    "fx_denjunk_bottle.dff", // HAZ_TYP_GOLFJUNK_D
    "fx_denjunk_bit.dff", // HAZ_TYP_GOLFJUNK_E
    "fx_denhoff_knife.dff", // HAZ_TYP_DENNIS_KNIFE
    "fx_denhoff_knife.dff", // HAZ_TYP_DENNIS_KNIFEBOOM
    "fx_generic_ball.dff", // HAZ_TYP_TUNEBORN
    "fx_tube_bang.dff", // HAZ_TYP_TUNEBANG
    "fx_generic_ball.dff", // HAZ_TYP_TUNEBOOM
    "fx_generic_ball.dff", // HAZ_TYP_TUNEFLYT
    "b3_splash_boots.dff", // HAZ_TYP_SWIMWAKE
    "fx_bow_wave.dff", // HAZ_TYP_BOWWAVE
    "fx_splash_ring.dff", // HAZ_TYP_SPLASH_RING
    "fx_splish_drop.dff", // HAZ_TYP_SPLISH_DROP
    "fx_vissplash_wave.dff", // HAZ_TYP_VISSPLASH_WAVE
    "fx_vissplish_drop.dff", // HAZ_TYP_VISSPLASH_DROP
    "fx_vissplash_wave.dff", // HAZ_TYP_VISSPLASH_WATER
    "fx_vischoc_wave.dff", // HAZ_TYP_VISSPLASH_CHOC
    "fx_vislava_wave.dff", // HAZ_TYP_VISSPLASH_LAVA
    "fx_visbile_wave.dff", // HAZ_TYP_VISSPLASH_BILE
    "fx_visoil_wave.dff", // HAZ_TYP_VISSPLASH_OIL
    "fx_vissauce_wave.dff", // HAZ_TYP_VISSPLASH_SAUCE
    "fx_visnuclear_wave.dff", // HAZ_TYP_VISSPLASH_TOXIC
    "fx_reflect_v1", // HAZ_TYP_REFLECT_V1
    "sb_gauntlet.dff", // HAZ_TYP_PLYRBANG_V1
    "fx_bucko_boom.dff", // HAZ_TYP_POWERUP_EXPLOSION_V1
};

char* g_strz_hazshad[HAZ_TYP_NOMORE] = {
    "", // HAZ_TYP_UNKNOWN
    "", // HAZ_TYP_EXPLODE
    "", // HAZ_TYP_IMPACT
    "", // HAZ_TYP_IMPACTWET
    "", // HAZ_TYP_IMPACTBOOM
    "", // HAZ_TYP_DAZED
    "", // HAZ_TYP_SPAWNBALL_XX
    "", // HAZ_TYP_SPAWNBALL_GG
    "", // HAZ_TYP_SPAWNBALL_TR
    "", // HAZ_TYP_SPAWNBALL_PT
    "", // HAZ_TYP_SPAWNGROW
    "", // HAZ_TYP_BUCKOBOOM_V1
    "", // HAZ_TYP_TIKIBOOM
    "", // HAZ_TYP_SLAMWAVE_V1
    "", // HAZ_TYP_SLAMWAVE_V2
    "", // HAZ_TYP_SLAMWAVE_V3
    "fx_shadow_ball", // HAZ_TYP_FLING_V1
    "fx_shadow_ball", // HAZ_TYP_FLING_V2
    "fx_shadow_ball", // HAZ_TYP_FLING_V3
    "fx_shadow_ball", // HAZ_TYP_FLING_FROGFISH
    "", // HAZ_TYP_SPITPUDDLE_V1
    "", // HAZ_TYP_SPITPUDDLE_V2
    "", // HAZ_TYP_SPITPUDDLE_V3
    "", // HAZ_TYP_ICECREAMSPLAT
    "", // HAZ_TYP_SPITSTEAM
    "", // HAZ_TYP_SPITSPLASH
    "fx_shadow_ball", // HAZ_TYP_POPPER_V1
    "fx_shadow_ball", // HAZ_TYP_POPPER_V2
    "fx_shadow_ball", // HAZ_TYP_POPPER_V3
    "fx_shadow_ball", // HAZ_TYP_MERVBOMB_V1
    "fx_shadow_ball", // HAZ_TYP_MERVBOMB_V2
    "fx_shadow_ball", // HAZ_TYP_MERVBOMB_V3
    "fx_shadow_ball", // HAZ_TYP_MERVLET_V1
    "fx_shadow_ball", // HAZ_TYP_MERVLET_V2
    "fx_shadow_ball", // HAZ_TYP_MERVLET_V3
    "", // HAZ_TYP_ZAPBALL_V1
    "", // HAZ_TYP_ZAPBALL_V2
    "", // HAZ_TYP_ZAPBOOM_V1
    "", // HAZ_TYP_ZAPBOOM_V2
    "fx_shadow_ball", // HAZ_TYP_TURBALL_V1
    "fx_shadow_ball", // HAZ_TYP_TURBALL_V2
    "fx_shadow_ball", // HAZ_TYP_TURBALL_V3
    "", // HAZ_TYP_TURBOOM
    "fx_shadow_ball", // HAZ_TYP_GOLFBALL
    "", // HAZ_TYP_GOLFBOOM
    "", // HAZ_TYP_GOLFJUNK_A
    "", // HAZ_TYP_GOLFJUNK_B
    "", // HAZ_TYP_GOLFJUNK_C
    "", // HAZ_TYP_GOLFJUNK_D
    "", // HAZ_TYP_GOLFJUNK_E
    "fx_shadow_ball", // HAZ_TYP_DENNIS_KNIFE
    "", // HAZ_TYP_DENNIS_KNIFEBOOM
    "", // HAZ_TYP_TUNEBORN
    "", // HAZ_TYP_TUNEBANG
    "", // HAZ_TYP_TUNEBOOM
    "", // HAZ_TYP_TUNEFLYT
    "", // HAZ_TYP_SWIMWAKE
    "", // HAZ_TYP_BOWWAVE
    "", // HAZ_TYP_SPLASH_RING
    "", // HAZ_TYP_SPLISH_DROP
    "", // HAZ_TYP_VISSPLASH_WAVE
    "", // HAZ_TYP_VISSPLASH_DROP
    "", // HAZ_TYP_VISSPLASH_WATER
    "", // HAZ_TYP_VISSPLASH_CHOC
    "", // HAZ_TYP_VISSPLASH_LAVA
    "", // HAZ_TYP_VISSPLASH_BILE
    "", // HAZ_TYP_VISSPLASH_OIL
    "", // HAZ_TYP_VISSPLASH_SAUCE
    "", // HAZ_TYP_VISSPLASH_TOXIC
    "", // HAZ_TYP_REFLECT_V1
    "", // HAZ_TYP_PLYRBANG_V1
    "", // HAZ_TYP_POWERUP_EXPLOSION_V1
};

static zShrapnelAsset* g_data_hazshrap[HAZ_FRAG_NOMORE] = {};

char* g_strz_hazshrap[HAZ_FRAG_NOMORE] = {
    "",
    "shrap_turret_projboom",
    "frag_fx_phlegm",
    "frag_fx_fling_desert",
    "frag_fx_fling_trench",
    "frag_fx_fling_junk",
    "shrap_vissplash_splash",
    "shrap_vischoc_splash",
    "shrap_vislava_splash",
    "shrap_visbile_splash",
    "shrap_visoil_splash",
    "shrap_vissauce_splash",
    "shrap_visnuclear_splash",
    "fx_denjunk_blob",
};

static en_haztyp g_haz_uvModelTypes[] = {
    HAZ_TYP_BUCKOBOOM_V1,    HAZ_TYP_SWIMWAKE,        HAZ_TYP_BOWWAVE,
    HAZ_TYP_SPLASH_RING,     HAZ_TYP_SPLISH_DROP,     HAZ_TYP_FLING_V1,
    HAZ_TYP_FLING_V2,        HAZ_TYP_FLING_V3,        HAZ_TYP_SPITPUDDLE_V1,
    HAZ_TYP_SPITPUDDLE_V2,   HAZ_TYP_SPITPUDDLE_V3,   HAZ_TYP_VISSPLASH_WAVE,
    HAZ_TYP_VISSPLASH_DROP,  HAZ_TYP_VISSPLASH_WATER, HAZ_TYP_VISSPLASH_CHOC,
    HAZ_TYP_VISSPLASH_LAVA,  HAZ_TYP_VISSPLASH_BILE,  HAZ_TYP_VISSPLASH_OIL,
    HAZ_TYP_VISSPLASH_SAUCE, HAZ_TYP_VISSPLASH_TOXIC, HAZ_TYP_FORCE,
};

// Per-type UV animation state, filled by HAZBasic::Stuff_PostInit.
static UVAModelInfo g_haz_uvAnimInfo[HAZ_TYP_NOMORE];

namespace Basic
{
    namespace Shadow
    {
        HAZShadowProps shadProp_basic = { 0.65f, 20.0f, 0.35f, 15.0f, -1, 0.3f };
    } // namespace Shadow
} // namespace Basic

// Shared base of the zNMEHazardTypes.cpp hazard handlers; the scene hooks are inline (weak).
struct HAZBasic : zNMEHazardType
{
    void ScenePrepare()
    {
        shrapass = NULL;
        rawModel = NULL;
        if (uvAnimInfo)
        {
            uvAnimInfo->Clear();
        }
        uvAnimInfo = NULL;
        rast_shadow = NULL;
        shadProps = &Basic::Shadow::shadProp_basic;
        sndTable = g_hazardSoundTable[typ_hazard];
    }
    void SceneFinish()
    {
        if (uvAnimInfo)
        {
            uvAnimInfo->Hemorrage();
        }
        uvAnimInfo = NULL;
    }
    void ScenePostInit()
    {
        Stuff_PostInit();
    }
    void Cleanup(zNMEHazard* haz)
    {
        Stuff_Cleanup(haz);
    }
    void Render(zNMEHazard* haz)
    {
        Stuff_Render(haz, -1.0f);
    }
    void CollideResponse(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                         F32 tymInFuture);

    void Stuff_PostInit();
    inline void Stuff_UVAnimInit();
    void Stuff_Cleanup(zNMEHazard* haz);
    void Stuff_Render(zNMEHazard* haz, F32 customShadowRadius);
    void Stuff_FriendlyFire(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                            F32 tymInFuture);
    void QueueFriendlyFire(zNMEHazard* haz, zNMECommon* npc, const xVec3* pos, F32 dt);
    inline void Stuff_CollRespDflt(zNMEHazard* haz, S32, const xSweptSphere* swdata,
                                   F32 tym_inFuture);
    void DisperseBubWake(zNMEHazard* haz, F32 rad, const xVec3* vel, U32 num_bub);
    void Stuff_HurtObjects(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                           F32 tymInFuture);
    void RenderShadowComplex(zNMEHazard* haz, F32 custRadius);
    inline void RenderShadowSimple(zNMEHazard* haz, F32 custRadius);
};

// Base of the lobbed hazards; adds no virtuals (its weak vtable equals HAZBasic's).
struct HAZLobber : HAZBasic
{
    void LobOrientSpiral(zNMEHazard* haz, F32 spinRate);

    // No symbol in either build (inlined into FlingBlob's and FrogFishBlob's Update; the PS2 line
    // table puts it on one line); name is ours. LobOrientSpiral without the spin: faces the model
    // along its flight velocity.
    void LobOrient(zNMEHazard* haz)
    {
        HAZLob* hazlob = &haz->custdata.lobdata;
        xMat3x3 mat_look;
        xVec3 dir;
        F32 len = xVec3Length(&hazlob->vel);

        if (len > 0.00001f)
        {
            xVec3SMul(&dir, &hazlob->vel, -1.0f / len);
            xMat3x3LookVec(&mat_look, &dir);

            xMat4x3* frame = xModelGetFrame(haz->mdl_hazard);
            xMat3x3Copy(frame, &mat_look);
            xModelSetFrame(haz->mdl_hazard, frame);
        }
    }

    // No symbol in either build (always inlined into GolfBall's, PopRock's and SpawnBall's Update);
    // name is ours. Like LobOrientSpiral, but tumbles the hazard about the right axis of its flight
    // direction; tumbleRate is in revolutions per second. mat_spin goes first for retail's frame.
    void LobOrientTumble(zNMEHazard* haz, F32 tumbleRate)
    {
        HAZLob* hazlob = &haz->custdata.lobdata;
        xMat3x3 mat_spin;
        xMat3x3 mat_look;
        xVec3 dir;
        F32 len = xVec3Length(&hazlob->vel);

        if (len > 0.00001f)
        {
            xVec3SMul(&dir, &hazlob->vel, -1.0f / len);

            xMat3x3LookVec(&mat_look, &dir);
            F32 tym_live = haz->tym_lifespan - haz->tmr_remain;
            xMat3x3RotC(&mat_spin, mat_look.right.x, mat_look.right.y, mat_look.right.z,
                        tym_live * (2.0f * PI * tumbleRate));
            xMat3x3Mul(&mat_look, &mat_look, &mat_spin);

            xMat4x3* frame = xModelGetFrame(haz->mdl_hazard);
            xMat3x3Copy(frame, &mat_look);
            xModelSetFrame(haz->mdl_hazard, frame);
        }
    }
};

namespace PlyrBang
{
    namespace Ribbon
    {
        extern NMEHelpRib::RibData rib_plyrbang;
    } // namespace Ribbon

    // Member functions follow the original source order (the PS2 DWARF lists them
    // reversed); ScenePostInit is the key function that emits the vtable.
    struct HAZPlyrBang : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);

        // No symbol in retail or the PS2 demo (inline). Same body as
        // Reflect::HAZReflect::RibbonUpdate.
        void RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32 dt)
        {
            xFXRibbon* rib = ribdata->ribbon;
            NMEHelpRib::RibData* ribhelp;

            ribdata->cnt_skipRibInsert--;
            if (ribdata->cnt_skipRibInsert > 0)
            {
                return;
            }

            ribhelp = &Ribbon::rib_plyrbang;
            ribdata->cnt_skipRibInsert = ribhelp->updinfo.frq_ribJoint;
            if (ribhelp->flags.dev_enable)
            {
                ribhelp->ConfigRibbon(rib);
            }

            rib->insert(haz->pos_hazard, *(xVec3*)&haz->mdl_hazard->Mat->up,
                        haz->custdata.typical.rad_cur * ribhelp->updinfo.scl_master,
                        ribhelp->updinfo.alf_master, 0);
        }
    };
} // namespace PlyrBang

namespace Reflect
{
    namespace Ribbon
    {
        extern NMEHelpRib::RibData rib_reflect;
    } // namespace Ribbon

    // ScenePostInit is the key function that emits the vtable.
    struct HAZReflect : HAZLobber
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Upd_Ballistic(zNMEHazard* haz, F32 dt);
        void Upd_OwnerHoming(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void BoomRings(const zNMEHazard* haz);
        void CollideResponse(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                             F32 tym_inFuture);
        void RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32 dt);
    };
} // namespace Reflect

namespace Droplet
{
    namespace FX_SeaDrips
    {
        extern SparkyEmitter sea_drips;
    } // namespace FX_SeaDrips

    // ScenePostInit is the key function that emits the vtable.
    struct HAZDroplet : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace Droplet

namespace WaveRing
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZWaveRing : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void LaunchDroplets(zNMEHazard* haz);
    };
} // namespace WaveRing

namespace BowWave
{
    namespace FX_SeaDrips
    {
        extern SparkyEmitter sea_drips;
    } // namespace FX_SeaDrips

    // ScenePostInit is the key function that emits the vtable.
    struct HAZBowWave : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace BowWave

namespace TuneBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZTuneBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void BubbleBlast(zNMEHazard* haz);
    };
} // namespace TuneBoom

namespace TuneFlyt
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZTuneFlyt : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Cleanup(zNMEHazard* haz)
        {
            Stuff_Cleanup(haz);
        }
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace TuneFlyt

namespace TuneBang
{
    // Configure is the key function that emits the vtable.
    struct HAZTuneBang : HAZBasic
    {
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void BubbleCannon(zNMEHazard* haz);
    };
} // namespace TuneBang

namespace TuneBorn
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZTuneBorn : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        // GC only (the PS2 build has this emission written in Update); called from Update.
        void EmitNormalNotes(zNMEHazard* haz);
    };
} // namespace TuneBorn

namespace NyphBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZNyphBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void ViciousFlames(zNMEHazard* haz, F32 dt);
    };
} // namespace NyphBoom

namespace NyphToss
{
    namespace FX
    {
        namespace Ribbon
        {
            extern NMEHelpRib::RibData rib_knife;
        } // namespace Ribbon
    } // namespace FX

    namespace Shadow
    {
        extern HAZShadowProps shadPropData;
    } // namespace Shadow

    // ScenePrepare is the key function that emits the vtable.
    struct HAZNyphToss : HAZLobber
    {
        void ScenePrepare();
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void CollideResponse(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                             F32 tym_inFuture);
        void EndOfLifespan(zNMEHazard* haz);
        void KnifeImpact(zNMEHazard* haz, const xVec3* dir_norm);
        void Recon_Vicious(zNMEHazard* haz, const xVec3* dir_norm, S32 doStick);
    };
} // namespace NyphToss

namespace GolfJunk
{
    // Configure is the key function that emits the vtable.
    struct HAZGolfJunk : HAZBasic
    {
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace GolfJunk

namespace GolfBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZGolfBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace GolfBoom

namespace GolfBall
{
    namespace Shadow
    {
        extern HAZShadowProps shadPropData;
    } // namespace Shadow

    // ScenePrepare is the key function that emits the vtable.
    struct HAZGolfBall : HAZLobber
    {
        void ScenePrepare();
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);

        // No symbol in either build (always inlined into EndOfLifespan); name and signature are
        // ours, after FlingBlob's ShrapSplatter. Launches the splat along the normalized dir_up
        // (straight up without one) at spd_push.
        void ShrapSplatter(zNMEHazard* haz, const xVec3* dir_up, F32 spd_push)
        {
            zShrapnelAsset* shrap = g_data_hazshrap[HAZ_FRAG_GOLF_SPLAT];
            if (shrap == NULL)
            {
                return;
            }

            xVec3 dir = g_Y3;
            if (dir_up)
            {
                F32 len = dir_up->length();
                if (len > 0.00001f)
                {
                    dir = *dir_up * (1.0f / len);
                }
            }

            xVec3 vel_push = dir * spd_push;
            shrap->initCB(shrap, haz->mdl_hazard, &vel_push, NULL);
        }

        void EndOfLifespan(zNMEHazard* haz);
        void ReconToGolfBoom(zNMEHazard* haz, const xVec3* dir_norm);
    };
} // namespace GolfBall

namespace ZapBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZZapBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace ZapBoom

namespace ZapBall
{
    namespace FX
    {
        namespace Ribbon
        {
            extern NMEHelpRib::RibData rib_laser;
        } // namespace Ribbon
    } // namespace FX

    // ScenePostInit is the key function that emits the vtable.
    struct HAZZapBall : HAZLobber
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void ZapBallCollision(zNMEHazard* haz, F32 dt);
        void EndOfLifespan(zNMEHazard* haz);
        void RibbonFinal(zNMEHazard* haz);
        void ReconToZapBoom(zNMEHazard* haz, const xVec3* dir_norm);
        void RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32 dt);
    };
} // namespace ZapBall

namespace Mervlet
{
    namespace Shadow
    {
        extern HAZShadowProps shadPropData;
    } // namespace Shadow

    // ScenePrepare is the key function that emits the vtable.
    struct HAZMervlet : HAZLobber
    {
        void ScenePrepare();
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void EndOfLifespan(zNMEHazard* haz);
        void MervletCollision(zNMEHazard* haz, F32 dt);
        void ReconNukem(zNMEHazard* haz);
    };
} // namespace Mervlet

namespace MervBomb
{
    namespace Shadow
    {
        extern HAZShadowProps shadPropData;
    } // namespace Shadow

    // No symbol in either build (always inlined); name and optional delta out-parameter are ours,
    // after zNMECommon::XZDstSqToPlayer (BfBB's zNPCCommon one). Retail keeps the delta in a stack
    // vector and sums unfused squares, as the NPC version does through its optional pointer.
    static inline F32 XZDstSqToPlayer(const zNMEHazard* haz, xVec3* vec)
    {
        xVec3 dir;

        if (vec == NULL)
        {
            vec = &dir;
        }

        xVec3Sub(vec, &haz->pos_hazard,
                 (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
        vec->y = 0.0f;
        return xVec3Length2(vec);
    }

    // No symbol in either build (always inlined into HAZMervBomb::Update); name is ours. Bursts
    // late in the flight, or once the bomb is within 7.5 units of the player horizontally.
    static inline S32 TimeToBurst(zNMEHazard* haz)
    {
        if (haz->pam_interp < 0.1f)
        {
            return 0;
        }

        if (haz->pam_interp > 0.85f)
        {
            return 1;
        }

        if (XZDstSqToPlayer(haz, NULL) < 56.25f)
        {
            return 1;
        }
        return 0;
    }

    // ScenePrepare is the key function that emits the vtable.
    struct HAZMervBomb : HAZLobber
    {
        void ScenePrepare();
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void MervBomCollision(zNMEHazard* haz, F32 dt);
        // GC only (the PS2 DWARF has no entry); called from Update.
        void AirburstBoomFX(zNMEHazard* haz);
        void ReconAsBoom(zNMEHazard* haz);
        S32 KickBomblets(const zNMEHazard* parent);
        // GC only: weak in the main .text section right after Update, its only caller (the PS2
        // build inlines the explosion into Update).
        void EndOfLifespan(zNMEHazard* haz)
        {
            ReconAsBoom(haz);
        }
    };
} // namespace MervBomb

namespace PopRock
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZPopRock : HAZLobber
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        // RockCollision is GC only (the PS2 demo has no entry; it does the collision inside
        // Update); EndOfLifespan is in both builds.
        void RockCollision(zNMEHazard* haz, F32 dt);
        void EndOfLifespan(zNMEHazard* haz);
        inline void RockShatter(zNMEHazard* haz, const xVec3* dir_emit);
    };
} // namespace PopRock

namespace IceCreamSplat
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZIceCreamSplat : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);

        // No symbol in either build (Update's whole radius block is one PS2 line); named after
        // HAZPuddle::UpdRadius, whose shape it follows. Grows to rad_max and shrinks back to
        // rad_min at the end.
        F32 UpdRadius(zNMEHazard* haz)
        {
            HAZTypical* typical = &haz->custdata.typical;

            if (haz->pam_interp <= 0.15f)
            {
                F32 rat = haz->pam_interp / 0.15f;
                typical->rad_cur = SMOOTH(rat, typical->rad_min, typical->rad_max);
            }
            else if (haz->pam_interp >= 0.9f)
            {
                F32 pct = (haz->pam_interp - 0.9f) / (1.0f - 0.9f);
                pct = CLAMP(pct, 0.0f, 1.0f);
                typical->rad_cur = SMOOTH(pct, typical->rad_max, typical->rad_min);
            }
            else
            {
                typical->rad_cur = typical->rad_max;
            }

            return typical->rad_cur;
        }
    };
} // namespace IceCreamSplat

namespace FrogFishBlob
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZFrogFishBlob : HAZLobber
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);

        // No symbol in either build (always inlined into EndOfLifespan); name and signature are
        // ours, after FlingBlob's ShrapSplatter, which is called from the same three places. This
        // one ignores dir_up.
        void ShrapSplatter(zNMEHazard* haz, const xVec3* dir_up)
        {
            zShrapnelAsset* shrap = g_data_hazshrap[HAZ_FRAG_PHLEGM];
            xVec3 vel_push = xVec3::m_Null;

            shrap->initCB(shrap, haz->mdl_hazard, &vel_push, NULL);
        }

        // No symbol in either build (inlined into Update, which splats the phlegm on Patrick's
        // model; the PS2 DWARF lists no velocity local); name is ours.
        void SplatPhlegm(xModelInstance* mdl)
        {
            zShrapnelAsset* shrap = g_data_hazshrap[HAZ_FRAG_PHLEGM];
            xVec3 vel_push = xVec3::m_Null;

            shrap->initCB(shrap, mdl, &vel_push, NULL);
        }
        void EndOfLifespan(zNMEHazard* haz);
        void ReconBlob(zNMEHazard* haz);
    };
} // namespace FrogFishBlob

namespace Puddle
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZPuddle : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);

        // No GC symbol (inlined into Update and dead-stripped; the PS2 demo has it out of line).
        F32 UpdRadius(zNMEHazard* haz);
    };
} // namespace Puddle

namespace FlingBlob
{
    // No GC or PS2 symbol (inlined into ShrapSplatter); the name is ours, the signature from the
    // Xbox build's out-of-line copy. Same formula as zNMECommon::ReviewCollBounce's bounce.
    inline void ReflectOffNormal(xVec3* vec, const xVec3* dir_norm, F32 fac_elastic)
    {
        xVec3 vec_perp = *dir_norm * xVec3Dot(vec, dir_norm);
        *vec -= vec_perp * (1.0f + fac_elastic);
    }

    namespace Shadow
    {
        extern HAZShadowProps shadPropData;
    } // namespace Shadow

    // ScenePrepare is the key function that emits the vtable.
    struct HAZFlingBlob : HAZLobber
    {
        void ScenePrepare();
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void BlobCollision(zNMEHazard* haz, F32 dt);
        void EndOfLifespan(zNMEHazard* haz);

        void ShrapSplatter(zNMEHazard* haz, const xVec3* dir_up);
        void ReconBlob(zNMEHazard* haz, S32 forceItAnyway);
    };
} // namespace FlingBlob

namespace SlamWaveLG
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZSlamWaveLG : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
    };
} // namespace SlamWaveLG

namespace SlamWaveMD
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZSlamWaveMD : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
    };
} // namespace SlamWaveMD

namespace SlamWaveSM
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZSlamWaveSM : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
    };
} // namespace SlamWaveSM

namespace TurBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZTurBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace TurBoom

namespace TurBall
{
    namespace Shadow
    {
        extern HAZShadowProps shadPropData;
    } // namespace Shadow

    // ScenePrepare is the key function that emits the vtable.
    struct HAZTurBall : HAZLobber
    {
        void ScenePrepare();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void TurProjCollision(zNMEHazard* haz, F32 dt);
        void EndOfLifespan(zNMEHazard* haz);
        void ReconBoom(zNMEHazard* haz);
    };
} // namespace TurBall

namespace SpawnGrow
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZSpawnGrow : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void EmitNormalNotes(zNMEHazard* haz);
    };
} // namespace SpawnGrow

namespace SpawnBall
{
    namespace Ribbon
    {
        extern NMEHelpRib::RibData rib_trail;
    } // namespace Ribbon

    // ScenePostInit is the key function that emits the vtable.
    struct HAZSpawnBall : HAZLobber
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
        void RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32 dt);
    };
} // namespace SpawnBall

namespace Impact
{
    // Configure is the key function that emits the vtable.
    struct HAZImpact : HAZBasic
    {
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
    };
} // namespace Impact

namespace TikiBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZTikiBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };

    // xGridCheckBound callback of the tiki explosion.
    struct ExplodeCollChkData
    {
        zNMECommon* npc_orig;
        xBound bound;

        U8 operator()(xEnt& ent);
    };
} // namespace TikiBoom

namespace MachoBowlExplosion
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZMachoBowlExplosion : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace MachoBowlExplosion

namespace BuckOBoom
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZBuckOBoom : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace BuckOBoom

namespace Ballistic
{
    // Configure is the key function that emits the vtable.
    struct HAZBallistic : HAZLobber
    {
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace Ballistic

namespace Explode
{
    // ScenePostInit is the key function that emits the vtable.
    struct HAZExplode : HAZBasic
    {
        void ScenePostInit();
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace Explode

namespace Generic
{
    // Configure is the key function that emits the vtable.
    struct HAZGeneric : HAZBasic
    {
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
    };
} // namespace Generic

namespace Exist
{
    // Configure is the key function that emits the vtable.
    struct HAZExist : HAZBasic
    {
        en_hazrc Configure(zNMEHazard* haz);
        void Update(zNMEHazard* haz, F32 dt);
        void Render(zNMEHazard* haz);
    };
} // namespace Exist

struct HAZTypeRecord
{
    en_haztyp typ_haz;
    zNMEHazardType* cb_haz;
};

// No symbol in either build (always inlined into the Configure of the UV-scrolling hazard types;
// the PS2 demo shows the same flag-clearing lookup); name is ours. Drops the hazard's UV-animation
// flag when its type has no usable UV info.
inline UVAModelInfo* HAZ_UVAnimInfo(zNMEHazard* haz)
{
    UVAModelInfo* uva = GlobalData::haztypeHandlers[haz->typ_hazard]->uvAnimInfo;

    if (uva == NULL)
    {
        haz->flg_hazard &= ~0x100000;
        return NULL;
    }

    if (!uva->Valid())
    {
        haz->flg_hazard &= ~0x100000;
        return NULL;
    }

    return uva;
}

// No symbol in either build (always inlined); name is ours. Retail treats the model position like
// an inline's return value (as with HAZ_TypeHandler); a plain cast swaps registers at the
// HAZ_SpawnAt sites.
inline xVec3* HAZ_ModelPos(const zNMEHazard* haz)
{
    return (xVec3*)&haz->mdl_hazard->Mat->pos;
}

// No GC or PS2 symbol (inlined into the hazards that spawn others); name is ours. Starts a hazard
// of the given type for npc at pos; parameter order from the Xbox build's out-of-line copy. Retail
// emits the failed Configure first in every user.
inline void HAZ_SpawnAt(const xVec3* pos, en_haztyp haztype, zNMECommon* npc, F32 tym)
{
    zNMEHazard* haz = HAZ_Acquire();

    if (haz != NULL)
    {
        if (!haz->ConfigHelper(haztype))
        {
            haz->flg_hazard |= 0x4;
        }
        else
        {
            haz->npc_owner = npc;
            haz->Start(pos, tym);
        }
    }
}

// No symbol in either build (always inlined into the lobbed hazards' Update; the PS2 demo puts the
// whole launch velocity on one source line); name is ours. It clamps the flight time itself:
// retail loads tmr_remain before the 0.00001f literal, i.e. the clamp sees an evaluated argument.
inline void HAZ_LobLaunchVel(zNMEHazard* haz, F32 tym_left, F32 acc_grav)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 tym_flight = MAX(0.00001f, tym_left);

    hazlob->vel = hazlob->pos_tgt - haz->pos_hazard;
    hazlob->vel /= tym_flight;
    hazlob->vel.y += acc_grav * (0.5f * tym_flight);
}

// Launch toward pos_tgt using the remaining lifetime as the flight time. No symbol; name is ours.
// Retail inlines the velocity math one level deeper than the caller's own inlines (ZapBall::Update
// lays out its stack temporaries after the ribbon updates'), so it is a wrapper of its own.
inline void HAZ_LobLaunchRemain(zNMEHazard* haz, F32 acc_grav)
{
    HAZ_LobLaunchVel(haz, haz->tmr_remain, acc_grav);
}

// No symbol in either build (inlined into FlingBlob's and Puddle's Update, which put the whole test
// on one PS2 source line); name is ours. In the Neptune boss scene, the blobs go away while a
// cutscene runs or the player has no control.
inline S32 HAZ_ByeByeForCine()
{
    if (globals.sceneCur == NULL)
    {
        return 0;
    }

    if (globals.sceneCur->sceneID != 'B401')
    {
        return 0;
    }

    if (globals.cmgr != NULL && globals.cmgr->csn != NULL)
    {
        return 1;
    }

    if (globals.player.ControlOff)
    {
        return 1;
    }

    return 0;
}

// No symbol in either build (inlined into FlingBlob's and FrogFishBlob's Update); name is ours.
// Launches a lobbed hazard on its first frame; a wrapper of its own, one inline level deeper, as
// retail's frame layout shows.
inline void HAZ_LobLaunch(zNMEHazard* haz, F32 acc_grav)
{
    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchVel(haz, haz->tmr_remain, acc_grav);
    }

    if (haz->flg_hazard & 8)
    {
        haz->PreCollide(acc_grav, 0.0f);
    }
}

// The sound helpers have no symbol in either build (always inlined); names are ours.
inline void HAZ_SndStop(iSndHandle snd)
{
    if (snd)
    {
        xSndMgrStop(snd);
    }
}

inline iSndHandle HAZ_SndPlay(const HAZSndTable* table, zNMEHazard* haz, en_hazsnd which)
{
    const HAZSndGrpEntry* grp;
    const xVec3* vel;
    iSndHandle snd;

    if (haz->flg_custom & 1)
    {
        return (iSndHandle)0;
    }

    grp = &table->grpEntry[which];
    if (!grp->sndGrpID)
    {
        return (iSndHandle)0;
    }

    vel = NULL;
    if (grp->flg_sndPlay & 0x1000)
    {
        vel = &haz->custdata.typical.vel;
    }

    snd = xSndMgrPlay(grp->sndGrpID, grp->flg_sndPlay, (xVec3*)&haz->mdl_hazard->Mat->pos, vel,
                      NULL, NULL, NULL);
    if (grp->flg_hazSnd & 2)
    {
        return snd;
    }
    return (iSndHandle)0;
}

// No symbol in either build (inlined into Puddle's and FlingBlob's Update in both); name is ours.
// True in the Neptune boss arena while a cutscene plays or the player's controls are off: the
// spit puddles and blobs are cleared away then.
inline S32 HAZ_NeptuneCineActive()
{
    if (globals.sceneCur == NULL)
    {
        return 0;
    }

    if (globals.sceneCur->sceneID != 'B401')
    {
        return 0;
    }

    if (globals.cmgr != NULL && globals.cmgr->csn != NULL)
    {
        return 1;
    }

    if (globals.player.ControlOff)
    {
        return 1;
    }

    return 0;
}

// No GC symbol (always inlined; the name is ours): does this hazard type animate its UVs?
static S32 HAZ_UsesUVAnim(en_haztyp haztyp)
{
    S32 found = 0;

    for (S32 i = 0; g_haz_uvModelTypes[i] != HAZ_TYP_FORCE; i++)
    {
        if (haztyp == g_haz_uvModelTypes[i])
        {
            found = 1;
            break;
        }
    }
    return found;
}

// No GC symbol (always inlined into Stuff_PostInit; the name is ours). Retail's registers show
// the UV setup was a helper of its own rather than code written in Stuff_PostInit.
inline void HAZBasic::Stuff_UVAnimInit()
{
    en_haztyp typ = typ_hazard;

    if (HAZ_UsesUVAnim(typ))
    {
        UVAModelInfo* info = &g_haz_uvAnimInfo[typ];
        info->Init(rawModel, 0);
        info->UVVelSet(0.0f, 0.0f);
        uvAnimInfo = info;
    }
}

// 11 compiled-out placeholders (conditions unknown) keep GC/2.6 from auto-inlining this, as retail
// calls it from all 32 ScenePostInit callers.
void HAZBasic::Stuff_PostInit()
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    en_haztyp typ = typ_hazard;

    if (g_strz_hazModel[typ][0] != '\0')
    {
        rawModel = (RpAtomic*)NMEC_FindAsset(xStrHash(g_strz_hazModel[typ]));
    }

    if (g_strz_hazshad[typ][0] != '\0')
    {
        rast_shadow = NMEC_FindRWRaster(g_strz_hazshad[typ]);
    }

    if (rawModel != NULL)
    {
        Stuff_UVAnimInit();
    }
}

void HAZBasic::Stuff_Cleanup(zNMEHazard* haz)
{
    if (sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if (haz->mdl_hazard)
    {
        if (haz->mdl_hazard)
        {
            xModelInstanceFree(haz->mdl_hazard);
        }
        haz->mdl_hazard = NULL;
    }

    if (haz->shadCacheComplex)
    {
        NMEC_ShadowCacheRelease(haz->shadCacheComplex);
        haz->shadCacheComplex = NULL;
    }

    if (haz->shadCacheSimple)
    {
        NMEC_SimpShadowCacheRelease(haz->shadCacheSimple);
        haz->shadCacheSimple = NULL;
    }

    if (haz->custdata.collide.parabinfo)
    {
        NMEC_ParabRelease(haz->custdata.collide.parabinfo);
        haz->custdata.collide.parabinfo = NULL;
    }

    if (haz->fxdata.ribdata.ribbon)
    {
        zFXRibbonPoolDelete(haz->fxdata.ribdata.ribbon);
        haz->fxdata.ribdata.ribbon = NULL;
    }

    for (S32 i = 0; i < 4; i++)
    {
        if (haz->fxdata.ribdata.riblist[i])
        {
            zFXRibbonPoolDelete(haz->fxdata.ribdata.riblist[i]);
            haz->fxdata.ribdata.riblist[i] = NULL;
        }
    }

    for (S32 i = 0; i < 4; i++)
    {
        if (haz->fxdata.lytdata.lytlist[i])
        {
            zLightningKill(haz->fxdata.lytdata.lytlist[i]);
            haz->fxdata.lytdata.lytlist[i] = NULL;
        }
    }

    haz->flg_hazard = 0;
}

void HAZBasic::Stuff_Render(zNMEHazard* haz, F32 customShadowRadius)
{
    // Neither is in the PS2 DWARF: retail loads the model into a register kept across the calls
    // (the first xModelRender reuses that load) and materializes the shadow test as a bool.
    xModelInstance* minst = haz->mdl_hazard;
    bool doShadow = rast_shadow && shadProps && (haz->shadCacheComplex || haz->shadCacheSimple);

    if (!doShadow)
    {
        xModelRender(haz->mdl_hazard);
    }
    else
    {
        HAZShadowProps* props = shadProps;
        S32 cullShadowToo = 0;
        xVec3 shadVec = *(xVec3*)&haz->mdl_hazard->Mat->pos;
        shadVec.y -= props->hyt_shadow;

        xSphere* sph = iModelWorldBoundSphere(minst->Data, minst->Mat, NULL);
        S32 cull = xModelSphereCullPlusShadow(sph, &shadVec, &cullShadowToo);
        if (!cull)
        {
            xModelRender(minst);
        }

        if (!cullShadowToo)
        {
            if (haz->shadCacheComplex)
            {
                RenderShadowComplex(haz, customShadowRadius);
            }
            else if (haz->shadCacheSimple)
            {
                RenderShadowSimple(haz, customShadowRadius);
            }
        }
    }
}

// No symbol in either build (always inlined into the Dennis-owned hazards' Update); name is ours.
// Looks a stage up in a DEN_STAGE_UNKNOWN-terminated list.
inline S32 HAZ_DenStageInList(en_denstage stage, const en_denstage* list)
{
    S32 found = 0;

    while (*list != DEN_STAGE_UNKNOWN)
    {
        if (stage == *list)
        {
            found = 1;
            break;
        }
        list++;
    }

    return found;
}

// No GC symbol (always inlined into CollideResponse); the PS2 symbol has an unnamed S32 parameter.
inline void HAZBasic::Stuff_CollRespDflt(zNMEHazard* haz, S32, const xSweptSphere* swdata,
                                         F32 tym_inFuture)
{
    HAZCollide* hazcol = &haz->custdata.collide;
    xParabola* parab = hazcol->parabinfo;
    F32 tym_alive = MIN(haz->tym_lifespan - haz->tmr_remain, haz->tym_lifespan);
    xEnt* ent_hit;

    haz->tmr_remain = MIN(haz->tmr_remain, tym_inFuture);
    parab->maxTime = tym_alive + haz->tmr_remain;
    haz->tym_lifespan = parab->maxTime;

    xParabolaEvalPos(parab, &hazcol->pos_collide, parab->maxTime);
    hazcol->dir_normal = swdata->worldNormal;

    ent_hit = (xEnt*)swdata->optr;
    if (ent_hit == NULL || ent_hit->baseType != eBaseTypeStatic)
    {
        haz->flg_hazard |= 0x40000;
    }
}

// No GC symbol: a plain member auto-inlined into its only caller and dead-stripped (its static
// table keeps a plain typesWeCanHurt$N name). Name unknown.
void HAZBasic::Stuff_HurtObjects(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                                 F32 tymInFuture)
{
    static const en_ZBASETYPE typesWeCanHurt[] = {
        eBaseTypePlatform, eBaseTypeStatic,      eBaseTypeDynamic, eBaseTypePendulum,
        eBaseTypeHangable, eBaseTypeHangableObj, eBaseTypeButton,  eBaseTypeDestructObj,
        eBaseTypeBoulder,  eBaseTypeUnknown,
    };

    if ((flg_hazcolf & 2) == 2 && swdata->optr != NULL)
    {
        xBase* ent_hit = (xBase*)swdata->optr;
        U8 typ_hit = ent_hit->baseType;
        S32 canHurt = 0;

        for (const en_ZBASETYPE* type = typesWeCanHurt; *type != eBaseTypeUnknown; type++)
        {
            if (typ_hit == *type)
            {
                canHurt = 1;
                break;
            }
        }

        if (canHurt)
        {
            F32 fparam[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            zEntEvent(haz->npc_owner, 0, ent_hit, eEventHit, fparam, NULL, 0, FE_NO);
        }
    }
}

void HAZBasic::CollideResponse(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                               F32 tymInFuture)
{
    Stuff_CollRespDflt(haz, flg_hazcolf, swdata, tymInFuture);

    if ((haz->flg_hazard & 0x8000000) && (flg_hazcolf & 2))
    {
        Stuff_HurtObjects(haz, flg_hazcolf, swdata, tymInFuture);
    }

    if ((haz->flg_hazard & 0x4000000) && (flg_hazcolf & 4))
    {
        Stuff_FriendlyFire(haz, flg_hazcolf, swdata, tymInFuture);
    }
}

void HAZBasic::Stuff_FriendlyFire(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                                  F32 tymInFuture)
{
    if ((flg_hazcolf & 4) == 4 && swdata->optr != NULL &&
        ((xBase*)swdata->optr)->baseType == eBaseTypeNPC)
    {
        xVec3 dir_hit = swdata->worldNormal;
        QueueFriendlyFire(haz, (zNMECommon*)swdata->optr, &dir_hit, tymInFuture);
    }
}

void HAZBasic::QueueFriendlyFire(zNMEHazard* haz, zNMECommon* npc, const xVec3* pos, F32 dt)
{
    if (dt < 0.25f)
    {
        npc->Damage(DMGTYP_FRIENDLYFIRE, haz->npc_owner, pos, -1, ePowerupLevelNormal);
    }
    else
    {
        NMEMsg msg;

        memset(&msg, 0, sizeof(NMEMsg));
        msg.msgid = NME_MID_DAMAGE;
        msg.sendto = npc->id;
        msg.from = haz->npc_owner ? haz->npc_owner->id : 0;
        msg.infotype = NME_MDAT_DAMAGE;
        msg.dmgdata.dmg_type = DMGTYP_FRIENDLYFIRE;
        msg.dmgdata.dmg_from = haz->npc_owner;
        msg.dmgdata.vec_dmghit = *pos;
        msg.dmgdata.amt_damage = -1;
        msg.dmgdata.fac_powerup = 0;
        zNMEMsg_SendMsg(&msg, npc, dt);
    }
}

// Emits a burst of bubbles behind the hazard (opposite its velocity), scattered over the plane of
// the model's up and right axes.
void HAZBasic::DisperseBubWake(zNMEHazard* haz, F32 rad, const xVec3* vel, U32 num_bub)
{
    F32 dst_disperse;
    F32 dst_behind;
    xVec3 pos_disperse;
    xVec3 vel_disperse;
    xVec3 dir_backward;
    xVec3 pos_emit;

    if (num_bub > 100)
    {
        num_bub = 100;
    }

    dst_disperse = 0.5f * rad;
    dst_behind = 1.5f * rad;

    pos_disperse = *(xVec3*)&haz->mdl_hazard->Mat->up * (dst_disperse * (2.0f * (xurand() - 0.5f)));
    pos_disperse +=
        *(xVec3*)&haz->mdl_hazard->Mat->right * (dst_disperse * (2.0f * (xurand() - 0.5f)));

    vel_disperse = *(xVec3*)&haz->mdl_hazard->Mat->up * (8.0f * (2.0f * (xurand() - 0.5f)));
    vel_disperse += *(xVec3*)&haz->mdl_hazard->Mat->right * (8.0f * (2.0f * (xurand() - 0.5f)));

    dir_backward = *vel;
    dir_backward.invert();
    // not in the DWARF: the PS2 line table gives the length its own line before the test.
    // SQR (macro), not SQ (inline): the inline's F32 parameters change the float register colouring.
    F32 len = xsqrt(SQR(dir_backward.x) + SQR(dir_backward.y) + SQR(dir_backward.z));
    if (len > 0.00001f)
    {
        dir_backward /= len;
    }
    else
    {
        dir_backward = g_O3;
    }

    pos_emit = haz->pos_hazard;
    pos_emit += dir_backward * dst_behind;

    zFX_SpawnBubbleTrail(&pos_emit, num_bub, &pos_disperse, &vel_disperse);
}

// The divide is its own statement and the unclamped fade goes through alf_shadow, which gives
// retail's float registers. 4 compiled-out placeholders (conditions unknown) push the inline cost
// past GC/2.6's limit (512), as retail calls it from Stuff_Render.
void HAZBasic::RenderShadowComplex(zNMEHazard* haz, F32 custRadius)
{
    HAZShadowProps* props = shadProps;
    F32 val;
    F32 alf_shadow;
    xMat4x3 mat_fake;
    F32 radius;
    S32 doRefill;

    if (props == NULL)
    {
        return;
    }

    xASSERT_PLACEHOLDER();

    haz->cnt_nextFill--;

    val = NMEC_ds2_toCam(&haz->pos_hazard, NULL);
    if (val > SQ(props->rad_doCast))
    {
        return;
    }

    xASSERT_PLACEHOLDER();

    val /= SQ(props->rad_doCast);
    alf_shadow = 1.0f - val + props->fac_maxEarly;
    val = CLAMP(alf_shadow, 0.0f, 1.0f);
    alf_shadow = val * props->alf_max;

    xASSERT_PLACEHOLDER();

    mat_fake.right = *(xVec3*)&haz->mdl_hazard->Mat->right;
    mat_fake.at = g_NY3;
    mat_fake.up = *(xVec3*)&haz->mdl_hazard->Mat->at;
    mat_fake.pos = haz->pos_hazard;

    radius = custRadius;
    if (custRadius < 0.0f)
    {
        radius = props->rad_shadow;
    }

    xASSERT_PLACEHOLDER();

    doRefill = haz->cnt_nextFill < 0;
    NMEC_RenderProjTexture(rast_shadow, alf_shadow, &mat_fake, radius, props->hyt_shadow,
                           haz->shadCacheComplex, doRefill, NULL);

    if (doRefill)
    {
        haz->cnt_nextFill = props->cacheFillFreq;
    }
}

// No symbol in either build (inlined into RenderShadowSimple); the simple-shadow counterpart of
// NMEC_RenderProjTexture, name and parameters are ours.
static inline void HAZ_RenderSimpShadow(RwRaster* rast, F32 factor, const xMat4x3* mat, F32 radius,
                                        F32 height, xShadowSimpleCache* cache, S32 fillCache)
{
    cache->ptr_raster = rast;
    if (fillCache)
    {
        cache->collPriority += 5;
    }
    xShadowSimple_Add_Expert(cache, mat, CLAMP(255.0f * factor, 0.0f, 255.0f), radius, height,
                             1.0f);
}

// No GC symbol (always inlined into Stuff_Render).
// The divide is its own statement, which gives retail's float registers for the fade and clamp.
inline void HAZBasic::RenderShadowSimple(zNMEHazard* haz, F32 custRadius)
{
    HAZShadowProps* props = shadProps;
    F32 val;
    F32 alf_shadow;
    S32 doRefill;
    F32 radius;

    if (props == NULL)
    {
        return;
    }

    haz->cnt_nextFill--;

    val = NMEC_ds2_toCam(&haz->pos_hazard, NULL);
    if (val > SQ(props->rad_doCast))
    {
        return;
    }

    val /= SQ(props->rad_doCast);
    val = 1.0f - val + props->fac_maxEarly;
    val = CLAMP(val, 0.0f, 1.0f);
    alf_shadow = val * props->alf_max;

    doRefill = haz->cnt_nextFill < 0;

    radius = custRadius;
    if (custRadius < 0.0f)
    {
        radius = props->rad_shadow;
    }

    HAZ_RenderSimpShadow(rast_shadow, alf_shadow, (xMat4x3*)haz->mdl_hazard->Mat, radius,
                         props->hyt_shadow, haz->shadCacheSimple, doRefill);

    if (doRefill)
    {
        haz->cnt_nextFill = props->cacheFillFreq;
    }
}

en_hazrc Exist::HAZExist::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZTypical* typical = &haz->custdata.typical;

    haz->flg_hazard |= 0x19180;
    if (haz->typ_hazard == HAZ_TYP_DAZED)
    {
        haz->flg_hazard |= 0x2000000;
    }

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    typical->rad_min = 0.5f;
    typical->rad_max = 0.5f;
    typical->rad_cur = 0.5f;
    typical->vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void Exist::HAZExist::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    typical->rad_cur = xlerp(typical->rad_min, typical->rad_max, haz->pam_interp);
}

void Exist::HAZExist::Render(zNMEHazard* haz)
{
    haz->mdl_hazard->Scale = haz->custdata.typical.rad_cur;
    Stuff_Render(haz, -1.0f);
}

namespace Generic
{
    namespace Render
    {
        static const xVec3 scl_size = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace Generic

en_hazrc Generic::HAZGeneric::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x19380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 3.0f;
    haz->custdata.typical.rad_min = 0.1f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3 * 5.0f;

    return HAZ_RC_SUCCESS;
}

void Generic::HAZGeneric::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if (haz->pam_interp > 0.25f)
    {
        typical->rad_cur = typical->rad_max;
    }
    else
    {
        typical->rad_cur = LERP(4.0f * haz->pam_interp, typical->rad_min, typical->rad_max);
    }

    haz->pos_hazard += typical->vel * dt;
}

// No PS2 symbol (the demo writes this inline in the lobbed hazards' Update); the parameter is the
// spin rate in revolutions per second, the names are ours. 15 compiled-out placeholders (conditions
// unknown) push its inline cost past GC/2.6's limit (512), as retail calls it.
void HAZLobber::LobOrientSpiral(zNMEHazard* haz, F32 spinRate)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    HAZLob* hazlob = &haz->custdata.lobdata;
    xMat3x3 mat_look;
    xMat3x3 mat_spin;
    xVec3 dir;
    F32 len = xVec3Length(&hazlob->vel);

    if (len > 0.00001f)
    {
        xVec3SMul(&dir, &hazlob->vel, -1.0f / len);

        xMat3x3LookVec(&mat_look, &dir);
        F32 tym_live = haz->tym_lifespan - haz->tmr_remain;
        F32 ang_spun = tym_live * (2.0f * PI * spinRate);
        xMat3x3RotC(&mat_spin, mat_look.at.x, mat_look.at.y, mat_look.at.z, ang_spun);
        xMat3x3Mul(&mat_look, &mat_look, &mat_spin);

        haz->TypData_RotMatSet(&mat_look);
    }
}

namespace Explode
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter puffBoomage = {
            NPAR_TYP_RADIOACTIVE,
            0.35f,
            { 0x78, 0x78, 0x78, 0x4B },
            { 0.5f, 0.95f },
            { 0.0f, 7.0f, 0.0f },
            7.2f,
            98,
            0,
            4,
            0,
            { 1.0f, -2.5f },
        };
        static const PuffEmitter puffSmokage = {
            NPAR_TYP_DUSTPUFF,
            0.45f,
            { 0x32, 0x32, 0x32, 0xAF },
            { 0.85f, 1.05f },
            { 0.0f, 20.0f, 0.0f },
            7.2f,
            99,
            0,
            4,
            0,
            { 1.0f, -3.5f },
        };
    } // namespace FX
} // namespace Explode

void Explode::HAZExplode::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RADIOACTIVE, NULL, 0);
}

en_hazrc Explode::HAZExplode::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0xe00b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.83f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 1.5f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3 * 0.0f;

    return HAZ_RC_SUCCESS;
}

void Explode::HAZExplode::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    typical->rad_cur = LERP(QUB(haz->pam_interp), typical->rad_min, typical->rad_max);

    haz->pos_hazard += typical->vel * dt;

    if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
    {
        if (haz->ColPlyrSphere(0.75f * typical->rad_cur))
        {
            haz->HurtThePlayer();
        }
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.75f)
    {
        HAZTypical* typdat = &haz->custdata.typical;

        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f, 1);
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 11.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        if (haz->pam_interp < 0.8f)
        {
            F32 radius = 0.75f * typical->rad_cur;

            if (!haz->cnt_generic)
            {
                FX::puffBoomage.RoundRing(&haz->pos_hazard, radius, NULL);
            }
            FX::puffSmokage.RoundRing(&haz->pos_hazard, radius, NULL);
        }
    }
}

void Explode::HAZExplode::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 0.55f, 0.0f));

    rad = 2.2f * typical->rad_cur;
    haz->mdl_hazard->Scale = Explode::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace Ballistic
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace Ballistic

en_hazrc Ballistic::HAZBallistic::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0x19380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;
    hazlob->flg_collide |= 7;

    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 0.35f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

void Ballistic::HAZBallistic::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 tym;

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 8)
    {
        hazlob->vel = hazlob->pos_tgt - haz->pos_hazard;
        hazlob->vel /= haz->tmr_remain;
        hazlob->vel.y += 3.0f * (0.5f * haz->tmr_remain);
    }

    if (haz->flg_hazard & 8)
    {
        haz->PreCollide(3.0f, 0.0f);
    }

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);
}

void Ballistic::HAZBallistic::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    rad = hazlob->rad_cur;
    haz->mdl_hazard->Scale = Ballistic::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace BuckOBoom
{
    namespace Render
    {
        static const xVec3 scl_wide = { 1.0f, 0.75f, 1.0f };
        static const xVec3 scl_tall = { 0.5f, 2.0f, 0.5f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter puffBoomage = {
            NPAR_TYP_RADIOACTIVE,
            0.5f,
            { 0x78, 0x78, 0x78, 0x4B },
            { 1.3f, 2.2f },
            { 0.0f, 7.0f, 0.0f },
            7.2f,
            99,
            0,
            4,
            0,
            { 1.0f, -2.5f },
        };
        static const PuffEmitter puffSmokage = {
            NPAR_TYP_DUSTPUFF,
            0.75f,
            { 0x32, 0x32, 0x32, 0xAF },
            { 1.25f, 2.25f },
            { 0.0f, 20.0f, 0.0f },
            7.2f,
            99,
            0,
            3,
            0,
            { 1.0f, -3.5f },
        };
    } // namespace FX
} // namespace BuckOBoom

void BuckOBoom::HAZBuckOBoom::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RADIOACTIVE, NULL, 0);
}

en_hazrc BuckOBoom::HAZBuckOBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0xe00b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.83f;
    haz->custdata.typical.rad_min = 1.25f;
    haz->custdata.typical.rad_max = 7.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

    return HAZ_RC_SUCCESS;
}

void BuckOBoom::HAZBuckOBoom::Update(zNMEHazard* haz, F32 dt)
{
    F32 rad_collide;
    F32 radius;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    haz->custdata.typical.rad_cur =
        LERP(1.0f - QUB(1.0f - haz->pam_interp), haz->custdata.typical.rad_min,
             haz->custdata.typical.rad_max);

    if (haz->flg_hazard & 8)
    {
        zFX_SpawnBubbleSlam(&haz->pos_hazard, 50, PI, 15.0f, 2.5f);
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.65f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        rad_collide = 0.75f * haz->custdata.typical.rad_cur;

        if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
        {
            if (haz->ColPlyrSphere(rad_collide))
            {
                haz->HurtThePlayer();
            }
        }

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, rad_collide, HAZ_ModelPos(haz), -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, rad_collide, HAZ_ModelPos(haz), -1.0f, 1);
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        if (haz->pam_interp < 0.8f)
        {
            radius = 0.75f * haz->custdata.typical.rad_cur;

            if (!haz->cnt_generic)
            {
                FX::puffBoomage.RoundRing(&haz->pos_hazard, radius, NULL);
            }
            FX::puffSmokage.RoundRing(&haz->pos_hazard, radius, NULL);
        }
    }
}

void BuckOBoom::HAZBuckOBoom::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(SMOOTH(haz->pam_interp, 0.75f, 0.0f));

    rad = 2.5f * typical->rad_cur;
    haz->mdl_hazard->Scale = BuckOBoom::Render::scl_wide * rad;

    Stuff_Render(haz, -1.0f);
}

namespace MachoBowlExplosion
{
    namespace Render
    {
        static const xVec3 scl_wide = { 1.0f, 1.0f, 1.0f };
        static const xVec3 scl_tall = { 0.5f, 2.0f, 0.5f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter puffBoomage = {
            NPAR_TYP_RADIOACTIVE,
            0.5f,
            { 0x78, 0x78, 0x78, 0x4B },
            { 0.5f, 1.25f },
            { 0.0f, 7.0f, 0.0f },
            7.2f,
            98,
            0,
            3,
            0,
            { 1.0f, -1.0f },
        };
        static const PuffEmitter puffSmokage = {
            NPAR_TYP_DUSTPUFF,
            0.75f,
            { 0x32, 0x32, 0x32, 0xAA },
            { 1.15f, 1.65f },
            { 0.0f, 15.0f, 0.0f },
            7.2f,
            99,
            0,
            4,
            0,
            { 1.0f, -1.0f },
        };
    } // namespace FX
} // namespace MachoBowlExplosion

void MachoBowlExplosion::HAZMachoBowlExplosion::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RADIOACTIVE, NULL, 0);
}

en_hazrc MachoBowlExplosion::HAZMachoBowlExplosion::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x2009180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 0.1f;
    haz->custdata.typical.rad_max = 5.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

    return HAZ_RC_SUCCESS;
}

void MachoBowlExplosion::HAZMachoBowlExplosion::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typdat = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    typdat->rad_cur = LERP(QUB(haz->pam_interp), typdat->rad_min, typdat->rad_max);

    if (haz->flg_hazard & 8)
    {
        zFX_SpawnBubbleSlam(&haz->pos_hazard, 50, PI, 15.0f, 2.5f);
    }

    if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
    {
        if (haz->ColPlyrSphere(typdat->rad_cur))
        {
            haz->HurtThePlayer();
        }
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.5f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f, 1);
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        if (haz->pam_interp < 0.8f)
        {
            F32 radius = 0.75f * typdat->rad_cur;

            if (!haz->cnt_generic)
            {
                FX::puffBoomage.RoundRing(&haz->pos_hazard, radius, NULL);
            }
            FX::puffSmokage.RoundRing(&haz->pos_hazard, radius, NULL);
        }
    }
}

void MachoBowlExplosion::HAZMachoBowlExplosion::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(SMOOTH(haz->pam_interp, 0.75f, 0.0f));

    rad = 2.0f * typical->rad_cur;
    haz->mdl_hazard->Scale = MachoBowlExplosion::Render::scl_wide * rad;

    Stuff_Render(haz, -1.0f);
}

namespace TikiBoom
{
    namespace Render
    {
        static const xVec3 scl_wide = { 1.0f, 0.7f, 1.0f };
        static const xVec3 scl_tall = { 0.5f, 2.0f, 0.5f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter puffBoomage = {
            NPAR_TYP_RADIOACTIVE,
            0.5f,
            { 0x78, 0x78, 0x78, 0x4B },
            { 0.65f, 1.35f },
            { 0.0f, 5.0f, 0.0f },
            7.2f,
            99,
            0,
            5,
            0,
            { 1.0f, -2.5f },
        };
        static const PuffEmitter puffSmokage = {
            NPAR_TYP_DUSTPUFF,
            0.65f,
            { 0x32, 0x32, 0x32, 0x96 },
            { 0.85f, 1.5f },
            { 0.0f, 25.0f, 0.0f },
            7.2f,
            98,
            0,
            5,
            0,
            { 1.0f, -3.5f },
        };
    } // namespace FX
} // namespace TikiBoom

void TikiBoom::HAZTikiBoom::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RADIOACTIVE, NULL, 0);
}

en_hazrc TikiBoom::HAZTikiBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x200b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.88f;
    haz->custdata.typical.rad_min = 0.1f;
    haz->custdata.typical.rad_max = 5.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

    return HAZ_RC_SUCCESS;
}

// No GC or PS2 symbol (its code is part of Update in both builds); the name is ours.
static inline void TikiBoom_ExplodeCollide(zNMEHazard* haz, const xVec3* pos_center)
{
    TikiBoom::ExplodeCollChkData chkdata;

    memset(&chkdata, 0, sizeof(TikiBoom::ExplodeCollChkData));
    chkdata.npc_orig = haz->npc_owner;
    chkdata.bound.type = XBOUND_TYPE_SPHERE;
    chkdata.bound.sph.r = 5.0f;
    chkdata.bound.sph.center = *pos_center;

    xQuickCullForSphere(&xqc_def_ctrl, &chkdata.bound.qcd, &chkdata.bound.sph);
    xGridCheckBound(npcs_grid, chkdata.bound, chkdata.bound.qcd, chkdata);
    xGridCheckBound(colls_grid, chkdata.bound, chkdata.bound.qcd, chkdata);
}

// No GC or PS2 symbol, name ours: retail materializes the result (1/0 on each path) before
// testing it after the hazard flag, which the plain '||' expression does not reproduce.
static inline S32 TikiBoom_PlayerSmashes()
{
    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        return 1;
    }
    if (gCurrentPlayer == eCurrentPlayerSpongePat)
    {
        return 1;
    }
    if (zEntPlayerIsInSlidingMode())
    {
        return 1;
    }
    return 0;
}

void TikiBoom::HAZTikiBoom::Update(zNMEHazard* haz, F32 dt)
{
    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    haz->custdata.typical.rad_cur =
        LERP(haz->pam_interp, haz->custdata.typical.rad_min, haz->custdata.typical.rad_max);

    if (haz->flg_hazard & 8)
    {
        zFX_SpawnBubbleSlam(&haz->pos_hazard, 50, PI, 15.0f, 2.5f);
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.62f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        F32 rad_collide = 0.75f * haz->custdata.typical.rad_cur;

        if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
        {
            if (haz->ColPlyrSphere(rad_collide))
            {
                haz->HurtThePlayer();
            }
        }

        TikiBoom_ExplodeCollide(haz, (xVec3*)&haz->mdl_hazard->Mat->pos);
    }

    // not in the DWARF: plyrSmashes. The PS2 line table gives this test its own line before the
    // hazard-flag test, and retail evaluates it first (calling zEntPlayerIsInSlidingMode even when
    // the flag is clear).
    S32 plyrSmashes = TikiBoom_PlayerSmashes();

    if ((haz->flg_hazard & 0x2000) && plyrSmashes)
    {
        F32 rad_collide = 0.75f * haz->custdata.typical.rad_max;

        if (haz->ColPlyrSphere(rad_collide))
        {
            if (gCurrentPlayer == eCurrentPlayerCar)
            {
                F32 parms[4] = { -1.0f, 0.0f, 0.0f, 0.0f };
                zEntEvent(haz->npc_owner, 0, xglobals->___player_ent_dont_use_directly,
                          eEventGiveHealth, parms, NULL, 0, FE_NO);
            }
            else if (gCurrentPlayer == eCurrentPlayerSpongePat)
            {
                F32 hitArgs[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                zEntEvent(haz->npc_owner, 0, xglobals->___player_ent_dont_use_directly, eEventHit,
                          hitArgs, NULL, 0, FE_NO);
            }
            else
            {
                haz->HurtThePlayer();
            }
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        if (haz->pam_interp < 0.8f)
        {
            F32 radius = 0.75f * haz->custdata.typical.rad_cur;

            if (!haz->cnt_generic)
            {
                FX::puffBoomage.RoundRing(&haz->pos_hazard, radius, NULL);
            }
            FX::puffSmokage.RoundRing(&haz->pos_hazard, radius, NULL);
        }
    }
}

void TikiBoom::HAZTikiBoom::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(SMOOTH(haz->pam_interp, 0.75f, 0.0f));

    rad = 2.0f * typical->rad_cur;
    haz->mdl_hazard->Scale = TikiBoom::Render::scl_wide * rad;

    Stuff_Render(haz, -1.0f);
}

// Retail calls this from xGridCheckBound<TikiBoom::ExplodeCollChkData>. The body has 28 statements,
// so two compiled-out placeholders (conditions unknown) keep GC/2.6 from auto-inlining it (>= 30
// statements).
U8 TikiBoom::ExplodeCollChkData::operator()(xEnt& ent)
{
    xCollis colrec;
    zNMECommon* npc_hit;
    en_npcdmg typ_dmg;
    en_npctyp ntyp_hit;

    xASSERT_PLACEHOLDER();

    if (&ent == npc_orig)
    {
        return 1;
    }

    if (!(ent.chkby & XENT_COLLTYPE_NPC))
    {
        return 1;
    }

    memset(&colrec, 0, sizeof(xCollis));

    xASSERT_PLACEHOLDER();

    if (ent.collLev == 5)
    {
        xBoundHitsModel(&bound, ent.model, &colrec, 1, PI / 4.0f);
    }
    else
    {
        xBoundHitsBound(&bound, &ent.bound, &colrec);
    }

    if (!(colrec.flags & 1))
    {
        return 1;
    }

    switch (ent.baseType)
    {
    case eBaseTypeNPC:
        npc_hit = (zNMECommon*)&ent;
        if (!npc_hit->flg_vuln)
        {
            break;
        }

        typ_dmg = DMGTYP_NMEATTACK;
        if (npc_orig->itemType == NME_TYPE_TIKI_THUNDER)
        {
            typ_dmg = DMGTYP_THUNDER_TIKI_EXPLOSION;
        }

        ntyp_hit = (en_npctyp)npc_hit->itemType;
        if (ntyp_hit >= NME_TYPE_TIKI && ntyp_hit <= NME_TYPE_TIKI_STONE)
        {
            ((zNMETiki*)npc_hit)->timeToLive = 0.00001f;
        }

        npc_hit->Damage(typ_dmg, npc_orig, &colrec.tohit, -1, ePowerupLevelNormal);
        break;
    case eBaseTypeButton:
        break;
    case eBaseTypeStatic:
    case eBaseTypeDestructObj:
    {
        F32 hitArgs[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        zEntEvent(npc_orig, 0, &ent, eEventHit, hitArgs, NULL, 0, FE_NO);
        break;
    }
    }

    return 1;
}

namespace Impact
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace Impact

en_hazrc Impact::HAZImpact::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.1f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 2.25f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void Impact::HAZImpact::Update(zNMEHazard* haz, F32 dt)
{
    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }
}

namespace SpawnBall
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace Ribbon
    {
        static const xFXRibbon::curve_node curvedata[7] = {
            { 0.0f, { 0xFF, 0x96, 0xFF, 0xFF }, 0.0f }, { 0.2f, { 0xFF, 0xFF, 0x00, 0xFF }, 1.0f },
            { 0.3f, { 0x00, 0xFF, 0xFF, 0xC8 }, 1.5f }, { 0.4f, { 0x64, 0xFF, 0x9B, 0xE1 }, 0.8f },
            { 0.6f, { 0xFF, 0x9B, 0x64, 0xC8 }, 1.2f }, { 0.8f, { 0x64, 0x64, 0xFF, 0x5C }, 0.6f },
            { 1.0f, { 0x00, 0xC8, 0xFF, 0x00 }, 0.0f },
        };

        NMEHelpRib::RibData rib_trail = {
            { 1, 2, 0, 0 },
            { "fx_spawn_trail", (xFXRibbon::curve_node*)curvedata, 7, 0.75f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };
    } // namespace Ribbon
} // namespace SpawnBall

void SpawnBall::HAZSpawnBall::ScenePostInit()
{
    Stuff_PostInit();
    Ribbon::rib_trail.FindTexture();
}

en_hazrc SpawnBall::HAZSpawnBall::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0x1019280;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;

    haz->flg_hazard |= 0x100;

    hazlob->flg_collide = 0;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    if (Ribbon::rib_trail.flags.enable)
    {
        haz->fxdata.ribdata.ribbon = zFXRibbonPoolNew();
    }
    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        Ribbon::rib_trail.ConfigRibbon(haz->fxdata.ribdata.ribbon);
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.35f;
    hazlob->rad_max = 0.75f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

// keepFlightTime is a const local (the PS2 DWARF drops the qualifier): GC/2.6 then loads tmr_remain
// for it separately from the inlined PreCollide's update, as retail does.
void SpawnBall::HAZSpawnBall::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    // The PS2 line table sets rat on its own line before the size ramp. Retail scales by a
    // reciprocal literal created at optimization time (numbered after every parse-time literal),
    // which needs the divisor to be a propagated variable rather than a constant.
    F32 rat = 0.15f;

    if (haz->pam_interp > rat)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP((1.0f / rat) * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 8)
    {
        hazlob->vel = hazlob->pos_tgt - haz->pos_hazard;
        hazlob->vel /= haz->tmr_remain;
        hazlob->vel.y += 10.0f * (0.5f * haz->tmr_remain);
    }

    if (haz->flg_hazard & 8)
    {
        const F32 keepFlightTime = haz->tmr_remain;
        haz->PreCollide(10.0f, 0.0f);
        haz->tmr_remain = keepFlightTime;
        haz->tym_lifespan = keepFlightTime;
    }

    F32 tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrientTumble(haz, 1.0f);

    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        RibbonUpdate(haz, &haz->fxdata.ribdata, dt);
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        DisperseBubWake(haz, hazlob->rad_cur, &hazlob->vel, 16);
    }
}

void SpawnBall::HAZSpawnBall::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(0.2f);

    rad = 1.2f * hazlob->rad_cur;
    haz->mdl_hazard->Scale = SpawnBall::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

void SpawnBall::HAZSpawnBall::RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32 dt)
{
    xFXRibbon* rib = ribdata->ribbon;
    NMEHelpRib::RibData* ribhelp;
    F32 scale;
    F32 alpha;
    F32 ang;

    ribdata->cnt_skipRibInsert--;
    if (ribdata->cnt_skipRibInsert > 0)
    {
        return;
    }

    ribhelp = &Ribbon::rib_trail;
    ribdata->cnt_skipRibInsert = ribhelp->updinfo.frq_ribJoint;
    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(rib);
    }

    scale = haz->custdata.lobdata.rad_cur * ribhelp->updinfo.scl_master;
    alpha = ribhelp->updinfo.alf_master;

    ang = 2.0f * PI * xfmod(4.5f * haz->pam_interp, 1.0f) - PI;
    // not in the DWARF: retail makes both calls before reading Mat.
    F32 ang_cos = icos(ang);
    F32 ang_sin = isin(ang);
    xVec3 orient = *(xVec3*)&haz->mdl_hazard->Mat->up * ang_sin +
                   *(xVec3*)&haz->mdl_hazard->Mat->right * ang_cos;

    rib->insert(haz->pos_hazard, orient, scale, alpha, 0);
}

namespace SpawnGrow
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX_Notes
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.0f },
            { 0.15f, { 0xFF, 0x00, 0x00, 0x7F }, 1.0f },
            { 0.3f, { 0xFF, 0x00, 0xFF, 0x7F }, 0.75f },
            { 0.45f, { 0x00, 0x00, 0xFF, 0x7F }, 0.9f },
            { 0.6f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.5f },
            { 0.75f, { 0x00, 0xFF, 0x00, 0x7F }, 0.7f },
            { 0.9f, { 0xFF, 0xFF, 0x00, 0x7F }, 0.35f },
            { 1.0f, { 0xF0, 0xF0, 0x64, 0x00 }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.25f, 0.0f };
        static const xVec3 vec_offplyr = { 1.0f, -0.15f, 0.0f };

        static const ExhaustEmitter notes = {
            NPAR_TYP_NOTES,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 4.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 8, 0.5f, 0.6f },
        };
    } // namespace FX_Notes

    namespace FX_SuperNotes
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.0f },
            { 0.15f, { 0xFF, 0x00, 0x00, 0x7F }, 1.0f },
            { 0.3f, { 0xFF, 0x00, 0xFF, 0x7F }, 0.75f },
            { 0.45f, { 0x00, 0x00, 0xFF, 0x7F }, 0.9f },
            { 0.6f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.5f },
            { 0.75f, { 0x00, 0xFF, 0x00, 0x7F }, 0.7f },
            { 0.9f, { 0xFF, 0xFF, 0x00, 0x7F }, 0.35f },
            { 1.0f, { 0xF0, 0xF0, 0x64, 0x00 }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.25f, 0.0f };
        static const xVec3 vec_offplyr = { 1.0f, -0.15f, 0.0f };

        static const ExhaustEmitter notes = {
            NPAR_TYP_NOTES_UPGRADE,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.15f, -0.15f },
            { 3.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 8, 1.0f, 1.0f },
        };

        // No symbol in either build (always inlined into HAZSpawnGrow::Update); name is ours.
        // Same shape as TuneFlyt::FX_Thrust::EmitThrust; the PS2 DWARF of Update lists no
        // locals besides pup.
        static inline void EmitSuperNotes(zNMEHazard* haz)
        {
            xVec3 pos_emit = haz->pos_hazard;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * vec_offset.x;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * vec_offset.y;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * vec_offset.z;

            notes.EmitRing(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at, -100000.0f, 1.0f, -1,
                           NULL);
        }
    } // namespace FX_SuperNotes

    namespace FX_Waves
    {
        OrientCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 1.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.15f, 0.0f };

        static const OrientEmitter waves = {
            NPAR_TYP_RKOWAVES,
            { 0, 0, 0, 0 },
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace FX_Waves

    namespace FX_Smokey
    {
        ExhaustCurve curve_data[7] = {
            { 0.0f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.0f },
            { 0.33f, { 0xFF, 0xFF, 0x00, 0x7F }, 1.0f },
            { 0.33f, { 0xFF, 0xFF, 0x00, 0x7F }, 1.0f },
            { 0.66f, { 0xFF, 0x00, 0xFF, 0x7F }, 1.0f },
            { 0.66f, { 0xFF, 0x00, 0xFF, 0x7F }, 1.0f },
            { 0.99f, { 0x32, 0x32, 0x32, 0x7F }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.35f, 0.0f };

        static const ExhaustEmitter exhaust = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { 2.0f, 0.0f, 10.0f },
            { 0.5f, 0.5f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.76f, 0.3f },
            { 30, 0, 0.25f, 0.0f },
            { -5.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 7, 1.0f, 0.5f },
        };

        // No symbol in either build (always inlined into HAZSpawnGrow::Update); name is ours.
        // Same shape as TuneFlyt::FX_Thrust::EmitThrust.
        static inline void EmitSmokey(zNMEHazard* haz)
        {
            xVec3 pos_emit = haz->pos_hazard;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * vec_offset.x;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * vec_offset.y;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * vec_offset.z;

            xVec3 dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->at * -1.0f;

            exhaust.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1, NULL);
        }
    } // namespace FX_Smokey

    namespace FX_Sparks
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.0f, 0.35f };

        static const SparkyEmitter sparker = {
            NPAR_TYP_SPARKY,
            { 0, 0, { 0, 0 } },
            { 16.0f, 7.0f },
            { 0.02f, 0.25f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.75f, 0.5f },
            { 30, 20, 0.1f, -0.1f },
            { 15.0f, 10.0f, 1.0f, 0.5f, -10.0f, 2.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 0.25f },
        };
    } // namespace FX_Sparks
} // namespace SpawnGrow

void SpawnGrow::HAZSpawnGrow::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_RKOWAVES, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_NOTES, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_NOTES_UPGRADE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_EXHAUST, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_HEATED, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SPARKY, NULL, 0);
}

en_hazrc SpawnGrow::HAZSpawnGrow::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 1.0f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

// The S8 on the random bit is from retail (extrwi + extsb: a signed char value; plain char is
// unsigned here).
void SpawnGrow::HAZSpawnGrow::Update(zNMEHazard* haz, F32 dt)
{
    haz->custdata.typical.rad_cur =
        xlerp(haz->custdata.typical.rad_min, haz->custdata.typical.rad_max, haz->pam_interp);

    en_plyrpup pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR);
    if (pup > ePowerupLevelNormal)
    {
        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = 12.0f * FrameTime();
            FX_SuperNotes::EmitSuperNotes(haz);
        }

        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = 15.0f * FrameTime();
        }

        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = (1 + (S8)((xrand_GenRandInt32() & 0x800000) != 0)) * FrameTime();
        }

        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = (12 + (S8)((xrand_GenRandInt32() & 0x800000) != 0)) * FrameTime();
            FX_Smokey::EmitSmokey(haz);
        }
    }
    else
    {
        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = 7.0f * FrameTime();
            EmitNormalNotes(haz);
        }
    }
}

void SpawnGrow::HAZSpawnGrow::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 1.0f, 1.0f));

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = SpawnGrow::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

void SpawnGrow::HAZSpawnGrow::EmitNormalNotes(zNMEHazard* haz)
{
    xVec3 pos_emit = haz->pos_hazard;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * FX_Notes::vec_offset.x;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * FX_Notes::vec_offset.y;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * FX_Notes::vec_offset.z;
    FX_Notes::notes.EmitRing(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at, -100000.0f, 1.0f, -1,
                             NULL);

    xVec3 pos_plyr = haz->pos_hazard;
    pos_plyr += *(xVec3*)&haz->mdl_hazard->Mat->right * FX_Notes::vec_offplyr.x;
    pos_plyr += *(xVec3*)&haz->mdl_hazard->Mat->up * FX_Notes::vec_offplyr.y;
    pos_plyr += *(xVec3*)&haz->mdl_hazard->Mat->at * FX_Notes::vec_offplyr.z;
    FX_Notes::notes.EmitRing(&pos_plyr,
                             (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->at,
                             -100000.0f, 1.0f, -1, NULL);
}

namespace TurBall
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace TurBall

HAZShadowProps TurBall::Shadow::shadPropData = { 0.4f, 20.0f, 0.35f, 15.0f, -1, 0.3f };

void TurBall::HAZTurBall::ScenePrepare()
{
    HAZBasic::ScenePrepare();
    shadProps = &Shadow::shadPropData;
}

en_hazrc TurBall::HAZTurBall::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xc61b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;

    hazlob->flg_collide |= 7;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xFD;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.35f;
    hazlob->rad_max = 0.35f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

// keepFlightTime is const, as in SpawnBall::HAZSpawnBall::Update (retail's separate tmr_remain load).
// The launch clamps the flight time once, inside the launch helper (as the PS2 demo shows).
void TurBall::HAZTurBall::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;

    if (haz->pam_interp > 0.1f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(10.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        EndOfLifespan(haz);
        return;
    }

    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchRemain(haz, 0.0f);

        if (sndTable)
        {
            haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
        }
    }

    if (haz->flg_hazard & 8)
    {
        const F32 keepFlightTime = haz->tmr_remain;
        haz->PreCollide(0.0f, 0.0f);
        haz->tmr_remain = MIN(haz->tmr_remain, keepFlightTime);
        haz->tym_lifespan = haz->tmr_remain;
        parab->maxTime = haz->tmr_remain;
    }

    F32 tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrientSpiral(haz, 4.0f);
    TurProjCollision(haz, dt);

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 5.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        xVec3 pos_here = haz->pos_hazard;
        xVec3 pos_past = haz->pos_hazard;
        pos_past -= hazlob->vel * (5.0f * dt);
        zFX_SpawnBubbleTrail(&pos_here, &pos_past, 3, NULL, NULL);
    }
}

void TurBall::HAZTurBall::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    rad = hazlob->rad_cur;
    haz->mdl_hazard->Scale = TurBall::Render::vec_scale * rad;

    Stuff_Render(haz, 2.5f * hazlob->rad_cur);
}

void TurBall::HAZTurBall::TurProjCollision(zNMEHazard* haz, F32 dt)
{
    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            haz->custdata.lobdata.rad_cur, &haz->custdata.lobdata.vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            if (haz->npc_owner == NULL)
            {
                zEntPlayer_Damage(NULL, 1);
            }
            else
            {
                zEntPlayer_DamageNPCKnockBack(haz->npc_owner, 1, &haz->pos_hazard,
                                              ePlayerNMEDamageEnemyProjectile);
            }
        }
        ReconBoom(haz);
    }
    else
    {
        haz->StaggeredCollide();
    }
}

// The normalized dir is unused in retail too; it survives because xVec3Copy is out of line.
// dir is not in the PS2 DWARF: the PS2 build dropped the dead computation (MW MIPS lists no
// optimized-out locals); the name is ours.
void TurBall::HAZTurBall::EndOfLifespan(zNMEHazard* haz)
{
    xParabola* parab = haz->custdata.lobdata.parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        if (!(haz->flg_hazard & 0x40000))
        {
            xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        }

        ReconBoom(haz);
    }
    else
    {
        xVec3 dir;

        xParabolaEvalVel(parab, &dir, parab->maxTime);
        dir.invert();

        F32 len = xVec3Length(&dir);
        if (len > 0.00001f)
        {
            dir /= len;
        }

        ReconBoom(haz);
    }
}

// The asserts are compiled out (conditions unknown); 14 of them push its inline cost past GC/2.6's
// limit (512), as retail calls it.
void TurBall::HAZTurBall::ReconBoom(zNMEHazard* haz)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xVec3 dir_norm = haz->custdata.lobdata.dir_normal;

    if (!haz->Reconfigure(HAZ_TYP_TURBOOM))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (xVec3Length2(&dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, &dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

namespace TurBoom
{
    namespace Render
    {
        static const xVec3 scl_wide = { 1.0f, 0.7f, 1.0f };
        static const xVec3 scl_tall = { 0.5f, 2.0f, 0.5f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter puffBoomage = {
            NPAR_TYP_RADIOACTIVE,
            0.35f,
            { 0x78, 0x78, 0x78, 0x4B },
            { 0.45f, 0.9f },
            { 0.0f, 5.0f, 0.0f },
            7.2f,
            98,
            0,
            3,
            0,
            { 1.0f, -2.5f },
        };
        static const PuffEmitter puffSmokage = {
            NPAR_TYP_DUSTPUFF,
            0.45f,
            { 0x32, 0x32, 0x32, 0xAF },
            { 0.85f, 1.05f },
            { 0.0f, 12.0f, 0.0f },
            7.2f,
            99,
            0,
            3,
            0,
            { 1.0f, -2.5f },
        };
    } // namespace FX
} // namespace TurBoom

void TurBoom::HAZTurBoom::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RADIOACTIVE, NULL, 0);
}

en_hazrc TurBoom::HAZTurBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0xE00B180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.5f;
    haz->custdata.typical.rad_min = 0.2f;
    haz->custdata.typical.rad_max = 1.43f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

    return HAZ_RC_SUCCESS;
}

void TurBoom::HAZTurBoom::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typdat = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    typdat->rad_cur = LERP(haz->pam_interp, typdat->rad_min, typdat->rad_max);

    if (haz->flg_hazard & 8)
    {
        zFX_SpawnBubbleSlam(&haz->pos_hazard, 30, PI, 8.0f, 2.5f);
    }

    if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
    {
        if (haz->ColPlyrSphere(typdat->rad_cur))
        {
            haz->HurtThePlayer();
        }
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.7f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f, 1);
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        if (haz->pam_interp < 0.8f)
        {
            F32 radius = 0.75f * typdat->rad_cur;

            if (!haz->cnt_generic)
            {
                FX::puffBoomage.RoundRing(&haz->pos_hazard, radius, NULL);
            }
            FX::puffSmokage.RoundRing(&haz->pos_hazard, radius, NULL);
        }
    }
}

void TurBoom::HAZTurBoom::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(SMOOTH(haz->pam_interp, 0.75f, 0.0f));

    rad = 2.0f * typical->rad_cur;
    haz->mdl_hazard->Scale = TurBoom::Render::scl_wide * rad;

    Stuff_Render(haz, -1.0f);
}

namespace SlamWaveSM
{
    namespace FX
    {
        static const PuffEmitter puffWave = {
            NPAR_TYP_DUSTPUFF,
            0.35f,
            { 0xFF, 0xFF, 0xFF, 0xAF },
            { 0.15f, 0.7f },
            { 0.0f, -10.0f, 0.0f },
            37.2f,
            92,
            0,
            11,
            0,
            { 1.0f, 5.0f },
        };
    } // namespace FX
} // namespace SlamWaveSM

void SlamWaveSM::HAZSlamWaveSM::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc SlamWaveSM::HAZSlamWaveSM::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.22f;
    haz->custdata.typical.rad_min = 0.4f;
    haz->custdata.typical.rad_max = 0.11f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void SlamWaveSM::HAZSlamWaveSM::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    typical->rad_cur = LERP(haz->pam_interp, typical->rad_min, typical->rad_max);

    if (haz->flg_hazard & 8)
    {
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -1.5f;
        zFX_SpawnBubbleSlam(&pos, 25, PI, 2.5f, 1.5f);
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        F32 spd = 3.0f * xurand() + 4.0f;
        xVec3 vel = g_Y3 * spd;
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -1.5f;
        FX::puffWave.RoundRing(&pos, typical->rad_cur, &vel);
    }
}

namespace SlamWaveMD
{
    namespace FX
    {
        static const PuffEmitter puffWave = {
            NPAR_TYP_DUSTPUFF,
            0.3f,
            { 0x8C, 0x8C, 0x64, 0xAF },
            { 0.25f, 0.7f },
            { 0.0f, -55.0f, 0.0f },
            17.2f,
            99,
            1,
            21,
            0,
            { 1.0f, -1.5f },
        };
    } // namespace FX
} // namespace SlamWaveMD

void SlamWaveMD::HAZSlamWaveMD::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc SlamWaveMD::HAZSlamWaveMD::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.5f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 1.25f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void SlamWaveMD::HAZSlamWaveMD::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    typical->rad_cur = LERP(haz->pam_interp, typical->rad_min, typical->rad_max);

    if (haz->flg_hazard & 8)
    {
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -1.0f;
        zFX_SpawnBubbleSlam(&pos, 40, PI, 4.5f, 0.75f);
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        F32 spd = 5.0f + 6.0f * xurand();
        xVec3 vel = g_Y3 * spd;
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -1.0f;
        FX::puffWave.RoundRing(&pos, typical->rad_cur, &vel);
    }
}

namespace SlamWaveLG
{
    namespace FX
    {
        static const PuffEmitter puffWave = {
            NPAR_TYP_DUSTPUFF,
            1.25f,
            { 0x64, 0x64, 0x64, 0xFF },
            { 0.85f, 1.5f },
            { 0.0f, -3.5f, 0.0f },
            6.2f,
            99,
            1,
            4,
            0,
            { 1.0f, -1.0f },
        };
    } // namespace FX
} // namespace SlamWaveLG

void SlamWaveLG::HAZSlamWaveLG::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc SlamWaveLG::HAZSlamWaveLG::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.5f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 1.25f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void SlamWaveLG::HAZSlamWaveLG::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    typical->rad_cur = LERP(haz->pam_interp, typical->rad_min, typical->rad_max);

    if (haz->flg_hazard & 8)
    {
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -1.25f;
        zFX_SpawnBubbleSlam(&pos, 40, PI, 6.5f, 0.5f);
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        F32 spd = 1.0f + 1.5f * xurand();
        xVec3 vel = g_Y3 * spd;
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -1.25f;
        FX::puffWave.RoundRing(&pos, typical->rad_cur, &vel);
    }
}

namespace FlingBlob
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const xVec3 vec_emitOffset = { 0.0f, 0.0f, -0.35f };
        static const PuffEmitter spitTrail_DE = {
            NPAR_TYP_FLINGVAPORS_DE,
            1.0f,
            { 0x96, 0xC8, 0xC8, 0xC8 },
            { 0.35f, 0.6f },
            { 0.0f, -10.0f, 0.0f },
            7.2f,
            93,
            0,
            3,
            0,
            { 0.2f, 5.0f },
        };
        static const PuffEmitter spitTrail_TR = {
            NPAR_TYP_FLINGVAPORS_TR,
            1.0f,
            { 0x96, 0xC8, 0xC8, 0xC8 },
            { 0.35f, 0.6f },
            { 0.0f, -10.0f, 0.0f },
            7.2f,
            93,
            0,
            3,
            0,
            { 0.2f, 5.0f },
        };
        static const PuffEmitter spitTrail_JK = {
            NPAR_TYP_FLINGVAPORS_JK,
            1.0f,
            { 0x96, 0xC8, 0xC8, 0xC8 },
            { 0.35f, 0.6f },
            { 0.0f, -10.0f, 0.0f },
            7.2f,
            93,
            0,
            3,
            0,
            { 0.2f, 5.0f },
        };
    } // namespace FX
} // namespace FlingBlob

HAZShadowProps FlingBlob::Shadow::shadPropData = { 0.4f, 20.0f, 0.35f, 15.0f, -1, 0.3f };

void FlingBlob::HAZFlingBlob::ScenePrepare()
{
    HAZBasic::ScenePrepare();
    shadProps = &Shadow::shadPropData;
}

void FlingBlob::HAZFlingBlob::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_DE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_TR, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_JK, NULL, 0);
}

en_hazrc FlingBlob::HAZFlingBlob::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xd71b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;

    hazlob->flg_collide |= 7;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xFD;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.35f;
    hazlob->rad_max = 0.35f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    UVAModelInfo* uva = HAZ_UVAnimInfo(haz);
    if (uva)
    {
        uva->UVVelSet(0.0f, -2.0f);
    }

    return HAZ_RC_SUCCESS;
}

// The launch goes through HAZ_LobLaunch like FrogFishBlob's, one inline level deeper, as retail's
// frame layout shows. The PS2 DWARF's 'S32 byebye' is left out: retail keeps no flag across
// statements.
void FlingBlob::HAZFlingBlob::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 tym;
    xVec3 pos_emit;

    if (HAZ_ByeByeForCine())
    {
        haz->flg_hazard |= 0x4;
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }
        return;
    }

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }
        EndOfLifespan(haz);
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    HAZ_LobLaunch(haz, 3.0f);

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrient(haz);

    BlobCollision(haz, dt);

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        pos_emit = haz->pos_hazard;
        pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * FX::vec_emitOffset.x;
        pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * FX::vec_emitOffset.y;
        pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * FX::vec_emitOffset.z;

        if (haz->typ_hazard == HAZ_TYP_FLING_V1)
        {
            FX::spitTrail_DE.Footstroke(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at);
        }
        else if (haz->typ_hazard == HAZ_TYP_FLING_V2)
        {
            FX::spitTrail_TR.Footstroke(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at);
        }
        else if (haz->typ_hazard == HAZ_TYP_FLING_V3)
        {
            FX::spitTrail_JK.Footstroke(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at);
        }
        else if (typ_hazard == HAZ_TYP_FLING_FROGFISH)
        {
            FX::spitTrail_TR.Footstroke(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at);
        }
        else
        {
            haz->flg_hazard |= 0x4;
        }
    }
}

void FlingBlob::HAZFlingBlob::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(SMOOTH(haz->pam_interp, 0.25f, 0.25f));

    rad = 7.0f * hazlob->rad_cur;
    haz->mdl_hazard->Scale = FlingBlob::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

void FlingBlob::HAZFlingBlob::BlobCollision(zNMEHazard* haz, F32 dt)
{
    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            haz->custdata.lobdata.rad_cur, &haz->custdata.lobdata.vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            NMEC_PlayerSlip();
            haz->HurtThePlayer();
        }

        HAZ_SpawnAt(HAZ_ModelPos(haz), HAZ_TYP_IMPACTWET, haz->npc_owner, -1.0f);
        ShrapSplatter(haz, &g_Y3);
        haz->flg_hazard |= 0x4;
    }
    else
    {
        haz->StaggeredCollide();
    }
}

// Same statement order as the PS2 demo and the Xbox build. The fallback direction is haz->Up(), as
// in BfBB's NPCHazard::Upd_TTFlight; the impact is spawned at HAZ_ModelPos like the other
// HAZ_SpawnAt sites.
void FlingBlob::HAZFlingBlob::EndOfLifespan(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        ShrapSplatter(haz, &hazlob->dir_normal);

        if (!(haz->flg_hazard & 0x40000))
        {
            ReconBlob(haz, 0);
        }
        else
        {
            ReconBlob(haz, 1);
        }
    }
    else
    {
        xVec3 whence;

        xParabolaEvalVel(parab, &whence, parab->maxTime);
        whence.invert();

        // not in the DWARF: the length is computed once, then compared and divided by.
        F32 len = xVec3Length(&whence);
        if (len > 0.00001f)
        {
            whence /= len;
            ShrapSplatter(haz, &whence);
        }
        else
        {
            ShrapSplatter(haz, haz->Up());
        }

        ReconBlob(haz, 1);
    }

    HAZ_SpawnAt(HAZ_ModelPos(haz), HAZ_TYP_IMPACTWET, haz->npc_owner, -1.0f);
}

// No GC symbol: auto-inlined into BlobCollision and EndOfLifespan and dead-stripped (out of line in
// the PS2 build). Launches the blob's splatter, its parabola velocity bounced off dir_up and
// damped.
void FlingBlob::HAZFlingBlob::ShrapSplatter(zNMEHazard* haz, const xVec3* dir_up)
{
    en_hazfrag ftyp = HAZ_FRAG_GENERIC;
    zShrapnelAsset* shrap;
    xParabola* parab;
    xVec3 vel_push;

    if (haz->typ_hazard == HAZ_TYP_FLING_V1)
    {
        ftyp = HAZ_FRAG_FLING_DE;
    }
    else if (haz->typ_hazard == HAZ_TYP_FLING_V2)
    {
        ftyp = HAZ_FRAG_FLING_TR;
    }
    else if (haz->typ_hazard == HAZ_TYP_FLING_V3)
    {
        ftyp = HAZ_FRAG_FLING_JK;
    }

    if (ftyp == HAZ_FRAG_GENERIC)
    {
        return;
    }

    shrap = g_data_hazshrap[ftyp];
    if (shrap == NULL)
    {
        return;
    }

    parab = haz->custdata.lobdata.parabinfo;
    xParabolaEvalVel(parab, &vel_push, parab->maxTime);

    if (xVec3Length2(dir_up) > 0.0f)
    {
        xVec3 vec_tmp = { 0.0f, vel_push.y, 0.0f };
        ReflectOffNormal(&vec_tmp, dir_up, 1.0f);
        vel_push.y = vec_tmp.y;
        vel_push *= 0.1f;
    }

    shrap->initCB(shrap, haz->mdl_hazard, &vel_push, NULL);
}

void FlingBlob::HAZFlingBlob::ReconBlob(zNMEHazard* haz, S32 forceItAnyway)
{
    xVec3 dir_norm = haz->custdata.lobdata.dir_normal;
    en_haztyp ht;

    if (typ_hazard == HAZ_TYP_FLING_V1)
    {
        ht = HAZ_TYP_SPITPUDDLE_V1;
    }
    else if (typ_hazard == HAZ_TYP_FLING_V2)
    {
        ht = HAZ_TYP_SPITPUDDLE_V2;
    }
    else if (typ_hazard == HAZ_TYP_FLING_V3)
    {
        ht = HAZ_TYP_SPITPUDDLE_V3;
    }
    else if (typ_hazard == HAZ_TYP_FLING_FROGFISH)
    {
        ht = HAZ_TYP_SPITPUDDLE_V2;
    }
    else
    {
        ht = HAZ_TYP_UNKNOWN;
        haz->flg_hazard |= 0x4;
    }

    if (!haz->Reconfigure(ht))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (forceItAnyway)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &g_X3);

        // The at axis is still built from dir_norm, not from the forced up axis.
        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, &dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }
    else if (xVec3Length2(&dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, &dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

namespace Puddle
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 0.5f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter spitVapors_DE = {
            NPAR_TYP_FLINGVAPORS_DE,
            1.5f,
            { 0x96, 0xC8, 0xC8, 0x32 },
            { 0.85f, 1.55f },
            { 0.0f, 10.0f, 0.0f },
            7.2f,
            93,
            0,
            3,
            0,
            { 1.25f, -5.0f },
        };
        static const PuffEmitter spitVapors_TR = {
            NPAR_TYP_FLINGVAPORS_TR,
            1.5f,
            { 0x96, 0xC8, 0xC8, 0x32 },
            { 0.85f, 1.55f },
            { 0.0f, 10.0f, 0.0f },
            7.2f,
            93,
            0,
            3,
            0,
            { 1.25f, -5.0f },
        };
        static const PuffEmitter spitVapors_JK = {
            NPAR_TYP_FLINGVAPORS_JK,
            1.5f,
            { 0x96, 0xC8, 0xC8, 0x32 },
            { 0.85f, 1.55f },
            { 0.0f, 10.0f, 0.0f },
            7.2f,
            93,
            0,
            3,
            0,
            { 1.25f, -5.0f },
        };
    } // namespace FX
} // namespace Puddle

void Puddle::HAZPuddle::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_DE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_TR, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_JK, NULL, 0);
}

en_hazrc Puddle::HAZPuddle::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0xd10b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 2.25f;
    haz->custdata.typical.rad_min = 0.1f;
    haz->custdata.typical.rad_max = 1.25f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

    UVAModelInfo* uva = HAZ_UVAnimInfo(haz);
    if (uva)
    {
        uva->UVVelSet(0.3f, 0.0f);
    }

    return HAZ_RC_SUCCESS;
}

void Puddle::HAZPuddle::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typdat = &haz->custdata.typical;

    if (HAZ_NeptuneCineActive())
    {
        haz->flg_hazard |= 0x4;
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }
        return;
    }

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    // Landed on a slope too steep to stick to: dry up quickly.
    if ((haz->flg_hazard & 8) && xVec3Dot((xVec3*)&haz->mdl_hazard->Mat->up, &g_Y3) < 0.86f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard &= ~0x100;
        haz->LifespanSet(0.87f, 0.87f);
    }

    UpdRadius(haz);

    if (haz->ColPlyrSphere(typdat->rad_cur) && (haz->flg_hazard & 0x2000))
    {
        NMEC_PlayerSlip();
        haz->HurtThePlayer();
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.5f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f, 1);
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        if (haz->pam_interp < 0.9f)
        {
            if (haz->typ_hazard == HAZ_TYP_SPITPUDDLE_V1)
            {
                FX::spitVapors_DE.ImpactRing(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at);
            }
            else if (haz->typ_hazard == HAZ_TYP_SPITPUDDLE_V2)
            {
                FX::spitVapors_TR.ImpactRing(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at);
            }
            else if (haz->typ_hazard == HAZ_TYP_SPITPUDDLE_V3)
            {
                FX::spitVapors_JK.ImpactRing(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at);
            }
        }
    }
}

void Puddle::HAZPuddle::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 alfa = 1.0f;
    F32 pct;
    F32 rad;

    if (haz->flg_hazard & 0x3000000)
    {
        if (haz->typ_hazard == HAZ_TYP_SPITPUDDLE_V1)
        {
            alfa = 0.025f;
        }
        else if (haz->typ_hazard == HAZ_TYP_SPITPUDDLE_V2)
        {
            alfa = 0.115f;
        }
        else if (haz->typ_hazard == HAZ_TYP_SPITPUDDLE_V3)
        {
            alfa = 0.025f;
        }
    }

    if (haz->pam_interp < 0.9f)
    {
        alfa *= 0.12f;
    }
    else
    {
        pct = (haz->pam_interp - 0.9f) / (1.0f - 0.9f);
        alfa *= LERP(CLAMP(pct, 0.0f, 1.0f), 0.12f, 0.0f);
    }

    haz->SetAlpha(alfa);

    rad = 4.0f * typical->rad_cur;
    haz->mdl_hazard->Scale = Puddle::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

// Grows to rad_max, eases to the middle size while it lies there and shrinks away at the end.
F32 Puddle::HAZPuddle::UpdRadius(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 pam = haz->pam_interp;
    F32 rad_mid = LERP(0.75f, typical->rad_min, typical->rad_max);
    F32 pct;

    if (pam <= 0.1f)
    {
        typical->rad_cur = SMOOTH(pam / 0.1f, typical->rad_min, typical->rad_max);
    }
    else if (pam >= 0.9f)
    {
        pct = (pam - 0.9f) / (1.0f - 0.9f);
        pct = CLAMP(pct, 0.0f, 1.0f);
        typical->rad_cur = SMOOTH(pct, rad_mid, typical->rad_min);
    }
    else
    {
        pct = (pam - 0.1f) / (0.9f - 0.1f);
        pct = CLAMP(pct, 0.0f, 1.0f);
        typical->rad_cur = SMOOTH(pct, typical->rad_max, rad_mid);
    }

    return typical->rad_cur;
}

namespace FrogFishBlob
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter spitTrail = {
            NPAR_TYP_FLINGVAPORS_TR,
            1.0f,
            { 0x96, 0xC8, 0xC8, 0xC8 },
            { 0.25f, 0.65f },
            { 0.0f, -10.0f, 0.0f },
            7.2f,
            93,
            0,
            5,
            0,
            { 0.2f, 5.0f },
        };
    } // namespace FX
} // namespace FrogFishBlob

void FrogFishBlob::HAZFrogFishBlob::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc FrogFishBlob::HAZFrogFishBlob::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xd61b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;

    hazlob->flg_collide |= 7;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xFD;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.35f;
    hazlob->rad_max = 0.35f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

void FrogFishBlob::HAZFrogFishBlob::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 tym;

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 0x10000000)
    {
        EndOfLifespan(haz);
        return;
    }

    HAZ_LobLaunch(haz, 5.0f);

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrient(haz);

    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            hazlob->rad_cur, &hazlob->vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            NMEC_PlayerSlip();
            haz->HurtThePlayer();
        }

        SplatPhlegm(globals.player.model_patrick);

        haz->flg_hazard |= 0x4;
    }
    else
    {
        haz->StaggeredCollide();
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        FX::spitTrail.Footstroke(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at);
    }
}

void FrogFishBlob::HAZFrogFishBlob::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(0.35f);

    rad = 7.0f * hazlob->rad_cur;
    haz->mdl_hazard->Scale = FrogFishBlob::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

// ShrapSplatter ignores its direction, so the normalized whence is dead in retail too (the PS2
// DWARF does not list it).
void FrogFishBlob::HAZFrogFishBlob::EndOfLifespan(zNMEHazard* haz)
{
    xParabola* parab = haz->custdata.lobdata.parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        ShrapSplatter(haz, &haz->custdata.lobdata.dir_normal);

        if (!(haz->flg_hazard & 0x40000))
        {
            ReconBlob(haz);
        }
        else
        {
            haz->flg_hazard |= 0x4;
        }
    }
    else
    {
        xVec3 whence;

        xParabolaEvalVel(parab, &whence, parab->maxTime);
        whence.invert();

        F32 len = xVec3Length(&whence);
        if (len > 0.00001f)
        {
            whence /= len;
            ShrapSplatter(haz, &whence);
        }
        else
        {
            ShrapSplatter(haz, (xVec3*)&haz->mdl_hazard->Mat->up);
        }

        haz->flg_hazard |= 0x4;
    }
}

// The asserts are compiled out (conditions unknown); 10 of them push its inline cost past GC/2.6's
// limit (512), as retail calls it. The PS2 demo also keeps this one out of line.
void FrogFishBlob::HAZFrogFishBlob::ReconBlob(zNMEHazard* haz)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xVec3 dir_norm = haz->custdata.lobdata.dir_normal;
    en_haztyp ht = (typ_hazard == HAZ_TYP_FLING_FROGFISH) ? HAZ_TYP_SPITPUDDLE_V2 : HAZ_TYP_UNKNOWN;

    if (!haz->Reconfigure(ht))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (xVec3Length2(&dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, &dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

namespace IceCreamSplat
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 0.5f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter iceCreamParticles = {
            NPAR_TYP_DUSTPUFF,
            1.0f,
            { 0x91, 0x80, 0xAC, 0x64 },
            { 0.85f, 1.55f },
            { 0.0f, 10.0f, 0.0f },
            7.2f,
            93,
            0,
            5,
            0,
            { 1.0f, -5.0f },
        };
    } // namespace FX
} // namespace IceCreamSplat

void IceCreamSplat::HAZIceCreamSplat::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc IceCreamSplat::HAZIceCreamSplat::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 2.25f;
    haz->custdata.typical.rad_min = 0.1f;
    haz->custdata.typical.rad_max = 0.4f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

    return HAZ_RC_SUCCESS;
}

void IceCreamSplat::HAZIceCreamSplat::Update(zNMEHazard* haz, F32 dt)
{
    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    UpdRadius(haz);

    haz->ColPlyrSphere(haz->custdata.typical.rad_cur);

    if (haz->flg_casthurt == 0 && haz->npc_owner != NULL && haz->pam_interp > 0.5f)
    {
        haz->flg_casthurt = 1;
        haz->flg_hazard |= 0x40;
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        if (haz->pam_interp < 0.15f)
        {
            FX::iceCreamParticles.ImpactRing(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at);
        }
    }
}

void IceCreamSplat::HAZIceCreamSplat::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 alfa;
    F32 pct;
    F32 rad;

    if (haz->pam_interp < 0.9f)
    {
        alfa = 1.0f;
    }
    else
    {
        pct = (haz->pam_interp - 0.9f) / (1.0f - 0.9f);
        alfa = LERP(CLAMP(pct, 0.0f, 1.0f), 1.0f, 0.0f);
    }

    haz->SetAlpha(alfa);

    rad = 4.0f * typical->rad_cur;
    haz->mdl_hazard->Scale = IceCreamSplat::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace PopRock
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 30.0f, 30.0f, 30.0f };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 0.5f, 1.0f, 0.5f };
    } // namespace Render

    namespace FX
    {
        static const PopGasEmitter gasTrail = {
            NPAR_TYP_POPGAS,
            0.55f,
            { 0x00, 0xE1, 0x64, 0x3C },
            { 0.4f, 1.0f },
            { 0.0f, 0.0f, 0.0f },
            0.7853982f,
            99,
            0,
            2,
            0,
            { 0.0f, -1.0f, 0.0f, 0.0f, 0.0f },
        };
    } // namespace FX
} // namespace PopRock

void PopRock::HAZPopRock::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_POPGAS, NULL, 0);
}

en_hazrc PopRock::HAZPopRock::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xe61b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;

    hazlob->flg_collide |= 7;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xFD;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.3f;
    hazlob->rad_max = 0.3f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

// The PS2 line table puts the launch velocity and the orientation on one line each (inlined calls);
// retail lays the launch temporary out one inline level deeper, as HAZ_LobLaunchRemain does.
void PopRock::HAZPopRock::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 tym;

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        EndOfLifespan(haz);
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchRemain(haz, 0.0f);
    }

    if (haz->flg_hazard & 8)
    {
        haz->PreCollide(0.0f, 0.0f);
    }

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrientTumble(haz, 4.0f);

    RockCollision(haz, dt);

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        FX::gasTrail.PopRockTrail(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at);
    }
}

void PopRock::HAZPopRock::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(0.67f);

    rad = 2.0f * hazlob->rad_cur;
    haz->mdl_hazard->Scale = PopRock::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

void PopRock::HAZPopRock::RockCollision(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;

    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            hazlob->rad_cur, &hazlob->vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            haz->HurtThePlayer();
        }

        // Not in the PS2 DWARF (no RockCollision there); the gas puffs out along the rock's
        // velocity at the end of its flight.
        xParabola* parab = hazlob->parabinfo;
        xVec3 vel_end;

        xParabolaEvalVel(parab, &vel_end, parab->maxTime);
        RockShatter(haz, &vel_end);

        haz->flg_hazard |= 0x4;
    }
    else
    {
        haz->StaggeredCollide();
    }
}

void PopRock::HAZPopRock::EndOfLifespan(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        RockShatter(haz, &hazlob->dir_normal);
        haz->flg_hazard |= 0x4;
    }
    else
    {
        xVec3 whence;

        xParabolaEvalVel(parab, &whence, parab->maxTime);
        whence.invert();

        F32 len = xVec3Length(&whence);
        if (len > 0.00001f)
        {
            whence /= len;
            RockShatter(haz, &whence);
        }
        else
        {
            RockShatter(haz, (xVec3*)&haz->mdl_hazard->Mat->up);
        }

        haz->flg_hazard |= 0x4;
    }
}

// No GC symbol (inlined into EndOfLifespan and RockCollision; the PS2 build has it out of line).
// Spawns an impact at the rock and puffs gas out of it in every direction.
inline void PopRock::HAZPopRock::RockShatter(zNMEHazard* haz, const xVec3* dir_emit)
{
    HAZ_SpawnAt(HAZ_ModelPos(haz), HAZ_TYP_IMPACT, haz->npc_owner, -1.0f);

    xVec3 vol_region = { 0.75f, 0.5f, 0.75f };
    xVec3 pos_home = haz->pos_hazard;

    for (S32 i = 0; i < 5; i++)
    {
        xVec3 pos_emit = vol_region;
        pos_emit.x *= 2.0f * (xurand() - 0.5f);
        pos_emit.y *= 2.0f * (xurand() - 0.5f);
        pos_emit.z *= 2.0f * (xurand() - 0.5f);
        pos_emit += pos_home;

        FX::gasTrail.PopRockTrail(&pos_emit, dir_emit);
        FX::gasTrail.PopRockTrail(&pos_emit, &g_X3);
        FX::gasTrail.PopRockTrail(&pos_emit, &g_NX3);
        FX::gasTrail.PopRockTrail(&pos_emit, &g_Y3);
        FX::gasTrail.PopRockTrail(&pos_emit, &g_NY3);
        FX::gasTrail.PopRockTrail(&pos_emit, &g_Z3);
        FX::gasTrail.PopRockTrail(&pos_emit, &g_NZ3);
    }
}

namespace MervBomb
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const PuffEmitter rocketTrail = {
            NPAR_TYP_DUSTPUFF,
            0.75f,
            { 0xC8, 0xC8, 0xC8, 0xC8 },
            { 0.55f, 1.1f },
            { 0.0f, 5.0f, 0.0f },
            37.2f,
            98,
            0,
            1,
            0,
            { 1.0f, -5.0f },
        };
    } // namespace FX
} // namespace MervBomb

HAZShadowProps MervBomb::Shadow::shadPropData = { 0.4f, 35.0f, 0.35f, 15.0f, -1, 0.3f };

namespace MervBomb
{
    // Unreferenced in the retail build (its strings are still pooled).
    static char* strz_tgtmode[2] = { "orig", "norm" };
} // namespace MervBomb

void MervBomb::HAZMervBomb::ScenePrepare()
{
    HAZBasic::ScenePrepare();
    shadProps = &Shadow::shadPropData;
}

void MervBomb::HAZMervBomb::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc MervBomb::HAZMervBomb::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xc61b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;
    hazlob->flg_collide |= 7;

    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 0.35f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

// keepFlightTime is const, as in SpawnBall::HAZSpawnBall::Update (retail's separate tmr_remain
// load). The launch goes through HAZ_LobLaunchRemain, which clamps the flight time once.
void MervBomb::HAZMervBomb::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    S32 kickit;

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        EndOfLifespan(haz);
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    hazlob->rad_cur = hazlob->rad_max;

    kickit = TimeToBurst(haz);

    if (kickit)
    {
        AirburstBoomFX(haz);

        if (haz->typ_hazard == HAZ_TYP_MERVBOMB_V1)
        {
            if (KickBomblets(haz))
            {
                haz->flg_hazard |= 0x40;
            }
        }
        else if (haz->typ_hazard == HAZ_TYP_MERVBOMB_V2)
        {
            if (KickBomblets(haz))
            {
                haz->flg_hazard |= 0x40;
            }
        }
        else
        {
            if (KickBomblets(haz))
            {
                haz->flg_hazard |= 0x40;
            }
        }

        haz->flg_hazard |= 4;
        return;
    }

    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchRemain(haz, 2.2f);
    }

    if (haz->flg_hazard & 8)
    {
        const F32 keepFlightTime = haz->tmr_remain;
        haz->PreCollide(2.2f, 0.0f);
        haz->tmr_remain = MIN(haz->tmr_remain, keepFlightTime);
        haz->tym_lifespan = haz->tmr_remain;
    }

    F32 tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrientSpiral(haz, 4.0f);
    MervBomCollision(haz, dt);

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        DisperseBubWake(haz, hazlob->rad_cur, &hazlob->vel, 4);
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        xVec3 vel = hazlob->vel * 0.25f;
        FX::rocketTrail.Footstroke(&haz->pos_hazard, &vel);
    }
}

void MervBomb::HAZMervBomb::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    rad = 2.0f * hazlob->rad_cur;
    haz->mdl_hazard->Scale = MervBomb::Render::vec_scale * rad;

    Stuff_Render(haz, 2.5f * hazlob->rad_cur);
}

void MervBomb::HAZMervBomb::MervBomCollision(zNMEHazard* haz, F32 dt)
{
    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            haz->custdata.lobdata.rad_cur, &haz->custdata.lobdata.vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            if (haz->npc_owner == NULL)
            {
                zEntPlayer_Damage(NULL, 1);
            }
            else
            {
                zEntPlayer_DamageNPCKnockBack(haz->npc_owner, 1, &haz->pos_hazard,
                                              ePlayerNMEDamageEnemyProjectile);
            }
        }
        ReconAsBoom(haz);
    }
    else
    {
        haz->StaggeredCollide();
    }
}

void MervBomb::HAZMervBomb::AirburstBoomFX(zNMEHazard* haz)
{
    F32 rad;
    xVec3 vel;

    rad = MAX(0.05f, 1.4f * haz->custdata.lobdata.rad_cur);
    NMEC_BurstBubble(NME_BURST_BALL, &haz->pos_hazard, rad, 0);

    vel = *(xVec3*)&haz->mdl_hazard->Mat->at;
    vel.invert();
    DisperseBubWake(haz, rad, &vel, 20);
}

// The asserts are compiled out (conditions unknown); 14 of them push its inline cost past GC/2.6's
// limit (512), as retail calls it.
void MervBomb::HAZMervBomb::ReconAsBoom(zNMEHazard* haz)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xVec3 dir_norm = haz->custdata.lobdata.dir_normal;

    if (!haz->Reconfigure(HAZ_TYP_EXPLODE))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (xVec3Length2(&dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, &dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

namespace MervBomb
{
    // No GC or PS2 symbol (inlined twice into KickBomblets); the name is ours. Flight time of a
    // Mervlet lobbed from pos_src to pos_tgt: the horizontal distance at 5 units per second, at
    // least 0.15 seconds.
    static inline F32 FlightTime(const xVec3* pos_tgt, const xVec3* pos_src)
    {
        xVec3 vec = *pos_tgt - *pos_src;
        vec.y = 0.0f;
        F32 dst = vec.length();
        if (dst < 0.00001f)
        {
            return 0.15f;
        }
        return MAX(0.15f, dst / 5.0f);
    }
} // namespace MervBomb

// Dead-stripped placeholder: retail's .rodata keeps an unreferenced all-zero 0xC template here,
// next to KickBomblets' data. Its owner and type are unknown.
static void __deadstripped_zNMEHazardTypes()
{
    xVec3 unknown = {};
}

// Splits the bomb into five Mervlets lobbed at the player: in a jittered ring around the player
// when the player stands (almost) still, else along the player's predicted path. Then an impact
// boom at the bomb. Returns the number of Mervlets launched.
S32 MervBomb::HAZMervBomb::KickBomblets(const zNMEHazard* parent)
{
    S32 counted = 0;
    en_haztyp ht_child = HAZ_TYP_MERVLET_V1;

    if (typ_hazard == HAZ_TYP_MERVBOMB_V2)
    {
        ht_child = HAZ_TYP_MERVLET_V2;
    }
    else if (typ_hazard == HAZ_TYP_MERVBOMB_V3)
    {
        ht_child = HAZ_TYP_MERVLET_V3;
    }

    S32 surround = 0;
    xVec3 pos_a;
    xVec3 pos_b;

    zEntPlayer_PredictPos(&pos_a, 0.0f, 1.0f, 1);
    zEntPlayer_PredictPos(&pos_b, 1.0f, 1.0f, 1);

    xVec3 delta = pos_b - pos_a;
    if (delta.length2() < 4.0f)
    {
        surround = 1;
    }

    F32 ang_curr = 0.0f;
    for (S32 i = 0; i < 5; i++)
    {
        zNMEHazard* haz = HAZ_Acquire();
        if (haz == NULL)
        {
            continue;
        }

        if (!haz->ConfigHelper(ht_child))
        {
            haz->flg_hazard |= 0x4;
            continue;
        }

        haz->npc_owner = parent->npc_owner;
        if (i > 0)
        {
            haz->flg_custom |= 0x1;
        }

        ang_curr += (2.0f * PI / 5.0f) * (0.25f * (xurand() - 0.5f)) + (2.0f * PI / 5.0f);

        F32 tym_flight;
        if (surround)
        {
            // not in the DWARF: both builds call icos first and keep its result across isin.
            F32 cos_ang = icos(ang_curr);
            F32 sin_ang = isin(ang_curr);
            xVec3 delta = { sin_ang, 0.0f, cos_ang };
            delta *= 2.5f * xurand();
            delta.y = -0.0f; // both builds store a negative zero here

            xVec3 pos_tgt =
                *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos + delta;
            haz->custdata.lobdata.pos_tgt = pos_tgt;

            tym_flight = FlightTime(&pos_tgt, &parent->pos_hazard);
        }
        else
        {
            xVec3 pos_tgt;
            zEntPlayer_PredictPos(&pos_tgt, 0.6f * i, 1.0f, 1);

            tym_flight = FlightTime(&pos_tgt, &parent->pos_hazard);
            haz->custdata.lobdata.pos_tgt = pos_tgt;
        }

        haz->Start(&parent->pos_hazard, tym_flight);
        counted++;
    }

    HAZ_SpawnAt(HAZ_ModelPos(parent), HAZ_TYP_IMPACTBOOM, parent->npc_owner, -1.0f);

    return counted;
}

namespace Mervlet
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        namespace Smoky
        {
            static const PuffEmitter rocketTrail = {
                NPAR_TYP_DUSTHEAT,
                1.25f,
                { 0x96, 0x50, 0xA0, 0x64 },
                { 0.1f, 0.25f },
                { 0.0f, 1.0f, 0.0f },
                57.2f,
                99,
                0,
                5,
                0,
                { 1.0f, 1.0f },
            };
        } // namespace Smoky
    } // namespace FX
} // namespace Mervlet

HAZShadowProps Mervlet::Shadow::shadPropData = { 0.4f, 20.0f, 0.35f, 15.0f, -1, 0.3f };

void Mervlet::HAZMervlet::ScenePrepare()
{
    HAZBasic::ScenePrepare();
    shadProps = &Shadow::shadPropData;
}

void Mervlet::HAZMervlet::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTHEAT, NULL, 0);
}

en_hazrc Mervlet::HAZMervlet::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xc61b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;
    hazlob->flg_collide |= 7;

    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    haz->custdata.typical.rad_min = 0.25f;
    haz->custdata.typical.rad_max = 0.25f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

void Mervlet::HAZMervlet::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 tym;

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        EndOfLifespan(haz);
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 8)
    {
        hazlob->vel = hazlob->pos_tgt - haz->pos_hazard;
        hazlob->vel /= haz->tmr_remain;
        hazlob->vel.y += 3.0f * (0.5f * haz->tmr_remain);
    }

    if (haz->flg_hazard & 8)
    {
        haz->PreCollide(3.0f, 0.0f);
    }

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    haz->OrientToDir(&hazlob->vel, 0);
    MervletCollision(haz, dt);

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        DisperseBubWake(haz, hazlob->rad_cur, &hazlob->vel, 1);
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        xVec3 vel = hazlob->vel * 0.25f;
        FX::Smoky::rocketTrail.Footstroke(&haz->pos_hazard, &vel);
    }
}

void Mervlet::HAZMervlet::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    rad = hazlob->rad_cur;
    haz->mdl_hazard->Scale = Mervlet::Render::vec_scale * rad;

    Stuff_Render(haz, 2.5f * hazlob->rad_cur);
}

// The normalized dir is unused in retail too; it survives because xVec3Copy is out of line.
void Mervlet::HAZMervlet::EndOfLifespan(zNMEHazard* haz)
{
    xParabola* parab = haz->custdata.lobdata.parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        if (!(haz->flg_hazard & 0x40000))
        {
            xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        }

        ReconNukem(haz);
    }
    else
    {
        xVec3 dir;

        xParabolaEvalVel(parab, &dir, parab->maxTime);
        dir.invert();

        F32 len = xVec3Length(&dir);
        if (len > 0.00001f)
        {
            dir /= len;
        }

        ReconNukem(haz);
    }
}

void Mervlet::HAZMervlet::MervletCollision(zNMEHazard* haz, F32 dt)
{
    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            haz->custdata.lobdata.rad_cur, &haz->custdata.lobdata.vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            if (haz->npc_owner == NULL)
            {
                zEntPlayer_Damage(NULL, 1);
            }
            else
            {
                zEntPlayer_DamageNPCKnockBack(haz->npc_owner, 1, &haz->pos_hazard,
                                              ePlayerNMEDamageEnemyProjectile);
            }
        }
        ReconNukem(haz);
    }
    else
    {
        haz->StaggeredCollide();
    }
}

// The asserts are compiled out (conditions unknown); 14 of them push its inline cost past GC/2.6's
// limit (512), as retail calls it.
void Mervlet::HAZMervlet::ReconNukem(zNMEHazard* haz)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xVec3 dir_norm = haz->custdata.lobdata.dir_normal;

    if (!haz->Reconfigure(HAZ_TYP_EXPLODE))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (xVec3Length2(&dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, &dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

namespace ZapBall
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 0.0f, 30.0f, 0.0f };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const PlasmaEmitter hotTrail = {
            NPAR_TYP_ENERGY,
            0.55f,
            { 0xFF, 0xE1, 0xFF, 0xFF },
            { 0.4f, 1.0f },
            { 0.0f, 0.0f, 0.0f },
            0.7853982f,
            99,
            0,
            2,
            0,
            { 0.0f, -1.0f, 0.0f, 0.0f, 0.0f },
        };

        namespace Ribbon
        {
            static const xFXRibbon::curve_node curvedata[5] = {
                { 0.0f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.0f },
                { 0.1f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.5f },
                { 0.35f, { 0x00, 0xFF, 0xFF, 0x7F }, 1.5f },
                { 0.9f, { 0x32, 0xC8, 0xFF, 0x40 }, 0.25f },
                { 1.0f, { 0x64, 0x64, 0x9B, 0x00 }, 0.0f },
            };

            NMEHelpRib::RibData rib_laser = {
                { 1, 2, 0, 0 },
                { "fx_solid", (xFXRibbon::curve_node*)curvedata, 5, 0.25f, 0.5f },
                { -1, 1.0f, 2.0f },
                { NULL, 0 },
            };
        } // namespace Ribbon
    } // namespace FX
} // namespace ZapBall

void ZapBall::HAZZapBall::ScenePostInit()
{
    Stuff_PostInit();
    FX::Ribbon::rib_laser.FindTexture();
    zNMEParFX_PartyInvite(NPAR_TYP_ENERGY, NULL, 0);
}

// The zap ball trails two laser ribbons; it fails to launch without both.
en_hazrc ZapBall::HAZZapBall::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0xc21b280;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;
    hazlob->flg_collide |= 7;

    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    if (FX::Ribbon::rib_laser.flags.enable)
    {
        haz->fxdata.ribdata.riblist[0] = zFXRibbonPoolNew();
        haz->fxdata.ribdata.riblist[1] = zFXRibbonPoolNew();
    }

    if (haz->fxdata.ribdata.riblist[0] != NULL && haz->fxdata.ribdata.riblist[1] != NULL)
    {
        FX::Ribbon::rib_laser.ConfigRibbon(haz->fxdata.ribdata.riblist[0]);
        FX::Ribbon::rib_laser.ConfigRibbon(haz->fxdata.ribdata.riblist[1]);
    }
    else
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.25f;
    hazlob->rad_max = 0.25f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

void ZapBall::HAZZapBall::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 keepFlightTime;
    F32 tym;

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 0x10000000)
    {
        RibbonFinal(haz);

        EndOfLifespan(haz);
    }
    else
    {
        if (haz->flg_hazard & 8)
        {
            HAZ_LobLaunchRemain(haz, 0.0f);
        }

        if (haz->flg_hazard & 8)
        {
            keepFlightTime = haz->tmr_remain + 1.5f;
            haz->PreCollide(0.0f, 0.0f);
            haz->tmr_remain = MIN(haz->tmr_remain, keepFlightTime);
            haz->tym_lifespan = haz->tmr_remain;
        }

        tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
        xParabolaEvalPos(parab, &haz->pos_hazard, tym);
        xParabolaEvalVel(parab, &hazlob->vel, tym);

        LobOrientSpiral(haz, 7.0f);
        ZapBallCollision(haz, dt);

        if (haz->fxdata.ribdata.ribbon != NULL)
        {
            RibbonUpdate(haz, &haz->fxdata.ribdata, dt);
        }
    }
}

void ZapBall::HAZZapBall::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    // Retail scales the model without checking it for NULL.
    rad = hazlob->rad_cur;
    haz->mdl_hazard->Scale = ZapBall::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

// The PS2 DWARF lists no length local (only hazlob, parab, whence).
void ZapBall::HAZZapBall::ZapBallCollision(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab;
    xVec3 whence;

    if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                            hazlob->rad_cur, &hazlob->vel, dt, 1))
    {
        if (haz->flg_hazard & 0x2000)
        {
            if (haz->npc_owner == NULL)
            {
                zEntPlayer_Damage(NULL, 1);
            }
            else
            {
                zEntPlayer_DamageNPCKnockBack(haz->npc_owner, 1, &haz->pos_hazard,
                                              ePlayerNMEDamageEnemyProjectile);
            }
        }

        // Head back the way the ball came.
        parab = hazlob->parabinfo;
        xParabolaEvalVel(parab, &whence, parab->maxTime);
        whence *= -1.0f;

        F32 len = whence.length();
        if (len < 0.1f)
        {
            whence = g_Y3;
        }
        else
        {
            whence /= len;
        }

        ReconToZapBoom(haz, &whence);
    }
    else
    {
        haz->StaggeredCollide();
    }
}

void ZapBall::HAZZapBall::EndOfLifespan(zNMEHazard* haz)
{
    xParabola* parab = haz->custdata.lobdata.parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        ReconToZapBoom(haz, &haz->custdata.lobdata.dir_normal);
    }
    else
    {
        xVec3 dir;

        xParabolaEvalVel(parab, &dir, parab->maxTime);
        dir.invert();

        // xVec3Length rather than dir.length() matches retail's register use (as in GolfBall).
        F32 len = xVec3Length(&dir);
        if (len > 0.00001f)
        {
            dir /= len;
        }
        else
        {
            dir = *(xVec3*)&haz->mdl_hazard->Mat->up;
        }

        ReconToZapBoom(haz, &dir);
    }
}

// No GC or PS2 symbol; the name is ours. The PS2 build puts this whole block on one line in both
// callers (Update, ReconToZapBoom) and both pass the fixed frame time instead of Update's dt, so
// it was an inline helper.
inline void ZapBall::HAZZapBall::RibbonFinal(zNMEHazard* haz)
{
    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        // Force a ribbon joint right now.
        haz->fxdata.ribdata.cnt_skipRibInsert = -1;
        RibbonUpdate(haz, &haz->fxdata.ribdata,
                     gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
    }
}

void ZapBall::HAZZapBall::ReconToZapBoom(zNMEHazard* haz, const xVec3* dir_norm)
{
    en_haztyp htyp;

    RibbonFinal(haz);

    if (haz->typ_hazard == HAZ_TYP_ZAPBALL_V1)
    {
        htyp = HAZ_TYP_ZAPBOOM_V1;
    }
    else if (haz->typ_hazard == HAZ_TYP_ZAPBALL_V2)
    {
        htyp = HAZ_TYP_ZAPBOOM_V2;
    }
    else
    {
        htyp = HAZ_TYP_ZAPBOOM_V1;
    }

    if (!haz->Reconfigure(htyp))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (xVec3Length2(dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

// No GC symbol: inlined into both callers (Update and RibbonFinal).
inline void ZapBall::HAZZapBall::RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32)
{
    F32 scale;
    F32 alpha;
    xVec3 pos_emit;

    ribdata->cnt_skipRibInsert--;
    if (ribdata->cnt_skipRibInsert > 0)
    {
        return;
    }

    ribdata->cnt_skipRibInsert = FX::Ribbon::rib_laser.updinfo.frq_ribJoint;

    scale = haz->custdata.lobdata.rad_cur * FX::Ribbon::rib_laser.updinfo.scl_master;
    alpha = FX::Ribbon::rib_laser.updinfo.alf_master;

    // Emit from the front of the ball.
    pos_emit = haz->pos_hazard;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * haz->custdata.lobdata.rad_cur;

    ribdata->riblist[0]->insert(pos_emit, *(xVec3*)&haz->mdl_hazard->Mat->right, scale, alpha, 0);
    ribdata->riblist[1]->insert(pos_emit, *(xVec3*)&haz->mdl_hazard->Mat->up, scale, alpha, 0);
}

namespace ZapBoom
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 30.0f, 30.0f, 30.0f };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace ZapBoom

void ZapBoom::HAZZapBoom::ScenePostInit()
{
    Stuff_PostInit();
}

// No GC or PS2 symbol, and the PS2 DWARF of Configure lists none of its locals (add info, loop
// counter, result), so it was an inline helper; the name is ours.
static inline S32 ZapBoom_LightningStart(zNMEHazard* haz)
{
    _tagLightningAdd info;

    memset(&info, 0, sizeof(_tagLightningAdd));
    NMEC_MakeLightningInfo(NME_LYT_ZAPBOOM, &info);
    info.start = &haz->pos_hazard;
    info.time = 15.0f;
    info.end = &haz->pos_hazard;

    for (S32 i = 0; i < 4; i++)
    {
        haz->fxdata.lytdata.lytlist[i] = zLightningAdd(&info);
        if (haz->fxdata.lytdata.lytlist[i] == NULL)
        {
            return 0;
        }
    }

    return 1;
}

en_hazrc ZapBoom::HAZZapBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x921b280;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.75f;
    haz->custdata.typical.rad_min = 0.25f;
    haz->custdata.typical.rad_max = 0.5f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    rc = ZapBoom_LightningStart(haz);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    xVec3 spin = Initialize::vec_rotate;
    spin.x *= 2.0f * (xurand() - 0.5f);
    spin.y *= 2.0f * (xurand() - 0.5f);
    spin.z *= 2.0f * (xurand() - 0.5f);
    spin *= (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
    haz->TypData_RotMatStore(&spin);

    return HAZ_RC_SUCCESS;
}

// No GC or PS2 symbol, and the PS2 DWARF of Update lists none of its locals (position, offset,
// loop counter), so it was an inline helper; the name is ours. Jitters the four bolts around
// the hazard within its current radius.
static inline void ZapBoom_LightningJitter(zNMEHazard* haz)
{
    HAZFXLightning* lytdata = &haz->fxdata.lytdata;
    xVec3 pos = haz->pos_hazard;
    F32 rad = haz->custdata.typical.rad_cur;

    for (S32 i = 0; i < 4; i++)
    {
        xVec3 off;
        off.x = rad * (2.0f * (xurand() - 0.5f));
        off.y = rad * (2.0f * (xurand() - 0.5f));
        off.z = rad * (2.0f * (xurand() - 0.5f));

        xVec3 pos_start = pos + off;
        xVec3 pos_end = pos - off;
        zLightningModifyEndpoints(lytdata->lytlist[i], &pos_start, &pos_end);
    }
}

void ZapBoom::HAZZapBoom::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typdat = &haz->custdata.typical;

    // not in the DWARF: retail keeps the ARCH3 value as an anonymous temporary, which a LERP of
    // ARCH3 written inline does not give.
    F32 rat = ARCH3(haz->pam_interp);
    typdat->rad_cur = LERP(rat, typdat->rad_min, typdat->rad_max);

    if (haz->flg_hazard & 0x10000000)
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (haz->flg_hazard & 8)
    {
        ZapBoom_LightningJitter(haz);
    }

    if (!haz->cnt_generic && haz->npc_owner && haz->pam_interp > 0.7f)
    {
        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f, 1);
        }
    }

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 3.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        ZapBoom_LightningJitter(haz);
    }
}

void ZapBoom::HAZZapBoom::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    rad = hazlob->rad_cur;
    haz->mdl_hazard->Scale = ZapBoom::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace GolfBall
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 30.0f, 30.0f, 30.0f };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        static const xVec3 vec_emitOffset = { 0.0f, 0.25f, 0.25f };
        static const PopGasEmitter gasTrail = {
            NPAR_TYP_POPGAS,
            2.0f,
            { 0x1E, 0xC8, 0x64, 0x96 },
            { 1.0f, 1.75f },
            { 0.0f, 0.0f, 0.0f },
            0.7853982f,
            99,
            0,
            1,
            0,
            { 0.0f, 2.0f },
        };
    } // namespace FX
} // namespace GolfBall

HAZShadowProps GolfBall::Shadow::shadPropData = { 0.4f, 20.0f, 0.35f, 15.0f, -1, 0.3f };

void GolfBall::HAZGolfBall::ScenePrepare()
{
    HAZBasic::ScenePrepare();
    shadProps = &Shadow::shadPropData;
}

void GolfBall::HAZGolfBall::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_POPGAS, NULL, 0);
}

en_hazrc GolfBall::HAZGolfBall::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0x161b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    haz->tmr_remain = 0.25f;
    haz->custdata.typical.rad_min = 0.5f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

// The launch goes through HAZ_LobLaunchRemain: retail lays out the launch velocity's temporary
// below LobOrientTumble's dir, i.e. one inline level deeper (as in MervBomb's Update).
void GolfBall::HAZGolfBall::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    zNMEDennis* dennis;
    S32 rc;
    F32 tym;

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        EndOfLifespan(haz);
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    dennis = (zNMEDennis*)haz->npc_owner;
    if (dennis && dennis->SelfType() == NME_TYPE_DENNIS_V1)
    {
        en_denstage okStages[4] = { DEN_STAGE_A, DEN_STAGE_B, DEN_STAGE_C, DEN_STAGE_UNKNOWN };
        rc = HAZ_DenStageInList(dennis->stageCurr, okStages);
        if (!rc)
        {
            haz->flg_hazard |= 0x4;
            return;
        }
    }

    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchRemain(haz, 7.5f);
    }

    if (haz->flg_hazard & 8)
    {
        haz->PreCollide(7.5f, 0.5f);
    }

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    LobOrientTumble(haz, -1.0f);

    // The PS2 demo calls a GolfCollision(haz, dt) wrapper here; retail has no such symbol and calls
    // StaggeredCollide directly (GC/2.6 could not have inlined the wrapper).
    haz->StaggeredCollide();

    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        xVec3 pos_emit = haz->pos_hazard;
        pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * FX::vec_emitOffset.x;
        pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * FX::vec_emitOffset.y;
        pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * FX::vec_emitOffset.z;
        FX::gasTrail.PopRockTrail(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->up);
    }
}

void GolfBall::HAZGolfBall::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(0.27f);

    rad = hazlob->rad_cur;
    haz->mdl_hazard->Scale = GolfBall::Render::vec_scale * rad;

    Stuff_Render(haz, 2.5f * hazlob->rad_cur);
}

void GolfBall::HAZGolfBall::EndOfLifespan(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;

    if (haz->flg_hazard & 0x20000)
    {
        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        ShrapSplatter(haz, &g_Y3, 1.0f);
        ReconToGolfBoom(haz, &hazlob->dir_normal);
    }
    else
    {
        xVec3 whence;

        xParabolaEvalVel(parab, &whence, parab->maxTime);
        whence.invert();

        // not in the DWARF (as in the other lobbed hazards' EndOfLifespan); xVec3Length rather
        // than whence.length() matches retail's register use.
        F32 len = xVec3Length(&whence);
        if (len > 0.00001f)
        {
            whence /= len;
        }
        else
        {
            whence = g_Y3;
        }

        ShrapSplatter(haz, &whence, 1.0f);
        ReconToGolfBoom(haz, &whence);
    }
}

// The asserts are compiled out (conditions unknown); 15 of them push its inline cost past GC/2.6's
// limit (512), as retail calls it.
void GolfBall::HAZGolfBall::ReconToGolfBoom(zNMEHazard* haz, const xVec3* dir_norm)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (!haz->Reconfigure(HAZ_TYP_GOLFBOOM))
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (xVec3Length2(dir_norm) > 0.0f)
    {
        xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, dir_norm);

        NMEC_MakePerp((xVec3*)&haz->mdl_hazard->Mat->at, dir_norm);

        xVec3Cross((xVec3*)&haz->mdl_hazard->Mat->right, (xVec3*)&haz->mdl_hazard->Mat->up,
                   (xVec3*)&haz->mdl_hazard->Mat->at);
    }

    haz->tym_lifespan = haz->tmr_remain;
    haz->flg_hazard |= 0x1a;
}

namespace GolfBoom
{
    namespace FX
    {
        static const PuffEmitter puffVibor = {
            NPAR_TYP_DUSTPUFF,
            0.45f,
            { 0x9B, 0xFF, 0x9B, 0xAF },
            { 0.85f, 1.5f },
            { 0.0f, -10.0f, 0.0f },
            37.2f,
            99,
            1,
            33,
            0,
            { 1.0f, -5.0f },
        };
    } // namespace FX
} // namespace GolfBoom

void GolfBoom::HAZGolfBoom::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

en_hazrc GolfBoom::HAZGolfBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.85f;
    haz->custdata.typical.rad_min = 0.55f;
    haz->custdata.typical.rad_max = 5.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void GolfBoom::HAZGolfBoom::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    if (haz->npc_owner && haz->npc_owner->SelfType() == NME_TYPE_DENNIS_V1)
    {
        en_denstage okStages[4] = { DEN_STAGE_A, DEN_STAGE_B, DEN_STAGE_C, DEN_STAGE_UNKNOWN };
        if (!HAZ_DenStageInList(((zNMEDennis*)haz->npc_owner)->stageCurr, okStages))
        {
            haz->flg_hazard |= 0x4;
            return;
        }
    }

    typical->rad_cur = LERP(haz->pam_interp, typical->rad_min, typical->rad_max);

    haz->tmr_parEmit = MAX(-1.0f, haz->tmr_parEmit - dt);
    if (haz->tmr_parEmit < 0.0f)
    {
        haz->tmr_parEmit = 5.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        F32 spd = 2.0f + 3.0f * xurand();
        xVec3 vel = g_Y3 * spd;
        xVec3 pos = haz->pos_hazard + *(xVec3*)&haz->mdl_hazard->Mat->up * -0.25f;
        FX::puffVibor.RoundRing(&pos, typical->rad_cur, &vel);
    }
}

void GolfBoom::HAZGolfBoom::Render(zNMEHazard* haz)
{
    Stuff_Render(haz, -1.0f);
}

namespace GolfJunk
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace GolfJunk

en_hazrc GolfJunk::HAZGolfJunk::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0xc00b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.25f;
    haz->custdata.typical.rad_min = 0.35f;
    haz->custdata.typical.rad_max = 0.35f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_Y3 * 1.0f;

    return HAZ_RC_SUCCESS;
}

void GolfJunk::HAZGolfJunk::Update(zNMEHazard* haz, F32 dt)
{
    HAZTypical* typical = &haz->custdata.typical;

    if (haz->npc_owner && haz->npc_owner->SelfType() == NME_TYPE_DENNIS_V1)
    {
        en_denstage okStages[4] = { DEN_STAGE_A, DEN_STAGE_B, DEN_STAGE_C, DEN_STAGE_UNKNOWN };
        if (!HAZ_DenStageInList(((zNMEDennis*)haz->npc_owner)->stageCurr, okStages))
        {
            haz->flg_hazard |= 0x4;
            return;
        }
    }

    if (haz->pam_interp <= 0.25f)
    {
        typical->rad_cur = SMOOTH(haz->pam_interp / 0.25f, typical->rad_min, typical->rad_max);
    }
    else if (haz->pam_interp >= 0.75f)
    {
        // Retail bug kept: both builds compute pam_interp - 3.0 here (the division binds first).
        typical->rad_cur =
            SMOOTH(haz->pam_interp - 0.75f / 0.25f, typical->rad_max, typical->rad_min);
    }
    else
    {
        typical->rad_cur = typical->rad_max;
    }

    haz->pos_hazard += typical->vel * dt;

    if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
    {
        if (haz->ColPlyrSphere(typical->rad_cur))
        {
            haz->HurtThePlayer();
        }
    }

    if (haz->cnt_generic == 0 && haz->npc_owner && haz->pam_interp > 0.7f)
    {
        HAZTypical* typdat = &haz->custdata.typical;

        haz->cnt_generic = 1;
        haz->flg_hazard |= 0x40;

        if (haz->flg_hazard & 0x4000000)
        {
            zNMEMsg_AreaNMEExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f);
        }

        if (haz->flg_hazard & 0x8000000)
        {
            zNMEMsg_AreaExplode(haz->npc_owner, typdat->rad_cur, &haz->pos_hazard, -1.0f, 1);
        }
    }
}

void GolfJunk::HAZGolfJunk::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 0.75f, 0.0f));

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = GolfJunk::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace NyphToss
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 30.0f, 30.0f, 30.0f };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render
} // namespace NyphToss

HAZShadowProps NyphToss::Shadow::shadPropData = { 0.4f, 20.0f, 0.35f, 15.0f, -1, 0.3f };

namespace NyphToss
{
    namespace FX
    {
        static const PopGasEmitter gasTrail = {
            NPAR_TYP_POPGAS,
            0.55f,
            { 0xFF, 0xE1, 0xFF, 0xFF },
            { 0.4f, 1.0f },
            { 0.0f, 0.0f, 0.0f },
            0.7853982f,
            99,
            0,
            2,
            0,
            { 0.0f, -1.0f, 0.0f, 0.0f, 0.0f },
        };

        namespace Ribbon
        {
            static const xFXRibbon::curve_node curvedata[11] = {
                { 0.0f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.0f },
                { 0.1f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.5f },
                { 0.2f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.0f },
                { 0.3f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.25f },
                { 0.4f, { 0xC8, 0xC8, 0xC8, 0xFF }, 0.75f },
                { 0.5f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.0f },
                { 0.6f, { 0xC8, 0xC8, 0xC8, 0xFF }, 0.5f },
                { 0.7f, { 0xC8, 0xC8, 0xC8, 0xC8 }, 0.75f },
                { 0.8f, { 0xC8, 0xC8, 0xC8, 0xC8 }, 0.25f },
                { 0.9f, { 0xC8, 0xC8, 0xC8, 0x96 }, 0.5f },
                { 1.0f, { 0xC8, 0xC8, 0xC8, 0x00 }, 0.0f },
            };

            NMEHelpRib::RibData rib_knife = {
                { 1, 0, 0, 0 },
                { "lightning", (xFXRibbon::curve_node*)curvedata, 11, 0.22f, 0.5f },
                { 1, 1.0f, 1.0f },
                { NULL, 0 },
            };
        } // namespace Ribbon
    } // namespace FX
} // namespace NyphToss

void NyphToss::HAZNyphToss::ScenePrepare()
{
    HAZBasic::ScenePrepare();
    shadProps = &Shadow::shadPropData;
}

void NyphToss::HAZNyphToss::ScenePostInit()
{
    Stuff_PostInit();
    FX::Ribbon::rib_knife.FindTexture();
    zNMEParFX_PartyInvite(NPAR_TYP_POPGAS, NULL, 0);
}

en_hazrc NyphToss::HAZNyphToss::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZCollide* hazcol;
    HAZLob* hazlob;

    haz->flg_hazard |= 0x161b380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazcol = &haz->custdata.collide;
    hazcol->flg_collide |= 7;
    hazcol->flg_collide &= ~3;
    hazcol->flg_collide &= ~4;

    hazlob = &haz->custdata.lobdata;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xfd;
        }
        haz->cnt_nextFill = -1;
    }

    if (FX::Ribbon::rib_knife.flags.enable)
    {
        haz->fxdata.ribdata.ribbon = zFXRibbonPoolNew();
    }
    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        FX::Ribbon::rib_knife.ConfigRibbon(haz->fxdata.ribdata.ribbon);
    }

    haz->tmr_remain = 0.25f;
    hazlob->rad_min = 0.5f;
    hazlob->rad_max = 0.5f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;

    return HAZ_RC_SUCCESS;
}

void NyphToss::HAZNyphToss::Update(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;
    F32 tym;

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        EndOfLifespan(haz);
        return;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    if (haz->pam_interp > 0.25f)
    {
        hazlob->rad_cur = hazlob->rad_max;
    }
    else
    {
        hazlob->rad_cur = LERP(4.0f * haz->pam_interp, hazlob->rad_min, hazlob->rad_max);
    }

    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchVel(haz, haz->tmr_remain, 45.0f);
    }

    if (haz->flg_hazard & 8)
    {
        haz->PreCollide(45.0f, 0.0f);
    }

    tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);

    // tym is reused for the speed and then the scale (one register in the PS2 code too).
    // Not in the PS2 DWARF: dir, mat_look (block locals in retail's stack layout).
    tym = xVec3Length(&hazlob->vel);
    if (tym > 0.00001f)
    {
        xMat3x3 mat_look;
        xVec3 dir;

        tym = -1.0f / tym;
        xVec3SMul(&dir, &hazlob->vel, tym);
        xMat3x3LookVec(&mat_look, &dir);
        haz->TypData_RotMatSet(&mat_look);
    }

    haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                        hazlob->rad_cur, &hazlob->vel, dt, 1);
    haz->StaggeredCollide();

    // Not in the PS2 DWARF: rib, scale and orient (its line table puts this whole block on one
    // line, as if inlined from a helper, but retail's stack layout matches block locals here).
    xFXRibbon* rib = haz->fxdata.ribdata.ribbon;
    if (rib)
    {
        haz->fxdata.ribdata.cnt_skipRibInsert--;
        if (haz->fxdata.ribdata.cnt_skipRibInsert <= 0)
        {
            haz->fxdata.ribdata.cnt_skipRibInsert = FX::Ribbon::rib_knife.updinfo.frq_ribJoint;
            F32 scale = hazlob->rad_cur * FX::Ribbon::rib_knife.updinfo.scl_master;
            xVec3 orient = *(xVec3*)&haz->mdl_hazard->Mat->right;
            rib->insert(haz->pos_hazard, orient, scale, FX::Ribbon::rib_knife.updinfo.alf_master,
                        0);
        }
    }
}

void NyphToss::HAZNyphToss::Render(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad;

    haz->SetAlpha(1.0f);

    rad = 5.0f * hazlob->rad_cur;
    haz->mdl_hazard->Scale = NyphToss::Render::vec_scale * rad;

    Stuff_Render(haz, 2.5f * hazlob->rad_cur);
}

void NyphToss::HAZNyphToss::CollideResponse(zNMEHazard* haz, S32 flg_hazcolf,
                                            const xSweptSphere* swdata, F32 tym_inFuture)
{
    HAZLob* hazlob = &haz->custdata.lobdata;

    if (flg_hazcolf & 0x10)
    {
        xParabola* parab = hazlob->parabinfo;
        F32 tym_alive = haz->tym_lifespan - haz->tmr_remain;
        F32 tym_endPtEval = tym_alive + tym_inFuture;

        tym_alive = MIN(tym_alive, haz->tym_lifespan);
        haz->tmr_remain = MIN(haz->tmr_remain, tym_inFuture);
        parab->maxTime = tym_alive + haz->tmr_remain;
        haz->tym_lifespan = parab->maxTime;

        xParabolaEvalPos(parab, &hazlob->pos_collide, tym_endPtEval);
        hazlob->dir_normal = swdata->worldNormal;

        haz->flg_hazard |= 0x80000;
    }

    Stuff_CollRespDflt(haz, flg_hazcolf, swdata, tym_inFuture);

    hazlob->flg_collide = 0;
}

void NyphToss::HAZNyphToss::EndOfLifespan(zNMEHazard* haz)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;

    if (haz->flg_hazard & 0x80000)
    {
        if (haz->flg_hazard & 0x2000)
        {
            if (haz->npc_owner == NULL)
            {
                zEntPlayer_Damage(NULL, 1);
            }
            else
            {
                zEntPlayer_DamageNPCKnockBack(haz->npc_owner, 1, &haz->pos_hazard,
                                              ePlayerNMEDamageEnemyProjectile);
            }
        }

        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);

        xVec3 whence = hazlob->vel;
        KnifeImpact(haz, &whence);
    }
    else if (haz->flg_hazard & 0x20000)
    {
        xParabolaEvalPos(parab, &haz->pos_hazard, parab->maxTime);
        Recon_Vicious(haz, &hazlob->dir_normal, 1);
    }
    else
    {
        xVec3 whence;

        xParabolaEvalVel(parab, &whence, parab->maxTime);
        whence.invert();

        F32 len = xVec3Length(&whence);
        if (len > 0.00001f)
        {
            whence /= len;
            KnifeImpact(haz, &whence);
        }
        else
        {
            KnifeImpact(haz, (xVec3*)&haz->mdl_hazard->Mat->up);
        }
    }
}

void NyphToss::HAZNyphToss::KnifeImpact(zNMEHazard* haz, const xVec3* dir_norm)
{
    Recon_Vicious(haz, dir_norm, 0);
}

// dir_norm is unused (the PS2 build has no such parameter). The two rad_old tests are separate
// statements, as in zNMECommon::LaunchProjectile (one test compiles to a single compare).
void NyphToss::HAZNyphToss::Recon_Vicious(zNMEHazard* haz, const xVec3* dir_norm, S32 doStick)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    F32 rad_old = hazlob->rad_cur;
    xMat4x3 mat_impact = *xModelGetFrame(haz->mdl_hazard);
    S32 rc;

    rc = haz->Reconfigure(HAZ_TYP_DENNIS_KNIFEBOOM);
    if (!rc)
    {
        haz->flg_hazard |= 0x4;
        return;
    }

    if (rad_old >= 0.0f)
    {
        hazlob->rad_min = rad_old;
    }
    if (rad_old >= 0.0f)
    {
        hazlob->rad_max = rad_old;
    }
    hazlob->rad_cur = hazlob->rad_min;

    haz->TypData_RotMatSet(&mat_impact);

    haz->Start(&mat_impact.pos, -1.0f);

    if (doStick)
    {
        return;
    }

    // not in the DWARF: retail keeps the new time in a register for both LifespanSet arguments;
    // storing it to tmr_remain first and passing tmr_remain twice reloads it.
    F32 tym = MAX(0.25f, 0.5f * haz->tmr_remain);
    haz->LifespanSet(tym, tym);
    haz->flg_hazard &= ~0x100;
}

namespace NyphBoom
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { 2.0f * PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace Vicious
    {
        static const xVec3 dir_emitFlames = { 0.0f, 0.75f, 1.0f };

        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.25f, { 0xC8, 0xC8, 0xC8, 0x32 }, 1.0f },
            { 0.32f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.5f, { 0x32, 0x64, 0xC8, 0x32 }, 1.0f },
            { 0.62f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.75f, { 0xC8, 0xC8, 0xC8, 0x32 }, 1.0f },
            { 0.85f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 1.0f, { 0x32, 0x64, 0xC8, 0x00 }, 0.5f },
        };

        ExhaustEmitter etherealFlames = {
            NPAR_TYP_SEASPARKLE,
            { 1, 0, { 0, 0 } },
            { 15.0f, 0.0f, 6.0f },
            { 1.0f, 1.0f },
            { 0.15f, { 0.05f, 0.25f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.5f },
            { 3, 3, 0.35f, -0.45f },
            { 5.0f, 5.0f, 0.5f, 0.25f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 8, 1.0f, 1.0f },
        };
    } // namespace Vicious
} // namespace NyphBoom

void NyphBoom::HAZNyphBoom::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_SEASPARKLE, NULL, 0);
}

en_hazrc NyphBoom::HAZNyphBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0xc00b180;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 0.5f;
    haz->custdata.typical.rad_max = 0.5f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void NyphBoom::HAZNyphBoom::Update(zNMEHazard* haz, F32 dt)
{
    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    haz->custdata.typical.rad_cur =
        LERP(haz->pam_interp, haz->custdata.typical.rad_max, haz->custdata.typical.rad_min);

    if ((haz->flg_hazard & 0x2000) && !(globals.player.DamageTimer > 0.0f))
    {
        if (haz->ColPlyrSphere(haz->custdata.typical.rad_cur))
        {
            haz->HurtThePlayer();
        }
    }

    ViciousFlames(haz, dt);
}

void NyphBoom::HAZNyphBoom::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 1.0f, 0.0f));

    rad = 5.0f * typical->rad_cur;
    haz->mdl_hazard->Scale = NyphBoom::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

// No GC symbol: a plain member auto-inlined into its only caller (Update) and dead-stripped; the
// PS2 build has it out of line.
void NyphBoom::HAZNyphBoom::ViciousFlames(zNMEHazard* haz, F32 dt)
{
    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = 4.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

        xVec3 dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->at;
        dir_emit.invert();
        xVec3 pos_emit = *(xVec3*)&haz->mdl_hazard->Mat->pos;

        Vicious::etherealFlames.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1, NULL);
    }
}

namespace TuneBorn
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX_Notes
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.0f },
            { 0.15f, { 0xFF, 0x00, 0x00, 0x7F }, 1.0f },
            { 0.3f, { 0xFF, 0x00, 0xFF, 0x7F }, 0.75f },
            { 0.45f, { 0x00, 0x00, 0xFF, 0x7F }, 0.9f },
            { 0.6f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.5f },
            { 0.75f, { 0x00, 0xFF, 0x00, 0x7F }, 0.7f },
            { 0.9f, { 0xFF, 0xFF, 0x00, 0x7F }, 0.35f },
            { 1.0f, { 0xF0, 0xF0, 0x64, 0x00 }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.25f, 0.0f };
        static const xVec3 vec_offplyr = { 1.0f, -0.15f, 0.0f };

        static const ExhaustEmitter notes = {
            NPAR_TYP_NOTES,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 4.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 8, 0.5f, 0.6f },
        };
    } // namespace FX_Notes

    namespace FX_SuperNotes
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.0f },
            { 0.15f, { 0xFF, 0x00, 0x00, 0x7F }, 1.0f },
            { 0.3f, { 0xFF, 0x00, 0xFF, 0x7F }, 0.75f },
            { 0.45f, { 0x00, 0x00, 0xFF, 0x7F }, 0.9f },
            { 0.6f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.5f },
            { 0.75f, { 0x00, 0xFF, 0x00, 0x7F }, 0.7f },
            { 0.9f, { 0xFF, 0xFF, 0x00, 0x7F }, 0.35f },
            { 1.0f, { 0xF0, 0xF0, 0x64, 0x00 }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.25f, 0.0f };
        static const xVec3 vec_offplyr = { 1.0f, -0.15f, 0.0f };

        static const ExhaustEmitter notes = {
            NPAR_TYP_NOTES_UPGRADE,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.15f, -0.15f },
            { 3.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 8, 1.0f, 1.0f },
        };

        // No symbol in either build (always inlined into HAZTuneBorn::Update); name is ours, after
        // the GC's out-of-line HAZTuneBorn::EmitNormalNotes.
        static inline void EmitSuperNotes(zNMEHazard* haz)
        {
            xVec3 pos_emit = haz->pos_hazard;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * vec_offset.x;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * vec_offset.y;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * vec_offset.z;

            notes.EmitRing(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at, -100000.0f, 1.0f, -1,
                           NULL);
        }
    } // namespace FX_SuperNotes

    namespace FX_Waves
    {
        OrientCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 1.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.15f, 0.0f };

        static const OrientEmitter waves = {
            NPAR_TYP_RKOWAVES,
            { 0, 0, 0, 0 },
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace FX_Waves

    namespace FX_Smokey
    {
        ExhaustCurve curve_data[7] = {
            { 0.0f, { 0x00, 0xFF, 0xFF, 0x7F }, 0.0f },
            { 0.33f, { 0xFF, 0xFF, 0x00, 0x7F }, 1.0f },
            { 0.33f, { 0xFF, 0xFF, 0x00, 0x7F }, 1.0f },
            { 0.66f, { 0xFF, 0x00, 0xFF, 0x7F }, 1.0f },
            { 0.66f, { 0xFF, 0x00, 0xFF, 0x7F }, 1.0f },
            { 0.99f, { 0x32, 0x32, 0x32, 0x7F }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.35f, 0.0f };

        static const ExhaustEmitter exhaust = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { 2.0f, 0.0f, 10.0f },
            { 0.5f, 0.5f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.76f, 0.3f },
            { 30, 0, 0.25f, 0.0f },
            { -5.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 7, 1.0f, 0.5f },
        };

        // No symbol in either build (always inlined into HAZTuneBorn::Update); name is ours.
        static inline void EmitSmoke(zNMEHazard* haz)
        {
            xVec3 pos_emit = haz->pos_hazard;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * vec_offset.x;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * vec_offset.y;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * vec_offset.z;

            xVec3 dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->at * -1.0f;

            exhaust.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1, NULL);
        }
    } // namespace FX_Smokey

    namespace FX_Sparks
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.0f, 0.35f };

        static const SparkyEmitter sparker = {
            NPAR_TYP_SPARKY,
            { 0, 0, { 0, 0 } },
            { 16.0f, 7.0f },
            { 0.02f, 0.25f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.75f, 0.5f },
            { 30, 20, 0.1f, -0.1f },
            { 15.0f, 10.0f, 1.0f, 0.5f, -10.0f, 2.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 0.25f },
        };
    } // namespace FX_Sparks
} // namespace TuneBorn

void TuneBorn::HAZTuneBorn::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_RKOWAVES, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_NOTES, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_NOTES_UPGRADE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_EXHAUST, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_HEATED, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SPARKY, NULL, 0);
}

en_hazrc TuneBorn::HAZTuneBorn::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 1.0f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void TuneBorn::HAZTuneBorn::Update(zNMEHazard* haz, F32 dt)
{
    haz->custdata.typical.rad_cur =
        xlerp(haz->custdata.typical.rad_min, haz->custdata.typical.rad_max, haz->pam_interp);

    en_plyrpup pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR);
    if (pup > ePowerupLevelNormal)
    {
        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = 12.0f * FrameTime();
            FX_SuperNotes::EmitSuperNotes(haz);
        }

        // These two timers only reset themselves (in the PS2 build too); nothing is emitted.
        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = 15.0f * FrameTime();
        }

        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = (1 + (S8)((xrand_GenRandInt32() & 0x800000) != 0)) * FrameTime();
        }

        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = (12 + (S8)((xrand_GenRandInt32() & 0x800000) != 0)) * FrameTime();
            FX_Smokey::EmitSmoke(haz);
        }
    }
    else
    {
        haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);
        if (haz->tmr_generic < 0.0f)
        {
            haz->tmr_generic = 7.0f * FrameTime();
            EmitNormalNotes(haz);
        }
    }
}

void TuneBorn::HAZTuneBorn::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 1.0f, 1.0f));

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = TuneBorn::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

void TuneBorn::HAZTuneBorn::EmitNormalNotes(zNMEHazard* haz)
{
    xVec3 pos_emit = haz->pos_hazard;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * FX_Notes::vec_offset.x;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * FX_Notes::vec_offset.y;
    pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * FX_Notes::vec_offset.z;
    FX_Notes::notes.EmitRing(&pos_emit, (xVec3*)&haz->mdl_hazard->Mat->at, -100000.0f, 1.0f, -1,
                             NULL);

    xVec3 pos_plyr = haz->pos_hazard;
    pos_plyr += *(xVec3*)&haz->mdl_hazard->Mat->right * FX_Notes::vec_offplyr.x;
    pos_plyr += *(xVec3*)&haz->mdl_hazard->Mat->up * FX_Notes::vec_offplyr.y;
    pos_plyr += *(xVec3*)&haz->mdl_hazard->Mat->at * FX_Notes::vec_offplyr.z;
    FX_Notes::notes.EmitRing(&pos_plyr,
                             (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->at,
                             -100000.0f, 1.0f, -1, NULL);
}

namespace TuneBang
{
    namespace Render
    {
        static const xVec3 vec_scale = { 0.25f, 0.25f, 1.0f };
    } // namespace Render
} // namespace TuneBang

en_hazrc TuneBang::HAZTuneBang::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.75f;
    haz->custdata.typical.rad_min = 0.5f;
    haz->custdata.typical.rad_max = 0.5f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void TuneBang::HAZTuneBang::Update(zNMEHazard* haz, F32 dt)
{
    haz->custdata.typical.rad_cur =
        xlerp(haz->custdata.typical.rad_min, haz->custdata.typical.rad_max, haz->pam_interp);

    if (haz->flg_hazard & 0x8)
    {
        BubbleCannon(haz);
    }
}

void TuneBang::HAZTuneBang::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 alfa;
    F32 rad;

    alfa = 1.0f;
    if (haz->pam_interp < 0.2f)
    {
        alfa = LERP(haz->pam_interp / 0.2f, 1.0f, 1.0f);
    }

    haz->SetAlpha(alfa);

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = TuneBang::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace TuneBang
{
    namespace FX_Cannonade
    {
        static const xVec3 vec_offset = { 0.0f, 0.0f, 0.35f };
    } // namespace FX_Cannonade
} // namespace TuneBang

// The model axes are read through zNMEHazard's At()/Right()/Up() (BfBB's NPCHazard accessors): the
// inline's return value becomes a temporary, which splits the pos_base block where retail does.
// Other functions of this file read the matrix directly, as HI did in places.
void TuneBang::HAZTuneBang::BubbleCannon(zNMEHazard* haz)
{
    xVec3* buffer = (xVec3*)xMemPushTemp(2 * 120 * sizeof(xVec3));
    if (buffer)
    {
        xVec3 pos_base;
        xVec3 dir_axis, dir_perp, dir_orth;

        pos_base = haz->pos_hazard;
        pos_base += *haz->Right() * FX_Cannonade::vec_offset.x;
        pos_base += *haz->Up() * FX_Cannonade::vec_offset.y;
        pos_base += *haz->At() * FX_Cannonade::vec_offset.z;

        dir_axis = *haz->At();
        dir_perp = *haz->Right();
        dir_orth = *haz->Up();

        xVec3* buf_vel = buffer + 120;
        for (S32 i = 0; i < 120; i++)
        {
            F32 ang_curr = -PI + (2.0f * PI / 120.0f) * (i + xurand());

            // not in the DWARF: ang_cos/ang_sin. Retail calls icos and isin once each and uses
            // both results for the position jitter and the velocity spread.
            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);

            F32 rad_jitr = 0.05f * xurand();
            buffer[i] = pos_base;
            buffer[i] += dir_perp * (ang_cos * rad_jitr);
            buffer[i] += dir_orth * (ang_sin * rad_jitr);

            F32 rad_curr = 0.25f * xurand();
            xVec3 dir_dest = dir_axis;
            dir_dest += dir_perp * (ang_cos * rad_curr);
            dir_dest += dir_orth * (ang_sin * rad_curr);
            dir_dest.normalize();

            buf_vel[i] = dir_dest * (15.0f * xurand() + 2.0f);
        }

        zParPTankSpawnBubbles(buffer, buf_vel, 120, 1.0f);
        xMemPopTemp(buffer);
    }
}

namespace TuneFlyt
{
    namespace Render
    {
        static const xVec3 vec_scale = { 0.25f, 0.25f, 1.0f };
    } // namespace Render

    namespace FX_Warp
    {
        WarpData warphelp = { 0, 0, 0.0f, 0.0f, 0.0f, 0.0f };
    } // namespace FX_Warp

    namespace FX_Thrust
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
        };

        static const xVec3 vec_offset = { 0.0f, 0.0f, 0.0f };

        static const SparkyEmitter thrust = {
            NPAR_TYP_COLORS,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f },
            { 0.02f, 0.25f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.75f, 0.0f },
            { 23, 0, 0.25f, 0.0f },
            { 1.0f, 0.0f, 2.0f, 0.0f, 2.0f, -3.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 0.5f, 1.0f },
        };

        // No symbol in either build (always inlined into HAZTuneFlyt::Update); name is ours.
        // Retail places pos_emit/dir_emit with the compiler temporaries, as it does for the
        // locals of an inlined function, and the PS2 DWARF of Update lists no locals.
        static inline void EmitThrust(zNMEHazard* haz)
        {
            xVec3 pos_emit = haz->pos_hazard;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->right * vec_offset.x;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->up * vec_offset.y;
            pos_emit += *(xVec3*)&haz->mdl_hazard->Mat->at * vec_offset.z;

            xVec3 dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->at * -1.0f;

            thrust.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);
        }
    } // namespace FX_Thrust
} // namespace TuneFlyt

void TuneFlyt::HAZTuneFlyt::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_COLORS, NULL, 0);
}

en_hazrc TuneFlyt::HAZTuneFlyt::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.75f;
    haz->custdata.typical.rad_min = 0.5f;
    haz->custdata.typical.rad_max = 0.5f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void TuneFlyt::HAZTuneFlyt::Update(zNMEHazard* haz, F32 dt)
{
    if (!(haz->flg_hazard & 0x10000000))
    {
        haz->custdata.typical.rad_cur =
            LERP(haz->pam_interp, haz->custdata.typical.rad_min, haz->custdata.typical.rad_max);

        if (haz->flg_hazard & 8)
        {
            haz->custdata.misctyp.ptr = NULL;
        }

        haz->tmr_parEmit = MAX(-1.0f, haz->tmr_parEmit - dt);
        if (haz->tmr_parEmit < 0.0f)
        {
            haz->tmr_parEmit = 2.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            FX_Thrust::EmitThrust(haz);
        }
    }
}

void TuneFlyt::HAZTuneFlyt::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 alfa;
    F32 rad;

    alfa = 1.0f;
    if (haz->pam_interp < 0.2f)
    {
        alfa = LERP(haz->pam_interp / 0.2f, 1.0f, 1.0f);
    }

    haz->SetAlpha(alfa);

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = TuneFlyt::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace TuneBoom
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
        };

        static const SparkyEmitter sparker = {
            NPAR_TYP_SPARKY,
            { 0, 0, { 0, 0 } },
            { 16.0f, 0.0f },
            { 0.02f, 0.25f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.35f, 1.15f },
            { 5, 5, 0.05f, -0.05f },
            { 10.0f, 10.0f, 1.0f, 0.5f, 0.0f, 2.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 0.5f, 0.25f },
        };
    } // namespace FX

    namespace FX_Rings
    {
        OrientCurve curve_data[7] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 1.0f },  { 0.2f, { 0x00, 0xFF, 0x00, 0x7F }, 2.0f },
            { 0.4f, { 0x00, 0x00, 0xFF, 0x7F }, 4.0f },  { 0.6f, { 0x00, 0xFF, 0xFF, 0x7F }, 8.0f },
            { 0.8f, { 0xFF, 0x00, 0xFF, 0x7F }, 16.0f }, { 0.9f, { 0xFF, 0xFF, 0x00, 0x7F }, 8.0f },
            { 1.0f, { 0x37, 0x37, 0x37, 0x00 }, 0.0f },
        };

        static const OrientEmitter boomrings = {
            NPAR_TYP_RKOWAVES,
            { 0, 1, 0, 0 },
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.15f },
            { 1, 0, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 7, 1.0f, 1.0f },
        };
    } // namespace FX_Rings
} // namespace TuneBoom

void TuneBoom::HAZTuneBoom::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_SPARKY, NULL, 0);
}

en_hazrc TuneBoom::HAZTuneBoom::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x9080;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 0.15f;
    haz->custdata.typical.rad_min = 1.0f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    return HAZ_RC_SUCCESS;
}

void TuneBoom::HAZTuneBoom::Update(zNMEHazard* haz, F32 dt)
{
    if ((haz->flg_hazard & 0x10000000) && sndTable && haz->sndIDPlayed)
    {
        HAZ_SndStop(haz->sndIDPlayed);
        haz->sndIDPlayed = (iSndHandle)0;
    }

    if ((haz->flg_hazard & 8) && sndTable)
    {
        haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
    }

    haz->custdata.typical.rad_cur =
        LERP(haz->pam_interp, haz->custdata.typical.rad_min, haz->custdata.typical.rad_max);

    if (haz->flg_hazard & 8)
    {
        BubbleBlast(haz);
    }

    haz->tmr_parEmit = MAX(-1.0f, haz->tmr_parEmit - dt);
    if (haz->tmr_parEmit < 0.0f)
    {
        haz->tmr_parEmit = 15.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
    }
}

void TuneBoom::HAZTuneBoom::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 0.75f, 0.0f));

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = TuneBoom::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

void TuneBoom::HAZTuneBoom::BubbleBlast(zNMEHazard* haz)
{
    xVec3* buffer = (xVec3*)xMemPushTemp(2 * 150 * sizeof(xVec3));
    if (buffer != NULL)
    {
        xVec3 pos_base = haz->pos_hazard;
        xVec3* buf_vel = buffer + 150;
        en_plyrpup pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR);
        // not in the DWARF: spd_blast. Both builds pick the speed right after the powerup lookup,
        // before the loops.
        F32 spd_blast = (pup > ePowerupLevelNormal) ? 7.0f : 5.0f;

        for (S32 seg = 0; seg < 12; seg++)
        {
            F32 ang_wedge = (2.0f * PI / 12) * seg;

            for (S32 i = 0; i < 12; i++)
            {
                F32 ang_curr = -PI + ang_wedge + (2.0f * PI / 12) * xurand();
                // not in the DWARF: retail makes both calls before spd_y.
                F32 ang_cos = icos(ang_curr);
                F32 ang_sin = isin(ang_curr);
                // not in the DWARF: vel. Retail takes the address of this bubble's velocity
                // slot here, before spd_y (the position slot only at the end).
                xVec3& vel = buf_vel[seg * 12 + i];

                F32 spd_y = 2.0f * (xurand() - 0.5f);
                F32 pythag = 1.0f - SQ(spd_y);
                pythag = xsqrt(pythag);

                xVec3 dir_dest;
                dir_dest = g_X3 * (ang_cos * pythag);
                dir_dest += g_Z3 * (ang_sin * pythag);
                dir_dest += g_Y3 * spd_y;

                vel = dir_dest * (spd_blast * xurand());

                xVec3& pos = buffer[seg * 12 + i];
                pos = pos_base;
                pos += dir_dest * 0.55f;
            }
        }

        zParPTankSpawnBubbles(buffer, buf_vel, 12 * 12, 1.0f);
        xMemPopTemp(buffer);
    }
}

namespace BowWave
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX_SeaDrips
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        SparkyEmitter sea_drips = {
            NPAR_TYP_SEAMIST,
            { 0, 0, { 0, 0 } },
            { 10.0f, 0.0f },
            { 0.15f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.5f },
            { 1, 0, 1.0f, -1.0f },
            { 10.0f, -1.0f, 10.0f, 5.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 3.0f },
        };
    } // namespace FX_SeaDrips
} // namespace BowWave

void BowWave::HAZBowWave::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_SEAMIST, NULL, 0);
}

// not in the DWARF: uva (the PS2 demo keeps the lookup's result for the next two lines)
en_hazrc BowWave::HAZBowWave::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x109980;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 1.0f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    UVAModelInfo* uva = HAZ_UVAnimInfo(haz);
    if (uva)
    {
        uva->UVVelSet(0.0f, 2.0f);
    }

    return HAZ_RC_SUCCESS;
}

void BowWave::HAZBowWave::Update(zNMEHazard* haz, F32 dt)
{
    if (!(haz->flg_hazard & 0x10000000))
    {
        haz->custdata.typical.rad_cur =
            LERP(haz->pam_interp, haz->custdata.typical.rad_min, haz->custdata.typical.rad_max);

        haz->tmr_parEmit = MAX(-1.0f, haz->tmr_parEmit - dt);
        if (haz->tmr_parEmit < 0.0f)
        {
            haz->tmr_parEmit = 10.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            FX_SeaDrips::sea_drips.DenSeaItRain((xVec3*)&haz->mdl_hazard->Mat->pos,
                                                (xVec3*)&haz->mdl_hazard->Mat->pos, NULL,
                                                -100000.0f, 1.0f, -1);
        }
    }
}

void BowWave::HAZBowWave::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 1.0f, 1.0f));

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = BowWave::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace WaveRing
{
    namespace Render
    {
        static const xVec3 scl_wideNorm = { 2.0f, 0.8f, 2.0f };
        static const xVec3 scl_tallNorm = { 1.0f, 1.6f, 1.0f };
    } // namespace Render

    namespace RingDrips
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0x50, 0xB4, 0xF5, 0xFF }, 1.0f },
            { 1.0f, { 0x50, 0xB4, 0xF5, 0x00 }, 0.5f },
        };

        SparkyEmitter ring_drips = {
            NPAR_TYP_RAIN,
            { 0, 0, { 0, 0 } },
            { 12.5f, 0.0f },
            { 0.15f, 0.4f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.5f },
            { 5, 0, 1.0f, -1.0f },
            { 4.0f, 4.0f, 1.0f, 1.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace RingDrips
} // namespace WaveRing

void WaveRing::HAZWaveRing::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_RAIN, NULL, 0);
}

// not in the DWARF: uva (the PS2 demo keeps the lookup's result for the next two lines)
en_hazrc WaveRing::HAZWaveRing::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x109980;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 1.0f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    UVAModelInfo* uva = HAZ_UVAnimInfo(haz);
    if (uva)
    {
        uva->UVVelSet(0.0f, -1.0f);
    }

    return HAZ_RC_SUCCESS;
}

void WaveRing::HAZWaveRing::Update(zNMEHazard* haz, F32 dt)
{
    if (haz->flg_hazard & 0x10000000)
    {
        haz->flg_hazard |= 4;
    }
    else
    {
        haz->custdata.typical.rad_cur =
            LERP(haz->pam_interp, haz->custdata.typical.rad_min, haz->custdata.typical.rad_max);

        if (haz->flg_hazard & 8)
        {
            LaunchDroplets(haz);
        }
    }
}

void WaveRing::HAZWaveRing::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;
    xVec3 scale;

    haz->SetAlpha(LERP(haz->pam_interp, 1.0f, 1.0f));

    rad = typical->rad_cur;
    F32 squash = CLAMP(xabs(isin(PI * haz->pam_interp)), 0.0f, 1.0f);

    scale = WaveRing::Render::scl_wideNorm;
    scale.y *= squash;
    haz->mdl_hazard->Scale = scale * rad;
    Stuff_Render(haz, -1.0f);

    scale = WaveRing::Render::scl_tallNorm;
    scale.y *= squash;
    haz->mdl_hazard->Scale = scale * rad;
    Stuff_Render(haz, -1.0f);
}

void WaveRing::HAZWaveRing::LaunchDroplets(zNMEHazard* haz)
{
    en_hazfrag typ_frag = HAZ_FRAG_GENERIC;
    zShrapnelAsset* shrap;

    switch (haz->typ_hazard)
    {
    case HAZ_TYP_VISSPLASH_WAVE:
    case HAZ_TYP_VISSPLASH_DROP:
        break;
    case HAZ_TYP_VISSPLASH_WATER:
        typ_frag = HAZ_FRAG_WATER_DROPS;
        break;
    case HAZ_TYP_VISSPLASH_CHOC:
        typ_frag = HAZ_FRAG_CHOC_DROPS;
        break;
    case HAZ_TYP_VISSPLASH_LAVA:
        typ_frag = HAZ_FRAG_LAVA_DROPS;
        break;
    case HAZ_TYP_VISSPLASH_BILE:
        typ_frag = HAZ_FRAG_BILE_DROPS;
        break;
    case HAZ_TYP_VISSPLASH_OIL:
        typ_frag = HAZ_FRAG_OIL_DROPS;
        break;
    case HAZ_TYP_VISSPLASH_SAUCE:
        typ_frag = HAZ_FRAG_SAUCE_DROPS;
        break;
    case HAZ_TYP_VISSPLASH_TOXIC:
        typ_frag = HAZ_FRAG_TOXIC_DROPS;
        break;
    }

    if (typ_frag == HAZ_FRAG_GENERIC)
    {
        return;
    }

    shrap = g_data_hazshrap[typ_frag];
    if (shrap == NULL)
    {
        return;
    }

    F32 spd_emit = 1.25f * haz->custdata.typical.rad_cur;
    xVec3 vel_emit = *haz->Up() * spd_emit;

    shrap->initCB(shrap, haz->mdl_hazard, &vel_emit, NULL);
}

namespace Droplet
{
    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX_SeaDrips
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        SparkyEmitter sea_drips = {
            NPAR_TYP_SEAMIST,
            { 0, 0, { 0, 0 } },
            { 10.0f, 0.0f },
            { 0.15f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.5f },
            { 1, 0, 1.0f, -1.0f },
            { 10.0f, -1.0f, 10.0f, 5.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 3.0f },
        };
    } // namespace FX_SeaDrips
} // namespace Droplet

void Droplet::HAZDroplet::ScenePostInit()
{
    Stuff_PostInit();
    zNMEParFX_PartyInvite(NPAR_TYP_SEAMIST, NULL, 0);
}

// not in the DWARF: uva (the PS2 demo keeps the lookup's result for the next two lines)
en_hazrc Droplet::HAZDroplet::Configure(zNMEHazard* haz)
{
    S32 rc;

    haz->flg_hazard |= 0x109980;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    haz->tmr_remain = 1.0f;
    haz->custdata.typical.rad_min = 1.0f;
    haz->custdata.typical.rad_max = 1.0f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->custdata.typical.vel = g_O3;

    UVAModelInfo* uva = HAZ_UVAnimInfo(haz);
    if (uva)
    {
        uva->UVVelSet(0.0f, 2.0f);
    }

    return HAZ_RC_SUCCESS;
}

void Droplet::HAZDroplet::Update(zNMEHazard* haz, F32 dt)
{
    if (!(haz->flg_hazard & 0x10000000))
    {
        haz->custdata.typical.rad_cur =
            LERP(haz->pam_interp, haz->custdata.typical.rad_min, haz->custdata.typical.rad_max);

        haz->tmr_parEmit = MAX(-1.0f, haz->tmr_parEmit - dt);
        if (haz->tmr_parEmit < 0.0f)
        {
            haz->tmr_parEmit = 10.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            FX_SeaDrips::sea_drips.DenSeaItRain((xVec3*)&haz->mdl_hazard->Mat->pos,
                                                (xVec3*)&haz->mdl_hazard->Mat->pos, NULL,
                                                -100000.0f, 1.0f, -1);
        }
    }
}

void Droplet::HAZDroplet::Render(zNMEHazard* haz)
{
    HAZTypical* typical = &haz->custdata.typical;
    F32 rad;

    haz->SetAlpha(LERP(haz->pam_interp, 1.0f, 1.0f));

    rad = typical->rad_cur;
    haz->mdl_hazard->Scale = Droplet::Render::vec_scale * rad;

    Stuff_Render(haz, -1.0f);
}

namespace Reflect
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace FX_Boom
    {
        OrientCurve curve_data[8] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0xFF }, 0.25f },
            { 0.15f, { 0xFF, 0x00, 0x00, 0xFF }, 0.5f },
            { 0.3f, { 0xFF, 0x00, 0xFF, 0xE1 }, 1.0f },
            { 0.45f, { 0x00, 0x00, 0xFF, 0xE1 }, 2.0f },
            { 0.6f, { 0x00, 0xFF, 0xFF, 0xC8 }, 1.0f },
            { 0.75f, { 0x00, 0xFF, 0x00, 0x96 }, 0.5f },
            { 0.9f, { 0xFF, 0xFF, 0x00, 0x64 }, 0.25f },
            { 1.0f, { 0xF0, 0xF0, 0x00, 0x00 }, 0.0f },
        };

        static const OrientEmitter boomRings = {
            NPAR_TYP_RINGS,
            { 0, 1, 2, 0 },
            { 0.0f, 0.0f, 10.0f },
            { 1.0f, 0.4f },
            { 0.25f, { 0.1f, 0.9f }, 0x20 },
            { 1.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 3.0f, 3.0f, 0.0f, 2.0f, -5.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 8, 1.0f, 2.0f },
        };
    } // namespace FX_Boom

    namespace FX_Par
    {
        OrientCurve curve_data[8] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0xFF }, 0.5f },
            { 0.15f, { 0xFF, 0x00, 0x00, 0xFF }, 0.8f },
            { 0.3f, { 0xFF, 0x00, 0xFF, 0xFF }, 1.1f },
            { 0.45f, { 0x00, 0x00, 0xFF, 0xFF }, 1.3f },
            { 0.6f, { 0x00, 0xFF, 0xFF, 0x7F }, 1.45f },
            { 0.75f, { 0x00, 0xFF, 0x00, 0x7F }, 1.6f },
            { 0.9f, { 0xFF, 0xFF, 0x00, 0x7F }, 1.7f },
            { 1.0f, { 0xF0, 0xF0, 0x64, 0x00 }, 1.75f },
        };

        static const OrientEmitter machRings = {
            NPAR_TYP_MACHRING,
            { 0, 0, 0, 0 },
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20 },
            { 0.25f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 8, 1.0f, 1.0f },
        };
    } // namespace FX_Par

    namespace FX
    {
        static const PlasmaEmitter plasmaSputter = {
            NPAR_TYP_PLASMA,
            0.3f,
            { 0x96, 0x96, 0x96, 0x96 },
            { 0.1f, 0.15f },
            { 0.0f, 0.0f, 0.0f },
            0.0f,
            93,
            0,
            11,
            0,
            { 0.2f, 0.0f, 0.3f, 1.0f, 1.0f },
        };
    } // namespace FX

    namespace Ribbon
    {
        static const xFXRibbon::curve_node curvedata[4] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0xFF }, 1.0f },
            { 0.2f, { 0xFF, 0xFF, 0xFF, 0xFF }, 1.0f },
            { 0.6f, { 0xFF, 0xFF, 0xFF, 0xFF }, 0.4f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0xFF }, 0.0f },
        };

        NMEHelpRib::RibData rib_reflect = {
            { 1, 0, 0, 0 },
            { "fx_rainbow", (xFXRibbon::curve_node*)curvedata, 4, 0.3f, 0.5f },
            { -1, 1.0f, 1.5f },
            { NULL, 0 },
        };
    } // namespace Ribbon
} // namespace Reflect

void Reflect::HAZReflect::ScenePostInit()
{
    Stuff_PostInit();
    Ribbon::rib_reflect.FindTexture();
    zNMEParFX_PartyInvite(NPAR_TYP_PLASMA, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RINGS, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_MACHRING, NULL, 0);
}

en_hazrc Reflect::HAZReflect::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZLob* hazlob;

    haz->flg_hazard |= 0x19280;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    hazlob = &haz->custdata.lobdata;

    hazlob->flg_collide |= 7;
    hazlob->parabinfo = NMEC_ParabReserve();
    if (hazlob->parabinfo == NULL)
    {
        return HAZ_RC_FAILED;
    }

    if (Ribbon::rib_reflect.flags.enable)
    {
        haz->fxdata.ribdata.ribbon = zFXRibbonPoolNew();
    }
    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        Ribbon::rib_reflect.ConfigRibbon(haz->fxdata.ribdata.ribbon);
    }

    haz->tmr_remain = 0.35f;
    hazlob->rad_min = 0.75f;
    hazlob->rad_max = 0.75f;
    hazlob->rad_cur = hazlob->rad_min;
    hazlob->vel = g_Y3;
    // Retail stores -1 to the timer union four times (the PS2 line table puts them on one line);
    // which four union names were used is unknown.
    haz->tmr_generic = haz->tmr_parEmit = haz->tmr_nextEmit = haz->tmr_skipEmit = -1.0f;

    return HAZ_RC_SUCCESS;
}

// No GC or PS2 symbol (inlined into Update); the name is ours. Retail clamps the flight time with
// MIN rather than MAX, so it never exceeds zero and the radius stays at rad_min after launch.
static inline void Reflect_GrowRadius(zNMEHazard* haz)
{
    F32 tym = MIN(0.0f, haz->tym_lifespan - haz->tmr_remain);

    if (tym >= 0.1f)
    {
        haz->custdata.lobdata.rad_cur = haz->custdata.lobdata.rad_max;
    }
    else
    {
        haz->custdata.lobdata.rad_cur =
            LERP(tym / 0.1f, haz->custdata.lobdata.rad_min, haz->custdata.lobdata.rad_max);
    }
}

void Reflect::HAZReflect::Update(zNMEHazard* haz, F32 dt)
{
    S32 weAreHoming = (haz->npc_owner != NULL);

    if (weAreHoming)
    {
        Upd_OwnerHoming(haz, dt);
    }
    else
    {
        Upd_Ballistic(haz, dt);
    }

    Reflect_GrowRadius(haz);

    if (haz->flg_hazard & 0x10000000)
    {
        if (sndTable && haz->sndIDPlayed)
        {
            HAZ_SndStop(haz->sndIDPlayed);
            haz->sndIDPlayed = (iSndHandle)0;
        }

        BoomRings(haz);
        return;
    }

    if (haz->flg_hazard & 8)
    {
        if (weAreHoming)
        {
            haz->flg_hazard &= ~0x1000;
        }

        if (sndTable)
        {
            haz->sndIDPlayed = HAZ_SndPlay(sndTable, haz, HAZ_SND_FLIGHT);
        }
    }

    LobOrientSpiral(haz, 4.0f);

    if (!weAreHoming)
    {
        if (haz->ColTestCapsule(&xglobals->___player_ent_dont_use_directly->bound, &haz->pos_hazard,
                                haz->custdata.lobdata.rad_cur, &haz->custdata.lobdata.vel, dt, 1))
        {
            if (haz->flg_hazard & 0x2000)
            {
                haz->HurtThePlayer();
            }

            // Not in the PS2 DWARF (the PS2 build drops it): retail evaluates the parabola's end
            // velocity into a local that nothing reads (the name is ours).
            xVec3 vel_end;
            xParabolaEvalVel(haz->custdata.lobdata.parabinfo, &vel_end,
                             haz->custdata.lobdata.parabinfo->maxTime);

            haz->flg_hazard |= 0x4;
        }
        else
        {
            haz->StaggeredCollide();
        }
    }

    if (haz->tmr_generic < 0.0f)
    {
        haz->tmr_generic = (gVidMode == eGameVidModePAL) ? 0.02f : (1.0f / 60.0f);
        FX_Par::machRings.EmitRing(&haz->pos_hazard, (xVec3*)&haz->mdl_hazard->Mat->at, -100000.0f,
                                   1.0f, -1);
    }
    haz->tmr_generic = MAX(-1.0f, haz->tmr_generic - dt);

    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        RibbonUpdate(haz, &haz->fxdata.ribdata, dt);
    }
}

// keepFlightTime is const, as in SpawnBall::HAZSpawnBall::Update (retail's separate tmr_remain
// load). tym is not in the DWARF: retail evaluates the elapsed flight time once for both parabola
// lookups.
void Reflect::HAZReflect::Upd_Ballistic(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    xParabola* parab = hazlob->parabinfo;

    if (haz->flg_hazard & 8)
    {
        HAZ_LobLaunchRemain(haz, 0.0f);
    }

    if (haz->flg_hazard & 8)
    {
        const F32 keepFlightTime = haz->tmr_remain;
        haz->PreCollide(0.0f, 0.0f);
        haz->tmr_remain = MIN(haz->tmr_remain, keepFlightTime);
        haz->tym_lifespan = haz->tmr_remain;
    }

    F32 tym = MIN(haz->tym_lifespan, haz->tym_lifespan - haz->tmr_remain);
    xParabolaEvalPos(parab, &haz->pos_hazard, tym);
    xParabolaEvalVel(parab, &hazlob->vel, tym);
}

void Reflect::HAZReflect::Upd_OwnerHoming(zNMEHazard* haz, F32 dt)
{
    HAZLob* hazlob = &haz->custdata.lobdata;
    zNMECommon* npc_tgt = haz->npc_owner;

    xVec3 dir_tgt = *xEntGetCenter(npc_tgt) - *(xVec3*)&haz->mdl_hazard->Mat->pos;
    // not in the DWARF: dst_tgt. Both builds keep the distance to the owner live until the step is
    // clamped to it, and retail computes the step (75 * dt) in its own register.
    F32 dst_tgt = dir_tgt.length();
    if (dst_tgt > 0.00001f)
    {
        dir_tgt /= dst_tgt;
    }
    else
    {
        dir_tgt = globals.oldSkoolCamera.mat.at;
    }

    if (dst_tgt < 0.45f)
    {
        haz->tmr_remain = 0.0f;
        npc_tgt->Damage(DMGTYP_REFLECTED, xglobals->___player_ent_dont_use_directly, &dir_tgt, -1,
                        ePowerupLevelNormal);
        haz->flg_hazard |= 0x4;
        haz->flg_hazard |= 0x10000000;
    }
    else
    {
        haz->LifespanSet(0.5f, 1.0f);
    }

    hazlob->vel = dir_tgt * 75.0f;

    F32 dst_travThisStep = 75.0f * dt;
    if (dst_travThisStep > dst_tgt)
    {
        dst_travThisStep = dst_tgt;
    }

    xVec3 vec_dpos = dir_tgt * dst_travThisStep;
    xVec3 pos_haz = *(xVec3*)&haz->mdl_hazard->Mat->pos + vec_dpos;
    haz->PosSet(&pos_haz);
}

void Reflect::HAZReflect::Render(zNMEHazard* haz)
{
}

// No GC symbol (inlined into Update); out of line in the PS2 build.
void Reflect::HAZReflect::BoomRings(const zNMEHazard* haz)
{
    xVec3 pos_emit = *(xVec3*)&haz->mdl_hazard->Mat->pos;
    xVec3 dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->right;

    FX_Boom::boomRings.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);
    dir_emit.invert();
    FX_Boom::boomRings.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);

    dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->up;
    FX_Boom::boomRings.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);
    dir_emit.invert();
    FX_Boom::boomRings.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);

    dir_emit = *(xVec3*)&haz->mdl_hazard->Mat->at;
    FX_Boom::boomRings.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);
    dir_emit.invert();
    FX_Boom::boomRings.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1);
}

void Reflect::HAZReflect::CollideResponse(zNMEHazard* haz, S32 flg_hazcolf,
                                          const xSweptSphere* swdata, F32 tym_inFuture)
{
    xEnt* ent_hit;

    if (!(flg_hazcolf & 0x18))
    {
        Stuff_CollRespDflt(haz, flg_hazcolf, swdata, tym_inFuture);

        if (flg_hazcolf & 3)
        {
            ent_hit = (xEnt*)swdata->optr;
            if (ent_hit != NULL)
            {
                F32 hitArgs[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                zEntEvent(haz->npc_owner, 0, ent_hit, eEventHit, hitArgs, NULL, 0, FE_NO);
            }
        }
        else
        {
            zNMECommon* npc = (zNMECommon*)swdata->optr;
            xVec3 dir = swdata->worldNormal;
            dir.invert();
            npc->Damage(DMGTYP_REFLECTED, xglobals->___player_ent_dont_use_directly, &dir, -1,
                        ePowerupLevelNormal);
        }

        haz->custdata.collide.flg_collide = 0;
    }
}

// No GC symbol (inlined into Update); out of line in the PS2 build. rib is const (the DWARF does
// not record const on scalar locals): retail colours it as a separate copy of the loaded member.
void Reflect::HAZReflect::RibbonUpdate(const zNMEHazard* haz, HAZFXRibbon* ribdata, F32 dt)
{
    xFXRibbon* const rib = ribdata->ribbon;
    NMEHelpRib::RibData* ribhelp;

    ribdata->cnt_skipRibInsert--;
    if (ribdata->cnt_skipRibInsert > 0)
    {
        return;
    }

    ribhelp = &Ribbon::rib_reflect;
    ribdata->cnt_skipRibInsert = ribhelp->updinfo.frq_ribJoint;
    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(rib);
    }

    rib->insert(haz->pos_hazard, *(xVec3*)&haz->mdl_hazard->Mat->up,
                haz->custdata.lobdata.rad_cur * ribhelp->updinfo.scl_master,
                ribhelp->updinfo.alf_master, 0);
}

namespace PlyrBang
{
    namespace Initialize
    {
        static const xVec3 vec_rotate = { PI, 0.25f * PI, 0.25f * PI };
    } // namespace Initialize

    namespace Render
    {
        static const xVec3 vec_scale = { 1.0f, 1.0f, 1.0f };
    } // namespace Render

    namespace Ribbon
    {
        static const xFXRibbon::curve_node curvedata[4] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x20 }, 1.0f },
            { 0.2f, { 0xFF, 0xFF, 0xFF, 0x5A }, 1.5f },
            { 0.6f, { 0xFF, 0xFF, 0xFF, 0x3C }, 0.4f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 0.0f },
        };

        NMEHelpRib::RibData rib_plyrbang = {
            { 0, 0, 0, 0 },
            { "particle_cloud", (xFXRibbon::curve_node*)curvedata, 4, 0.25f, 0.2f },
            { 1, 1.0f, 1.26f },
            { NULL, 0 },
        };
    } // namespace Ribbon
} // namespace PlyrBang

void PlyrBang::HAZPlyrBang::ScenePostInit()
{
    Stuff_PostInit();
    Ribbon::rib_plyrbang.FindTexture();
}

en_hazrc PlyrBang::HAZPlyrBang::Configure(zNMEHazard* haz)
{
    S32 rc;
    HAZBang* hazbang;

    haz->flg_hazard |= 0x8380;

    rc = haz->GrabModel(typ_hazard);
    if (!rc)
    {
        return HAZ_RC_FAILED;
    }

    if (rast_shadow != NULL)
    {
        haz->shadCacheSimple = NMEC_SimpShadowCacheReserve();
        if (haz->shadCacheSimple != NULL)
        {
            xShadowSimple_CacheInit_Expert(haz->shadCacheSimple, rast_shadow, 0);
            haz->shadCacheSimple->flags |= 0x8;
            haz->shadCacheSimple->flags &= 0xFD;
        }
        haz->cnt_nextFill = -1;
    }

    if (Ribbon::rib_plyrbang.flags.enable)
    {
        haz->fxdata.ribdata.ribbon = zFXRibbonPoolNew();
    }

    if (haz->fxdata.ribdata.ribbon != NULL)
    {
        Ribbon::rib_plyrbang.ConfigRibbon(haz->fxdata.ribdata.ribbon);
    }

    haz->tmr_remain = 5.0f;

    hazbang = &haz->custdata.bang;
    hazbang->rad_min = 2.25f;
    hazbang->rad_max = 2.25f;
    hazbang->rad_cur = hazbang->rad_min;
    hazbang->vel = g_Y3;
    hazbang->state = 0;

    return HAZ_RC_SUCCESS;
}

void PlyrBang::HAZPlyrBang::Update(zNMEHazard* haz, F32 dt)
{
    HAZBang* hazbang = &haz->custdata.bang;

    switch (hazbang->state)
    {
    case 0:
        zEntPlayerGauntletBlastRadius(0.0f);
        if (hazbang->ent != NULL)
        {
            xVec3 pos;
            xMat4x3Toworld(&pos, (xMat4x3*)hazbang->ent->model->Mat, &hazbang->offset);
            haz->PosSet(&pos);

            xMat3x3 tmp_descaled;
            F32 dummy;
            xVec3NormalizeMacro(&tmp_descaled.right, &hazbang->ent->model->Mat->right, &dummy);
            xVec3NormalizeMacro(&tmp_descaled.up, &hazbang->ent->model->Mat->up, &dummy);
            xVec3NormalizeMacro(&tmp_descaled.at, &hazbang->ent->model->Mat->at, &dummy);

            xVec3 targetEuler;
            xMat3x3GetEuler(&tmp_descaled, &targetEuler);

            xVec3 ypr;
            xVec3Sub(&ypr, &targetEuler, &hazbang->targetOffsetRot);
            xVec3AddTo(&ypr, &hazbang->offsetRot);

            xMat3x3 rotMat;
            xMat3x3Euler(&rotMat, &ypr);
            xMat3x3Copy((xMat3x3*)haz->mdl_hazard->Mat, &rotMat);

            if (haz->tmr_remain < 3.0f && haz->tmr_remain > 0.0f)
            {
                F32 factor = 2.5f / haz->tmr_remain;
                if (haz->mdl_hazard->RedMultiplier < 0.4f)
                {
                    zEntPlayerFX::GauntletExplosion::SetSparks(false);
                    haz->mdl_hazard->RedMultiplier = 1.0f;
                    xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("SB_BASH2_TICK_GRP")), 0, NULL, NULL,
                                NULL, NULL, NULL);
                }
                else
                {
                    haz->mdl_hazard->RedMultiplier -= factor * dt;
                }
                haz->mdl_hazard->BlueMultiplier = 0.0f;
                haz->mdl_hazard->GreenMultiplier = 0.0f;
                zEntPlayerFX::GauntletExplosion::Sparks(*(xVec3*)&haz->mdl_hazard->Mat->pos);
            }

            if (haz->tmr_remain <= 0.05f)
            {
                haz->mdl_hazard->Alpha = 0.0f;
                zEntPlayerFX::GauntletExplosion::Explode(*(xVec3*)&haz->mdl_hazard->Mat->pos);
                haz->flg_hazard |= 0x40;
            }
        }
        break;
    case 1:
        if (haz->tmr_remain <= 0.0f)
        {
            haz->flg_hazard |= 0x4;
            zEntPlayerKillGauntlet();
            haz->mdl_hazard->RedMultiplier = 1.0f;
            haz->mdl_hazard->BlueMultiplier = 1.0f;
            haz->mdl_hazard->GreenMultiplier = 1.0f;
            return;
        }
        zEntPlayerGauntletBlastRadius(globals.player.g.BBashGauntletBlastRadius);
        break;
    }

    if (haz->tmr_remain <= 0.0f)
    {
        haz->tmr_remain = 0.5f;
        hazbang->state = 1;
    }
    else if (haz->fxdata.ribdata.ribbon != NULL)
    {
        RibbonUpdate(haz, &haz->fxdata.ribdata, dt);
    }
}

void PlyrBang::HAZPlyrBang::Render(zNMEHazard* haz)
{
    Stuff_Render(haz, -1.0f);
}

void HAZ_RegisterTypes()
{
    static Generic::HAZGeneric generic;
    static Exist::HAZExist dazed;
    static Explode::HAZExplode explode;
    static Impact::HAZImpact impact;
    static Impact::HAZImpact impactwet;
    static Impact::HAZImpact impactboom;
    static SpawnBall::HAZSpawnBall spawnball_xx;
    static SpawnBall::HAZSpawnBall spawnball_gg;
    static SpawnBall::HAZSpawnBall spawnball_tr;
    static SpawnBall::HAZSpawnBall spawnball_pt;
    static SpawnGrow::HAZSpawnGrow spawngrow;
    static BuckOBoom::HAZBuckOBoom buckoboom;
    static TikiBoom::HAZTikiBoom tikiboom;
    static SlamWaveSM::HAZSlamWaveSM slamwave_v1;
    static SlamWaveMD::HAZSlamWaveMD slamwave_v2;
    static SlamWaveLG::HAZSlamWaveLG slamwave_v3;
    static TurBall::HAZTurBall turret_v1;
    static TurBall::HAZTurBall turret_v2;
    static TurBall::HAZTurBall turret_v3;
    static TurBoom::HAZTurBoom turboom;
    static FlingBlob::HAZFlingBlob fling_v1;
    static FlingBlob::HAZFlingBlob fling_v2;
    static FlingBlob::HAZFlingBlob fling_v3;
    static FrogFishBlob::HAZFrogFishBlob fling_ff;
    static Puddle::HAZPuddle puddle_v1;
    static Puddle::HAZPuddle puddle_v2;
    static Puddle::HAZPuddle puddle_v3;
    static IceCreamSplat::HAZIceCreamSplat icecreamsplat;
    static Exist::HAZExist spitsteam;
    static Exist::HAZExist spitsplash;
    static PopRock::HAZPopRock popper_v1;
    static PopRock::HAZPopRock popper_v2;
    static PopRock::HAZPopRock popper_v3;
    static MervBomb::HAZMervBomb mervbomb_v1;
    static MervBomb::HAZMervBomb mervbomb_v2;
    static MervBomb::HAZMervBomb mervbomb_v3;
    static Mervlet::HAZMervlet mervlet_v1;
    static Mervlet::HAZMervlet mervlet_v2;
    static Mervlet::HAZMervlet mervlet_v3;
    static ZapBall::HAZZapBall zapball_zapper;
    static ZapBall::HAZZapBall zapball_mervyn;
    static ZapBoom::HAZZapBoom zapboom_zapper;
    static ZapBoom::HAZZapBoom zapboom_mervyn;
    static GolfBall::HAZGolfBall golfball;
    static GolfBoom::HAZGolfBoom golfboom;
    static GolfJunk::HAZGolfJunk golfjunk_a;
    static GolfJunk::HAZGolfJunk golfjunk_b;
    static GolfJunk::HAZGolfJunk golfjunk_c;
    static GolfJunk::HAZGolfJunk golfjunk_d;
    static GolfJunk::HAZGolfJunk golfjunk_e;
    static NyphToss::HAZNyphToss denknife;
    static NyphBoom::HAZNyphBoom nyphboom;
    static TuneBorn::HAZTuneBorn tuneborn;
    static TuneBang::HAZTuneBang tunebang;
    static TuneBoom::HAZTuneBoom tuneboom;
    static TuneFlyt::HAZTuneFlyt tuneflyt;
    static BowWave::HAZBowWave swimwake;
    static BowWave::HAZBowWave bowwave;
    static WaveRing::HAZWaveRing splash;
    static WaveRing::HAZWaveRing viswave;
    static Droplet::HAZDroplet splish;
    static Droplet::HAZDroplet visdrop;
    static WaveRing::HAZWaveRing viswater;
    static WaveRing::HAZWaveRing vischoc;
    static WaveRing::HAZWaveRing vislava;
    static WaveRing::HAZWaveRing visbile;
    static WaveRing::HAZWaveRing visoil;
    static WaveRing::HAZWaveRing vissauce;
    static WaveRing::HAZWaveRing vistoxic;
    static Reflect::HAZReflect reflect_v1;
    static PlyrBang::HAZPlyrBang plyrbang_v1;
    static MachoBowlExplosion::HAZMachoBowlExplosion machoBowlExplosionV1;
    HAZTypeRecord hazard_types[] = {
        { HAZ_TYP_UNKNOWN, &generic },
        { HAZ_TYP_EXPLODE, &explode },
        { HAZ_TYP_IMPACT, &impact },
        { HAZ_TYP_IMPACTWET, &impactwet },
        { HAZ_TYP_IMPACTBOOM, &impactboom },
        { HAZ_TYP_DAZED, &dazed },
        { HAZ_TYP_SPAWNBALL_XX, &spawnball_xx },
        { HAZ_TYP_SPAWNBALL_GG, &spawnball_gg },
        { HAZ_TYP_SPAWNBALL_TR, &spawnball_tr },
        { HAZ_TYP_SPAWNBALL_PT, &spawnball_pt },
        { HAZ_TYP_SPAWNGROW, &spawngrow },
        { HAZ_TYP_BUCKOBOOM_V1, &buckoboom },
        { HAZ_TYP_TIKIBOOM, &tikiboom },
        { HAZ_TYP_SLAMWAVE_V1, &slamwave_v1 },
        { HAZ_TYP_SLAMWAVE_V2, &slamwave_v2 },
        { HAZ_TYP_SLAMWAVE_V3, &slamwave_v3 },
        { HAZ_TYP_FLING_V1, &fling_v1 },
        { HAZ_TYP_FLING_V2, &fling_v2 },
        { HAZ_TYP_FLING_V3, &fling_v3 },
        { HAZ_TYP_FLING_FROGFISH, &fling_ff },
        { HAZ_TYP_SPITPUDDLE_V1, &puddle_v1 },
        { HAZ_TYP_SPITPUDDLE_V2, &puddle_v2 },
        { HAZ_TYP_SPITPUDDLE_V3, &puddle_v3 },
        { HAZ_TYP_ICECREAMSPLAT, &icecreamsplat },
        { HAZ_TYP_SPITSTEAM, &spitsteam },
        { HAZ_TYP_SPITSPLASH, &spitsplash },
        { HAZ_TYP_POPPER_V1, &popper_v1 },
        { HAZ_TYP_POPPER_V2, &popper_v2 },
        { HAZ_TYP_POPPER_V3, &popper_v3 },
        { HAZ_TYP_MERVBOMB_V1, &mervbomb_v1 },
        { HAZ_TYP_MERVBOMB_V2, &mervbomb_v2 },
        { HAZ_TYP_MERVBOMB_V3, &mervbomb_v3 },
        { HAZ_TYP_MERVLET_V1, &mervlet_v1 },
        { HAZ_TYP_MERVLET_V2, &mervlet_v2 },
        { HAZ_TYP_MERVLET_V3, &mervlet_v3 },
        { HAZ_TYP_ZAPBALL_V1, &zapball_zapper },
        { HAZ_TYP_ZAPBALL_V2, &zapball_mervyn },
        { HAZ_TYP_ZAPBOOM_V1, &zapboom_zapper },
        { HAZ_TYP_ZAPBOOM_V2, &zapboom_mervyn },
        { HAZ_TYP_TURBALL_V1, &turret_v1 },
        { HAZ_TYP_TURBALL_V2, &turret_v2 },
        { HAZ_TYP_TURBALL_V3, &turret_v3 },
        { HAZ_TYP_TURBOOM, &turboom },
        { HAZ_TYP_GOLFBALL, &golfball },
        { HAZ_TYP_GOLFBOOM, &golfboom },
        { HAZ_TYP_GOLFJUNK_A, &golfjunk_a },
        { HAZ_TYP_GOLFJUNK_B, &golfjunk_b },
        { HAZ_TYP_GOLFJUNK_C, &golfjunk_c },
        { HAZ_TYP_GOLFJUNK_D, &golfjunk_d },
        { HAZ_TYP_GOLFJUNK_E, &golfjunk_e },
        { HAZ_TYP_DENNIS_KNIFE, &denknife },
        { HAZ_TYP_DENNIS_KNIFEBOOM, &nyphboom },
        { HAZ_TYP_TUNEBORN, &tuneborn },
        { HAZ_TYP_TUNEBANG, &tunebang },
        { HAZ_TYP_TUNEBOOM, &tuneboom },
        { HAZ_TYP_TUNEFLYT, &tuneflyt },
        { HAZ_TYP_SWIMWAKE, &swimwake },
        { HAZ_TYP_BOWWAVE, &bowwave },
        { HAZ_TYP_SPLASH_RING, &splash },
        { HAZ_TYP_SPLISH_DROP, &splish },
        { HAZ_TYP_VISSPLASH_WAVE, &viswave },
        { HAZ_TYP_VISSPLASH_DROP, &visdrop },
        { HAZ_TYP_VISSPLASH_WATER, &viswater },
        { HAZ_TYP_VISSPLASH_CHOC, &vischoc },
        { HAZ_TYP_VISSPLASH_LAVA, &vislava },
        { HAZ_TYP_VISSPLASH_BILE, &visbile },
        { HAZ_TYP_VISSPLASH_OIL, &visoil },
        { HAZ_TYP_VISSPLASH_SAUCE, &vissauce },
        { HAZ_TYP_VISSPLASH_TOXIC, &vistoxic },
        { HAZ_TYP_REFLECT_V1, &reflect_v1 },
        { HAZ_TYP_PLYRBANG_V1, &plyrbang_v1 },
        { HAZ_TYP_POWERUP_EXPLOSION_V1, &machoBowlExplosionV1 },
        { HAZ_TYP_NOMORE, NULL },
    };

    for (HAZTypeRecord* rec = hazard_types; rec->typ_haz != HAZ_TYP_NOMORE; rec++)
    {
        GlobalData::haztypeHandlers[rec->cb_haz->typ_hazard = rec->typ_haz] = rec->cb_haz;
    }
}

void HAZ_PostInit_Shrapnel()
{
    for (S32 i = 0; i < HAZ_FRAG_NOMORE; i++)
    {
        g_data_hazshrap[i] = NULL;
        if (g_strz_hazshrap[i] != NULL && g_strz_hazshrap[i][0] != '\0')
        {
            g_data_hazshrap[i] = (zShrapnelAsset*)NMEC_FindAsset(xStrHash(g_strz_hazshrap[i]));
        }
    }
}

void HAZ_TypeTweaks(const char* prefix_root)
{
    char prefix[128];
    char prefix_base[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "\003Globals TypeImp");
    sprintf(prefix, "%s|", prefix_base);
}

void HAZ_PostInit_Sounds()
{
    for (S32 i = 0; i < HAZ_TYP_NOMORE; i++)
    {
        HAZSndTable* sndTable = g_hazardSoundTable[i];
        if (sndTable != NULL && sndTable->grpEntry[0].nam_sndGrp != NULL &&
            sndTable->grpEntry[0].nam_sndGrp[0] != '\0')
        {
            sndTable->grpEntry[0].sndGrpID =
                xSndMgrGetSoundGroup(xStrHash(sndTable->grpEntry[0].nam_sndGrp));
        }
    }
}
