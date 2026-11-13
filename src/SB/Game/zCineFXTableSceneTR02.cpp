#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

namespace CineFXSceneTR02
{
    static NCINEntry g_cinfx_TR02_cliff[2] = {
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

    static NCINEntry g_cinfx_TR02_cave[2] = {
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
} // namespace CineFXSceneTR02

void zCineFX_Register_SceneTR02()
{
    static NCINMap cutmap[] = {
        { "cliff_monster_cine", CineFXSceneTR02::g_cinfx_TR02_cliff, 0 },
        { "cin_monster_retreat", CineFXSceneTR02::g_cinfx_TR02_cave, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
