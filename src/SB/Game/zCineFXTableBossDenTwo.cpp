#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zNMEHazard.h"
#include "zNMEParFX.h"

namespace CinFXDenTwo
{

    static void NCIN_HazBowWave_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        NCINHaz* storage = &fxrec->fxdata.hazdata;
        zNMEHazard* haz;
        S32 rc;

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
                rc = haz->ConfigHelper(HAZ_TYP_BOWWAVE);
                if (rc)
                {
                    haz->npc_owner = NULL;
                    haz->Start(&g_O3, -1.0f);
                    storage->npchaz = haz;
                }
            }
        }

        if (storage->npchaz == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
    }

    static void NCIN_HazBowWave_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                   RwMatrixTag* animMat, U32 animIndex, U32)
    {
        zNMEHazard* haz;
        F32 siz_bowWave;
        xMat4x3* mat_root;
        S32 idx_bone;
        F32 dang;
        xVec3 pos_emit;
        xMat3x3 mat_setHazard;
        xMat4x3* mat_bone;
        xVec3 vec_offset;
        xMat3x3 mat_rot;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        haz = fxrec->fxdata.hazdata.npchaz;
        if (haz == NULL)
        {
            return;
        }

        siz_bowWave = fxrec->basic.pos_A[1].y;

        haz->LifespanSet(0.5f, 1.0f);

        if (siz_bowWave >= 0.0f)
        {
            haz->custdata.typical.rad_min = siz_bowWave;
        }
        if (siz_bowWave >= 0.0f)
        {
            haz->custdata.typical.rad_max = siz_bowWave;
        }
        haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;

        idx_bone = fxrec->index.idx_bone;
        dang = xDangleClamp(fxrec->basic.pos_A[1].x);

        if (idx_bone < 0)
        {
            pos_emit = fxrec->basic.pos_A[0];
            xMat3x3RotC(&mat_setHazard, g_Y3.x, g_Y3.y, g_Y3.z, dang);
        }
        else
        {
            mat_root = (xMat4x3*)&animMat[0];
            mat_bone = (xMat4x3*)&animMat[idx_bone];

            vec_offset = fxrec->basic.pos_A[0];
            pos_emit = mat_bone->pos;
            pos_emit += mat_bone->right * vec_offset.x;
            pos_emit += mat_bone->up * vec_offset.y;
            pos_emit += mat_bone->at * vec_offset.z;

            if (idx_bone != 0)
            {
                xVec3AddTo(&pos_emit, &mat_root->pos);
            }

            // Retail bug: the rotation is built into mat_setHazard, so the multiply below reads
            // mat_rot uninitialized (and overwrites the rotation just built).
            xMat3x3RotC(&mat_setHazard, mat_bone->up.x, mat_bone->up.y, mat_bone->up.z, dang);
            xMat3x3Mul(&mat_setHazard, &mat_rot, mat_bone);
        }

        haz->PosSet(&pos_emit);
        haz->TypData_RotMatSet(&mat_setHazard);
    }

    static void NCIN_SplashExhaust_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        xVec3 pos_emit;
        xVec3 dir_emit;

        if (fxrec->input.fxparm == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
        else if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }

        if (fxrec->flg_stat & NCIN_STAT_DEAD)
        {
            return;
        }

        pos_emit = fxrec->basic.pos_A[0];
        dir_emit = fxrec->basic.pos_A[1];
        dir_emit.safe_normalize(g_Z3);

        ((ExhaustEmitter*)fxrec->input.fxparm)->EmitOnAxis(&pos_emit, &dir_emit, 1.0f);
    }

    static void NCIN_SplashExhaust_Twk(const char*, NCINEntry*)
    {
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

    namespace Sheeting
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.25f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.32f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.5f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.62f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.75f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.85f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 1.0f, { 0x32, 0x64, 0xC8, 0x00 }, 0.5f },
        };

        ExhaustEmitter action = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 15.0f, 0.0f, 6.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.5f },
            { 5, 5, 3.0f, -2.0f },
            { 5.0f, 5.0f, 0.5f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 8, 1.0f, 1.0f },
        };

    } // namespace Sheeting

    namespace Comet
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.25f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.32f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.5f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.62f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.75f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.85f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 1.0f, { 0x32, 0x64, 0xC8, 0x00 }, 0.5f },
        };

        ExhaustEmitter boot_spray = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 15.0f, 0.0f, 6.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.5f },
            { 12, 5, 1.5f, -1.5f },
            { 5.0f, 5.0f, 0.5f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 8, 1.0f, 2.0f },
        };

    } // namespace Comet

    namespace DenTaDa
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.25f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.32f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.5f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.62f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.75f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.85f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 1.0f, { 0x32, 0x64, 0xC8, 0x00 }, 0.5f },
        };

        ExhaustEmitter obscureDennis = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 15.0f, 0.0f, 6.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.5f },
            { 12, 5, 1.5f, -1.5f },
            { 5.0f, 5.0f, 0.5f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 8, 1.0f, 2.0f },
        };

    } // namespace DenTaDa

    namespace PatFall
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.25f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.32f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.5f, { 0x32, 0x64, 0xC8, 0x96 }, 1.0f },
            { 0.62f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 0.75f, { 0xC8, 0xC8, 0xC8, 0x96 }, 1.0f },
            { 0.85f, { 0x32, 0x64, 0xC8, 0x96 }, 0.0f },
            { 1.0f, { 0x32, 0x64, 0xC8, 0x00 }, 0.5f },
        };

        ExhaustEmitter patSplash = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 15.0f, 0.0f, 6.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.5f },
            { 10, 5, 0.5f, -0.5f },
            { 4.0f, 8.0f, 0.5f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 8, 1.0f, 1.0f },
        };

    } // namespace PatFall

    namespace CatBow
    {
        ExhaustCurve curve_data[8] = {
            { 0.0f, { 0x32, 0x64, 0xC8, 0xFF }, 1.0f },
            { 0.25f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.0f },
            { 0.32f, { 0x32, 0x64, 0xC8, 0xFF }, 2.0f },
            { 0.5f, { 0x32, 0x64, 0xC8, 0xFF }, 1.0f },
            { 0.62f, { 0x32, 0x64, 0xC8, 0xFF }, 1.5f },
            { 0.75f, { 0xC8, 0xC8, 0xC8, 0xFF }, 1.0f },
            { 0.85f, { 0x32, 0x64, 0xC8, 0xFF }, 1.0f },
            { 1.0f, { 0x32, 0x64, 0xC8, 0x00 }, 0.5f },
        };

        ExhaustEmitter bowSplash = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 25.0f, 0.0f, 1.0f },
            { 1.5f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 1.0f },
            { 3, 0, 5.0f, -5.0f },
            { 25.0f, 10.0f, 8.5f, 1.5f, 0.0f, 0.0f, { 0.0f, 0.0f, -5.0f } },
            { curve_data, 8, 1.0f, 4.0f },
        };

    } // namespace CatBow

    static NCINEntry g_B301_DenTwoIntro[8] = {
        {
            { 0x1001, 5.3333335f, 9.0f, "Boot Spray" },
            { (NCINUpdFunc)NCIN_HazBowWave_Upd, (NCINAnimFunc)NCIN_HazBowWave_AR, NULL, NULL },
            { 2, 2, 0, 0 },
            { { { 0.0f, -0.2f, -4.5f }, { 0.0f, 2.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 8.666667f, 9.433333f, "Boot Comet Jaws Theme" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.5f, -4.5f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Comet::boot_spray },
        },
        {
            { 0x1003, 9.0f, 9.333334f, "Boot Leaps" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.5f, -1.5f }, { 0.0f, -1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Sheeting::action },
        },
        {
            { 0x1004, 9.900001f, 10.400001f, "Dennis Returns" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 4, 2, 0, 0 },
            { { { 0.0f, 0.75f, 0.25f }, { 0.0f, 1.0f, 1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &DenTaDa::obscureDennis },
        },
        {
            { 0x1002, 10.166667f, 10.666667f, "Boot Comet Topple" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 1.0f, -4.5f }, { 0.0f, 1.0f, -1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Comet::boot_spray },
        },
        {
            { 0x1005, 15.666668f, 16.0f, "Pat Tumble Splash" },
            { (NCINUpdFunc)NCIN_SplashExhaust_Upd, NULL, NULL,
              (NCINTweakFunc)NCIN_SplashExhaust_Twk },
            { 0, 2, 0, 0 },
            { { { 0.0f, 1.5f, -16.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &PatFall::patSplash },
        },
        {
            { 0x1006, 17.066668f, 18.666668f, "Pat Heel Spray" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 1, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.5f }, { 0.0f, 1.0f, 1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &PatFall::patSplash },
        },
        { 0 },
    };

    static NCINEntry g_B301_DenTwoDeath[4] = {
        {
            { 0x1007, 3.6666667f, 4.666667f, "Patrick Clings" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 2, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.5f }, { 0.0f, 1.0f, 1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &PatFall::patSplash },
        },
        {
            { 0x1008, 10.000001f, 11.000001f, "Catamaran Approach Left" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 3, 2, 0, 0 },
            { { { -45.0f, 2.0f, -30.0f }, { 0.0f, 1.0f, -1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &CatBow::bowSplash },
        },
        {
            { 0x1008, 10.000001f, 11.000001f, "Catamaran Approach Right" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_BoneEmitExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_BoneEmitExhaust_Twk },
            { 3, 2, 0, 0 },
            { { { 45.0f, 2.0f, -30.0f }, { 0.0f, 1.0f, -1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &CatBow::bowSplash },
        },
        { 0 },
    };
} // namespace CinFXDenTwo

void zCineFX_Register_BossDenTwo()
{
    static NCINMap cutmap[] = {
        { "boss_dennis02_intro", CinFXDenTwo::g_B301_DenTwoIntro, 0 },
        { "boss_dennis2_death", CinFXDenTwo::g_B301_DenTwoDeath, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
