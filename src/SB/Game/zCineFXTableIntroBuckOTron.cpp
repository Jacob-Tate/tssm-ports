#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zNPCTypes.h"
#include "zNMEParFX.h"
#include "zNMEHazard.h"

namespace IntroBuckOTron
{
    static void NCIN_SpawnballLaunch_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        NCINHaz* storage = &fxrec->fxdata.hazdata;
        zNMEHazard* haz;
        S32 rc;
        HAZLob* hazlob;
        xVec3 pos_orig;
        xVec3 pos_dest;
        F32 tym;

        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;

            if (storage->npchaz != NULL)
            {
                // The retail code sets the same flag twice (conditionally, then always).
                if (fxrec->basic.pos_B[0].x > 0.0f)
                {
                    storage->npchaz->flg_hazard |= 0x4;
                }

                storage->npchaz->flg_hazard |= 0x4;
            }

            storage->npchaz = NULL;
            return;
        }

        if (fxrec->flg_stat & NCIN_STAT_FIRST)
        {
            haz = HAZ_Acquire();
            if (haz != NULL)
            {
                rc = haz->ConfigHelper(HAZ_TYP_SPAWNBALL_GG);
                if (rc)
                {
                    haz->npc_owner = NULL;

                    hazlob = &haz->custdata.lobdata;
                    hazlob->flg_collide = 0;

                    pos_orig = fxrec->basic.pos_A[0];
                    pos_dest = fxrec->basic.pos_A[1];

                    tym = fxrec->basic.pos_B[0].x;
                    if (tym < 0.0f)
                    {
                        // The distance-based flight time is immediately replaced by the
                        // span of the effect in the retail code.
                        xVec3 diff = pos_orig - pos_dest;
                        tym = xVec3Length(&diff);
                        tym = fxrec->info.tym_end - fxrec->info.tym_beg;
                        if (tym < 0.00001f)
                        {
                            tym = 0.25f;
                        }
                    }

                    hazlob->pos_tgt = pos_dest;
                    haz->Start(&pos_orig, tym);

                    storage->npchaz = haz;
                }
            }
        }

        if (storage->npchaz == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
        else
        {
            storage->npchaz->flg_hazard &= ~0x60000;
        }
    }

    static void NCIN_BuckExplode_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        NCINHaz* storage = &fxrec->fxdata.hazdata;

        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;

            if (storage->npchaz != NULL && fxrec->basic.pos_B[0].x > 0.0f)
            {
                storage->npchaz->flg_hazard |= 0x4;
            }

            storage->npchaz = NULL;
            return;
        }

        if (!(fxrec->flg_stat & NCIN_STAT_FIRST) && storage->npchaz == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
    }

    static void NCIN_BuckExplode_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                    RwMatrixTag* animMat, U32 animIndex, U32)
    {
        NCINHaz* storage = &fxrec->fxdata.hazdata;
        zNMEHazard* haz;
        S32 rc;
        xMat4x3* mat_root;
        xVec3 pos_emit;
        S32 idx_bone;
        xMat4x3* mat_bone;
        xVec3 vec_offset;
        F32 tym;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        if (!(fxrec->flg_stat & NCIN_STAT_8))
        {
            return;
        }

        haz = HAZ_Acquire();
        if (haz == NULL)
        {
            return;
        }

        rc = haz->ConfigHelper(HAZ_TYP_BUCKOBOOM_V1);
        if (!rc)
        {
            return;
        }

        haz->npc_owner = NULL;
        haz->custdata.collide.flg_collide = 0;

        idx_bone = fxrec->index.idx_bone;
        if (idx_bone < 0)
        {
            pos_emit = fxrec->basic.pos_A[0];
        }
        else
        {
            mat_root = (xMat4x3*)&animMat[0];
            mat_bone = (xMat4x3*)&animMat[idx_bone];

            vec_offset = fxrec->basic.pos_A[1];
            pos_emit = mat_bone->pos;
            pos_emit += mat_bone->right * vec_offset.x;
            pos_emit += mat_bone->up * vec_offset.y;
            pos_emit += mat_bone->at * vec_offset.z;

            if (idx_bone != 0)
            {
                xVec3AddTo(&pos_emit, &mat_root->pos);
            }
        }

        tym = fxrec->basic.pos_B[0].x;
        if (tym > 0.25f)
        {
            haz->Start(&pos_emit, tym);
        }
        else
        {
            haz->Start(&pos_emit, MAX(0.25f, fxrec->info.tym_end - fxrec->info.tym_beg));
        }

        storage->npchaz = haz;
        storage->npchaz->flg_hazard &= ~0x60000;
    }

    namespace Belch
    {
        FogBreathEmitter yuckyFlogs = {
            NPAR_TYP_FOGBREATH,
            1.53f,
            { 0x65, 0x84, 0x00, 0xC8 },
            { 0.5f, 2.0f },
            { 0.0f, 3.5f, 0.0f },
            0.5f,
            90,
            0,
            0,
            0,
            { 4, -5.0f, -9.0f, { 0.5f, 0.35f, 1.0f } },
        };

        FogBreathEmitter ickyBlogs = {
            NPAR_TYP_FOGBREATH,
            2.25f,
            { 0x73, 0xC8, 0x00, 0x80 },
            { 0.25f, 0.8f },
            { 0.0f, 4.5f, 0.0f },
            4.5f,
            87,
            0,
            0,
            0,
            { 4, -4.0f, -5.0f, { 0.5f, 0.35f, 1.0f } },
        };

    } // namespace Belch

    static NCINEntry g_BB03_BuckOTronIntro[14] = {
        {
            { 0x1001, 2.8333335f, 3.3333335f, "Muzzle Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 8, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 100.0f } },
              { { 0.0f, 0.0f, 6.0f }, { 0.3f, 0.5f, 1.0f } } },
            { NULL },
        },
        {
            { 0x1001, 2.6666667f, 4.0f, "Muzzle Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 8, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 250.0f } },
              { { 0.0f, 0.0f, 7.0f }, { 0.3f, 0.5f, 0.35f } } },
            { NULL },
        },
        {
            { 0x1002, 2.7333336f, 3.3333335f, "Spawn Child Left" },
            { (NCINUpdFunc)NCIN_SpawnballLaunch_Upd, NULL, NULL, NULL },
            { 1, 8, 0, 0 },
            { { { 55.85f, 10.72f, -37.89f }, { 56.18f, 8.01f, -35.65f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 2.8333335f, 3.3333335f, "Spawn Child Right" },
            { (NCINUpdFunc)NCIN_SpawnballLaunch_Upd, NULL, NULL, NULL },
            { 1, 8, 0, 0 },
            { { { 55.85f, 10.72f, -37.89f }, { 54.48f, 8.01f, -39.54f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 2.8333335f, 4.166667f, "Child Appears Left" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, NULL, NULL, NULL },
            { 2, 0, 0, 0 },
            { { { 56.18f, 8.01f, -35.65f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 2.8333335f, 4.3333335f, "Child Appears Right" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, NULL, NULL, NULL },
            { 3, 0, 0, 0 },
            { { { 54.48f, 8.01f, -39.54f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1004, 11.166667f, 11.733334f, "Burpo Base" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEBreathSpew_AR, NULL,
              (NCINTweakFunc)NCIN_NMEBreathSpew_Twk },
            { 3, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Belch::yuckyFlogs },
        },
        {
            { 0x1004, 11.266667f, 11.666667f, "Burpo Plus" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEBreathSpew_AR, NULL,
              (NCINTweakFunc)NCIN_NMEBreathSpew_Twk },
            { 3, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Belch::ickyBlogs },
        },
        {
            { 0x1004, 12.233334f, 12.700001f, "Burpo Base" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEBreathSpew_AR, NULL,
              (NCINTweakFunc)NCIN_NMEBreathSpew_Twk },
            { 2, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Belch::yuckyFlogs },
        },
        {
            { 0x1004, 12.333334f, 12.633334f, "Burpo Plus" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEBreathSpew_AR, NULL,
              (NCINTweakFunc)NCIN_NMEBreathSpew_Twk },
            { 2, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Belch::ickyBlogs },
        },
        {
            { 0x1006, 14.466667f, 16.333334f, "Bucketotron Explodes" },
            { (NCINUpdFunc)NCIN_BuckExplode_Upd, (NCINAnimFunc)NCIN_BuckExplode_AR, NULL, NULL },
            { 1, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 1.75f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1007, 14.466667f, 15.000001f, "Bucketotron Shrapnel" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)NCIN_NMEEjectShrapnel_AR, NULL,
              NULL },
            { 1, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 7.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { (void*)NME_TYPE_BUCKOTRON_V3 },
        },
        {
            { 0x1007, 14.500001f, 14.666667f, "Bucketotron Shrapnel" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)NCIN_EjectShrapnel_AR, NULL, NULL },
            { 1, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 7.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { (void*)"shrap_buckotron_gg_death" },
        },
        { 0 },
    };
} // namespace IntroBuckOTron

void zCineFX_Register_IntroBuckOTron()
{
    static NCINMap cutmap[] = {
        { "enemy_bucketotron_intro", IntroBuckOTron::g_BB03_BuckOTronIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
