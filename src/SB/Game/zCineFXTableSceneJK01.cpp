#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath3.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zNMEParFX.h"

namespace CineFXSceneJK01
{

    static void NCIN_ImpactExhaust_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
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

    static void NCIN_ImpactExhaust_Twk(const char*, NCINEntry*)
    {
    }

    namespace LeapDust
    {
        PuffEmitter dust_small = {
            NPAR_TYP_DUSTPUFF,
            0.55f,
            { 0xAA, 0xAA, 0xAA, 0x64 },
            { 0.2f, 0.3f },
            { 0.0f, 3.5f, 0.0f },
            3.5f,
            96,
            0,
            20,
            0,
            { 0.15f, 1.5f },
        };

    } // namespace LeapDust

    namespace Plume
    {
        ExhaustCurve curve_data[5] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.5f },
            { 0.1f, { 0xC8, 0x64, 0x19, 0x7F }, 0.35f },
            { 0.2f, { 0xC8, 0x64, 0x32, 0x7F }, 0.75f },
            { 0.5f, { 0x3C, 0x64, 0x50, 0x57 }, 1.0f },
            { 1.0f, { 0x1E, 0x46, 0x1E, 0x00 }, 1.0f },
        };

        ExhaustEmitter impactPlume = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { -8.0f, 0.0f, 6.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.0f, 1.0f },
            { 2, 1, 1.0f, -1.0f },
            { 30.0f, 10.0f, 1.5f, 1.5f, -18.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 5, 1.0f, 5.0f },
        };

    } // namespace Plume

    namespace CoreBoom
    {
        ExhaustCurve curve_data[3] = {
            { 0.0f, { 0x8C, 0x5A, 0x00, 0x00 }, 1.0f },
            { 0.5f, { 0xFA, 0xF0, 0x19, 0xFF }, 1.0f },
            { 1.0f, { 0x3C, 0x3C, 0x3C, 0x00 }, 1.0f },
        };

        ExhaustEmitter coreBoom = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.25f, 0.0f },
            { 10, 0, 15.0f, -5.0f },
            { 15.0f, -10.0f, 0.0f, -10.0f, 0.0f, 16.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 3, 1.0f, 7.0f },
        };

    } // namespace CoreBoom

    namespace HotCore
    {
        ExhaustCurve curve_data[5] = {
            { 0.0f, { 0xFA, 0xFA, 0xC8, 0x00 }, 1.0f }, { 0.1f, { 0xFA, 0xF0, 0x19, 0x7F }, 1.0f },
            { 0.2f, { 0xFA, 0x8C, 0x32, 0x7F }, 1.0f }, { 0.4f, { 0xBE, 0x00, 0x00, 0x57 }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 1.0f },
        };

        ExhaustEmitter hotCore = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { -10.0f, 0.0f, 10.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 1.0f },
            { 4, 2, 10.0f, 0.0f },
            { 9.0f, 9.0f, -2.0f, -3.0f, -10.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 5, 1.0f, 8.0f },
        };

    } // namespace HotCore

    namespace Smolder
    {
        ExhaustCurve curve_data[5] = {
            { 0.0f, { 0xFA, 0xFA, 0xC8, 0x00 }, 1.0f }, { 0.1f, { 0x32, 0x28, 0x19, 0x7F }, 1.0f },
            { 0.2f, { 0x32, 0x28, 0x32, 0x7F }, 1.0f }, { 0.4f, { 0x32, 0x32, 0x32, 0x57 }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.0f },
        };

        ExhaustEmitter smolder = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 1.2f, 1.8f },
            { 1, 1, 0.8f, -0.1f },
            { 4.0f, 5.0f, 0.0f, 5.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 5, 1.0f, 10.0f },
        };

    } // namespace Smolder

    static NCINEntry g_cinfx_JK01_tvcrash[27] = {
        {
            { 0x1001, 0.0f, 2.1666667f, "Fruit Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f } },
              { { 0.0f, 1.0f, 1.0f }, { 0.5f, 0.35f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1001, 2.3333335f, 2.966667f, "Fruit Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 100.0f } },
              { { 0.0f, 0.0f, 5.0f }, { 1.0f, 1.0f, 5.0f } } },
            { NULL },
        },
        {
            { 0x1002, 2.8666668f, 3.1666667f, "Impact Plume" },
            { (NCINUpdFunc)NCIN_ImpactExhaust_Upd, NULL, NULL,
              (NCINTweakFunc)NCIN_ImpactExhaust_Twk },
            { 0, 0, 0, 0 },
            { { { 151.0f, 30.0f, 186.0f }, { 0.0f, 0.0f, -1.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Plume::impactPlume },
        },
        {
            { 0x1003, 2.966667f, 3.1666667f, "Core Explode" },
            { (NCINUpdFunc)NCIN_ImpactExhaust_Upd, NULL, NULL,
              (NCINTweakFunc)NCIN_ImpactExhaust_Twk },
            { 0, 0, 0, 0 },
            { { { 150.0f, 20.0f, 200.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &CoreBoom::coreBoom },
        },
        {
            { 0x1004, 3.1666667f, 13.333334f, "Core Flames" },
            { (NCINUpdFunc)NCIN_ImpactExhaust_Upd, NULL, NULL,
              (NCINTweakFunc)NCIN_ImpactExhaust_Twk },
            { 0, 0, 0, 0 },
            { { { 150.0f, 20.0f, 200.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &HotCore::hotCore },
        },
        {
            { 0x1005, 7.0000005f, 13.333334f, "Core Smoke" },
            { (NCINUpdFunc)NCIN_ImpactExhaust_Upd, NULL, NULL,
              (NCINTweakFunc)NCIN_ImpactExhaust_Twk },
            { 0, 0, 0, 0 },
            { { { 150.0f, 25.0f, 170.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smolder::smolder },
        },
        {
            { 0x1006, 2.966667f, 3.8333335f, "TV Bits-n-Bubs - 13" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 13, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 500.0f } },
              { { 0.0f, 0.0f, 25.0f }, { 20.0f, 5.0f, 4.0f } } },
            { NULL },
        },
        {
            { 0x1007, 6.266667f, 6.5000005f, "Leap Dust Pat 188-195" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 6.6000004f, 6.8333335f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 7.3333335f, 7.5000005f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 7.4000006f, 7.666667f, "Leap Dust Pat" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 8.0f, 8.166667f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 8.333334f, 8.5f, "Leap Dust Pat - 250" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 8.566668f, 8.766667f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 9.0f, 9.266667f, "Leap Dust Pat - 270" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 9.266667f, 9.433333f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 9.666667f, 9.900001f, "Leap Dust Pat - 290" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 10.000001f, 10.166667f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 10.666667f, 10.966667f, "Leap Dust Pat - 320" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 10.766667f, 10.966667f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 11.666667f, 11.866668f, "Leap Dust Pat - 350" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 11.666667f, 11.866668f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 12.333334f, 12.566668f, "Leap Dust Pat - 370" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 12.333334f, 12.566668f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 13.1f, 13.266667f, "Leap Dust Pat - 393" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        {
            { 0x1007, 13.1f, 13.266667f, "Leap Dust SB" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &LeapDust::dust_small },
        },
        { 0 },
    };
} // namespace CineFXSceneJK01

void zCineFX_Register_SceneJK01()
{
    static NCINMap cutmap[] = {
        { "tv_explode_cine", CineFXSceneJK01::g_cinfx_JK01_tvcrash, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
