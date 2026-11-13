#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xEvent.h"
#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"
#include "iEnv.h"

#include "zCutsceneMgr.h"
#include "zGlobals.h"
#include "zLightning.h"
#include "zNMEParFX.h"
#include "zNMESupplement.h"
#include "zScene.h"
#include "xRand.h"

namespace CinFXNeptune
{

    static void NCIN_NepTridLyt_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        _tagLightningAdd addInfo;
        xVec3 pnt;
        F32 lyfe;

        if (killit)
        {
            if (fxrec->fxdata.lytdata.lyt_zap != NULL)
            {
                zLightningKill(fxrec->fxdata.lytdata.lyt_zap);
            }

            fxrec->fxdata.lytdata.lyt_zap = NULL;
            fxrec->flg_stat |= NCIN_STAT_DEAD;
            return;
        }

        if (fxrec->flg_stat & NCIN_STAT_FIRST)
        {
            memset(&addInfo, 0, sizeof(_tagLightningAdd));

            NMEC_MakeLightningInfo(NME_LYT_NEPTUNETRIDENT, &addInfo);

            pnt.x = 0.0f;
            pnt.y = 0.0f;
            pnt.z = 0.0f;
            addInfo.start = &pnt;
            addInfo.end = &pnt;

            lyfe = fxrec->info.tym_end - fxrec->info.tym_beg;
            lyfe = MAX(0.1f, lyfe);
            addInfo.time = 0.25f + lyfe;

            fxrec->fxdata.lytdata.lyt_zap = zLightningAdd(&addInfo);
        }

        if (fxrec->fxdata.lytdata.lyt_zap == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
    }

    static void NCIN_NepTridLyt_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                   RwMatrixTag* animMat, U32 animIndex, U32)
    {
        zLightning* light;
        S32 boneIndexSrc;
        S32 boneIndexTgt;
        xVec3 vec_srcOffset;
        xVec3 vec_srcRand;
        xVec3 vec_tgtOffset;
        xVec3 vec_tgtRand;
        xMat4x3* mat_boneSrc;
        xMat4x3* mat_boneTgt;
        xMat4x3* mat_root;
        xVec3 pos_src;
        xVec3 pos_tgt;

        light = fxrec->fxdata.lytdata.lyt_zap;
        if (light == NULL)
        {
            return;
        }

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        boneIndexSrc = fxrec->index.idx_bone;
        boneIndexTgt = fxrec->index.idx_boneA;
        if (fxrec->index.idx_boneA < 0)
        {
            boneIndexTgt = boneIndexSrc;
        }

        vec_srcOffset = fxrec->basic.pos_A[0];
        vec_srcRand = fxrec->basic.pos_A[1];
        vec_tgtOffset = fxrec->basic.pos_B[0];
        vec_tgtRand = fxrec->basic.pos_B[1];

        mat_root = (xMat4x3*)&animMat[0];
        mat_boneSrc = (xMat4x3*)&animMat[boneIndexSrc];
        mat_boneTgt = (xMat4x3*)&animMat[boneIndexTgt];

        pos_src = vec_srcOffset;
        pos_src += mat_boneSrc->right * (vec_srcRand.x * (2.0f * (xurand() - 0.5f)));
        pos_src += mat_boneSrc->up * (vec_srcRand.y * (2.0f * (xurand() - 0.5f)));
        pos_src += mat_boneSrc->at * (vec_srcRand.z * (2.0f * (xurand() - 0.5f)));

        xMat3x3RMulVec(&pos_src, mat_boneSrc, &pos_src);
        xVec3AddTo(&pos_src, &mat_boneSrc->pos);

        if (boneIndexSrc != 0)
        {
            xVec3AddTo(&pos_src, &mat_root->pos);
        }

        pos_tgt = vec_tgtOffset;
        pos_tgt += mat_boneTgt->right * (vec_tgtRand.x * (2.0f * (xurand() - 0.5f)));
        pos_tgt += mat_boneTgt->up * (vec_tgtRand.y * (2.0f * (xurand() - 0.5f)));
        pos_tgt += mat_boneTgt->at * (vec_tgtRand.z * (2.0f * (xurand() - 0.5f)));

        xMat3x3RMulVec(&pos_tgt, mat_boneTgt, &pos_tgt);
        // Retail bug: the target endpoint is offset by the SOURCE bone's position.
        xVec3AddTo(&pos_tgt, &mat_boneSrc->pos);

        if (boneIndexTgt != 0)
        {
            xVec3AddTo(&pos_tgt, &mat_root->pos);
        }

        zLightningModifyEndpoints(light, &pos_src, &pos_tgt);
    }

    static void NCIN_NepTridLyt_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_NMEExhaustImpact_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                         RwMatrixTag*, U32 animIndex, U32)
    {
        xVec3 pos_emit;
        xVec3 dir_emit;
        F32 spd_mult;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        pos_emit = fxrec->basic.pos_A[0];
        dir_emit = fxrec->basic.pos_A[1];
        spd_mult = fxrec->basic.pos_B[0].x;

        dir_emit.safe_normalize(g_Y3);

        ((ExhaustEmitter*)fxrec->input.fxparm)
            ->EmitRing(&pos_emit, &dir_emit, -100000.0f, spd_mult, -1, NULL);
    }

    static void NCIN_NMEExhaustImpact_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_FloorDestroy(const zCutsceneMgr*, NCINEntry*, RpAtomic*, RwMatrixTag*, U32,
                                  U32)
    {
        xEnt* entDestructFloor;
        xJSPHeader* underFloorJSP;

        entDestructFloor = (xEnt*)zSceneFindObject(0xE2BA79D0);
        underFloorJSP = (xJSPHeader*)xSTFindAsset(0x56320732, NULL);

        if (entDestructFloor != NULL)
        {
            F32 zeroParam[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            zEntEvent(entDestructFloor, entDestructFloor, eEventEnable);
            zEntEvent(NULL, entDestructFloor, eEventLaunchShrapnel, zeroParam,
                      (xBase*)xSTFindAsset(0xF07A4606, NULL), 0xF07A4606);
            zEntEvent(entDestructFloor, entDestructFloor, eEventInvisible);
        }

        if (underFloorJSP != NULL)
        {
            iEnvJSPOn(globals.sceneCur->env->geom, underFloorJSP);
        }
    }

    namespace Rubble
    {
        PuffEmitter cloud_inner = {
            NPAR_TYP_DUSTPUFF,
            1.2f,
            { 0x78, 0x78, 0x78, 0xB9 },
            { 2.1f, 2.8f },
            { 0.0f, -5.0f, 0.0f },
            1.84f,
            93,
            0,
            4,
            0,
            { 2.5f, 3.5f },
        };

        PuffEmitter cloud_outer = {
            NPAR_TYP_DUSTPUFF,
            1.5f,
            { 0x78, 0x78, 0x78, 0xB9 },
            { 1.5f, 2.3f },
            { 0.0f, -5.0f, 0.0f },
            3.84f,
            93,
            0,
            3,
            0,
            { 6.0f, -3.5f },
        };

    } // namespace Rubble

    namespace ImpactCore
    {
        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x8C, 0x8C, 0x8C, 0x00 }, 0.0f },
            { 0.1f, { 0x8C, 0x8C, 0x8C, 0xC8 }, 0.5f },
            { 0.5f, { 0x8C, 0x8C, 0x8C, 0xC8 }, 3.0f },
            { 1.0f, { 0x8C, 0x8C, 0x8C, 0x00 }, 5.0f },
        };

        ExhaustEmitter eject_core = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { -5.0f, 0.0f, 4.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.1f },
            { 10, 5, 0.35f, -0.25f },
            { 4.0f, 4.0f, 1.3f, 2.3f, 0.0f, 10.5f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 0.5f },
        };

    } // namespace ImpactCore

    namespace ImpactFill
    {
        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x3C, 0x3C, 0x3C, 0x00 }, 0.0f },
            { 0.1f, { 0x3C, 0x3C, 0x3C, 0xFF }, 1.0f },
            { 0.5f, { 0x3C, 0x3C, 0x3C, 0xFF }, 1.0f },
            { 1.0f, { 0x3C, 0x3C, 0x3C, 0x00 }, 1.0f },
        };

        ExhaustEmitter eject_fill = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { -5.0f, 2.0f, 11.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.25f, 0.0f },
            { 3, 2, 0.35f, -0.25f },
            { 6.0f, 4.0f, 0.3f, 0.3f, 0.0f, 0.5f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 1.0f },
        };

    } // namespace ImpactFill

    static NCINEntry g_B401_NeptuneIntro[12] = {
        {
            { 0x1001, 0.0f, 4.666667f, "HeliPak Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 33, 0, 0 },
            { { { 0.12f, 0.33f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.03f, 0.32f, 0.1f } } },
            { NULL },
        },
        {
            { 0x1001, 0.0f, 4.666667f, "HeliPak Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 33, 0, 0 },
            { { { -0.12f, 0.33f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.03f, 0.32f, 0.1f } } },
            { NULL },
        },
        {
            { 0x1001, 12.333334f, 19.333334f, "HeliPak Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 33, 0, 0 },
            { { { 0.12f, 0.33f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.03f, 0.32f, 0.1f } } },
            { NULL },
        },
        {
            { 0x1001, 12.333334f, 19.333334f, "HeliPak Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 33, 0, 0 },
            { { { -0.12f, 0.33f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.03f, 0.32f, 0.1f } } },
            { NULL },
        },
        {
            { 0x1001, 33.666668f, 43.166668f, "HeliPak Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 33, 0, 0 },
            { { { 0.12f, 0.33f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.03f, 0.32f, 0.1f } } },
            { NULL },
        },
        {
            { 0x1001, 33.666668f, 43.166668f, "HeliPak Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 33, 0, 0 },
            { { { -0.12f, 0.33f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.03f, 0.32f, 0.1f } } },
            { NULL },
        },
        {
            { 0x1002, 43.333336f, 45.333336f, "Trident Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 5, 44, 0, 0 },
            { { { 0.0f, -0.5f, 0.0f }, { 0.0f, 0.0f, 200.0f } },
              { { 1.0f, -1.0f, -1.0f }, { 0.4f, 1.45f, 0.45f } } },
            { NULL },
        },
        {
            { 0x1003, 42.666668f, 45.333336f, "Trident Light Hand Base" },
            { (NCINUpdFunc)NCIN_NepTridLyt_Upd, (NCINAnimFunc)NCIN_NepTridLyt_AR, NULL,
              (NCINTweakFunc)NCIN_NepTridLyt_Twk },
            { 5, 44, 44, 0 },
            { { { 0.0f, -2.1f, 0.0f }, { 0.2f, 0.0f, 0.2f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.2f, 0.0f, 0.2f } } },
            { NULL },
        },
        {
            { 0x1003, 43.000004f, 45.333336f, "Trident Light Base Tong" },
            { (NCINUpdFunc)NCIN_NepTridLyt_Upd, (NCINAnimFunc)NCIN_NepTridLyt_AR, NULL,
              (NCINTweakFunc)NCIN_NepTridLyt_Twk },
            { 5, 44, 44, 0 },
            { { { 0.0f, -1.0f, 0.0f }, { 0.2f, 0.0f, 0.2f } },
              { { 0.0f, -0.4f, 0.0f }, { 0.4f, 0.2f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 43.333336f, 45.333336f, "Trident Light Tong Tong" },
            { (NCINUpdFunc)NCIN_NepTridLyt_Upd, (NCINAnimFunc)NCIN_NepTridLyt_AR, NULL,
              (NCINTweakFunc)NCIN_NepTridLyt_Twk },
            { 5, 44, -1, 0 },
            { { { -0.4f, -0.4f, 0.0f }, { 0.2f, 0.4f, 0.0f } },
              { { 0.4f, -0.4f, 0.0f }, { 0.2f, 0.4f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1003, 43.666668f, 45.333336f, "Trident Lightning" },
            { (NCINUpdFunc)NCIN_NepTridLyt_Upd, (NCINAnimFunc)NCIN_NepTridLyt_AR, NULL,
              (NCINTweakFunc)NCIN_NepTridLyt_Twk },
            { 5, 44, -1, 0 },
            { { { -0.4f, -0.4f, 0.0f }, { 0.2f, 0.4f, 0.0f } },
              { { 0.4f, -0.4f, 0.0f }, { 0.2f, 0.4f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };

    static NCINEntry g_B401_NeptuneTran[17] = {
        {
            { 0x1003, 0.0f, 6.666667f, "Trident Lightning" },
            { (NCINUpdFunc)NCIN_NepTridLyt_Upd, (NCINAnimFunc)NCIN_NepTridLyt_AR, NULL,
              (NCINTweakFunc)NCIN_NepTridLyt_Twk },
            { 5, 44, -1, 0 },
            { { { 0.0f, -3.4f, 0.0f }, { 0.2f, 0.0f, 0.2f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.5f, 0.0f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1003, 0.0f, 6.666667f, "Trident Lightning" },
            { (NCINUpdFunc)NCIN_NepTridLyt_Upd, (NCINAnimFunc)NCIN_NepTridLyt_AR, NULL,
              (NCINTweakFunc)NCIN_NepTridLyt_Twk },
            { 5, 44, -1, 0 },
            { { { 0.0f, -3.4f, 0.0f }, { 0.3f, 0.4f, 0.5f } },
              { { 0.0f, -0.75f, 0.0f }, { 0.3f, 0.4f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1004, 2.3333335f, 4.3333335f, "Trident Wake Head" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 44, 0, 0 },
            { { { 0.0f, -0.5f, 0.0f }, { 0.0f, 0.0f, 300.0f } },
              { { 0.0f, 1.0f, 0.0f }, { 0.5f, 0.32f, 1.0f } } },
            { NULL },
        },
        {
            { 0x1005, 2.5000002f, 2.6000001f, "Trident Impact 1 Core" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { -0.18f, 0.24f, 2.19f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactCore::eject_core },
        },
        {
            { 0x1005, 2.5000002f, 2.6000001f, "Trident Impact 1 Fill" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { -0.18f, 0.24f, 2.19f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactFill::eject_fill },
        },
        {
            { 0x1006, 2.6000001f, 2.9333334f, "Trident Emerge 1 Core" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { 15.6f, 0.11f, -11.4f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactCore::eject_core },
        },
        {
            { 0x1006, 2.6000001f, 2.9333334f, "Trident Emerge 1 Fill" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { 15.6f, 0.11f, -11.4f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactFill::eject_fill },
        },
        {
            { 0x1004, 2.6000001f, 2.9333334f, "Trident Emerge 1 Bub" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 43, 0, 0 },
            { { { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 300.0f } },
              { { 0.0f, 0.5f, 0.0f }, { 0.35f, 0.32f, 1.5f } } },
            { NULL },
        },
        {
            { 0x1005, 3.5000002f, 3.8333335f, "Trident Impact 2 Core" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { -6.52f, -0.04f, -9.49f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactCore::eject_core },
        },
        {
            { 0x1005, 3.5000002f, 3.6666667f, "Trident Impact 2 Fill" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { -6.52f, -0.04f, -9.49f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.7f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactFill::eject_fill },
        },
        {
            { 0x1006, 4.0f, 4.3333335f, "Trident Emerge 2 Core" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { -1.2f, -0.13f, -2.07f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactCore::eject_core },
        },
        {
            { 0x1006, 4.0f, 4.3333335f, "Trident Emerge 2 Fill" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEExhaustImpact_AR, NULL,
              (NCINTweakFunc)NCIN_NMEExhaustImpact_Twk },
            { 0, 0, 0, 0 },
            { { { -1.2f, -0.13f, -2.07f }, { 0.0f, 1.0f, 0.0f } },
              { { 1.4f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &ImpactFill::eject_fill },
        },
        {
            { 0x1004, 4.0f, 4.3333335f, "Trident Emerge 2 Bub" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 43, 0, 0 },
            { { { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 300.0f } },
              { { 0.0f, 0.5f, 0.0f }, { 0.35f, 0.32f, 1.5f } } },
            { NULL },
        },
        {
            { 0x1008, 2.6666667f, 2.8333335f, "Cave In Dust Inner" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Rubble::cloud_inner },
        },
        {
            { 0x1008, 3.3333335f, 4.0f, "Cave In Dust Outer" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Rubble::cloud_outer },
        },
        {
            { 0x1009, 4.3333335f, 4.666667f, "Floor destroy" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)NCIN_FloorDestroy, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };

    static NCINEntry g_B401_NeptuneDefeat[2] = {
        {
            { 0x1000, 0.33333334f, -0.033333335f, "Example" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };

    static NCINEntry g_B401_NeptuneDeath[5] = {
        {
            { 0x100A, 1.9666668f, 2.1666667f, "Crown Bubbles Lg" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 18, 0, 0 },
            { { { 0.0f, 0.4f, 0.0f }, { 0.0f, 0.0f, 100.0f } },
              { { 0.0f, 5.0f, 0.0f }, { 0.25f, 0.32f, 1.5f } } },
            { NULL },
        },
        {
            { 0x100A, 1.9666668f, 2.1666667f, "Crown Bubbles Md" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 18, 0, 0 },
            { { { 0.0f, 0.4f, 0.0f }, { 0.0f, 0.0f, 200.0f } },
              { { 0.0f, 5.0f, 0.0f }, { 0.25f, 0.32f, 0.5f } } },
            { NULL },
        },
        {
            { 0x100A, 1.9666668f, 2.1666667f, "Crown Bubbles Sm" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 18, 0, 0 },
            { { { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, 200.0f } },
              { { 0.0f, 3.0f, 0.0f }, { 0.25f, 0.32f, 0.25f } } },
            { NULL },
        },
        {
            { 0x100B, 2.0f, 2.3333335f, "Bucket explode" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)NCIN_EjectShrapnel_AR, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { (void*)"frag_neptune_boss" },
        },
        { 0 },
    };
} // namespace CinFXNeptune

void zCineFX_Register_BossNeptune()
{
    static NCINMap cutmap[] = {
        { "neptune_boss_intro", CinFXNeptune::g_B401_NeptuneIntro, 0 },
        { "boss_neptune_tran", CinFXNeptune::g_B401_NeptuneTran, 0 },
        { "neptune_defeat", CinFXNeptune::g_B401_NeptuneDefeat, 0 },
        { "boss_neptune_death", CinFXNeptune::g_B401_NeptuneDeath, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
