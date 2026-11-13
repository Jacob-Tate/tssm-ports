#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath3.h"
#include "xVec3Inlines.h"
#include "xFX.h"

#include "zCutsceneMgr.h"
#include "zFXRibbonPool.h"
#include "zNMEHazard.h"
#include "zNMEParFX.h"
#include "zNMESupport.h"

using NMEHelpRib::RibData;

namespace IntroPopper
{

    static void NCIN_PopGround_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        if (fxrec->input.fxparm == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
            return;
        }

        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
    }

    static void NCIN_PopGround_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                  RwMatrixTag* animMat, U32 animIndex, U32)
    {
        xVec3 pos_emit;
        S32 idx_bone;
        xMat4x3* mat_root;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        pos_emit = fxrec->basic.pos_A[0];

        idx_bone = fxrec->index.idx_bone;
        if (idx_bone >= 0)
        {
            mat_root = (xMat4x3*)&animMat[0];
            pos_emit = ((xMat4x3*)&animMat[idx_bone])->pos;
            if (idx_bone != 0)
            {
                xVec3AddTo(&pos_emit, &mat_root->pos);
            }
        }

        ((PopGasEmitter*)fxrec->input.fxparm)->Fountain(&pos_emit, NULL);
    }

    static void NCIN_PopGround_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_HuffNPuff_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        if (fxrec->input.fxparm == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
            return;
        }

        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
    }

    static void NCIN_HuffNPuff_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                  RwMatrixTag* animMat, U32 animIndex, U32)
    {
        S32 idx_bone;
        xMat4x3* mat_root;
        xMat4x3* mat_bone;
        xVec3 pos_emit;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        idx_bone = fxrec->index.idx_bone;
        mat_root = (xMat4x3*)&animMat[0];
        mat_bone = (xMat4x3*)&animMat[idx_bone];

        pos_emit = mat_bone->pos;
        if (idx_bone != 0)
        {
            xVec3AddTo(&pos_emit, &mat_root->pos);
        }

        ((PuffEmitter*)fxrec->input.fxparm)->Footstroke(&pos_emit, &mat_bone->at);
    }

    static void NCIN_HuffNPuff_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_NMERibHelp_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        RibData* ribhelp = (RibData*)fxrec->input.fxparm;
        NCINRibbon* storage;

        if (ribhelp == NULL)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
            return;
        }

        storage = &fxrec->fxdata.ribbondata;

        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
            if (storage->ribbon != NULL)
            {
                zFXRibbonPoolDelete(storage->ribbon);
                storage->ribbon = NULL;
            }
            return;
        }

        if (fxrec->flg_stat & NCIN_STAT_FIRST)
        {
            ribhelp->FindTexture();
            if (ribhelp->txtrxtra.txtr_texture == NULL)
            {
                fxrec->flg_stat |= NCIN_STAT_DEAD;
                return;
            }

            storage->ribbon = zFXRibbonPoolNew();
            if (storage->ribbon == NULL)
            {
                fxrec->flg_stat |= NCIN_STAT_DEAD;
                return;
            }

            ribhelp->ConfigRibbon(storage->ribbon);
        }

        if (ribhelp->flags.dev_enable)
        {
            ribhelp->ConfigRibbon(storage->ribbon);
        }
    }

    static void NCIN_NMERibHelp_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                   RwMatrixTag* animMat, U32 animIndex, U32)
    {
        NCINRibbon* storage;
        RibData* ribhelp;
        S32 idx_bone;
        xMat4x3* mat_root;
        xMat4x3* mat_bone;
        xMat4x3* mat_boneA;
        xVec3 pos_emit;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        storage = &fxrec->fxdata.ribbondata;
        if (storage->ribbon == NULL)
        {
            return;
        }

        ribhelp = (RibData*)fxrec->input.fxparm;
        if (ribhelp == NULL)
        {
            return;
        }

        idx_bone = fxrec->index.idx_bone;
        mat_root = (xMat4x3*)&animMat[0];
        mat_bone = (xMat4x3*)&animMat[idx_bone];
        mat_boneA = (xMat4x3*)&animMat[fxrec->index.idx_boneA];

        pos_emit = mat_bone->pos;
        if (idx_bone != 0)
        {
            xVec3AddTo(&pos_emit, &mat_root->pos);
        }

        xVec3 dir_emit = mat_boneA->pos - mat_bone->pos;

        F32 len = dir_emit.length(); // not in the DWARF: the sqrt is reused for the ribbon width
        if (len < 0.1f)
        {
            return;
        }

        storage->ribbon->insert(pos_emit, dir_emit, len * ribhelp->updinfo.scl_master,
                                ribhelp->updinfo.alf_master, 0);
    }

    static void NCIN_NMERibHelp_Twk(const char*, NCINEntry*)
    {
    }

    static void NCIN_PopRockLaunch_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
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
                rc = haz->ConfigHelper(HAZ_TYP_POPPER_V1);
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

    static PopGasEmitter g_gasFountA = {
        NPAR_TYP_POPGAS,
        0.8f,
        { 0x64, 0xE1, 0x64, 0x3C },
        { 0.35f, 2.5f },
        { 0.0f, 0.0f, 0.0f },
        183.78539f,
        85,
        0,
        7,
        0,
        { 0.0f, 30.0f, 0.4f, 5.0f, 0.0f },
    };

    static PopGasEmitter g_gasFountB = {
        NPAR_TYP_POPGAS,
        0.8f,
        { 0x64, 0xE1, 0x64, 0x3C },
        { 0.35f, 2.5f },
        { 0.0f, 0.0f, 0.0f },
        183.78539f,
        85,
        0,
        7,
        0,
        { 0.0f, 30.0f, 0.4f, 5.0f, 0.0f },
    };

    static PopGasEmitter g_gasGround = {
        NPAR_TYP_POPGAS,
        2.0f,
        { 0x96, 0xC8, 0x7D, 0xC8 },
        { 0.25f, 1.0f },
        { 0.0f, -10.0f, 0.0f },
        0.7853982f,
        97,
        0,
        25,
        0,
        { 1.4f, 0.0f, 0.3f, 1.0f, 1.0f },
    };

    PuffEmitter g_huffNPuff_01 = {
        NPAR_TYP_DUSTPUFF,
        0.2f,
        { 0xFF, 0xFF, 0xFF, 0x37 },
        { 0.1f, 0.3f },
        { 0.0f, 5.0f, 0.0f },
        0.0f,
        80,
        0,
        2,
        0,
        { 3.2f, 1.0f },
    };

    PuffEmitter g_huffNPuff_02 = {
        NPAR_TYP_DUSTPUFF,
        1.0f,
        { 0xFF, 0xFF, 0xFF, 0x37 },
        { 0.1f, 0.5f },
        { 0.0f, 2.1f, 0.0f },
        0.0f,
        94,
        0,
        2,
        0,
        { 0.5f, 2.0f },
    };

    namespace PatChase
    {
        static const xFXRibbon::curve_node curvedata[5] = {
            { 0.0f, { 0xF5, 0xA6, 0xA1, 0xC8 }, 1.0f },
            { 0.35f, { 0xF5, 0xA6, 0xA1, 0x50 }, 0.5f },
            { 0.55f, { 0xF5, 0xA6, 0xA1, 0x3C }, 0.25f },
            { 0.8f, { 0xF5, 0xA6, 0xA1, 0x14 }, 0.1f },
            { 1.0f, { 0xF5, 0xA6, 0xA1, 0x00 }, 0.0f },
        };

        NMEHelpRib::RibData rib_PatChase_1 = {
            { 1, 1, 0, 0 },
            { "tr02_cin_patstreak", (xFXRibbon::curve_node*)curvedata, 5, 0.35f, 1.0f },
            { 1, 0.35f, 0.2f },
            { NULL, 0 },
        };

        NMEHelpRib::RibData rib_PatChase_2 = {
            { 1, 1, 0, 0 },
            { "tr02_cin_patstreak", (xFXRibbon::curve_node*)curvedata, 5, 0.2f, 1.0f },
            { 1, 0.17f, 0.15f },
            { NULL, 0 },
        };

        NMEHelpRib::RibData rib_PatChase_3 = {
            { 1, 1, 0, 0 },
            { "tr02_cin_patstreak", (xFXRibbon::curve_node*)curvedata, 5, 0.55f, 1.0f },
            { 1, 0.7f, 1.0f },
            { NULL, 0 },
        };

        NMEHelpRib::RibData rib_PatChase_4 = {
            { 1, 1, 0, 0 },
            { "tr02_cin_patstreak", (xFXRibbon::curve_node*)curvedata, 5, 0.35f, 1.0f },
            { 1, 0.7f, 0.5f },
            { NULL, 0 },
        };

    } // namespace PatChase

    static NCINEntry g_TR02_cin_popperIntro[13] = {
        {
            { 0x1000, 1.3333334f, 1.6666667f, "Popper Appear 1 - 0 Ground" },
            { (NCINUpdFunc)NCIN_PopGround_Upd, (NCINAnimFunc)NCIN_PopGround_AR, NULL,
              (NCINTweakFunc)NCIN_PopGround_Twk },
            { 1, -1, 0, 0 },
            { { { -95.5f, 1.32f, 25.145f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_gasGround },
        },
        {
            { 0xFF0001, 3.746667f, 4.066667f, "Patrick Boo" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)zCinFX_BubSlamBone_AR, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { -95.5f, 1.32f, 25.145f }, { 0.0f, 0.0f, 0.0f } },
              { { 128.0f, 8.0f, 2.5f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 3.7333336f, 4.3333335f, "Popper Evade 1" },
            { (NCINUpdFunc)NCIN_PopGround_Upd, (NCINAnimFunc)NCIN_PopGround_AR, NULL,
              (NCINTweakFunc)NCIN_PopGround_Twk },
            { 1, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_gasGround },
        },
        {
            { 0x1002, 7.833334f, 8.266667f, "Pat Pursue 1a" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 0, 10, 53, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &PatChase::rib_PatChase_1 },
        },
        {
            { 0x1002, 9.166667f, 10.000001f, "Pat Pursue 2" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 0, 10, 53, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &PatChase::rib_PatChase_2 },
        },
        {
            { 0x1001, 15.833334f, 17.333334f, "Popper Evade 2" },
            { (NCINUpdFunc)NCIN_PopGround_Upd, (NCINAnimFunc)NCIN_PopGround_AR, NULL,
              (NCINTweakFunc)NCIN_PopGround_Twk },
            { 1, 1, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_gasGround },
        },
        {
            { 0x1003, 18.533335f, 18.733334f, "Heavy Breath 1" },
            { (NCINUpdFunc)NCIN_HuffNPuff_Upd, (NCINAnimFunc)NCIN_HuffNPuff_AR, NULL,
              (NCINTweakFunc)NCIN_HuffNPuff_Twk },
            { 0, 15, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_huffNPuff_01 },
        },
        {
            { 0x1003, 19.900002f, 20.133335f, "Heavy Breath 2" },
            { (NCINUpdFunc)NCIN_HuffNPuff_Upd, (NCINAnimFunc)NCIN_HuffNPuff_AR, NULL,
              (NCINTweakFunc)NCIN_HuffNPuff_Twk },
            { 0, 15, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_huffNPuff_02 },
        },
        {
            { 0x1001, 20.933334f, 21.400002f, "Popper Appear 2" },
            { (NCINUpdFunc)NCIN_PopGround_Upd, (NCINAnimFunc)NCIN_PopGround_AR, NULL,
              (NCINTweakFunc)NCIN_PopGround_Twk },
            { 1, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_gasGround },
        },
        {
            { 0x1004, 22.333334f, 22.733334f, "Pop Shot Launch" },
            { (NCINUpdFunc)NCIN_PopRockLaunch_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { -106.153f, 2.095f, 32.974f }, { -96.309f, 1.8f, 27.534f } },
              { { 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1004, 20.000002f, 23.666668f, "Pop Shot Impact" },
            { (NCINUpdFunc)NCIN_PopRockLaunch_Upd, NULL, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { -101.0f, 2.4f, 35.4f }, { -96.309f, 1.85f, 27.534f } },
              { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0xFF0003, 23.333334f, 24.066668f, "Patrick Rockets" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 250.0f } },
              { { 0.0f, 2.0f, 0.0f }, { 0.45f, 0.5f, 0.83f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace IntroPopper

void zCineFX_Register_IntroPopper()
{
    static NCINMap cutmap[] = {
        { "enemy_popper_intro", IntroPopper::g_TR02_cin_popperIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
