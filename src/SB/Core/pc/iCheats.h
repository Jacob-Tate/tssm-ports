#ifndef ICHEATS_H
#define ICHEATS_H

#include <types.h>

#include <stddef.h>

// PC-only: the Cheats tab's two halves (docs/CHEATS.md). Adapted from the BfBB
// PC port's iCheats. The GameCube build has none of this: the one call into it
// from game code (zGameLoop) is under PLATFORM_PC.
//
// THE GAME'S OWN CHEATS are the original's and stay the original's: its nine
// button codes (zGameExtras.cpp's cheatList) and the costumes and functions of
// its Extras screen (zMenuSB.cpp's list). The tab asks for one with
// iCheatsRequestCode or iCheatsRequestExtra; the request waits until the next
// frame of zGameLoop, where iCheatsGameFrame hands it to the game's own code
// (zGameExtras_PCEnterCheat, zMenuSB_PCToggleExtra: what a matched code and the
// Extras screen's select do). Nothing about them is in config.ini; they are
// saved, reset and forgotten exactly as the original does it. Entering a code
// with the buttons works as before and is seen by the tab, which reads the
// game's state every frame.
//
// THE PORT'S CHEATS are config.ini's [cheats] section, each off by default, and
// never part of a save. One struct holds what is in force (iCheatsInForce);
// iCheatsGameFrame applies it every frame of zGameLoop and puts back the
// original's value when one is turned off. With all of them off it writes
// nothing.
//
// CO-OP: iCheatsShared is everything here that changes how the game plays. The
// host fills one with iCheatsSharedGet and sends it; a joiner hands it to
// iCheatsSharedSet and from then on follows it (its own switches are locked,
// iCheatsFollowing) until iCheatsSharedRelease.
//
// iCheats.cpp is the tables and the state, with no game code (so
// tests/cheattest.cpp links it alone); iCheatsGame.cpp is a game unit.

// --- the game's button codes ---------------------------------------------------------

struct iCheatCode
{
    // The word for it in TSSM_TEST_CHEAT and the docs: "max_health".
    const char* id;
    const char* name;
    // The eight presses, as the GameCube's X and Y: "XXXXYXYX".
    const char* code;
    // Its bit in the game's cheat word (zGameExtras_CheatFlags).
    U32 flag;
    // TRUE when entering it again clears the bit (the costumes, double points).
    bool toggles;
    // What it does and how long it lasts, for the tab.
    const char* what;
    const char* lasts;
};

// In cheatList's order: a code's index is its row there.
S32 iCheatCodeCount();
const iCheatCode* iCheatCodeAt(S32 index);
S32 iCheatCodeFind(const char* id);

// The low 16 bits of the cheat word are what a save keeps (zGameExtras_Save).
#define ICHEATS_SAVED_FLAGS 0xFFFFu

// --- the Extras screen's costumes and functions ---------------------------------------

struct iCheatExtra
{
    // As SB04.INI's Extra lines name them: type 4 is a costume, 5 a function.
    S32 type;
    S32 id;
    // "mermaid_spongebob": TSSM_TEST_CHEAT's word.
    const char* word;
    // The Extras screen's own name for it.
    const char* name;
    const char* what;
    // TRUE when it shows at the next level load rather than at once.
    bool nextLevel;
};

S32 iCheatExtraCount();
const iCheatExtra* iCheatExtraAt(S32 index);
const iCheatExtra* iCheatExtraFind(S32 type, S32 id);
const iCheatExtra* iCheatExtraFindWord(const char* word);

// --- the port's cheats ------------------------------------------------------------------

enum iCheatPort
{
    // Hits knock the player about but take no health: the developers' switch
    // zGlobalSettings::TakeDamage, which zEntPlayer_Damage reads.
    iCHEAT_NO_DAMAGE,
    // The developers' flight: zGlobalSettings::CheatFlyToggle, with which white
    // held and black pressed (L2 and Z) starts and stops it.
    iCHEAT_FLY,
    // The tab switches the Extras screen's costumes and functions whatever the
    // treasure chest count. Not game logic by itself.
    iCHEAT_UNLOCK_EXTRAS,
    iCHEAT_PORT_COUNT
};

struct iCheatPortInfo
{
    const char* key;  // "cheats.no_damage"
    const char* word; // "no_damage"
    const char* name;
    const char* what;
    // What it can do to a game in progress; NULL when nothing.
    const char* caution;
};

const iCheatPortInfo* iCheatPortAt(iCheatPort cheat);
S32 iCheatPortFind(const char* word);

// What is in force: the host's while following one, else config.ini's as of the
// last iCheatsApply.
bool iCheatsInForce(iCheatPort cheat);

// Re-reads [cheats] from the config layer. iSystemInit calls it once; the tab
// after it changes a key. They apply from the next frame of zGameLoop.
void iCheatsApply();

// Every port cheat off in the config layer (the caller saves the file) and
// applied.
void iCheatsAllPortOff();

// --- requests from the tab -------------------------------------------------------------

enum iCheatRequestKind
{
    iCHEAT_REQUEST_CODE,  // value: the code's index
    iCHEAT_REQUEST_EXTRA, // value: the row of the Extras list
    iCHEAT_REQUEST_REFILL // the port's one-shot: health to the maximum
};

struct iCheatRequest
{
    iCheatRequestKind kind;
    S32 value;
};

// Queues one for the next frame of zGameLoop. FALSE (and nothing queued) while
// following a host, or when the queue is full.
bool iCheatsRequest(iCheatRequestKind kind, S32 value);
// The oldest request, taken off the queue; FALSE when there is none.
bool iCheatsNextRequest(iCheatRequest* out);

// --- co-op ---------------------------------------------------------------------------------

#define ICHEATS_SHARED_VERSION 1u
// Room for every row of the Extras list (zMenuSB.cpp's sExtrasList[50]).
#define ICHEATS_SHARED_EXTRAS 64

struct iCheatsShared
{
    U32 version;
    // Bit per iCheatPort in force.
    U32 port;
    // The game's cheat word.
    U32 gameFlags;
    // Bit per row of the Extras list whose costume or function is on.
    U32 extrasOn[ICHEATS_SHARED_EXTRAS / 32];
    // globals.player.InvManlinessPointsMultiplier (the double points code
    // leaves it at 2).
    U32 pointsMultiplier;
};

// 24 bytes, little-endian, whatever the platform: what goes on the wire.
#define ICHEATS_SHARED_BYTES 24
void iCheatsSharedPack(const iCheatsShared* in, U8* out);
// FALSE for another version or a short buffer; `out` is left alone then.
bool iCheatsSharedUnpack(const U8* in, size_t size, iCheatsShared* out);

// A joiner: follow this state from now on. Its own requests are dropped and its
// [cheats] keys wait.
void iCheatsSharedSet(const iCheatsShared* host);
void iCheatsSharedRelease();
bool iCheatsFollowing();
// The state being followed, or NULL. The game side holds the game to it every
// frame of zGameLoop.
const iCheatsShared* iCheatsSharedFollowed();

// --- the game side (iCheatsGame.cpp, a game unit) --------------------------------------------

// zGameLoop, every frame, right after the original's own cheat entry
// (zGameExtras_SceneUpdate): the tab's requests, a host's state, and the port's
// cheats.
void iCheatsGameFrame();

// A host: the state to send, read from the game as it is now.
void iCheatsSharedGet(iCheatsShared* out);

// What the tab shows, read from the game as it is now.
struct iCheatsStatus
{
    // TRUE while zGameLoop is running a level: the only time a request is taken.
    bool inGame;
    U32 gameFlags;
    U32 pointsMultiplier;
    U32 chests;  // globals.player.InvTreasureChestTotal
    bool flying; // globals.player.cheat_mode
    U32 health;
    U32 maxHealth;
};
// Once per drawn frame of the tab (it also notes that a frame was drawn, which
// is how inGame is told).
void iCheatsGetStatus(iCheatsStatus* out);

// The Extras list as the game holds it: how many rows, and one row. FALSE past
// the end. A row whose type is not 4 or 5 is a movie, sound or art pack.
S32 iCheatsExtraRows();
bool iCheatsExtraRow(S32 row, S32* type, S32* id, S32* chestsNeeded, bool* on);

// TSSM_TEST_CHEAT and TSSM_TEST_CHEATLOG (iTestEvents.h), once per presented
// frame.
void iCheatsTestFrame(S32 frame);

#endif
