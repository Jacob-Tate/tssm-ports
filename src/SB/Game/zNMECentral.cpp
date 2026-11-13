#include "zNMECentral.h"
#include "zNMEConfig.h"
#include "zNMEMessenger.h"
#include "zNMEHazard.h"
#include "zNMESupport.h"
#include "zNMESupplement.h"
#include "zNMEParFX.h"
#include "zNMESpawner.h"
#include "zNMECommon.h"
#include "zNMETypeBuckOTron.h"
#include "zNMETypeCritter.h"
#include "zNMETypeTurret.h"
#include "zNMETypeStandard.h"
#include "zNMETypeFriendly.h"
#include "zNMETypeBossDennis.h"
#include "zGlobals.h"
#include "zGame.h"
#include "zHud.h"
#include "zCutsceneMgr.h"
#include "xCutscene.h"
#include "iCamera.h"
#include "xordarray.h"

#include <rwcore.h>
#include <rpworld.h>

// scope:local in retail (Ratatouille's zModuleMgr.cpp has it file-static too).
static st_XORDEREDARRAY g_modlist;

// No GC symbol (always inlined): the loops below test a byte-sized result.
inline bool zNMECentral_IsCinematic()
{
    return globals.cmgr != NULL && globals.cmgr->csn != NULL && globals.cmgr->csn->Ready;
}

void zNMECentral_Startup()
{
    xBehaveMgr* bmgr;

    XOrdInit(&g_modlist, 32, 0);

    bmgr = xBehaveMgr_GetSelf();
    zNMEGoals_RegisterTypes(bmgr->GetFactory());

    zNMEConfig_Startup();
    zNMEMsg_Startup();
    zNMEHazard_Startup();
    zNMESupport_Startup();
    zNMESupplement_Startup();
    zNMEParFX_Startup();
    zNMESpawner_Startup();
    ZNME_Common_Startup();
    ZNME_BuckOTron_Startup();
    ZNME_Critter_Startup();
    ZNME_Turret_Startup();
    ZNME_Standard_Startup();
    ZNME_Friendly_Startup();
    ZNME_Boss_Startup();
}

void zNMECentral_Shutdown()
{
    zNMEHazard_Shutdown();
    zNMEConfig_Shutdown();
    XOrdDone(&g_modlist, 0);
}

// No GC symbol: inlined into every module startup (its unused copy is dead-stripped).
void zNMECentral_RegisterModule(zModule* mod, S32 modID)
{
    mod->modID = modID;
    mod->flg_skipRuntime = 0;
    XOrdAppend(&g_modlist, mod);
    mod->ModulePrepUse();
}

void zNMECentral_ScenePrepare()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->ScenePrepare();
    }
}

void zNMECentral_SceneFinish()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->SceneFinish();
    }
}

void zNMECentral_SceneInit()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->SceneInit();
    }
}

void zNMECentral_ScenePostInit()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->ScenePostInit();
    }
}

void zNMECentral_SceneSetup()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->SceneSetup();
    }
}

void zNMECentral_ScenePostSetup()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->ScenePostSetup();
    }
}

void zNMECentral_SceneReset()
{
    S32 num_mod = g_modlist.cnt;
    S32 i;

    for (i = 0; i < num_mod; i++)
    {
        ((zModule*)g_modlist.list[i])->SceneReset();
    }
}

void zNMECentral_Timestep(F32 dt)
{
    S32 isPaused;
    S32 isCinematic;
    S32 num_mod;
    S32 i;
    zModule* mod;

    isPaused = zGameIsPaused();
    isCinematic = zNMECentral_IsCinematic();

    num_mod = g_modlist.cnt;
    for (i = 0; i < num_mod; i++)
    {
        mod = (zModule*)g_modlist.list[i];
        if (mod->flg_skipRuntime)
        {
            continue;
        }
        if (isPaused && !mod->flg_updateWhenPaused)
        {
            continue;
        }
        if (isCinematic && !mod->flg_updateInCinematic)
        {
            continue;
        }

        mod->Timestep(dt);
    }
}

void zNMECentral_PostTimestep(F32 dt)
{
    S32 isPaused;
    S32 isCinematic;
    S32 num_mod;
    S32 i;
    zModule* mod;

    isPaused = zGameIsPaused();
    isCinematic = zNMECentral_IsCinematic();

    num_mod = g_modlist.cnt;
    for (i = 0; i < num_mod; i++)
    {
        mod = (zModule*)g_modlist.list[i];
        if (mod->flg_skipRuntime)
        {
            continue;
        }
        if (isPaused && !mod->flg_updateWhenPaused)
        {
            continue;
        }
        if (isCinematic && !mod->flg_updateInCinematic)
        {
            continue;
        }

        mod->PostTimestep(dt);
    }
}

void zNMECentral_RenderToBuckets()
{
    S32 isPaused;
    S32 isCinematic;
    S32 num_mod;
    S32 i;
    zModule* mod;

    isPaused = zGameIsPaused();
    isCinematic = zNMECentral_IsCinematic();

    num_mod = g_modlist.cnt;
    for (i = 0; i < num_mod; i++)
    {
        mod = (zModule*)g_modlist.list[i];
        if (mod->flg_skipRuntime)
        {
            continue;
        }
        if (isPaused && !mod->flg_updateWhenPaused)
        {
            continue;
        }
        if (isCinematic && !mod->flg_updateInCinematic)
        {
            continue;
        }

        mod->RenderToBuckets();
    }
}

void zNMECentral_Render()
{
    S32 isPaused;
    S32 isCinematic;
    S32 num_mod;
    S32 i;
    zModule* mod;

    isPaused = zGameIsPaused();
    isCinematic = zNMECentral_IsCinematic();

    num_mod = g_modlist.cnt;
    for (i = 0; i < num_mod; i++)
    {
        mod = (zModule*)g_modlist.list[i];
        if (mod->flg_skipRuntime)
        {
            continue;
        }
        if (isPaused && !mod->flg_updateWhenPaused)
        {
            continue;
        }
        if (isCinematic && !mod->flg_updateInCinematic)
        {
            continue;
        }

        mod->Render();
    }
}

void zNMECentral_PostRender()
{
    S32 isPaused;
    S32 isCinematic;
    S32 num_mod;
    S32 i;
    zModule* mod;

    isPaused = zGameIsPaused();
    isCinematic = zNMECentral_IsCinematic();

    num_mod = g_modlist.cnt;
    for (i = 0; i < num_mod; i++)
    {
        mod = (zModule*)g_modlist.list[i];
        if (mod->flg_skipRuntime)
        {
            continue;
        }
        if (isPaused && !mod->flg_updateWhenPaused)
        {
            continue;
        }
        if (isCinematic && !mod->flg_updateInCinematic)
        {
            continue;
        }

        mod->PostRender();
    }
}

void zNMECentral_HudRender()
{
    RwCamera* oldCamera;
    S32 isPaused;
    S32 isCinematic;
    S32 num_mod;
    S32 i;
    zModule* mod;

    oldCamera = RwCameraGetCurrentCamera();
    if (oldCamera != NULL)
    {
        RwCameraEndUpdate(oldCamera);
    }

    RwCameraSetRaster(zHud::HudCamera, RwCameraGetRaster(globals.screen->icam));
    RwCameraSetZRaster(zHud::HudCamera, RwCameraGetZRaster(globals.screen->icam));
    RpWorldAddCamera(xglobals->currWorld, zHud::HudCamera);
    RwCameraBeginUpdateWrapper(zHud::HudCamera);

    isPaused = zGameIsPaused();
    isCinematic = zNMECentral_IsCinematic();

    num_mod = g_modlist.cnt;
    for (i = 0; i < num_mod; i++)
    {
        mod = (zModule*)g_modlist.list[i];
        if (!mod->flg_needHUDRender || mod->flg_skipRuntime)
        {
            continue;
        }
        if (isPaused && !mod->flg_updateWhenPaused)
        {
            continue;
        }
        if (isCinematic && !mod->flg_updateInCinematic)
        {
            continue;
        }

        mod->HudRender();
    }

    RwCameraEndUpdate(zHud::HudCamera);
    RwCameraSetRaster(zHud::HudCamera, NULL);
    RwCameraSetZRaster(zHud::HudCamera, NULL);
    RpWorldRemoveCamera(xglobals->currWorld, zHud::HudCamera);

    if (oldCamera != NULL)
    {
        RwCameraBeginUpdateWrapper(oldCamera);
    }
}
