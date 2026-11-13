#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

namespace CineFXGeneral
{
    static NCINEntry g_cinfx_100Percent[2] = {
        {
            { 0x1000, 0.0f, -0.033333335f, "Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubTrailBoneSimp_AR, NULL,
              (NCINTweakFunc)zCinFX_BubTrailBoneSimp_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace CineFXGeneral

void zCineFX_Register_General()
{
    static NCINMap cutmap[] = {
        { "100_ending", CineFXGeneral::g_cinfx_100Percent, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
