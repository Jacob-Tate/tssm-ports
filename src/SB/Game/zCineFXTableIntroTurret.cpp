#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zNMEHazard.h"

namespace IntroTurret
{
    static void NCIN_TurMissileLaunch_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
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

            if (storage->npchaz != NULL && fxrec->basic.pos_B[0].x > 0.0f)
            {
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
                rc = haz->ConfigHelper(HAZ_TYP_TURBALL_V1);
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
            storage->npchaz->flg_hazard |= 0x20000;
        }
    }

    static void NCIN_MissileBoom_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        NCINHaz* storage = &fxrec->fxdata.hazdata;
        zNMEHazard* haz;
        S32 rc;
        xVec3 pos_orig;
        F32 tym;

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

        if (fxrec->flg_stat & NCIN_STAT_FIRST)
        {
            haz = HAZ_Acquire();
            if (haz != NULL)
            {
                rc = haz->ConfigHelper(HAZ_TYP_TURBOOM);
                if (rc)
                {
                    haz->npc_owner = NULL;
                    haz->custdata.collide.flg_collide = 0;

                    pos_orig = fxrec->basic.pos_A[0];

                    tym = fxrec->basic.pos_B[0].x;
                    if (tym < 0.25f)
                    {
                        tym = MAX(0.25f, fxrec->info.tym_end - fxrec->info.tym_beg);
                    }

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
            storage->npchaz->flg_hazard |= 0x20000;
        }
    }

    static NCINEntry g_TT01_TurretIntro[9] = {
        {
            { 0x1001, 8.5f, 9.333334f, "Key Flying Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, 200.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.32f, 0.3f } } },
            { NULL },
        },
        {
            { 0x1001, 8.5f, 9.333334f, "Key Flying Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 80.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.05f, 0.32f, 0.3f } } },
            { NULL },
        },
        {
            { 0x1002, 11.900001f, 14.000001f, "Missile Launch" },
            { (NCINUpdFunc)NCIN_TurMissileLaunch_Upd, NULL, NULL, NULL },
            { 3, 16, 0, 0 },
            { { { 2.85f, 21.8f, -45.6f }, { 13.6f, 21.8f, -60.3f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 14.000001f, 14.466667f, "Missile Launch" },
            { (NCINUpdFunc)NCIN_MissileBoom_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 13.6f, 21.8f, -60.3f }, { 0.0f, 0.0f, 0.0f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 12.200001f, 14.400001f, "Missile Launch" },
            { (NCINUpdFunc)NCIN_TurMissileLaunch_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 2.85f, 21.8f, -45.6f }, { 14.0f, 21.8f, -59.6f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 14.000001f, 14.466667f, "Missile Launch" },
            { (NCINUpdFunc)NCIN_MissileBoom_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 14.0f, 21.8f, -59.6f }, { 0.0f, 0.0f, 0.0f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 12.533334f, 14.666667f, "Missile Launch" },
            { (NCINUpdFunc)NCIN_TurMissileLaunch_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 2.85f, 21.8f, -45.6f }, { 14.7f, 21.8f, -59.3f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 14.000001f, 14.466667f, "Missile Launch" },
            { (NCINUpdFunc)NCIN_MissileBoom_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 14.7f, 21.8f, -59.3f }, { 0.0f, 0.0f, 0.0f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace IntroTurret

void zCineFX_Register_IntroTurret()
{
    static NCINMap cutmap[] = {
        { "enemy_turret_intro", IntroTurret::g_TT01_TurretIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
