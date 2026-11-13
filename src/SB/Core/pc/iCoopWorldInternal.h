#ifndef ICOOPWORLDINTERNAL_H
#define ICOOPWORLDINTERNAL_H

// PC-only: the seam between the two game-side halves of co-op's shared world,
// and nothing else includes it. iCoopWorld.cpp (M2: the level's table of
// shared things, snapshots, the stand-in, the guest's puppets, following the
// host) and iCoopCombat.cpp (M3: the guest's hits, damage to the guest, the
// guest's respawn, pickups; docs/MULTIPLAYER.md 6). Game thread only.

#include <types.h>

#include "iNetProto.h"

#include <stddef.h>

struct xBase;
struct xEnt;
struct xModelInstance;

// --- iCoopWorld.cpp -------------------------------------------------------------------------

// The net ID of a shared thing, or -1.
S32 iCoopWorldLookup(const xBase* b);

// The shared enemy with net ID `id` (checked against the table), or NULL.
xEnt* iCoopWorldEnemy(S32 id);
// Net ID `id` as a breakable (a static or a platform with a destructible), or NULL.
struct zDestructible;
xEnt* iCoopWorldBreakable(S32 id, zDestructible** dest);

// This level's table: its scene, hash and length. FALSE when this level has
// none (or the game is between levels).
bool iCoopWorldTable(U32* scene, U32* hash, S32* count);

// Guest: the host's things are posed from its snapshots this frame.
bool iCoopWorldGuestSynced();

// Host: the guest is in this level and its newest position is believable (the
// speed check of 5.4); `pos` gets it.
bool iCoopWorldPartnerHere(F32 pos[3]);
// Host: the guest's centre (as the game takes a player's for a hit), while
// it may be a target (in this level, believable, playing).
bool iCoopWorldPartnerTarget(F32 pos[3]);

// Host: the stand-in (5.4) at the guest's place, for applying one of its hits
// (iCoopStandInEnd puts the player back). FALSE when it cannot be done now.
bool iCoopStandInGuest();

// Guest: puts this player beside the host (its newest state `host`): a step
// to its side with floor under it, the camera behind (5.6). `why` is constant
// text for the log.
void iCoopWorldPlaceBeside(const iNet::PlayerState& host, const char* why);

// Guest: the same, once this game has been in plain play for a second (after
// the host's level reset, a fade or a cutscene).
void iCoopWorldPlaceSoon(const char* why);

// Guest: this level is one it followed the host to (its save keeps nothing
// of it, 5.7).
bool iCoopWorldFollowedHere();

// --- iCoopCombat.cpp ------------------------------------------------------------------------

// Once a frame from iCoopWorldFrame, connected: `partner` is the newest state
// (NULL for none), `partnerAt` its arrival.
void iCoopCombatFrame(U64 now, const iNet::PlayerState* partner, U64 partnerAt);

// Host: the PICKUPS to send with this state (NULL: none due, or the guest is
// not in this level), and the bytes the host's snapshot must leave for it and
// for the events waiting.
const iNet::Pickups* iCoopCombatOutgoing(U64 now, bool partnerHere, size_t* reserve,
                                         const iNet::Progress** prog);

// A new session or the end of one; leaving a level.
void iCoopCombatReset();
// Not connected this frame: a guest respawn that was due falls back to the
// game's own lost chance (iCoopGuestRespawn's caller does not ask again).
void iCoopCombatOffline();
void iCoopCombatSceneExit();
// The level was reset without leaving it (zSceneReset).
void iCoopCombatSceneReset();
// Guest: this player's own progress back once the session is over (no
// session, and not joining again); `why` is constant text for the log.
void iCoopCombatProgressDone(const char* why);
// Guest: the host's world reached this level here (its own save must not
// keep that).
bool iCoopCombatHostChangedLevel();
// Guest: the host is in this level (each frame, before anything of its world
// is shown here): the first time, this level's own state is kept aside.
void iCoopCombatHostHere(U32 scene);
// Guest, saving a level whose block is kept: that block as it was before the
// host's world came (a level reset may have written it since). FALSE: none.
bool iCoopCombatPutOwnBlock(U32 scene);

// Host, during a stand-in: how long the guest is still safe from another hit
// after the last one sent to it (as the game's own DamageTimer), in seconds.
F32 iCoopCombatGuestDamageTimer();

// --- iCoopHud.cpp (the counters a level's texts show, and their texts) ----------------------

// Once a frame from iCoopWorldFrame, connected: `partner` is the newest state
// (NULL for none).
void iCoopHudFrame(U64 now, const iNet::PlayerState* partner);
// Host: the COUNTERS to send with this state (NULL: none due, or the guest
// is not in this level); `reserve` grows by the bytes it takes.
const iNet::Counters* iCoopHudOutgoing(U64 now, bool partnerHere, size_t* reserve);
// A new session or the end of one; leaving a level (before its save).
void iCoopHudReset();
void iCoopHudSceneExit();

// --- iCoopLook.cpp (M4: characters, costumes, the partner's models) -----------------------

// The model this player is drawn with now (the game's ePlayerEnum), or
// iNet::kModelUnknown: PLAYER_STATE's `model`.
U8 iCoopLookLocalModel();

// The partner's model for this level: this game's own copy of `model` (built
// once per level), or of the same character in an outfit this level has when
// `model` is not loaded here (`drawnAs`: which); NULL when there is none (only
// the name tag).
xModelInstance* iCoopLookPuppetModel(U8 model, U8 character, S32* drawnAs);
// The partner's visible parts (its state's, places in the chain of the model
// it wears) as places in the chain of the model it is drawn with.
U16 iCoopLookPartsFor(U16 parts, U8 sentModel, S32 drawnModel);

// Once a frame from iCoopFrame while co-op runs (in a level): the character
// switches asked for, the level's start, LB, and EV_LOOK when it changed.
// `partner`: the newest state, or NULL.
void iCoopLookFrame(U64 now, const iNet::PlayerState* partner);

// The partner's EV_LOOK.
void iCoopLookEvent(const iNet::GameEvent& e);

// A new session; leaving a level (the models go with it).
void iCoopLookReset();
void iCoopLookSceneExit();

// --- iCoopPuppet.cpp -------------------------------------------------------------------------

// Leaving a level: the partner's model (built from the level's memory) is
// dropped, so nothing draws it before the next iCoopFrame picks a new one.
void iCoopPuppetSceneExit();

// --- iCoopFlow.cpp (M4: level changes, cutscenes, fly-throughs, saving) ----------------------

// Once a frame from iCoopWorldFrame, connected: `partner` the newest state.
void iCoopFlowFrame(U64 now, const iNet::PlayerState* partner, U64 partnerAt);

// The host's EV_LEVEL and EV_SHOW, on the guest.
void iCoopFlowEvent(const iNet::GameEvent& e, U64 now);

// Leaving a level (zSceneExit): the host tells the guest where it goes.
void iCoopFlowSceneExit();

// A new session or the end of one.
void iCoopFlowReset();

// Guest: the level the host is going to (its EV_LEVEL) while that is fresh, or
// 0. iCoopWorld.cpp's following goes there at once.
U32 iCoopFlowHostLeftTo(U64 now);

// Guest: this game is going to `scene` to follow the host; `lead`: because
// the host just went there (EV_LEVEL), so the level's start waits for it.
void iCoopFlowFollowIssued(U32 scene, bool lead);

// --- iCoopPuppet.cpp: co-op's own cost (M5) --------------------------------------------------

// How long co-op's own code takes each frame, for TSSM_COOP_LOG's "cost" line
// every 5 s (docs/MULTIPLAYER.md 8.7): the frame's work (iCoopFrame), drawing
// the partner, the guest's posing of the host's things, the host's stand-ins.
// A scope is only ever opened after a hook's own "co-op is off" return.
enum
{
    ICOOP_COST_FRAME,
    ICOOP_COST_DRAW,
    ICOOP_COST_POSE,
    ICOOP_COST_STANDIN,
    ICOOP_COST_KINDS
};
void iCoopCostAdd(S32 kind, U64 ns);
U64 iCoopNowNs();
struct iCoopCostScope
{
    S32 kind;
    U64 t0;
    explicit iCoopCostScope(S32 k) : kind(k), t0(iCoopNowNs()) {}
    ~iCoopCostScope() { iCoopCostAdd(kind, iCoopNowNs() - t0); }
};

#endif
