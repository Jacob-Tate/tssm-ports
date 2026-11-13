#include "zMenuSB.h"
#include "zUIModel.h"
#include "zUICustom.h"
#include "zUIText.h"
#include "zUIImage.h"
#include "zTalkBox.h"
#include "xHudMeter.h"
#include "zMenu.h"
#include "zEntPlayer.h"
#include "zPlayerSB.h"

#include <string.h>
#ifdef PLATFORM_PC
#include "iScreen.h"
#endif

extern _tagGameVidMode gVidMode;
extern S32 g_flg_chEnabled;

namespace
{
    PowerUpMenuTableEntry sPowerUpMenuTable[11] = {
        { ePOWERUPMENU_KarateSpin, "POWERUP_COUNTER_KS", 0, NULL },
        { ePOWERUPMENU_Bash, "POWERUP_COUNTER_BA", 1, NULL },
        { ePOWERUPMENU_BubbleBowl, "POWERUP_COUNTER_SB", 2, NULL },
        { ePOWERUPMENU_SonicWave, "POWERUP_COUNTER_SW", 3, NULL },
        { ePOWERUPMENU_BellyBump, "POWERUP_COUNTER_BB", 4, NULL },
        { ePOWERUPMENU_Cartwheel, "POWERUP_COUNTER_CW", 5, NULL },
        { ePOWERUPMENU_BellyFlop, "POWERUP_COUNTER_BF", 6, NULL },
        { ePOWERUPMENU_Throw, "POWERUP_COUNTER_TH", 7, NULL },
        { ePOWERUPMENU_SBHealth, "POWERUP_COUNTER_HS", 8, NULL },
        { ePOWERUPMENU_PatHealth, "POWERUP_COUNTER_HP", 9, NULL },
        { ePOWERUPMENU_PointsAvail, "POWERUP_COUNTER_PA", ePOWERUPMENU_PointsAvail, NULL },
    };
    U32 sPowerUpMenuTableSize = sizeof(sPowerUpMenuTable) / sizeof(sPowerUpMenuTable[0]);
    char* cPowerUpMenuRefreshGroupName = "PU SET TEXT ALL GROUP";

    // The movie shown for each upgrade level (the first level of a power-up is not an upgrade).
    PowerUpMovieTableEntry sPowerUpMovieTable[10] = {
        { 0, { eFMVFile_NoMovie, eFMVFile_Pwr_SB_Karate2, eFMVFile_NoMovie } },
        { 1, { eFMVFile_Pwr_SB_Bash1, eFMVFile_Pwr_SB_Bash2, eFMVFile_NoMovie } },
        { 2, { eFMVFile_Pwr_SB_Bowl1, eFMVFile_Pwr_SB_Bowl2, eFMVFile_NoMovie } },
        { 3, { eFMVFile_Pwr_SB_Guitar1, eFMVFile_Pwr_SB_Guitar2, eFMVFile_NoMovie } },
        { 4, { eFMVFile_NoMovie, eFMVFile_Pwr_Pat_Spin2, eFMVFile_NoMovie } },
        { 5, { eFMVFile_Pwr_Pat_Cartwheel1, eFMVFile_Pwr_Pat_Cartwheel2, eFMVFile_NoMovie } },
        { 6, { eFMVFile_Pwr_Pat_Bellyflop1, eFMVFile_Pwr_Pat_Bellyflop2, eFMVFile_NoMovie } },
        { 7, { eFMVFile_Pwr_Pat_Throw1, eFMVFile_Pwr_Pat_Throw2, eFMVFile_NoMovie } },
        { 8, { eFMVFile_Pwr_SB_Health4, eFMVFile_Pwr_SB_Health5, eFMVFile_Pwr_SB_Health6 } },
        { 9, { eFMVFile_Pwr_Pat_Health4, eFMVFile_Pwr_Pat_Health5, eFMVFile_Pwr_Pat_Health6 } },
    };
    U32 sPowerUpMovieTableSize = sizeof(sPowerUpMovieTable) / sizeof(sPowerUpMovieTable[0]);
    taskEntry sDefaultWorldTasks[20];
    U32 sNumWorlds;
    extraEntry sExtrasList[50];
    U32 sNumExtras;
    U8 sNumChestsNeeded;
    eFMVFile sExtrasMovieList[14] = {
        eFMVFile_Trailer2, eFMVFile_Trailer3, eFMVFile_Story1,  eFMVFile_Story2,  eFMVFile_Story3,
        eFMVFile_Story4,   eFMVFile_Story5,   eFMVFile_Story6,  eFMVFile_Story7,  eFMVFile_Story8,
        eFMVFile_Story9,   eFMVFile_Story10,  eFMVFile_Story11, eFMVFile_Story12,
    };
    S32 sExtrasMovieListSize;

    // Entries 0-3 are the costumes unlocked on the extras screen, 4-7 the costumes
    // unlocked by cheats (gameExtrasFlag is the cheat's g_flg_chEnabled bit).
    extraCostumeEntry sExtraCostumeList[8] = {
        { 'PLSB', eCurrentPlayerSpongeBob, 0 },
        { 'PLSD', eCurrentPlayerSpongeBob, 0 },
        { 'PLPB', eCurrentPlayerPatrick, 0 },
        { 'PLP1', eCurrentPlayerPatrick, 0 },
        { 'PLSA', eCurrentPlayerSpongeBob, 0x2000000 },
        { 'PLSC', eCurrentPlayerSpongeBob, 0x4000000 },
        { 'PLPA', eCurrentPlayerPatrick, 0x10000000 },
        { 'PLPC', eCurrentPlayerPatrick, 0x8000000 },
    };
    U32 sExtraCostumeListSize = sizeof(sExtraCostumeList) / sizeof(sExtraCostumeList[0]);
    U8 bNoChatter;
    xModelInstance* tempModel;
    U32 sNextSceneID;
} // namespace

xVec3 UpgradeSparkleEffect::sparkleStart = { 0.0f, 0.0f, 0.0f };
xVec3 UpgradeSparkleEffect::sparkleEnd = { 640.0f, 0.0f, 0.0f };
xVec3 UpgradeSparkleEffect::sparkleScale = { 1.0f, 1.0f, 2.0f };
xVec3 UpgradeSparkleEffect::sparkleVel = { 0.2f, 0.2f, 0.2f };
F32 UpgradeSparkleEffect::sparkleTime = 1e38f;
F32 UpgradeSparkleEffect::totalTimeForEffect;
F32 UpgradeSparkleEffect::multiplier;
F32 UpgradeSparkleEffect::randomYOffset;
F32 UpgradeSparkleEffect::probability;
zUITextAsset* UpgradeSparkleEffect::powerupText[10];
S32 UpgradeSparkleEffect::powerupIndex = -1;
U8 UpgradeSparkleEffect::effectStarted;
F32 UpgradeSparkleEffect::fmvDelay = -1.0f;
iSndGroupHandle UpgradeSparkleEffect::upgradeSoundGroupHandle = (iSndGroupHandle)-1;

namespace
{
    xSoundFX* sCurrentChatter;
} // namespace

// Defined in zGameExtras.cpp.
extern S32 g_flg_chEnabled;

// Defined in zMusic.cpp.
extern zMusicTrackInfo sMusicTrack[2];
extern zMusicState sMusic;

// Not in the PS2 DWARF (no symbol): stops the extras-screen chatter sound that is playing.
// The PS2 line table puts all of it on a single line of PlayUpgradeFMV.
static inline void StopChatter()
{
    if (sCurrentChatter != NULL)
    {
        zEntEvent(sCurrentChatter, eEventStop);
        sCurrentChatter = NULL;
    }
}

// Not in the PS2 DWARF (no symbol): plays the movie of the power-up's current upgrade level.
static inline void PlayPowerUpMovie(S32 powerupIdx)
{
    U32 level = globals.player.g.PowerUp[powerupIdx];
    U32 i;

    for (i = 0; i < sPowerUpMovieTableSize; i++)
    {
        if (powerupIdx == sPowerUpMovieTable[i].powerup)
        {
            zMenuFMVPlay(zFMVFileGetName(sPowerUpMovieTable[i].level[level - 1]), 1, 0.1f, true,
                         true);
            return;
        }
    }
}

static inline U32 GetWorldNumber(U32 sceneID);
static inline U32 GetExtraStatus(U32 type, U32 id);
inline void ClearAllButThisCostume(U32 index);

// Not in the PS2 DWARF (no symbol, inlined into zMenuSB_InitPowerUpMenu).
static inline PowerUpMenuTableEntry* GetPowerUpMenuTableEntry(U32 entry)
{
    U32 i;

    for (i = 0; i < sPowerUpMenuTableSize; i++)
    {
        if (entry == sPowerUpMenuTable[i].entry)
        {
            return &sPowerUpMenuTable[i];
        }
    }

    return NULL;
}

// Not in the PS2 DWARF (no symbol, inlined into zMenuSB_InitPowerUpMenu).
static inline void InitPowerUpMenuCounter(U32 entry)
{
    PowerUpMenuTableEntry* tableEntry = GetPowerUpMenuTableEntry(entry);
    tableEntry->counter = (_xCounter*)zSceneFindObject(xStrHash(tableEntry->name));
}

void zMenuSB_InitPowerUpMenu()
{
    U32 i;

    for (i = ePOWERUPMENU_KarateSpin; i < ePOWERUPMENU_Max; i++)
    {
        InitPowerUpMenuCounter(i);
    }

    zMenuSB_UpdatePowerUpMenu();
}

void zMenuSB_UpdatePowerUpMenu()
{
    U32 i;

    for (i = 0; i < sPowerUpMenuTableSize; i++)
    {
        if (sPowerUpMenuTable[i].counter != NULL)
        {
            if (sPowerUpMenuTable[i].powerup != ePOWERUPMENU_PointsAvail)
            {
                sPowerUpMenuTable[i].counter->count =
                    globals.player.g.PowerUp[sPowerUpMenuTable[i].powerup];
            }
            else if (sPowerUpMenuTable[i].entry == ePOWERUPMENU_PointsAvail)
            {
                sPowerUpMenuTable[i].counter->count = globals.player.CurrentUnusedSkillPoints;
            }
        }
    }

    // Refresh the power-up menu if it is the page currently shown in the pause menu.
    // not in the DWARF: the counter is tested twice in one expression.
    _xCounter* counter =
        (_xCounter*)zSceneFindObject(xStrHash("PAUSESTATE MENU OR POWERUP COUNTER"));
    bool refresh = counter != NULL && counter->count == 2 && zGameIsPaused();
    if (refresh)
    {
        zEntEvent(cPowerUpMenuRefreshGroupName, eEventEvaluate);
    }
}

bool zMenuSB_EnsureFreeAudioStream(bool willingToStopMainGameMusic)
{
    bool haveFreeStreamedVoice;

    xSndMgrStopSounds(2, false);
    haveFreeStreamedVoice = xSndMgrVoiceAvailable(exVoiceType_Streamed, 0);
    if (haveFreeStreamedVoice)
    {
        return true;
    }

    if (willingToStopMainGameMusic)
    {
        if (sMusicTrack[0].sndHandle != -1 && sMusicTrack[1].sndHandle != -1 &&
            sMusicTrack[1].trackDesc == eTempMusic)
        {
            sMusic.uMusicRestart = sMusicTrack[0].sndAssetID;
            _zMusicStop(0);
        }
    }

    return xSndMgrVoiceAvailable(exVoiceType_Streamed, 0);
}

void zMenuSB_SaveMenuCounters(xSerial* s)
{
    U32 taskGroupID;
    xGroup* group;
    U32 numCounters;
    _xCounter* counter;
    U32 i;

    taskGroupID = xStrHash("TASK COUNTER GROUP");
    group = (xGroup*)zSceneFindObject(taskGroupID);
    numCounters = xGroupGetCount(group);
    for (i = 0; i < numCounters; i++)
    {
        counter = (_xCounter*)xGroupGetItemPtr(group, i);
        xCounterSave(counter, s);
    }
}

void zMenuSB_LoadMenuCounters(xSerial* s)
{
    U32 taskGroupID;
    xGroup* group;
    U32 numCounters;
    _xCounter* counter;
    U32 i;

    taskGroupID = xStrHash("TASK COUNTER GROUP");
    group = (xGroup*)zSceneFindObject(taskGroupID);
    numCounters = xGroupGetCount(group);
    for (i = 0; i < numCounters; i++)
    {
        counter = (_xCounter*)xGroupGetItemPtr(group, i);
        xCounterLoad(counter, s);
    }
}

void zMenuSB_InitTaskDefaults(xIniFile* ini)
{
    S32 idx;
    char* buf;
    U32 currentWorld;
    U32 currentTask;

    memset(sDefaultWorldTasks, 0, sizeof(sDefaultWorldTasks));
    idx = 0;
    buf = xIniGetString(ini, "TaskStatus", "", idx);
    currentWorld = 0;
    while (idx > 0)
    {
        while (is_ws(*buf))
        {
            buf++;
        }

        strncpy(sDefaultWorldTasks[currentWorld].name, buf, 4);
        buf += 4;

        for (currentTask = 0; *buf != '\0'; buf++, currentTask++)
        {
            while (is_ws(*buf))
            {
                buf++;
            }

            sDefaultWorldTasks[currentWorld].taskStatus[currentTask] = *buf - '0';
        }

        sDefaultWorldTasks[currentWorld].numTasks = currentTask;
        currentWorld++;
        buf = xIniGetString(ini, "TaskStatus", "", idx);
    }

    sNumWorlds = currentWorld;
}

void zMenuSB_LoadTaskDefaults()
{
    char counterName[19] = "COUNTER_01_01_BB01";
    U32 currentWorld;
    U32 currentTask;
    _xCounter* counter;

    for (currentWorld = 0; currentWorld < sNumWorlds; currentWorld++)
    {
        strncpy(&counterName[14], sDefaultWorldTasks[currentWorld].name, 4);
        counterName[8] = (currentWorld + 1) / 10 + '0';
        counterName[9] = (currentWorld + 1) % 10 + '0';

        for (currentTask = 0; currentTask < sDefaultWorldTasks[currentWorld].numTasks;
             currentTask++)
        {
            counterName[11] = (currentTask + 1) / 10 + '0';
            counterName[12] = (currentTask + 1) % 10 + '0';

            counter = (_xCounter*)zSceneFindObject(xStrHash(counterName));
            if (counter != NULL)
            {
                counter->count = sDefaultWorldTasks[currentWorld].taskStatus[currentTask];
            }
        }
    }
}

// No symbol: inlined into AllTasksCB. Its counterName template follows zMenuSB_LoadTaskDefaults'
// in retail's .rodata, so it is defined here.
inline void zMenuSB_UnlockAllTasks()
{
    char counterName[19] = "COUNTER_01_01_BB01";
    U32 currentWorld;
    U32 currentTask;
    _xCounter* counter;

    for (currentWorld = 0; currentWorld < sNumWorlds; currentWorld++)
    {
        strncpy(&counterName[14], sDefaultWorldTasks[currentWorld].name, 4);
        counterName[8] = (currentWorld + 1) / 10 + '0';
        counterName[9] = (currentWorld + 1) % 10 + '0';

        for (currentTask = 0; currentTask < sDefaultWorldTasks[currentWorld].numTasks;
             currentTask++)
        {
            counterName[11] = (currentTask + 1) / 10 + '0';
            counterName[12] = (currentTask + 1) % 10 + '0';

            counter = (_xCounter*)zSceneFindObject(xStrHash(counterName));
            if (counter != NULL && counter->count == 1)
            {
                counter->count = 2;
            }
        }
    }
}

U32 zMenuSB_IsTaskUnlocked(U32 sceneID, U32 taskID)
{
    if (sceneID == 'XX00')
    {
        return 0;
    }

    // 'world' is not a DWARF local (it may belong to an inline helper); folding the '- 1' into the
    // index lets CW merge it into the load offset, which the target does not do.
    U32 world = GetWorldNumber(sceneID) - 1;
    return sDefaultWorldTasks[world].taskStatus[taskID] != 0;
}

// Returns the 1-based number of the world with the given scene ID (0 if there is none).
static inline U32 GetWorldNumber(U32 sceneID)
{
    U32 i;

    for (i = 0; i < sNumWorlds; i++)
    {
        if (sceneID ==
            ((sDefaultWorldTasks[i].name[0] << 24) | (sDefaultWorldTasks[i].name[1] << 16) |
             (sDefaultWorldTasks[i].name[2] << 8) | sDefaultWorldTasks[i].name[3]))
        {
            return i + 1;
        }
    }

    return 0;
}

U8 zUICustomExtrasManager::GetNumChestsNeeded()
{
    return sNumChestsNeeded;
}

U32 zUICustomExtrasManager::IsCostumeEnabled(eSNDCurrentPlayer player)
{
    U32 i;

    for (i = 0; i < sExtraCostumeListSize; i++)
    {
        if (sExtraCostumeList[i].player == player)
        {
            if (i < 4)
            {
                if (GetExtraStatus(4, i + 1))
                {
                    return sExtraCostumeList[i].costumeID;
                }
            }
            else if ((sExtraCostumeList[i].gameExtrasFlag & zGameExtras_CheatFlags()) != 0)
            {
                return sExtraCostumeList[i].costumeID;
            }
        }
    }

    return 0;
}

U32 zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats cheat)
{
    return GetExtraStatus(5, cheat);
}

// Status of the extra with the given type and id (0 if it is locked or not in the list).
static inline U32 GetExtraStatus(U32 type, U32 id)
{
    U32 i;

    for (i = 0; i < sNumExtras; i++)
    {
        if (sExtrasList[i].type == type && sExtrasList[i].id == id)
        {
            return sExtrasList[i].status;
        }
    }

    return 0;
}

U32 zUICustomExtrasManager::GivePlayerTreasureChest(U32 numChests)
{
    U32 newChestTotal;
    U32 i;
    xBase* b;

    if (numChests == 0)
    {
        return 0;
    }

    newChestTotal = globals.player.InvTreasureChestTotal;
    globals.player.InvTreasureChestTotal += numChests;
    for (i = 0; i < sNumExtras; i++)
    {
        if (sExtrasList[i].chestsNeeded > newChestTotal &&
            sExtrasList[i].chestsNeeded <= globals.player.InvTreasureChestTotal)
        {
            b = zSceneFindObject(0x8C118620);
            if (b != NULL && b->baseType == eBaseTypeTimer)
            {
                zEntEvent(b, eEventRun);
            }
            return 1;
        }
    }

    return 0;
}

void zUICustomExtrasManager::ResetExtrasStatus()
{
    U32 i;

    for (i = 0; i < sNumExtras; i++)
    {
        sExtrasList[i].status = 0;
    }
}

void zUICustomExtrasManager::InitExtrasList(xIniFile* ini)
{
    S32 idx;
    char* buf;
    U32 currentExtra;
    S32 chestCount;

    memset(sExtrasList, 0, sizeof(sExtrasList));
    idx = 0;
    buf = xIniGetString(ini, "Extra", "", idx);
    currentExtra = 0;
    chestCount = 0;
    while (idx > 0)
    {
        while (is_ws(*buf))
        {
            buf++;
        }

        sExtrasList[currentExtra].type = atoi(buf);
        while (!is_ws(*buf))
        {
            buf++;
        }

        while (is_ws(*buf))
        {
            buf++;
        }

        sExtrasList[currentExtra].id = atoi(buf);
        while (!is_ws(*buf))
        {
            buf++;
        }

        while (is_ws(*buf))
        {
            buf++;
        }

        if (buf != NULL)
        {
            chestCount += atoi(buf);
        }
        else
        {
            chestCount++;
        }

        sExtrasList[currentExtra].chestsNeeded = chestCount;
        sExtrasList[currentExtra].status = 0;
        currentExtra++;
        buf = xIniGetString(ini, "Extra", "", idx);
    }

    sNumExtras = currentExtra;
}

zUICustomExtrasManager::zUICustomExtrasManager(zUI* ui)
    : zUICustom(ui), mCurRow(0), mCurColumn(0), mNumRows(0), mNumColumns(0), mOffsetX(0.0f),
      mOffsetY(0.0f), mbInSubScreen(0), mGrid(NULL), mIconTypes(NULL), mAuraGroup(NULL),
      mDescription(NULL), mActionText(NULL), mNextExtraText(NULL), mNextExtraGroup(NULL),
      mSoundMove(NULL), mSoundDeny(NULL), mSoundConfirm(NULL), mSubScreens(NULL)
{
}

void zUICustomExtrasManager::InitScreen()
{
    U32 i;
    xGroup* subscreenGroup;
    zUI* subUI;
    xGroup* gridGroup;
    xBase* b;
    xGroup* iconGroup;
    zUI* baseIcon;
    xGroup* rowGroup;
    U8 size;
    U8 j;

    subscreenGroup = (xGroup*)zSceneFindObject(ui->asset->customWidget);
    mSubScreens = (zUICustom**)xMemAlloc(gActiveHeap, 6 * sizeof(zUICustom*), 0);
    memset(mSubScreens, 0, 6 * sizeof(zUICustom*));

    subUI = (zUI*)xGroupGetItemPtr(subscreenGroup, 1);
    if (subUI != NULL)
    {
        subUI->custom = new zUICustomExtrasSound(subUI);
        mSubScreens[2] = subUI->custom;
    }

    subUI = (zUI*)xGroupGetItemPtr(subscreenGroup, 2);
    if (subUI != NULL)
    {
        subUI->custom = new zUICustomExtrasArt(subUI);
        mSubScreens[3] = subUI->custom;
    }

    gridGroup = NULL;
    b = zSceneFindObject(0x7C0DAEA6);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        gridGroup = (xGroup*)b;
    }

    iconGroup = NULL;
    b = zSceneFindObject(0x1FC8BE0E);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        iconGroup = (xGroup*)b;
    }

    mIconTypes = (zUI**)xMemAlloc(gActiveHeap, 6 * sizeof(zUI*), 0);
    for (i = 0; i < 6; i++)
    {
        mIconTypes[i] = (zUI*)xGroupGetItemPtr(iconGroup, i);
    }

    baseIcon = mIconTypes[0];
    mOffsetX = baseIcon->asset->x;
    mOffsetY = baseIcon->asset->y;

    b = zSceneFindObject(0x3A9A636B);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        mAuraGroup = (xGroup*)b;
    }

    mNumRows = xGroupGetCount(gridGroup);
    for (i = 0; i < mNumRows; i++)
    {
        rowGroup = (xGroup*)xGroupGetItemPtr(gridGroup, i);
        size = xGroupGetCount(rowGroup);
        if (i == 0)
        {
            mNumColumns = size;
            mGrid = (zUI**)xMemAlloc(gActiveHeap, mNumRows * sizeof(zUI*) * mNumColumns, 0);
        }

        for (j = 0; j < size; j++)
        {
            // Not in the PS2 DWARF (index, custom): retail computes the grid index once, before
            // xGroupGetItemPtr, and keeps the icon pointer like a named local declared after j.
            U32 index = j + (U8)i * mNumColumns;
            mGrid[index] = (zUI*)xGroupGetItemPtr(rowGroup, j);
            zUICustomExtrasIcon* custom = (zUICustomExtrasIcon*)mGrid[index]->custom;
            custom->InitModel((zUIModel*)mIconTypes[sExtrasList[index].type],
                              (zUIModel*)mIconTypes[0], mOffsetX, mOffsetY);
        }
    }

    b = zSceneFindObject(0xC4CA8175);
    if (b != NULL && b->baseType == eBaseTypeUIText)
    {
        mDescription = (zUIText*)b;
    }

    b = zSceneFindObject(0x4732EF88);
    if (b != NULL && b->baseType == eBaseTypeUIText)
    {
        mActionText = (zUIText*)b;
    }

    b = zSceneFindObject(0xF371CBFC);
    if (b != NULL && b->baseType == eBaseTypeUIText)
    {
        mNextExtraText = (zUIText*)b;
    }

    b = zSceneFindObject(0x62DA6901);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        mNextExtraGroup = (xGroup*)b;
    }

    b = zSceneFindObject(0xEABBEB32);
    if (b != NULL && b->baseType == eBaseTypeSoundFX)
    {
        mSoundMove = (xSoundFX*)b;
    }

    b = zSceneFindObject(0x2C1DF711);
    if (b != NULL && b->baseType == eBaseTypeSoundFX)
    {
        mSoundDeny = (xSoundFX*)b;
    }

    b = zSceneFindObject(0xF542B861);
    if (b != NULL && b->baseType == eBaseTypeSoundFX)
    {
        mSoundConfirm = (xSoundFX*)b;
    }
}

void zUICustomExtrasManager::EnterScreen()
{
    U32 i;
    U32 numExtras;
    zUI* placeholder;
    U32 numItems;
    U32 j;
    zUI* uiObject;
    F32 x;
    F32 y;
    zUICustomExtrasIcon* custom;

    numExtras = MIN(sNumExtras, mNumRows * mNumColumns);

    for (i = 0; i < numExtras; i++)
    {
        if (sExtrasList[i].chestsNeeded > globals.player.InvTreasureChestTotal)
        {
            break;
        }

        placeholder = mGrid[i];
        custom = (zUICustomExtrasIcon*)placeholder->custom;
        if (sExtrasList[i].type != 0 && !custom->misUnlocked)
        {
            // Not a DWARF local: the target keeps the icon's model widget in a register for
            // both stores (name as in zUICustomExtrasIcon::InitModel).
            zUIModel* uiModel = (zUIModel*)custom->ui;
            uiModel->model = custom->munlockedModel;
            uiModel->atbl = custom->munlockedAtbl;

            x = custom->munlockedOffsetX;
            y = custom->munlockedOffsetY;
            custom->ui->startMovement.x += x - custom->mOffsetX;
            custom->ui->startMovement.y += y - custom->mOffsetY;
            custom->mOffsetX = x;
            custom->mOffsetY = y;

            x = custom->munlockedWidth;
            y = custom->munlockedHeight;
            if (x)
            {
                custom->ui->startMovement.width = x;
            }
            if (y)
            {
                custom->ui->startMovement.height = y;
            }
            custom->mWidth = x;
            custom->mHeight = y;

            custom->misUnlocked = TRUE;
        }

        if (sExtrasList[i].chestsNeeded > globals.player.InvTreasureChestOpened)
        {
            zEntEvent(placeholder, eEventUISelect);
        }
        else
        {
            zEntEvent(placeholder, eEventUIUnselect);
        }

        ((zUICustomExtrasIcon*)placeholder->custom)->mbIsHidden = FALSE;

        if (sExtrasList[i].status != 0 && (sExtrasList[i].type == 4 || sExtrasList[i].type == 5))
        {
            zEntEvent(mGrid[i], eEventAnimPlayLoop, 2.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    if (i < numExtras)
    {
        sNumChestsNeeded = sExtrasList[i].chestsNeeded;
        ((zUICustomExtrasIcon*)mGrid[i]->custom)->mbIsHidden = TRUE;
    }

    numItems = xGroupGetCount(mNextExtraGroup);
    for (j = 0; j < numItems; j++)
    {
        uiObject = (zUI*)xGroupGetItemPtr(mNextExtraGroup, j);
        if (i < numExtras)
        {
            x = mGrid[i]->asset->x - mIconTypes[0]->asset->x;
            y = mGrid[i]->asset->y - mIconTypes[0]->asset->y;
        }
        else
        {
            x = -1000.0f;
            y = -1000.0f;
        }

        custom = (zUICustomExtrasIcon*)uiObject->custom;
        custom->ui->startMovement.x += x - custom->mOffsetX;
        custom->ui->startMovement.y += y - custom->mOffsetY;
        custom->mOffsetX = x;
        custom->mOffsetY = y;
        uiObject->ResetMotion();
    }

    if (mbInSubScreen)
    {
        mbInSubScreen = FALSE;
    }
    else
    {
        ResetAura();
    }

    ((zPlayerSB*)xglobals->___player_ent_dont_use_directly)->GetTreasureChestHud()->show(false);
}

void zUICustomExtrasManager::ResetAura()
{
    U32 size;
    U32 i;
    F32 x;
    F32 y;
    zUICustomExtrasIcon* custom;

    size = xGroupGetCount(mAuraGroup);
    for (i = 0; i < size; i++)
    {
        custom = (zUICustomExtrasIcon*)((zUI*)xGroupGetItemPtr(mAuraGroup, i))->custom;
        custom->mbFullReset = TRUE;
        custom->ui->ResetMotion();
        custom->mbFullReset = FALSE;
        custom->mOffsetX = custom->mOffsetY = 0.0f;

        x = 0.0f;
        y = 0.0f;
        custom->ui->startMovement.x += x - custom->mOffsetX;
        custom->ui->startMovement.y += y - custom->mOffsetY;
        custom->mOffsetX = x;
        custom->mOffsetY = y;
    }

    MoveSelection(0, 0, false);
}

void zUICustomExtrasManager::MoveSelection(U8 row, U8 column, bool playSound)
{
    U8 badLocation;
    U32 gridIndex;
    U32 size;
    U32 i;
    zUI* aura;
    F32 x;
    F32 y;
    zUICustomExtrasIcon* custom;

    badLocation = FALSE;
    if (row >= mNumRows || column >= mNumColumns)
    {
        badLocation = TRUE;
    }

    if (badLocation)
    {
        if (playSound && mSoundDeny != NULL)
        {
            zEntEvent(mSoundDeny, eEventPlay);
        }
    }
    else
    {
        gridIndex = column + row * mNumColumns;
        mCurRow = row;
        mCurColumn = column;

        size = xGroupGetCount(mAuraGroup);
        for (i = 0; i < size; i++)
        {
            aura = (zUI*)xGroupGetItemPtr(mAuraGroup, i);
            custom = (zUICustomExtrasIcon*)aura->custom;

            x = mGrid[gridIndex]->asset->x - mIconTypes[0]->asset->x;
            y = mGrid[gridIndex]->asset->y - mIconTypes[0]->asset->y;
            custom->ui->startMovement.x += x - custom->mOffsetX;
            custom->ui->startMovement.y += y - custom->mOffsetY;
            custom->mOffsetX = x;
            custom->mOffsetY = y;
        }

        UpdateText();

        if (playSound && mSoundMove != NULL)
        {
            zEntEvent(mSoundMove, eEventPlay);
        }
    }
}

void zUICustomExtrasManager::UpdateText()
{
    // Not a DWARF local: the target computes the grid index once and keeps it across the
    // xStrHash/xTextFindString calls.
    U32 gridIndex;
    U8 isLocked;
    char* descriptionText;
    U32 type;
    char* actionText;

    gridIndex = mCurColumn + mCurRow * mNumColumns;
    isLocked = sExtrasList[gridIndex].chestsNeeded > globals.player.InvTreasureChestTotal;

    if (mDescription != NULL)
    {
        if (isLocked)
        {
            descriptionText = xTextFindString(xStrHash("EXTRAS DESCRIPTION LOCKED"), NULL);
        }
        else
        {
            char descriptionString[25] = "EXTRAS DESCRIPTION 01-01";

            descriptionString[19] = sExtrasList[gridIndex].type / 10 + '0';
            descriptionString[20] = sExtrasList[gridIndex].type % 10 + '0';
            descriptionString[22] = sExtrasList[gridIndex].id / 10 + '0';
            descriptionString[23] = sExtrasList[gridIndex].id % 10 + '0';
            descriptionText = xTextFindString(xStrHash(descriptionString), NULL);
        }

        mDescription->text = descriptionText;
    }

    if (mActionText != NULL)
    {
        char actionString[26] = "EXTRAS TYPE ACTION 01 OFF";

        if (isLocked)
        {
            type = 0;
        }
        else
        {
            type = sExtrasList[gridIndex].type;
        }

        actionString[19] = type / 10 + '0';
        actionString[20] = type % 10 + '0';
        if (sExtrasList[gridIndex].status != 0)
        {
            actionString[23] = 'N';
            actionString[24] = '\0';
        }

        actionText = xTextFindString(xStrHash(actionString), NULL);
        mActionText->text = actionText;
    }
}

void zUICustomExtrasManager::SelectCurrentIcon()
{
    // Not a DWARF local: the target computes the grid index once and keeps it across the
    // event calls.
    U32 gridIndex = mCurColumn + mCurRow * mNumColumns;
    extraEntry& currentIcon = sExtrasList[gridIndex];

    if (currentIcon.chestsNeeded > globals.player.InvTreasureChestTotal)
    {
        if (mSoundDeny != NULL)
        {
            zEntEvent(mSoundDeny, eEventPlay);
        }
    }
    else
    {
        if (mSoundConfirm != NULL)
        {
            zEntEvent(mSoundConfirm, eEventPlay);
        }

        switch (currentIcon.type)
        {
        case 1:
            StopChatter();
            zMenuFMVPlay(zFMVFileGetName(sExtrasMovieList[currentIcon.id - 1]), 0x90001, 0.1f, true,
                         true);
            {
                xColor_tag clearColor = { 0x32, 0x0C, 0x0C, 0xFF };
                iCameraClear(globals.screen->icam, clearColor);
            }
            iCameraShowRaster(globals.screen->icam);
            break;
        case 2:
            mbInSubScreen = TRUE;
            ((zUICustomExtrasSound*)mSubScreens[2])->mExtraID = currentIcon.id;
            zEntEvent(ui, eEventUIFocusOff_Unselect);
            zEntEvent(mSubScreens[2]->ui, eEventUIFocusOn_Select);
            break;
        case 3:
            mbInSubScreen = TRUE;
            ((zUICustomExtrasArt*)mSubScreens[3])->mExtraID = currentIcon.id;
            zEntEvent(ui, eEventUIFocusOff_Unselect);
            zEntEvent(mSubScreens[3]->ui, eEventUIFocusOn_Select);
            break;
        case 4:
            currentIcon.status = 1 - currentIcon.status;
            if (currentIcon.status != 0)
            {
                ClearAllButThisCostume(currentIcon.id);
                zEntEvent(mGrid[gridIndex], eEventAnimPlayLoop, 2.0f, 0.0f, 0.0f, 0.0f);
            }
            else
            {
                zEntEvent(mGrid[gridIndex], eEventAnimStop);
            }
            UpdateText();
            break;
        case 5:
            currentIcon.status = 1 - currentIcon.status;
            if (currentIcon.status != 0)
            {
                zEntEvent(mGrid[gridIndex], eEventAnimPlayLoop, 2.0f, 0.0f, 0.0f, 0.0f);
            }
            else
            {
                zEntEvent(mGrid[gridIndex], eEventAnimStop);
            }
            UpdateText();
            break;
        }
    }
}

U8 zUICustomExtrasManager::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                       xBase* toParamWidget, U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventSceneEnter:
        UpgradeSparkleEffect::powerupText[0] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 01 KS UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[1] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 02 BA UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[2] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 03 SB UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[3] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 04 SW UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[4] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 06 BB UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[5] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 07 CW UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[6] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 08 BF UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[7] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 09 TH UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[8] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 05 HS UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::powerupText[9] =
            (zUITextAsset*)xSTFindAsset(xStrHash("POWERUP 10 HP UIF CONFIRM"), NULL);
        UpgradeSparkleEffect::upgradeSoundGroupHandle =
            xSndMgrGetSoundGroup(xStrHash("CHEAT_SUCCESS"));
        InitScreen();
        bNoChatter = TRUE;
        return 1;
    case eEventUIFocusOn_Select:
        EnterScreen();
        return 1;
    case eEventUIMotionFinished:
        UpdateText();
        return 1;
    case eEventPadPressUp:
        MoveSelection(mCurRow - 1, mCurColumn, true);
        return 0;
    case eEventPadPressDown:
        MoveSelection(mCurRow + 1, mCurColumn, true);
        return 0;
    case eEventPadPressLeft:
        MoveSelection(mCurRow, mCurColumn - 1, true);
        return 0;
    case eEventPadPressRight:
        MoveSelection(mCurRow, mCurColumn + 1, true);
        return 0;
    case eEventPadPressX:
        SelectCurrentIcon();
        return 0;
    case eEventPadPressTriangle:
        MoveSelection(0, 0, false);
        globals.player.InvTreasureChestOpened = globals.player.InvTreasureChestTotal;
        ((zPlayerSB*)xglobals->___player_ent_dont_use_directly)->GetTreasureChestHud()->hide();
        bNoChatter = FALSE;
        return 0;
    }

    return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

void zUICustomExtrasManager::PostUpdate(F32 dt)
{
    U32 i;

    for (i = 0; i < 6; i++)
    {
        mIconTypes[i]->Update(dt, 0);
    }
}

zUICustomExtrasIcon::zUICustomExtrasIcon(zUI* ui)
    : zUICustom(ui), mOffsetX(0.0f), mOffsetY(0.0f), mWidth(0.0f), mHeight(0.0f), mbFullReset(0),
      misUnlocked(1), mbIsHidden(0)
{
}

void zUICustomExtrasIcon::InitModel(zUIModel* model, zUIModel* placeholder, F32 globalOffsetX,
                                    F32 globalOffsetY)
{
    zUIModel* uiModel;
    zUIModelAsset* uiModelAsset;
    zUIModelAsset* modelAsset;

    uiModel = (zUIModel*)ui;
    uiModelAsset = (zUIModelAsset*)uiModel->asset;
    modelAsset = (zUIModelAsset*)model->asset;
    uiModelAsset->model = modelAsset->model;
    uiModelAsset->animationList = modelAsset->animationList;
    uiModelAsset->surface = modelAsset->surface;
    uiModel->InitModel();

    munlockedModel = uiModel->model;
    munlockedAtbl = uiModel->atbl;
    munlockedOffsetX = modelAsset->x - globalOffsetX;
    munlockedOffsetY = modelAsset->y - globalOffsetY;
    munlockedWidth = modelAsset->width;
    munlockedHeight = modelAsset->height;

    uiModel->model = placeholder->model;
    uiModel->atbl = placeholder->atbl;
    misUnlocked = FALSE;
}

void zUICustomExtrasIcon::ResetMotion()
{
    if (!mbFullReset)
    {
        zUICustomExtrasIcon::PostInitMotion();
    }

    if (ui->selected)
    {
        ui->RunMotion(ui->selectedMotion, true, false);
    }
}

void zUICustomExtrasIcon::PostInitMotion()
{
    ui->current.x += mOffsetX;
    ui->current.y += mOffsetY;
}

void zUICustomExtrasIcon::PreUpdate(F32 dt)
{
    zUIModel* uimodel;

    if (!misUnlocked && ui->baseType == eBaseTypeUIModel)
    {
        uimodel = (zUIModel*)ui;
        tempModel = uimodel->model;
        uimodel->model = NULL;
    }
}

void zUICustomExtrasIcon::PostUpdate(F32 dt)
{
    if (!misUnlocked && ui->baseType == eBaseTypeUIModel)
    {
        ((zUIModel*)ui)->model = tempModel;
    }
}

U8 zUICustomExtrasIcon::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                    xBase* toParamWidget, U32 toParamWidgetID)
{
    U8 select_status;

    if (toEvent == eEventUIReset)
    {
        select_status = ui->selected;
        ui->Reset();
        ui->selected = select_status;
        return 0;
    }
    else if (toEvent == eEventVisible)
    {
        if (mbIsHidden)
        {
            return 0;
        }

        return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
    }

    return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

U8 zUICustomExtrasSound::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                     xBase* toParamWidget, U32 toParamWidgetID)
{
    xGroup* soundList;

    switch (toEvent)
    {
    case eEventSceneEnter:
        InitScreen();
        bNoChatter = FALSE;
        return 1;
    case eEventUIFocusOn_Select:
        EnterScreen();
        bNoChatter = TRUE;
        return 1;
    // newSound is not a PS2 DWARF local, but both builds keep the new index in a register across
    // the zEntEvent call instead of reloading mCurrentSound.
    case eEventPadPressUp:
        if (mCurrentSound != 0)
        {
            U8 newSound = mCurrentSound - 1;
            mCurrentSound = newSound;
            zEntEvent(mTextGroup, eEventUIUnselect);
            zEntEvent(xGroupGetItemPtr(mTextGroup, newSound), eEventUISelect);
            if (mSoundMove != NULL)
            {
                zEntEvent(mSoundMove, eEventPlay);
            }
        }
        else if (mSoundDeny != NULL)
        {
            zEntEvent(mSoundDeny, eEventPlay);
        }
        return 0;
    case eEventPadPressDown:
        if (mCurrentSound < mNumSounds - 1)
        {
            U8 newSound = mCurrentSound + 1;
            mCurrentSound = newSound;
            zEntEvent(mTextGroup, eEventUIUnselect);
            zEntEvent(xGroupGetItemPtr(mTextGroup, newSound), eEventUISelect);
            if (mSoundMove != NULL)
            {
                zEntEvent(mSoundMove, eEventPlay);
            }
        }
        else if (mSoundDeny != NULL)
        {
            zEntEvent(mSoundDeny, eEventPlay);
        }
        return 0;
    case eEventPadPressX:
        StopChatter();
        if (mCurrentSoundFX != NULL)
        {
            zEntEvent(mCurrentSoundFX, eEventStop);
        }
        mCurrentSoundFX = NULL;
        if (zMenuSB_EnsureFreeAudioStream(true))
        {
            soundList = (xGroup*)xGroupGetItemPtr(mSoundGroup, mExtraID - 1);
            mCurrentSoundFX = (xSoundFX*)xGroupGetItemPtr(soundList, mCurrentSound);
            zEntEvent(mCurrentSoundFX, eEventPlay);
        }
        return 0;
    case eEventPadPressTriangle:
        if (mCurrentSoundFX != NULL)
        {
            zEntEvent(mCurrentSoundFX, eEventStop);
        }
        zEntEvent(mTextGroup, eEventUIUnselect);
        bNoChatter = FALSE;
        return 1;
    }

    return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

// No GC symbol: auto-inlined into HandleEvent (the PS2 demo still has it out of line).
void zUICustomExtrasSound::InitScreen()
{
    xBase* b;

    b = zSceneFindObject(0xAC9A6C39);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        mSoundGroup = (xGroup*)b;
    }

    b = zSceneFindObject(0xAFEE33A2);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        mTextGroup = (xGroup*)b;
    }

    b = zSceneFindObject(0xEABBEB32);
    if (b != NULL && b->baseType == eBaseTypeSoundFX)
    {
        mSoundMove = (xSoundFX*)b;
    }

    b = zSceneFindObject(0x2C1DF711);
    if (b != NULL && b->baseType == eBaseTypeSoundFX)
    {
        mSoundDeny = (xSoundFX*)b;
    }
}

// No GC symbol: auto-inlined into HandleEvent (the PS2 demo still has it out of line).
void zUICustomExtrasSound::EnterScreen()
{
    xGroup* soundList = (xGroup*)xGroupGetItemPtr(mSoundGroup, mExtraID - 1);
    mNumSounds = xGroupGetCount(soundList);
    U32 numText = xGroupGetCount(mTextGroup);

    char soundTextName[19] = "EXTRAS SOUND 00-00";
    soundTextName[13] = mExtraID / 10 + '0';
    soundTextName[14] = mExtraID % 10 + '0';

    for (U32 i = 0; i < numText; i++)
    {
        zUIText* uiText = (zUIText*)xGroupGetItemPtr(mTextGroup, i);
        if (i < mNumSounds)
        {
            soundTextName[16] = (i + 1) / 10 + '0';
            soundTextName[17] = (i + 1) % 10 + '0';
            char* descriptionText = xTextFindString(xStrHash(soundTextName), NULL);
            uiText->text = descriptionText;
            zEntEvent(uiText, eEventEnable);
        }
        else
        {
            zEntEvent(uiText, eEventDisable);
        }
    }

    mCurrentSound = 0;
    zEntEvent(mTextGroup, eEventUIUnselect);
    zEntEvent(xGroupGetItemPtr(mTextGroup, 0), eEventUISelect);
}

// No symbol on GC or PS2 (always inlined): shows the description of the current image
// ("EXTRAS ART <extra>-<image>").
inline void zUICustomExtrasArt::UpdateDescription()
{
    if (mDescription != NULL)
    {
        char descriptionString[17] = "EXTRAS ART 01-01";
        U32 imageNumber;

        descriptionString[11] = mExtraID / 10 + '0';
        descriptionString[12] = mExtraID % 10 + '0';
        imageNumber = ((zUICustomImageSlideshow*)mImage->custom)->m_CurrentImage + 1;
        descriptionString[14] = imageNumber / 10 + '0';
        descriptionString[15] = imageNumber % 10 + '0';
        mDescription->text = xTextFindString(xStrHash(descriptionString), NULL);
    }
}

// No symbol on GC or PS2 (always inlined): shows the first image of the selected extra.
inline void zUICustomExtrasArt::EnterScreen()
{
    xGroup* imageList = (xGroup*)xGroupGetItemPtr(mImageGroup, mExtraID - 1);
    U32 numImages = xGroupGetCount(imageList);
    zUICustomImageSlideshow* slideshow = (zUICustomImageSlideshow*)mImage->custom;

    slideshow->SetTextureList(imageList);
    slideshow->UpdateTexture();
    UpdateDescription();
    mImageTime = iTimeGet();
}

U8 zUICustomExtrasArt::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                   xBase* toParamWidget, U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventSceneEnter:
        InitScreen();
        return 1;
    case eEventUIFocusOn_Select:
        EnterScreen();
        return 1;
    case eEventPadPressSquare:
    {
        iTime curTime = iTimeGet();

        if (iTimeDiffSec(mImageTime, curTime) > 0.5f)
        {
            mImageTime = curTime;
            zEntEvent(mImage, eEventUISignalUp);
            zEntEvent(mImage, eEventUIChangeTexture);
            UpdateDescription();
            if (mSoundMove != NULL)
            {
                zEntEvent(mSoundMove, eEventPlay);
            }
        }
        return 0;
    }
    case eEventPadPressX:
    {
        iTime curTime = iTimeGet();

        if (iTimeDiffSec(mImageTime, curTime) > 0.5f)
        {
            mImageTime = curTime;
            zEntEvent(mImage, eEventUISignalDown);
            zEntEvent(mImage, eEventUIChangeTexture);
            UpdateDescription();
            if (mSoundMove != NULL)
            {
                zEntEvent(mSoundMove, eEventPlay);
            }
        }
        return 0;
    }
    }

    return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

void zUICustomExtrasArt::InitScreen()
{
    xBase* b;

    b = zSceneFindObject(0x4AC6CE9A);
    if (b != NULL && b->baseType == eBaseTypeGroup)
    {
        mImageGroup = (xGroup*)b;
    }

    b = zSceneFindObject(0xC1D950EF);
    if (b != NULL && b->baseType == eBaseTypeUIImage)
    {
        mImage = (zUIImage*)b;
    }

    b = zSceneFindObject(0x562DB998);
    if (b != NULL && b->baseType == eBaseTypeUIText)
    {
        mDescription = (zUIText*)b;
    }

    b = zSceneFindObject(0xEABBEB32);
    if (b != NULL && b->baseType == eBaseTypeSoundFX)
    {
        mSoundMove = (xSoundFX*)b;
    }
}

zUICustomLoadingText::zUICustomLoadingText(zUI* ui) : zUICustom(ui)
{
}

U8 zUICustomLoadingText::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                     xBase* toParamWidget, U32 toParamWidgetID)
{
    char* levelName;

    if (toEvent != eEventVisible)
    {
        return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
    }

    levelName = (char*)zSceneGetLevelName(sNextSceneID);
    ((zUIText*)ui)->text = levelName;
    return 1;
}

// Dead-stripped placeholder: retail pools 1.5f here, from stripped code between
// zUICustomLoadingText::HandleEvent and PlayUpgradeFMV. Body unknown.
static void __deadstripped2_zMenuSB(F32* out)
{
    out[0] = 1.5f;
}

void UpgradeSparkleEffect::PlayUpgradeFMV(S32 powerupIdx)
{
    if (powerupIdx < 0 || powerupIdx >= 10)
    {
        return;
    }

    StopChatter();
    PlayPowerUpMovie(powerupIdx);
    if (zMenuSB_EnsureFreeAudioStream(true))
    {
        zEntPlayer_SNDPlayUpgradeVO(powerupIdx);
    }
}

void UpgradeSparkleEffect::Start(xVec3& start, xVec3& end, F32 timeForEffect, F32 randomYMultiplier,
                                 F32 speedMultiplier, F32 probabilityToSpawn)
{
    totalTimeForEffect = timeForEffect;
    sparkleTime = 0.0f;
    sparkleStart = start;
    sparkleEnd = end;
    randomYOffset = randomYMultiplier;
    probability = probabilityToSpawn;
    multiplier = speedMultiplier;
    effectStarted = TRUE;
}

// The camera axes are scaled component by component: xVec3SMul would make the scale an inline
// parameter and permute the float registers.
void UpgradeSparkleEffect::Update(F32 dt)
{
    RwCamera* currentCamera;
    RwMatrix* mat;
    xVec3 pos[2];
    xVec3 vel[2];
    S32 i;
    F32 scale;
    F32 fRand;

    if (sparkleTime > totalTimeForEffect)
    {
        if (effectStarted)
        {
            if (fmvDelay < 0.0f)
            {
                fmvDelay = 0.2f;
            }
            else
            {
                fmvDelay -= dt;
                if (fmvDelay < 0.0f)
                {
                    PlayUpgradeFMV(powerupIndex);
                    effectStarted = FALSE;
                    powerupIndex = -1;
                    fmvDelay = -1.0f;
                    return;
                }
            }
        }

        return;
    }

    currentCamera = RwCameraGetCurrentCamera();
    if (currentCamera == NULL)
    {
        return;
    }

    mat = RwFrameGetMatrix(RwCameraGetFrame(currentCamera));
    if (xurand() >= probability)
    {
        for (i = 0; i < sizeof(pos) / sizeof(pos[0]); i++)
        {
            // Not in the DWARF: single-assignment values the PS2 build kept as compiler
            // temporaries (its DWARF lists only scale and fRand); retail keeps both in registers.
            F32 startX;
            F32 startY;
            // Not in the DWARF: the scaled camera axis added to pos[i].
            xVec3 offset;

            pos[i].x = mat->pos.x;
            pos[i].y = mat->pos.y;
            pos[i].z = mat->pos.z;
            memset(&vel[i], 0, sizeof(xVec3));

            // scale first holds the screen-space end x: retail computes it before startX and
            // does not fuse it into the subtraction, as a re-assigned variable does.
#ifdef PLATFORM_PC
            // PC: the menu is in the 4:3 UI box, which is this share of the view
            // window (iScreen.h). The same at 4:3.
            scale = 2.0f * currentCamera->viewWindow.x * iScreenUIFracXF() *
                    (1.0f - sparkleScale.z * (sparkleEnd.x / 640.0f));
            startX = 2.0f * currentCamera->viewWindow.x * iScreenUIFracXF() *
                     (1.0f - sparkleScale.z * (sparkleStart.x / 640.0f));
            startY = 2.0f * currentCamera->viewWindow.y * iScreenUIFracYF() *
                     (1.0f - sparkleScale.z * (sparkleStart.y / 480.0f));
#else
            scale = 2.0f * currentCamera->viewWindow.x *
                    (1.0f - sparkleScale.z * (sparkleEnd.x / 640.0f));
            startX = 2.0f * currentCamera->viewWindow.x *
                     (1.0f - sparkleScale.z * (sparkleStart.x / 640.0f));
            startY = 2.0f * currentCamera->viewWindow.y *
                     (1.0f - sparkleScale.z * (sparkleStart.y / 480.0f));
#endif
            vel[i].x = (scale - startX) / totalTimeForEffect;

            scale = sparkleScale.x * (sparkleTime * vel[i].x + startX);
            offset.x = mat->right.x * scale;
            offset.y = mat->right.y * scale;
            offset.z = mat->right.z * scale;
            xVec3AddTo(&pos[i], &offset);

            fRand = randomYOffset * xurand();
            fRand *= (S32)(fRand * 1000.0f) % 2 ? -1.0f : 1.0f;
            fRand = sparkleScale.y * startY + fRand;
            offset.x = mat->up.x * fRand;
            offset.y = mat->up.y * fRand;
            offset.z = mat->up.z * fRand;
            xVec3AddTo(&pos[i], &offset);

            offset.x = mat->at.x * sparkleScale.z;
            offset.y = mat->at.y * sparkleScale.z;
            offset.z = mat->at.z * sparkleScale.z;
            xVec3AddTo(&pos[i], &offset);

            sparkleTime += dt * multiplier;
        }

        zParPTankSpawnEffectsParticles(pos, vel, sizeof(pos) / sizeof(pos[0]));
    }
}

zUICustomUpgradeTutorialContoller::zUICustomUpgradeTutorialContoller(zUI* ui) : zUICustom(ui)
{
    upgradeTutorialTalkBox = NULL;
    upgradeTutorialID = ui->asset->customWidget;
    ui->visible = TRUE;
}

void zUICustomUpgradeTutorialContoller::Reset()
{
    ui->visible = TRUE;
}

void zUICustomUpgradeTutorialContoller::PreUpdate(F32 dt)
{
    if (upgradeTutorialTalkBox != NULL && upgradeTutorialTalkBox->flag.visible)
    {
        if (globals.player.ControlOff || zHud::ModelAndFontHideOnTextBox::IsIncludedTextboxActive())
        {
            upgradeTutorialTalkBox->hide();
        }
    }
}

U8 zUICustomUpgradeTutorialContoller::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                                  xBase* toParamWidget, U32 toParamWidgetID)
{
    ztalkbox* activeTalkbox;
    U8 textBox;

    switch (toEvent)
    {
    case eEventSceneBegin:
        upgradeTutorialTalkBox = (ztalkbox*)zSceneFindObject(upgradeTutorialID);
        autoSave = (zUIText*)zSceneFindObject(xStrHash("AUTO_SAVE"));
        if (autoSave == NULL)
        {
            // Left over from a stripped debug message: the retail build still hashes the name.
            xStrHash("AUTO_SAVE");
        }
        break;
    case eEventUISignalActivateScreen:
        activeTalkbox = ztalkbox::get_active();
        textBox = zHud::ModelAndFontHideOnTextBox::IsIncludedTextboxActive();
        if (activeTalkbox == NULL && !textBox && !autoSave->visible &&
            upgradeTutorialTalkBox != NULL && !globals.player.ControlOff)
        {
            upgradeTutorialTalkBox->set_text("{i:TUTORIAL FIRST UPGRADE POINT TXT}");
            upgradeTutorialTalkBox->show();
            zEntEvent(upgradeTutorialTalkBox, eEventStartConversation);
        }
        break;
    case eEventUISignalDeactivateScreen:
        if (upgradeTutorialTalkBox != NULL)
        {
            zEntEvent(upgradeTutorialTalkBox, eEventEndConversation);
        }
        break;
    default:
        return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
    }

    return 1;
}

zUICustomDialogPlayer::zUICustomDialogPlayer(zUI* ui) : zUICustom(ui)
{
}

U8 zUICustomDialogPlayer::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                      xBase* toParamWidget, U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventSceneBegin:
        sCurrentChatter = NULL;
        return 1;
    case eEventUISignalEffect:
        if (bNoChatter)
        {
            return 1;
        }

        if (sCurrentChatter != NULL && xSndMgrIsPlaying(sCurrentChatter->sndHandle))
        {
            return 1;
        }

        if (zMenuSB_EnsureFreeAudioStream(true))
        {
            zEntEvent(toParamWidget, eEventPlay);
            sCurrentChatter = (xSoundFX*)toParamWidget;
        }
        return 1;
    case eEventEnable:
        if (sCurrentChatter != NULL)
        {
            zEntEvent(sCurrentChatter, eEventStop);
            sCurrentChatter = NULL;
        }
        return 1;
    }

    return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

zUICustomPalRedirector::zUICustomPalRedirector(zUI* ui) : zUICustom(ui)
{
}

U8 zUICustomPalRedirector::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                       xBase* toParamWidget, U32 toParamWidgetID)
{
    zUI* palUI;

    if (gVidMode == eGameVidModePAL)
    {
        palUI = (zUI*)zSceneFindObject(ui->asset->customWidget);
        if (palUI->custom == NULL ||
            palUI->custom->HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID))
        {
            palUI->DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
        }
        return 0;
    }

    return zUICustom::HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

// The retail build inlines this into all of its callers (SelectCurrentIcon and the zGameExtras
// cheat callbacks) and dead-strips the out-of-line copy.
inline void ClearAllButThisCostume(U32 index)
{
    eSNDCurrentPlayer playerID = sExtraCostumeList[index].player;
    U32 i;
    U8 curID;
    U32 j;

    for (i = 0; i < sNumExtras; i++)
    {
        curID = sExtrasList[i].id;
        if (sExtrasList[i].type == 4 && curID != index &&
            sExtraCostumeList[curID - 1].player == playerID)
        {
            sExtrasList[i].status = 0;
        }
    }

    for (j = 4; j < sExtraCostumeListSize; j++)
    {
        if (sExtraCostumeList[j].player == playerID && j != index)
        {
            g_flg_chEnabled &= ~sExtraCostumeList[j].gameExtrasFlag;
        }
    }
}

// Enables the cheat costume whose gameExtrasFlag is costumeFlag by switching off every other
// costume of the same player (not in the PS2 DWARF: inlined into every caller).
inline void EnableCheatCostume(U32 costumeFlag)
{
    if (g_flg_chEnabled & costumeFlag)
    {
        for (U32 i = 4; i < sExtraCostumeListSize; i++)
        {
            if (sExtraCostumeList[i].gameExtrasFlag == costumeFlag)
            {
                ClearAllButThisCostume(i);
                break;
            }
        }
    }
}

// Dead-stripped placeholder: retail pools 3.0f and 100000.0f (xsqrt constants) here, from stripped
// code ahead of this file's first surviving function. Body unknown.
static void __deadstripped_zMenuSB(F32* out)
{
    out[0] = 3.0f;
    out[1] = 100000.0f;
}
