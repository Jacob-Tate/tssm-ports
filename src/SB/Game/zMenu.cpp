#include <string.h>

#include "iTime.h"
#include "xEnv.h"
#include "xFXHighDynamicRange.h"
#include "xsavegame.h"
#include "xSkyDome.h"
#include "xUpdateCull.h"
#include "zCam.h"
#include "zEntPlayer.h"
#include "zFMV.h"
#include "zGame.h"
#include "zMain.h"
#include "zMenu.h"
#include "iCamera.h"
#include "iDraw.h"
#include "iFile.h"
#include "iTRC.h"
#include "xCM.h"
#include "xColor.h"
#include "xDebug.h"
#include "xFont.h"
#include "xPad.h"
#include "xRand.h"
#include "xRumbleManager.h"
#include "xScrFx.h"
#include "xSnd.h"
#include "xString.h"
#include "xTRC.h"
#include "zGameExtras.h"
#include "zGameState.h"
#include "zMusic.h"
#include "zSaveLoad.h"
#include "zScene.h"

//bool menu_fmv_played;
S32 sFirstBoot = 1;
//S32 logoTmr;
F32 time_elapsed = 0.01f;
F32 time_last;
F32 time_current;
F32 sAttractMode_timer;
F32 sOneLiner_timer;
//S32 promptSel;
S32 card;
//S32 var;
//S32 fullCard;
S32 sInMenu;
static S32 corruptFileCount;
static char corruptFiles[3][64];
//F32 ONELINER_WAITTIME;
F32 holdTmr = 10.0f;
U8 sAllowAttract;

S32 zMenuRunning()
{
    return sInMenu;
}

void zMenuInit(U32 theSceneID)
{
    sInMenu = 1;
    iTimeGet();
    xrand_RandomInt32();
    zMainShowProgressBar(50);
    zSceneInit(theSceneID, 0);
    zCamSceneEnter(*globals.sceneCur, g_I3);
    zMusicInit();
    zMainShowProgressBar(90);
    if (sFirstBoot)
    {
        zGameModeSwitch(eGameMode_Intro);
        zGameStateSwitch(0);
    }
    else
    {
        zGameModeSwitch(eGameMode_Title);
        zGameStateSwitch(0);
    }
}

void zMenuExit()
{
    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventVisible, 0.0f, 0.0f, 0.0f, 0.0f);
    zMusicKill();
    xSndMgrStopSounds(-3, false);
    zCamSceneExit();
    zSceneExit(0);
    sInMenu = 0;
    xFXHighDynamicRangeEnable(true);
}

void zMenuSetup()
{
    sAttractMode_timer = 45.0f;
    sOneLiner_timer = xrand_RandomFloatRange(46.0f, 90.0f);
    sAllowAttract = FALSE;
    zGameInitPlayer();
    zSceneSetup();
    zGameResetPlayerToSceneStart();
    xAnimPlaySetState(
        xglobals->___player_ent_dont_use_directly->model->Anim->Single,
        xAnimTableGetState(xglobals->___player_ent_dont_use_directly->model->Anim->Table, "Menu01"),
        0.0f);
    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventInvisible, 0.0f, 0.0f, 0.0f, 0.0f);
    zEnvStartingCamera(gCurEnv);
    xSkyDome_Setup();
    zEntEventAll(NULL, 0, eEventSceneBegin, NULL);
    zEntEventAll(NULL, 0, eEventRoomBegin, NULL);
    if (globals.updateMgr)
    {
        xUpdateCull_Update(globals.updateMgr, 100);
    }
    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventControlOff);
    xFXHighDynamicRangeEnable(false);
}

U32 zMenuLoop()
{
    S32 ostrich_delay;
    U32 nextSceneID;
    S32 draw_black;
    char* fname;
    U32 i;
    S32 isPaused;

    time_last = iTimeDiffSec(iTimeGet()) - SECS_PER_VBLANK;
    g_flg_chEnabled = 0;
    xScrFxFade(&g_BLACK, &g_CLEAR, 0.0f, 1);
    ostrich_delay = 10;

    do
    {
        draw_black = 0;
        if (gGameMode == eGameMode_Intro)
        {
            switch (gGameState)
            {
            case 0:
                draw_black = 1;
                zGameStateSwitch(3);
                xScrFxFade(&g_CLEAR, &g_BLACK, 0.0f, 1);
                break;
            case 3:
                draw_black = 1;
                zFMVPlay(zFMVFileGetName(eFMVFile_NickLOGO), 0x10001, 0.0f, 0, true, false);
                zFMVPlay(zFMVFileGetName(eFMVFile_NickMovieLOGO), 0x10001, 0.0f, 0, true, false);
                zFMVPlay(zFMVFileGetName(eFMVFile_THQLOGO), 0x10001, 0.0f, 0, true, false);
                zFMVPlay(zFMVFileGetName(eFMVFile_HeavyIronLOGO), 0x10001, 0.0f, 0, true, false);
                holdTmr = 10.0f;
                zGameModeSwitch(eGameMode_Title);
                zGameStateSwitch(1);
                xScrFxFade(&g_BLACK, &g_CLEAR, 0.1f, 1);
                break;
            }
        }

        time_current = iTimeDiffSec(iTimeGet());
        time_elapsed = time_current - time_last;
        if (time_elapsed < 0.0f)
        {
            time_elapsed = SECS_PER_VBLANK;
        }

        xCMupdate(time_elapsed);

        if (gGameMode == eGameMode_Title)
        {
            sAttractMode_timer -= time_elapsed;
            sOneLiner_timer -= time_elapsed;

            if (sAllowAttract && !globals.noMovies && sAttractMode_timer < 0.0f)
            {
                zGameStateSwitch(1);
            }

            if (sOneLiner_timer < 0.0f)
            {
                xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("FROZONE_START_MENU_IDLE_GRP")), 0, NULL,
                            NULL, NULL, NULL, NULL);
                sOneLiner_timer = 46.0f;
            }

            if (gGameState == 1)
            {
                if (sFirstBoot)
                {
                    sFirstBoot = 0;
                }
                else
                {
                    fname = zFMVFileGetName(eFMVFile_AttractMode);
                    zMenuFMVPlay(fname, 0x3f3303, 0.1f, true, globals.firstStartPressed);
                }
                zGameStateSwitch(0);
                sAttractMode_timer = 45.0f;
            }
            else
            {
                zMusicUpdate(time_elapsed);
                for (i = 0; i < 4; i++)
                {
                    xRumble::Manager::Get().Update(globals.pad[i], xVec3::m_Null, time_elapsed,
                                                   true);
                }
            }

            if (mPad[globals.currentActivePad].pressed)
            {
                sAttractMode_timer = 45.0f;
                sOneLiner_timer = xrand_RandomFloatRange(46.0f, 90.0f);
            }
        }

        time_last = time_current;
        xPadUpdate(globals.currentActivePad, time_elapsed);

        if (!globals.firstStartPressed)
        {
            xPadUpdate(1, time_elapsed);
            xPadUpdate(2, time_elapsed);
            xPadUpdate(3, time_elapsed);
        }

        iDrawBegin();
        isPaused = zGameIsPaused();
        zSceneUpdate(time_elapsed);

        if (!isPaused)
        {
            xglobals->___player_ent_dont_use_directly->update(
                xglobals->___player_ent_dont_use_directly, globals.sceneCur, time_elapsed);
        }

        xSndMgrUpdate();
        iTRCDisk::TRCCheck();

        if (!isPaused || gGameMode == eGameMode_Title)
        {
            zCamUpdate(time_elapsed);
        }

        zCamPrepareRender();
        zCamBeginRender();
        zSceneRender();
        xDebugUpdate();

        if (draw_black)
        {
            RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
            RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
            xScrFxDrawScreenSizeRectangle();
        }

        iDrawEnd();
        zCamEndRender();
        iCameraShowRaster(globals.screen->icam);

        nextSceneID = zMenuUpdateMode();

        if (globals.sceneCur->pendingPortal)
        {
            if (gGameMode != eGameMode_Game)
            {
                zGameModeSwitch(eGameMode_Game);
            }
            zGameStateSwitch(0);
            nextSceneID = globals.sceneCur->pendingPortal->passet->sceneID;
        }

        gFrameCount++;

        if (ostrich_delay > 0)
        {
            ostrich_delay--;
        }
        else
        {
            gGameOstrich = eGameOstrich_InScene;
            if (gTrcPad[0].state != TRC_PadInserted)
            {
                globals.dontShowPadMessageDuringLoadingOrCutScene = TRUE;
                xTRCPad(gTrcPad[0].id, TRC_PadMissing);
            }
        }

        iFileAsyncService();
    } while (gGameMode != eGameMode_Game);

    g_flg_chEnabled = 0;
    return nextSceneID;
}

// Dead-stripped placeholder: retail's .data keeps an unreferenced, zeroed xtextbox here, the
// function-local 'static xtextbox tb' of zMenu code that did not survive. Only the static is
// restored.
static void __deadstripped_zMenu()
{
    static xtextbox tb = xtextbox::create();
}

// No symbol in TSSM: inlined into zMenuLoop.
inline U32 zMenuUpdateMode()
{
    U32 retVal = 0;
    F32 elapsed1 = iTimeDiffSec(iTimeGet()) - time_last;

    if (gGameMode == eGameMode_Load)
    {
        sTimeLast = iTimeGet();
        retVal = zSaveLoad_LoadLoop();
        if (retVal == '0000')
        {
            retVal = 0;
        }
        else if (retVal == SCENE_ID_MNU_START)
        {
            retVal = (globals.sceneStart[0] << 24) | (globals.sceneStart[1] << 16) |
                     (globals.sceneStart[2] << 8) | globals.sceneStart[3];
        }
    }
    else if (gGameMode == eGameMode_Save)
    {
        sTimeLast = iTimeGet();
        zSaveLoad_SaveLoop();
        retVal = (globals.sceneStart[0] << 24) | (globals.sceneStart[1] << 16) |
                 (globals.sceneStart[2] << 8) | globals.sceneStart[3];
    }

    time_last = iTimeDiffSec(iTimeGet()) - elapsed1;
    return retVal;
}

U32 zMenuGetCorruptFile(char name[][64])
{
    for (S32 i = 0; i < corruptFileCount; i++)
    {
        strcpy(name[i], corruptFiles[i]);
    }
    return corruptFileCount;
}

S32 zMenuCardCheckStartup(S32* bytesNeeded, S32* availOnDisk, S32* neededFiles)
{
    st_XSAVEGAME_DATA* ldinst = xSGInit(XSG_MODE_SAVE);
    S32 tgtcnt;
    S32 tgtmax;
    S32 rc = 0;
    S32 tgtslot;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        card = -1;
        xSGTgtPhysSlotIdx(ldinst, 0);
        xSGTgtSelect(ldinst, 0);
        tgtslot = xSGTgtIsFormat(ldinst, 0, NULL);
        if (tgtslot == 0)
        {
            *bytesNeeded = -1;
            *availOnDisk = -1;
            *neededFiles = -1;
            card = xSGTgtPhysSlotIdx(ldinst, 0);
            rc = 1;
            break;
        }
        else if (tgtslot < 0)
        {
            *bytesNeeded = -5;
            *availOnDisk = -5;
            *neededFiles = -5;
            card = xSGTgtPhysSlotIdx(ldinst, 0);
            rc = 1;
            break;
        }
        else
        {
            corruptFileCount = xSGCheckForCorruptFiles(ldinst, corruptFiles);
            if (corruptFileCount != 0)
            {
                *bytesNeeded = -4;
                *availOnDisk = -4;
                *neededFiles = -4;
                card = xSGTgtPhysSlotIdx(ldinst, 0);
                rc = 1;
                break;
            }
            else
            {
                rc = xSGTgtHaveRoomStartup(ldinst, 0, 0xCC00, -1, bytesNeeded, availOnDisk,
                                           neededFiles);
                if (rc)
                {
                    break;
                }
                card = xSGTgtPhysSlotIdx(ldinst, 0);
                rc = 0;
                break;
            }
        }

    case 1:
        if (xSGTgtPhysSlotIdx(ldinst, 0) == 1)
        {
            card = iSGCheckForWrongDevice();
            if (card >= 0)
            {
                rc = 1;
                *bytesNeeded = -2;
                *availOnDisk = -2;
                *neededFiles = -2;
                break;
            }
        }

        xSGTgtSelect(ldinst, 0);
        tgtslot = xSGTgtIsFormat(ldinst, 0, NULL);
        if (tgtslot == 0)
        {
            *bytesNeeded = -1;
            *availOnDisk = -1;
            *neededFiles = -1;
            card = xSGTgtPhysSlotIdx(ldinst, 0);
            rc = 1;
            break;
        }
        else if (tgtslot < 0)
        {
            *bytesNeeded = -5;
            *availOnDisk = -5;
            *neededFiles = -5;
            card = xSGTgtPhysSlotIdx(ldinst, 0);
            rc = 1;
            break;
        }
        else
        {
            corruptFileCount = xSGCheckForCorruptFiles(ldinst, corruptFiles);
            if (corruptFileCount != 0)
            {
                *bytesNeeded = -4;
                *availOnDisk = -4;
                *neededFiles = -4;
                card = xSGTgtPhysSlotIdx(ldinst, 0);
                rc = 1;
                break;
            }
            else
            {
                rc = xSGTgtHaveRoomStartup(ldinst, 0, 0xCC00, -1, bytesNeeded, availOnDisk,
                                           neededFiles);
                if (rc)
                {
                    break;
                }
                card = xSGTgtPhysSlotIdx(ldinst, 0);
                break;
            }
        }

    case 0:
        rc = 1;
        card = iSGCheckForWrongDevice();
        if (card >= 0)
        {
            *bytesNeeded = -2;
            *availOnDisk = -2;
            *neededFiles = -2;
            break;
        }
        else
        {
            *bytesNeeded = -3;
            *availOnDisk = -3;
            *neededFiles = -3;
            break;
        }
    }

    xSGDone(ldinst);
    return rc ? 0 : 1;
}

U32 zMenuGetBadCard()
{
    return card + 1;
}

// No symbol in TSSM (BfBB has an out-of-line copy): inlined at every call site.
inline void zMenuFMVPlay(char* filename, U32 buttons, F32 time, bool skippable, bool lockController)
{
    if (filename != NULL)
    {
        zFMVPlay(filename, buttons, time, 0, skippable, lockController);
        time_last = iTimeDiffSec(iTimeGet());
        time_current = time_last;
        sAttractMode_timer = 45.0f;
    }
}

S32 zMenuIsFirstBoot()
{
    return sFirstBoot;
}

void zMenuPause(bool bPause)
{
    if (bPause == FALSE)
    {
        time_last = iTimeDiffSec(iTimeGet()) - SECS_PER_VBLANK;
        sTimeLast = iTimeGet();
    }
}

void zMenuAllowAtract(bool allowAttract)
{
    sAllowAttract = allowAttract;
}
