#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zNMEParFX.h"

namespace CineFXScenePT01
{
    namespace Dust
    {
        PuffEmitter statue_dust = {
            NPAR_TYP_DUSTPUFF,
            0.75f,
            { 0xC8, 0xC8, 0xC8, 0x8C },
            { 0.5f, 1.0f },
            { 0.0f, 10.0f, 0.0f },
            3.5f,
            96,
            0,
            20,
            0,
            { 1.0f, 3.0f },
        };

        PuffEmitter camera_dust = {
            NPAR_TYP_DUSTPUFF,
            2.75f,
            { 0xC8, 0xC8, 0xC8, 0x8C },
            { 0.5f, 1.0f },
            { 0.0f, 2.0f, 0.0f },
            2.5f,
            96,
            0,
            20,
            0,
            { 1.0f, 1.0f },
        };

    } // namespace Dust

    static NCINEntry g_cinfx_PT01_statue[7] = {
        {
            { 0x1001, 4.266667f, 4.766667f, "Statue Pieces Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { -11.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dust::statue_dust },
        },
        {
            { 0x1001, 4.3333335f, 4.8333335f, "Statue Pieces Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { -8.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dust::statue_dust },
        },
        {
            { 0x1001, 4.9f, 5.4f, "Statue Pieces Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { -1.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dust::statue_dust },
        },
        {
            { 0x1001, 5.066667f, 5.566667f, "Statue Pieces Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dust::statue_dust },
        },
        {
            { 0x1001, 5.1333337f, 5.6333337f, "Statue Pieces Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { 3.0f, 0.0f, 8.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dust::statue_dust },
        },
        {
            { 0x1001, 4.3333335f, 6.666667f, "Statue Pieces Falls" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 2, 0, 0, 0 },
            { { { -3.0f, 0.0f, 13.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Dust::camera_dust },
        },
        { 0 },
    };
} // namespace CineFXScenePT01

void zCineFX_Register_ScenePT01()
{
    static NCINMap cutmap[] = {
        { "planktopolis", CineFXScenePT01::g_cinfx_PT01_statue, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
