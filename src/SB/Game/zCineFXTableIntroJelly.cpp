#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zNMEParFX.h"
#include "zLightning.h"
#include "zNMESupplement.h"

#include <string.h>
#include "xRand.h"

namespace IntroJelly
{
    static void NCIN_BB02JellyLight_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
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

            NMEC_MakeLightningInfo(NME_LYT_JELLYPINK, &addInfo);

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

    static void NCIN_BB02JellyDanger_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                        RwMatrixTag* animMat, U32 animIndex, U32)
    {
        zLightning* light;
        S32 idx_animSrc;
        S32 idx_animTgt;

        light = fxrec->fxdata.lytdata.lyt_zap;
        if (light == NULL)
        {
            return;
        }

        idx_animSrc = (S32)fxrec->basic.pos_A[0].x;
        idx_animTgt = (S32)fxrec->basic.pos_B[0].x;

        if (animIndex == idx_animSrc)
        {
            S32 boneIndex;
            xVec3 vec_offset;
            xMat4x3* mat_bone;
            xMat4x3* mat_root;
            xVec3 pnt;

            boneIndex = (S32)fxrec->basic.pos_A[0].y;
            vec_offset = fxrec->basic.pos_A[1];

            mat_root = (xMat4x3*)&animMat[0];
            mat_bone = (xMat4x3*)&animMat[boneIndex];

            xMat3x3RMulVec(&pnt, mat_bone, &vec_offset);
            xVec3AddTo(&pnt, &mat_bone->pos);

            if (boneIndex != 0)
            {
                xVec3AddTo(&pnt, &mat_root->pos);
            }

            zLightningModifyEndpoints(light, &pnt, &light->func.endPoint[1]);
        }

        if (animIndex == idx_animTgt)
        {
            S32 boneIndex;
            xVec3 vec_offset;
            xMat4x3* mat_bone;
            xMat4x3* mat_root;
            xVec3 pnt;

            boneIndex = (S32)fxrec->basic.pos_B[0].y;
            vec_offset = fxrec->basic.pos_B[1];

            mat_root = (xMat4x3*)&animMat[0];
            mat_bone = (xMat4x3*)&animMat[boneIndex];

            xMat3x3RMulVec(&pnt, mat_bone, &vec_offset);
            xVec3AddTo(&pnt, &mat_bone->pos);

            if (boneIndex != 0)
            {
                xVec3AddTo(&pnt, &mat_root->pos);
            }

            zLightningModifyEndpoints(light, &light->func.endPoint[0], &pnt);
        }
    }

    static void NCIN_BB02JellyAttack_AR(const zCutsceneMgr* csnmgr, NCINEntry* fxrec,
                                        RpAtomic* model, RwMatrixTag* animMat, U32 animIndex,
                                        U32 dataIndex)
    {
        NCIN_BB02JellyDanger_AR(csnmgr, fxrec, model, animMat, animIndex, dataIndex);
    }

    static void NCIN_BB02JellyGround_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                        RwMatrixTag* animMat, U32 animIndex, U32)
    {
        zLightning* light;
        S32 idx_animSrc;
        S32 boneIndex;
        xVec3 vec_offset;
        xMat4x3* mat_bone;
        xMat4x3* mat_root;
        xVec3 pnt;
        xVec3 pos_ground;

        light = fxrec->fxdata.lytdata.lyt_zap;
        if (light == NULL)
        {
            return;
        }

        idx_animSrc = (S32)fxrec->basic.pos_A[0].x;
        if (animIndex != idx_animSrc)
        {
            return;
        }

        boneIndex = (S32)fxrec->basic.pos_A[0].y;
        vec_offset = fxrec->basic.pos_A[1];

        mat_root = (xMat4x3*)&animMat[0];
        mat_bone = (xMat4x3*)&animMat[boneIndex];

        xMat3x3RMulVec(&pnt, mat_bone, &vec_offset);
        xVec3AddTo(&pnt, &mat_bone->pos);

        if (boneIndex != 0)
        {
            xVec3AddTo(&pnt, &mat_root->pos);
        }

        pos_ground = pnt;
        pos_ground.x += 4.0f * (2.0f * (xurand() - 0.5f));
        pos_ground.y -= 5.0f;
        pos_ground.z += 4.0f * (2.0f * (xurand() - 0.5f));

        zLightningModifyEndpoints(light, &pnt, &pos_ground);
    }

    inline void NCIN_BB02JellyGround_Twk(const char*, NCINEntry*)
    {
    }

    inline void NCIN_BB02JellyDanger_Twk(const char*, NCINEntry*)
    {
    }

    inline void NCIN_BB02JellyAttack_Twk(const char*, NCINEntry*)
    {
    }

    NCINLytNMEInfo g_lyt_jelly = {
        NME_LYT_JELLYPINK,
        { 0, 0, { 0.0f, 0.0f, 0.0f } },
        { 8, 0, { 0.0f, 0.0f, 0.0f } },
    };

    namespace Angry
    {
        ExhaustCurve curve_data[3] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.0f },
            { 0.5f, { 0xFF, 0x64, 0x37, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0x00, 0x00, 0x00 }, 0.25f },
        };

        ExhaustEmitter steam = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { -0.6f, 0.0f, 7.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.25f, 0.0f },
            { 1, 0, 0.1f, -0.1f },
            { 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.3f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 3, 0.1f, 0.6f },
        };

    } // namespace Angry

    namespace Dazed
    {
        ExhaustCurve curve_data[3] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
            { 0.5f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 0.25f },
        };

        ExhaustEmitter steam = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 4.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 3, 0.5f, 0.6f },
        };

    } // namespace Dazed

    static NCINEntry g_BB02_JellyIntro[20] = {
        {
            { 0x1001, 1.0666667f, 2.1333334f, "Ground Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 1.8333334f, 2.8333335f, "Ground Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 2.6666667f, 4.166667f, "Ground Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 4.6000004f, 6.0000005f, "Ground Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 14.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 4.7333336f, 6.0000005f, "Ground Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 4.8333335f, 6.0000005f, "Attack Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyAttack_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyAttack_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 8.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 5.166667f, 6.0000005f, "Attack Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyAttack_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyAttack_Twk },
            { 0, 0, 0, 0 },
            { { { 8.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 8.0f, 31.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 5.5000005f, 6.0000005f, "Attack Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyAttack_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyAttack_Twk },
            { 0, 0, 0, 0 },
            { { { 8.0f, 31.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 8.0f, 54.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 5.666667f, 6.1000004f, "Attack Lightning" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 8.0f, 54.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1005, 10.733334f, 11.500001f, "Death By Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubBurst_AR, NULL,
              (NCINTweakFunc)zCinFX_BubBurst_Twk },
            { 0, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 250.0f } },
              { { 0.25f, 0.25f, 0.25f }, { 0.25f, 1.0f, 0.75f } } },
            { NULL },
        },
        {
            { 0x1006, 12.666667f, 13.166667f, "a-Disappear With Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubBurst_AR, NULL,
              (NCINTweakFunc)zCinFX_BubBurst_Twk },
            { 0, 10, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.35f, 0.2f, 0.35f }, { 0.15f, 0.2f, 0.55f } } },
            { NULL },
        },
        {
            { 0x1006, 12.766667f, 13.166667f, "b-Disappear With Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubBurst_AR, NULL,
              (NCINTweakFunc)zCinFX_BubBurst_Twk },
            { 0, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.35f, 0.2f, 0.35f }, { 0.15f, 0.2f, 0.75f } } },
            { NULL },
        },
        {
            { 0x1006, 12.866668f, 13.166667f, "c-Disappear With Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubBurst_AR, NULL,
              (NCINTweakFunc)zCinFX_BubBurst_Twk },
            { 0, 14, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.35f, 0.2f, 0.35f }, { 0.15f, 0.2f, 0.55f } } },
            { NULL },
        },
        {
            { 0x1006, 13.000001f, 13.166667f, "d-Disappear With Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubBurst_AR, NULL,
              (NCINTweakFunc)zCinFX_BubBurst_Twk },
            { 0, 7, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 450.0f } },
              { { 0.35f, 0.2f, 0.35f }, { 0.45f, 0.3f, 0.75f } } },
            { NULL },
        },
        {
            { 0x1007, 15.166667f, 20.000002f, "Family Fued" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyDanger_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyDanger_Twk },
            { 0, 0, 0, 0 },
            { { { 4.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 3.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1007, 15.833334f, 20.000002f, "Family Fued" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyDanger_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyDanger_Twk },
            { 0, 0, 0, 0 },
            { { { 3.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 5.0f, 14.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1007, 16.666668f, 24.000002f, "Family Fued" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 6.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1007, 18.333334f, 24.000002f, "Family Fued" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 2.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1007, 20.000002f, 24.000002f, "Family Fued" },
            { (NCINUpdFunc)NCIN_BB02JellyLight_Upd, (NCINAnimFunc)NCIN_BB02JellyGround_AR, NULL,
              (NCINTweakFunc)NCIN_BB02JellyGround_Twk },
            { 0, 0, 0, 0 },
            { { { 6.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace IntroJelly

void zCineFX_Register_IntroJelly()
{
    static NCINMap cutmap[] = {
        { "enemy_critter_intro", IntroJelly::g_BB02_JellyIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
