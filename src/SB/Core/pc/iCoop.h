#ifndef ICOOP_H
#define ICOOP_H

#include <types.h>

#include <stddef.h>

// PC-only: online co-op for two players (docs/MULTIPLAYER.md). One game hosts,
// the other joins by IP address and port, and each draws the other's player
// as a puppet in its own copy of the level. M1 ("ghost buddy"): both players
// see each other move and animate. M2 (shared world): in the same level, the
// host's enemies, moving platforms and destructibles are the guest's too, the
// enemies chase whichever player is nearer, and the guest follows the host
// from level to level. M3 (combat): the guest's hits on the host's enemies,
// the enemies' hits on the guest, the guest's respawn beside the host, and
// pickups the host owns. M4 (playing through together): the host's level
// changes take the guest along and both start the level together, the
// host's cutscenes play on both, only the host's game saves; each player
// picks their character and costume, and the partner is drawn with a model
// of its own.
//
// Off unless a player turns it on. While it is off every entry point below
// returns on its first line: no socket, no thread, no allocation, and the game
// is exactly the single-player game.
//
// Seven parts:
//   iCoop.cpp        the service: settings, the I/O thread that owns the
//                    socket and the session (iNetSession.h), the mailboxes
//                    between that thread and the game thread, the status the
//                    Co-op tab shows. Platform code, no game headers.
//   iCoopAssets.cpp  the game-files hash both sides must agree on.
//   iCoopPuppet.cpp  game side, compiled with the game units: reads the local
//                    player each frame, and drives and draws the partner's
//                    puppet with the idle character's model.
//   iCoopWorld.cpp   game side: the shared world of M2 (the level's table of
//                    shared things, snapshots, the stand-in, the guest's
//                    puppets, following the host's level).
//   iCoopCombat.cpp  game side: M3 (hits, damage, the guest's respawn,
//                    pickups).
//   iCoopFlow.cpp    game side: M4's level changes, cutscenes and saving.
//   iCoopLook.cpp    game side: M4's characters, costumes and the partner's
//                    models.
// The Co-op tab and the HUD layer (name tag, notices) are gui/iGuiCoop.cpp.

// --- the game's two hooks (PLATFORM_PC blocks in the game code) -------------------

// zGameLoop, after the player update, paused or not: sends this player's state
// and works out where and how the partner is drawn this frame.
void iCoopFrame(F32 dt, S32 paused);

// zSceneRenderPreFX, right after the player is drawn (model buckets off, the
// player's light kit on): draws the partner's puppet.
void iCoopRenderPartner();

// --- M2's hooks (docs/MULTIPLAYER.md 5.8) ------------------------------------------------
//
// Each returns at once (FALSE, or nothing done) unless a session is up and
// both players are in the same level. Pointers are the scene's own xBase*
// (as zSceneSet stores them); they are only ever compared with the table's.

struct xBase;

// Guest, zSceneUpdate's entity loop and zMODEnemy::Timestep: TRUE when `b`
// is a host-owned enemy or mover. It has been posed from the host's
// snapshots (or kept still), and its own update must not run.
bool iCoopWorldGuestPose(xBase* b, F32 dt);

// Guest, zMODEnemy::PostTimestep: TRUE when `b` is host-owned (skip it).
bool iCoopWorldGuestOwns(xBase* b);

// Guest, zNMECommon::Damage / zNMETiki::Damage: TRUE when the enemy is
// host-owned, so the guest's own attacks do nothing to it (M2).
bool iCoopWorldNoDamage(xBase* b);

// Host, zMODEnemy::Timestep, around one enemy's update: when the guest is
// that enemy's target, the host's player entity stands in at the guest's
// place until iCoopStandInEnd puts it back.
void iCoopStandInBegin(xBase* b);
void iCoopStandInEnd();

// zEntPlayer's damage and event entry points: TRUE during a stand-in (the
// hit was meant for the guest, not this player).
bool iCoopStandInActive();

// Host, xUpdateCull_DistanceSquaredCB: TRUE when the point `pos` (x, y, z) is
// within the update distance (squared) of the guest.
bool iCoopCullNearPartner(const F32* pos, F32 distSq);

// Host, zNMECommon::DeathFXKick: enemy `b` played its death effect (its
// shrapnel, sound and bubbles); the guest's copy plays it too.
void iCoopWorldDeathFx(xBase* b);

// zSaveLoadAutoSaveUpdate: TRUE while joined (the guest does not autosave).
bool iCoopNoAutoSave();

// zSceneExit: the level's table is gone.
void iCoopWorldSceneExit();

// zSceneReset, at its end: the level was reset (a lost chance) without leaving it.
void iCoopWorldSceneReset();

// --- M3's hooks (docs/MULTIPLAYER.md 6.6) ------------------------------------------------
//
// Fights, damage, the guest's respawn and pickups, while both players are in
// the same level. Each returns at once (FALSE, 0, or nothing done) otherwise,
// and when co-op is off.

// Guest, zNMECommon::Damage / zNMETiki::Damage, when iCoopWorldNoDamage said
// the enemy is the host's: this player's attack (`dmgtype`, the game's
// en_npcdmg; `vecHit` its direction or NULL) hit enemy `b`, and goes to the
// host as a claim. TRUE when a claim went (the enemy's "ouch" may play here).
bool iCoopGuestHit(xBase* b, S32 dmgtype, const F32* vecHit);

// Host, zEntPlayer_PowerupLevel: while a guest's hit is applied, the power-up
// level of the guest's attack (TRUE, with `level`).
bool iCoopClaimPowerup(S32* level);

// Host, the player's damage functions during a stand-in (iCoopStandInActive):
// the hit goes to the guest instead. kind 0: plain; 1: knocked back away from
// the point `vec` (`sub`: the game's ePlayerNMEDamage); 2: knocked back with
// the velocity `vec`. Returns what the damage function returns (1: it landed).
S32 iCoopStandInHit(xBase* src, U32 damage, S32 kind, const F32* vec, S32 sub);

// Host, zEntPlayerEventCB during a stand-in: a hit or a damage event an enemy
// sent "the player" goes to the guest; any other event is dropped.
void iCoopStandInEvent(xBase* from, U32 event, const F32* param);

// Host, zMODHazard::Timestep, around one hazard's update (a projectile): when
// the guest is nearer it than the host, the host's player stands in at the
// guest's place, so it can hit the guest. TRUE when it did: the caller ends it
// with iCoopStandInEnd after the update.
bool iCoopStandInNear(const F32* pos);

// Guest, zEntPlayer_Update, when this player's defeat has played out: TRUE
// when it comes back beside the host in a bubble (no life lost, nothing
// reset; the caller only refills its health), FALSE for the game's own lost
// chance.
bool iCoopGuestRespawn();

// Guest, CheckPickupAgainstPlayer, when this player touches pickup `b`: TRUE
// when the pickup is the host's (it is asked of the host; nothing happens
// here).
bool iCoopPickupTouch(xBase* b);

// --- protocol 7's hooks (docs/MULTIPLAYER.md 13) ------------------------------------------
//
// Each returns at once (the game's own answer, FALSE, or nothing done) when
// co-op is off.

// Host, zNMECommon::Damage, in place of "tmr_invuln < 0 && IsPresent()": in
// co-op each player's hits have their own invulnerability on enemy `b` (one
// player's hit does not hold the other's back), and a hit while the enemy
// reels from the other player's (its wound goal, which takes no damage) is
// held and lands when that ends. The rest is the hit (Damage's own
// parameters), for that. TRUE when the hit lands now.
bool iCoopEnemyHittable(xBase* b, F32 invuln, S32 present, S32 dmgtype, const F32* vecHit, S32 amount, S32 pup);

// Guest, zDestructibleAsset_EventCB, a hit (`event`, `param`) on breakable
// `b`: TRUE when it is the host's (the hit goes to the host as a claim and
// breaks nothing here).
bool iCoopGuestBreakableHit(xBase* b, U32 event, const F32* param);

// Host, the enemies' breath and cloud particles (zNMEParFXType*): TRUE when
// a particle that can hurt may hurt the guest (it is in this level); FALSE at
// once when co-op is off. Then iCoopParticleHit for one at `pos` (x, y, z):
// it hurts the guest too when within `radiusSq` of the guest's centre.
bool iCoopParticleWanted();
void iCoopParticleHit(const F32* pos, F32 radiusSq);

// Guest, zEntPickupEventCB, this game's own script gives pickup `b`: TRUE
// when it is the host's (a token or chest: asked of the host).
bool iCoopPickupGiven(xBase* b);

// Guest, xCounterEventCB: TRUE while counter `b` shows the host's count
// (a counter a text shows, in the host's level): this game's own level does
// not count it, so its copy of a challenge never ends a wave by itself.
bool iCoopCounterHeld(xBase* b, U32 event);

// Guest, zEntPlayerEventCB's eEventGivePowerUp (kind 1) and
// eEventUpgradePowerUp (kind 2) for move `index`: TRUE while this game plays
// by the host's progress (the move is asked of the host; nothing changes
// here).
bool iCoopProgressClaim(S32 kind, S32 index);

// zSceneSave: TRUE when the level `scene` being saved into memory is one
// whose save block must stay as this game had it (a guest's level that the
// host's world changed).
bool iCoopKeepSceneSave(U32 scene);

// --- M4's hooks (docs/MULTIPLAYER.md 7.7) ------------------------------------------------
//
// Level changes, cutscenes, saving, characters and costumes. Each returns at
// once (FALSE, 0, NULL, or nothing done) when co-op is off.

// zGameUpdateMode, between a level's load and its start (zGameInit and
// zGameSetup): when the other player's game goes to the same level with this
// one, waits (at most 10 s, the window kept answering) until it has loaded it
// too, so both start it together.
void iCoopLevelStartBarrier(U32 scene);

// zPortalEventCB: TRUE when a guest in the host's level walked into a portal
// to another level (the host leads; nothing happens).
bool iCoopRefusePortal(U32 toScene);

// zCutsceneMgrEventCB, when a cutscene started (`param`: its start event's);
// zCutsceneMgrUpdate, when it ends. zCameraFlyEventCB, when a fly-through
// started; zCameraFlyStop, before it stops. The host tells the guest.
struct xBase;
void iCoopShowCutsceneStart(xBase* mgr, const F32* param);
void iCoopShowCutsceneEnd(xBase* mgr);
void iCoopShowFlyStart(xBase* fly);
void iCoopShowFlyStop();

// The cutscene and fly-through skip buttons: FALSE for a guest while the host
// watches the same (the host skips for both).
bool iCoopMaySkipShow();

// zSaveLoad_SaveGame: TRUE when this game must not write its save (a joined
// guest; a notice says why).
bool iCoopNoSave();

// zConfig_getAlternateCostume: this player's co-op costume for `character`
// (0 SpongeBob, 1 Patrick) as the pack's tag, when one is chosen and
// unlocked in this save.
bool iCoopCostumeOverride(S32 character, U32* tag);

// zPlayerLoadMultipleHIPs: the partner's outfit packs this level needs too
// (at most `max`, none of `loaded`), so the partner can be drawn in them.
S32 iCoopExtraPlayerPacks(U32 sceneID, const U32* loaded, S32 loadedCount, U32* out, S32 max);

// zEntPlayer_SpongeBobInit / zEntPlayer_PatrickInit: when the partner's packs
// were loaded too, this player's own model info for `character` (the game
// would take the first one it finds), else NULL.
void* iCoopLocalModelInfo(S32 character, U32* size);

// --- the platform layer ----------------------------------------------------------------

// iSystemInit, once config.ini is read and the asset and save folders are
// known: reads [coop], the TSSM_COOP_* variables and --host / --join /
// --coop-name, and starts hosting or joining if they say so.
void iCoopInit();

// iSystemExit: says goodbye to the partner and stops the I/O thread.
void iCoopShutdown();

// The game thread is running: called every frame from the pad poll, which
// pumps the window's events (iPadHostPoll in iPadHostSDL.cpp and
// iPadHostNull.cpp). The I/O thread tells the partner when this stops for a
// while (a held window, a hitch, a level load), and ends the session after a
// minute (docs/MULTIPLAYER.md 5.11). Returns at once when co-op is off.
void iCoopGameBeat();

// --- control, for the Co-op tab (game thread) ----------------------------------------

enum iCoopRole
{
    ICOOP_ROLE_OFF,
    ICOOP_ROLE_HOST,
    ICOOP_ROLE_JOIN
};

enum iCoopPhase
{
    ICOOP_PHASE_OFF,          // nothing running
    ICOOP_PHASE_HASHING,      // checking this PC's game files (once; cached)
    ICOOP_PHASE_WAITING,      // hosting, no partner yet
    ICOOP_PHASE_CONNECTING,   // joining: HELLO sent, no answer yet
    ICOOP_PHASE_CHECKING,     // joining: the password proof is on its way
    ICOOP_PHASE_CONNECTED,    // a partner is in the session
    ICOOP_PHASE_REFUSED,      // joining: the host said no (see `reason`)
    ICOOP_PHASE_NO_ANSWER,    // joining: nothing came back within 10 s
    ICOOP_PHASE_ENDED,        // joining: the session ended (see `reason`)
    ICOOP_PHASE_ERROR,        // could not start (see `reason`)
    ICOOP_PHASE_RECONNECTING, // joining, M5: the connection was lost; joining again (up to a minute)
};

// M5: how the connection is doing, for the HUD and the Co-op tab.
enum iCoopQuality
{
    ICOOP_QUALITY_NONE,        // not connected, or not measured yet
    ICOOP_QUALITY_GOOD,        // ping under 120 ms, under 3% lost
    ICOOP_QUALITY_FAIR,        // ping under 250 ms, under 10% lost
    ICOOP_QUALITY_POOR,        // more than that
    ICOOP_QUALITY_INTERRUPTED, // nothing at all from the partner for 1.5 s (not a stall it reported)
};

// What the Co-op tab shows. Every string here is either constant text of the
// port's or the partner's name, which has passed iNet::name_valid.
struct iCoopStatus
{
    iCoopRole role;
    iCoopPhase phase;
    char reason[320];        // why refused / ended / failed: constant text
    U16 port;                // hosting: the UDP port listened on
    char localAddress[48];   // hosting: this PC's address on its network, or ""
    U32 wrongPasswords;      // hosting: wrong passwords so far
    bool connected;
    char partnerName[17];
    S32 pingMs;              // -1 until measured
    // From the partner's newest state (connected only).
    bool partnerFresh;       // a state arrived in the last 3 s
    U32 partnerScene;        // 4-character scene ID, or 0
    U8 partnerCharacter;     // iNet::CHAR_*
    U8 partnerModel;         // M4: the model it is drawn as (iCoopModelName), or 0xFF
    bool partnerInMyLevel;
    char partnerLevelName[64];
    // M2: the shared world (iCoopWorld.cpp) and the traffic.
    bool worldShared;        // this level's things are shared right now
    S32 worldThings;         // how many (enemies, movers, breakables)
    S32 worldMismatch;       // snapshots refused because the levels differ
    bool following;          // joining: on the way to the host's level
    F32 kbOut, kbIn;         // KB/s over the last few seconds, -1 before
    // Stalls (5.11): how long the partner's game has run no frame, as its I/O
    // thread reports it (a held window, a hitch, a level load); 0 while it
    // runs.
    U32 partnerStallMs;
    // M5: the connection (connected only). Loss in percent over the last few
    // dozen packets, -1 until known: in (the partner's to this game) and out.
    S32 lossInPct, lossOutPct;
    U32 partnerSilentMs;     // since anything at all came from the partner
    iCoopQuality quality;
    // M5, joining: after a lost connection, joining again by itself.
    U32 reconnectLeftSec;    // ICOOP_PHASE_RECONNECTING: seconds left of the minute
    U32 reconnects;          // sessions this join got back after a lost connection
    // M5, hosting: the guest's old session, silent, replaced by its new join.
    U32 rejoins;
    // The home network (docs/MULTIPLAYER.md 14.2). Hosting: other devices on
    // this network can find the game; else why not (constant text, or "").
    bool lanAnswering;
    char lanNote[200];
    // Hosting: a game other devices may find and join (never a loopback or
    // local co-op host, except in the loopback test mode), and this device's
    // addresses on its home networks, numbers without the port.
    bool lanHost;
    U8 lanAddressCount;
    char lanAddresses[4][16];
    // Finding games (iCoopFindGames): asking, and for how many more seconds.
    bool finding;
    U32 findLeftSec;
    // Through the relay server (docs/MULTIPLAYER.md 15): the room's code once
    // the server gave it ("" before), whether the other player is in the room,
    // and the round trip to the server (-1 before it is known).
    bool relay;
    char relayRoom[5];
    bool relayPartner;
    S32 relayPingMs;
};

void iCoopGetStatus(iCoopStatus* out);

// Hosting on `port` with `password` (spaces and dashes are ignored, letters
// are not case-sensitive) as `name`. `loopback`: only this PC can join (tests).
// FALSE, with a sentence in `why`, when a value is not acceptable.
bool iCoopHost(U16 port, const char* password, const char* name, bool loopback, char* why, size_t whySize);

// Joining `address` ("IP" or "IP:port"; numbers only) with `password` as `name`.
// `remember`: once the session is up (the host proved the password), the
// address goes to [coop] address for the next time; a join that never
// connects saves nothing.
bool iCoopJoin(const char* address, const char* password, const char* name, char* why,
               size_t whySize, bool remember = false);

// Through the co-op relay server (docs/MULTIPLAYER.md 15): no forwarded port.
// `server` is "host" or "host:port" (names allowed). Hosting asks the server
// for a room: its 4-character code shows in iCoopStatus::relayRoom, and the
// guest needs it and the join code (`password`, the session's password as
// when hosting directly). Joining enters room `room`. The server address goes
// to [coop] server when hosting or joining starts.
bool iCoopHostServer(const char* server, const char* password, const char* name, char* why, size_t whySize);
bool iCoopJoinServer(const char* server, const char* room, const char* password, const char* name, char* why,
                     size_t whySize);

// Stops hosting or joining; says goodbye to a partner.
void iCoopStop();

// Hosting: ends the session and blocks the partner's address until hosting
// stops.
void iCoopKick();

// A fresh join code: 12 characters from an alphabet without I, L, O or U,
// written as "K7QX-M2PD-9TRW" (15 characters and a terminator).
void iCoopNewJoinCode(char out[16]);

// The name / port / address / name-tag setting the tab starts from ([coop]
// and the overrides), and whether the game started in join mode without a
// password (the overlay then opens on the Co-op tab to ask for one).
const char* iCoopDefaultName();
U16 iCoopDefaultPort();
const char* iCoopDefaultAddress();
const char* iCoopDefaultServer(); // [coop] server
bool iCoopDefaultLoopback();
bool iCoopNeedsPassword();
const char* iCoopStartupJoinCode(); // the code a startup host generated, or ""

bool iCoopNameTagEnabled();
void iCoopSetNameTagEnabled(bool on);

// M5: join again by itself when the connection is lost ([coop] reconnect),
// and the connection-quality line on the HUD ([coop] quality).
bool iCoopReconnectEnabled();
void iCoopSetReconnectEnabled(bool on);
bool iCoopQualityHudEnabled();
void iCoopSetQualityHudEnabled(bool on);

// M5: this build's co-op version, as both players can compare it: the first
// 8 hex digits of the build ID the handshake checks (2.5).
void iCoopVersionCode(char out[9]);

// The game-files code (docs/MULTIPLAYER.md 14.1): the first 8 hex digits of
// the game-files hash this game last sent or checked, and how many files it
// covers, so two players can see whose files differ. FALSE before the first
// host or join of this run.
bool iCoopGameFilesCode(char out[9], U32* files);

// --- the home network (docs/MULTIPLAYER.md 14.2) -----------------------------------

// Finding games: a question on this device's networks once a second for up
// to a minute (`on`, again for "Search again"), or no more (`!on`). FALSE
// while hosting or joining.
bool iCoopFindGames(bool on);

enum
{
    ICOOP_FOUND_READY, // same co-op version and game files
    ICOOP_FOUND_VERSION, // a different co-op version
    ICOOP_FOUND_FILES, // different game files
};

struct iCoopFoundGame
{
    char address
        [48]; // the host's game address, numbers ("192.168.0.23:2171"); a newer version's without a port
    char name[17]; // passed iNet::name_valid; "" for a newer version
    U8 platform; // iCoopPlatformName
    bool hasPlayer; // two players already
    U32 scene; // 0, or 4 characters of A-Z 0-9 not yet checked against this game's levels
    U8 status; // ICOOP_FOUND_*
    bool newer; // a newer discovery version: only its address is known
    char version[9]; // the host's co-op version code, "" for a newer version
    char files[9]; // the host's game-files code, "" for a newer version
};

// The games found so far (at most 8), oldest first.
S32 iCoopGetFound(iCoopFoundGame* out, S32 max);
const char* iCoopPlatformName(U8 platform); // constant text, "other device" for unknown values
U8 iCoopThisPlatform(); // what this device's answers say it is

// Joins a game of the list (by its address) with `password`, remembering the
// address once the session is up. FALSE, with a sentence, when the game is
// neither listed nor the one kept by iCoopKeepFound, or cannot be joined.
bool iCoopJoinFound(const char* address, const char* password, const char* name, char* why,
                    size_t whySize);
// Keeps a listed game (by its address) joinable after the list empties, while
// its code entry is open; the next call replaces it. FALSE when not listed.
bool iCoopKeepFound(const char* address);

// "Visible on this network" ([coop] lan_visible): a host answers the
// questions of devices on its network (never a loopback or local co-op host).
bool iCoopLanVisible();
void iCoopSetLanVisible(bool on);

// [coop] show_code: while a host visible on the home network waits for a
// player, its join code is drawn in a corner of the picture, to be read off
// the TV.
bool iCoopShowCodeEnabled();
void iCoopSetShowCodeEnabled(bool on);

// A level's name for the list (game side): "" for 0, the 4 characters when
// this game has no level of that ID.
void iCoopSceneName(U32 scene, char* out, size_t outSize);

// M5, tests only (TSSM_TEST_BLACKOUT): for `seconds`, the I/O thread drops
// everything it would send and everything that arrives, as a pulled cable
// would.
void iCoopTestBlackout(F32 seconds);

// Joining: follow the host from level to level ([coop] follow), and "go to
// the host's level" once.
bool iCoopFollowEnabled();
void iCoopSetFollowEnabled(bool on);
void iCoopGoToHost();

// M4: characters and costumes (docs/MULTIPLAYER.md 7.4), for the Co-op tab's
// Customize section. Characters: 0 SpongeBob, 1 Patrick. Costume choices
// 0..4: 0 the game's own, 1-4 that character's extras costumes.
enum
{
    ICOOP_CHAR_AUTO = 0,      // a guest takes the other character than the host's
    ICOOP_CHAR_SPONGEBOB = 1,
    ICOOP_CHAR_PATRICK = 2,
};
S32 iCoopCharacterSetting();
// Saves [coop] character and switches now when it can (within 3 s).
void iCoopSetCharacterSetting(S32 setting);
S32 iCoopCostumeSetting(S32 character);
// Saves [coop] costume_spongebob / costume_patrick; worn from the next level.
void iCoopSetCostumeSetting(S32 character, S32 choice);
const char* iCoopCostumeName(S32 character, S32 choice);
// Unlocked in this player's own save (or the game's own, always).
bool iCoopCostumeOffered(S32 character, S32 choice);

struct iCoopLookView
{
    bool inLevel;
    S32 character;            // 0 SpongeBob, 1 Patrick, -1 another (the car, the slide)
    const char* modelName;    // what this player is drawn as now
    const char* switchWhyNot; // NULL: a switch can be done now
    S32 effective[2];         // the costume each character gets when the next level loads
    bool partnerKnown;        // the partner's EV_LOOK came
    S32 partnerCharacter;
    S32 partnerCostume[2];
    bool partnerFallback;     // the partner's outfit is not loaded in this level: drawn in one of the same
                              // character this level has, until the next level loads (7.5)
};
void iCoopGetLookView(iCoopLookView* out);
// A model's name for the tab (the game's ePlayerEnum), "" for an unknown one.
const char* iCoopModelName(U8 model);

// --- for the HUD layer (gui/iGuiCoop.cpp, game thread) --------------------------------

// TRUE while co-op is doing anything, so the GUI draws its HUD layer.
bool iCoopHudActive();

// Where the partner's name tag goes this frame, as a fraction of the game's
// picture (0..1, y down), and whether that point is in front of the camera.
// FALSE when there is no partner to tag in this level.
bool iCoopNameTag(F32* x, F32* y, bool* inFront);

// Notices for the HUD ("joined", "left", ...): constant text, oldest first.
// Returns FALSE when there is none; otherwise `ageSec` is how long ago it came.
bool iCoopNotice(S32 index, const char** text, F32* ageSec);

#endif
