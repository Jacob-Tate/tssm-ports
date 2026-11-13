#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zNMEParFX.h"

namespace CineFXSceneDE01
{
    namespace Smoke
    {
        PuffEmitter tower_smoke = {
            NPAR_TYP_DUSTPUFF,
            4.0f,
            { 0x96, 0x96, 0x96, 0x8C },
            { 0.3f, 0.7f },
            { 0.0f, 2.0f, 0.0f },
            3.5f,
            96,
            0,
            12,
            0,
            { 3.5f, 0.5f },
        };

        PuffEmitter tower_smokebig = {
            NPAR_TYP_DUSTPUFF,
            0.35f,
            { 0xC8, 0xC8, 0xC8, 0x8C },
            { 0.7f, 0.9f },
            { 0.0f, 0.0f, 0.0f },
            3.5f,
            96,
            0,
            120,
            0,
            { 3.5f, 15.0f },
        };

        PuffEmitter generator_smoke = {
            NPAR_TYP_DUSTPUFF,
            2.5f,
            { 0x96, 0x96, 0x96, 0x8C },
            { 0.3f, 0.7f },
            { 0.0f, 3.0f, 0.0f },
            3.5f,
            96,
            0,
            7,
            0,
            { 1.5f, -1.1f },
        };

        PuffEmitter generator_fire = {
            NPAR_TYP_DUSTPUFF,
            1.5f,
            { 0xFF, 0x90, 0x00, 0x8C },
            { 0.3f, 0.7f },
            { 0.0f, 3.0f, 0.0f },
            3.5f,
            96,
            0,
            5,
            0,
            { 1.2f, -1.1f },
        };

        PuffEmitter plank_smoke = {
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
            { 1.0f, 4.0f },
        };

        PuffEmitter yolkel_smoke = {
            NPAR_TYP_DUSTPUFF,
            1.0f,
            { 0xC8, 0xC8, 0xC8, 0x8C },
            { 0.5f, 1.0f },
            { 0.0f, 10.0f, 0.0f },
            3.5f,
            96,
            0,
            20,
            0,
            { 1.5f, 2.5f },
        };

    } // namespace Smoke

    static NCINEntry g_cinfx_DE01_tower[11] = {
        {
            { 0x1002, 4.166667f, 16.5f, "Generator Smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 1.1f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::generator_smoke },
        },
        {
            { 0x1002, 4.166667f, 16.5f, "Generator Fire" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 1.1f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::generator_fire },
        },
        {
            { 0x1001, 8.066668f, 8.666667f, "Yolkels Die" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::yolkel_smoke },
        },
        {
            { 0x1002, 9.733334f, 9.933333f, "Plank smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 1.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::plank_smoke },
        },
        {
            { 0x1002, 11.433334f, 11.900001f, "Plank smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 1.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::plank_smoke },
        },
        {
            { 0x1002, 11.566668f, 12.066668f, "Plank smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 4.4f, 0.0f, -1.3f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::plank_smoke },
        },
        {
            { 0x1002, 11.166667f, 16.5f, "Tower Smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::tower_smoke },
        },
        {
            { 0x1002, 11.700001f, 12.200001f, "Plank smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 1.6f, 0.0f, 5.9f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::plank_smoke },
        },
        {
            { 0x1002, 11.766667f, 12.266667f, "Plank smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 5.0f, 0.0f, -0.5f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::plank_smoke },
        },
        {
            { 0x1002, 11.833334f, 12.033334f, "Tower Big Smoke" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Smoke::tower_smokebig },
        },
        { 0 },
    };
} // namespace CineFXSceneDE01

void zCineFX_Register_SceneDE01()
{
    static NCINMap cutmap[] = {
        { "cin_yolkel_crushed", CineFXSceneDE01::g_cinfx_DE01_tower, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
