#include "zNMEZapGun.h"
#include "zNMETypeStdMervyn.h"
#include "zNMETypeStdZapper.h"
#include "zNMESupport.h"
#include "zNMEParFX.h"
#include "zFXRibbonPool.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "xSnd.h"

#include <math.h>

namespace ZapGun
{
    namespace ZapperParm
    {
        static const xVec3 vec_offset = { 0.0f, 0.0f, 1.0f };
    } // namespace ZapperParm

    namespace MervynParm
    {
        static const xVec3 vec_offset = { 0.0f, 0.0f, 1.4f };
    } // namespace MervynParm

    namespace Idle
    {
        static const PlasmaEmitter plasmaSputter = {
            NPAR_TYP_PLASMA,
            0.55f,
            { 0x00, 0xC0, 0xFF, 0xFF },
            { 0.1f, 0.15f },
            { 0.0f, 0.0f, 0.0f },
            0.0f,
            93,
            0,
            5,
            0,
            { 0.2f, 0.0f, 0.1f, 4.0f, 2.0f },
        };
    } // namespace Idle

    namespace Warn
    {
        static const PlasmaEmitter plasmaWarning = {
            NPAR_TYP_PLASMA,
            0.35f,
            { 0x00, 0xC0, 0xFF, 0xFF },
            { 0.1f, 0.15f },
            { 0.0f, 0.0f, 0.0f },
            0.0f,
            98,
            0,
            5,
            0,
            { -4.0f, 0.0f, 0.15f, 0.5f, 0.0f },
        };
        static const F32 pct_spdRamp[2] = { 1.0f, 1.0f };
        static const S32 num_emitRamp[2] = { 2, 6 };
    } // namespace Warn

    namespace Dare
    {
        static const PlasmaEmitter plasmaDaring = {
            NPAR_TYP_PLASMA,
            0.55f,
            { 0xFF, 0xC0, 0x9B, 0xFF },
            { 0.1f, 0.15f },
            { 0.0f, 0.0f, 0.0f },
            0.0f,
            85,
            0,
            7,
            0,
            { -3.75f, 0.0f, 0.7f, -2.5f, 0.0f },
        };
    } // namespace Dare

    namespace Hate
    {
        static const PlasmaEmitter plasmaAngry = {
            NPAR_TYP_MUZZLE,
            0.2f,
            { 0x00, 0xC0, 0xFF, 0xFF },
            { 0.15f, 1.5f },
            { 0.0f, 0.0f, 0.0f },
            25.0f,
            93,
            0,
            1,
            0,
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        };

        static const xFXRibbon::curve_node curvedata[4] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0xFF }, 1.0f },
            { 0.35f, { 0xFF, 0x46, 0x46, 0xC8 }, 0.9f },
            { 0.8f, { 0xFF, 0x8C, 0x46, 0x96 }, 0.3f },
            { 1.0f, { 0xFF, 0xFF, 0x00, 0x32 }, 0.0f },
        };

        NMEHelpRib::RibData rib_revolve = {
            { 1, 1, 0, 0 },
            { "lightning", (xFXRibbon::curve_node*)curvedata, 4, 0.35f, 0.6f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

        static const F32 pct_spdRamp[2] = { 0.5f, 1.5f };
    } // namespace Hate

    namespace Java
    {
        static const PlasmaEmitter plasmaErupt = {
            NPAR_TYP_PLASMA,
            0.4f,
            { 0x00, 0xC0, 0xFF, 0xFF },
            { 0.0f, 0.75f },
            { 0.0f, 0.0f, 0.0f },
            0.0f,
            70,
            0,
            1,
            0,
            { 4.0f, 3.0f, 0.1f, 2.0f, 2.0f },
        };
    } // namespace Java

    namespace LaserData
    {
        static S32 needuvincr;
        static const xColor_tag rgba_beg = { 0xFF, 0xFF, 0x00, 0x64 };
        static const xColor_tag rgba_end = { 0xFF, 0x00, 0x00, 0x37 };
    } // namespace LaserData

    namespace LaserPulser
    {
        static S32 needuvincr;
        static const xColor_tag rgba_beg = { 0x00, 0x00, 0xC8, 0xFF };
        static const xColor_tag rgba_end = { 0x00, 0xC8, 0xC8, 0xFF };
    } // namespace LaserPulser

    namespace LaserRepel
    {
        static S32 needuvincr;
        static const xColor_tag rgba_beg = { 0x00, 0x00, 0x00, 0x9B };
        static const xColor_tag rgba_end = { 0x96, 0x00, 0x96, 0x9B };
    } // namespace LaserRepel

    NMELaser NMEZapomatic::laser_target;
    NMELaser NMEZapomatic::laser_pulser;
    NMELaser NMEZapomatic::laser_repel;
} // namespace ZapGun

namespace ZapGun
{

    inline F32 NMEZapomatic::DangerZoneRatio()
    {
        xVec3 pos_tgt = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        F32 r2_near;
        F32 r2_far;
        F32 rat = 0.0f;

        if (IsMervyn())
        {
            zNMEMervyn* merv = (zNMEMervyn*)npc_owner;
            zEntPlayer_PredictPos(&pos_tgt, 1.0f, 1.0f, 1);
            r2_near = SQ(merv->cfg_mervyn->ranges.rad_zap);
            r2_far = SQ(merv->cfg_mervyn->ranges.rad_zapgrey);
        }
        else
        {
            zNMEZapper* zap = (zNMEZapper*)npc_owner;
            zEntPlayer_PredictPos(&pos_tgt, 1.0f, 1.0f, 1);
            r2_near = SQ(zap->cfg_zapper->ranges.rad_zap);
            r2_far = SQ(zap->cfg_zapper->ranges.rad_zapgrey);
        }

        F32 dst2 = npc_owner->XZDstSqToPos(&pos_tgt, NULL, NULL);

        if (r2_far < r2_near)
        {
            return rat;
        }
        if (r2_far < 0.001f)
        {
            return rat;
        }
        if (dst2 < 0.25f)
        {
            return rat;
        }

        dst2 -= r2_near;
        if (dst2 < 0.0f)
        {
            return rat;
        }

        rat = dst2 / (r2_far - r2_near);
        if (rat > 1.0f)
        {
            return 1.0f;
        }
        return rat;
    }

    void NMEZapomatic::Setup()
    {
        Hate::rib_revolve.FindTexture();
        LaserSetup();
    }

    void NMEZapomatic::Reset()
    {
        ModeSet(ZAP_MODE_STARTUP, 0);
        tmr_cycle = 0.0f;
    }

    void NMEZapomatic::ModeSwitch(en_zapmod mode)
    {
        switch (zapmode)
        {
        case ZAP_MODE_SHOOT:
        {
            if (ribbon != NULL)
            {
                zFXRibbonPoolDelete(ribbon);
            }
            ribbon = NULL;
            tmr_warmup = tmr_megaprog = -1.0f;

            zNMECommon* npc = npc_owner;
            if (npc->snd_table->sndGroupHandle[8] != -1)
            {
                xSndMgrStop(npc->sndID[8]);
                npc->sndID[8] = (iSndHandle)-1;
            }
            break;
        }
        }

        cnt_nextEmit = -1;
        cnt_siphonEmit = -1;

        switch (mode)
        {
        case ZAP_MODE_SHOOT:
            Shoot_Prep();
            break;
        }

        zapmode = mode;
    }

    // No symbol (inlined into zMODEnemy::PostTimestep and dead-stripped); the name is ours. Scrolls
    // the textures of the lasers drawn since the last frame. Above NewTimeUpdate for literal order.
    void NMEZapomatic::LaserUVScroll(F32 dt)
    {
        if (LaserData::needuvincr)
        {
            LaserData::needuvincr = 0;
            laser_target.UVScrollUpdate(dt);
        }
        if (LaserPulser::needuvincr)
        {
            LaserPulser::needuvincr = 0;
            laser_pulser.UVScrollUpdate(dt);
        }
        if (LaserRepel::needuvincr)
        {
            LaserRepel::needuvincr = 0;
            laser_repel.UVScrollUpdate(dt);
        }
    }

    void NMEZapomatic::NewTimeUpdate(F32 dt)
    {
        if (npc_owner == NULL)
        {
            return;
        }

        if (tmr_cycle < 0.0f)
        {
            tmr_cycle = 0.0f;
        }
        tmr_cycle += dt;
        if (tmr_cycle > 1.0f)
        {
            tmr_cycle = fmod(tmr_cycle, 1.0);
        }

        dir_aim = xModelGetFrame(npc_owner->model)->at;

        switch (zapmode)
        {
        case ZAP_MODE_NOTHING:
            break;
        case ZAP_MODE_IDLE:
            Idle_Upd(dt);
            break;
        case ZAP_MODE_WARN:
            Warn_Upd(dt);
            break;
        case ZAP_MODE_SHOOT:
            Shoot_Upd(dt);
            break;
        case ZAP_MODE_REPEL:
            Repel_Upd(dt);
            break;
        }

        if (flg_repelBowlBall)
        {
            Repel_Upd(dt);
        }
    }

    void NMEZapomatic::Render()
    {
        LaserRender();
    }

    void NMEZapomatic::CacheUsefulPos()
    {
        CalcNewGunTip(&pos_gunTip);

        if (IsMervyn())
        {
            zEntPlayer_PredictPos(&pos_aimPoint, 1.0f / 1.8f, 1.0f, 1);
        }
        else
        {
            zEntPlayer_PredictPos(&pos_aimPoint, 1.0f / 7.5f, 1.0f, 1);
        }

        xEnt* plyr = xglobals->___player_ent_dont_use_directly;
        pos_aimPoint.y += (*xEntGetCenter(plyr) - *xEntGetPos(plyr)).y;
    }

    void NMEZapomatic::CalcNewGunTip(xVec3* pos_tip)
    {
        zNMECommon* npc = npc_owner;
        xMat4x3* mat_root = (xMat4x3*)npc->model->Mat;
        S32 idx;
        xVec3 vec_offset;

        if (IsMervyn())
        {
            idx = MervynParm::idx_gunBone;
            vec_offset = MervynParm::vec_offset;
        }
        else
        {
            idx = ZapperParm::idx_gunBone;
            vec_offset = ZapperParm::vec_offset;
        }

        xMat4x3* mat_bone = &mat_root[idx];
        xVec3 pos_gun = vec_offset;
        xMat4x3Toworld(&pos_gun, mat_bone, &pos_gun);
        xMat4x3Toworld(&pos_gun, mat_root, &pos_gun);
        *pos_tip = pos_gun;
    }

    void NMEZapomatic::Idle_Upd(F32 dt)
    {
        AimGunBone();
        CacheUsefulPos();
    }

    void NMEZapomatic::Warn_Upd(F32 dt)
    {
        AimGunBone();
        CacheUsefulPos();
        Warn_ParFX(dt);
    }

    // Compiled-out asserts (conditions unknown). With them the function is too big to be
    // auto-inlined; retail calls it from Warn_Upd and Repel_Upd.
    void NMEZapomatic::Warn_ParFX(F32 dt)
    {
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        if (cnt_nextEmit >= 0)
        {
            cnt_nextEmit--;
            return;
        }

        cnt_nextEmit = 1;

        F32 pct = 1.0f - DangerZoneRatio();
        S32 num_emit = Warn::num_emitRamp[0] +
                       (S32)(pct * (F32)(Warn::num_emitRamp[1] - Warn::num_emitRamp[0]));
        F32 pct_spd = pct * (Warn::pct_spdRamp[1] - Warn::pct_spdRamp[0]) + Warn::pct_spdRamp[0];
        Warn::plasmaWarning.ArcWelder(&pos_gunTip, &dir_aim, pct_spd, num_emit);
    }

    void NMEZapomatic::Repel_Upd(F32 dt)
    {
        AimGunBone();
        CacheUsefulPos();
        LaserUpdate();
        Warn_ParFX(dt);
    }

    void NMEZapomatic::Shoot_Prep()
    {
        NMEHelpRib::RibData* ribhelp = &Hate::rib_revolve;

        ribbon = zFXRibbonPoolNew();
        if (ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribbon);
        }

        zapatak = ZAP_ATAK_WARMUP;
        idx_plyrBone = 1;
        energized = 0;
        tmr_warmup = 1.5f;
        tmr_megaprog = 0.35f;

        npc_owner->SndPlayFromTable(8); // eNMESound_MervZapperLaserL
    }

    void NMEZapomatic::Shoot_Upd(F32 dt)
    {
        AimGunBone();
        CacheUsefulPos();

        switch (zapatak)
        {
        case ZAP_ATAK_WARMUP:
            LaserUpdate();

            Dare_ParFX(dt);

            tmr_warmup = MAX(-1.0f, tmr_warmup - dt);

            if (npc_owner->XZDstSqToPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL,
                                        NULL) < 20.25f)
            {
                tmr_warmup = -1.0f;
            }

            if (xnegative(tmr_warmup))
            {
                zapatak = ZAP_ATAK_FULLON;
                zNMECommon* npc = npc_owner;
                npc->SndPlayFromTable(9); // eNMESound_MervZapperBlast
            }
            else if (energized == 0 && tmr_warmup < 0.5f)
            {
                energized = 1;
                zNMECommon* npc = npc_owner;
                npc->SndPlayFromTable(7); // eNMESound_MervZapperEnergize
            }
            break;
        case ZAP_ATAK_FULLON:
            LaserUpdate();

            Hate_ParFX(dt);

            tmr_megaprog = MAX(-1.0f, tmr_megaprog - dt);

            if (npc_owner->XZDstSqToPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL,
                                        NULL) > 25.0f &&
                tmr_megaprog < 0.0f)
            {
                zapatak = ZAP_ATAK_WARMUP;
                idx_plyrBone = 1;
                energized = 0;
                tmr_warmup = 1.5f;
                tmr_megaprog = 0.35f;
            }
            break;
        }
    }

    void NMEZapomatic::Dare_ParFX(F32 dt)
    {
        if (cnt_siphonEmit >= 0)
        {
            cnt_siphonEmit--;
        }
        else
        {
            cnt_siphonEmit = 1;
            Dare::plasmaDaring.ArcWelder(&pos_gunTip, &dir_aim, 1.0f, 4);
        }
    }

    void NMEZapomatic::Hate_ParFX(F32 dt)
    {
        if (cnt_nextEmit >= 0)
        {
            cnt_nextEmit--;
        }
        else
        {
            cnt_nextEmit = 1;
            F32 pct = icos(tmr_cycle * (2.0f * PI) + -PI);
            Hate::plasmaAngry.ArcWelder(
                &pos_gunTip, &dir_aim,
                pct * (Hate::pct_spdRamp[1] - Hate::pct_spdRamp[0]) + Hate::pct_spdRamp[0], -1);
        }
    }

    // Placeholder for dead-stripped code: retail keeps these literals between Hate_ParFX and
    // LaserSetup, but no surviving function uses them. The bodies are unknown.
    static void __deadstripped_zNMEZapGun(F32* out)
    {
        out[0] = 5.0f;
        out[1] = 1.5f;
        out[2] = 2.0f;
        out[3] = 1.0f / 1.8f;
        out[4] = 1.0f / 7.5f;
    }

    // Placeholder for a dead-stripped zapball spawner: retail emits the weak zNMEHazardType
    // virtuals here, so a stripped ZapGun function made a virtual Configure call, and the zapball
    // hazards have handlers but no surviving spawner. Only acquire and configure are known.
    static S32 __deadstripped2_zNMEZapGun()
    {
        zNMEHazard* haz = HAZ_Acquire();
        if (haz == NULL)
        {
            return 0;
        }
        return haz->ConfigHelper(HAZ_TYP_ZAPBALL_V1);
    }

    void NMEZapomatic::LaserSetup()
    {
        LaserApplyConfig();
    }

    // The compiled-out asserts in the inlined LaserApplyConfig_<laser> helpers make this too big
    // to be auto-inlined; retail calls it from LaserSetup.
    void NMEZapomatic::LaserApplyConfig()
    {
        if (laser_target.rast_laser == NULL)
        {
            LaserApplyConfig_laser_target();
        }
        if (laser_pulser.rast_laser == NULL)
        {
            LaserApplyConfig_laser_pulser();
        }
        if (laser_repel.rast_laser == NULL)
        {
            LaserApplyConfig_laser_repel();
        }
    }

    // No GC symbol (inlined into LaserApplyConfig); the PS2 demo has it as a static member. The
    // placeholders stand for compiled-out asserts (conditions unknown).
    void NMEZapomatic::LaserApplyConfig_laser_target()
    {
        char* str_zapball = "fx_solid";

        // not in the DWARF: holds the raster from the lookup to its store after flg_render
        RwRaster* raster = NMEC_FindRWRaster(str_zapball);

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        laser_target.flg_render = 3;
        laser_target.rast_laser = raster;
        laser_target.uv_scroll[0] = 0.1f;
        laser_target.uv_scroll[1] = 0.0f;
        laser_target.radius[0] = 0.025f;
        laser_target.radius[1] = 0.01f;
        laser_target.rgba[0].red = LaserData::rgba_beg.r;
        laser_target.rgba[0].green = LaserData::rgba_beg.g;
        laser_target.rgba[0].blue = LaserData::rgba_beg.b;
        laser_target.rgba[0].alpha = LaserData::rgba_beg.a;
        laser_target.rgba[1].red = LaserData::rgba_end.r;
        laser_target.rgba[1].green = LaserData::rgba_end.g;
        laser_target.rgba[1].blue = LaserData::rgba_end.b;
        laser_target.rgba[1].alpha = LaserData::rgba_end.a;
    }

    // No GC symbol (inlined into LaserApplyConfig); the PS2 demo has it as a static member.
    void NMEZapomatic::LaserApplyConfig_laser_pulser()
    {
        char* str_zapball = "fx_solid";

        // not in the DWARF: holds the raster from the lookup to its store after flg_render
        RwRaster* raster = NMEC_FindRWRaster(str_zapball);

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        laser_pulser.flg_render = 3;
        laser_pulser.rast_laser = raster;
        laser_pulser.uv_scroll[0] = 0.1f;
        laser_pulser.uv_scroll[1] = 0.0f;
        laser_pulser.radius[0] = 0.05f;
        laser_pulser.radius[1] = 0.2f;
        laser_pulser.rgba[0].red = LaserPulser::rgba_beg.r;
        laser_pulser.rgba[0].green = LaserPulser::rgba_beg.g;
        laser_pulser.rgba[0].blue = LaserPulser::rgba_beg.b;
        laser_pulser.rgba[0].alpha = LaserPulser::rgba_beg.a;
        laser_pulser.rgba[1].red = LaserPulser::rgba_end.r;
        laser_pulser.rgba[1].green = LaserPulser::rgba_end.g;
        laser_pulser.rgba[1].blue = LaserPulser::rgba_end.b;
        laser_pulser.rgba[1].alpha = LaserPulser::rgba_end.a;
    }

    // No GC symbol (inlined into LaserApplyConfig); the PS2 demo has it as a static member.
    void NMEZapomatic::LaserApplyConfig_laser_repel()
    {
        char* str_zapball = "fx_solid";

        // not in the DWARF: holds the raster from the lookup to its store after flg_render
        RwRaster* raster = NMEC_FindRWRaster(str_zapball);

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        laser_repel.flg_render = 3;
        laser_repel.rast_laser = raster;
        laser_repel.uv_scroll[0] = 0.1f;
        laser_repel.uv_scroll[1] = 0.0f;
        laser_repel.radius[0] = 0.05f;
        laser_repel.radius[1] = 0.1f;
        laser_repel.rgba[0].red = LaserRepel::rgba_beg.r;
        laser_repel.rgba[0].green = LaserRepel::rgba_beg.g;
        laser_repel.rgba[0].blue = LaserRepel::rgba_beg.b;
        laser_repel.rgba[0].alpha = LaserRepel::rgba_beg.a;
        laser_repel.rgba[1].red = LaserRepel::rgba_end.r;
        laser_repel.rgba[1].green = LaserRepel::rgba_end.g;
        laser_repel.rgba[1].blue = LaserRepel::rgba_end.b;
        laser_repel.rgba[1].alpha = LaserRepel::rgba_end.a;
    }

    static void LaserErupt(const xVec3* pos_from);

    // No symbol (inlined into LaserUpdate); the helper, parameter and local names are ours. Whether
    // the beam from pos_from to pos_to points along the NPC's facing.
    static inline S32 LaserCanHit(zNMECommon* npc, const xVec3* pos_from, const xVec3* pos_to)
    {
        xVec3 dir_beam = *pos_to - *pos_from;
        F32 len = dir_beam.length();
        if (len < 0.00001f)
        {
            return 0;
        }

        dir_beam /= len;
        if (xVec3Dot(&xModelGetFrame(npc->model)->at, &dir_beam) < 0.66f)
        {
            return 0;
        }
        return 1;
    }

    // No PS2 DWARF entry (inlined into NewTimeUpdate in the demo).
    void NMEZapomatic::LaserUpdate()
    {
        LaserPulser::needuvincr = 1;

        if (LaserCanHit(npc_owner, &pos_gunTip, &pos_aimPoint))
        {
            npc_owner->flg_xtrarend = 1;

            if (zapmode == ZAP_MODE_SHOOT && tmr_warmup < 0.0f)
            {
                zEntPlayer_DamageNPCKnockBack(npc_owner, 1, &xModelGetFrame(npc_owner->model)->pos,
                                              ePlayerNMEDamageEnemyLaserBeam);
                LaserErupt(&pos_gunTip);
            }
        }
    }

    void NMEZapomatic::LaserRender()
    {
        switch (zapmode)
        {
        case ZAP_MODE_SHOOT:
            if (tmr_warmup < 0.0f)
            {
                LaserRender_laser_pulser();
            }
            else
            {
                LaserRender_laser_target();
            }
            break;
        case ZAP_MODE_REPEL:
            LaserRender_laser_repel();
            break;
        }
    }

    // No symbol (inlined into LaserUpdate); the helper, parameter and local names are ours. Sparks
    // from pos_from towards the player. Defined below LaserRender for retail's literal order.
    static void LaserErupt(const xVec3* pos_from)
    {
        xVec3 dir_erupt = *pos_from - xglobals->___player_ent_dont_use_directly->bound.sph.center;
        F32 len = dir_erupt.length();
        if (len < 0.00001f)
        {
            return;
        }

        dir_erupt /= len;
        Java::plasmaErupt.ArcWelder(&xglobals->___player_ent_dont_use_directly->bound.sph.center,
                                    &dir_erupt, 1.0f, -1);
    }
} // namespace ZapGun

void zMervEngine::Reset()
{
    SetState(eState_Off);
}

void zMervEngine::SetState(eState state)
{
    if (state == mState)
    {
        return;
    }

    switch (mState)
    {
    case eState_Idle:
    {
        zNMECommon* npc = mOwnerNPC;
        if (npc->snd_table->sndGroupHandle[0] != -1)
        {
            xSndMgrStop(npc->sndID[0]);
            npc->sndID[0] = (iSndHandle)-1;
        }
        break;
    }
    case eState_Alert:
    {
        zNMECommon* npc = mOwnerNPC;
        if (npc->snd_table->sndGroupHandle[3] != -1)
        {
            xSndMgrStop(npc->sndID[3]);
            npc->sndID[3] = (iSndHandle)-1;
        }
        break;
    }
    }

    PlaySound(state);
    mState = state;
}

void zMervEngine::Process(F32 dt)
{
    if (mState == eState_Off)
    {
        return;
    }

    bool shutdown = false;
    if (!mOwnerNPC->IsAlive())
    {
        shutdown = true;
    }
    else
    {
        switch (mOwnerNPC->psy_self->GIDOfActive())
        {
        case NME_GOAL_DEAD:
        case NME_GOAL_AFTERLIFE:
        case NME_GOAL_SPAWN:
        case NME_GOAL_SCAREWAIT:
            shutdown = true;
            break;
        }
    }

    if (shutdown)
    {
        SetState(eState_Off);
    }
    else if (mSndPlayResult == -1)
    {
        PlaySound(mState);
    }
}

void zMervEngine::PlaySound(eState state)
{
    switch (state)
    {
    case eState_Idle:
        mSndPlayResult = mOwnerNPC->SndPlayFromTable(0); // eNMESound_MervEngineIdleL
        break;
    case eState_Alert:
        mSndPlayResult = mOwnerNPC->SndPlayFromTable(3); // eNMESound_MervEngineAlertL
        break;
    }
}
