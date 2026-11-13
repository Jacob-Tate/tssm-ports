#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zNMEParFX.h"

namespace CinFXDenOne
{
    namespace Poofy
    {
        PuffEmitter den_land = {
            NPAR_TYP_DUSTPUFF,
            0.5f,
            { 0x6E, 0x78, 0x64, 0x64 },
            { 0.1f, 0.5f },
            { 0.0f, 5.0f, 0.0f },
            3.84f,
            93,
            0,
            25,
            0,
            { 0.1f, 4.5f },
        };

        PuffEmitter boot_drop = {
            NPAR_TYP_DUSTPUFF,
            1.5f,
            { 0x6E, 0x6E, 0x5A, 0xC8 },
            { 0.3f, 1.6f },
            { 0.0f, 10.0f, 0.0f },
            10.84f,
            95,
            0,
            15,
            0,
            { 1.5f, 4.5f },
        };

    } // namespace Poofy

    static NCINEntry g_B201_DenOneIntro[3] = {
        {
            { 0x1001, 24.500002f, 24.833334f, "Grab Mustache" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 31, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1000.0f } },
              { { 0.0f, 0.0f, -2.0f }, { 0.3f, 0.5f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1002, 28.000002f, 28.500002f, "SB Plucked" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 2, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1000.0f } },
              { { 0.0f, -1.0f, 0.0f }, { 0.35f, 0.5f, 0.25f } } },
            { NULL },
        },
        { 0 },
    };

    static NCINEntry g_B201_DenOneDeath[8] = {
        {
            { 0x1003, 0.9333334f, 1.0666667f, "Dennis Lands Right" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 50, 0, 0 },
            { { { 0.0f, -0.25f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Poofy::den_land },
        },
        {
            { 0x1003, 1.0f, 1.1f, "Dennis Lands Left" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 48, 0, 0 },
            { { { 0.0f, -0.25f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Poofy::den_land },
        },
        {
            { 0x1004, 3.0000002f, 3.8333335f, "Other Foot Drops" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 2, 0, 0 },
            { { { 0.0f, 1.0f, -2.0f }, { 0.0f, 0.0f, 400.0f } },
              { { 0.0f, -3.0f, 0.0f }, { 2.25f, 2.0f, 0.9f } } },
            { NULL },
        },
        {
            { 0x1004, 3.0000002f, 3.8333335f, "Other Foot Drops" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 2, 0, 0 },
            { { { 0.0f, 1.0f, -2.0f }, { 0.0f, 0.0f, 400.0f } },
              { { 0.0f, -3.0f, 0.0f }, { 2.25f, 2.0f, 1.2f } } },
            { NULL },
        },
        {
            { 0x1004, 3.0000002f, 3.8333335f, "Other Foot Drops" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 1, 2, 0, 0 },
            { { { 0.0f, 1.0f, -2.0f }, { 0.0f, 0.0f, 400.0f } },
              { { 0.0f, -3.0f, 0.0f }, { 2.25f, 2.0f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1005, 3.5000002f, 3.6666667f, "Boot Dust Heel" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 1, 2, 0, 0 },
            { { { 0.0f, -0.5f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Poofy::boot_drop },
        },
        {
            { 0x1005, 3.5666668f, 3.7333336f, "Boot Dust Toe" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 1, 2, 0, 0 },
            { { { 0.0f, -0.5f, -2.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Poofy::boot_drop },
        },
        { 0 },
    };
} // namespace CinFXDenOne

void zCineFX_Register_BossDenOne()
{
    static NCINMap cutmap[] = {
        { "boss_dennis01_intro", CinFXDenOne::g_B201_DenOneIntro, 0 },
        { "boss_dennis1_death", CinFXDenOne::g_B201_DenOneDeath, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
