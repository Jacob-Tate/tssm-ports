#include "zGame.h"
#include "zCam.h"
#include "zDispatcher.h"
#include "zHud.h"
#include "zPlayerBase.h"
#include "zPlayerSB.h"
#include "zMenuSB.h"
#include "zEnv.h"
#include "zFX.h"
#include "zJSPExtraData.h"
#include "zLOD.h"
#include "zScene.h"
#include "xDynAsset.h"
#include "xMarkerAsset.h"
#include "xModel.h"
#include "xScrFx.h"
#include "xSkyDome.h"
#include "xUpdateCull.h"
#include "xstransvc.h"
#include "xutil.h"
#include "xRumbleManager.h"
#include "zLoadingScreen.h"
#include "iAsync.h"
#include "iCamera.h"
#include "iDraw.h"
#include "iTRC.h"
#include "zMain.h"
#include "xDebug.h"
#include "xMemMgr.h"
#include "xTextureManager.h"
#include "xTRC.h"
#include "zCutsceneMgr.h"
#include "zGameExtras.h"
#include "iTime.h"
#include "zBusStop.h"
#include "zCamera.h"
#include "zExplosion.h"
#include "zFire.h"
#include "zMusic.h"
#include "zParticleGenerator.h"
#include "zSaveLoad.h"
#include "zVolume.h"
#include "xAntenna.h"
#include "xFont.h"
#include "xModelBlur.h"
#include "xParticleBatch.h"
#include "xScreenWarp.h"
#include "xSnd.h"
#include "iEnv.h"
#include "iFile.h"

#ifdef PLATFORM_PC
#include "iCheats.h"
#include "iWindow.h"
#include "iXboxWater.h"
#endif

#include <dolphin/os.h>

#include <stdio.h>
#include <string.h>

extern "C" {
void RwGameCubeSetMinRetraceCount(RwUInt8 count);
}

U32 startPressed = -1;
static U32 sPlayerMarkerStartID;
static F32 sGameOverTimer;
F32 sTimeElapsed;
F32 gRealTimeElapsed;
iTime sTimeLast;
iTime sTimeCurrent;
U32 gLevelChanged;
static S32 g_hiphopReloadHIP;
static S32 g_hiphopForcePortal;
static S32 g_hiphopResetOnly;
static S32 g_hiphopEnabled;
static xColor_tag sFadeColor;
static U32 gzAsyncSceneID;
xColor_tag black;
xColor_tag clear;
U32 gSoak;
char** soaklevels;
char* allsoaklevels[33] = {
    "AM01", "AM02", "AM03", "BB01", "BB02", "BB03", "DE01", "DE02", "TT01", "TT02", "B101",
    "TR01", "TR02", "TR03", "JK01", "JK02", "B201", "GG02", "SC02", "B301", "PT01", "PT02",
    "PT03", "B401", "B402", "BL01", "BL02", "BL03", "BL04", "FB01", "FB02", "FB03", NULL,
};
char* otherNonSoakLevels[44] = {
    "TL01", "TL02", "TL03", "TL04", "TL05", "TL06", "TL07", "TL08", "TL09", "TL10", "TL61",
    "TL62", "TL63", "TL70", "TL71", "TL72", "TL73", "TL74", "TL75", "TL76", "TL77", "TL78",
    "TL79", "TL80", "TL81", "TL82", "TL83", "TL84", "TL85", "TL86", "TL87", "TL88", "TL89",
    "TL90", "TL91", "TL92", "TL93", "TL94", "TL95", "TL96", "TL97", "TL98", "TL99", NULL,
};
static F32 SOAK_DELAY = 2.0f;
static F32 SOAK_DELAYRAND = 4.0f;
static F32 soaktime = 4.0f;
S32 doCallLogging;
S32 enableMemDebugLogging;
S32 enableHackNProbeLogging;
S32 enableSuperSoaker;
S32 enableMuttonMasher;
static S32 superSoak_simulatePlayerAction;
static U32 superSoakExtraPadOnInput;
static U32 superSoakExtraPadPressInput;
xPortalAsset dummyPortalAsset;
_zPortal dummyPortal;
static U32 loadMeter;
static U32 loadCDMeter;

namespace
{
    // Defined in zMenuSB.cpp (the loading screen's level-name text reads it).
    extern U32 sNextSceneID;
} // namespace

struct pointer_asset : xDynAsset
{
    xVec3 loc;
    F32 yaw;
    F32 pitch;
    F32 roll;

    static const char* type_name()
    {
        return "pointer";
    }
};

// No symbol (always inlined); the name is a guess.
static inline bool zGameIsPointerAsset(const xDynAsset* a)
{
    return a->baseType == 0 && a->version == 1 && a->type == xStrHash(pointer_asset::type_name());
}

// The pause menu's world counter shows the world the player is currently in.
inline void zGameUpdatePauseWorldCounter()
{
    _xCounter* counter = (_xCounter*)zSceneFindObject(0xA7E692A7);
    if (counter != NULL && counter->baseType == eBaseTypeCounter)
    {
        S32 world = 0;
        S32 task = 0;
        zDispatcher_FindWorldTask(globals.sceneCur->sceneID, world, task);
        counter->count = world;
    }
}

static U32 PickNextSoak()
{
    U32 nextsoak;
    char* name;
    // not in the DWARF: the soak direction name, only used by the debug printf (the PS2 DWARF may
    // have dropped it).
    char* dirname;
    U32 tag;
    S32 num_search;

    static S32 soakidx = 0;
    static S32 soakcnt = 0;
    static enum en_SOAK_DIR
    {
        SOAK_FOR,
        SOAK_BACK,
        SOAK_RAND,
        SOAK_NOMORE,
        SOAK_FORCE = 0x7FFFFFFF
    } soakdir = SOAK_FOR;
    static S32 justwrap = 0;

    name = NULL;

    if (soakcnt <= 0)
    {
        while (soaklevels[soakcnt] != NULL)
        {
            soakcnt++;
        }
    }

    if (soakcnt == 0)
    {
        return 0;
    }

    switch (soakdir)
    {
    case SOAK_FOR:
        dirname = "FORWARD";
        soakidx++;
        if (soakidx >= soakcnt)
        {
            if (justwrap)
            {
                soakidx = 0;
            }
            else
            {
                soakidx = soakcnt - 2;
                soakdir = SOAK_BACK;
                dirname = "BACKWARD";
            }
        }
        name = soaklevels[soakidx];
        break;
    case SOAK_BACK:
        dirname = "BACKWARD";
        soakidx--;
        if (soakidx < 0)
        {
            if (justwrap)
            {
                soakidx = soakcnt - 1;
            }
            else
            {
                soakidx = 0;
                soakdir = SOAK_RAND;
                dirname = "RANDOM";
            }
        }
        name = soaklevels[soakidx];
        break;
    case SOAK_RAND:
    default:
        dirname = "RANDOM";
        tag = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        num_search = 0;
        while (tag == globals.sceneCur->sceneID && num_search < soakcnt)
        {
            num_search++;
            name = xUtil_select(soaklevels, soakcnt, NULL);
            tag = (name[0] << 24) | (name[1] << 16) | (name[2] << 8) | name[3];
        }
        break;
    }

    char useme[5] = {};
    if (useme[0] != '\0')
    {
        name = useme;
    }

    nextsoak = (name[0] << 24) | (name[1] << 16) | (name[2] << 8) | name[3];

    static S32 sumtotal = 0;
    sumtotal++;
    printf("SOAKTEST: Portal -> Next scene: %s  Total switches: %d (%s)\n", name, sumtotal,
           dirname);

    return nextsoak;
}

// No symbol in TSSM: inlined twice into zGameInit (Ratatouille's GC DWARF names it).
static inline void zGameInit_Guts(U32 theSceneID)
{
    xRumble::Manager::Get().Reset();
    xCameraFXKill();

    if (g_hiphopReloadHIP)
    {
        zSceneInit(theSceneID, 1);
    }
    else
    {
        zSceneInit(theSceneID, 0);
    }

    g_hiphopReloadHIP = 0;
    g_hiphopForcePortal = 0;

    zCamSceneEnter(*globals.sceneCur, g_I3);
    zMusicInit();
    zGameInitPlayer();
}

// No symbol in TSSM (inlined into zGameInit, out-of-line copy stripped); the name is Ratatouille's.
static void* asyncLoadSubthread(void*)
{
    zGameInit_Guts(gzAsyncSceneID);
    return NULL;
}

void zGameInit(U32 theSceneID)
{
    globals.LoadingScene = TRUE;

    if (globals.asyncLoadingScreen)
    {
        gzAsyncSceneID = theSceneID;
        sNextSceneID = theSceneID;

        zLoadingScreen screen;

        globals.InitializingLoadingScreen = TRUE;
        zAsync::Init(&screen);
        zAsync::Allocate();
        globals.InitializingLoadingScreen = FALSE;

        iAsyncStart(zAsync::RenderThread);
        asyncLoadSubthread(NULL);
        iAsyncTerminate();
        zAsync::Free();
    }
    else
    {
        zGameInit_Guts(theSceneID);
    }

    globals.LoadingScene = FALSE;
}

void zGameExit()
{
    zGameExtras_SceneExit();
    xRumble::Manager::Get().Reset();
    zHud::hud_system::instance().destroy();
    zMusicKill();
    xSndMgrStopSounds(-3, false);
    xSndMgrUpdate();
    zCamSceneExit();
    if (g_hiphopReloadHIP)
    {
        zSceneExit(1);
    }
    else
    {
        zSceneExit(0);
    }
}

void zGameSetup()
{
    zSceneSetup();

    zJSPExtraData::ShowAllAssociatedGroups(false);

    RpWorld* world = globals.sceneCur->env->geom->world;
    xModel_SceneEnter(world);
    zFX_SceneEnter(world);

    zEnvStartingCamera(gCurEnv);
    xScrFxReset();
    zSceneLoad(globals.sceneCur);

    if (globals.ForceAllCinematics)
    {
        globals.ForceCinematic = 1;
    }

    if (globals.ForceCinematic > 0)
    {
        S32 csncount = xSTAssetCountByType('CSNM');
        if (globals.ForceCinematic <= csncount)
        {
            for (S32 cnum = 0; cnum < csncount; cnum++)
            {
                if (cnum == globals.ForceCinematic - 1)
                {
                    zEntEvent(
                        zSceneFindObject(((xBaseAsset*)xSTFindAssetByType('CSNM', cnum, NULL))->id),
                        eEventPreload);
                }
                else
                {
                    zEntEvent(
                        zSceneFindObject(((xBaseAsset*)xSTFindAssetByType('CSNM', cnum, NULL))->id),
                        eEventDisable);
                }
            }
        }
        else
        {
            globals.ForceCinematic = 0;
        }
    }
    else if (globals.ForceCinematic == -1)
    {
        globals.ForceCinematic = 1;
    }

    xSkyDome_Setup();

    zEntEventAll(NULL, 0, eEventSceneBegin, NULL);
    zEntEventAll(NULL, 0, eEventRoomBegin, NULL);
    zEntEventAll(NULL, 0, eEventSceneEnter, NULL);
    if (gLevelChanged)
    {
        zEntEventAll(NULL, 0, eEventLevelBegin, NULL);
    }

#ifdef PLATFORM_PC
    // PC: the Xbox water's bodies hidden and their tiles built (iXboxWater.h), where the Xbox
    // does it (0x52A48).
    zWaterBody::setup_all();
#endif

    if (globals.updateMgr != NULL)
    {
        xUpdateCull_Update(globals.updateMgr, 100);
    }

    zLOD_Update(100);
    zGameExtras_SceneInit();
    zGameResetPlayerToSceneStart();
    zSceneStoreCheckPoint(0);
}

// Main loop timing (static long32 in the PS2 DWARF); defined here for retail's .sbss order.
static S64 t0;
static S64 t1;
static S64 w0;
static S64 w1;
static S64 gloop_time;
static S64 gwait_time;
static S32 gloop_ct;
static F32 gloop_time_secs;
static F32 gwait_time_secs;
static F32 gloop_net_time_secs;
F32 snd_radius;
U32 gLastProfileDump;
void (*zSB04_BossPreRender)();

static void zGameUpdateMode();

// No symbol (always inlined into zGameLoop); the name is a guess. Retail evaluates both
// flags before testing the pad state, i.e. they are arguments of an inlined call.
static inline bool zGamePadMissingMsgAllowed(bool playerInControl, bool msgAllowed)
{
    return playerInControl && gTrcPad[0].state != TRC_PadInserted && !globals.NoPadCheck &&
           msgAllowed;
}

// No symbol (always inlined into zGameLoop); the name is BfBB's (Ratatouille's DWARF has it too).
static inline S32 zGameLoopContinue()
{
    if (gGameMode == eGameMode_Game)
    {
        return gGameState == eGameState_Play || gGameState == eGameState_GameOver ||
               gGameState == eGameState_GameStats;
    }

    if (gGameMode == eGameMode_Save)
    {
        zSaveLoad_SaveLoop();
        sTimeLast = iTimeGet();
        t1 = iTimeGet();
    }

    return TRUE;
}

void zGameLoop()
{
    S32 ostrich_delay = 10;
    S32 isPaused;
    U8 moviePadInsertion;

    zGameStateSwitch(eGameState_Play);

    if (globals.cmgr != NULL)
    {
        zCutsceneMgrFinishLoad(globals.cmgr);
    }

    sTimeLast = iTimeGet() - (iTime)(OS_TIMER_CLOCK / VBLANKS_PER_SEC);
    gSceneFrameCount = 0;

    do
    {
        OSCheckActiveThreads();

        sTimeCurrent = iTimeGet();
        sTimeElapsed = iTimeDiffSec(sTimeLast, sTimeCurrent);
#ifdef PLATFORM_PC
        // PC: a frame that came on time runs on a steady frame time, as every
        // console frame is one NTSC retrace, so the enemies' end-of-animation
        // checks see their ends as on the console (iSteadyFrame.h).
        sTimeElapsed = iWindowGameFrameTime(sTimeElapsed);
#endif

        if (sTimeElapsed > 0.1f)
        {
            sTimeElapsed = 0.1f;
        }

        if (gWasDiskError)
        {
            gWasDiskError = 0;
            sTimeElapsed = 1.0f / 30.0f;
        }

        gRealTimeElapsed = sTimeElapsed;

        if (globals.slowdown)
        {
            sTimeElapsed /= (F32)(1 << globals.slowdown);
        }

        sTimeLast = sTimeCurrent;

        t0 = t1;
        t1 = iTimeGet();
        gloop_time += t1 - t0;
        gloop_ct++;
        gloop_time_secs = iTimeDiffSec(gloop_time) / gloop_ct;
        gloop_net_time_secs = iTimeDiffSec(gloop_time - gwait_time) / gloop_ct;

        xPadUpdate(globals.currentActivePad, sTimeElapsed);

        if (globals.player.g.AttractModeDuringGameplay)
        {
            static F32 sAttractMode_timer = 45.0f;

            if (globals.cmgr == NULL && !zCameraIsFlyCamRunning())
            {
                sAttractMode_timer -= sTimeElapsed;
            }

            if (mPad[globals.currentActivePad].pressed ||
                mPad[globals.currentActivePad].analog[0].mag ||
                mPad[globals.currentActivePad].analog[1].mag)
            {
                sAttractMode_timer = 45.0f;
            }

            if (sAttractMode_timer < 0.0f)
            {
                zGameStateSwitchEvent(eEventDispatcher_SetGameState_Exit);
                sAttractMode_timer = 45.0f;
            }
        }

        if (!globals.player.g.CheatPlayerSwitch)
        {
            if (globals.pad0->on & XPAD_BUTTON_LEFT)
            {
                globals.pad0->analog1.x = -globals.player.g.AnalogMax;
            }
            else if (globals.pad0->on & XPAD_BUTTON_RIGHT)
            {
                globals.pad0->analog1.x = globals.player.g.AnalogMax;
            }

            if (globals.pad0->on & XPAD_BUTTON_UP)
            {
                globals.pad0->analog1.y = -globals.player.g.AnalogMax;
            }
            else if (globals.pad0->on & XPAD_BUTTON_DOWN)
            {
                globals.pad0->analog1.y = globals.player.g.AnalogMax;
            }
        }

        iTRCDisk::TRCCheck();

        xPadNormalizeAnalog(*globals.pad[globals.currentActivePad], 40, 127);

        globals.update_dt = sTimeElapsed;

        zGameExtras_SceneUpdate(sTimeElapsed);
#ifdef PLATFORM_PC
        // PC: the Cheats tab's requests and the port's own cheats (iCheats.h).
        iCheatsGameFrame();
#endif
        iFileAsyncService();
        iDrawBegin();

        isPaused = zGameIsPaused();

        zSceneUpdate(sTimeElapsed);

        if (!isPaused)
        {
            xglobals->___player_ent_dont_use_directly->update(
                xglobals->___player_ent_dont_use_directly, globals.sceneCur, sTimeElapsed);
        }

        xAntennaUpdate(sTimeElapsed);

        if (!isPaused)
        {
            xMat4x3 playerMat = *(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;

            // playerVel is not in the PS2 DWARF (that build stored straight into gSnd), but retail
            // builds the vector in a stack slot from both branches.
            xVec3 playerVel;
            if (!xfeq0(sTimeElapsed))
            {
                playerVel = (xglobals->___player_ent_dont_use_directly->frame->mat.pos -
                             xglobals->___player_ent_dont_use_directly->frame->oldmat.pos) /
                            sTimeElapsed;
            }
            else
            {
                playerVel.assign(0.0f, 0.0f, 0.0f);
            }

            xSndSetListenerData(xSndListener_PLAYER, &playerMat, playerVel);
            xSndSetListenerData(xSndListener_CAMERA, &globals.cam->mat, globals.cam->vel);
        }

        xSndMgrUpdate();

        if (globals.sceneCur != NULL && globals.sceneCur->pendingPortal == NULL)
        {
            for (U32 i = 0; i < 4; i++)
            {
                xRumble::Manager::Get().Update(
                    globals.pad[i], xglobals->___player_ent_dont_use_directly->frame->mat.pos,
                    sTimeElapsed, gGameMode == eGameMode_Save || gGameMode == eGameMode_Pause);
            }
        }

        zSceneUpdateSFXWidgets();
        xModelBlur::update_all(sTimeElapsed);

        if (!isPaused)
        {
            zCamUpdate(sTimeElapsed);
        }

        xCameraFXBegin(globals.cam->mat);
        xCameraFXUpdate(globals.cam->mat, globals.screen->icam, sTimeElapsed);
        zScene_UpdateFlyToInterface(sTimeElapsed);

        w0 = iTimeGet();

        zCamPrepareRender();

        if (globals.sceneCur->enableDrawing)
        {
            zExplosion::pre_render();
            zFire::pre_render();
            zParticlePreRender();
            xParticleBatchPreRender();
            xScreenWarp::pre_render();

            if (zSB04_BossPreRender != NULL)
            {
                zSB04_BossPreRender();
            }

#ifdef PLATFORM_PC
            // PC: the shadows, before the scene's camera begins (zScene.cpp).
            zScenePreRenderShadows();
            // PC: the Xbox water's refraction and reflection targets, there
            // too (iXboxWater.h).
            xWaterPreRender(globals.screen->icam);
#endif
        }

        zCamBeginRender();

        w1 = iTimeGet();
        gwait_time += w1 - w0;
        gwait_time_secs = iTimeDiffSec(gwait_time) / gloop_ct;

        xScrFxUpdate(globals.screen->icam, sTimeElapsed);
        zVolume_OccludePrecalc(&globals.cam->mat.pos);
        zSceneRender();
        xFontPrintTopText();
        xDebugUpdate();
        iDrawEnd();
        zCamEndRender();
        iEnvEndRenderFX(NULL);
        RwGameCubeSetMinRetraceCount(globals.minVSyncCnt);

        if (globals.sceneCur != NULL && globals.sceneCur->pendingPortal == NULL)
        {
            iCameraShowRaster(globals.screen->icam);
        }

        xCameraFXEnd(globals.cam->mat, globals.screen->icam);
        zMusicUpdate(sTimeElapsed);
        zGameUpdateMode();

        gFrameCount++;
        gSceneFrameCount++;

        if (ostrich_delay > 0)
        {
            ostrich_delay--;
        }
        else
        {
            gGameOstrich = eGameOstrich_InScene;

            moviePadInsertion = !gBusStopIsRunning && !globals.player.ControlOff;
            if (zGamePadMissingMsgAllowed(moviePadInsertion,
                                          !globals.dontShowPadMessageDuringLoadingOrCutScene))
            {
                globals.dontShowPadMessageDuringLoadingOrCutScene = TRUE;
                xTRCPad(gTrcPad[0].id, TRC_PadMissing);
            }

            zSaveLoadAutoSaveUpdate();
        }
    } while (zGameLoopContinue());
}

S32 zGameIsPaused()
{
    if (gGameMode == eGameMode_Stall)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Pause)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Save)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Title)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Load)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Save)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Intro)
    {
        return TRUE;
    }
    if (gGameMode == eGameMode_Boot)
    {
        return TRUE;
    }
    return FALSE;
}

pause_ability zGameOkToPause()
{
    if (globals.cmgr != NULL)
    {
        return PA_NONE;
    }
    if (zGameIsPaused())
    {
        return PA_NONE;
    }
    if (globals.sceneCur->sceneID == 'B402')
    {
        return PA_NONE;
    }
    if (globals.player.ControlOff & 0xFFFF92FF)
    {
        return PA_NONE;
    }
    if (gTrcPad[0].state == TRC_PadMissing)
    {
        return PA_STALL;
    }
    if (globals.player.PlayerIsFrozen)
    {
        return PA_NONE;
    }
    return PA_MENU;
}

void zGamePause()
{
    if (gGameMode != eGameMode_Pause)
    {
        zMenuSB_UpdatePowerUpMenu();

        zGameUpdatePauseWorldCounter();

        zGameModeSwitch(eGameMode_Pause);
        zGameStateSwitch(ePauseState_Pause);
        xRumble::Manager::Get().StopRumbling();
        zEntEvent("techbutton6_click", eEventPlay);
    }
}

void zGameStall()
{
    if (!zGameIsPaused())
    {
        zGameModeSwitch(eGameMode_Stall);
        xSndMgrPauseSounds(0, TRUE, TRUE);
        xSndMgrPauseSounds(1, TRUE, TRUE);
        xSndMgrPauseSounds(2, TRUE, FALSE);
        iPadStopRumble(globals.pad0);
        zEntEvent("techbutton6_click", 24);
    }
}

// Dead-stripped placeholder: retail's .data keeps an unreferenced, zeroed xtextbox here, the
// function-local 'static xtextbox tb' of zGame code that did not survive. Only the static is
// restored.
static void __deadstripped_zGame()
{
    static xtextbox tb = xtextbox::create();
}

static void zGameUpdateMode()
{
    xPortalAsset* passet;
    U32 nextSceneID;
    U32 sPlayerMarkerStartCamID;

    if (gGameMode != eGameMode_Game)
    {
        return;
    }

    if (gSoak && gGameState == eGameState_Play && globals.cmgr == NULL)
    {
        soaktime -= 2.0f * SECS_PER_VBLANK;
        if (soaktime < 0.0f)
        {
            xMemDebug_SoakLog(xUtil_idtag2string(globals.sceneCur->sceneID, 0));
            gGameState = eGameState_SceneSwitch;
            dummyPortalAsset.assetCameraID = 0;
            dummyPortalAsset.assetMarkerID = 0;
            dummyPortalAsset.ang = 0.0f;
            dummyPortalAsset.sceneID = PickNextSoak();
            dummyPortal.passet = &dummyPortalAsset;
            globals.sceneCur->pendingPortal = &dummyPortal;
            soaktime = SOAK_DELAYRAND * xurand() + SOAK_DELAY;
        }
    }

    if (gGameState == eGameState_Play)
    {
        iTimeGameAdvance(sTimeElapsed);
        if (globals.pad0->pressed & 1)
        {
            switch (zGameOkToPause())
            {
            case PA_NONE:
                xTRCReset();
                startPressed = 1;
                break;
            case PA_MENU:
                if (!startPressed)
                {
                    zGamePause();
                }
                break;
            case PA_STALL:
                zGameStall();
                break;
            }
        }
        else
        {
            startPressed = 0;
        }
    }
    else if (gGameState == eGameState_GameStats)
    {
        return;
    }
    else if (gGameState == eGameState_SceneSwitch)
    {
        passet = globals.sceneCur->pendingPortal->passet;
        nextSceneID = passet->sceneID;
        sPlayerMarkerStartID = passet->assetMarkerID;
        sPlayerMarkerStartCamID = passet->assetCameraID;

        zGameExit();
        xTextureManager::Get().FreeMemory();
        zGameInit(nextSceneID);
        zGameSetup();
        globals.sceneCur->enableDrawing = TRUE;
        if (globals.cmgr != NULL)
        {
            zCutsceneMgrFinishLoad(globals.cmgr);
        }
        zSceneStoreCheckPoint(sPlayerMarkerStartCamID);

        sTimeLast = iTimeGet() - (iTime)(OS_TIMER_CLOCK / VBLANKS_PER_SEC);
        zGameStateSwitch(eGameState_Play);
    }
    else if (gGameState == eGameState_LoseChance)
    {
        zSceneReset();
        zGameStateSwitch(eGameState_Play);
    }
    else if (gGameState == eGameState_GameOver)
    {
        if (sGameOverTimer == 0.0f)
        {
            sGameOverTimer = 5.0f;
            xScrFxFade(&g_CLEAR, &g_BLACK, 4.5f, TRUE);
        }
        else
        {
            sGameOverTimer -= sTimeElapsed;
            xprintf("GAME OVER (%f secs)\n", sGameOverTimer);
            if (sGameOverTimer <= 0.0f)
            {
                sGameOverTimer = 0.0f;
                zGameStateSwitch(eGameState_Exit);
            }
        }
    }
    else if (gGameState == eGameState_Exit)
    {
        sGameOverTimer = 0.0f;
        zMainShowProgressBar(10);
    }
}

// Screen transition data (retail .sbss/.sdata: after zGameLoop's sAttractMode_timer).
RwCamera* sGameScreenTransCam;
static RpWorld* gWorld;
static RpLight* DirectionalLight;
U32 bgID = 0x1d33b0bb;
F32 bgu1;
F32 bgv1;
F32 bgu2 = 1.333f;
F32 bgv2 = 1.0f;
U8 bgr = 0x60;
U8 bgg = 0x60;
U8 bgb = 0x60;
U8 bga = 0x80;

void zGameScreenTransitionBegin()
{
    if (!globals.asyncLoadingScreen)
    {
        zGameSetOstrich(eGameOstrich_Loading);
        globals.dontShowPadMessageDuringLoadingOrCutScene = FALSE;
        sGameScreenTransCam = iCameraCreate(FB_XRES, FB_YRES, 0);
        if (sGameScreenTransCam != NULL)
        {
            DirectionalLight = RpLightCreate(rpLIGHTDIRECTIONAL);
            if (DirectionalLight != NULL)
            {
                RwFrame* frame;
                RwRGBAReal col;
                col.red = col.green = col.blue = 1.0f;
                col.alpha = 0.0f;
                RpLightSetColor(DirectionalLight, &col);

                frame = RwFrameCreate();
                _rwObjectHasFrameSetFrame(DirectionalLight, frame);

                RwBBox box;
                box.sup.z = box.sup.y = box.sup.x = 10000.0f;
                box.inf.z = box.inf.y = box.inf.x = -10000.0f;
                gWorld = RpWorldCreate(&box);
                RpWorldAddCamera(gWorld, sGameScreenTransCam);
            }
        }
    }
}

void zGameScreenTransitionUpdate(F32 percentComplete, char* msg)
{
    if (!globals.asyncLoadingScreen && !zMenuIsFirstBoot())
    {
        zGameScreenTransitionUpdate(percentComplete, msg, NULL);
    }
}

void zGameScreenTransitionUpdate(F32 percentComplete, char* msg, U8* rgba)
{
    if (!globals.asyncLoadingScreen && !zMenuIsFirstBoot())
    {
        RwRGBA back_col = { 0x00, 0x00, 0x00, 0xFF };
        RwTexture* tex;
        RwRaster* ras;
        RwIm2DVertex vx[4];

        if (rgba != NULL)
        {
            back_col.red = rgba[0];
            back_col.green = rgba[1];
            back_col.blue = rgba[2];
            back_col.alpha = rgba[3];
        }

        xPadUpdate(globals.currentActivePad, sTimeElapsed);
        iDrawBegin();

        if (sGameScreenTransCam != NULL)
        {
            iTRCDisk::TRCCheck();
            RwCameraClear(sGameScreenTransCam, &back_col, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
            RwCameraBeginUpdateWrapper(sGameScreenTransCam);

            tex = (RwTexture*)xSTFindAsset(bgID, NULL);
            if (tex != NULL && (ras = tex->raster, ras != NULL))
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
                RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
                RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
                RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ras);

                F32 z = RwIm2DGetFarScreenZ();

                vx[0].x = 0.0f;
                vx[0].y = 0.0f;
                vx[0].z = z;
                vx[0].emissiveColor.red = bgr;
                vx[0].emissiveColor.green = bgb;
                vx[0].emissiveColor.blue = bgg;
                vx[0].emissiveColor.alpha = bga;
                vx[0].u = bgu1;
                vx[0].v = bgv1;

                vx[1].x = 0.0f;
                vx[1].y = FB_YRES;
                vx[1].z = z;
                vx[1].emissiveColor.red = bgr;
                vx[1].emissiveColor.green = bgb;
                vx[1].emissiveColor.blue = bgg;
                vx[1].emissiveColor.alpha = bga;
                vx[1].u = bgu1;
                vx[1].v = bgv2;

                vx[2].x = FB_XRES;
                vx[2].y = 0.0f;
                vx[2].z = z;
                vx[2].emissiveColor.red = bgr;
                vx[2].emissiveColor.green = bgb;
                vx[2].emissiveColor.blue = bgg;
                vx[2].emissiveColor.alpha = bga;
                vx[2].u = bgu2;
                vx[2].v = bgv1;

                vx[3].x = FB_XRES;
                vx[3].y = FB_YRES;
                vx[3].z = z;
                vx[3].emissiveColor.red = bgr;
                vx[3].emissiveColor.green = bgb;
                vx[3].emissiveColor.blue = bgg;
                vx[3].emissiveColor.alpha = bga;
                vx[3].u = bgu2;
                vx[3].v = bgv2;

                RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vx, 4);
                RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
            }
        }

        xprintf("Loading... %3.2f\n", percentComplete);
        if (msg != NULL)
        {
            xprintf(msg);
        }

        char meter[256] = "...";
        switch ((loadMeter / 25) % 5)
        {
        case 0:
            strcpy(meter, "   ");
            break;
        case 1:
            strcpy(meter, ".  ");
            break;
        case 2:
            strcpy(meter, ".. ");
            break;
        case 3:
            strcpy(meter, "...");
            break;
        case 4:
            loadMeter = 0;
            break;
        }
        loadMeter++;

        xDebugUpdate();
        zFX_SpawnBubbleWall();
        iDrawEnd();

        if (sGameScreenTransCam != NULL)
        {
            RwCameraEndUpdate(sGameScreenTransCam);
            RwCameraShowRaster(sGameScreenTransCam, NULL, 1);
        }
    }
}

// Dead-stripped placeholder: retail's .rodata keeps this unreferenced quad index list here.
// Ratatouille's stripped zGameScreenTransition_UIRender has it as the local Idx[6]; only the list
// is restored.
static void __deadstripped2_zGame()
{
    RwImVertexIndex Idx[6] = { 0, 1, 2, 0, 2, 3 };
}

void zGameScreenTransitionEnd()
{
    if (!globals.asyncLoadingScreen)
    {
        _rwFrameSyncDirty();
        if (DirectionalLight != NULL)
        {
            RpLightDestroy(DirectionalLight);
            DirectionalLight = NULL;
        }
        if (gWorld != NULL)
        {
            if (sGameScreenTransCam != NULL)
            {
                RpWorldRemoveCamera(gWorld, sGameScreenTransCam);
                iCameraDestroy(sGameScreenTransCam);
                sGameScreenTransCam = NULL;
            }
            RpWorldDestroy(gWorld);
            gWorld = NULL;
        }
    }
}

// No symbol in TSSM: inlined into zGameInit and zMenuSetup (out-of-line copy stripped).
inline void zGameInitPlayer()
{
    xEntAsset* asset =
        (xEntAsset*)xSTFindAssetByType('PLYR', xSTAssetCountByType('PLYR') - 1, NULL);
    S32 playerCount;
    S32 i;
    U32 playerTag;
    zPlayerSB* player;
    U32 altCostume;
    U32 playerForPointerTag;

    asset->baseType = eBaseTypePlayer;
    player = (zPlayerSB*)xglobals->___player_ent_dont_use_directly;
    playerCount = zConfig_getPlayerCount(globals.sceneCur->sceneID);
    for (i = 0; i < playerCount; i++)
    {
        playerTag = zConfig_getPlayerTag(globals.sceneCur->sceneID, playerCount - 1 - i);
        altCostume = zConfig_getAlternateCostume(playerTag);
        if (altCostume != 0)
        {
            playerTag = altCostume;
        }
        asset->modelInfoID = xStrHash(zConfig_getPlayerModelInfoString(playerTag));
        player->Init(asset, playerCount - 1 - i);
    }

    // Both branches make the same call in the retail code (two separate call sites in the target).
    if (sPlayerMarkerStartID != 0)
    {
        playerForPointerTag = zConfig_getPlayerTag(globals.sceneCur->sceneID, 0);
    }
    else
    {
        playerForPointerTag = zConfig_getPlayerTag(globals.sceneCur->sceneID, 0);
    }
    globals.playerTag = playerForPointerTag;
    zPlayerReset(xglobals->___player_ent_dont_use_directly);
}

// No symbol in TSSM: inlined into zGameSetup and zMenuSetup (out-of-line copy stripped).
inline void zGameResetPlayerToSceneStart()
{
    pointer_asset* pointer;
    xEntFrame* frame;
    xMarkerAsset* marker;

    if (sPlayerMarkerStartID == 0)
    {
        return;
    }

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        sPlayerMarkerStartID = 0;
        return;
    }

    pointer = (pointer_asset*)xSTFindAsset(sPlayerMarkerStartID, NULL);
    if (pointer != NULL && zGameIsPointerAsset(pointer))
    {
        frame = xglobals->___player_ent_dont_use_directly->frame;
        frame->mat.pos = pointer->loc;
        frame->rot.angle = (PI / 180.0f) * pointer->yaw;
        frame->rot.axis = xVec3::m_UnitAxisY;
        xMat3x3Euler(&xglobals->___player_ent_dont_use_directly->frame->mat, frame->rot.angle, 0.0f,
                     0.0f);
        *(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat = frame->mat;
        zCamReset(frame->mat);
    }
    else
    {
        marker = (xMarkerAsset*)xSTFindAsset(sPlayerMarkerStartID, NULL);
        if (marker != NULL)
        {
            xVec3Copy(&xglobals->___player_ent_dont_use_directly->frame->mat.pos, &marker->pos);
        }
    }

    sPlayerMarkerStartID = 0;
}

void DoTheResetinator()
{
}
