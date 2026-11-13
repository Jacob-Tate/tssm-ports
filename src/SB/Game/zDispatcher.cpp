#include "zDispatcher.h"

#include "iTRC.h"
#include "iSystem.h"
#include "iCutscene.h"
#include "zNPCFXCinematic.h"
#include "xCM.h"
#include "zMusic.h"
#include "xScrFx.h"
#include "zSaveLoad.h"
#include "zHud.h"
#include "zRumble.h"
#include "zVar.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "xIni.h"
#include "xString.h"
#include "zMenuSB.h"
#include "zMenu.h"
#include "zGame.h"
#include "zScene.h"
#include "zPortal.h"
#include "zFMV.h"
#include "zCar.h"
#include "zCam.h"
#include "zCamera.h"
#include "zEntPlayer.h"
#include "zEntCruiseBubble.h"
#include "zUIMgr.h"
#include "zUIText.h"
#include "xstransvc.h"
#include "xCamera.h"
#include "xMath.h"
#include "xDynAsset.h"

#include <stdio.h>
#include <string.h>

static S32 g_zdsp_init;
static st_ZDISPATCH_DEPOT g_zdsp_depot = { 0 };

namespace
{
    struct World
    {
        char prefix[2];
        struct
        {
            char id[4];
            U32 startPointerId;
        } tasks[10];
    };

    World worlds[20];
    S16 currentWorld;
    S16 currentTask;
    S16 currentPointer;
} // namespace

extern U8 menu_fmv_played;
extern char zEventLogBuf[256][20];

static inline void ZDSP_instInit(st_ZDISPATCH_DATA* dspdata, xBaseAsset* bass);

static S32 ZDSP_doCommand(st_ZDISPATCH_DATA* dspdata, st_ZDISPATCH_CONTEXT* cmdCtxt);

static void WRAP_xsnd_setMusicVolume(S32 volLevel);

static void WRAP_xsnd_setSFXVolume(S32 volLevel);

void zDispatcherParseINI(xIniFile* ini)
{
    S32 i;
    char itemName[16];
    char value[256];
    char* value_;
    S32 j;
    char* ltok;
    char* tok;

    memset(worlds, 0, sizeof(worlds));
    currentWorld = 0;
    currentTask = 0;
    currentPointer = 0;

    for (i = 0; i < 20; i++)
    {
        sprintf(itemName, "Menu%02d", i + 1);
        value_ = (char*)xIniGetString(ini, itemName, NULL);
        j = 0;
        if (value_ == NULL)
        {
            worlds[i].prefix[0] = 'X';
            worlds[i].prefix[1] = 'X';
        }
        else
        {
            strcpy(value, value_);
            tok = xStrTok(value, " ", &ltok);
            worlds[i].prefix[0] = tok[0];
            worlds[i].prefix[1] = tok[1];
            tok = xStrTok(NULL, " ", &ltok);
            while (tok != NULL)
            {
                if (tok[2] != '\0' && tok[2] != '.')
                {
                    worlds[i].tasks[j].id[0] = tok[0];
                    worlds[i].tasks[j].id[1] = tok[1];
                    worlds[i].tasks[j].id[2] = tok[2];
                    worlds[i].tasks[j].id[3] = tok[3];
                    tok += 4;
                }
                else
                {
                    worlds[i].tasks[j].id[0] = worlds[i].prefix[0];
                    worlds[i].tasks[j].id[1] = worlds[i].prefix[1];
                    worlds[i].tasks[j].id[2] = tok[0];
                    worlds[i].tasks[j].id[3] = tok[1];
                    tok += 2;
                }

                if (*tok == '.')
                {
                    worlds[i].tasks[j].startPointerId = xStrHash(tok + 1);
                }
                else
                {
                    worlds[i].tasks[j].startPointerId = 0;
                }

                tok = xStrTok(NULL, " ", &ltok);
                j++;
            }
        }

        for (; j < 10; j++)
        {
            worlds[i].tasks[j].id[0] = worlds[i].prefix[0];
            worlds[i].tasks[j].id[1] = worlds[i].prefix[1];
            worlds[i].tasks[j].id[2] = '0';
            worlds[i].tasks[j].id[3] = '0';
            worlds[i].tasks[j].startPointerId = 0;
        }
    }
}

void zDispatcher_Startup()
{
}

void zDispatcher_Shutdown()
{
}

void zDispatcher_scenePrepare()
{
    st_ZDISPATCH_DEPOT* depot = &g_zdsp_depot;
    if (g_zdsp_init++ == 0)
    {
        memset(depot, 0, sizeof(st_ZDISPATCH_DEPOT));
    }
}

void zDispatcher_sceneFinish()
{
    st_ZDISPATCH_DEPOT* depot = &g_zdsp_depot;

    g_zdsp_init--;
    if (g_zdsp_init == 0)
    {
        memset(depot, 0, sizeof(st_ZDISPATCH_DEPOT));
    }
}

st_ZDISPATCH_DATA* zDispatcher_memPool(S32 cnt)
{
    st_ZDISPATCH_DATA* pool;
    st_ZDISPATCH_DEPOT* depot = &g_zdsp_depot;
    if (cnt < 1)
    {
        return NULL;
    }
    else
    {
        pool = (st_ZDISPATCH_DATA*)xMemAlloc(gActiveHeap, (U32)cnt * sizeof(st_ZDISPATCH_DATA), 0);
        memset(pool, 0, cnt * sizeof(st_ZDISPATCH_DATA));
        depot->raw_pool = pool;
        depot->raw_cnt = cnt;
        return pool;
    }
}

st_ZDISPATCH_DATA* zDispatcher_getInst(st_ZDISPATCH_DATA* pool, S32 idx)
{
    return &pool[idx];
}

void zDispatcher_Init(st_ZDISPATCH_DATA* dspdata, xBaseAsset* bass)
{
    ZDSP_instInit(dspdata, bass);
}

void zDispatcher_InitDep(st_ZDISPATCH_DATA* dspdata, zScene* zscn)
{
}

void zDispatcher_Save(st_ZDISPATCH_DATA* dspdata, xSerial* s)
{
    xBaseSave(dspdata, s);
}

void zDispatcher_Load(st_ZDISPATCH_DATA* dspdata, xSerial* s)
{
    xBaseLoad(dspdata, s);
}

static inline void ZDSP_instInit(st_ZDISPATCH_DATA* dspdata, xBaseAsset* bass)
{
    xBaseInit(dspdata, bass);
    dspdata->rawass = bass;
    dspdata->eventFunc = ZDSP_elcb_event;
    if (dspdata->linkCount != 0)
    {
        dspdata->link = (xLinkAsset*)(dspdata->rawass + 1);
    }
    else
    {
        dspdata->link = NULL;
    }
}

inline void ZDSP_instInitDep(st_ZDISPATCH_DATA* dspdata, zScene* scene)
{
}

inline void ZDSP_instReset(st_ZDISPATCH_DATA* dspdata, zScene* scene)
{
    xBaseAsset* ass = dspdata->rawass;
    xBaseReset(dspdata, ass);
    dspdata->placeholder = 0;
    ZDSP_instInit(dspdata, ass);
    ZDSP_instInitDep(dspdata, scene);
}

inline void ZDSP_injectCmd(st_ZDISPATCH_DATA* dspdata, en_DISPATCH_COMMAND cmd)
{
    ZDSP_injectCmd(dspdata, cmd, NULL, NULL, NULL);
}

inline void ZDSP_injectCmd(st_ZDISPATCH_DATA* dspdata, en_DISPATCH_COMMAND cmd, S32 i)
{
    ZDSP_injectCmd(dspdata, cmd, &i, NULL, NULL);
}

inline void ZDSP_injectCmd(st_ZDISPATCH_DATA* dspdata, en_DISPATCH_COMMAND cmd, void* indata,
                           void* inxtra, void* result)
{
    st_ZDISPATCH_CONTEXT ctx = { ZDSP_CMD_INITDFLT, NULL, NULL, NULL };
    ctx.cmd = cmd;
    ctx.indata = indata;
    ctx.inxtra = inxtra;
    ctx.result = result;
    ZDSP_doCommand(dspdata, &ctx);
}

static S32 ZDSP_doCommand(st_ZDISPATCH_DATA* dspdata, st_ZDISPATCH_CONTEXT* cmdCtxt)
{
    S32 iv;
    en_DISPATCH_COMMAND cmd = cmdCtxt->cmd;
    void* indata = cmdCtxt->indata;
    void* result = cmdCtxt->result;
    static S32 warned;
    static S8 init;

    if ((S32)init == 0)
    {
        warned = 0;
        init = 1;
    }
    if (warned == 0)
    {
        warned = 1;
    }

    switch (cmd)
    {
    case ZDSP_CMD_CTRL_CFGGET:
        *(S32*)result = 0;
        break;
    case ZDSP_CMD_CTRL_CFGSET:
        if (result != NULL)
        {
            *(S32*)result = 0;
        }
        break;
    case ZDSP_CMD_CTRL_VIBEGET:
        *(S32*)result = zVarEntryCB_VibrationOn(NULL);
        break;
    case ZDSP_CMD_CTRL_VIBESET:
        globals.option_vibration = *(S32*)indata;
        xPadRumbleEnable(globals.currentActivePad, globals.option_vibration);
        break;
    case ZDSP_CMD_CTRL_SUBTSET:
        globals.option_subtitles = *(S32*)indata;
        break;
    case ZDSP_CMD_SNDMOD_GET:
        *(S32*)result = zVarEntryCB_SndMode(NULL);
        break;
    case ZDSP_CMD_SNDMOD_SET:
        if (*(S32*)indata == 0)
        {
            gSnd.sndMode = xSndMode_Mono;
            iSndSetMode();
        }
        else if (*(S32*)indata == 1)
        {
            gSnd.sndMode = xSndMode_Dolby;
            iSndSetMode();
        }

        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndMode(NULL);
        }
        break;
    case ZDSP_CMD_MUSVOL_GET:
        *(S32*)result = zVarEntryCB_SndMusicVol(NULL);
        break;
    case ZDSP_CMD_MUSVOL_SET:
        WRAP_xsnd_setMusicVolume(*(S32*)indata);
        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndMusicVol(NULL);
        }
        break;
    case ZDSP_CMD_MUSVOL_INCR:
        iv = zVarEntryCB_SndMusicVol(NULL);
        if (indata != NULL)
        {
            iv += *(S32*)indata;
        }
        else
        {
            iv += 1;
        }
        WRAP_xsnd_setMusicVolume(iv);
        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndMusicVol(NULL);
        }
        break;
    case ZDSP_CMD_MUSVOL_DECR:
        iv = zVarEntryCB_SndMusicVol(NULL);
        if (indata != NULL)
        {
            iv -= *(S32*)indata;
        }
        else
        {
            iv -= 1;
        }
        WRAP_xsnd_setMusicVolume(iv);
        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndMusicVol(NULL);
        }
        break;
    case ZDSP_CMD_SFXVOL_GET:
        *(S32*)result = zVarEntryCB_SndFXVol(NULL);
        break;
    case ZDSP_CMD_SFXVOL_SET:
        WRAP_xsnd_setSFXVolume(*(S32*)indata);
        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndFXVol(NULL);
        }
        break;
    case ZDSP_CMD_SFXVOL_INCR:
        iv = zVarEntryCB_SndFXVol(NULL);
        if (indata != NULL)
        {
            iv += *(S32*)indata;
        }
        else
        {
            iv += 1;
        }
        WRAP_xsnd_setSFXVolume(iv);
        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndFXVol(NULL);
        }
        break;
    case ZDSP_CMD_SFXVOL_DECR:
        iv = zVarEntryCB_SndFXVol(NULL);
        if (indata != NULL)
        {
            iv -= *(S32*)indata;
        }
        else
        {
            iv -= 1;
        }
        WRAP_xsnd_setSFXVolume(iv);
        if (result != NULL)
        {
            *(S32*)result = zVarEntryCB_SndFXVol(NULL);
        }
        break;
    case ZDSP_CMD_GSTATE_GET:
        *(S32*)result = zGameStateGet();
        break;
    case ZDSP_CMD_GSTATE_SET:
        zGameStateSwitch(*(S32*)indata);
        if (result != NULL)
        {
            *(S32*)result = zGameStateGet();
        }
        break;
    case ZDSP_CMD_GMODE_GET:
        *(S32*)result = zGameModeGet();
        break;
    case ZDSP_CMD_GMODE_SET:
        zGameModeSwitch(*(eGameMode*)indata);
        if (result != NULL)
        {
            *(S32*)result = zGameModeGet();
        }
        break;
    case ZDSP_CMD_CTRL_SUBTGET:
    case ZDSP_CMD_SNDVOL_GET:
    case ZDSP_CMD_SNDVOL_SET:
    case ZDSP_CMD_SNDVOL_INCR:
    case ZDSP_CMD_SNDVOL_DECR:
    case ZDSP_CHECKPNT_SET:
        break;
    }
    return 1;
}

// pointer_asset is defined in zCam.cpp, earlier in this wad.
// No symbol (always inlined); the name is a guess, after zGame.cpp's pointer check.
static inline bool ZDSP_isPointerAsset(const xDynAsset* a)
{
    return a->baseType == 0 && a->version == 1 && a->type == xStrHash(pointer_asset::type_name());
}

// Defined after ZDSP_doCommand: retail's .sbss has ZDSP_doCommand's warned/init before them.
static S32 oldVibrationOption;
static xSndMode oldSoundMode;
static U32 oldMusicVolume;
static U32 oldSFXVolume;

inline void zDispatcherStoreOptions()
{
    oldVibrationOption = globals.option_vibration;
    oldSoundMode = gSnd.sndMode;
    oldMusicVolume = zVarEntryCB_SndMusicVol(NULL);
    oldSFXVolume = zVarEntryCB_SndFXVol(NULL);
}

inline void zDispatcherRestoreOptions()
{
    xPadRumbleEnable(globals.currentActivePad, oldVibrationOption);
    globals.option_vibration = oldVibrationOption;
    gSnd.sndMode = oldSoundMode;
    iSndSetMode();
    WRAP_xsnd_setMusicVolume(oldMusicVolume);
    WRAP_xsnd_setSFXVolume(oldSFXVolume);
}

void ZDSP_elcb_event(xBase* from, xBase* xb, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                     U32 toParamWidgetID)
{
    st_ZDISPATCH_DATA* dspdata = (st_ZDISPATCH_DATA*)xb;

    switch (toEvent)
    {
    case eEventEnable:
    case eEventDisable:
        break;
    case eEventReset:
        ZDSP_instReset(dspdata, globals.sceneCur);
        break;
    case eEventMusicNewSong:
        zMusicNewSongEvent(toParam, toParamWidgetID);
        break;
    case eEventMusicTempSong:
        if (zMenuSB_EnsureFreeAudioStream(true))
        {
            zMusicTempSongEvent(toParam, toParamWidgetID);
        }
        break;
    case eEventMusicTempSongStop:
        zMusicTempSongStopEvent();
        break;
    case eEventSceneBegin:
    case eEventSceneEnd:
    case eEventRoomBegin:
    case eEventRoomEnd:
        break;
    case eEventDispatcher_PadCfg_PresetA:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_CFGSET, 0);
        break;
    case eEventDispatcher_PadCfg_PresetB:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_CFGSET, 1);
        break;
    case eEventDispatcher_PadCfg_PresetC:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_CFGSET, 2);
        break;
    case eEventDispatcher_PadCfg_PresetD:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_CFGSET, 3);
        break;
    case eEventDispatcher_PadVibrateOn:
    {
        S32 old_vibration = globals.option_vibration;
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_VIBESET, 1);
        if (toParamWidget != NULL && old_vibration == 0)
        {
            zEntEvent(toParamWidget, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f,
                      0.0f);
        }
        break;
    }
    case eEventDispatcher_PadVibrateOff:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_VIBESET, 0);
        break;
    case eEventDispatcher_SubtitlesOn:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_SUBTSET, 1);
        break;
    case eEventDispatcher_SubtitlesOff:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_CTRL_SUBTSET, 0);
        break;
    case eEventDispatcher_SoundMono:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_SNDMOD_SET, 0);
        break;
    case eEventDispatcher_SoundStereo:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_SNDMOD_SET, 1);
        break;
    case eEventDispatcher_SoundMasterIncrease:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_SNDVOL_INCR);
        break;
    case eEventDispatcher_SoundMasterDecrease:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_SNDVOL_DECR);
        break;
    case eEventDispatcher_SoundMusicIncrease:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_MUSVOL_INCR);
        break;
    case eEventDispatcher_SoundMusicDecrease:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_MUSVOL_DECR);
        break;
    case eEventDispatcher_SoundSFXIncrease:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_SFXVOL_INCR);
        break;
    case eEventDispatcher_SoundSFXDecrease:
        ZDSP_injectCmd(dspdata, ZDSP_CMD_SFXVOL_DECR);
        break;
    case eEventDispatcher_SetSoundEffect:
        xSndMgrSetEffect((xSndEffect)*toParam);
        break;
    case eEventAllowAttractMode:
        zMenuAllowAtract(true);
        break;
    case eEventDisallowAttractMode:
        zMenuAllowAtract(false);
        break;
    case eEventDispatcher_SetIntroState_Sony:
    case eEventDispatcher_SetIntroState_Publisher:
    case eEventDispatcher_SetIntroState_Developer:
    case eEventDispatcher_SetIntroState_License:
    case eEventDispatcher_SetIntroState_Count:
    case eEventDispatcher_SetTitleState_Start:
    case eEventDispatcher_SetTitleState_Attract:
    case eEventDispatcher_SetTitleState_Count:
    case eEventDispatcher_SetLoadState_SelectMemCard:
    case eEventDispatcher_SetLoadState_SelectSlot:
    case eEventDispatcher_SetLoadState_Loading:
    case eEventDispatcher_SetLoadState_Count:
    case eEventDispatcher_SetOptionsState_Options:
    case eEventDispatcher_SetOptionsState_Count:
    case eEventDispatcher_SetSaveState_SelectMemCard:
    case eEventDispatcher_SetSaveState_SelectSlot:
    case eEventDispatcher_SetSaveState_Saving:
    case eEventDispatcher_SetSaveState_Count:
    case eEventDispatcher_SetPauseState_Pause:
    case eEventDispatcher_SetPauseState_Options:
    case eEventDispatcher_SetPauseState_Count:
    case eEventDispatcher_SetGameState_Play:
    case eEventDispatcher_SetGameState_LoseChance:
    case eEventDispatcher_SetGameState_GameOver:
    case eEventDispatcher_SetGameState_SceneSwitch:
    case eEventDispatcher_SetGameState_Dead:
    case eEventDispatcher_GameState_Exit:
    case eEventDispatcher_SetGameState_Exit:
        zGameStateSwitchEvent(toEvent);
        break;
    case eEventDispatcher_PauseGame_Safe:
        if (zGameOkToPause())
        {
            zGamePause();
        }
        break;
    case eEventDispatcher_PauseGame:
        zGamePause();
        break;
    case eEventDispatcher_SetGameState_FirstTime:
        globals.autoSaveFeature = FALSE;
        zGameStateSwitchEvent(toEvent);
        break;
    case eEventDispatcher_SLBack:
    case eEventDispatcher_SLCancel:
    case eEventDispatcher_SLRetry:
    case eEventDispatcher_SLSelectCard:
    case eEventDispatcher_SLSelectSlot:
    case eEventDispatcher_SLOkay:
    case eEventDispatcher_UserSelectYes:
    case eEventDispatcher_UserSelectNo:
    case eEventDispatcher_UserSelectBack:
    case eEventDispatcher_UserSelectionReset:
    case eEventDispatcher_LoadSavePromptDead:
        zSaveLoad_DispatchCB(toEvent, toParam);
        break;
    case eEventDispatcher_StoreCheckPoint:
    {
        pointer_asset* pointerAsset = (pointer_asset*)toParamWidget;
        if (!ZDSP_isPointerAsset(pointerAsset))
        {
            // The name is looked up but unused (presumably for a stripped debug message).
            xSTAssetName(from->id);
        }
        else
        {
            zSceneStoreCheckPoint(pointerAsset->loc, (PI / 180.0f) * pointerAsset->yaw, 0);
            zSceneDiskSpin(1.0f);
        }
        break;
    }
    case eEventDispatcher_ShowHud:
        zHud::hud_system::instance().show(true);
        break;
    case eEventDispatcher_HideHud:
        zHud::hud_system::instance().hide();
        break;
    case eEventDispatcher_FadeOut:
        xScrFxFade(&g_CLEAR, &g_BLACK, *toParam, 1);
        break;
    case eEventPlayMovie:
    {
        zFMVFile* file = zFMVFileGetFile((eFMVFile)(U32)*toParam);
        if (file != NULL)
        {
            zFMVPlay(file->fileName, 0x90001, 0.1f, 0, true, true);
        }
        break;
    }
    case eEventDispatcherScreenAdjustON:
        zSceneEnableScreenAdj(1);
        zSceneSetOldScreenAdj();
        break;
    case eEventDispatcherScreenAdjustOFF:
        zSceneEnableScreenAdj(0);
        break;
    case eEventForceSceneReset:
        zGameStateSwitch(eGameState_LoseChance);
        break;
    case eEventCameraFXShake:
        xCameraFXShake(toParam[0], toParam[1], toParam[2], toParam[3], 0.0f, NULL, NULL, false);
        break;
    case eEventWarpSetTask:
    {
        currentTask = *toParam - 1.0f;
        if (*toParam != (S32)*toParam)
        {
            F32 pointer = *toParam - (S32)*toParam;
            pointer *= 10.0f;
            currentPointer = pointer - 1.0f;
        }
        else
        {
            currentPointer = 0;
        }
        if (currentTask < 0 || currentTask >= 10)
        {
            currentTask = 0;
        }
        break;
    }
    case eEventWarpSetWorld:
        currentWorld = *toParam - 1.0f;
        if (currentWorld < 0 || currentWorld >= 20)
        {
            currentWorld = 0;
        }
        break;
    case eEventWarpGo:
    {
        static _zPortal portal;
        static xPortalAsset portalAsset;

        memset(&portalAsset, 0, sizeof(xPortalAsset));
        portalAsset.baseType = eBaseTypePortal;
        if (worlds[currentWorld].tasks[currentTask].startPointerId == 0)
        {
            portalAsset.assetCameraID = 0xBC44C2B2;
            portalAsset.assetMarkerID = 0xD03C41C6;
            portalAsset.sceneID = (worlds[currentWorld].tasks[currentTask].id[0] << 24) |
                                  (worlds[currentWorld].tasks[currentTask].id[1] << 16) |
                                  (worlds[currentWorld].tasks[currentTask].id[2] << 8) |
                                  worlds[currentWorld].tasks[currentTask].id[3];
        }
        else
        {
            portalAsset.assetMarkerID = worlds[currentWorld].tasks[currentTask].startPointerId;
            portalAsset.assetCameraID = 0xBC44C2B2;
            portalAsset.sceneID = (worlds[currentWorld].tasks[currentTask].id[0] << 24) |
                                  (worlds[currentWorld].tasks[currentTask].id[1] << 16) |
                                  (worlds[currentWorld].tasks[currentTask].id[2] << 8) |
                                  worlds[currentWorld].tasks[currentTask].id[3];
        }
        zMusicKill();
        zPortalInit(&portal, &portalAsset);
        zSceneSwitch(&portal, 1);
        if (*toParam)
        {
            zGameStateSwitchEvent(eEventDispatcher_SetGameState_FirstTime);
        }
        else
        {
            zGameStateSwitchEvent(eEventDispatcher_SetGameState_Play);
        }
        globals.sceneCur->enableDrawing = FALSE;
        break;
    }
    case eEventSceneEnableDraw:
        globals.sceneCur->enableDrawing = TRUE;
        break;
    case eEventSceneDisableDraw:
        globals.sceneCur->enableDrawing = FALSE;
        break;
    case eEventSetCameraStartOrientation:
        zCamSetStartOrientation(toParamWidgetID);
        break;
    case eEventSetFollowCameraOrientation:
        zCameraSetFollowOrientation(toParam[0], toParam[1]);
        break;
    case eEventFireCruiseBubble:
        if (cruise_bubble::CheckAbilityToLaunch())
        {
            cruise_bubble::launch();
        }
        break;
    case eEventCarSuccessAnimPlay:
    {
        zCar* car = zEntPlayerCarGetCar();
        if (car != NULL && gCurrentPlayer == eCurrentPlayerCar)
        {
            car->animSuccessMode = TRUE;
        }
        break;
    }
    case eEventCarFailureAnimPlay:
    {
        zCar* car = zEntPlayerCarGetCar();
        if (car != NULL && gCurrentPlayer == eCurrentPlayerCar)
        {
            car->animFailedMode = TRUE;
        }
        break;
    }
    case eEventUpgradePowerUp:
        zEntEvent(xglobals->___player_ent_dont_use_directly, eEventUpgradePowerUp, toParam);
        break;
    case eEventGivePowerUp:
        zEntEvent(xglobals->___player_ent_dont_use_directly, eEventGivePowerUp, toParam);
        break;
    case eEventUISparkTrail:
        if (toParamWidgetID != 0)
        {
            zUITextAsset* uia = (zUITextAsset*)xSTFindAsset(toParamWidgetID, NULL);
            xVec3 start;
            xVec3 end;
            if (toParam != NULL)
            {
                start.x = uia->x + toParam[0];
                start.y = uia->y + toParam[1];
                start.z = uia->z + toParam[2];
                end.x = toParam[0] + (uia->x + uia->width);
                end.y = uia->y + toParam[1];
                end.z = uia->z + toParam[2];
            }
            else
            {
                start.x = uia->x;
                start.y = uia->y;
                start.z = uia->z;
                end.x = uia->x + uia->width;
                end.y = uia->y;
                end.z = uia->z;
            }
            UpgradeSparkleEffect::Start(start, end, 2.0f, 0.1f, 1.5f, 0.5f);
        }
        break;
    case eEventCreditsStart:
        xCMstart((xCreditsData*)toParamWidget, *toParam, xb);
        break;
    case eEventCreditsStop:
        xCMstop();
        break;
    case eEventCreditsSetDest:
        xCMsetDest(toParam[0], toParam[1], toParam[2], toParam[3]);
        break;
    case eEventUIEnableHDR:
        zUIMgrEnableHDR(true);
        break;
    case eEventUIDisableHDR:
        zUIMgrEnableHDR(false);
        break;
    case eEventAutoSave:
        if (globals.autoSaveFeature)
        {
            zSaveLoad_TriggerAutoSave();
        }
        break;
    case eEventStoreOptions:
        zDispatcherStoreOptions();
        break;
    case eEventRestoreOptions:
        zDispatcherRestoreOptions();
        break;
    case eEventDigup:
    case eEventLobMasterShootFromWidget:
    case eEventDispatcherAssert:
    case eEventHDRFade:
        break;
    }
}

// No GC symbol, but plain static (as in BfBB), not inline: the auto-inlined function is still
// compiled, which puts its int->float constant here in retail .sdata2.
static void WRAP_xsnd_setMusicVolume(S32 volLevel)
{
    F32 musicVol = 0.1f * volLevel;
    F32 clampedVolume = CLAMP(musicVol, 0.0f, 1.0f);

    xSndMgrSetUserVolume(3, clampedVolume);
}

static void WRAP_xsnd_setSFXVolume(S32 volLevel)
{
    F32 sfxVol = 0.1f * volLevel;
    F32 clampedVolume = CLAMP(sfxVol, 0.0f, 1.0f);

    xSndMgrSetUserVolume(-2, clampedVolume);
}

void zDispatcher_FindWorldTask(U32 sceneID, S32& world, S32& task)
{
    char sceneName[4];
    S32 i;
    S32 j;

    sceneName[0] = (sceneID >> 24) & 0xFF;
    sceneName[1] = (sceneID >> 16) & 0xFF;
    sceneName[2] = (sceneID >> 8) & 0xFF;
    sceneName[3] = sceneID & 0xFF;

    world = 0;
    task = 0;

    for (i = 0; i < 20; i++)
    {
        for (j = 0; j < 10; j++)
        {
            if (*(U32*)worlds[i].tasks[j].id == *(U32*)sceneName)
            {
                world = i + 1;
                task = j + 1;
                return;
            }
        }
    }
}

U32 zDispatcher_FindTaskPointer(U32 sceneID, S32 task)
{
    char sceneName[4];
    S32 i;
    S32 j;

    sceneName[0] = sceneID >> 24;
    sceneName[1] = sceneID >> 16;
    sceneName[2] = sceneID >> 8;
    sceneName[3] = sceneID;

    for (i = 0; i < 20; i++)
    {
        for (j = 0; j < 10; j++)
        {
            if (*(U32*)worlds[i].tasks[j].id == *(U32*)sceneName)
            {
                return worlds[i].tasks[task - 1].startPointerId;
            }
        }
    }

    return 0;
}

U32 zDispatcher_FindNextValidWorld(U32 sceneID)
{
    char sceneName[4];
    S32 worldIndex;
    S32 i;
    S32 j;
    S32 newIndex;

    sceneName[0] = sceneID >> 24;
    sceneName[1] = sceneID >> 16;
    sceneName[2] = sceneID >> 8;
    sceneName[3] = sceneID;

    worldIndex = -1;
    for (i = 0; i < 20; i++)
    {
        for (j = 0; j < 10; j++)
        {
            if (*(U32*)worlds[i].tasks[j].id == *(U32*)sceneName)
            {
                worldIndex = i;
                break;
            }
        }

        if (worldIndex != -1)
        {
            break;
        }
    }

    if (worldIndex == -1)
    {
        return sceneID;
    }

    newIndex = worldIndex;
    do
    {
        if (zMenuSB_IsTaskUnlocked(
                (worlds[newIndex].tasks[0].id[0] << 24) | (worlds[newIndex].tasks[0].id[1] << 16) |
                    (worlds[newIndex].tasks[0].id[2] << 8) | worlds[newIndex].tasks[0].id[3],
                0))
        {
            break;
        }

        newIndex++;
        if (newIndex >= 20)
        {
            newIndex = 0;
        }
    } while (newIndex != worldIndex);

    if (newIndex == worldIndex)
    {
        return sceneID;
    }

    return (worlds[newIndex].tasks[0].id[0] << 24) | (worlds[newIndex].tasks[0].id[1] << 16) |
           (worlds[newIndex].tasks[0].id[2] << 8) | worlds[newIndex].tasks[0].id[3];
}

S16 zDispatcher_GetCurrentWorld()
{
    return currentWorld;
}
