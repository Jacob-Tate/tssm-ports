// The Cheats tab's tables and state; see iCheats.h. No game code here: what
// reaches into the game is iCheatsGame.cpp.

#include "iCheats.h"

#include "iConfig.h"

#include <string.h>

namespace
{
    // --- the game's button codes ----------------------------------------------------
    //
    // zGameExtras.cpp's cheatList, row for row: the code (its key_code array's
    // last eight entries, the GameCube's X for XPAD_BUTTON_O and Y for
    // XPAD_BUTTON_SQUARE), the bit (flg_keep) and whether a second entry clears
    // it (flg_mode & 1). iCheatsGame.cpp checks every row against the game's
    // own table at startup and says so in the log.

    const char kUntilTitle[] = "Until you go back to the title screen.";

    const iCheatCode kCodes[] = {
        { "double_points", "Double Manliness Points", "XXYYXYYX", 0x40000, true,
          "Every Manliness Point you pick up counts twice.",
          "Until you quit the game. Entering the code a second time does not undo it, in the "
          "original either: the doubling stays." },
        { "max_health", "All health upgrades", "XXXXYXYX", 0x100, false,
          "SpongeBob's and Patrick's health goes up to six units, as if every health upgrade "
          "was bought. It does not fill your health up now.",
          "For good: it is saved with your game, and cannot be undone." },
        { "all_moves", "All moves", "YYXYXXYY", 0x20000, false,
          "Every move you have not learned yet is learned (at its first level).",
          "For good: moves are saved with your game, and cannot be undone." },
        { "all_macho_moves", "All moves, upgraded", "YYXYXXYX", 0x400000, false,
          "Every move is learned and upgraded to its Macho version.",
          "For good: moves are saved with your game, and cannot be undone." },
        { "all_tasks", "All tasks", "XYXXYXYY", 0x800000, false,
          "Every task the game has not given you yet is given, so the pause menu's task list "
          "can take you to any of them.",
          "For good: tasks are saved with your game, and cannot be undone." },
        { "spongebob_caveman", "SpongeBob: Caveman costume", "YYYYXYYY", 0x2000000, true,
          "From the next level you load, SpongeBob wears the caveman costume. It takes off "
          "SpongeBob's other costumes.",
          kUntilTitle },
        { "spongebob_ripped_shorts", "SpongeBob: Ripped Shorts costume", "YYYYXYYX", 0x4000000,
          true,
          "From the next level you load, SpongeBob wears the ripped shorts. It takes off "
          "SpongeBob's other costumes.",
          kUntilTitle },
        { "patrick_goofy_goober", "Patrick: Goofy Goober costume", "YYYYXYXY", 0x8000000, true,
          "From the next level you load, Patrick wears the Goofy Goober costume. It takes off "
          "Patrick's other costumes.",
          kUntilTitle },
        { "patrick_caveman", "Patrick: Caveman costume", "YYYYXYXX", 0x10000000, true,
          "From the next level you load, Patrick wears the caveman costume. It takes off "
          "Patrick's other costumes.",
          kUntilTitle },
    };

    const S32 kCodeCount = (S32)(sizeof(kCodes) / sizeof(kCodes[0]));

    // --- the Extras screen ---------------------------------------------------------------
    //
    // SB04.INI's "Extra" lines of type 4 and 5, named as the Extras screen
    // names them (MNUI's text).

    const iCheatExtra kExtras[] = {
        { 4, 1, "mermaid_spongebob", "Mermaid Man SpongeBob",
          "SpongeBob dressed as Mermaid Man.", true },
        { 4, 2, "plain_spongebob", "Plain Sponge SpongeBob", "SpongeBob as a plain sponge.",
          true },
        { 4, 3, "disco_patrick", "Disco Patrick", "Patrick in his disco outfit.", true },
        { 4, 4, "plain_patrick", "Plain Patrick", "Patrick's plain look.", true },
        { 5, 1, "targeting_hand", "New Targetting Hand",
          "A different hand marks what you are aiming at.", false },
        { 5, 2, "fish_hook_hand", "Fish Hook Hand",
          "A fish hook, not the hand, brings you back when you leave the level's edge.", false },
        { 5, 3, "cucaracha_horn", "La Cucaracha Horn", "The Patty Wagon's horn plays La Cucaracha.",
          false },
        { 5, 4, "long_antennas", "Long Buckethead Antennas",
          "The antennas of the bucket-headed fish are twice as long.", false },
        { 5, 5, "antenna_fountain", "Buckethead Antenna Fountain",
          "The antennas of the bucket-headed fish spray like fountains.", false },
        { 5, 6, "tiny_foggers", "Tiny Foggers", "The Fogger enemies are tiny.", false },
        { 5, 7, "magnetic_pickup", "Magnetic Pickup",
          "Pickups fly to you from four times as far away.", false },
    };

    const S32 kExtraCount = (S32)(sizeof(kExtras) / sizeof(kExtras[0]));

    // --- the port's cheats ---------------------------------------------------------------

    const iCheatPortInfo kPort[iCHEAT_PORT_COUNT] = {
        { "cheats.no_damage", "no_damage", "No damage",
          "Hits still knock you about, but take no health. Falling off the level and the few "
          "things that end a try at once still do.",
          NULL },
        { "cheats.fly", "fly", "Flight",
#if defined(__SWITCH__) || defined(__3DS__)
          // The game's Y is the button printed X here (gui/iGuiCheats.cpp).
          "The developers' flight. Hold white and press black (L and R) to start and stop it. "
          "While flying you pass through walls, nothing hurts you, X rises, "
#elif defined(__ANDROID__)
          "The developers' flight. Hold white and press black (LB and RB on a controller; the "
          "touch controls have no white) to start and stop it. While flying you pass through walls, "
          "nothing hurts you, Y rises, "
#else
          "The developers' flight. Hold white and press black (LB and RB on an Xbox controller, "
          "Z and C on the keyboard by default) to start and stop it. While flying you pass through walls, "
          "nothing hurts you, Y rises, "
#endif
          "the Karate Spin button sinks, and holding a trigger flies faster.",
          "You can fly past what a level expects you to do, and into places it never meant "
          "you to reach: tasks may not start or finish, and you can get stuck. Save before "
          "you use it." },
        { "cheats.unlock_extras", "unlock_extras", "Extras without treasure chests",
          "This tab can switch every costume and function of the Extras screen on, however "
          "many treasure chests you have. The Extras screen itself still asks for the chests.",
          NULL },
    };

    // --- what is in force ------------------------------------------------------------------

    // The one place the port's cheats are held: config.ini's as of the last
    // iCheatsApply, or the host's while one is followed.
    struct State
    {
        bool own[iCHEAT_PORT_COUNT];
        bool following;
        iCheatsShared host;
    };

    State sState;

    const S32 kMaxRequests = 16;
    iCheatRequest sRequests[kMaxRequests];
    S32 sRequestCount;

    void Put32(U8* out, U32 v)
    {
        out[0] = (U8)v;
        out[1] = (U8)(v >> 8);
        out[2] = (U8)(v >> 16);
        out[3] = (U8)(v >> 24);
    }

    U32 Get32(const U8* in)
    {
        return (U32)in[0] | ((U32)in[1] << 8) | ((U32)in[2] << 16) | ((U32)in[3] << 24);
    }
} // namespace

S32 iCheatCodeCount()
{
    return kCodeCount;
}

const iCheatCode* iCheatCodeAt(S32 index)
{
    return index >= 0 && index < kCodeCount ? &kCodes[index] : NULL;
}

S32 iCheatCodeFind(const char* id)
{
    for (S32 i = 0; i < kCodeCount; i++)
    {
        if (strcmp(kCodes[i].id, id) == 0)
        {
            return i;
        }
    }
    return -1;
}

S32 iCheatExtraCount()
{
    return kExtraCount;
}

const iCheatExtra* iCheatExtraAt(S32 index)
{
    return index >= 0 && index < kExtraCount ? &kExtras[index] : NULL;
}

const iCheatExtra* iCheatExtraFind(S32 type, S32 id)
{
    for (S32 i = 0; i < kExtraCount; i++)
    {
        if (kExtras[i].type == type && kExtras[i].id == id)
        {
            return &kExtras[i];
        }
    }
    return NULL;
}

const iCheatExtra* iCheatExtraFindWord(const char* word)
{
    for (S32 i = 0; i < kExtraCount; i++)
    {
        if (strcmp(kExtras[i].word, word) == 0)
        {
            return &kExtras[i];
        }
    }
    return NULL;
}

const iCheatPortInfo* iCheatPortAt(iCheatPort cheat)
{
    return cheat >= 0 && cheat < iCHEAT_PORT_COUNT ? &kPort[cheat] : NULL;
}

S32 iCheatPortFind(const char* word)
{
    for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
    {
        if (strcmp(kPort[i].word, word) == 0)
        {
            return i;
        }
    }
    return -1;
}

bool iCheatsInForce(iCheatPort cheat)
{
    if (cheat < 0 || cheat >= iCHEAT_PORT_COUNT)
    {
        return false;
    }
    if (sState.following)
    {
        return (sState.host.port & (1u << cheat)) != 0;
    }
    return sState.own[cheat];
}

void iCheatsApply()
{
    for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
    {
        sState.own[i] = iConfigGetBool(kPort[i].key, FALSE) != 0;
    }
}

void iCheatsAllPortOff()
{
    for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
    {
        iConfigSet(kPort[i].key, "off");
    }
    iCheatsApply();
}

bool iCheatsRequest(iCheatRequestKind kind, S32 value)
{
    if (sState.following || sRequestCount >= kMaxRequests)
    {
        return false;
    }
    sRequests[sRequestCount].kind = kind;
    sRequests[sRequestCount].value = value;
    sRequestCount++;
    return true;
}

bool iCheatsNextRequest(iCheatRequest* out)
{
    if (sRequestCount == 0)
    {
        return false;
    }
    *out = sRequests[0];
    sRequestCount--;
    memmove(&sRequests[0], &sRequests[1], sizeof(sRequests[0]) * sRequestCount);
    return true;
}

void iCheatsSharedPack(const iCheatsShared* in, U8* out)
{
    Put32(out + 0, in->version);
    Put32(out + 4, in->port);
    Put32(out + 8, in->gameFlags);
    Put32(out + 12, in->extrasOn[0]);
    Put32(out + 16, in->extrasOn[1]);
    Put32(out + 20, in->pointsMultiplier);
}

bool iCheatsSharedUnpack(const U8* in, size_t size, iCheatsShared* out)
{
    if (size < ICHEATS_SHARED_BYTES || Get32(in) != ICHEATS_SHARED_VERSION)
    {
        return false;
    }
    out->version = Get32(in + 0);
    out->port = Get32(in + 4);
    out->gameFlags = Get32(in + 8);
    out->extrasOn[0] = Get32(in + 12);
    out->extrasOn[1] = Get32(in + 16);
    out->pointsMultiplier = Get32(in + 20);
    return true;
}

void iCheatsSharedSet(const iCheatsShared* host)
{
    sState.host = *host;
    sState.following = true;
    // A joiner's own requests are not the host's choice.
    sRequestCount = 0;
}

void iCheatsSharedRelease()
{
    sState.following = false;
}

bool iCheatsFollowing()
{
    return sState.following;
}

const iCheatsShared* iCheatsSharedFollowed()
{
    return sState.following ? &sState.host : NULL;
}
