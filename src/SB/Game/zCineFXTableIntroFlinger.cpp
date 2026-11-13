#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zNMEHazard.h"
#include "zNMEParFX.h"

namespace IntroFlinger
{
    static void NCIN_FlingSpit_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
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
                rc = haz->ConfigHelper(HAZ_TYP_FLING_V1);
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
            storage->npchaz->flg_hazard |= 0x60000;
        }
    }

    static void NCIN_BoneEmitExhaust_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                        RwMatrixTag* animMat, U32 animIndex, U32)
    {
        S32 idx_bone;
        xMat4x3* mat_bone;
        xMat4x3* mat_root;
        xVec3 pos_emit;
        xVec3 dir_rel;
        xVec3 dir_emit;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        idx_bone = fxrec->index.idx_bone;
        mat_root = (xMat4x3*)&animMat[0];
        mat_bone = (xMat4x3*)&animMat[idx_bone];

        pos_emit = fxrec->basic.pos_A[0];
        xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
        xVec3AddTo(&pos_emit, &mat_bone->pos);

        if (idx_bone != 0)
        {
            xVec3AddTo(&pos_emit, &mat_root->pos);
        }

        dir_rel = fxrec->basic.pos_A[1];
        F32 len = dir_rel.length(); // not in the DWARF: the single sqrt is reused for the scale
        if (len < 0.00001f)
        {
            dir_emit = mat_bone->at;
        }
        else
        {
            dir_rel *= 1.0f / len;
            dir_emit = mat_bone->right * dir_rel.x;
            dir_emit += mat_bone->up * dir_rel.y;
            dir_emit += mat_bone->at * dir_rel.z;
        }

        ((ExhaustEmitter*)fxrec->input.fxparm)->EmitOnAxis(&pos_emit, &dir_emit, 1.0f);
    }

    static void NCIN_BoneEmitExhaust_Twk(const char*, NCINEntry*)
    {
    }

    namespace SlipSlide
    {
        ExhaustCurve curve_data[3] = {
            { 0.0f, { 0x46, 0xA0, 0xDC, 0x20 }, 0.25f },
            { 0.5f, { 0x46, 0xA0, 0xDC, 0x40 }, 1.0f },
            { 1.0f, { 0x46, 0xA0, 0xDC, 0x00 }, 0.25f },
        };

        ExhaustEmitter slideGas = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.5f },
            { 0, 2, 0.1f, -0.1f },
            { 0.2f, 0.2f, 0.5f, 0.2f, 0.0f, 0.2f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 3, 1.0f, 0.75f },
        };

    } // namespace SlipSlide

    static NCINEntry g_DE01_FlingerIntro[6] = {
        {
            { 0x1001, 11.666667f, 13.333334f, "Cactus Bubbles Fall" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 200.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.32f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1001, 17.0f, 17.433334f, "Cactus Bubbles Batted" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 3, 0, 0 },
            { { { 0.0f, -0.24f, 0.0f }, { 0.0f, 0.0f, 200.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.32f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1001, 17.266668f, 17.566668f, "Cactus Bubbles Batted" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 350.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.32f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1002, 21.333334f, 22.166668f, "Flinger Spitball" },
            { (NCINUpdFunc)NCIN_FlingSpit_Upd, NULL, NULL, NULL },
            { 0, -1, 0, 0 },
            { { { 50.32f, 7.86f, 68.88f }, { 53.96f, 4.14f, 65.51f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 22.000002f, 24.000002f, "Player Slip" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 5, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, -1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &SlipSlide::slideGas },
        },
        { 0 },
    };
} // namespace IntroFlinger

void zCineFX_Register_IntroFlinger()
{
    static NCINMap cutmap[] = {
        { "enemy_flinger_intro", IntroFlinger::g_DE01_FlingerIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
