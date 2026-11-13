#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zNMEParFX.h"

namespace IntroFogger
{
    namespace Dusty
    {
        PuffEmitter stage_lightly = {
            NPAR_TYP_DUSTPUFF,
            0.55f,
            { 0xC8, 0xC8, 0xC8, 0x8C },
            { 0.5f, 1.0f },
            { 0.0f, 10.0f, 0.0f },
            3.5f,
            96,
            0,
            10,
            0,
            { 1.5f, 2.5f },
        };

        PuffEmitter stage_main = {
            NPAR_TYP_DUSTPUFF,
            1.25f,
            { 0x96, 0x96, 0x96, 0x64 },
            { 0.5f, 2.0f },
            { 0.0f, 10.0f, 0.0f },
            3.84f,
            93,
            0,
            10,
            0,
            { 2.5f, 4.5f },
        };

        PuffEmitter stage_lights = {
            NPAR_TYP_DUSTPUFF,
            1.54f,
            { 0xC8, 0xC8, 0xC8, 0x50 },
            { 0.75f, 2.75f },
            { 0.0f, 20.0f, 0.0f },
            3.84f,
            93,
            0,
            10,
            0,
            { 3.0f, -2.5f },
        };

    } // namespace Dusty

    namespace Breath
    {
        FogBreathEmitter breathCore = {
            NPAR_TYP_FOGBREATH,
            2.23f,
            { 0x65, 0x84, 0x00, 0xC8 },
            { 0.5f, 2.4f },
            { 0.0f, 4.5f, 0.0f },
            0.5f,
            85,
            0,
            0,
            0,
            { 4, -3.0f, -5.0f, { 0.1f, 1.35f, 1.0f } },
        };

        FogBreathEmitter breathExtra = {
            NPAR_TYP_FOGBREATH,
            2.25f,
            { 0x73, 0xC8, 0x00, 0x80 },
            { 0.25f, 0.8f },
            { 0.0f, 4.5f, 0.0f },
            4.5f,
            87,
            0,
            0,
            0,
            { 4, -4.0f, -5.0f, { 0.5f, 0.35f, 1.0f } },
        };

    } // namespace Breath

    static NCINEntry g_BB03_FoggerIntro[9] = {
        {
            { 0x1001, 11.333334f, 11.666667f, "Pat Falls" },
            { (NCINUpdFunc)zCinFX_CycleOnce_Upd, (NCINAnimFunc)zCinFX_BubBurst_AR, NULL,
              (NCINTweakFunc)zCinFX_BubBurst_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1002, 19.233334f, 20.000002f, "Fogger Belch Base" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEBreathSpew_AR, NULL,
              (NCINTweakFunc)NCIN_NMEBreathSpew_Twk },
            { 2, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Breath::breathCore },
        },
        {
            { 0x1002, 19.233334f, 20.000002f, "Fogger Belch Chunks" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEBreathSpew_AR, NULL,
              (NCINTweakFunc)NCIN_NMEBreathSpew_Twk },
            { 2, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Breath::breathExtra },
        },
        {
            { 0x1003, 20.400002f, 20.666668f, "Stage Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 4, 3, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dusty::stage_lightly },
        },
        {
            { 0x1003, 21.333334f, 21.666668f, "Stage Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 4, 4, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dusty::stage_main },
        },
        {
            { 0x1003, 21.833334f, 22.333334f, "Stage Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 4, 2, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dusty::stage_lightly },
        },
        {
            { 0x1003, 22.166668f, 22.666668f, "Stage Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 4, 5, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dusty::stage_lightly },
        },
        {
            { 0x1003, 24.500002f, 25.000002f, "Stage Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 6, 6, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dusty::stage_lights },
        },
        { 0 },
    };
} // namespace IntroFogger

void zCineFX_Register_IntroFogger()
{
    static NCINMap cutmap[] = {
        { "enemy_fogger_intro", IntroFogger::g_BB03_FoggerIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
