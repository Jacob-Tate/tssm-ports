#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

namespace CineFXSceneBB02
{
    static NCINEntry g_cinfx_BB02_manager[2] = {
        {
            { 0x1000, 0.0f, 1.0f, "Bubbles" },
            { (NCINUpdFunc)zCinFX_Generic_Upd, (NCINAnimFunc)zCinFX_BubTrailBoneSimp_AR, NULL,
              (NCINTweakFunc)zCinFX_BubTrailBoneSimp_Twk },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace CineFXSceneBB02

void zCineFX_Register_SceneBB02()
{
    static NCINMap cutmap[] = {
        { "bikini_bottom", CineFXSceneBB02::g_cinfx_BB02_manager, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
