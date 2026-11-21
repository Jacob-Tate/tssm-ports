#include "zGameExtras.h"
#include "zMenuSB.h"

static void EGG_update_TEST(F32 dt, EGGItem* egg);
static void EGG_init_TEST(EGGItem* egg);
static void EGG_reset_TEST(EGGItem* egg);
static void EGG_done_TEST(EGGItem* egg);
static void EGG_update_BDay(F32 dt, EGGItem* egg);
static void EGG_init_BDay(EGGItem* egg);
static void EGG_reset_BDay(EGGItem* egg);
static void EGG_done_BDay(EGGItem* egg);
static void ChaChingCB();
static void MaxHealthSlotsCB();
static void AllMovesCB();
static void AllUpgradedMovesCB();
static void AllTasksCB();
static void TargetCostumeCBSBCaveman();
static void TargetCostumeCBSBRippedShorts();
static void TargetCostumeCBPatGoofyGoober();
static void TargetCostumeCBPatCaveman();

// zMenuSB.cpp
inline void EnableCheatCostume(U32 costumeFlag);

static S32 g_enableGameExtras;
static S32 g_currDay;
static S32 g_currMonth;
static S32 g_gameExtraFlags;
static EGGItemFuncs EGGModuleTest = { EGG_update_TEST, EGG_init_TEST, EGG_reset_TEST,
                                      EGG_done_TEST };
static EGGItemFuncs EGGBirthDay = { EGG_update_BDay, EGG_init_BDay, EGG_reset_BDay, EGG_done_BDay };
static EGGItemFuncs EGGSnow = {};
static EGGItemFuncs EGGRain = {};
static EGGItemFuncs EGGEmpty = {};
static EGGItem g_eggBasket[2] = { { EGG_check_ExtrasFlags, &EGGEmpty, 0, 0 }, {} };

S32 zGameExtras_ExtrasFlags()
{
    return g_gameExtraFlags;
}

void zGameExtras_MoDay(S32* month, S32* day)
{
    *month = g_currMonth;
    *day = g_currDay;
}

inline void zGameExtras_SceneInit()
{
    g_enableGameExtras = 0;
    g_currDay = iGetDay();
    g_currMonth = iGetMonth();
    S32 somethingIsEnabled = 0;

    EGGItem* egg_next = g_eggBasket;

    while (egg_next->fun_check)
    {
        EGGItem* egg = egg_next++;

        egg->enabled = egg->fun_check(egg);

        if (egg->enabled)
        {
            somethingIsEnabled++;

            if (egg->funcs->fun_init)
            {
                egg->funcs->fun_init(egg);
            }
        }
    }

    if (somethingIsEnabled)
    {
        g_enableGameExtras = 1;
    }
}

void zGameExtras_SceneReset()
{
    if (!g_enableGameExtras)
    {
        return;
    }

    EGGItem* egg_next = g_eggBasket;

    while (egg_next->fun_check)
    {
        EGGItem* egg = egg_next++;

        if (egg->enabled)
        {
            if (egg->funcs->fun_reset)
            {
                egg->funcs->fun_reset(egg);
            }
        }
    }
}

inline void zGameExtras_SceneExit()
{
    if (!g_enableGameExtras)
    {
        return;
    }

    if (globals.cmgr)
    {
        return;
    }

    if (zGameIsPaused())
    {
        return;
    }

    EGGItem* egg_next = g_eggBasket;

    while (egg_next->fun_check)
    {
        EGGItem* egg = egg_next++;

        if (egg->enabled)
        {
            egg->enabled = 0;

            if (egg->funcs->fun_done)
            {
                egg->funcs->fun_done(egg);
            }
        }
    }

    g_enableGameExtras = 0;
    g_currDay = 0;
    g_currMonth = 0;
}

// 'const F32 dt' (DWARF does not record top-level const): with it the inlined copy in zGameLoop
// gets its own load of sTimeElapsed, as in retail.
inline void zGameExtras_SceneUpdate(const F32 dt)
{
    zGameCheatsUpdate(dt);

    if (!g_enableGameExtras)
    {
        return;
    }

    if (globals.cmgr)
    {
        return;
    }

    if (zGameIsPaused())
    {
        return;
    }

    EGGItem* egg_next = g_eggBasket;

    while (egg_next->fun_check)
    {
        EGGItem* egg = egg_next++;

        if (egg->enabled)
        {
            if (egg->funcs->fun_update)
            {
                egg->funcs->fun_update(dt, egg);
            }
        }
    }
}

static void EGG_update_TEST(F32, EGGItem*)
{
    static S32 doOnce = 1;

    if (doOnce)
    {
        doOnce = 0;
    }
}

static void EGG_init_TEST(EGGItem*)
{
}

static void EGG_reset_TEST(EGGItem*)
{
}

static void EGG_done_TEST(EGGItem*)
{
}

static S32 EGG_check_BDayAll(EGGItem*)
{
    S32 result = 0;
    char* scenes[] = { "HB01", NULL };
    U32 sceneID = globals.sceneCur->sceneID;
    char** list = scenes;
    char* name;

    // The result of this scene check is never used.
    while ((name = *list) != NULL)
    {
        list++;
        if (sceneID == ((name[0] & ~0x20) << 24 | name[1] << 16 | name[2] << 8 | name[3]))
        {
            break;
        }
    }

    switch (g_currMonth)
    {
    case JANUARY:
        if (g_currDay == 1)
            result = 1;
        break;

    case FEBRUARY:
        if (g_currDay == 2)
            result = 1;
        break;

    case MARCH:
        if (g_currDay == 22)
            result = 1;
        break;

    case APRIL:
    case MAY:
    case JUNE:
    case JULY:
    case AUGUST:
        break;

    case SEPTEMBER:
        if (g_currDay == 8)
            result = 1;
        break;

    case OCTOBER:
        break;

    case NOVEMBER:
        if (g_currDay == 5)
            result = 1;
        break;

    case DECEMBER:
        break;
    }

    return result;
}

static void EGG_update_BDay(F32, EGGItem*)
{
}

static void EGG_init_BDay(EGGItem*)
{
}

static void EGG_done_BDay(EGGItem*)
{
}

static void EGG_reset_BDay(EGGItem*)
{
}

S32 EGG_check_ExtrasFlags(EGGItem*)
{
    switch (g_currMonth)
    {
    case JANUARY:
        if (g_currDay == 1) // New Year's Day
            g_gameExtraFlags |= 0b000000001;
        break;

    case FEBRUARY:
        if (g_currDay == 2)
            g_gameExtraFlags |= 0b100000000;
        break;

    case MARCH:
        if (g_currDay == 15)
            g_gameExtraFlags |= 0b000001000;
        if (g_currDay == 17) // St. Patrick's Day
            g_gameExtraFlags |= 0b000000100;
        if (g_currDay == 18)
            g_gameExtraFlags |= 0b100000000;
        if (g_currDay == 21)
            g_gameExtraFlags |= 0b100000000;
        if (g_currDay == 22)
            g_gameExtraFlags |= 0b100000000;
        break;

    case APRIL:
        if (g_currDay == 1) // April Fools' Day
            g_gameExtraFlags |= 0b010000000;
        break;

    case MAY:
        break;

    case JUNE:
        if (g_currDay == 6)
            g_gameExtraFlags |= 0b001000000;
        break;

    case JULY:
        if (g_currDay == 4) // Independence Day
            g_gameExtraFlags |= 0b000000010;
        break;

    case AUGUST:
        if (g_currDay == 8)
            g_gameExtraFlags |= 0b100000000;
        break;

    case SEPTEMBER:
        if (g_currDay == 8)
            g_gameExtraFlags |= 0b000000010;
        break;

    case OCTOBER:
        if (g_currDay == 5)
            g_gameExtraFlags |= 0b100000000;
        if (g_currDay == 14)
            g_gameExtraFlags |= 0b100000000;
        if (g_currDay == 22)
            g_gameExtraFlags |= 0b100000000;
        if (g_currDay == 31) // Halloween
            g_gameExtraFlags |= 0b000100000;
        break;

    case NOVEMBER:
        if (g_currDay == 5)
            g_gameExtraFlags |= 0b100000000;
        break;

    case DECEMBER:
        break;
    }

    return 0;
}

// Cheat state (PS2 DWARF order: after g_eggBasket; retail .sbss: after EGG_update_TEST's doOnce).
static S32 g_flg_chEnabled;
static F32 sCheatTimer;
static S32 sCheatInputCount;
// Cheat button sequences (retail .data). Y and X name the GameCube buttons that the PS2-named
// pad masks map to: XPAD_BUTTON_SQUARE is Y, XPAD_BUTTON_O is X.
#define Y XPAD_BUTTON_SQUARE
#define X XPAD_BUTTON_O
U32 sCheatAddShiny[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, X, X, Y, Y, X, X, Y };
U32 sCheatAddSpatulas[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, Y, Y, X, X, Y, Y, X };
U32 sCheatBubbleBowl[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, Y, X, Y, X, X, Y, Y };
U32 sCheatCruiseBubble[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, X, Y, X, Y, Y, X, X };
U32 sCheatMonsterGallery[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, Y, X, Y, Y, X, Y, X };
U32 sCheatArtTheatre[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, X, Y, X, X, Y, X, Y };
U32 sCheatTestCheats[16] = { Y, X, Y, X, X, Y, X, X, X, Y, Y, Y, Y, X, X, Y };
U32 sCheatTargetCostumeSBCaveman[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, Y, Y, X, Y, Y, Y };
U32 sCheatTargetCostumeSBRippedShorts[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, Y, Y, X, Y, Y, X };
U32 sCheatTargetCostumePatGoofyGoober[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, Y, Y, X, Y, X, Y };
U32 sCheatTargetCostumePatCaveman[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, Y, Y, X, Y, X, X };
U32 sMaxHealth[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, X, X, X, Y, X, Y, X };
U32 sManliness[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, X, X, Y, Y, X, X };
U32 sGoofyGoober[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, Y, Y, X, Y, X, X, X };
U32 sBubbleBash[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, X, Y, Y, X, X, X, Y };
U32 sMachoBowl[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, X, X, Y, X, Y, Y, X };
U32 sSonicGuitar[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, X, Y, Y, Y, X, Y, Y };
U32 sBellyFlop[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, X, Y, X, Y, Y, X, Y };
U32 sPickupThrow[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, Y, Y, X, X, Y, X, Y };
U32 sAllMoves[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, X, Y, X, X, Y, Y };
U32 sAllUpgradedMoves[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, X, Y, X, X, Y, X };
U32 sChaChing[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, X, Y, Y, X, Y, Y, X };
U32 sTenSkillPoints[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, X, X, Y, Y, X, Y };
U32 sTenTreasureChests[16] = { 0, 0, 0, 0, 0, 0, 0, 0, Y, Y, X, X, Y, X, X, Y };
U32 sAllTasks[16] = { 0, 0, 0, 0, 0, 0, 0, 0, X, Y, X, X, Y, X, Y, Y };
#undef Y
#undef X
static GECheat cheatList[10] = {
    { sChaChing, ChaChingCB, 0x40000, 1 },
    { sMaxHealth, MaxHealthSlotsCB, 0x100, 0 },
    { sAllMoves, AllMovesCB, 0x20000, 0 },
    { sAllUpgradedMoves, AllUpgradedMovesCB, 0x400000, 0 },
    { sAllTasks, AllTasksCB, 0x800000, 0 },
    { sCheatTargetCostumeSBCaveman, TargetCostumeCBSBCaveman, 0x2000000, 1 },
    { sCheatTargetCostumeSBRippedShorts, TargetCostumeCBSBRippedShorts, 0x4000000, 1 },
    { sCheatTargetCostumePatGoofyGoober, TargetCostumeCBPatGoofyGoober, 0x8000000, 1 },
    { sCheatTargetCostumePatCaveman, TargetCostumeCBPatCaveman, 0x10000000, 1 },
    {},
};
U32 sCheatPressed[16] = {};
static xEnt* sGalleryTitle;

S32 zGameExtras_CheatFlags()
{
    return g_flg_chEnabled;
}

void zGameExtras_Save(xSerial* xser)
{
    xser->Write(g_flg_chEnabled & 0xFFFF);
}

void zGameExtras_Load(xSerial* xser)
{
    S32 keepers = 0;
    xser->Read(&keepers);
    g_flg_chEnabled |= keepers;
}

inline S32 TestCheat(U32* cheat)
{
    S32 i = 15;

    if (!cheat[i])
    {
        return 0;
    }

    while (i >= 0)
    {
        if (cheat[i] != sCheatPressed[i])
        {
            return 0;
        }
        i--;
    }

    return 1;
}

inline void AddToCheatPressed(U32 button)
{
    for (S32 i = 0; i < 15; i++)
    {
        sCheatPressed[i] = sCheatPressed[i + 1];
    }

    sCheatPressed[15] = button;
}

inline void GEC_CheatFlagAdd(S32 bit)
{
    g_flg_chEnabled |= bit;
}

inline void GEC_CheatFlagToggle(S32 bit)
{
    g_flg_chEnabled ^= bit;
}

void zGameCheatsUpdate(F32 dt)
{
    if (gGameMode != eGameMode_Pause)
    {
        return;
    }

    S32 startover = 0;
    if ((globals.pad0->pressed &
         ~(XPAD_BUTTON_L1 | XPAD_BUTTON_R1 | XPAD_BUTTON_O | XPAD_BUTTON_SQUARE)) ||
        (globals.pad0->on &
         ~(XPAD_BUTTON_L1 | XPAD_BUTTON_R1 | XPAD_BUTTON_O | XPAD_BUTTON_SQUARE)))
    {
        startover = 1;
    }
    else if ((globals.pad0->on & (XPAD_BUTTON_L1 | XPAD_BUTTON_R1)) !=
             (XPAD_BUTTON_L1 | XPAD_BUTTON_R1))
    {
        startover = 1;
    }
    else if (!(globals.pad0->pressed & (XPAD_BUTTON_O | XPAD_BUTTON_SQUARE)))
    {
        if (sCheatInputCount > 0)
        {
            sCheatTimer -= dt;
        }

        if (sCheatTimer <= 0.0f)
        {
            startover = 1;
        }
        else
        {
            return;
        }
    }

    if (!startover)
    {
        if (sCheatInputCount > 32)
        {
            startover = 1;
        }
        sCheatInputCount++;
    }

    if (startover)
    {
        if (sCheatInputCount > 0)
        {
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("CHEAT_FAILED_GRP")), 0, NULL, NULL, NULL,
                        NULL, NULL);
        }
        sCheatInputCount = 0;
        sCheatTimer = 2.0f;
        memset(sCheatPressed, 0, sizeof(sCheatPressed));
        return;
    }

    AddToCheatPressed(globals.pad0->pressed & (XPAD_BUTTON_O | XPAD_BUTTON_SQUARE));
    xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("CHEAT_STEP_GRP")), 0, NULL, NULL, NULL, NULL, NULL);
    sCheatTimer = 2.0f;

    S32 match = 0;
    GECheat* rec_next = cheatList;
    while (rec_next->key_code)
    {
        GECheat* rec_curr = rec_next++;

        if (!TestCheat(rec_curr->key_code))
        {
            continue;
        }

        match++;

        if (rec_curr->flg_mode & 1)
        {
            GEC_CheatFlagToggle(rec_curr->flg_keep);
        }
        else
        {
            GEC_CheatFlagAdd(rec_curr->flg_keep);
        }

        if (rec_curr->fun_cheat)
        {
            rec_curr->fun_cheat();
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("CHEAT_SUCCESS")), 0, NULL, NULL, NULL, NULL,
                        NULL);
        }
    }

    if (match)
    {
        sCheatInputCount = 0;
        sCheatTimer = -1.0f;
        memset(sCheatPressed, 0, sizeof(sCheatPressed));
    }
}

static void ChaChingCB()
{
    globals.player.InvManlinessPointsMultiplier = 2;
}

static void MaxHealthSlotsCB()
{
    globals.player.sb.MaxHealth = 6;
    globals.player.patrick.MaxHealth = 6;
    globals.player.g.PowerUp[ePOWERUPMENU_SBHealth] = 3;
    globals.player.g.PowerUp[ePOWERUPMENU_PatHealth] = 3;
    zMenuSB_UpdatePowerUpMenu();
}

static void AllMovesCB()
{
    S32 i;

    for (i = 0; i < 8; i++)
    {
        if (globals.player.g.PowerUp[i] < 1)
        {
            globals.player.g.PowerUp[i] = 1;
        }
    }
    zMenuSB_UpdatePowerUpMenu();
}

static void AllUpgradedMovesCB()
{
    for (S32 i = 0; i < 8; i++)
    {
        globals.player.g.PowerUp[i] = 2;
    }
    zMenuSB_UpdatePowerUpMenu();
}

static void AllTasksCB()
{
    zMenuSB_UnlockAllTasks();
}

static void TargetCostumeCBSBCaveman()
{
    EnableCheatCostume(0x2000000);
}

static void TargetCostumeCBSBRippedShorts()
{
    EnableCheatCostume(0x4000000);
}

static void TargetCostumeCBPatGoofyGoober()
{
    EnableCheatCostume(0x8000000);
}

static void TargetCostumeCBPatCaveman()
{
    EnableCheatCostume(0x10000000);
}

// Not in the GC symbol table: unreferenced in TSSM, so the linker dead-stripped it (and
// sGalleryTitle), but its "KIOSK SELECT UIF" string still sits at this position in the wad's
// string pool. Body from BfBB.
void zGame_HackGalleryInit()
{
    U32 obj = xStrHash("KIOSK SELECT UIF");
    sGalleryTitle = (xEnt*)zSceneFindObject(obj);
}

#ifdef PLATFORM_PC
// PC: the Cheats tab (iCheats.h) reads cheatList and enters a code without the buttons.

const GECheat* zGameExtras_PCCheat(S32 index)
{
    if (index < 0 || index >= (S32)(sizeof(cheatList) / sizeof(cheatList[0])) - 1)
    {
        return NULL;
    }
    return &cheatList[index];
}

// What zGameCheatsUpdate does for a row of cheatList when the buttons match it.
void zGameExtras_PCEnterCheat(S32 index, bool sound)
{
    const GECheat* rec = zGameExtras_PCCheat(index);
    if (rec == NULL)
    {
        return;
    }

    if (rec->flg_mode & 1)
    {
        GEC_CheatFlagToggle(rec->flg_keep);
    }
    else
    {
        GEC_CheatFlagAdd(rec->flg_keep);
    }

    if (rec->fun_cheat)
    {
        rec->fun_cheat();
        if (sound)
        {
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("CHEAT_SUCCESS")), 0, NULL, NULL, NULL, NULL,
                        NULL);
        }
    }
}

// A co-op joiner takes the host's cheat word (iCheatsShared).
void zGameExtras_PCSetCheatFlags(S32 flags)
{
    g_flg_chEnabled = flags;
}
#endif
