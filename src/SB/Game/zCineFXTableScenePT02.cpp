#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zNMEParFX.h"

namespace CineFXScenePT02
{
    PuffEmitter g_carleap_01 = {
        NPAR_TYP_DUSTPUFF,
        1.2f,
        { 0xB4, 0xB4, 0xB4, 0x50 },
        { 4.0f, 4.8f },
        { 0.0f, 10.0f, 0.0f },
        20.0f,
        93,
        0,
        10,
        0,
        { 1.5f, 5.0f },
    };

    static NCINEntry g_PT02_carleap[4] = {
        {
            { 0x1003, 0.0f, 2.6666667f, "Prop Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubTrailBoneSimp_AR, NULL,
              (NCINTweakFunc)zCinFX_BubTrailBoneSimp_Twk },
            { 0, 12, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 5.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f } } },
            { NULL },
        },
        {
            { 0x1002, 0.0f, 2.6666667f, "Wake Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 29, 0, 0 },
            { { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 25.0f } },
              { { 0.0f, 0.0f, 3.0f }, { 1.5f, 0.5f, 0.0f } } },
            { NULL },
        },
        {
            { 0x1001, 0.0f, 0.6f, "Dust Cloud" },
            { (NCINUpdFunc)zCinFX_GenNCINInput_Upd, (NCINAnimFunc)NCIN_NMEPuffPropWash_AR, NULL,
              (NCINTweakFunc)NCIN_NMEPuffPropWash_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -10.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_carleap_01 },
        },
        { 0 },
    };
} // namespace CineFXScenePT02

void zCineFX_Register_ScenePT02()
{
    static NCINMap cutmap[] = {
        { "planktopolis_drive", CineFXScenePT02::g_PT02_carleap, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
