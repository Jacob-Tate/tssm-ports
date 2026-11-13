#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xFX.h"
#include "xMath3.h"
#include "xString.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"

#include "zCutsceneMgr.h"
#include "zNMEParFX.h"
#include "zNMESupport.h"
#include "zNPCTypes.h"

namespace IntroZapper
{

    static void NCIN_ZapGunSparks_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
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

        ((PlasmaEmitter*)fxrec->input.fxparm)->ArcWelder(&pos_emit, &dir_emit, 1.0f, -1);
    }

    static void NCIN_ZapGunSparks_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_EngineExhaust_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
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

    static void NCIN_EngineExhaust_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_ZapGunLaser_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                    RwMatrixTag* animMat, U32 animIndex, U32)
    {
        if (animIndex == fxrec->index.idx_anim)
        {
            S32 boneIndex;
            xMat4x3* mat_bone;
            xMat4x3* mat_root;
            xVec3 pos_emit;

            boneIndex = fxrec->index.idx_bone;
            mat_root = (xMat4x3*)&animMat[0];
            mat_bone = (xMat4x3*)&animMat[boneIndex];

            pos_emit = fxrec->basic.pos_A[0];
            xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
            pos_emit += mat_bone->pos;

            if (boneIndex != 0)
            {
                pos_emit += mat_root->pos;
            }

            fxrec->fxdata.customdata.v[0] = pos_emit;
            fxrec->fxdata.customdata.n[0] |= 0x1;
        }

        if (animIndex == fxrec->index.idx_boneA)
        {
            S32 boneIndex;
            xMat4x3* mat_bone;
            xMat4x3* mat_root;
            xVec3 pos_emit;

            boneIndex = fxrec->index.idx_boneB;
            mat_root = (xMat4x3*)&animMat[0];
            mat_bone = (xMat4x3*)&animMat[boneIndex];

            pos_emit = fxrec->basic.pos_A[1];
            xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
            pos_emit += mat_bone->pos;

            if (boneIndex != 0)
            {
                pos_emit += mat_root->pos;
            }

            fxrec->fxdata.customdata.v[1] = pos_emit;
            fxrec->fxdata.customdata.n[0] |= 0x2;
        }

        if ((fxrec->fxdata.customdata.n[0] & 0x3) == 0x3)
        {
            NMELaser* laser = (NMELaser*)fxrec->input.fxparm;

            laser->flg_render |= 0x7;
            laser->Render(&fxrec->fxdata.customdata.v[0], &fxrec->fxdata.customdata.v[1]);
        }
    }

    static void NCIN_ZapGunLaser_Rnd(const zCutsceneMgr*, NCINEntry*)
    {
    }

    static void NCIN_ZapGunLaser_Twk(const char*, NCINEntry*)
    {
    }

    namespace SBLeap
    {
        static const xFXRibbon::curve_node curvedata[2] = {
            { 0.0f, { 0xC8, 0xAA, 0x41, 0x64 }, 1.0f },
            { 1.0f, { 0xC8, 0xAA, 0x41, 0x00 }, 0.0f },
        };

        NMEHelpRib::RibData rib_leap = {
            { 1, 1, 0, 0 },
            { "fx_streak2", (xFXRibbon::curve_node*)curvedata, 2, 1.5f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

    } // namespace SBLeap

    namespace Glove
    {
        static const xFXRibbon::curve_node curvedata[2] = {
            { 0.0f, { 0xB4, 0x00, 0x00, 0x64 }, 1.0f },
            { 1.0f, { 0xB4, 0x00, 0x00, 0x00 }, 0.0f },
        };

        NMEHelpRib::RibData rib_glove = {
            { 1, 1, 0, 0 },
            { "fx_streak2", (xFXRibbon::curve_node*)curvedata, 2, 1.75f, 1.0f },
            { 1, 1.0f, 0.5f },
            { NULL, 0 },
        };

    } // namespace Glove

    namespace EngineSmoke
    {
        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x7F, 0x7F, 0x7F, 0x00 }, 0.25f },
            { 0.6f, { 0x7F, 0x7F, 0x7F, 0xC8 }, 1.0f },
            { 0.8f, { 0x28, 0x28, 0x28, 0xA0 }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.75f },
        };

        ExhaustEmitter jet_smoke = {
            NPAR_TYP_JETSMOKE,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 5.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.3f },
            { 2, 0, 0.25f, -0.25f },
            { 3.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 1.0f },
        };

    } // namespace EngineSmoke

    namespace EngineFlame
    {
        ExhaustCurve curve_data[5] = {
            { 0.0f, { 0xFA, 0xFA, 0xC8, 0x7F }, 0.1f },
            { 0.1f, { 0xFA, 0xF0, 0x19, 0x7F }, 0.25f },
            { 0.2f, { 0xFA, 0x8C, 0x32, 0x7F }, 0.75f },
            { 0.4f, { 0xBE, 0x00, 0x00, 0x57 }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.0f },
        };

        ExhaustEmitter jet_flame = {
            NPAR_TYP_JETFLAME,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 10.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.3f, 0.1f },
            { 1, 0, 0.12f, 0.0f },
            { 4.0f, 1.0f, 0.2f, 0.8f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 5, 1.0f, 2.0f },
        };

    } // namespace EngineFlame

    namespace LazWarn
    {
        PlasmaEmitter warn_sparks = {
            NPAR_TYP_PLASMA,
            0.55f,
            { 0x00, 0xC0, 0xFF, 0xFF },
            { 0.1f, 0.15f },
            { 0.0f, 0.0f, 0.0f },
            0.0f,
            88,
            0,
            5,
            0,
            { 5.25f, 0.0f, 0.1f, 0.0f, 0.0f },
        };

    } // namespace LazWarn

    namespace LazDare
    {
        PlasmaEmitter dare_sparks = {
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
            { -3.05f, 0.0f, 0.7f, -4.0f, 0.0f },
        };

    } // namespace LazDare

    namespace LazHate
    {
        PlasmaEmitter hate_sparks = {
            NPAR_TYP_MUZZLE,
            0.2f,
            { 0x00, 0xC0, 0xFF, 0xFF },
            { 0.25f, 1.5f },
            { 0.0f, 0.0f, 0.0f },
            25.0f,
            93,
            0,
            1,
            0,
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        };

    } // namespace LazHate

    namespace LazJava
    {
        PlasmaEmitter java_erupt = {
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

    } // namespace LazJava

    namespace LazData
    {
        static NMELaser laser_sight;
        static NMELaser laser_main;

        namespace PropSight
        {
            char* str_zapball = "fx_solid";
            static const xColor_tag rgba_beg = { 0xFF, 0xFF, 0x00, 0x64 };
            static const xColor_tag rgba_end = { 0xFF, 0x00, 0x00, 0x37 };
        } // namespace PropSight

        namespace PropMain
        {
            char* str_zapball = "fx_solid";
            static const xColor_tag rgba_beg = { 0x00, 0x00, 0xFF, 0xC8 };
            static const xColor_tag rgba_end = { 0x00, 0xC8, 0xC8, 0xC8 };
        } // namespace PropMain
    } // namespace LazData

    static NCINEntry g_JK01_introZapper[30] = {
        {
            { 0x1001, 0.0f, 23.500002f, "Antenna" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 0.33333334f, 23.500002f, "Exhaust 1 - Flame" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_EngineExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_EngineExhaust_Twk },
            { 0, 25, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &EngineFlame::jet_flame },
        },
        {
            { 0x1002, 0.33333334f, 23.500002f, "Exhaust 1 - Smoke" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_EngineExhaust_AR, NULL,
              (NCINTweakFunc)NCIN_EngineExhaust_Twk },
            { 0, 25, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &EngineSmoke::jet_smoke },
        },
        {
            { 0x1003, 1.3333334f, 5.3333335f, "Sparks Idle" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazWarn::warn_sparks },
        },
        {
            { 0x100A, 2.0f, 2.6666667f, "SB Ribbon" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &SBLeap::rib_leap },
        },
        {
            { 0x1009, 5.0000005f, 8.0f, "SB Attack Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 3, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.0f, 1.0f, 1.0f }, { 0.5f, 0.35f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1004, 5.3333335f, 7.833334f, "Sparks Warn" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazDare::dare_sparks },
        },
        {
            { 0x1006, 6.3333335f, 7.833334f, "Laser Scan" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunLaser_AR,
              (NCINRendFunc)NCIN_ZapGunLaser_Rnd, (NCINTweakFunc)NCIN_ZapGunLaser_Twk },
            { 0, 28, 3, 6 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, -0.05f, 0.17f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazData::laser_sight },
        },
        {
            { 0x1005, 7.833334f, 8.166667f, "Sparks Hate" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 0.75f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazHate::hate_sparks },
        },
        {
            { 0x1008, 7.833334f, 8.166667f, "Laser Kill" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunLaser_AR,
              (NCINRendFunc)NCIN_ZapGunLaser_Rnd, (NCINTweakFunc)NCIN_ZapGunLaser_Twk },
            { 0, 28, 3, 6 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, -0.05f, 0.17f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazData::laser_main },
        },
        {
            { 0x100B, 7.833334f, 8.200001f, "Plasma Brand WhoopAss" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 3, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazJava::java_erupt },
        },
        {
            { 0x1004, 8.166667f, 9.333334f, "Sparks Warn" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazDare::dare_sparks },
        },
        {
            { 0x1009, 8.166667f, 8.5f, "SB Knocked Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 3, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 100.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.35f, 0.75f } } },
            { NULL },
        },
        {
            { 0x100B, 8.166667f, 8.5f, "Plasma Brand WhoopAss" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 3, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazJava::java_erupt },
        },
        {
            { 0x100C, 8.5f, 8.666667f, "SB Land Dust" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x100B, 8.5f, 9.166667f, "Plasme AfterFX" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 3, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazJava::java_erupt },
        },
        {
            { 0x1004, 9.333334f, 10.000001f, "Sparks Warn" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazDare::dare_sparks },
        },
        {
            { 0x1003, 10.000001f, 11.000001f, "Sparks Idle" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazWarn::warn_sparks },
        },
        {
            { 0x1003, 12.333334f, 13.333334f, "Sparks Idle" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazWarn::warn_sparks },
        },
        {
            { 0x1004, 13.333334f, 19.166668f, "Sparks Warn" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazDare::dare_sparks },
        },
        {
            { 0x1006, 14.166667f, 14.733335f, "Laser Scan" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunLaser_AR,
              (NCINRendFunc)NCIN_ZapGunLaser_Rnd, (NCINTweakFunc)NCIN_ZapGunLaser_Twk },
            { 0, 28, 3, 6 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, -0.05f, 0.17f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazData::laser_sight },
        },
        {
            { 0x1007, 14.666667f, 17.766668f, "Laser Single" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunLaser_AR,
              (NCINRendFunc)NCIN_ZapGunLaser_Rnd, (NCINTweakFunc)NCIN_ZapGunLaser_Twk },
            { 0, 28, 3, 6 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, -0.05f, 0.17f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazData::laser_sight },
        },
        {
            { 0x1003, 19.166668f, 20.000002f, "Sparks Idle" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_ZapGunSparks_AR, NULL,
              (NCINTweakFunc)NCIN_ZapGunSparks_Twk },
            { 0, 28, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LazWarn::warn_sparks },
        },
        {
            { 0x100D, 21.833334f, 22.133335f, "Glove Ribbon 1" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 1, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Glove::rib_glove },
        },
        {
            { 0x100D, 22.166668f, 22.633335f, "Glove Ribbon 2" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 1, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Glove::rib_glove },
        },
        {
            { 0x100D, 22.666668f, 23.166668f, "Glove Ribbon 3" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 1, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Glove::rib_glove },
        },
        {
            { 0x100E, 23.166668f, 23.500002f, "Glove Impact" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x100F, 23.433334f, 24.833334f, "Zapper Explode" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)NCIN_NMEEjectShrapnel_AR, NULL,
              NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { (void*)NME_TYPE_ZAPPER_V1 },
        },
        {
            { 0x100A, 27.000002f, 28.333334f, "SB Ribbon" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 2, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &SBLeap::rib_leap },
        },
        { 0 },
    };
} // namespace IntroZapper

void zCineFX_ZapperIntro_PostInit()
{
    PKRAssetTOCInfo tocdata;
    U32 aid_cutscn;
    S32 rc;

    aid_cutscn = xStrHash("enemy_merv_intro");
    rc = xSTGetAssetInfo(aid_cutscn, &tocdata);
    if (rc)
    {
        using namespace IntroZapper::LazData;

        laser_sight.rast_laser = NULL;
        laser_sight.uv_base[0] = 0.0f;
        laser_sight.uv_base[1] = 0.0f;
        laser_sight.rast_laser = NMEC_FindRWRaster(PropSight::str_zapball);
        laser_sight.uv_scroll[0] = 0.1f;
        laser_sight.uv_scroll[1] = 0.0f;
        laser_sight.radius[0] = 0.025f;
        laser_sight.radius[1] = 0.01f;
        laser_sight.rgba[0].red = PropSight::rgba_beg.r;
        laser_sight.rgba[0].green = PropSight::rgba_beg.g;
        laser_sight.rgba[0].blue = PropSight::rgba_beg.b;
        laser_sight.rgba[0].alpha = PropSight::rgba_beg.a;
        laser_sight.rgba[1].red = PropSight::rgba_end.r;
        laser_sight.rgba[1].green = PropSight::rgba_end.g;
        laser_sight.rgba[1].blue = PropSight::rgba_end.b;
        laser_sight.rgba[1].alpha = PropSight::rgba_end.a;

        laser_main.rast_laser = NULL;
        laser_main.uv_base[0] = 0.0f;
        laser_main.uv_base[1] = 0.0f;
        laser_main.rast_laser = NMEC_FindRWRaster(PropMain::str_zapball);
        laser_main.uv_scroll[0] = 0.1f;
        laser_main.uv_scroll[1] = 0.0f;
        laser_main.radius[0] = 0.05f;
        laser_main.radius[1] = 0.25f;
        laser_main.rgba[0].red = PropMain::rgba_beg.r;
        laser_main.rgba[0].green = PropMain::rgba_beg.g;
        laser_main.rgba[0].blue = PropMain::rgba_beg.b;
        laser_main.rgba[0].alpha = PropMain::rgba_beg.a;
        laser_main.rgba[1].red = PropMain::rgba_end.r;
        laser_main.rgba[1].green = PropMain::rgba_end.g;
        laser_main.rgba[1].blue = PropMain::rgba_end.b;
        laser_main.rgba[1].alpha = PropMain::rgba_end.a;
    }
}

void zCineFX_Register_IntroZapper()
{
    static NCINMap cutmap[] = {
        { "enemy_merv_intro", IntroZapper::g_JK01_introZapper, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
