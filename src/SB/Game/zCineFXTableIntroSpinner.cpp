#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xFX.h"

#include "zNMESupport.h"

namespace IntroSpinner
{
    namespace Spinning
    {
        static const xFXRibbon::curve_node curvedata[4] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0xFF }, 1.0f },
            { 0.35f, { 0xFF, 0x46, 0x46, 0xC8 }, 0.9f },
            { 0.8f, { 0xFF, 0x8C, 0x46, 0x96 }, 0.3f },
            { 1.0f, { 0xFF, 0xFF, 0x00, 0x32 }, 0.0f },
        };

        NMEHelpRib::RibData rib_spin = {
            { 1, 1, 0, 0 },
            { "fx_spinner_swirl", (xFXRibbon::curve_node*)curvedata, 4, 0.35f, 0.6f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

    } // namespace Spinning

    static NCINEntry g_TT01_SpinnerIntro[3] = {
        {
            { 0x1002, 14.833334f, 16.333334f, "Worm Streak" },
            { (NCINUpdFunc)NCIN_NMERibHelp_Upd, (NCINAnimFunc)NCIN_NMERibHelp_AR, NULL,
              (NCINTweakFunc)NCIN_NMERibHelp_Twk },
            { 2, 27, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.075f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &Spinning::rib_spin },
        },
        {
            { 0x1003, 15.400001f, 17.5f, "Turbulent Bubbles" },
            { (NCINUpdFunc)zCinFX_BubbleTrail_Upd, (NCINAnimFunc)zCinFX_BubbleTrail_AR, NULL,
              (NCINTweakFunc)zCinFX_BubbleTrail_Twk },
            { 0, 17, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 300.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.65f, 0.32f, 1.0f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace IntroSpinner

void zCineFX_Register_IntroSpinner()
{
    static NCINMap cutmap[] = {
        { "enemy_spinner_intro", IntroSpinner::g_TT01_SpinnerIntro, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
