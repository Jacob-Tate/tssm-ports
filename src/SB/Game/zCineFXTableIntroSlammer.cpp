#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xFX.h"

#include "zNMEParFX.h"
#include "zNMESupport.h"

namespace IntroSlammer
{
    namespace Swipe
    {
        static const xFXRibbon::curve_node curvedata[5] = {
            { 0.0f, { 0xFF, 0x9B, 0x00, 0x55 }, 0.0f }, { 0.2f, { 0xFF, 0x64, 0xFF, 0x55 }, 0.8f },
            { 0.5f, { 0xFF, 0xFF, 0x00, 0x55 }, 1.0f }, { 0.8f, { 0xFF, 0x64, 0xFF, 0x55 }, 0.8f },
            { 1.0f, { 0xFF, 0x9B, 0x00, 0x55 }, 0.0f },
        };

        NMEHelpRib::RibData rib_swipe = {
            { 1, 1, 0, 0 },
            { "fx_streak2", (xFXRibbon::curve_node*)curvedata, 5, 0.75f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

    } // namespace Swipe

    namespace Wham
    {
        PuffEmitter bambam = {
            NPAR_TYP_DUSTPUFF,
            0.85f,
            { 0x96, 0x96, 0x96, 0x64 },
            { 0.25f, 0.75f },
            { 0.0f, 10.0f, 0.0f },
            1.84f,
            93,
            0,
            25,
            0,
            { 0.35f, 2.5f },
        };

        PuffEmitter bambam_big = {
            NPAR_TYP_DUSTPUFF,
            1.5f,
            { 0x96, 0x96, 0x96, 0x64 },
            { 0.5f, 2.0f },
            { 0.0f, 10.0f, 0.0f },
            3.84f,
            93,
            0,
            25,
            0,
            { 0.35f, 4.5f },
        };

    } // namespace Wham

    static NCINEntry g_DE01_SlammerIntro[7] = {
        {
            { 0x1001, 3.3333335f, 4.0f, "Slam Strike" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 3, 20, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Swipe::rib_swipe },
        },
        {
            { 0x1003, 3.9333336f, 4.166667f, "Slam Strike Poof" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 3, 20, 0, 0 },
            { { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Wham::bambam },
        },
        {
            { 0x1002, 5.3333335f, 6.166667f, "Slam Swipe" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 3, 20, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Swipe::rib_swipe },
        },
        {
            { 0x1002, 9.666667f, 10.333334f, "Slam Swipe" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 3, 20, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Swipe::rib_swipe },
        },
        {
            { 0x1001, 13.333334f, 13.500001f, "Slam Strike" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 3, 20, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Swipe::rib_swipe },
        },
        {
            { 0x1003, 13.500001f, 13.666667f, "Slam Strike Dust" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 4, 8, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Wham::bambam_big },
        },
        { 0 },
    };
} // namespace IntroSlammer

void zCineFX_Register_IntroSlammer()
{
    static NCINMap cutmap[] = {
        { "enemy_slammer_intro", IntroSlammer::g_DE01_SlammerIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
