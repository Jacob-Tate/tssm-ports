#include "zVar.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/printf.h>

#include "zGlobals.h"
#include "zMain.h"
#include "zReference.h"
#include "xutil.h"
#include "xFont.h"
#include "iTime.h"
#include "xTimer.h"
#include "xCounter.h"
#include "xTextAsset.h"
#include "zUICustom.h"
#include "zSaveLoad.h"
#include "zScene.h"
#include "zMenu.h"
#include "zDispatcher.h"

#include <string.h>

U32 zVarEntryCB_SndFXVol(void*);
U32 zVarEntryCB_SndMusicVol(void*);

S32 bad_card_available;
S32 bad_card_needed;

namespace
{
    U32 selSceneID;
} // namespace

namespace
{
    char* var_text_BadCard()
    {
        static char buffer[12];
        S32 card = zMenuGetBadCard();
        if (card > 0)
        {
            sprintf(buffer, "%c", 'A' + (card - 1));
        }
        else
        {
            strcpy(buffer, "A or B");
        }
        return buffer;
    }

    char* var_text_BadCardAvailable()
    {
        static char buffer[12];
        sprintf(buffer, "%d", bad_card_available);
        return buffer;
    }

    char* var_text_BadCardNeeded()
    {
        static char buffer[12];
        sprintf(buffer, "%d", bad_card_needed);
        return buffer;
    }

    // The compiled-out asserts are Ratatouille's; without them the zVMCB_GameSlotN callbacks call
    // the zVarGameSlotInfo(S32) helper out of line. 'size' exists only for its assert. "Empty" is
    // used directly, not via the PS2 DWARF's empty_string local, as retail reloads it for each use.
    char* zVarGameSlotInfo(S32 i, char* buffer, size_t max_len)
    {
        char date[32] = "";
        char time[32] = "";
        S32 hour;
        char ampm[32] = "";
        char biggerbuf[256] = "";

        strncpy(date, zSaveLoadGameTable[i].date, 5);
        date[2] = '/';
        sprintf(biggerbuf, "%s/%c%c", date, zSaveLoadGameTable[i].date[8],
                zSaveLoadGameTable[i].date[9]);
        strncpy(date, biggerbuf, sizeof(date));
        date[sizeof(date) - 1] = '\0';

        sprintf(time, "%c%c", zSaveLoadGameTable[i].date[11], zSaveLoadGameTable[i].date[12]);
        hour = atoi(time);
        if (hour >= 12)
        {
            strncpy(ampm, "PM", 3);
            if (hour != 12)
            {
                hour -= 12;
            }
        }
        else
        {
            strncpy(ampm, "AM", 3);
            if (hour == 0)
            {
                hour = 12;
            }
        }

        sprintf(time, "%d:%c%c %s", hour, zSaveLoadGameTable[i].date[14],
                zSaveLoadGameTable[i].date[15], ampm);
        sprintf(date, "%s %s", date, time);
        xASSERT(strlen(time) < 32);
        xASSERT(strlen(date) < 32);
        time[sizeof(time) - 1] = '\0';
        date[sizeof(date) - 1] = '\0';

        if (xStricmp(zSaveLoadGameTable[i].label, "Empty") == 0)
        {
            S32 size = sprintf(buffer, "Empty");
            xASSERT((unsigned int)size <= max_len);
        }
        else
        {
            zSaveLoad_BuildName(buffer, max_len, i);
        }

        return buffer;
    }

    // One static buffer shared by all the zVMCB_GameSlot callbacks, as in retail.
    inline char* zVarGameSlotInfo(S32 i)
    {
        static char buffer[128];
        return zVarGameSlotInfo(i, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot0()
    {
        static char buffer[128];
        return zVarGameSlotInfo(0, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot1()
    {
        static char buffer[128];
        return zVarGameSlotInfo(1, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot2()
    {
        static char buffer[128];
        return zVarGameSlotInfo(2, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot3()
    {
        static char buffer[128];
        return zVarGameSlotInfo(3, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot4()
    {
        static char buffer[128];
        return zVarGameSlotInfo(4, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot5()
    {
        static char buffer[128];
        return zVarGameSlotInfo(5, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot6()
    {
        static char buffer[128];
        return zVarGameSlotInfo(6, buffer, sizeof(buffer) - 1);
    }

    char* var_text_GameSlot7()
    {
        static char buffer[128];
        return zVarGameSlotInfo(7, buffer, sizeof(buffer) - 1);
    }

    char* var_text_MCAccessType()
    {
        // Text asset IDs (names unknown): accessing, loading, saving, formatting.
        static const U32 state_text[4] = { 0x31DCA62F, 0xF5F06159, 0x18891A95, 0xC8FAC798 };
        return xTextFindString(state_text[zSaveLoad_getMCAccessType()], NULL);
    }

    char* var_text_MCAutoSaveCard()
    {
        static char buffer[12];
        sprintf(buffer, "%c", 'A' + zSaveLoadGetAutoSaveCard());
        return buffer;
    }

    char* var_text_MCSelectedCard()
    {
        // Text asset IDs (names unknown): no card, card A, card B.
        static char buffer[32];
        char* text;
        if (currentCard == -1)
        {
            text = xTextFindString(0x6DE8EACC, NULL);
        }
        else if (currentCard == 0)
        {
            text = xTextFindString(0x94F41D1E, NULL);
        }
        else
        {
            text = xTextFindString(0xA681D36F, NULL);
        }
        strcpy(buffer, text);
        return buffer;
    }

    char* var_text_MCSelectedGame()
    {
        static char buffer[12];
        sprintf(buffer, "%d", zSaveLoad_getgame() + 1);
        return buffer;
    }

    char* var_text_SpaceAvailable()
    {
        static char buffer[12];
        sprintf(buffer, "%d", zSaveLoad_getMCavailable());
        return buffer;
    }

    char* var_text_SpaceAvailableString()
    {
        static char buffer[64];
        char spaceStr[32];
        S32 spaceAvailable = zSaveLoad_getMCavailable();

        memset(spaceStr, 0, sizeof(spaceStr));
        memset(buffer, 0, sizeof(buffer));
        sprintf(spaceStr, "%d", spaceAvailable);
        sprintf(buffer, "%s", spaceStr);
        return buffer;
    }

    char* var_text_SpaceNeeded()
    {
        static char buffer[12];
        sprintf(buffer, "%d", zSaveLoad_getMCneeded());
        return buffer;
    }

    char* var_text_MCName()
    {
        static char buffer[32];
        switch (zSaveLoad_getcard())
        {
        case 0:
            strcpy(buffer, "{i:LD MC1 TXT}");
            break;
        case 1:
            strcpy(buffer, "{i:LD MC2 TXT}");
            break;
        default:
            strcpy(buffer, "");
            break;
        }
        return buffer;
    }

    char* var_text_MCMaxSpace()
    {
        static char buffer[32];
        sprintf(buffer, "%d", 30);
        return buffer;
    }

    char* var_text_MCMinSpace()
    {
        static char buffer[32];
        sprintf(buffer, "%d", 10);
        return buffer;
    }

    char* var_text_ActivePad()
    {
        static char buffer[12];
        sprintf(buffer, "%d", globals.currentActivePad + 1);
        return buffer;
    }

    char* var_text_CurrentArea()
    {
        return (char*)zSceneGetAreaname(globals.sceneCur->sceneID);
    }

    char* var_text_CurrentDate()
    {
        static char buffer[32];
        iGetCurrFormattedDate(buffer);
        return buffer;
    }

    char* var_text_CurrentLevel()
    {
        return (char*)zSceneGetLevelName(globals.sceneCur->sceneID);
    }

    char* var_text_CurrentScene()
    {
        static char buffer[32];
        sprintf(buffer, "%s", xUtil_idtag2string(globals.sceneCur->sceneID, 0));
        return buffer;
    }

    char* var_text_CurrentTime()
    {
        static char buffer[32];
        iGetCurrFormattedTime(buffer);
        return buffer;
    }

    char* var_text_PlayerPosition()
    {
        static char buffer[48];
        xVec3& v = xglobals->___player_ent_dont_use_directly->frame->mat.pos;
        sprintf(buffer, "%d,%d,%d", (S32)v.x, (S32)v.y, (S32)v.z);
        return buffer;
    }

    char* var_text_PS2PadName()
    {
        // The PS2 build fills in the controller name here; on GC it stays empty.
        static char buffer[48];
        memset(buffer, 0, sizeof(buffer));
        return buffer;
    }

    char* var_text_SelectedArea()
    {
        return (char*)zSceneGetAreaname(selSceneID);
    }

    char* var_text_SoundFXVolume()
    {
        static char buffer[12];
        sprintf(buffer, "%d", zVarEntryCB_SndFXVol(NULL));
        return buffer;
    }

    char* var_text_SoundMusicVolume()
    {
        static char buffer[12];
        sprintf(buffer, "%d", zVarEntryCB_SndMusicVol(NULL));
        return buffer;
    }

    char* var_text_NumChestsNeeded()
    {
        static char buffer[3];
        sprintf(buffer, "%d", zUICustomExtrasManager::GetNumChestsNeeded());
        return buffer;
    }

    char* var_text_NumChestsCurrentLevel()
    {
        static char buffer[3];
        S16 world = zDispatcher_GetCurrentWorld();
        if (world >= 0)
        {
            sprintf(buffer, "%d", globals.player.InvTreasureChest[world]);
        }
        else
        {
            strcpy(buffer, "0");
        }
        return buffer;
    }

    char* var_text_NumChestsTotalCurrentLevel()
    {
        static char buffer[3];
        S16 world = zDispatcher_GetCurrentWorld();
        if (world >= 0)
        {
            sprintf(buffer, "%d", globals.player.InvTreasureChestMax[world]);
        }
        else
        {
            strcpy(buffer, "0");
        }
        return buffer;
    }

    var_type vars[35] = {
        { SUBSTR("ActivePad"), var_text_ActivePad },
        { SUBSTR("BadCard"), var_text_BadCard },
        { SUBSTR("BadCardAvailable"), var_text_BadCardAvailable },
        { SUBSTR("BadCardNeeded"), var_text_BadCardNeeded },
        { SUBSTR("CurrentArea"), var_text_CurrentArea },
        { SUBSTR("CurrentDate"), var_text_CurrentDate },
        { SUBSTR("CurrentLevel"), var_text_CurrentLevel },
        { SUBSTR("CurrentScene"), var_text_CurrentScene },
        { SUBSTR("CurrentTime"), var_text_CurrentTime },
        { SUBSTR("GameSlot0"), var_text_GameSlot0 },
        { SUBSTR("GameSlot1"), var_text_GameSlot1 },
        { SUBSTR("GameSlot2"), var_text_GameSlot2 },
        { SUBSTR("GameSlot3"), var_text_GameSlot3 },
        { SUBSTR("GameSlot4"), var_text_GameSlot4 },
        { SUBSTR("GameSlot5"), var_text_GameSlot5 },
        { SUBSTR("GameSlot6"), var_text_GameSlot6 },
        { SUBSTR("GameSlot7"), var_text_GameSlot7 },
        { SUBSTR("MCAccessType"), var_text_MCAccessType },
        { SUBSTR("MCAutoSaveCard"), var_text_MCAutoSaveCard },
        { SUBSTR("MCMaxSpace"), var_text_MCMaxSpace },
        { SUBSTR("MCMinSpace"), var_text_MCMinSpace },
        { SUBSTR("MCName"), var_text_MCName },
        { SUBSTR("MCSelectedCard"), var_text_MCSelectedCard },
        { SUBSTR("MCSelectedGame"), var_text_MCSelectedGame },
        { SUBSTR("NumChestsCurrentLevel"), var_text_NumChestsCurrentLevel },
        { SUBSTR("NumChestsNeeded"), var_text_NumChestsNeeded },
        { SUBSTR("NumChestsTotalCurrentLevel"), var_text_NumChestsTotalCurrentLevel },
        { SUBSTR("PlayerPosition"), var_text_PlayerPosition },
        { SUBSTR("PS2PadName"), var_text_PS2PadName },
        { SUBSTR("SelectedArea"), var_text_SelectedArea },
        { SUBSTR("SoundFXVolume"), var_text_SoundFXVolume },
        { SUBSTR("SoundMusicVolume"), var_text_SoundMusicVolume },
        { SUBSTR("SpaceAvailable"), var_text_SpaceAvailable },
        { SUBSTR("SpaceAvailableString"), var_text_SpaceAvailableString },
        { SUBSTR("SpaceNeeded"), var_text_SpaceNeeded },
    };

    // No out-of-line copy in the target: binary search of the (sorted) vars table.
    inline var_type* find_var(const substr& s)
    {
        U32 start = 0;
        U32 end = sizeof(vars) / sizeof(vars[0]);
        while (start != end)
        {
            U32 i = (start + end) / 2;
            var_type& t = vars[i];
            S32 c = icompare(s, t.name);
            if (c < 0)
            {
                end = i;
            }
            else if (c > 0)
            {
                start = i + 1;
            }
            else
            {
                return &t;
            }
        }

        return NULL;
    }

    inline char* var_text(const substr& name)
    {
        const var_type* var = find_var(name);
        if (var == NULL)
        {
            return NULL;
        }
        return var->get_text();
    }

    void parse_tag_var(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                       const xtextbox::split_tag& ti)
    {
        if (ti.action.size != 1 || ti.action.text[0] != ':' || ti.value.size <= 1)
        {
            return;
        }

        j.context = var_text(ti.value);
        j.context_size = 0;
        if (j.context != NULL)
        {
            j.context_size = 0xFC00;
            j.flag.insert = j.flag.dynamic = 1;
        }
    }
} // namespace

void var_init()
{
    static xtextbox::tag_type var_tag = { SUBSTR("var"), parse_tag_var, NULL, NULL };
    xtextbox::register_tags(&var_tag, 1);
}

void zVarInit(zVarEntry* table)
{
    for (S32 i = 0; i < 24; i++)
    {
        table[i].varNameID = xStrHash(table[i].varName);
    }
}

void zVarNewGame()
{
}

U32 zVarEntryCB_SndMode(void*)
{
    return gSnd.sndMode != xSndMode_Mono;
}

U32 zVarEntryCB_SndMusicVol(void*)
{
    F32 floatVol = xSndMgrGetUserVolume(3);
    floatVol *= 10.0f;
    return 0.5f + floatVol;
}

U32 zVarEntryCB_SndFXVol(void*)
{
    F32 floatVol = xSndMgrGetUserVolume(0);
    floatVol *= 10.0f;
    return 0.5f + floatVol;
}

U32 zVarEntryCB_MCAvailable(void* arg)
{
    if (xSGGameIsEmpty(NULL, 0) == 1)
    {
        if (xSGGameIsEmpty(NULL, 1) == 1)
        {
            return 0;
        }
        if (xSGGameIsEmpty(NULL, 1) == 0)
        {
            return 2;
        }
    }
    else if (xSGGameIsEmpty(NULL, 0) == 0)
    {
        if (xSGGameIsEmpty(NULL, 1) == 1)
        {
            return 1;
        }
        if (xSGGameIsEmpty(NULL, 1) == 0)
        {
            return 3;
        }
    }

    return 0;
}

U32 zVarEntryCB_VibrationOn(void* arg)
{
    // I'm pretty sure they actually and'd with a bit mask here, I couldn't find
    // another way (such as casting to bool) to get the right instructions.
    return globals.option_vibration & 0x1;
}

U32 zVarEntryCB_SubtitlesOn(void*)
{
    return globals.option_subtitles & 0x1;
}

char* zVMCB_SoundMusicVolume(char* buffer)
{
    return buffer + sprintf(buffer, "%d", zVarEntryCB_SndMusicVol(NULL));
}

char* zVMCB_SoundFXVolume(char* buffer)
{
    return buffer + sprintf(buffer, "%d", zVarEntryCB_SndFXVol(NULL));
}

char* zVMCB_Scene(char* buffer)
{
    return buffer + sprintf(buffer, "%s", xUtil_idtag2string(globals.sceneCur->sceneID, 0));
}

// Retail passes a stray trailing 0 to sprintf in the GameSlot/MCSelected callbacks.
char* zVMCB_GameSlot0(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(0), 0);
}

char* zVMCB_GameSlot1(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(1), 0);
}

char* zVMCB_GameSlot2(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(2), 0);
}

char* zVMCB_GameSlot3(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(3), 0);
}

char* zVMCB_GameSlot4(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(4), 0);
}

char* zVMCB_GameSlot5(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(5), 0);
}

char* zVMCB_GameSlot6(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(6), 0);
}

char* zVMCB_GameSlot7(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zVarGameSlotInfo(7), 0);
}

char* zVMCB_MCSelectedGame(char* buffer)
{
    return buffer + sprintf(buffer, "%d", zSaveLoad_getgame() + 1, 0);
}

char* zVMCB_MCSelectedCard(char* buffer)
{
    return buffer + sprintf(buffer, "%c", 'A' + zSaveLoad_getcard(), 0);
}

char* zVMCB_CurrentPosition(char* buffer)
{
    xVec3& v = xglobals->___player_ent_dont_use_directly->frame->mat.pos;
    return buffer + sprintf(buffer, "%d,%d,%d", (S32)v.x, (S32)v.y, (S32)v.z);
}

char* zVMCB_CurrentArea(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zSceneGetAreaname(globals.sceneCur->sceneID));
}

char* zVMCB_SelectedArea(char* buffer)
{
    return buffer + sprintf(buffer, "%s", zSceneGetAreaname(selSceneID));
}

char* zVMCB_CurrentDate(char* buffer)
{
    return buffer + iGetCurrFormattedDate(buffer);
}

char* zVMCB_CurrentTime(char* buffer)
{
    return buffer + iGetCurrFormattedTime(buffer);
}

char* zVMCB_SpaceAvailable(char* buffer)
{
    return buffer + sprintf(buffer, "%d", zSaveLoad_getMCavailable());
}

char* zVMCB_SpaceAvailableString(char* buffer)
{
    char spaceStr[64];
    S32 spaceAvailable = zSaveLoad_getMCavailable();

    memset(spaceStr, 0, sizeof(spaceStr));
    sprintf(spaceStr, "%d", spaceAvailable);
    return buffer + sprintf(buffer, "%s", spaceStr);
}

char* zVMCB_SpaceNeeded(char* buffer)
{
    return buffer + sprintf(buffer, "%d", zSaveLoad_getMCneeded());
}

char* zVMCB_MCAccessType(char* buffer)
{
    char text[64];
    switch (zSaveLoad_getMCAccessType())
    {
    case 0:
        strcpy(text, "Accessing");
        break;
    case 2:
        strcpy(text, "Saving data");
        break;
    case 1:
        strcpy(text, "Loading data");
        break;
    case 3:
        strcpy(text, "Formatting memory card");
        break;
    }
    return buffer + sprintf(buffer, "%s", text);
}

U32 zVarEntryCB_CurrentSceneLetter(void*)
{
    char s[16];
    sprintf(s, "%s", xUtil_idtag2string(globals.sceneCur->sceneID, 0));

    // Convert the most significant char to uppercase
    // Not in the PS2 DWARF: retail makes the range check a 0/1 flag first (likely an inlined
    // bool helper).
    U32 mostSignificantChar = s[0];
    bool lowercase = false;
    if (mostSignificantChar >= 'a' && mostSignificantChar <= 'z')
    {
        lowercase = true;
    }
    if (lowercase)
    {
        mostSignificantChar -= 0x20;
    }

    return (mostSignificantChar - 'A') + 1;
}

U32 zVarEntryCB_CurrentRoom(void*)
{
    S32 room;
    char s[16];
    sprintf(s, "%s", xUtil_idtag2string(globals.sceneCur->sceneID, 0));

    room = (s[2] - '0') * 10 + (s[3] - '0');

    // Convert the most significant char to uppercase
    // Not in the PS2 DWARF: retail makes the range check a 0/1 flag first (likely an inlined
    // bool helper).
    U32 mostSignificantChar = s[0];
    bool lowercase = false;
    if (mostSignificantChar >= 'a' && mostSignificantChar <= 'z')
    {
        lowercase = true;
    }
    if (lowercase)
    {
        mostSignificantChar -= 0x20;
    }

    return room + ((mostSignificantChar - 'A') + 1) * 100;
}

U32 zVarEntryCB_Date(void*)
{
    U32 date = iGetDay() + iGetMonth() * 100;
    return date;
}

U32 zVarEntryCB_Hour(void*)
{
    U32 hour = iGetHour();
    return hour;
}

U32 zVarEntryCB_Minute(void*)
{
    U32 minute = iGetMinute();
    return minute;
}

// The context of the "Counter Value" entry is an _xCounter.
U32 zVarEntryCB_CounterValue(void* context)
{
    return ((_xCounter*)context)->count;
}

U32 zVarEntryCB_IsEnabled(void* context)
{
    return xBaseIsEnabled((xBase*)context);
}

U32 zVarEntryCB_IsVisible(void* context)
{
    return xEntIsVisible((xEnt*)context) ? 1 : 0;
}

U32 zVarEntryCB_TimerSecondsLeftValue(void* context)
{
    xTimer* timer = (xTimer*)context;
    return timer->state == 1 ? (U32)timer->secondsLeft : 0;
}

U32 zVarEntryCB_TimerMilliSecondsLeftValue(void* context)
{
    xTimer* timer = (xTimer*)context;
    return timer->state == 1 ? (U32)(1000.0f * timer->secondsLeft) : 0;
}

U32 zVarEntryCB_IsMNUS(void*)
{
    return globals.sceneCur->sceneID == SCENE_ID_MNU_START ? 1 : 0;
}

U32 zVarEntryCB_DemoType(void*)
{
    return 0;
}

U32 zVarEntryCB_GoofyGooberTokens(void*)
{
    return globals.player.InvGoofyGoober;
}

U32 zVarEntryCB_ManlinessPoints(void*)
{
    return globals.player.InvManlinessPoints;
}

U32 zVarEntryCB_TreasureChest(void*)
{
    return globals.player.InvTreasureChestCurrentLevel;
}

U32 zVarEntryCB_PlayerCurrentHealth(void*)
{
    return globals.player.s->Health;
}

U32 zVarEntryCB_IsReferenceNULL(void* context)
{
    return ((zReference*)context)->current == NULL;
}

U32 zVarEntryCB_AlwaysPortal(void*)
{
    return globals.player.g.CheatAlwaysPortal;
}

zVarEntry zVarEntryTable[25] = {
    { 0, 0, "Sound mode", zVarEntryCB_SndMode },
    { 1, 0, "Music Volume", zVarEntryCB_SndMusicVol },
    { 2, 0, "SFX Volume", zVarEntryCB_SndFXVol },
    { 3, 0, "Memory Card Available", zVarEntryCB_MCAvailable },
    { 4, 0, "Vibration is on", zVarEntryCB_VibrationOn },
    { 5, 0, "Subtitles are enabled", zVarEntryCB_SubtitlesOn },
    { 6, 0, "Letter of scene", zVarEntryCB_CurrentSceneLetter },
    { 7, 0, "Room", zVarEntryCB_CurrentRoom },
    { 8, 0, "Current Date", zVarEntryCB_Date },
    { 9, 0, "Current Hour", zVarEntryCB_Hour },
    { 10, 0, "Current Minute", zVarEntryCB_Minute },
    { 11, 0, "Counter Value", zVarEntryCB_CounterValue },
    { 12, 0, "Is Enabled (0=No, 1=Yes)", zVarEntryCB_IsEnabled },
    { 13, 0, "Is Visible (0=No, 1=Yes)", zVarEntryCB_IsVisible },
    { 14, 0, "Timer Seconds Left", zVarEntryCB_TimerSecondsLeftValue },
    { 15, 0, "Timer Milliseconds Left", zVarEntryCB_TimerMilliSecondsLeftValue },
    { 16, 0, "Is MNUS?", zVarEntryCB_IsMNUS },
    { 17, 0, "Demo Type", zVarEntryCB_DemoType },
    { 18, 0, "Goofy Goober Tokens", zVarEntryCB_GoofyGooberTokens },
    { 19, 0, "Manliness Points", zVarEntryCB_ManlinessPoints },
    { 20, 0, "Level Treasure Chests", zVarEntryCB_TreasureChest },
    { 21, 0, "Player Current Health", zVarEntryCB_PlayerCurrentHealth },
    { 22, 0, "Is Reference NULL?", zVarEntryCB_IsReferenceNULL },
    { 23, 0, "Always Portal", zVarEntryCB_AlwaysPortal },
    { 0, 0, NULL, NULL },
};

zVarMagicEntry sVarMagicEntryTrable[23] = {
    { 0, "SoundMusicVolume", zVMCB_SoundMusicVolume },
    { 0, "SoundFXVolume", zVMCB_SoundFXVolume },
    { 0, "CurrentScene", zVMCB_Scene },
    { 0, "GameSlot0", zVMCB_GameSlot0 },
    { 0, "GameSlot1", zVMCB_GameSlot1 },
    { 0, "GameSlot2", zVMCB_GameSlot2 },
    { 0, "GameSlot3", zVMCB_GameSlot3 },
    { 0, "GameSlot4", zVMCB_GameSlot4 },
    { 0, "GameSlot5", zVMCB_GameSlot5 },
    { 0, "GameSlot6", zVMCB_GameSlot6 },
    { 0, "GameSlot7", zVMCB_GameSlot7 },
    { 0, "MCSelectedGame", zVMCB_MCSelectedGame },
    { 0, "MCSelectedCard", zVMCB_MCSelectedCard },
    { 0, "PlayerPosition", zVMCB_CurrentPosition },
    { 0, "CurrentArea", zVMCB_CurrentArea },
    { 0, "SelectedArea", zVMCB_SelectedArea },
    { 0, "CurrentDate", zVMCB_CurrentDate },
    { 0, "CurrentTime", zVMCB_CurrentTime },
    { 0, "SpaceAvailable", zVMCB_SpaceAvailable },
    { 0, "SpaceAvailableString", zVMCB_SpaceAvailableString },
    { 0, "SpaceNeeded", zVMCB_SpaceNeeded },
    { 0, "MCAccessType", zVMCB_MCAccessType },
    { 0, NULL, NULL },
};
