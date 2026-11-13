#include "zGameState.h"

#include "xDebug.h"
#include "xEvent.h"
#include "xserializer.h"
#include "xSnd.h"
#include "xTextureManager.h"

#include "zEnt.h"
#include "zGame.h"
#include "zGlobals.h"

extern S32 gBashButton;
extern S32 gBowlButton;
extern S32 gCartwheelButton;

void zGameStatePauseUnpauseSnd(bool pause);

S32 gGameState = eGameState_Dead;
eGameMode gGameMode;
_GameOstrich gGameOstrich;

// clang-format off
static U32 sGameMode_DispatchTable[eGameMode_Count] =
{
};

static U32 sGameState_DispatchTable[eGameState_Count] =
{
    eEventDispatcher_GameState_FirstTime,
    eEventDispatcher_GameState_Play,
    eEventDispatcher_GameState_LoseChance,
    eEventDispatcher_GameState_GameOver,
    eEventDispatcher_GameState_SceneSwitch,
    eEventDispatcher_GameState_Dead
};

static U32 sPauseState_DispatchTable[ePauseState_Count] =
{
    eEventDispatcher_PauseState_Pause,
    eEventDispatcher_PauseState_Options
};

static U32 sSaveState_DispatchTable[eSaveState_Count] =
{
    eEventDispatcher_SaveState_SelectMemCard,
    eEventDispatcher_SaveState_SelectSlot,
    eEventDispatcher_SaveState_Saving
};

static U32 sOptionsState_DispatchTable[eOptionsState_Count] =
{
    eEventDispatcher_OptionsState_Options
};

static U32 sLoadState_DispatchTable[eLoadState_Count] =
{
    eEventDispatcher_LoadState_SelectMemCard,
    eEventDispatcher_LoadState_SelectSlot,
    eEventDispatcher_LoadState_Loading
};

static U32 sTitleState_DispatchTable[eTitleState_Count] =
{
    eEventDispatcher_TitleState_Start,
    eEventDispatcher_TitleState_Attract
};

static U32 sIntroState_DispatchTable[eIntroState_Count] =
{
    eEventDispatcher_IntroState_Sony,
    eEventDispatcher_IntroState_Publisher,
    eEventDispatcher_IntroState_Developer,
    eEventDispatcher_IntroState_License
};

static U32 sGameState_DoDispatchTable[eGameState_Count] =
{
    eEventDispatcher_SetGameState_FirstTime,
    eEventDispatcher_SetGameState_Play,
    eEventDispatcher_SetGameState_LoseChance,
    eEventDispatcher_SetGameState_GameOver,
    eEventDispatcher_SetGameState_GameStats,
    eEventDispatcher_SetGameState_SceneSwitch,
    eEventDispatcher_SetGameState_Dead,
    eEventDispatcher_SetGameState_Exit
};

static U32 sPauseState_DoDispatchTable[ePauseState_Count] =
{
    eEventDispatcher_SetPauseState_Pause,
    eEventDispatcher_SetPauseState_Options
};

static U32 sSaveState_DoDispatchTable[eSaveState_Count] =
{
    eEventDispatcher_SetSaveState_SelectMemCard,
    eEventDispatcher_SetSaveState_SelectSlot,
    eEventDispatcher_SetSaveState_Saving
};

static U32 sOptionsState_DoDispatchTable[eOptionsState_Count] =
{
    eEventDispatcher_SetOptionsState_Options
};

static U32 sLoadState_DoDispatchTable[eLoadState_Count] =
{
    eEventDispatcher_SetLoadState_SelectMemCard,
    eEventDispatcher_SetLoadState_SelectSlot,
    eEventDispatcher_SetLoadState_Loading
};

static U32 sTitleState_DoDispatchTable[eTitleState_Count] =
{
    eEventDispatcher_SetTitleState_Start,
    eEventDispatcher_SetTitleState_Attract
};

static U32 sIntroState_DoDispatchTable[eIntroState_Count] =
{
    eEventDispatcher_SetIntroState_Sony,
    eEventDispatcher_SetIntroState_Publisher,
    eEventDispatcher_SetIntroState_Developer,
    eEventDispatcher_SetIntroState_License
};
// clang-format on

S32 zGameStateGet()
{
    return gGameState;
}

eGameMode zGameModeGet()
{
    return gGameMode;
}

_GameOstrich zGameGetOstrich()
{
    return gGameOstrich;
}

void zGameSetOstrich(_GameOstrich o)
{
    gGameOstrich = o;
}

static inline S32 zGameStateFindEvent(U32* eventList, S32 eventCount, S32 targetMode,
                                      S32 targetEvent, S32* new_mode, S32* new_state)
{
    for (S32 i = 0; i < eventCount; ++i)
    {
        if (targetEvent == eventList[i])
        {
            *new_mode = targetMode;
            *new_state = i;
            return 1;
        }
    }
    return 0;
}

// The asserts are compiled out; 12 of them keep GC/2.6 from auto-inlining this into zGameLoop
// (>= 30 statements), as retail calls it.
void zGameStateSwitchEvent(S32 event)
{
    // cur_mode/cur_state are not in the PS2 DWARF (Ratatouille's has them): retail keeps both in
    // registers across the event search.
    S32 cur_mode = gGameMode;
    S32 cur_state = gGameState;
    S32 new_mode = -1;
    S32 new_state = -1;
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (zGameStateFindEvent(sGameState_DoDispatchTable, eGameState_Count, eGameMode_Game, event,
                            &new_mode, &new_state) ||
        zGameStateFindEvent(sPauseState_DoDispatchTable, ePauseState_Count, eGameMode_Pause, event,
                            &new_mode, &new_state) ||
        zGameStateFindEvent(sSaveState_DoDispatchTable, eSaveState_Count, eGameMode_Save, event,
                            &new_mode, &new_state) ||
        zGameStateFindEvent(sOptionsState_DoDispatchTable, eOptionsState_Count, eGameMode_Options,
                            event, &new_mode, &new_state) ||
        zGameStateFindEvent(sLoadState_DoDispatchTable, eLoadState_Count, eGameMode_Load, event,
                            &new_mode, &new_state) ||
        zGameStateFindEvent(sTitleState_DoDispatchTable, eTitleState_Count, eGameMode_Title, event,
                            &new_mode, &new_state) ||
        zGameStateFindEvent(sIntroState_DoDispatchTable, eIntroState_Count, eGameMode_Intro, event,
                            &new_mode, &new_state))
    {
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (new_mode != cur_mode)
    {
        zGameModeSwitch((eGameMode)new_mode);
    }

    if (new_mode != cur_mode || new_state != cur_state)
    {
        zGameStateSwitch(new_state);

        if (new_state == eGameState_Exit)
        {
            xSerialWipeMainBuffer();
        }
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
}

void zGameStateSwitch(S32 theNewState)
{
    S32 stateOld = gGameState;
    U32 theEvent = 0;

    gGameState = theNewState;

    if (theNewState == eGameState_Play && stateOld == eGameState_FirstTime)
    {
        xTextureManager::Get().FreeMemory();

        startPressed = 1;

        if (!globals.player.ControlOff && (globals.pad0->on & XPAD_BUTTON_SQUARE))
        {
            gBashButton = 1;
        }

        if (!globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O)))
        {
            gBowlButton = 1;
        }

        // Retail tests the same button as for the bowl here.
        if (!globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O)))
        {
            gCartwheelButton = 1;
        }
    }

    switch (gGameMode)
    {
    case eGameMode_Intro:
    {
        theEvent = sIntroState_DispatchTable[theNewState];
        break;
    }
    case eGameMode_Title:
    {
        theEvent = sTitleState_DispatchTable[theNewState];
        break;
    }
    case eGameMode_Load:
    {
        theEvent = sLoadState_DispatchTable[theNewState];
        break;
    }
    case eGameMode_Options:
    {
        theEvent = sOptionsState_DispatchTable[theNewState];
        break;
    }
    case eGameMode_Save:
    {
        theEvent = sSaveState_DispatchTable[theNewState];
        break;
    }
    case eGameMode_Pause:
    {
        theEvent = sPauseState_DispatchTable[theNewState];
        break;
    }
    case eGameMode_Game:
    {
        theEvent = sGameState_DispatchTable[theNewState];
        break;
    }
    }

    if (theEvent)
    {
        zEntEventAllOfType(theEvent, eBaseTypeDispatcher);
    }
}

void SB04FMVPauseSoundCB(bool doPause)
{
    if (zGameIsPaused())
    {
        xSndMgrPauseSounds(3, doPause, TRUE);
    }
    else if (doPause)
    {
        xSndMgrPauseSounds(-3, TRUE, TRUE);
    }
    else
    {
        xSndMgrPauseSounds(-3, FALSE, TRUE);
    }
}

// The asserts are compiled out; 13 of them keep GC/2.6 from auto-inlining this (>= 30 statements),
// as retail calls it. The first three are Ratatouille's; the other conditions are unknown
// (placeholders).
void zGameModeSwitch(eGameMode modeNew)
{
    xASSERT(modeNew > eGameMode_Boot);
    xASSERT(modeNew < eGameMode_Count);

    eGameMode modeOld = gGameMode;

    xASSERT(modeOld != modeNew);

    bool pausing = (modeNew == eGameMode_Save || modeNew == eGameMode_Pause);
    bool unpausing = (modeOld == eGameMode_Save || modeOld == eGameMode_Pause);

    pausing = pausing || modeNew == eGameMode_Stall;
    unpausing = unpausing || modeOld == eGameMode_Stall;

    if (pausing && modeOld == eGameMode_Game)
    {
        xTextureManager::Get().FreeMemoryBlockingCall();
        zGameStatePauseUnpauseSnd(TRUE);
    }
    else if ((unpausing || modeOld == eGameMode_Load) && modeNew == eGameMode_Game &&
             globals.sceneCur->pendingPortal == NULL)
    {
        zGameStatePauseUnpauseSnd(FALSE);
    }

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

    gGameMode = modeNew;

    zEntEventAllOfType(sGameMode_DispatchTable[modeNew], eBaseTypeDispatcher);
}

void zGameStatePauseUnpauseSnd(bool pause)
{
    xSndMgrPauseSounds(0, pause, TRUE);
    xSndMgrPauseSounds(1, pause, TRUE);
    xSndMgrPauseSounds(2, pause, FALSE);
}
