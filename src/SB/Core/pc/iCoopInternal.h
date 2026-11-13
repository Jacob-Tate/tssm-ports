#ifndef ICOOPINTERNAL_H
#define ICOOPINTERNAL_H

// PC-only: what the three halves of co-op (iCoop.h) share, and nothing else
// includes. No game types: iCoopPuppet.cpp is a game unit and iCoop.cpp is
// not, and this is the seam between them.

#include <types.h>

#include "iNetProto.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

// --- the service, for the puppet (game thread) -------------------------------------

// TRUE while hosting or joining (set and cleared on the game thread).
bool iCoopRunning();

// TRUE while a partner is in the session.
bool iCoopConnected();
// A guest whose session was lost and that is joining again by itself (M5): out
// of the session for the wire, still in it for its save.
bool iCoopJoiningAgain();

// 0 hosting, 1 joining: the slot this side's PLAYER_STATE carries.
U8 iCoopLocalSlot();

// The level this game is in (its scene ID, 0 between levels), for a host's
// answer to the home network's questions (iNetDisc.h).
void iCoopNoteScene(U32 scene);

// Hands this player's newest state to the I/O thread, which sends it at once,
// in one datagram with `snap` (the host's snapshot, or NULL), `snapAck` (the
// guest's newest applied snapshot, or 0), `pick` (the host's PICKUPS, or
// NULL), `prog` (the host's PROGRESS, or NULL) and `cnt` (the host's
// COUNTERS, or NULL), and the events not acknowledged yet.
void iCoopPostLocal(const iNet::PlayerState& s, const iNet::Snapshot* snap, U32 snapAck,
                    const iNet::Pickups* pick = NULL, const iNet::Progress* prog = NULL,
                    const iNet::Counters* cnt = NULL);

// M3: queues a reliable event for the partner (sent with every datagram until
// it is acknowledged). FALSE when no partner is connected or 64 are waiting.
bool iCoopPushEvent(const iNet::GameEvent& e);

// M3: the partner's oldest event not taken yet (each once, in order).
bool iCoopTakeEvent(iNet::GameEvent* out);

// M4: TRUE when the partner's newest EV_READY (type EV_READY) or EV_GO (EV_GO)
// names `scene`; these two are kept apart from the inbox, so a game between
// levels can wait for them. Clear forgets the newest of that type.
bool iCoopPeerLevelSignal(U8 type, U32 scene);
U32 iCoopPeerLevelScene(U8 type); // the newest one's scene, or 0
void iCoopClearLevelSignal(U8 type);

// M3: the bytes the events waiting to go out take in the next datagram (the
// host's snapshot leaves that much room), 0 for none.
size_t iCoopEventBytes();

// M3, guest: the newest PICKUPS from the host since the last call.
bool iCoopTakePickups(iNet::Pickups* out);
// Protocol 7, guest: the newest PROGRESS from the host since the last call.
bool iCoopTakeProgress(iNet::Progress* out);
// Protocol 7, guest: the host's COUNTERS since the last call (merged).
bool iCoopTakeCounters(iNet::Counters* out);

// Host: the newest SNAP_ACK from the guest since the last call (FALSE: none).
bool iCoopTakeSnapAck(U32* seq);

// Guest: the oldest snapshot that arrived and was not taken yet, and when
// (iCoopNowNs). At most 8 wait; the oldest go first when more arrive.
bool iCoopTakeSnapshot(iNet::Snapshot* out, U64* at);

// The shared world's line in the Co-op tab.
void iCoopSetWorldView(bool shared, S32 things, S32 mismatch, bool following);

// Joining: "go to the host's level" was pressed since the last call.
bool iCoopTakeGoToHost();

// --- the shared world, for the puppet (iCoopWorld.cpp; game thread) ---------------------

// Once a frame from iCoopFrame, after the partner's states are in:
// `partner` is the newest one (NULL when there is none), `partnerAt` its
// arrival time.
void iCoopWorldFrame(U64 now, const iNet::PlayerState* partner, U64 partnerAt);

// When this player's state is about to go out: the host's snapshot to send
// with it (or NULL), the guest's acknowledgement (or 0), and the host's
// PICKUPS, PROGRESS and COUNTERS (or NULL; M3, protocol 7).
void iCoopWorldOutgoing(U64 now, const iNet::Snapshot** snap, U32* ack, const iNet::Pickups** pick,
                        const iNet::Progress** prog, const iNet::Counters** cnt);

// A new session or the end of one: forget everything about the last.
void iCoopWorldReset();

// M3 (iCoopCombat.cpp): this guest's defeat is over and it comes back beside
// the host at the end of this frame; its state still says defeated.
bool iCoopCombatRespawning();

// This game ran no frame for `gap` ns until `now` (a held window, a hitch, a
// load): what arrived meanwhile is the partner's newest, not stale (5.11).
void iCoopWorldLocalStall(U64 now, U64 gap);

// One PLAYER_STATE as it arrived: when (iCoopNowNs) and its order of arrival.
struct iCoopSample
{
    iNet::PlayerState s;
    U64 at;
    U32 seq;
};

// Copies the partner's states that arrived after `*sinceSeq` (oldest first, at
// most `max`, the newest kept when there are more) and moves `*sinceSeq` on.
// Only states newer than every earlier one by the sender's frame counter are
// ever queued (newest wins), and the queue is emptied when a session starts.
S32 iCoopTakeSamples(iCoopSample* out, S32 max, U32* sinceSeq);

// TRUE while the partner's game reports a stall (its thread has run no frame
// for a while; its I/O thread says so 4 times a second), with how long so far.
bool iCoopPartnerStalled(U32* ms);

// A number that changes whenever a session starts or ends, so the puppet can
// forget the last partner.
U32 iCoopSessionEpoch();

// The clock the arrival times are on.
U64 iCoopNowNs();

// How long ago `at` was at `now`, both on iCoopNowNs. The I/O thread stamps
// an arrival after the game's frame read `now`, so `at` can be a little later
// than `now`: that is 0 (just arrived), never a wrapped-around huge age.
inline U64 iCoopAge(U64 now, U64 at)
{
    return at > now ? 0 : now - at;
}

// TSSM_COOP_LOG: log this player's sent state and the partner's received ones
// every this many frames (0: off).
S32 iCoopLogEvery();

// What the puppet worked out about the partner, for the Co-op tab, and where
// the name tag goes this frame.
void iCoopSetPartnerView(bool fresh, U32 scene, U8 character, bool inMyLevel, const char* levelName,
                         U8 model = iNet::kModelUnknown);
void iCoopSetNameTag(bool valid, F32 x, F32 y, bool inFront);

// A notice for the HUD layer: constant text only.
void iCoopAddNotice(const char* text);

// --- the game-files hash (iCoopAssets.cpp; I/O thread) ----------------------------------

struct iCoopAssetJob
{
    std::string assetRoot;          // iFileAssetRoot()
    std::string modRoot;            // iFileModRoot(), or ""
    std::vector<std::string> mods;  // the enabled mods' folders, in load order
    std::string cachePath;          // the cache file, or "" for none
};

// SHA-256 over the sorted list of (lower-case relative path, size, SHA-256 of
// the content) of every .HIP, .HOP and .INI under the asset folder, then every
// file of the mod folder and of each enabled mod. The per-file hashes are
// cached in `cachePath` by (path, size, modification time). FALSE, with a
// sentence in `why`, when the asset folder cannot be read. `files` gets how
// many files were hashed.
bool iCoopHashAssets(const iCoopAssetJob& job, uint8_t out[iNet::kHashSize], char* why,
                     size_t whySize, size_t* files);

#endif
