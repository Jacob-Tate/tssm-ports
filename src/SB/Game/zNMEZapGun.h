#ifndef ZNMEZAPGUN_H
#define ZNMEZAPGUN_H

#include "zNMECommon.h"
#include "zNMESupport.h"
#include "zNMEParFX.h"
#include "xMath3.h"
#include "xEntBoulder.h"
#include "zEntPlayer.h"
#include "xRand.h"

struct xFXRibbon;

struct zMervEngine
{
    enum eState
    {
        eState_Off,
        eState_Idle,
        eState_Alert
    };

    zNMECommon* mOwnerNPC;
    eState mState;
    S32 mSndPlayResult;

    void PlaySound(eState state);
    void Process(F32 dt);
    void SetState(eState state);
    void Reset();
};

namespace ZapGun
{
    enum en_zapmod
    {
        ZAP_MODE_STARTUP,
        ZAP_MODE_NOTHING,
        ZAP_MODE_IDLE,
        ZAP_MODE_WARN,
        ZAP_MODE_SHOOT,
        ZAP_MODE_REPEL,
        ZAP_MODE_NOMORE
    };

    enum en_zapatak
    {
        ZAP_ATAK_WARMUP,
        ZAP_ATAK_FULLON,
        ZAP_ATAK_RECYCLE,
        ZAP_ATAK_NOMORE
    };

    // Gun bone on both models (no symbols: folded constants).
    namespace ZapperParm
    {
        const S32 idx_gunBone = 28;
    } // namespace ZapperParm

    namespace MervynParm
    {
        const S32 idx_gunBone = 28;
    } // namespace MervynParm

    struct NMEZapomatic
    {
        F32 tmr_warmup;
        F32 tmr_megaprog;
        zNMECommon* npc_owner;
        en_zapmod zapmode;
        en_zapatak zapatak;
        S32 energized;
        xVec3 dir_aim;
        F32 tmr_cycle;
        xVec3 pos_gunTip;
        xVec3 pos_aimPoint;
        xFXRibbon* ribbon;
        F32 tmr_nextShot;
        S8 cnt_nextEmit;
        S8 cnt_siphonEmit;
        S8 flg_repelBowlBall;
        S8 idx_plyrBone;
        F32 tmr_nextPulse;
        xVec3 dir_tgtLaser;
        F32 len_tgtLaser;

        // No symbol (inlined): the Mervyn variants share the gun with the Zappers.
        S32 IsMervyn()
        {
            S32 typ = npc_owner->itemType;
            if (typ == NME_TYPE_MERVYN_V1)
            {
                return 1;
            }
            if (typ == NME_TYPE_MERVYN_V2)
            {
                return 1;
            }
            if (typ == NME_TYPE_MERVYN_V3)
            {
                return 1;
            }
            return 0;
        }

        // No symbol (inlined).
        S32 GunBoneIdx()
        {
            return IsMervyn() ? MervynParm::idx_gunBone : ZapperParm::idx_gunBone;
        }

        // No symbol (inlined): point the gun bone along the aim direction.
        void AimGunBone()
        {
            xMat3x3 mat_aim;
            xVec3 dir_look = dir_aim;
            dir_look.invert();
            xMat3x3LookVec(&mat_aim, &dir_look);

            S32 idx = GunBoneIdx();
            xMat4x3* mat_root = (xMat4x3*)npc_owner->model->Mat;
            xMat3x3 mat_inv;
            xMat3x3Transpose(&mat_inv, mat_root);
            xMat3x3Mul(&mat_root[idx], &mat_aim, &mat_inv);
        }

        // Weak in the target, where NMEZapomatic::Reset calls it out of line.
        void ModeSet(en_zapmod mode, S32 force)
        {
            if (mode != zapmode || force)
            {
                ModeSwitch(mode);
            }
        }

        // No symbol (inlined into LaserRender).
        void LaserRender_laser_pulser()
        {
            laser_pulser.Render(&pos_gunTip,
                                &xglobals->___player_ent_dont_use_directly->bound.sph.center);
        }

        // No symbol (inlined into LaserRender): beam to a sliding window of the player's bones.
        void LaserRender_laser_target()
        {
            xEnt* plyr = xglobals->___player_ent_dont_use_directly;
            S32 num_bone = plyr->model->BoneCount;
            S32 idx = CLAMP(idx_plyrBone, 1, num_bone - 8);
            xMat4x3* mat_root = (xMat4x3*)plyr->model->Mat;

            for (S32 i = 0; i < 7; i++)
            {
                xVec3 pos_tgt = *(xVec3*)&plyr->model->Mat[idx + i].pos;
                xMat4x3Toworld(&pos_tgt, mat_root, &pos_tgt);
                laser_target.Render(&pos_gunTip, &pos_tgt);
            }

            idx_plyrBone += 3;
            if (idx_plyrBone >= num_bone - 7)
            {
                idx_plyrBone = 1;
            }
        }

        // No symbol (inlined into LaserRender): the bubble bowl jitters the beam target.
        void LaserRender_laser_repel()
        {
            xEntBoulder* bbowl = zEntPlayerGetBBowl();
            if (bbowl != NULL && xEntIsVisible(bbowl))
            {
                for (S32 i = 0; i < 3; i++)
                {
                    xVec3 pos_tgt = bbowl->bound.sph.center;
                    pos_tgt += g_X3 * (0.25f * xurand());
                    pos_tgt += g_Y3 * (0.25f * xurand());
                    pos_tgt += g_Z3 * (0.25f * xurand());
                    laser_repel.Render(&pos_gunTip, &pos_tgt);
                }
            }
        }

        static NMELaser laser_target;
        static NMELaser laser_pulser;
        static NMELaser laser_repel;

        // No symbol (inlined into the Zapper and Mervyn Init); the name is ours. Claims the gun for
        // its NPC and resets the laser sprites and the particle types the gun uses.
        void Init(zNMECommon* npc)
        {
            npc_owner = npc;

            laser_target.rast_laser = NULL;
            laser_target.uv_base[0] = 0.0f;
            laser_target.uv_base[1] = 0.0f;
            laser_pulser.rast_laser = NULL;
            laser_pulser.uv_base[0] = 0.0f;
            laser_pulser.uv_base[1] = 0.0f;
            laser_repel.rast_laser = NULL;
            laser_repel.uv_base[0] = 0.0f;
            laser_repel.uv_base[1] = 0.0f;

            zNMEParFX_PartyInvite(NPAR_TYP_PLASMA, NULL, 0);
            zNMEParFX_PartyInvite(NPAR_TYP_MUZZLE, NULL, 0);
        }

        static void LaserUVScroll(F32 dt);
        void LaserRender();
        void LaserUpdate();
        static void LaserApplyConfig_laser_target();
        static void LaserApplyConfig_laser_pulser();
        static void LaserApplyConfig_laser_repel();
        void LaserApplyConfig();
        void LaserSetup();
        void Shoot_Upd(F32 dt);
        void Dare_ParFX(F32 dt);
        void Hate_ParFX(F32 dt);
        void Shoot_Prep();
        void Repel_Upd(F32 dt);
        F32 DangerZoneRatio();
        void Warn_ParFX(F32 dt);
        void Warn_Upd(F32 dt);
        void Idle_Upd(F32 dt);
        void CalcNewGunTip(xVec3* pos_tip);
        void CacheUsefulPos();
        void Render();
        void NewTimeUpdate(F32 dt);
        void ModeSwitch(en_zapmod mode);
        void Reset();
        void Setup();
    };
} // namespace ZapGun

#endif
