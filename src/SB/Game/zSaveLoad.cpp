#include "iTRC.h"

#include "xSnd.h"

#include "zSaveLoad.h"
#include "xutil.h"
#include "xParMgr.h"
#include "xDebug.h"
#include "iCamera.h"
#include "iDraw.h"
#include "zCam.h"
#include "zGame.h"
#include "zGameState.h"
#include "zHud.h"
#include "zMain.h"
#include "zScene.h"

#include <dolphin/os.h>

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

U32 saveSuccess;
static F32 saveload_time_last;
static F32 saveload_time_current;
static F32 saveload_time_elapsed = 0.01f;
S32 saveLoadPromptSel;
S32 badCard;
S32 sAvailable;
S32 sNeeded;
S32 sAccessType;

namespace
{
    S32 loadState;
    S32 saveState;
} // namespace

#ifdef PLATFORM_PC
// PLATFORM_PC: zSaveLoad_CardPick went straight to the only folder holding
// saves (iSGOnlyTargetWithGames), so backing out of the saves leaves the load
// screen.
static S32 sCardPickSkipped;

// PLATFORM_PC: a test's save by hand (TSSM_TEST_SAVE, iTestEvents.cpp): the
// next save screen goes straight to saving into this slot of the first
// folder, as if the player had picked them; -1: none asked.
static S32 sPcTestSaveGame = -1;

void zSaveLoad_PcTestSaveByHand(S32 game)
{
    sPcTestSaveGame = game;
}
#endif

U8 preAutoSaving;

S32 currentCard = -1;
S32 currentGame = -1;
F32 dontPoll = 1.0f;
S32 autoSaveCard = -1;

namespace
{
    U8 sIsLoading = 1;
} // namespace

S32 g_promptUserSelection = -1;

char currSceneStr[32] = "TEMP";
char sceneRead[32] = "0000";
zSaveLoadUI zSaveLoadUITable[63] = {
    { 0, 0, "ld gameslot group" },
    { 1, 0, "sv memcards group" },
    { 2, 0, "sv format prompt group" },
    { 3, 0, "sv mc missing group" },
    { 4, 0, "mnu4 ld mc1" },
    { 5, 0, "mnu4 ld mc2" },
    { 6, 0, "sv gameslot 0" },
    { 7, 0, "sv gameslot 1" },
    { 8, 0, "sv gameslot 2" },
    { 9, 0, "ld gameslot 3" },
    { 10, 0, "ld gameslot 4" },
    { 11, 0, "ld gameslot 5" },
    { 12, 0, "ld gameslot 6" },
    { 13, 0, "ld gameslot 7" },
    { 14, 0, "sv format prompt" },
    { 15, 0, "sv format yes" },
    { 16, 0, "sv format no" },
    { 17, 0, "sv mc missing" },
    { 18, 0, "ld nogames" },
    { 19, 0, "ld nogame" },
    { 20, 0, "start group" },
    { 21, 0, "sv gameslot group" },
    { 22, 0, "sv memcards group" },
    { 23, 0, "mnu4 ld mc1" },
    { 24, 0, "mnu4 ld mc2" },
    { 25, 0, "sv gameslot 0" },
    { 26, 0, "sv gameslot 1" },
    { 27, 0, "sv gameslot 2" },
    { 28, 0, "sv gameslot 3" },
    { 29, 0, "sv gameslot 4" },
    { 30, 0, "sv gameslot 5" },
    { 31, 0, "sv gameslot 6" },
    { 32, 0, "sv gameslot 7" },
    { 33, 0, "sv format prompt" },
    { 34, 0, "sv overwrite" },
    { 35, 0, "sv overwrite damaged" },
    { 36, 0, "sv mc missing" },
    { 37, 0, "sv nospace" },
    { 38, 0, "sv nospacegame" },
    { 39, 0, "ld mc dontremove" },
    { 40, 0, "sv mc dontremove" },
    { 41, 0, "sv mc dontremove" },
    { 42, 0, "ld damagedsave" },
    { 43, 0, "sv damaged save" },
    { 44, 0, "sv badformat" },
    { 45, 0, "sv badformatnocard" },
    { 46, 0, "sv badformat" },
    { 47, 0, "sv badformatnocard" },
    { 48, 0, "sv format confirm" },
    { 49, 0, "sv format confirm" },
    { 50, 0, "sv format group" },
    { 51, 0, "mnu4 disk free" },
    { 52, 0, "mnu4 disk free" },
    { 53, 0, "sv damaged card" },
    { 54, 0, "sv badsavenocard" },
    { 55, 0, "ld damaged card" },
    { 56, 0, "sv damaged card" },
    { 57, 0, "sv cardwrongdevice" },
    { 58, 0, "sv cardwrongdevice" },
    { 59, 0, "ld damagedsave" },
    { 60, 0, "ld damagedsave" },
    { 61, 0, "ld badload" },
    { 0, 0, NULL },
};

inline void zSaveLoadUITableInit(zSaveLoadUI* saveTable)
{
    for (S32 i = 0; i < 62; i++)
    {
        saveTable[i].nameID = xStrHash(saveTable[i].name);
    }
}
inline void zSaveLoadGameTableInit(zSaveLoadGame* saveTable)
{
    for (S32 i = 0; i < 3; i++)
    {
        saveTable[i].label[0] = 0;
        saveTable[i].date[0] = 0;
        saveTable[i].progress = 0;
        saveTable[i].size = 0;
        saveTable[i].thumbIconIndex = 0;
    }
}
inline void zSaveLoadInit()
{
    zSaveLoadUITableInit(zSaveLoadUITable);
    zSaveLoadGameTableInit(zSaveLoadGameTable);
}
inline void zSaveLoad_UIEvent(S32 i, U32 toEvent)
{
    zEntEvent(zSceneFindObject(zSaveLoadUITable[i].nameID), toEvent);
}
inline st_XSAVEGAME_DATA* zSaveLoadSGInit(en_SAVEGAME_MODE mode)
{
    if (mode == XSG_MODE_LOAD)
    {
        zSaveLoad_UIEvent(0x27, eEventVisible);
    }
    else
    {
        zSaveLoad_UIEvent(0x28, eEventVisible);
    }

    for (S32 i = 0; i < 801; i++)
    {
        if (i % 200 == 0)
        {
            zSaveLoad_Tick(true);
        }
    }

    return xSGInit(mode);
}
inline S32 zSaveLoadSGDone(st_XSAVEGAME_DATA* data)
{
    if (data->mode == XSG_MODE_LOAD)
    {
        zSaveLoad_UIEvent(0x27, eEventInvisible);
    }
    else
    {
        zSaveLoad_UIEvent(0x28, eEventInvisible);
    }
    return xSGDone(data);
}
inline S32 CardtoTgt(S32 card)
{
    st_XSAVEGAME_DATA* ldinst = xSGInit(XSG_MODE_LOAD);
    S32 tgtmax;

    switch (xSGTgtCount(ldinst, &tgtmax))
    {
    case 2:
        xSGDone(ldinst);
        return card;
    case 1:
        xSGDone(ldinst);
        return 0;
    case 0:
        xSGDone(ldinst);
        return -1;
    }
    return -1;
}
inline S32 zSaveLoad_CardCount()
{
    st_XSAVEGAME_DATA* ldinst = xSGInit(XSG_MODE_LOAD);
    S32 tgtcnt;
    S32 tgtmax;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    xSGDone(ldinst);
    return tgtcnt;
}

S32 zSaveLoad_CardCheckFormatted(int, int);

// No symbol in the target: always inlined. Retail keeps only the loop over the game slots; the
// thumbnail icon update in its body (see BfBB) is not compiled into the GameCube build.
inline void zUpdateThumbIcon()
{
    S32 i;
    S32 start;
    S32 end;
    if (gGameMode == eGameMode_Load)
    {
        start = 6;
        end = 9;
    }
    else
    {
        start = 25;
        end = 28;
    }

    for (i = start; i <= end; i++)
    {
    }
}

void zSaveLoad_Tick(bool padUpdate)
{
    saveload_time_current = (1.0f / OS_TIMER_CLOCK) * (F32)iTimeGet();

    saveload_time_elapsed = saveload_time_current - saveload_time_last;
    if (saveload_time_elapsed < 0.0f)
    {
        saveload_time_elapsed = SECS_PER_VBLANK;
    }
    else if (saveload_time_elapsed > 0.1f)
    {
        saveload_time_elapsed = SECS_PER_VBLANK;
    }

    dontPoll = dontPoll - saveload_time_elapsed;
    saveload_time_last = saveload_time_current;

    sTimeCurrent = iTimeGet();
    sTimeElapsed = iTimeDiffSec(sTimeLast, sTimeCurrent);
    sTimeLast = sTimeCurrent;

    if (padUpdate)
    {
        xPadUpdate(globals.currentActivePad, saveload_time_elapsed);
    }

    iTRCDisk::TRCCheck();
    iDrawBegin();
    xParMgrUpdate(saveload_time_elapsed);
    zSceneUpdate(saveload_time_elapsed);

    xMat4x3 playerMat = *xEntGetFrame(xglobals->___player_ent_dont_use_directly);
    playerMat.pos.y += 0.6f;
    xSndSetListenerData(xSndListener_CAMERA, &globals.cam->mat, globals.cam->vel);

    xVec3 playerVel = (xglobals->___player_ent_dont_use_directly->frame->mat.pos -
                       xglobals->___player_ent_dont_use_directly->frame->oldmat.pos) *
                      (1.0f / sTimeElapsed);
    xSndSetListenerData(xSndListener_PLAYER, &playerMat, playerVel);
    xSndMgrUpdate();

    if (gGameMode == eGameMode_Save && gGameState != 0)
    {
        zHud::hud_system::instance().update(sTimeElapsed);
    }

    zCamUpdate(saveload_time_elapsed);
    zCamPrepareRender();
    zCamBeginRender();
    zUpdateThumbIcon();
    zSceneRender();
    xDebugUpdate();
    iDrawEnd();
    zCamEndRender();
    iCameraShowRaster(globals.screen->icam);
    gFrameCount++;
}

// Not in retail: nothing calls these two, so the linker strips them, but their strings remain in
// retail's string pool.
void zSendEventToThumbIcon(U32 toEvent)
{
    const char* iconString = gGameMode == eGameMode_Load ? "MNU3 THUMBICON" : "MNU4 THUMBICON";
    zEntEvent(zSceneFindObject(xStrHash(iconString)), toEvent);
}

void zChangeThumbIcon(const char* icon)
{
    S32 arr[4];

    memset(arr, 0, sizeof(arr));
    arr[0] = xStrHash(icon);

    zEntEvent(zSceneFindObject(
                  xStrHash(gGameMode == eGameMode_Load ? "MNU3 THUMBICON" : "MNU4 THUMBICON")),
              eEventUIChangeTexture, (F32*)arr);
}

// No symbols: both format error prompts are always inlined into format, which ignores their
// result (BfBB's versions return 6).
inline S32 zSaveLoad_ErrorFormatPrompt(S32 mode)
{
    zSaveLoad_UIEvent(mode == 1 ? 0x2c : 0x2e, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }

    zSaveLoad_UIEvent(mode == 1 ? 0x2c : 0x2e, eEventUIFocusOff);
    return 6;
}

inline S32 zSaveLoad_ErrorFormatCardYankedPrompt(S32 mode)
{
    zSaveLoad_UIEvent(mode == 1 ? 0x2d : 0x2f, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }

    zSaveLoad_UIEvent(mode == 1 ? 0x2d : 0x2f, eEventUIFocusOff);
    return 6;
}

// The PS2 DWARF lists no local for the save game data; Ratatouille's DWARF names it ldinst.
S32 format(S32 num, S32 mode)
{
    st_XSAVEGAME_DATA* ldinst;
    S32 tgtcnt;
    S32 tgtmax;
    S32 rc;
    S32 tgtslot;

    ldinst = zSaveLoadSGInit(mode == 1 ? XSG_MODE_LOAD : XSG_MODE_SAVE);
    rc = 0;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        xSGTgtSelect(ldinst, num);
        rc = xSGTgtFormatTgt(ldinst, num, NULL);
        zSaveLoadSGDone(ldinst);
        if (rc == -1)
        {
            zSaveLoad_ErrorFormatCardYankedPrompt(mode);
            rc = 5;
        }
        else
        {
            if (rc == 0)
            {
                zSaveLoad_ErrorFormatPrompt(mode);
            }
            if (rc != 0)
            {
                rc = 1;
            }
        }
        break;
    case 1:
        tgtslot = xSGTgtPhysSlotIdx(ldinst, 0);
        if (tgtslot != num)
        {
            zSaveLoadSGDone(ldinst);
            rc = 5;
        }
        else if (tgtslot ^ num)
        {
            zSaveLoadSGDone(ldinst);
        }
        else
        {
            xSGTgtSelect(ldinst, 0);
            rc = xSGTgtFormatTgt(ldinst, 0, NULL);
            zSaveLoadSGDone(ldinst);
            if (rc == 0)
            {
                zSaveLoad_ErrorFormatPrompt(mode);
            }
        }
        break;
    case 0:
        rc = 5;
        zSaveLoadSGDone(ldinst);
        break;
    }
    return rc;
}

// The UI index is a local set by an if statement; a ternary compiles to branchless code here.
S32 zSaveLoad_CardPrompt(S32 mode)
{
    S32 i = 0x15;
    if (mode == 1)
    {
        i = 0;
    }
    zSaveLoad_UIEvent(i, eEventEnable);

    i = 0x15;
    if (mode == 1)
    {
        i = 0;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);

    i = 0x16;
    if (mode == 1)
    {
        i = 1;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);
    zSaveLoad_UIEvent(0x34, eEventInvisible);

    i = 0x24;
    if (mode == 1)
    {
        i = 0x11;
    }
    zSaveLoad_UIEvent(i, eEventVisible);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }

    i = 0x24;
    if (mode == 1)
    {
        i = 0x11;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);

    if (mode == 1)
    {
        zSaveLoad_UIEvent(0, eEventDisable);
    }

    if (saveLoadPromptSel != 3 && (mode != 1 || loadState != 0) && (mode != 0 || saveState != 0))
    {
        i = 0x16;
        if (mode == 1)
        {
            i = 1;
        }
        zSaveLoad_UIEvent(i, eEventVisible);
    }
    return saveLoadPromptSel;
}

S32 zSaveLoad_CardPromptFormat(S32 mode)
{
    S32 i = 0x15;
    if (mode == 1)
    {
        i = 0;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);

    i = 0x16;
    if (mode == 1)
    {
        i = 1;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);
    zSaveLoad_UIEvent(0x21, eEventVisible);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
        zSaveLoad_poll(5);
    }
    zSaveLoad_UIEvent(0x21, eEventInvisible);

    if (saveLoadPromptSel != 3)
    {
        i = 0x16;
        if (mode == 1)
        {
            i = 1;
        }
        zSaveLoad_UIEvent(i, eEventVisible);
    }
    return saveLoadPromptSel;
}

S32 zSaveLoad_CardPromptSpace(S32 mode)
{
    S32 i = 0x15;
    if (mode == 1)
    {
        i = 0;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);
    zSaveLoad_UIEvent(0x16, eEventInvisible);
    zSaveLoad_UIEvent(0x25, eEventUIFocusOn_Select);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
        zSaveLoad_poll(3);
    }
    zSaveLoad_UIEvent(0x25, eEventUIFocusOff_Unselect);

    i = 0x16;
    if (mode == 1)
    {
        i = 1;
    }
    zSaveLoad_UIEvent(i, eEventVisible);
    return saveLoadPromptSel;
}

S32 zSaveLoad_CardPromptGames(S32 mode)
{
    S32 i = 0x15;
    if (mode == 1)
    {
        i = 0;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);
    zSaveLoad_UIEvent(1, eEventInvisible);
    zSaveLoad_UIEvent(0x12, eEventVisible);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
        zSaveLoad_poll(3);
    }
    zSaveLoad_UIEvent(0x12, eEventInvisible);

    if (saveLoadPromptSel != 3)
    {
        i = 0x16;
        if (mode == 1)
        {
            i = 1;
        }
        zSaveLoad_UIEvent(i, eEventVisible);
    }
    return saveLoadPromptSel;
}

S32 zSaveLoad_CardPromptGameSlotEmpty()
{
    zSaveLoad_UIEvent(0, eEventInvisible);
    zSaveLoad_UIEvent(0x13, eEventVisible);
    zSaveLoad_UIEvent(0x13, eEventUIFocusOn_Select);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }
    zSaveLoad_UIEvent(0x13, eEventInvisible);
    zSaveLoad_UIEvent(0x13, eEventUIFocusOff_Unselect);
    return saveLoadPromptSel;
}

S32 zSaveLoad_CardPromptOverwrite()
{
    zSaveLoad_UIEvent(0x15, eEventInvisible);
    zSaveLoad_UIEvent(0x22, eEventVisible);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
        zSaveLoad_poll(5);
    }
    zSaveLoad_UIEvent(0x22, eEventInvisible);
    return saveLoadPromptSel;
}

S32 zSaveLoad_CardPromptOverwriteDamaged()
{
    zSaveLoad_UIEvent(0x15, eEventInvisible);
    zSaveLoad_UIEvent(0x23, eEventVisible);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
        zSaveLoad_poll(5);
    }
    zSaveLoad_UIEvent(0x23, eEventInvisible);
    return saveLoadPromptSel;
}

// No symbols in the target: these prompts are always inlined (into the save/load loops).
inline S32 zSaveLoad_ErrorPrompt(S32 cardNumber)
{
    S32 i = 0x2b;
    if (cardNumber == 1)
    {
        i = 0x2a;
    }
    zSaveLoad_UIEvent(i, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }

    i = 0x2b;
    if (cardNumber == 1)
    {
        i = 0x2a;
    }
    zSaveLoad_UIEvent(i, eEventUIFocusOff);
    return saveLoadPromptSel;
}

inline S32 zSaveLoad_SaveDamagedErrorPrompt(S32 cardNumber)
{
    zSaveLoad_UIEvent(0x35, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }
    zSaveLoad_UIEvent(0x35, eEventUIFocusOff);
    return saveLoadPromptSel;
}

inline S32 zSaveLoad_CardYankedErrorPrompt(S32 cardNumber)
{
    zSaveLoad_UIEvent(0x36, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }
    zSaveLoad_UIEvent(0x36, eEventUIFocusOff);
    return saveLoadPromptSel;
}

inline S32 zSaveLoad_DamagedSaveGameErrorPrompt(S32 cardNumber)
{
    zSaveLoad_UIEvent(0x3c, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }
    zSaveLoad_UIEvent(0x3c, eEventUIFocusOff);
    return saveLoadPromptSel;
}

inline S32 zSaveLoad_CardWrongDeviceErrorPrompt(S32 mode)
{
    S32 i = 0x3a;
    if (mode == 1)
    {
        i = 0x39;
    }
    zSaveLoad_UIEvent(i, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }

    i = 0x3a;
    if (mode == 1)
    {
        i = 0x39;
    }
    zSaveLoad_UIEvent(i, eEventUIFocusOff);
    return saveLoadPromptSel;
}

inline S32 zSaveLoad_CardDamagedErrorPrompt(S32 mode)
{
    S32 i = 0x38;
    if (mode == 1)
    {
        i = 0x37;
    }
    zSaveLoad_UIEvent(i, eEventUIFocusOn);

    saveLoadPromptSel = -1;
    while (saveLoadPromptSel == -1)
    {
        zSaveLoad_Tick(true);
    }

    i = 0x38;
    if (mode == 1)
    {
        i = 0x37;
    }
    zSaveLoad_UIEvent(i, eEventUIFocusOff);
    return saveLoadPromptSel;
}

inline S32 zSaveLoad_CardCheckSingle(S32 num)
{
    st_XSAVEGAME_DATA* ldinst = xSGInit(XSG_MODE_LOAD);
    S32 tgtcnt;
    S32 tgtmax;
    S32 tgtslot;
    S32 wrongDevice;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        xSGDone(ldinst);
        return 1;
    case 1:
        tgtslot = xSGTgtPhysSlotIdx(ldinst, 0);
        xSGDone(ldinst);
        wrongDevice = iSGCheckForWrongDevice();
        if (wrongDevice >= 0 && wrongDevice == num)
        {
            return 9;
        }
        if (tgtslot == num)
        {
            return 1;
        }
        return 0;
    case 0:
        wrongDevice = iSGCheckForWrongDevice();
        if (wrongDevice >= 0)
        {
            xSGDone(ldinst);
            return wrongDevice == num ? 9 : 0;
        }
        xSGDone(ldinst);
        return 0;
    }
    return -1;
}

// No GC symbol: the target inlines it into the prompt loops.
inline S32 zSaveLoad_poll(S32 i)
{
    if (dontPoll < 0.0f)
    {
        dontPoll = 1.0f;
        if (!zSaveLoad_CardCheckSingle(currentCard))
        {
            saveLoadPromptSel = i;
            currentGame = 0;
        }
        return 1;
    }
    return 1;
}

S32 zSaveLoad_CardCheckFormattedSingle(S32 num)
{
    st_XSAVEGAME_DATA* ldinst;
    S32 tgtcnt;
    S32 tgtmax;
    S32 rc;
    S32 tgtslot;

    ldinst = xSGInit(XSG_MODE_LOAD);
    rc = 0;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        rc = xSGTgtIsFormat(ldinst, num, NULL);
        switch (rc)
        {
        case -1:
            rc = 7;
            break;
        case 1:
            rc = 1;
            break;
        case 0:
        default:
            rc = 0;
            break;
        }
        break;
    case 1:
        tgtslot = xSGTgtPhysSlotIdx(ldinst, 0);
        if (tgtslot != num)
        {
            rc = -1;
        }
        else if (!(tgtslot ^ num))
        {
            rc = xSGTgtIsFormat(ldinst, 0, NULL);
            switch (rc)
            {
            case -1:
                rc = 7;
                break;
            case 1:
                rc = 1;
                break;
            case 0:
            default:
                rc = 0;
                break;
            }
        }
        break;
    case 0:
        rc = -1;
        break;
    }

    xSGDone(ldinst);
    return rc;
}

// Compiled-out asserts: the three xASSERTs are Ratatouille's, the other conditions are unknown.
// With them the function is too big to be auto-inlined; retail calls it.
S32 zSaveLoad_CardCheck(S32 cardNumber, S32 mode)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xASSERT(cardNumber != -1);
    S32 cardResult = zSaveLoad_CardCheckSingle(cardNumber);
    while (cardResult == 0)
    {
        badCard = 1;

        cardResult = zSaveLoad_CardPrompt(mode);
        if (cardResult == 2 || cardResult == 4)
        {
            return 2;
        }
        xASSERT(cardNumber != -1);
        cardResult = zSaveLoad_CardCheckSingle(cardNumber);
        xASSERT(cardNumber != -1);
    }
    return cardResult == 9 ? 9 : 1;
}

inline S32 zSaveLoad_CardCheckSpaceSingle_doCheck(st_XSAVEGAME_DATA* xsgdata, S32 num)
{
    S32 rc;

    if (xSGTgtIsFormat(xsgdata, num, NULL) <= 0)
    {
        return 6;
    }
    xSGTgtSelect(xsgdata, num);
    // Both branches make the same request.
    if (xSGTgtHasGameDir(xsgdata, num) == 1)
    {
        rc = xSGTgtHaveRoom(xsgdata, num, 0xcc00, -1, &sNeeded, &sAvailable, NULL);
    }
    else
    {
        rc = xSGTgtHaveRoom(xsgdata, num, 0xcc00, -1, &sNeeded, &sAvailable, NULL);
    }
    if (rc == 0)
    {
        rc = 0;
    }
    return rc;
}

inline S32 zSaveLoad_CardCheckSpaceSingle(S32 num)
{
    st_XSAVEGAME_DATA* ldinst;
    S32 tgtcnt;
    S32 tgtmax;
    S32 rc;
    S32 tgtslot;

    ldinst = xSGInit(XSG_MODE_SAVE);
    rc = 0;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        rc = zSaveLoad_CardCheckSpaceSingle_doCheck(ldinst, num);
        if (rc != 0)
        {
            rc = 1;
        }
        break;
    case 1:
        tgtslot = xSGTgtPhysSlotIdx(ldinst, 0);
        if (tgtslot != num)
        {
            rc = 5;
        }
        if (!(tgtslot ^ num))
        {
            rc = zSaveLoad_CardCheckSpaceSingle_doCheck(ldinst, 0);
        }
        break;
    case 0:
        rc = 5;
        break;
    }

    xSGDone(ldinst);
    return rc;
}

inline S32 zSaveLoad_CardCheckGamesSingle_doCheck(st_XSAVEGAME_DATA* xsgdata, S32 num)
{
    S32 rc;

    if (xSGTgtIsFormat(xsgdata, num, NULL) <= 0)
    {
        return 6;
    }
    xSGTgtSelect(xsgdata, num);
    if (!xSGTgtHasGameDir(xsgdata, num))
    {
        return 0;
    }
    rc = xSGTgtHaveRoom(xsgdata, num, 0xcc00, -1, &sNeeded, &sAvailable, NULL);
    if (rc == 0)
    {
        rc = 0;
    }
    return rc;
}

inline S32 zSaveLoad_CardCheckGamesSingle(S32 num)
{
    st_XSAVEGAME_DATA* ldinst;
    S32 tgtcnt;
    S32 tgtmax;
    S32 rc;
    S32 tgtslot;

    ldinst = xSGInit(XSG_MODE_LOAD);
    rc = 0;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        rc = zSaveLoad_CardCheckGamesSingle_doCheck(ldinst, num);
        if (rc != 0)
        {
            rc = 1;
        }
        break;
    case 1:
        tgtslot = xSGTgtPhysSlotIdx(ldinst, 0);
        if (tgtslot != num)
        {
            rc = 5;
        }
        if (!(tgtslot ^ num))
        {
            rc = zSaveLoad_CardCheckGamesSingle_doCheck(ldinst, 0);
        }
        break;
    case 0:
        rc = 5;
        break;
    }

    xSGDone(ldinst);
    return rc;
}

inline S32 zSaveLoad_CardCheckSlotEmpty_hasGame_doCheck(st_XSAVEGAME_DATA* xsgdata, S32 num,
                                                        S32 game)
{
    S32 rc;

    if (xSGTgtIsFormat(xsgdata, num, NULL) <= 0)
    {
        return -1;
    }
    xSGTgtSelect(xsgdata, num);
    rc = xSGGameIsEmpty(xsgdata, game);
    if (rc != 0)
    {
        rc = 0;
    }
    else if (rc == 0)
    {
        rc = 1;
    }
    return rc;
}

inline S32 zSaveLoad_CardCheckSlotEmpty_hasGame(S32 num, S32 game)
{
    st_XSAVEGAME_DATA* ldinst;
    S32 tgtcnt;
    S32 tgtmax;
    S32 rc;
    S32 tgtslot;

    ldinst = xSGInit(XSG_MODE_LOAD);
    rc = 0;

    tgtcnt = xSGTgtCount(ldinst, &tgtmax);
    switch (tgtcnt)
    {
    case 2:
        rc = zSaveLoad_CardCheckSlotEmpty_hasGame_doCheck(ldinst, num, game);
        if (rc != 10 && rc != 0)
        {
            rc = 1;
        }
        break;
    case 1:
        tgtslot = xSGTgtPhysSlotIdx(ldinst, 0);
        if (tgtslot != num)
        {
            rc = -1;
        }
        if (!(tgtslot ^ num))
        {
            rc = zSaveLoad_CardCheckSlotEmpty_hasGame_doCheck(ldinst, 0, game);
        }
        break;
    case 0:
        rc = -1;
        break;
    }
    xSGDone(ldinst);
    return rc;
}

inline S32 zSaveLoad_CardCheckSlotOverwrite_Free(S32 cardNumber, S32 gameNumber)
{
    S32 i = zSaveLoad_CardCheckSlotEmpty_hasGame(cardNumber, gameNumber);
    switch (i)
    {
    case -1:
        return i;
    case 0:
        return 1;
    case 1:
        return 0;
    case 10:
        return 10;
    default:
        return 0;
    }
}

inline bool IsValidName(char* name)
{
    if (strcmp(name, "") == 0)
    {
        return false;
    }

    for (char* p = name; *p != '\0'; p++)
    {
        if ((*p < 'A' || *p > 'z') && (*p < '0' || *p > '9') && *p != ' ' && *p != '!' &&
            *p != '.' && *p != '\'' && *p != ',' && *p != '-' && *p != '&')
        {
            return false;
        }
    }
    return true;
}

S32 zSaveLoad_CardCheckFormatted(S32 cardNumber, S32 mode)
{
    S32 result;
    while (result = zSaveLoad_CardCheckFormattedSingle(cardNumber), result != 1)
    {
        badCard = 1;

        result = zSaveLoad_CardCheckFormattedSingle(cardNumber);
        if (result == -1)
        {
            return -1;
        }

        if (result == 7)
        {
            return 7;
        }

        result = zSaveLoad_CardPromptFormat(mode);
        if (result == 2 || result == 4)
        {
            return result;
        }

        if (result == 5)
        {
            return 5;
        }

        sAccessType = 3;
        // not in the DWARF: the format result is tested and returned.
        S32 tmp = format(cardNumber, mode);
        sAccessType = 2;
        return tmp != 1 ? tmp : 11;
    }
    return 1;
}

S32 zSaveLoad_CardCheckValid(S32 cardNumber, S32 mode)
{
    if (mode == 1)
    {
        return zSaveLoad_CardCheckGames(cardNumber, mode);
    }
    else
    {
        return zSaveLoad_CardCheckSpace(cardNumber, mode);
    }
}

S32 zSaveLoad_CardCheckSpace(S32 cardNumber, S32 mode)
{
    S32 result = zSaveLoad_CardCheckSpaceSingle(cardNumber);
    while (result != 1)
    {
        badCard = 1;

        result = zSaveLoad_CardCheckSpaceSingle(cardNumber);
        if (result == -1 || result == 6 || result == 5)
        {
            return result;
        }

        result = zSaveLoad_CardPromptSpace(mode);
        if (result == 2 || result == 4)
        {
            return result;
        }
    }
    return 1;
}

S32 zSaveLoad_CardCheckGames(S32 cardNumber, S32 mode)
{
    S32 result = zSaveLoad_CardCheckGamesSingle(cardNumber);
    while (result != 1)
    {
        badCard = 1;

        result = zSaveLoad_CardCheckGamesSingle(cardNumber);
        if (result == -1 || result == 6 || result == 5)
        {
            return result;
        }

        result = zSaveLoad_CardPromptGames(mode);
        if (result == 2 || result == 4)
        {
            return result;
        }
    }
    return 1;
}

S32 zSaveLoad_CardCheckGameSlot(S32 cardNumber, S32 gameNumber, S32 mode)
{
    if (mode == 1)
    {
        return zSaveLoad_CardCheckSlotEmpty(cardNumber, gameNumber);
    }
    else
    {
        return zSaveLoad_CardCheckSlotOverwrite(cardNumber, gameNumber);
    }
}

S32 zSaveLoad_CardCheckSlotEmpty(S32 cardNumber, S32 gameNumber)
{
    S32 i = zSaveLoad_CardCheckSlotEmpty_hasGame(cardNumber, gameNumber);
    while (i != 1)
    {
        i = zSaveLoad_CardCheckSlotEmpty_hasGame(cardNumber, gameNumber);
        if (i == -1 || i == 6)
        {
            return i;
        }

        i = zSaveLoad_CardPromptGameSlotEmpty();
        if (i == 2 || i == 4)
        {
            return i;
        }
    }
    return 1;
}

S32 zSaveLoad_CardCheckSlotOverwrite(S32 cardNumber, S32 gameNumber)
{
    S32 result = zSaveLoad_CardCheckSlotOverwrite_Free(cardNumber, gameNumber);
    // NOTE: a loop that always breaks (as in BfBB) is an odd shape, but it is the only known
    // form that gives retail's early branch.
    while (result != 1 || result == 10)
    {
        if (result == -1 || result == 10)
        {
            return result;
        }

        if (IsValidName(zSaveLoadGameTable[gameNumber].label))
        {
            result = zSaveLoad_CardPromptOverwrite();
        }
        else
        {
            result = zSaveLoad_CardPromptOverwriteDamaged();
        }

        if (result == 5)
        {
            return result;
        }

        if (result == 2 || result == 4)
        {
            return result;
        }
        break;
    }

    return 1;
}

S32 zSaveLoad_CardPick(S32 mode)
{
    S32 done = 0;
    U8 formatDone = 0; // set but never read (as in the PS2 build)

    currentCard = -1;
    saveLoadPromptSel = -1;

#ifdef PLATFORM_PC
    // PLATFORM_PC: loading, with saves in only one of the two folders that
    // stand in for the memory card slots, goes straight to them, as the Xbox
    // release (one hard disk) does. Asking let a player pick the empty folder
    // and read "NO GAME SAVES PRESENT ON THE XBOX HARD DISK" with every save
    // in the other. Saving still asks where.
    sCardPickSkipped = 0;
    if (mode == 1)
    {
        S32 only = iSGOnlyTargetWithGames();
        if (only >= 0)
        {
            currentCard = only;
            sCardPickSkipped = 1;
            return 1;
        }
    }
#endif

    zSaveLoad_UIEvent(mode == 1 ? 1 : 0x16, eEventVisible);
    zSaveLoad_UIEvent(mode == 1 ? 4 : 0x17, eEventUIFocusOn_Select);

    while (!done && saveLoadPromptSel == -1)
    {
        if (currentCard != -1)
        {
            zSaveLoad_UIEvent(mode == 1 ? 1 : 0x16, eEventInvisible);

            done = zSaveLoad_CardCheck(currentCard, mode);
            switch (done)
            {
            case 2:
                currentCard = -1;
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case -1:
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case 9:
                zSaveLoad_CardWrongDeviceErrorPrompt(mode);
                done = -1;
                saveLoadPromptSel = -1;
                continue;
            }

            done = zSaveLoad_CardCheckFormatted(currentCard, mode);
            switch (done)
            {
            case 5:
                done = 0;
                if (saveLoadPromptSel == 3)
                {
                    saveLoadPromptSel = -1;
                    continue;
                }
                if (saveLoadPromptSel == 4)
                {
                    currentCard = -1;
                    return -1;
                }
                break;
            case 6:
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case 7:
                zSaveLoad_CardDamagedErrorPrompt(mode);
                done = -1;
                saveLoadPromptSel = -1;
                continue;
            case 2:
            case 4:
                currentCard = -1;
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case -1:
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case 11:
                formatDone = 1;
                break;
            }

            done = zSaveLoad_CardCheckValid(currentCard, mode);
            switch (done)
            {
            case 5:
            case 6:
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case 2:
            case 4:
                done = 0;
                saveLoadPromptSel = -1;
                currentGame = -1;
                currentCard = -1;
                break;
            case -1:
                done = 0;
                saveLoadPromptSel = -1;
                continue;
            case 3:
                break;
            }
        }

        zSaveLoad_Tick(true);
    }

    zSaveLoad_UIEvent(mode == 1 ? 1 : 0x16, eEventInvisible);

    if (done == 1)
    {
        return done;
    }
    if (saveLoadPromptSel != -1)
    {
        done = saveLoadPromptSel;
    }
    return done;
}

void BuildIt(char* build_txt, S32 i)
{
    char date1[32] = "";
    char date2[32];
    char biggerbuf[256] = "";
    char* damaged_save_name;
    char* unit_txt;
    char* units_txt;
    char* displaySizeUnit;

    damaged_save_name = xTextFindString(xStrHash("SV DAMAGED SAVE NAME TXT"), NULL);
    unit_txt = xTextFindString(xStrHash("DISK STORAGE UNITS SINGLE TXT"), NULL);
    units_txt = xTextFindString(xStrHash("DISK STORAGE UNITS TXT"), NULL);

    if (!IsValidName(zSaveLoadGameTable[i].label))
    {
        strcpy(build_txt, damaged_save_name);
    }
    else
    {
        strncpy(date1, zSaveLoadGameTable[i].date, 5);
        date1[2] = '/';
        sprintf(biggerbuf, "%s/%c%c", date1, zSaveLoadGameTable[i].date[8],
                zSaveLoadGameTable[i].date[9]);
        strncpy(date2, biggerbuf, sizeof(date2));
        date2[sizeof(date2) - 1] = '\0';

        if (zSaveLoadGameTable[i].size == 1)
        {
            displaySizeUnit = unit_txt;
        }
        else
        {
            displaySizeUnit = units_txt;
        }

        sprintf(biggerbuf, "%d%%  %s    (%d %s)\n%s", zSaveLoadGameTable[i].progress, date2,
                zSaveLoadGameTable[i].size, displaySizeUnit, zSaveLoadGameTable[i].label);
        strncpy(build_txt, biggerbuf, 128);
        // Out-of-bounds write kept from the original: every build stores to date2 + 63 here (BfBB
        // reads it as date1[31], the same address when date1 sits right above date2). The value
        // is never read.
        // PLATFORM_PC: dropped. Nothing reads the byte, and on a host it lands 32 bytes past
        // date2, wherever the compiler put the frame.
#ifndef PLATFORM_PC
        date2[63] = '\0';
#endif
    }
}

void zSaveLoad_BuildName(char* name_txt, size_t max_len, S32 idx)
{
    char desired[128];
    char current_name[128];
    char* damaged_save_name;
    S32 counter;
    S32 i;

    BuildIt(desired, idx);
    damaged_save_name = xTextFindString(xStrHash("SV DAMAGED SAVE NAME TXT"), NULL);

    counter = 0;
    for (i = 0; i < idx; i++)
    {
        BuildIt(current_name, i);
        if (strcmp(zSaveLoadGameTable[i].label, zSaveLoadGameTable[idx].label) == 0 &&
            strcmp(current_name, damaged_save_name) != 0)
        {
            counter++;
        }
    }

    if (counter > 0)
    {
        sprintf(name_txt, "%s (%d)", desired, counter);
        name_txt[max_len] = '\0';
    }
    else
    {
        strcpy(name_txt, desired);
    }
}

S32 zSaveLoad_GameSelect(S32 mode)
{
    S32 done = 0;
    st_XSAVEGAME_DATA* svinst;
    S32 i;
    S32 use_tgt;
    char* empty_string;
    char* damaged_save_name;

    badCard = 1;
    while (badCard != 0)
    {
        currentGame = -1;
        saveLoadPromptSel = -1;
        use_tgt = CardtoTgt(currentCard);

        if (mode == 1)
        {
            zSaveLoad_UIEvent(0, eEventEnable);
            zSaveLoad_UIEvent(0, eEventUIFocusOn);
        }
        else
        {
            zSaveLoad_UIEvent(0x34, eEventVisible);
        }

        for (i = 0; i < 3; i++)
        {
            svinst = xSGInit(XSG_MODE_LOAD);
            xSGTgtSelect(svinst, use_tgt);
            if (xSGGameIsEmpty(svinst, i))
            {
                empty_string = xTextFindString(xStrHash("SV EMPTY TXT"), NULL);
                strcpy(zSaveLoadGameTable[i].label, empty_string);
                strcpy(zSaveLoadGameTable[i].date, empty_string);
                zSaveLoadGameTable[i].progress = 0;
                zSaveLoadGameTable[i].size = 0;
                zSaveLoadGameTable[i].thumbIconIndex = -1;
            }
            else if (iSGIsGameCorrupt(svinst->isgsess, i))
            {
                damaged_save_name = xTextFindString(xStrHash("SV DAMAGED SAVE NAME TXT"), NULL);
                strcpy(zSaveLoadGameTable[i].label, damaged_save_name);
                strcpy(zSaveLoadGameTable[i].date, damaged_save_name);
                zSaveLoadGameTable[i].progress = 0;
                zSaveLoadGameTable[i].size = 0;
                zSaveLoadGameTable[i].thumbIconIndex = -1;
            }
            else
            {
                strcpy(zSaveLoadGameTable[i].label, xSGGameLabel(svinst, i));
                if (strcmpi(zSaveLoadGameTable[i].label, zSceneGetLevelName(SCENE_ID_MNU_START)) ==
                    0)
                {
                    strcpy(zSaveLoadGameTable[i].label,
                           zSceneGetLevelName(
                               (globals.sceneStart[0] << 24) | (globals.sceneStart[1] << 16) |
                               (globals.sceneStart[2] << 8) | globals.sceneStart[3]));
                }
                strcpy(zSaveLoadGameTable[i].date, xSGGameModDate(svinst, i));
                zSaveLoadGameTable[i].progress = xSGGameProgress(svinst, i);
                zSaveLoadGameTable[i].size = xSGGameSize(svinst, i);
                zSaveLoadGameTable[i].size = zSaveLoadGameTable[i].size + 0x1fff & 0xffffe000;
                zSaveLoadGameTable[i].size >>= 0xd;
                zSaveLoadGameTable[i].size += 2;
                zSaveLoadGameTable[i].thumbIconIndex = xSGGameThumbIndex(svinst, i);
                if (strcmpi(zSaveLoadGameTable[i].label, "") == 0)
                {
                    zSaveLoadGameTable[i].thumbIconIndex = -1;
                }
            }
            xSGDone(svinst);
        }

        i = 0x15;
        if (mode == 1)
        {
            i = 0;
        }
        zSaveLoad_UIEvent(i, eEventEnable);

        i = 0x15;
        if (mode == 1)
        {
            i = 0;
        }
        zSaveLoad_UIEvent(i, eEventVisible);
        zSaveLoad_UIEvent(0x19, eEventUIFocusOn_Select);
        badCard = 0;

        while (done == 0 && saveLoadPromptSel == -1)
        {
            zSaveLoad_Tick(true);
            zSaveLoad_poll(3);
            if (saveLoadPromptSel == 4)
            {
                done = saveLoadPromptSel;
            }

            if (currentGame == -1)
            {
                continue;
            }

            done = zSaveLoad_CardCheck(currentCard, mode);
            if (done != 1)
            {
                currentGame = -1;
                badCard = 0;

                if (done == 9)
                {
                    zSaveLoad_CardWrongDeviceErrorPrompt(mode);
                    done = 9;
                }
                break;
            }

            done = zSaveLoad_CardCheckFormatted(currentCard, mode);
            if (done != 1)
            {
                switch (done)
                {
                case 5:
                    done = 0;
                    saveLoadPromptSel = -1;
                    continue;
                case 7:
                    zSaveLoad_CardDamagedErrorPrompt(mode);
                    saveLoadPromptSel = -1;
                    done = 7;
                    badCard = 0;
                    continue;
                case 2:
                    currentGame = -1;
                    badCard = 0;
                    continue;
                default:
                    continue;
                }
            }

            done = zSaveLoad_CardCheckValid(currentCard, mode);
            if (done != 1)
            {
                switch (done)
                {
                case 5:
                case 6:
                    done = 0;
                    saveLoadPromptSel = -1;
                    continue;
                case 2:
                    saveLoadPromptSel = -1;
                    currentGame = -1;
                    badCard = 0;
                    continue;
                default:
                    break;
                }
            }

            if (badCard == 0)
            {
                done = zSaveLoad_CardCheckGameSlot(currentCard, currentGame, mode);
                if (done != 1)
                {
                    switch (done)
                    {
                    case -1:
                    case 5:
                        done = 0;
                        saveLoadPromptSel = -1;
                        break;
                    case 2:
                        currentGame = -1;
                        break;
                    case 4:
                        saveLoadPromptSel = -1;
                        break;
                    }
                }
            }
            else
            {
                done = 0;
                currentGame = -1;
            }
        }
    }

    if (mode == 1)
    {
        zSaveLoad_UIEvent(0, eEventEnable);
    }

    i = 0x15;
    if (mode == 1)
    {
        i = 0;
    }
    zSaveLoad_UIEvent(i, eEventInvisible);
    zSaveLoad_UIEvent(0x34, eEventInvisible);
    return done;
}

void zSaveLoadPreAutoSave(bool onOff)
{
    preAutoSaving = onOff;
    if (onOff)
    {
        ResetButton::DisableReset();
    }
    else
    {
        ResetButton::EnableReset();
    }
}

void zSaveLoadAutoSaveUpdate()
{
    xBase* sendTo;
    s32 memSize, sectorSize;

    if (globals.autoSaveFeature == 0 || gGameMode == eGameMode_Pause)
    {
        return;
    }
    if (preAutoSaving == 0)
    {
        return;
    }
#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 5.7): a joined guest does not
    // autosave, nor does it in a level it followed the host to until it
    // leaves it, so levels it only visited with the host stay out of its
    // save. FALSE at once when co-op is off.
    if (iCoopNoAutoSave())
    {
        preAutoSaving = 0;
        return;
    }
#endif

    S32 physicalSlot = xSGAutoSave_GetCache()->LastPhysicalSlot();
    if (physicalSlot >= 0)
    {
        autoSaveCard = physicalSlot;
        switch (CARDProbeEx(physicalSlot, &memSize, &sectorSize))
        {
        case 0:
        case -1:
            sendTo = zSceneFindObject(xStrHash("AUTO_SAVE"));
            if (sendTo != NULL)
            {
                zEntEvent(sendTo, eEventVisible);
            }
            break;
        default:
            sendTo = zSceneFindObject(xStrHash("AUTO_SAVE"));
            if (sendTo != NULL)
            {
                zEntEvent(sendTo, eEventInvisible);
            }

            sendTo = zSceneFindObject(xStrHash("AUTO_SAVE_FAILED"));
            if (sendTo != NULL)
            {
                zEntEvent(sendTo, eEventVisible);
            }
            globals.autoSaveFeature = 0;
            zSaveLoadPreAutoSave(0);
            zGameStall();
            break;
        }
    }
}

S32 zSaveLoad_DoAutoSave()
{
    S32 success = 0;
    S32 teststat = 1;
    S32 rc;
    en_XSGASYNC_STATUS asstat = XSG_ASTAT_NOOP;
    st_XSAVEGAME_DATA* svinst;

    XSGAutoData* autodata = xSGAutoSave_GetCache();
    if (autodata == NULL)
    {
        return -1;
    }
    if (!autodata->IsValid())
    {
        return -1;
    }

    S32 use_tgt = CardtoTgt(autodata->LastTarget());
    S32 lastGame = autodata->LastGame();
    autodata->Discard();
    svinst = xSGInit(XSG_MODE_SAVE);
    xSGTgtSelect(svinst, use_tgt);
    xSGGameSet(svinst, lastGame);

    if (svinst == NULL)
    {
        teststat = 0;
    }

    zSceneSave(globals.sceneCur);

    xSGAddSaveClient(svinst, 'ROOM', 0, xSGT_SaveInfoCB, xSGT_SaveProcCB);
    xSGAddSaveClient(svinst, 'PREF', 0, xSGT_SaveInfoPrefsCB, xSGT_SaveProcPrefsCB);

    xSerial_svgame_register(svinst, XSG_MODE_SAVE);
    U32 progress = zSceneCalcProgress();

    char label[64];
    strncpy(label, zSceneGetLevelName(globals.sceneCur->sceneID), sizeof(label));
    if (!xSGSetup(svinst, lastGame, label, progress, 0, zSceneGetLevelIndex()))
    {
        teststat = 0;
    }

    if (teststat != 0)
    {
        rc = xSGProcess(svinst);
        if (rc != 0)
        {
            asstat = xSGAsyncStatus(svinst, 1, 0, 0);
        }
        xSGGameIsEmpty(svinst, lastGame);
        xSGTgtHasGameDir(svinst, use_tgt);
        if (rc == 0)
        {
            teststat = false;
        }
        else
        {
            switch (asstat)
            {
            case XSG_ASTAT_INPROG:
                break;
            case XSG_ASTAT_SUCCESS:
                success = true;
                break;
            case XSG_ASTAT_FAILED:
                success = false;
                break;
            }
        }
    }

    if (teststat && xSGWrapup(svinst) == 0)
    {
        teststat = false;
    }

    if (xSGDone(svinst) == 0)
    {
        teststat = false;
    }

    if ((success) && (teststat))
    {
        if (autodata != NULL)
        {
            S32 physicalSlot = xSGTgtPhysSlotIdx(svinst, use_tgt);
            autodata->SetCache(use_tgt, lastGame, physicalSlot);
            globals.autoSaveFeature = 1;
        }
        return 1;
    }
    else
    {
        if (autodata != NULL)
        {
            autodata->Discard();
        }
        globals.autoSaveFeature = 0;
        return -1;
    }
}

// Reordering at beginning
S32 zSaveLoad_SaveGame()
{
    S32 success = false;
    S32 teststat = true;
    S32 rc;
    en_XSGASYNC_STATUS asstat = XSG_ASTAT_NOOP;
    S32 use_tgt = CardtoTgt(currentCard);
    S32 use_game = currentGame;
    autoSaveCard = currentCard;
#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 7.6): only the host's game saves while
    // a guest is in the session; a guest's save by hand is not written.
    // zSaveLoad_SaveLoop already closes a guest's save screen at once; this
    // catches a session joined while the screen was open (the game's own
    // failure prompt, and a co-op notice saying why). FALSE at once when
    // co-op is off.
    if (iCoopNoSave())
    {
        return 9;
    }
#endif

    st_XSAVEGAME_DATA* xsgdata = zSaveLoadSGInit(XSG_MODE_SAVE);
    if (xSGCheckMemoryCard(xsgdata, currentCard) == 0)
    {
        zSaveLoadSGDone(xsgdata);
        return 8;
    }

    xSGTgtSelect(xsgdata, use_tgt);
    xSGGameSet(xsgdata, use_game);
    if (xsgdata == NULL)
    {
        teststat = false;
    }

    zSceneSave(globals.sceneCur);

    xSGAddSaveClient(xsgdata, 'ROOM', NULL, xSGT_SaveInfoCB, xSGT_SaveProcCB);
    xSGAddSaveClient(xsgdata, 'PREF', NULL, xSGT_SaveInfoPrefsCB, xSGT_SaveProcPrefsCB);

    xSerial_svgame_register(xsgdata, XSG_MODE_SAVE);

    U32 progress = zSceneCalcProgress();
    char label[64];

    strncpy(label, zSceneGetLevelName(globals.sceneCur->sceneID), sizeof(label));

    if (!xSGSetup(xsgdata, use_game, label, progress, 0, zSceneGetLevelIndex()))
    {
        teststat = false;
    }

    en_XSG_WHYFAIL whyFail = XSG_WHYERR_NONE;
    if (teststat)
    {
        rc = xSGProcess(xsgdata);
        if (rc)
        {
            asstat = xSGAsyncStatus(xsgdata, 1, &whyFail, NULL);
        }

        xSGGameIsEmpty(xsgdata, use_game);
        xSGTgtHasGameDir(xsgdata, use_tgt);
        if (!rc)
        {
            teststat = false;
        }
        else
        {
            switch (asstat)
            {
            case XSG_ASTAT_SUCCESS:
                success = true;
                break;
            case XSG_ASTAT_FAILED:
                success = false;
                break;
            case XSG_ASTAT_NOOP:
            case XSG_ASTAT_INPROG:
                break;
            }
        }
    }

    if (teststat && !xSGWrapup(xsgdata))
    {
        teststat = false;
    }

    if (!xSGCheckMemoryCard(xsgdata, currentCard))
    {
        zSaveLoadSGDone(xsgdata);
        return 8;
    }

    if (!zSaveLoadSGDone(xsgdata))
    {
        teststat = false;
    }

    XSGAutoData* asg = xSGAutoSave_GetCache();
    if (success && teststat)
    {
        S32 physicalSlot = xSGTgtPhysSlotIdx(xsgdata, use_tgt);
        asg->SetCache(use_tgt, use_game, physicalSlot);
        globals.autoSaveFeature = 1;
        return 1;
    }
    asg->Discard();

    switch (whyFail)
    {
    case XSG_WHYERR_DAMAGE:
    case XSG_WHYERR_OTHER:
        return 7;
    case XSG_WHYERR_NOCARD:
    case XSG_WHYERR_CARDYANKED:
        return 8;
    default:
        return -1;
    }
}

S32 zSaveLoad_LoadGame()
{
    S32 success = false;
    S32 teststat = true;
    S32 rc;
    en_XSGASYNC_STATUS asstat = XSG_ASTAT_NOOP;
    S32 use_tgt = CardtoTgt(currentCard);
    autoSaveCard = currentCard;

    st_XSAVEGAME_DATA* xsgdata = zSaveLoadSGInit(XSG_MODE_LOAD);
    xSGTgtSelect(xsgdata, use_tgt);
    xSGGameSet(xsgdata, currentGame);

    if (xsgdata == NULL)
    {
        teststat = false;
    }

    xSGAddLoadClient(xsgdata, 'ROOM', NULL, xSGT_LoadLoadCB);
    xSGAddLoadClient(xsgdata, 'PREF', NULL, xSGT_LoadPrefsCB);
    xSerial_svgame_register(xsgdata, XSG_MODE_LOAD);

    if (!xSGSetup(xsgdata))
    {
        teststat = false;
    }

    en_XSG_WHYFAIL whyFail = XSG_WHYERR_NONE;
    if (teststat)
    {
        rc = xSGProcess(xsgdata);
        if (rc)
        {
            asstat = xSGAsyncStatus(xsgdata, 1, &whyFail, NULL);
        }
        if (!rc)
        {
            teststat = false;
        }
        else
        {
            switch (asstat)
            {
            case XSG_ASTAT_SUCCESS:
                success = true;
                break;
            case XSG_ASTAT_FAILED:
                success = false;
                whyFail = XSG_WHYERR_NOCARD;
                teststat = false;
                break;
            case XSG_ASTAT_NOOP:
            case XSG_ASTAT_INPROG:
                break;
            }
        }
    }

    if (teststat)
    {
        rc = xSGWrapup(xsgdata);
        if (rc < 0)
        {
            teststat = false;
            whyFail = XSG_WHYERR_OTHER;
        }
        else if (rc == 0)
        {
            teststat = false;
        }
    }

    if (!zSaveLoadSGDone(xsgdata))
    {
        teststat = false;
    }

    XSGAutoData* asg = xSGAutoSave_GetCache();
    S32 use_game;
    if (success && teststat)
    {
        use_game = currentGame;
        S32 physicalSlot = xSGTgtPhysSlotIdx(xsgdata, use_tgt);
        asg->SetCache(use_tgt, use_game, physicalSlot);
        globals.autoSaveFeature = 1;
        return 1;
    }
    asg->Discard();

    switch (whyFail)
    {
    case XSG_WHYERR_OTHER:
        return 7;
    case XSG_WHYERR_NOCARD:
        return 5;
    default:
        return -1;
    }
}

U32 zSaveLoad_LoadLoop()
{
    zSaveLoadInit();
    sAccessType = 1;
    sIsLoading = 1;
    loadState = 0;

    while (loadState != 6)
    {
        switch (loadState)
        {
        case 0:
        {
            S32 result;
            U8 hasCard = 1;
            while (zSaveLoad_CardCount() == 0)
            {
                S32 tmp = iSGCheckForWrongDevice();
                if (tmp >= 0)
                {
                    currentCard = tmp;
                    result = zSaveLoad_CardWrongDeviceErrorPrompt(1);
                    currentCard = -1;
                }
                else
                {
                    result = zSaveLoad_CardPrompt(1);
                }

                if (result == 2 || result == 4)
                {
                    loadState = 8;
                    hasCard = 0;
                    break;
                }
            }

            zSaveLoad_UIEvent(0x14, eEventInvisible);
            if (hasCard)
            {
                loadState = 1;
            }
            break;
        }
        case 1:
            switch (zSaveLoad_CardPick(1))
            {
            case 1:
                loadState = 2;
                break;
            case 4:
                loadState = 8;
                break;
            case -1:
                loadState = 0;
                break;
            case 0:
            case 2:
                break;
            }
            break;
        case 2:
            switch (zSaveLoad_GameSelect(1))
            {
            case 2:
            case 4:
#ifdef PLATFORM_PC
                // PLATFORM_PC: back (or cancel) from saves reached without a
                // card choice leaves the load screen; there is no choice to
                // go back to.
                if (sCardPickSkipped)
                {
                    loadState = 8;
                    break;
                }
#endif
            case 5:
            case 6:
            case 7:
            case 9:
                loadState = 1;
                break;
            case 1:
                loadState = 9;
                break;
            case -1:
                zSaveLoad_ErrorPrompt(1);
                loadState = 1;
                break;
            }
            break;
        case 5:
            break;
        case 8:
            zGameModeSwitch(eGameMode_Title);
            zGameStateSwitch(0);
            zEntEvent(zSceneFindObject(xStrHash("_LOAD GAME MGR")), eEventInvisible);
            zEntEvent(zSceneFindObject(xStrHash("LD LOAD GAME UIF")), eEventInvisible);
            loadState = 6;
            break;
        case 9:
            switch (zSaveLoad_LoadGame())
            {
            case 1:
                zGameModeSwitch(eGameMode_Game);
                zGameStateSwitch(0);
                loadState = 6;
                globals.autoSaveFeature = 1;
                break;
            case 7:
                zSaveLoad_DamagedSaveGameErrorPrompt(1);
                loadState = 1;
                break;
            case 5:
                zSaveLoad_UIEvent(0x3d, eEventUIFocusOn);
                saveLoadPromptSel = -1;
                while (saveLoadPromptSel == -1)
                {
                    zSaveLoad_Tick(true);
                }
                zSaveLoad_UIEvent(0x3d, eEventUIFocusOff);
                loadState = 1;
                break;
            default:
                zSaveLoad_ErrorPrompt(1);
                loadState = 1;
                break;
            }
            break;
        }
    }

    return (U32)sceneRead[0] << 24 | (U32)sceneRead[1] << 16 | (U32)sceneRead[2] << 8 |
           (U32)sceneRead[3];
}

U32 zSaveLoad_SaveLoop()
{
    zSaveLoadInit();
    sIsLoading = 0;
    saveSuccess = 0;
    saveState = 0;
#ifdef PLATFORM_PC
    if (sPcTestSaveGame >= 0)
    {
        currentCard = 0;
        currentGame = sPcTestSaveGame;
        sPcTestSaveGame = -1;
        saveState = 10;
    }
    // Online co-op (docs/MULTIPLAYER.md 7.6): a co-op guest's game does not
    // save. Its save screen closes at once, back to play, as a cancel does
    // (state 11), and a co-op notice says why; the game's own failure prompt
    // would blame the memory card ("not enough free blocks"). FALSE at once
    // when co-op is off.
    if (iCoopNoSave())
    {
        saveState = 11;
    }
#endif

    while (saveState != 6)
    {
        switch (saveState)
        {
        case 0:
            saveState = 1;
            break;
        case 1:
            switch (zSaveLoad_CardPick(0))
            {
            case 1:
                saveState = 2;
                break;
            case 4:
                if (globals.sceneCur->sceneID != SCENE_ID_MNU_START)
                {
                    saveState = 6;
                    gGameMode = eGameMode_Pause;
                    zEntEvent(zSceneFindObject(xStrHash("_RETURN_FROM SAVE MGR")), eEventInvisible);
                }
                else
                {
                    saveState = 6;
                    gGameMode = eGameMode_Title;
                    zEntEvent(zSceneFindObject(xStrHash("CREATE NEW GAME UIF")),
                              eEventUIFocusOn_Select);
                }
                break;
            case 11:
                saveState = 10;
                currentGame = 0;
                break;
            case 2:
                saveState = 11;
                break;
            default:
                saveState = 0;
                break;
            }
            break;
        case 2:
            sAccessType = 2;
            switch (zSaveLoad_GameSelect(0))
            {
            case 2:
            case 4:
            case 5:
            case 6:
            case 7:
            case 9:
                saveState = 1;
                break;
            case 1:
                saveState = 10;
                break;
            case -1:
                zSaveLoad_ErrorPrompt(0);
                saveState = 1;
                break;
            case 11:
                saveState = 10;
                currentGame = 0;
                break;
            }
            break;
        case 5:
            break;
        case 11:
            zGameModeSwitch(eGameMode_Game);
            zGameStateSwitch(1);
            saveState = 6;
            break;
        case 10:
            S32 result = zSaveLoad_SaveGame();
            if (result == 1)
            {
                zGameModeSwitch(eGameMode_Game);
                zGameStateSwitch(1);
                saveState = 6;
                saveSuccess = 1;
                zEntEvent(zSceneFindObject(xStrHash("SV COMPLETED")), eEventVisible);
            }
            else
            {
                switch (result)
                {
                case 7:
                    zSaveLoad_SaveDamagedErrorPrompt(0);
                    break;
                case 8:
                    zSaveLoad_CardYankedErrorPrompt(0);
                    break;
                case 10:
                    zSaveLoad_CardPromptSpace(0);
                    break;
                case 9:
                default:
                    zSaveLoad_ErrorPrompt(0);
                    break;
                }

                if (saveLoadPromptSel == 2)
                {
                    saveState = 11;
                }
                else
                {
                    saveState = 1;
                }
            }
            break;
        }
    }

    sAccessType = 0;
    return saveSuccess;
}

void zSaveLoad_DispatchCB(U32 dispatchEvent, const F32* toParam)
{
    st_XSAVEGAME_DATA* inst;

    switch (dispatchEvent)
    {
    case eEventDispatcher_SLBack:
        saveLoadPromptSel = 4;
        break;
    case eEventDispatcher_SLCancel:
        saveLoadPromptSel = 2;
        break;
    case eEventDispatcher_SLRetry:
        saveLoadPromptSel = 3;
        break;
    case eEventDispatcher_SLSelectCard:
        currentCard = *toParam;
        inst = xSGInit(gGameMode == eGameMode_Save ? XSG_MODE_SAVE : XSG_MODE_LOAD);
        zSaveLoad_CardCheckSpaceSingle_doCheck(inst, currentCard);
        xSGDone(inst);
        break;
    case eEventDispatcher_SLSelectSlot:
        currentGame = *toParam;
        break;
    case eEventDispatcher_SLOkay:
        saveLoadPromptSel = 3;
        break;
    case eEventDispatcher_UserSelectYes:
        g_promptUserSelection = 0;
        break;
    case eEventDispatcher_UserSelectNo:
        g_promptUserSelection = 1;
        break;
    case eEventDispatcher_UserSelectBack:
        g_promptUserSelection = 2;
        break;
    case eEventDispatcher_UserSelectionReset:
        g_promptUserSelection = -1;
        break;
    case eEventDispatcher_LoadSavePromptDead:
        break;
    }
}

S32 xSGT_SaveInfoCB(void* vp, st_XSAVEGAME_DATA* xsgdata, S32* need, S32* most)
{
    *need = xSGWriteStrLen(currSceneStr);
    *most = *need << 1;
    return 1;
}

S32 xSGT_SaveProcCB(void* vp, st_XSAVEGAME_DATA* xsgdata, st_XSAVEGAME_WRITECONTEXT* wctxt)
{
    strcpy(currSceneStr, xUtil_idtag2string(globals.sceneCur->sceneID, 0));
    return xSGWriteData(xsgdata, wctxt, currSceneStr, 1, strlen(currSceneStr)) + 1;
}

S32 xSGT_SaveInfoPrefsCB(void* p1, st_XSAVEGAME_DATA* data, S32* need, S32* most)
{
    *need = 20;
    *most = *need * 2;
    return 1;
}

S32 xSGT_SaveProcPrefsCB(void* vp, st_XSAVEGAME_DATA* xsgdata, st_XSAVEGAME_WRITECONTEXT* wctxt)
{
    S32 amy;
    S32 sum = 0;
    F32 fMusicVolume = xSndMgrGetUserVolume(3);
    F32 f3DVolume = xSndMgrGetUserVolume(0);

    amy = xSGWriteData(xsgdata, wctxt, (S32*)&gSnd.sndMode, 1);
    sum += amy;
    amy = xSGWriteData(xsgdata, wctxt, &fMusicVolume, 1);
    sum += amy;
    amy = xSGWriteData(xsgdata, wctxt, &f3DVolume, 1);
    sum += amy;
    amy = xSGWriteData(xsgdata, wctxt, &globals.option_vibration, 1);
    sum += amy;
    amy = xSGWriteData(xsgdata, wctxt, &globals.option_subtitles, 1);
    sum += amy;
    return sum + 1;
}

S32 xSGT_LoadLoadCB(void* vp, st_XSAVEGAME_DATA* xsgdata, st_XSAVEGAME_READCONTEXT* rctxt, U32 ui,
                    S32 i)
{
    char bigbuf[32] = {};
    S32 compdiff = 0;

    xSGReadData(xsgdata, rctxt, bigbuf, 1, strlen(currSceneStr));
    if (strlen(currSceneStr) != strlen(bigbuf))
    {
        compdiff = 1;
    }
    if (compdiff == 0)
    {
        strcpy(sceneRead, bigbuf);
    }

    return compdiff == 0;
}

S32 xSGT_LoadPrefsCB(void* vp, st_XSAVEGAME_DATA* xsgdata, st_XSAVEGAME_READCONTEXT* rctxt, U32 ui,
                     S32 i)
{
    // not a DWARF local: the read target of xSGReadData (needs an address)
    S32 sndMode;
    F32 fMusicVol;
    F32 f3DVol;

    xSGReadData(xsgdata, rctxt, &sndMode, 1);
    // Only restore a saved stereo/surround mode when the current output isn't mono.
    if (gSnd.sndMode != xSndMode_Mono && sndMode != xSndMode_Mono)
    {
        gSnd.sndMode = (xSndMode)sndMode;
    }
    xSGReadData(xsgdata, rctxt, &fMusicVol, 1);
    xSGReadData(xsgdata, rctxt, &f3DVol, 1);
    xSGReadData(xsgdata, rctxt, &globals.option_vibration, 1);
    xSGReadData(xsgdata, rctxt, &globals.option_subtitles, 1);
    xSndMgrSetUserVolume(3, fMusicVol);
    xSndMgrSetUserVolume(-2, f3DVol);
    if (globals.option_vibration != 0)
    {
        xPadRumbleEnable(globals.currentActivePad, 1);
    }
    else
    {
        xPadRumbleEnable(globals.currentActivePad, 0);
    }
    return 1;
}

// Not in retail (unused, so the linker strips it), but its "SV EMPTY TXT" string remains where
// BfBB has slotIsEmpty. The body follows BfBB, with TSSM's empty-slot text.
U32 zSaveLoad_slotIsEmpty(U32 slot)
{
    char* label = zSaveLoadGameTable[slot].label;
    return strcmp(label, xTextFindString(xStrHash("SV EMPTY TXT"), NULL)) == 0 ? 1 : 0;
}

void zSaveLoad_TriggerAutoSave()
{
    S32 result;
    xBase* sendTo;

#ifdef PLATFORM_PC
    // Online co-op (5.7): an autosave asked for in the same frame as its
    // pickup, before zSaveLoadAutoSaveUpdate could drop it. FALSE at once
    // when co-op is off.
    if (preAutoSaving && iCoopNoAutoSave())
    {
        zSaveLoadPreAutoSave(false);
        printf("tssm: coop: autosave skipped (a co-op guest's game)\n");
        return;
    }
#endif

    if (preAutoSaving)
    {
        xSndMgrPauseSounds(-3, true, true);
        result = zSaveLoad_DoAutoSave();
        xSndMgrPauseSounds(-3, false, true);

        if (result < 0)
        {
            sendTo = zSceneFindObject(xStrHash("AUTO_SAVE_FAILED"));
            if (sendTo != NULL)
            {
                zEntEvent(sendTo, eEventVisible);
            }
            zGameStall();
        }

        sendTo = zSceneFindObject(xStrHash("AUTO_SAVE"));
        if (sendTo != NULL)
        {
            zEntEvent(sendTo, eEventInvisible);
        }
        zSaveLoadPreAutoSave(false);
    }
}
