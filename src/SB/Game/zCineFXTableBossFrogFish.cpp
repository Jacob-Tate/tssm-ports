#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zNMEParFX.h"

namespace CinFXFrogFish
{
    static void NCIN_FrogPuffPropWash_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                         RwMatrixTag*, U32 animIndex, U32)
    {
        xVec3 pos_emit;
        xVec3 vel_drift;

        if (animIndex != fxrec->index.idx_anim)
        {
            return;
        }

        pos_emit = fxrec->basic.pos_A[0];
        vel_drift = fxrec->basic.pos_A[1];

        ((PuffEmitter*)fxrec->input.fxparm)
            ->RoundRing(&pos_emit, fxrec->basic.pos_B[0].x, &vel_drift);
    }

    static void NCIN_FrogPuffPropWash_Twk(const char*, NCINEntry*)
    {
    }

    namespace Wagon
    {
        PuffEmitter carhalt = {
            NPAR_TYP_DUSTPUFF,
            2.2f,
            { 0x4B, 0x4B, 0x4B, 0x8C },
            { 1.0f, 2.2f },
            { 0.0f, 10.0f, 0.0f },
            10.0f,
            93,
            0,
            2,
            0,
            { 1.25f, -3.0f },
        };

    } // namespace Wagon

    namespace HappyBob
    {
        PuffEmitter ice_scream_skip = {
            NPAR_TYP_DUSTPUFF,
            1.1f,
            { 0x64, 0x8C, 0x64, 0x50 },
            { 0.15f, 0.35f },
            { 0.0f, 1.0f, 0.0f },
            1.0f,
            93,
            0,
            1,
            0,
            { 0.15f, 0.5f },
        };

    } // namespace HappyBob

    namespace Froggy
    {
        PuffEmitter emerge = {
            NPAR_TYP_DUSTPUFF,
            1.34f,
            { 0x64, 0x8C, 0x64, 0x96 },
            { 1.25f, 2.75f },
            { 0.0f, -5.0f, 0.0f },
            1.0f,
            93,
            0,
            10,
            0,
            { 10.1f, 4.5f },
        };

    } // namespace Froggy

    namespace Bridge
    {
        PuffEmitter fall_clouds = {
            NPAR_TYP_DUSTPUFF,
            1.54f,
            { 0x96, 0x96, 0x96, 0x78 },
            { 1.25f, 1.75f },
            { 0.0f, 5.0f, 0.0f },
            0.25f,
            95,
            0,
            2,
            0,
            { 1.5f, 1.1f },
        };

    } // namespace Bridge

    namespace RabbitHole
    {
        PuffEmitter dustpoof = {
            NPAR_TYP_DUSTPUFF,
            1.1f,
            { 0x64, 0x8C, 0x64, 0x50 },
            { 0.15f, 0.65f },
            { 0.0f, -4.0f, 0.0f },
            1.0f,
            93,
            0,
            5,
            0,
            { 0.25f, 2.5f },
        };

    } // namespace RabbitHole

    static NCINEntry g_B101_FrogFishIntro[12] = {
        {
            { 0x1001, 0.0f, 1.3333334f, "Prop Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubTrailBoneSimp_AR, NULL,
              (NCINTweakFunc)zCinFX_BubTrailBoneSimp_Twk },
            { 3, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 2.0f, 0.0f, 0.0f }, { 0.3f, 0.3f, 0.3f } } },
            { NULL },
        },
        {
            { 0x1001, 5.8f, 6.666667f, "Prop Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubTrailBoneSimp_AR, NULL,
              (NCINTweakFunc)zCinFX_BubTrailBoneSimp_Twk },
            { 3, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 2.0f, 0.0f, 0.0f }, { 0.3f, 0.3f, 0.3f } } },
            { NULL },
        },
        {
            { 0x1004, 6.166667f, 6.666667f, "Halt Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubTrailBoneSimp_AR, NULL,
              (NCINTweakFunc)zCinFX_BubTrailBoneSimp_Twk },
            { 3, 18, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 12.0f, 0.0f, 0.0f }, { 1.5f, 1.5f, 1.5f } } },
            { NULL },
        },
        {
            { 0x1002, 6.166667f, 6.666667f, "Halt Dust Cloud" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 1, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Wagon::carhalt },
        },
        {
            { 0x1005, 19.000002f, 20.833334f, "Happy Bob" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 1, 1, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &HappyBob::ice_scream_skip },
        },
        {
            { 0x1006, 38.000004f, 38.333336f, "Frogfish Emerges" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_FrogPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_FrogPuffPropWash_Twk },
            { 3, 1, 0, 0 },
            { { { -4.0f, 1.0f, -40.0f }, { 0.0f, 2.5f, 0.0f } },
              { { 2.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Froggy::emerge },
        },
        {
            { 0x1006, 38.333336f, 38.666668f, "Frogfish Emerges" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_FrogPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_FrogPuffPropWash_Twk },
            { 3, 1, 0, 0 },
            { { { -4.0f, 1.0f, -40.0f }, { 0.0f, 2.5f, 0.0f } },
              { { 3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Froggy::emerge },
        },
        {
            { 0x1006, 38.666668f, 39.333336f, "Frogfish Emerges" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_FrogPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_FrogPuffPropWash_Twk },
            { 3, 1, 0, 0 },
            { { { -4.0f, 1.0f, -40.0f }, { 0.0f, 2.5f, 0.0f } },
              { { 4.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Froggy::emerge },
        },
        {
            { 0x1006, 39.333336f, 40.666668f, "Frogfish Emerges" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_FrogPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_FrogPuffPropWash_Twk },
            { 3, 1, 0, 0 },
            { { { -4.0f, 1.0f, -40.0f }, { 0.0f, 2.5f, 0.0f } },
              { { 5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Froggy::emerge },
        },
        {
            { 0x1007, 42.06667f, 43.833336f, "SB Tossed Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 2, 1, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.35f, 0.5f, 0.25f } } },
            { NULL },
        },
        {
            { 0x1008, 45.000004f, 47.166668f, "Bridge Collapse" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 13, 1, 0, 0 },
            { { { 0.0f, 0.0f, 4.0f }, { 0.0f, 4.5f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Bridge::fall_clouds },
        },
        { 0 },
    };

    static NCINEntry g_B101_FrogFishDeath[9] = {
        {
            { 0x1009, 2.1333334f, 2.5666668f, "Huggy Granny" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 1, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.35f, 0.5f, 0.25f } } },
            { NULL },
        },
        {
            { 0x100A, 3.6666667f, 4.0f, "Rabbit Hole" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 1, 3, 0, 0 },
            { { { 0.0f, -0.2f, 0.0f }, { 0.0f, -5.5f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &RabbitHole::dustpoof },
        },
        {
            { 0x100B, 5.166667f, 5.8333335f, "Rabbit Pull" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_FrogPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_FrogPuffPropWash_Twk },
            { 1, 0, 0, 0 },
            { { { 0.9f, 1.7f, 1.3f }, { 0.0f, 1.5f, 0.0f } },
              { { 0.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &RabbitHole::dustpoof },
        },
        {
            { 0x100A, 5.5000005f, 5.8333335f, "Rabbit Hole" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 1, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 3.5f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &RabbitHole::dustpoof },
        },
        {
            { 0x100C, 5.666667f, 6.0000005f, "Pat Yanked Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 250.0f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.35f, 0.5f, 0.25f } } },
            { NULL },
        },
        {
            { 0x100D, 6.5000005f, 8.0f, "SB Thrown Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 150.0f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.35f, 0.5f, 0.25f } } },
            { NULL },
        },
        {
            { 0x100E, 6.666667f, 8.666667f, "Pat Thrown Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 150.0f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.35f, 0.5f, 0.5f } } },
            { NULL },
        },
        {
            { 0x100E, 9.333334f, 9.533334f, "Pat Thrown Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 350.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.45f, 0.5f, 0.35f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace CinFXFrogFish

void zCineFX_Register_BossFrogFish()
{
    static NCINMap cutmap[] = {
        { "boss_frogfish_intro", CinFXFrogFish::g_B101_FrogFishIntro, 0 },
        { "boss_frogfish_death", CinFXFrogFish::g_B101_FrogFishDeath, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
