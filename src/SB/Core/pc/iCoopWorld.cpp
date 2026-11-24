// PC-only: the shared world of online co-op (iCoop.h, docs/MULTIPLAYER.md 5).
// Compiled with the game units: it reads and poses the level's entities.
//
// While both players are in the same level:
//   - both games make the same table of shared things from the level (5.2):
//     enemies, movers (platforms, pendulums) and breakables;
//   - the host sends a SNAPSHOT of them with each PLAYER_STATE, as a delta
//     against the newest one the guest acknowledged (5.3);
//   - the guest's copies stop simulating and are posed 100 ms in the past
//     between the snapshots around that moment (5.5);
//   - on the host, an enemy whose nearer player is the guest updates with the
//     host's player standing in at the guest's place (5.4), and the update cull
//     keeps things near the guest awake;
//   - the guest follows the host from level to level (5.6).
//
// Everything that comes from the network is used here only as validated
// numbers: a net ID is an index checked against this game's own table, an
// animation ID is looked up in this game's own table, a scene ID is checked
// against the game's own world table before it is loaded. Nothing from the
// network is a pointer, a path, a size or a format string.

#include "iCoop.h"
#include "iCapture.h"
#include "iCoopInternal.h"
#include "iCoopLocal.h"
#include "iCoopWorldInternal.h"

#include "xAnim.h"
#include "xBound.h"
#include "xCollide.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xMath3.h"
#include "xModel.h"
#include "xRay3.h"
#include "xScene.h"
#include "zCam.h"
#include "zCamera.h"
#include "zDestructible.h"
#include "zDispatcher.h"
#include "zEntSimpleObj.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zNMECommon.h"
#include "zNMEGoalStd.h"
#include "zNMENPCWrapper.h"
#include "zNMEShrapnel.h"
#include "zNMETypeStdFogger.h"
#include "zNMETypeTiki.h"
#include "zNPCTypes.h"
#include "zPlatform.h"
#include "zPortal.h"
#include "zScene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <utility>
#include <vector>

using namespace iNet;

namespace
{
    const U64 kNsPerMs = 1000000ull;
    const S32 kRing = 32;              // snapshots remembered (both sides)
    const S32 kSamples = 4;            // per thing, on the guest
    const S64 kInterpDelayMs = 100;    // the guest draws the host's world this far back
    const U64 kSyncStale = 3000 * kNsPerMs;   // no snapshot this long: the guest's world is its own again
                                              // (unless the host's game reports a stall: 5.11)
    // Stalls (5.11). Snapshots this far apart in the host's time had a stall
    // between them (either game's, or the link's): the guest shows the newer
    // one at once instead of sliding across the gap.
    const S64 kGapMs = 400;
    // Past the newest snapshot by more than this, the guest holds the
    // animations too (the host's game is not running them either).
    const S64 kAnimHoldMs = 150;
    // The host sends snapshots only while the guest's newest state is this
    // new; a stalled guest's stream pauses (and resumes against what it has).
    const U64 kSendFresh = 500 * kNsPerMs;
    // The guest shows the host's world once it has a state for every shared
    // thing (the first snapshots carry the whole table in a few packets), or
    // this long after the first one, whatever is missing (5.5).
    const U64 kFullWait = 2000 * kNsPerMs;
    const U64 kTargetFresh = 500 * kNsPerMs;  // the guest is a target only with a state this new
    const F32 kMaxSpeed = 60.0f;       // units a second; faster is a teleport (5.4)
    const U64 kTeleportEvery = 2000 * kNsPerMs;
    // A jump that lands this near the host's player is co-op's own placing
    // (beside it: 1.75; the host may have moved on by the time it is seen).
    const F32 kBesideHost = 8.0f;
    // A defeat's own jump as it starts (about 5.4 units seen), believed this far at most.
    const F32 kDefeatJump = 10.0f;
    const U64 kFollowAfter = 1500 * kNsPerMs; // the host in another level this long: follow (5.6)
    const F32 kSnapDist = 10.0f;       // a jump this long between two samples is not slid
    // Nor is one faster than this (units a second, over more than a unit): an
    // enemy put back at its spawn point when it dies, or by the host's level
    // reset after its defeat (DE01's robots: 2 units in 49 ms, 41 a second).
    // The fastest shared things measured move at 28 (BB02's platforms, over
    // every M5 run's log); a running player 10.
    const F32 kSnapSpeed = 35.0f;
    const F32 kLogRadius = 25.0f;      // TSSM_COOP_LOG: things this near a player are logged
    const S32 kLogMax = 6;
    // A death effect, break or respawn plays at most this often per thing,
    // whatever a (hostile) host's snapshots flip.
    const U64 kEffectEvery = 2000 * kNsPerMs;

    enum Kind : U8
    {
        K_ENEMY = 1,
        K_MOVER = 2,
        K_BREAKABLE = 3,
    };

    struct Thing
    {
        xEnt* ent;
        xBase* base; // as the scene stores it (the hooks compare with this)
        U32 id;
        U8 kind;
        bool hasAnim;
        zDestructible* dest;
        U8 fields; // the SE_* a full state of this thing has
    };

    // One thing as the guest believes it (both sides keep these per snapshot).
    // The animation time `t` was the host's at host time `tMs`; from there it
    // runs on by the state's own speed, looping or stopping as the state does,
    // on both sides alike (Predict), so a thing whose animation just plays on
    // costs nothing to send.
    struct WState
    {
        F32 pos[3];
        S16 rot[4];
        U32 anim;
        F32 t;
        S64 tMs;
        U8 flags;
        U8 alpha;
        U8 have; // SE_* fields known
        U8 pad;
    };

    // What Predict needs of an animation state, from this game's own table.
    struct AnimInfo
    {
        F32 duration;
        F32 speed;
        bool loop;
        bool ok;
    };

    struct Slot
    {
        U32 seq;
        S64 hostMs;
        std::vector<WState> st;
    };

    struct Sample
    {
        S64 hostMs;
        WState s;
    };

    struct GuestThing
    {
        Sample smp[kSamples]; // oldest first
        S32 count;
        S8 hostBroken; // -1 unknown, 0, 1
        bool shownSet;
        S8 deaths;     // the host's death-effect count last shown, -1 unknown
        U64 lastEffectAt; // the last death effect, break or respawn played for it
        U32 infoAnim; // the state infoOf was looked up for
        AnimInfo info;
        // TSSM_COOP_LOG: where Pose last put it, and on which frame, to catch
        // any other code moving a host-owned copy after it was posed.
        F32 posed[3];
        U32 posedFrame;
        bool heard; // the host has sent a state for it in this level
        bool revived; // its collision put back while the host shows it (its own dead goal had taken it)
    };

    struct HostThing
    {
        U32 lastSentSeq; // the newest snapshot that carried it, 0 for none
        U8 target;       // 0 the host's player, 1 the guest
        AnimInfo info;   // of its current animation state, this frame
        bool havePrev;   // the animation at the last capture, to tell a still one
        U32 prevAnim;
        F32 prevT;
        U8 deaths;       // death effects played (zNMECommon::DeathFXKick), modulo 4
    };

    // --- the level's table -------------------------------------------------------------

    std::vector<Thing> sThings;
    std::vector<std::pair<xBase*, U16> > sByPtr; // sorted by pointer
    U32 sTableHash;
    bool sTableValid;
    zScene* sTableScene;
    U32 sTableSceneId;
    S32 sEnemies, sMovers, sBreakables;

    // --- both sides ----------------------------------------------------------------------

    Slot sRing[kRing];
    std::vector<WState> sScratch;
    bool sDirty; // anything to forget on iCoopWorldReset
    U32 sFrameNo;
    // Stalls (5.11): this game's own recent stalls, each as when it began and
    // when the game ran again (what arrived during one counts from its end;
    // what arrived before one ages as usual), and whether the partner's game
    // reports one (this frame).
    struct LocalStall
    {
        U64 from;
        U64 resume;
    };
    const S32 kStallMemory = 8;
    LocalStall sStalls[kStallMemory];
    S32 sStallNext;
    bool sPartnerStalled;

    // --- the host ----------------------------------------------------------------------------

    U32 sSeq;
    U32 sAcked;
    bool sHostStarted; // snapshots have gone out since the guest came to this level
    bool sPartnerSending; // the guest's states are fresh: snapshots go out (paused otherwise, 5.11)
    bool sSendPausedLogged;
    std::vector<HostThing> sHost;
    std::vector<WState> sLastSent; // per thing: what the guest believes after the newest snapshot carrying it
    Snapshot sOutSnap;
    size_t sSnapReserve; // M3: room the next snapshot leaves for EVENTs and PICKUPS
    bool sPartnerInLevel;  // the guest is in this level (its newest state, at most 3 s old)
    bool sPartnerCullOk;   // sPartnerPos may keep things awake
    bool sPartnerTargetOk; // and enemies may target the guest
    xVec3 sPartnerPos;
    xVec3 sPartnerVel;
    U8 sPartnerHealth;
    U32 sPartnerFrame;
    U64 sPartnerLastAt;
    xVec3 sPartnerLast;
    bool sPartnerHaveLast;
    bool sPartnerLastDead; // the newest state before this one said defeated
    U64 sLastDeadJump; // the last jump believed for a defeat
    U64 sLastTeleport;
    U64 sSuspectUntil;
    U32 sLoggedSuspect;

    // the stand-in
    const S32 kMaxBones = 160;
    struct StandIn
    {
        bool active;
        xEnt* player;
        xModelInstance* model;
        S32 mats;
        RwMatrix mat[kMaxBones];
        xMat4x3 fmat, foldmat;
        xBound bound;
        xVec3 predictRotate, predictTranslate, predictCurrDir;
        F32 predictAngV, predictCurrVel;
        U32 health;
        F32 damageTimer;
    };
    StandIn sStand;
    U32 sStandIns;
    bool StandInAt();
    bool PlaceBeside(const PlayerState& host, const char* why);

    // --- the guest ------------------------------------------------------------------------------

    std::vector<GuestThing> sGuest;
    bool sGuestSync;    // this frame: host-owned things are posed, not simulated
    S32 sGuestKnown;    // things with a state from the host
    S32 sGuestPosable;  // things in the table
    U64 sGuestFirstAt;  // the first snapshot applied in this level, 0 for none
    bool sGuestFullLogged;
    U32 sPoseMovedLogged;
    U32 sJumpLogged;
    U32 sApplied;       // the newest snapshot applied, 0 for none
    S64 sAppliedHostMs;
    U64 sAppliedAt;
    bool sHaveHostClock;
    U32 sHostRaw;
    S64 sHostMs;
    const S32 kOffsets = 64;
    S64 sOffsets[kOffsets];
    S32 sOffsetCount, sOffsetNext;
    S64 sRenderHostMs;
    bool sHostStallSeen; // the host's game reported a stall since the last snapshot applied
    U32 sGapsLogged;
    S32 sMismatch;
    bool sMismatchLogged;
    U32 sUnknownAnimLogged;
    U32 sBreathLogged; // a fogger's breath drawn here, logged (per level)
    U32 sMissingBase;
    Snapshot sInSnap;
    std::vector<U8> sIncluded; // this snapshot carried it

    // following the host (5.6)
    U32 sFollowScene;
    U64 sFollowSince;
    U32 sFollowIssued;
    bool sPlaceOnArrival;
    const char* sPlaceWhy = "arrived in the host's level"; // constant text, for the log
    // Local co-op's Player 2: put beside Player 1 once, on its first join (asked
    // again if a new session's reset took the request away first).
    const char* const kLocalWhy = "joined beside Player 1";
    bool sLocalPlaced;
    U32 sLocalPlaceEpoch;
    U64 sPlainSince; // the guest's game in plain play since (no fly-through, cutscene or pause)
    const U64 kPlaceAfter = 1000 * kNsPerMs;
    // The level the guest went to by following the host (5.7): no autosave
    // there, while the session lasts and after it, until the guest leaves it.
    // Not cleared with the session.
    U32 sNoSaveScene;
    bool sNoSaveArrived;
    U32 sKeepSaveScene; // the level being left, whose own save block stays as it was
    xPortalAsset sPortalAsset;
    _zPortal sPortal;

    // --- small helpers --------------------------------------------------------------------------

    xEnt* Player()
    {
        return xglobals->___player_ent_dont_use_directly;
    }

    S64 NowMs(U64 now)
    {
        return (S64)(now / kNsPerMs);
    }

    // How old something that arrived at `at` is, not counting a stall of this
    // game: what the I/O thread took in while this game's thread was stopped
    // is as new as this game's first frame after it (5.11). Only what arrived
    // during a stall: what came before one ages through it as usual, so a
    // stall while nothing comes in (a lost connection) never makes the
    // partner's last word fresh again.
    U64 AgeSince(U64 now, U64 at)
    {
        for (S32 i = 0; i < kStallMemory; i++)
        {
            const LocalStall& st = sStalls[i];
            if (st.resume != 0 && at >= st.from && at < st.resume)
            {
                return iCoopAge(now, st.resume);
            }
        }
        return iCoopAge(now, at);
    }

    void SceneText(U32 id, char out[5])
    {
        for (int i = 0; i < 4; i++)
        {
            char c = (char)(id >> (24 - 8 * i));
            out[i] = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '?';
        }
        out[4] = '\0';
    }

    F32 Dist2(const xVec3& a, const F32* b)
    {
        F32 dx = a.x - b[0], dy = a.y - b[1], dz = a.z - b[2];
        return dx * dx + dy * dy + dz * dz;
    }

    F32 Dist2(const xVec3& a, const xVec3& b)
    {
        F32 dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool Finite(F32 f)
    {
        return f == f && f >= -kWorldLimit && f <= kWorldLimit;
    }

    AnimInfo InfoOf(const xAnimState* st)
    {
        AnimInfo ai;
        memset(&ai, 0, sizeof(ai));
        if (st == NULL || st->Data == NULL || !(st->Data->Duration > 0.0f) || !(st->Data->Duration < 1e5f))
        {
            return ai;
        }
        ai.duration = st->Data->Duration;
        ai.speed = (st->Speed == st->Speed && st->Speed >= 0.0f && st->Speed < 100.0f) ? st->Speed : 1.0f;
        ai.loop = (st->Flags & 0x70) == 0x10; // as xAnimPlayUpdate: LoopUpdate, else StopUpdate
        ai.ok = true;
        return ai;
    }

    // The animation time `sec` seconds after `t0`, as the game would play it.
    F32 Predict(F32 t0, F32 sec, const AnimInfo& ai)
    {
        if (!ai.ok || !(sec > 0.0f))
        {
            return t0;
        }
        F32 t = t0 + sec * ai.speed;
        if (ai.loop)
        {
            t = fmodf(t, ai.duration);
            if (t < 0.0f)
            {
                t += ai.duration;
            }
        }
        else if (t > ai.duration)
        {
            t = ai.duration;
        }
        return t;
    }

    // The animation time of `w` `sec` seconds on: still if the host said so.
    F32 PredictOf(const WState& w, F32 sec, const AnimInfo& ai)
    {
        return (w.flags & WF_STILL) ? w.t : Predict(w.t, sec, ai);
    }

    // How far apart two animation times are, the short way round a loop.
    F32 AnimGap(F32 a, F32 b, const AnimInfo& ai)
    {
        F32 d = fabsf(a - b);
        if (ai.ok && ai.loop && d > ai.duration * 0.5f)
        {
            d = ai.duration - d;
        }
        return d;
    }

    // --- rotations -----------------------------------------------------------------------------
    //
    // A rotation travels as a unit quaternion (x y z w) scaled to 32767, made
    // from the model matrix's rows with their lengths (the scale) taken out.
    // Both sides use these two functions, so the convention only has to agree
    // with itself. A mirrored matrix (negative determinant) has its first row
    // flipped first; whether a thing is mirrored is a property of its asset,
    // the same on both sides, so the receiver flips it back from its own.

    void RowsOf(const xMat4x3& m, F32 r[3][3], F32 len[3], bool* mirror)
    {
        const xVec3* rows[3] = { &m.right, &m.up, &m.at };
        for (int i = 0; i < 3; i++)
        {
            F32 l = sqrtf(rows[i]->x * rows[i]->x + rows[i]->y * rows[i]->y + rows[i]->z * rows[i]->z);
            if (!(l > 1e-6f) || !(l < 1e6f))
            {
                l = 1.0f;
            }
            len[i] = l;
            r[i][0] = rows[i]->x / l;
            r[i][1] = rows[i]->y / l;
            r[i][2] = rows[i]->z / l;
        }
        F32 det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                  r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
        *mirror = det < 0.0f;
        if (*mirror)
        {
            r[0][0] = -r[0][0];
            r[0][1] = -r[0][1];
            r[0][2] = -r[0][2];
        }
    }

    void QuatFromRows(const F32 m[3][3], S16 out[4])
    {
        F32 x, y, z, w;
        F32 tr = m[0][0] + m[1][1] + m[2][2];
        if (tr > 0.0f)
        {
            F32 s = sqrtf(tr + 1.0f) * 2.0f;
            w = 0.25f * s;
            x = (m[2][1] - m[1][2]) / s;
            y = (m[0][2] - m[2][0]) / s;
            z = (m[1][0] - m[0][1]) / s;
        }
        else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
        {
            F32 s = sqrtf(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
            w = (m[2][1] - m[1][2]) / s;
            x = 0.25f * s;
            y = (m[0][1] + m[1][0]) / s;
            z = (m[0][2] + m[2][0]) / s;
        }
        else if (m[1][1] > m[2][2])
        {
            F32 s = sqrtf(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
            w = (m[0][2] - m[2][0]) / s;
            x = (m[0][1] + m[1][0]) / s;
            y = 0.25f * s;
            z = (m[1][2] + m[2][1]) / s;
        }
        else
        {
            F32 s = sqrtf(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
            w = (m[1][0] - m[0][1]) / s;
            x = (m[0][2] + m[2][0]) / s;
            y = (m[1][2] + m[2][1]) / s;
            z = 0.25f * s;
        }
        F32 n = sqrtf(x * x + y * y + z * z + w * w);
        if (!(n > 1e-6f) || !(n < 1e6f))
        {
            out[0] = out[1] = out[2] = 0;
            out[3] = 32767;
            return;
        }
        if (w < 0.0f)
        {
            n = -n; // q and -q are the same rotation; w >= 0 keeps deltas small
        }
        F32 q[4] = { x / n, y / n, z / n, w / n };
        bool any = false;
        for (int i = 0; i < 4; i++)
        {
            F32 v = floorf(q[i] * 32767.0f + 0.5f);
            v = v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v);
            out[i] = (S16)v;
            any = any || out[i] != 0;
        }
        if (!any)
        {
            out[3] = 32767;
        }
    }

    void RowsFromQuat(const F32 qi[4], F32 m[3][3])
    {
        F32 n = sqrtf(qi[0] * qi[0] + qi[1] * qi[1] + qi[2] * qi[2] + qi[3] * qi[3]);
        F32 x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
        if (n > 1e-6f)
        {
            x = qi[0] / n;
            y = qi[1] / n;
            z = qi[2] / n;
            w = qi[3] / n;
        }
        m[0][0] = 1.0f - 2.0f * (y * y + z * z);
        m[0][1] = 2.0f * (x * y - z * w);
        m[0][2] = 2.0f * (x * z + y * w);
        m[1][0] = 2.0f * (x * y + z * w);
        m[1][1] = 1.0f - 2.0f * (x * x + z * z);
        m[1][2] = 2.0f * (y * z - x * w);
        m[2][0] = 2.0f * (x * z - y * w);
        m[2][1] = 2.0f * (y * z + x * w);
        m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    }

    // --- the table (5.2) ----------------------------------------------------------------------

    bool EnemyType(U32 t)
    {
        // Protocol 9 (docs/MULTIPLAYER.md 16): the Frogfish and its tongue are
        // shared too; iCoopBoss.cpp carries the fight's own state.
        return (t >= NME_TYPE_TIKI && t <= NME_TYPE_TIKI_STONE) || (t >= NME_TYPE_CRITTER && t <= NME_TYPE_BUCKOTRON_V7) ||
               t == NME_TYPE_FROGFISH || t == NME_TYPE_TONGUESPIN;
    }

    bool MoverPlatform(const zPlatform* p)
    {
        // Teeter-totters, conveyor belts, springboards, falling and breakaway
        // platforms and paddles move because the local player stands on them
        // or hits them: each game keeps its own, so each player's own weight
        // and hits move them on that player's screen (5.2).
        switch (p->subType)
        {
        case ZPLATFORM_SUBTYPE_TEETER:
        case ZPLATFORM_SUBTYPE_CONVBELT:
        case ZPLATFORM_SUBTYPE_SPRINGBOARD:
        case ZPLATFORM_SUBTYPE_FALLING:
        case ZPLATFORM_SUBTYPE_BREAKAWAY:
        case ZPLATFORM_SUBTYPE_PADDLE:
            return false;
        default:
            return true;
        }
    }

    U8 FieldsOf(const Thing& t)
    {
        if (t.kind == K_BREAKABLE)
        {
            return SE_FLAGS;
        }
        // Not SE_ALPHA: a model's alpha is mostly the camera's distance fade
        // (zLOD), which each game works out for its own camera.
        return (U8)(SE_POS | SE_ROT | SE_FLAGS | (t.hasAnim ? SE_ANIM : 0));
    }

    bool HasAnim(const xEnt* e)
    {
        const xModelInstance* m = e->model;
        return m != NULL && m->Anim != NULL && m->Anim->Table != NULL && m->Anim->Single != NULL;
    }

    void ClearTable()
    {
        sThings.clear();
        sByPtr.clear();
        sHost.clear();
        sGuest.clear();
        sIncluded.clear();
        for (S32 i = 0; i < kRing; i++)
        {
            sRing[i].seq = 0;
            sRing[i].st.clear();
        }
        sTableValid = false;
        sTableScene = NULL;
        sTableSceneId = 0;
        sTableHash = 0;
        sEnemies = sMovers = sBreakables = 0;
        sBreathLogged = 0;
    }

    void ResetSession()
    {
        for (S32 i = 0; i < kRing; i++)
        {
            sRing[i].seq = 0;
        }
        sSeq = 0;
        sAcked = 0;
        sHostStarted = false;
        for (size_t i = 0; i < sHost.size(); i++)
        {
            sHost[i].lastSentSeq = 0;
            sHost[i].target = 0;
        }
        for (size_t i = 0; i < sGuest.size(); i++)
        {
            sGuest[i].count = 0;
            sGuest[i].hostBroken = -1;
            sGuest[i].shownSet = false;
            sGuest[i].deaths = -1;
            sGuest[i].heard = false;
        }
        sGuestSync = false;
        sGuestKnown = 0;
        sGuestFirstAt = 0;
        sGuestFullLogged = false;
        sApplied = 0;
        sAppliedAt = 0;
        sAppliedHostMs = 0;
        sHostStallSeen = false;
        sPartnerSending = false;
        sSendPausedLogged = false;
        sHaveHostClock = false;
        sOffsetCount = sOffsetNext = 0;
        sMismatch = 0;
        sMismatchLogged = false;
        sMissingBase = 0;
        sPartnerInLevel = sPartnerCullOk = sPartnerTargetOk = false;
        sPartnerHaveLast = false;
        sPartnerLastDead = false;
        sLastDeadJump = 0;
        sLastTeleport = 0;
        sSuspectUntil = 0;
        sFollowScene = 0;
        sFollowIssued = 0;
        sPlaceOnArrival = false;
        sPlainSince = 0;
    }

    void BuildTable()
    {
        ClearTable();
        zScene* s = globals.sceneCur;
        if (s == NULL || s->base == NULL)
        {
            return;
        }
        for (U32 i = 0; i < s->num_base; i++)
        {
            xBase* b = s->base[i];
            if (b == NULL)
            {
                continue;
            }
            Thing t;
            memset(&t, 0, sizeof(t));
            t.base = b;
            t.id = b->id;
            switch (b->baseType)
            {
            case eBaseTypeNPC:
            {
                xEnt* e = (xEnt*)b;
                zNMENPCWrapper* w = static_cast<zNMENPCWrapper*>(e);
                if (!EnemyType((U32)w->SelfType()) || e->model == NULL || e->model->Mat == NULL)
                {
                    continue;
                }
                t.ent = e;
                t.kind = K_ENEMY;
                break;
            }
            case eBaseTypePlatform:
            {
                zPlatform* p = (zPlatform*)b;
                if (!MoverPlatform(p) || p->model == NULL || p->model->Mat == NULL)
                {
                    continue;
                }
                t.ent = p;
                t.kind = K_MOVER;
                t.dest = p->destructible;
                break;
            }
            case eBaseTypePendulum:
            {
                xEnt* e = (xEnt*)b;
                if (e->model == NULL || e->model->Mat == NULL)
                {
                    continue;
                }
                t.ent = e;
                t.kind = K_MOVER;
                break;
            }
            case eBaseTypeStatic:
            {
                zEntSimpleObj* o = (zEntSimpleObj*)b;
                if (o->destructible == NULL)
                {
                    continue;
                }
                t.ent = o;
                t.kind = K_BREAKABLE;
                t.dest = o->destructible;
                break;
            }
            default:
                continue;
            }
            t.hasAnim = t.kind != K_BREAKABLE && HasAnim(t.ent);
            t.fields = FieldsOf(t);
            sThings.push_back(t);
            if (sThings.size() >= kMaxNetIds)
            {
                printf("tssm: coop: this level has more than %d shared things; the rest stay each game's own\n",
                       (int)kMaxNetIds);
                break;
            }
        }
        // The same order on both games, whatever order the level was built in.
        std::stable_sort(sThings.begin(), sThings.end(), [](const Thing& a, const Thing& b) {
            return a.id != b.id ? a.id < b.id : a.kind < b.kind;
        });
        U32 h = 2166136261u;
        for (size_t i = 0; i < sThings.size(); i++)
        {
            U8 bytes[5] = { sThings[i].kind, (U8)sThings[i].id, (U8)(sThings[i].id >> 8), (U8)(sThings[i].id >> 16),
                            (U8)(sThings[i].id >> 24) };
            for (int k = 0; k < 5; k++)
            {
                h = (h ^ bytes[k]) * 16777619u;
            }
            sByPtr.push_back(std::make_pair(sThings[i].base, (U16)i));
            switch (sThings[i].kind)
            {
            case K_ENEMY: sEnemies++; break;
            case K_MOVER: sMovers++; break;
            default: sBreakables++; break;
            }
        }
        std::sort(sByPtr.begin(), sByPtr.end());
        sTableHash = h;
        sTableScene = s;
        sTableSceneId = s->sceneID;
        sTableValid = !sThings.empty();
        sHost.assign(sThings.size(), HostThing());
        GuestThing g;
        memset(&g, 0, sizeof(g));
        g.hostBroken = -1;
        g.deaths = -1;
        sGuest.assign(sThings.size(), g);
        sIncluded.assign(sThings.size(), 0);
        for (S32 i = 0; i < kRing; i++)
        {
            sRing[i].seq = 0;
            sRing[i].st.assign(sThings.size(), WState());
        }
        sScratch.assign(sThings.size(), WState());
        sLastSent.assign(sThings.size(), WState());
        // sSeq runs on through the session: a guest still in this level from
        // before takes only snapshots newer than its last.
        sAcked = 0;
        sApplied = 0;
        sGuestSync = false;
        sGuestKnown = 0;
        sGuestPosable = (S32)sThings.size();
        sGuestFirstAt = 0;
        sGuestFullLogged = false;
        char scene[5];
        SceneText(s->sceneID, scene);
        printf("tssm: coop: level %s shares %d things (%d enemies, %d movers, %d breakables), table %08x\n", scene,
               (int)sThings.size(), (int)sEnemies, (int)sMovers, (int)sBreakables, (unsigned)sTableHash);
        if (iCoopLogEvery() > 0)
        {
            // For tests: where the enemies start (tools/coop_test.py places
            // the players by them).
            S32 listed = 0;
            for (size_t i = 0; i < sThings.size() && listed < 400; i++)
            {
                const Thing& t = sThings[i];
                if (t.kind == K_BREAKABLE)
                {
                    continue;
                }
                listed++;
                const xVec3& p = *(const xVec3*)&t.ent->model->Mat->pos;
                if (t.kind == K_ENEMY)
                {
                    printf("tssm: coop: thing %d enemy type %u id %08x at %.2f %.2f %.2f%s\n", (int)i,
                           (unsigned)static_cast<zNMENPCWrapper*>(t.ent)->SelfType(), (unsigned)t.id, p.x, p.y, p.z,
                           (t.ent->baseFlags & 0x1) ? "" : " (disabled)");
                }
                else
                {
                    // For tests that look for a platform to ride: how it moves
                    // (its xEntMotion type, stopped or not) and the top of its
                    // bound.
                    S32 motion = -1, stopped = 0;
                    if (t.ent->baseType == eBaseTypePlatform)
                    {
                        const zPlatform* pl = (const zPlatform*)t.ent;
                        motion = pl->motion.type;
                        stopped = xEntMotionIsStopped(&pl->motion) ? 1 : 0;
                    }
                    xBox bb;
                    xBoundGetBox(bb, t.ent->bound);
                    F32 top = t.ent->bound.type != XBOUND_TYPE_NA ? bb.upper.y : p.y;
                    printf("tssm: coop: thing %d mover type %u sub %u id %08x at %.2f %.2f %.2f motion %d%s top "
                           "%.2f%s\n",
                           (int)i, (unsigned)t.ent->baseType, (unsigned)t.ent->subType, (unsigned)t.id, p.x, p.y,
                           p.z, (int)motion, stopped ? " stopped" : "", top,
                           (t.ent->baseFlags & 0x1) ? "" : " (disabled)");
                }
            }
        }
        fflush(stdout);
    }

    bool TableCurrent()
    {
        zScene* s = globals.sceneCur;
        return sTableValid && s != NULL && s == sTableScene && s->sceneID == sTableSceneId;
    }

    S32 Lookup(const xBase* b)
    {
        size_t lo = 0, hi = sByPtr.size();
        while (lo < hi)
        {
            size_t mid = (lo + hi) / 2;
            if (sByPtr[mid].first < b)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < sByPtr.size() && sByPtr[lo].first == b)
        {
            return sByPtr[lo].second;
        }
        return -1;
    }

    // --- capturing and comparing states -------------------------------------------------------

    void Capture(const Thing& t, WState* w)
    {
        memset(w, 0, sizeof(*w));
        const xEnt* e = t.ent;
        U8 flags = 0;
        if (e->flags & 0x1)
        {
            flags |= WF_SHOWN;
        }
        if (e->baseFlags & 0x1)
        {
            flags |= WF_ENABLED;
        }
        if (t.dest != NULL && !t.dest->active)
        {
            flags |= WF_BROKEN;
        }
        if (t.kind == K_ENEMY)
        {
            flags |= (U8)((sHost[&t - &sThings[0]].deaths & 3) << WF_DEATHS_SHIFT);
        }
        w->flags = flags;
        w->have = SE_FLAGS;
        if (t.kind == K_BREAKABLE)
        {
            return;
        }
        const xModelInstance* m = e->model;
        if (m == NULL || m->Mat == NULL)
        {
            return;
        }
        const xMat4x3& mat = *(const xMat4x3*)m->Mat;
        if (Finite(mat.pos.x) && Finite(mat.pos.y) && Finite(mat.pos.z))
        {
            w->pos[0] = mat.pos.x;
            w->pos[1] = mat.pos.y;
            w->pos[2] = mat.pos.z;
            w->have |= SE_POS;
        }
        F32 r[3][3], len[3];
        bool mirror;
        RowsOf(mat, r, len, &mirror);
        QuatFromRows(r, w->rot);
        w->have |= SE_ROT;
        F32 a = m->Alpha * 255.0f + 0.5f;
        w->alpha = (U8)(a >= 255.0f ? 255 : (a > 0.0f ? a : 0));
        w->have |= SE_ALPHA;
        if (t.hasAnim && m->Anim->Single->State != NULL)
        {
            HostThing& h = sHost[&t - &sThings[0]];
            w->anim = m->Anim->Single->State->ID;
            h.info = InfoOf(m->Anim->Single->State);
            F32 time = m->Anim->Single->Time;
            w->t = (time == time && time >= 0.0f) ? (time > kAnimTimeLimit ? kAnimTimeLimit : time) : 0.0f;
            w->have |= SE_ANIM;
            // Not running since the last capture (a frame or more ago): the
            // thing is not updated (culled, disabled) or its state has stopped.
            if (h.havePrev && h.prevAnim == w->anim && h.prevT == w->t)
            {
                w->flags |= WF_STILL;
            }
            h.havePrev = true;
            h.prevAnim = w->anim;
            h.prevT = w->t;
        }
    }

    // The fields of `cur` the guest does not have right in `bel` at host
    // time `nowMs` (`ai`: cur's animation state).
    U8 Diff(const WState& cur, const WState& bel, U8 fields, const AnimInfo& ai, S64 nowMs)
    {
        U8 mask = 0;
        U8 want = (U8)(fields & cur.have);
        U8 unknown = (U8)(want & ~bel.have);
        mask |= unknown;
        U8 both = (U8)(want & bel.have);
        if (both & SE_POS)
        {
            for (int i = 0; i < 3; i++)
            {
                F32 d = cur.pos[i] - bel.pos[i];
                if (d > 0.002f || d < -0.002f)
                {
                    mask |= SE_POS;
                }
            }
        }
        if (both & SE_ROT)
        {
            for (int i = 0; i < 4; i++)
            {
                S32 d = (S32)cur.rot[i] - (S32)bel.rot[i];
                if (d > 2 || d < -2)
                {
                    mask |= SE_ROT;
                }
            }
        }
        if (both & SE_ANIM)
        {
            // A new state, or the guest's running time off by more than 3 frames.
            if (cur.anim != bel.anim ||
                AnimGap(cur.t, PredictOf(bel, (F32)(nowMs - bel.tMs) / 1000.0f, ai), ai) > 0.05f)
            {
                mask |= SE_ANIM;
            }
        }
        if ((both & SE_FLAGS) && cur.flags != bel.flags)
        {
            mask |= SE_FLAGS;
        }
        if ((both & SE_ALPHA) && cur.alpha != bel.alpha)
        {
            mask |= SE_ALPHA;
        }
        return mask;
    }

    // `atMs`: the host time the snapshot carrying these fields was made.
    void Overwrite(WState* bel, const WState& cur, U8 mask, S64 atMs)
    {
        if (mask & SE_POS)
        {
            memcpy(bel->pos, cur.pos, sizeof(bel->pos));
        }
        if (mask & SE_ROT)
        {
            memcpy(bel->rot, cur.rot, sizeof(bel->rot));
        }
        if (mask & SE_ANIM)
        {
            bel->anim = cur.anim;
            bel->t = cur.t;
            bel->tMs = atMs;
        }
        if (mask & SE_FLAGS)
        {
            bel->flags = cur.flags;
        }
        if (mask & SE_ALPHA)
        {
            bel->alpha = cur.alpha;
        }
        bel->have |= mask;
    }

    bool SameState(const WState& a, const WState& b)
    {
        return a.have == b.have && !memcmp(a.pos, b.pos, sizeof(a.pos)) && !memcmp(a.rot, b.rot, sizeof(a.rot)) &&
               a.anim == b.anim && a.t == b.t && a.tMs == b.tMs && a.flags == b.flags && a.alpha == b.alpha;
    }

    Slot* FindSlot(U32 seq)
    {
        if (seq == 0)
        {
            return NULL;
        }
        Slot& s = sRing[seq % kRing];
        return (s.seq == seq && s.st.size() == sThings.size()) ? &s : NULL;
    }

    // --- the host: the partner's position (5.4) --------------------------------------------------

    void HostPartner(U64 now, const PlayerState* p, U64 at)
    {
        sPartnerCullOk = false;
        sPartnerTargetOk = false;
        // The guest is in this level while its newest state says so and is
        // fresh, or while its game reports a stall (a held window: it is
        // still there, and comes back to what it had, 5.11).
        sPartnerInLevel = p != NULL && p->scene == sTableSceneId && (AgeSince(now, at) < kSyncStale || sPartnerStalled);
        sPartnerSending = sPartnerInLevel && iCoopAge(now, at) < kSendFresh;
        if (p == NULL || AgeSince(now, at) > kTargetFresh || p->scene != sTableSceneId)
        {
            sPartnerHaveLast = false;
            return;
        }
        xVec3 pos = { p->pos[0], p->pos[1], p->pos[2] };
        if (p->frame != sPartnerFrame || !sPartnerHaveLast)
        {
            bool dead = (p->flags & PS_DEAD) != 0;
            if (sPartnerHaveLast)
            {
                F32 dt = (F32)((F64)(at - sPartnerLastAt) / 1e9);
                F32 d = sqrtf(Dist2(pos, sPartnerLast));
                // The jumps co-op makes itself (the guest put beside this
                // player: a respawn, after this player's defeat, on arrival)
                // land beside this player, which a cheat gains nothing by;
                // a defeat's own short jump as it starts is believed once
                // every 2 s. Neither counts as a teleport.
                xEnt* me = Player();
                bool beside =
                    me != NULL && me->model != NULL &&
                    Dist2(pos, *(const xVec3*)&me->model->Mat->pos) < kBesideHost * kBesideHost;
                bool defeat = dead && !sPartnerLastDead && d <= kDefeatJump &&
                              (sLastDeadJump == 0 || now - sLastDeadJump >= kTeleportEvery);
                if (d > kMaxSpeed * (dt > 0.0f ? dt : 0.0f) + 3.0f && (beside || defeat))
                {
                    if (!beside)
                    {
                        sLastDeadJump = now;
                    }
                }
                else if (d > kMaxSpeed * (dt > 0.0f ? dt : 0.0f) + 3.0f)
                {
                    // A teleport: a checkpoint, a respawn, a bus stop. One
                    // every 2 s is believed; more are ignored for 2 s.
                    if (sLastTeleport == 0 || now - sLastTeleport >= kTeleportEvery)
                    {
                        sLastTeleport = now;
                    }
                    else
                    {
                        sSuspectUntil = now + kTeleportEvery;
                        if (sLoggedSuspect < 8)
                        {
                            sLoggedSuspect++;
                            printf("tssm: coop: the guest moved faster than a player can; its position is "
                                   "ignored for 2 s\n");
                            fflush(stdout);
                        }
                    }
                }
            }
            sPartnerLast = pos;
            sPartnerLastAt = at;
            sPartnerFrame = p->frame;
            sPartnerHaveLast = true;
            sPartnerLastDead = dead;
        }
        if (now < sSuspectUntil)
        {
            return;
        }
        sPartnerPos = pos;
        sPartnerVel.x = p->vel[0] / 100.0f;
        sPartnerVel.y = p->vel[1] / 100.0f;
        sPartnerVel.z = p->vel[2] / 100.0f;
        sPartnerHealth = p->health;
        sPartnerCullOk = true;
        sPartnerTargetOk = (p->flags & PS_VISIBLE) && !(p->flags & (PS_SPECIAL | PS_PAUSED | PS_CUTSCENE | PS_DEAD)) &&
                           p->health > 0;
    }

    // --- the host: snapshots (5.3) ------------------------------------------------------------------

    // TSSM_COOP_LOG: the enemies and movers to log, the nearest to `center`
    // (the guest's place on both games, so both log the same ones) first.
    std::vector<std::pair<F32, U16> > sWatch;

    void WatchList(const xVec3& center, const std::vector<WState>* cur)
    {
        sWatch.clear();
        for (size_t i = 0; i < sThings.size(); i++)
        {
            const Thing& th = sThings[i];
            if (th.kind == K_BREAKABLE || th.ent->model == NULL)
            {
                continue;
            }
            const F32* p = cur != NULL ? (*cur)[i].pos : &th.ent->model->Mat->pos.x;
            F32 d = Dist2(center, p);
            if (d <= kLogRadius * kLogRadius)
            {
                sWatch.push_back(std::make_pair(d, (U16)i));
            }
        }
        std::sort(sWatch.begin(), sWatch.end());
        if (sWatch.size() > (size_t)kLogMax)
        {
            sWatch.resize(kLogMax);
        }
    }

    struct Candidate
    {
        U16 idx;
        U8 mask;
        F32 key; // smaller goes first
    };
    std::vector<Candidate> sCand;
    std::vector<WState> sCur;

    // How often a thing that changed goes out, by its distance to the nearer
    // player: every snapshot within the game's own update distance (70 units:
    // what a player can see moving), every 2nd up to twice that, every 4th
    // beyond.
    S32 TierPeriod(F32 d2)
    {
        if (d2 < 70.0f * 70.0f)
        {
            return 1;
        }
        return d2 < 140.0f * 140.0f ? 2 : 4;
    }

    const Snapshot* HostSnapshot(U64 now, bool partnerHere, bool sending)
    {
        if (TableCurrent() && partnerHere && !sending)
        {
            // The guest's game is stalled (no state for a moment, but in this
            // level): nothing goes out, and what it has is kept, so when it
            // runs again the next snapshot is a small one against the last it
            // acknowledged (5.11).
            if (!sSendPausedLogged && iCoopLogEvery() > 0)
            {
                sSendPausedLogged = true;
                printf("tssm: coop: snapshots paused (no state from the guest for a moment)\n");
                fflush(stdout);
            }
            return NULL;
        }
        if (sSendPausedLogged && sending)
        {
            sSendPausedLogged = false;
            printf("tssm: coop: snapshots resumed (base %u, %u since it)\n", (unsigned)sAcked,
                   (unsigned)(sSeq - sAcked));
            fflush(stdout);
        }
        if (!TableCurrent() || !partnerHere)
        {
            // The guest is elsewhere: when it comes back it starts over.
            if (sAcked != 0 || sHostStarted)
            {
                sHostStarted = false;
                sAcked = 0;
                for (S32 i = 0; i < kRing; i++)
                {
                    sRing[i].seq = 0;
                }
                for (size_t i = 0; i < sHost.size(); i++)
                {
                    sHost[i].lastSentSeq = 0;
                }
            }
            return NULL;
        }
        U32 ack;
        if (iCoopTakeSnapAck(&ack))
        {
            // Only an acknowledgement of something this host sent counts.
            if (sSeq != 0 && !frame_newer(ack, sSeq) && (sAcked == 0 || frame_newer(ack, sAcked)) &&
                FindSlot(ack) != NULL)
            {
                sAcked = ack;
            }
        }
        const Slot* base = FindSlot(sAcked);
        // The next snapshot (sSeq + 1) may be at most kSnapBaseWindow past its
        // base (the parser's rule; the ring, smaller, usually ends it first).
        if (base != NULL && sSeq - sAcked >= kSnapBaseWindow)
        {
            base = NULL;
        }
        size_t n = sThings.size();
        sCur.resize(n);
        sLastSent.resize(n);
        sCand.clear();
        xEnt* pl = Player();
        xVec3 hostPos = pl != NULL && pl->model != NULL ? *(xVec3*)&pl->model->Mat->pos : xVec3();
        S64 nowMs = NowMs(now);
        U32 seqNext = sSeq + 1 == 0 ? 1 : sSeq + 1;
        bool tierSkipped = false;
        for (size_t i = 0; i < n; i++)
        {
            sHost[i].info.ok = false;
            Capture(sThings[i], &sCur[i]);
            WState none;
            memset(&none, 0, sizeof(none));
            const WState& bel = base != NULL ? base->st[i] : none;
            const Thing& th = sThings[i];
            U8 mask = Diff(sCur[i], bel, th.fields, sHost[i].info, nowMs);
            // Sent since the base, not acknowledged yet: the guest may be
            // showing that newer value, so what differs from it goes too,
            // even when the thing is back where the base had it.
            bool unacked = sHost[i].lastSentSeq != 0 && (base == NULL || frame_newer(sHost[i].lastSentSeq, sAcked));
            if (unacked)
            {
                mask |= Diff(sCur[i], sLastSent[i], th.fields, sHost[i].info, nowMs);
            }
            if (mask == 0)
            {
                continue;
            }
            if (!(bel.have & SE_FLAGS))
            {
                mask = (U8)(th.fields & sCur[i].have); // first time: in full
            }
            const F32* p = sCur[i].pos;
            F32 d1 = Dist2(hostPos, p);
            F32 d2 = sPartnerInLevel ? Dist2(sPartnerPos, p) : d1;
            F32 dmin = d1 < d2 ? d1 : d2;
            bool known = (bel.have & SE_FLAGS) != 0;
            // Far things that move go out less often (they are drawn between
            // what arrives); what the guest has never had, and breaks, go at once.
            S32 period = TierPeriod(dmin);
            if (known && th.kind != K_BREAKABLE && period > 1 && (seqNext + (U32)i) % (U32)period != 0)
            {
                tierSkipped = true;
                continue;
            }
            // Nearest to either player first; what has waited longer moves
            // up, so far things are not starved when there is little room.
            Candidate c;
            c.idx = (U16)i;
            c.mask = mask;
            U32 age = sHost[i].lastSentSeq != 0 ? sSeq - sHost[i].lastSentSeq : 1000;
            age = age > 100 ? 100 : age;
            c.key = sqrtf(dmin) / (1.0f + 0.25f * (F32)age);
            if (th.kind == K_BREAKABLE)
            {
                c.key = 0.0f; // a few bytes, and a break should show at once
            }
            sCand.push_back(c);
        }
        std::sort(sCand.begin(), sCand.end(), [](const Candidate& a, const Candidate& b) { return a.key < b.key; });

        U32 seq = sSeq + 1;
        if (seq == 0)
        {
            seq = 1;
        }
        Snapshot& out = sOutSnap;
        out.scene = sTableSceneId;
        out.table = sTableHash;
        out.things = (U16)n;
        out.seq = seq;
        out.base = base != NULL ? sAcked : 0;
        out.host_ms = (U32)NowMs(now);
        out.count = 0;
        bool complete = !tierSkipped; // informational: the guest moves only what a snapshot carries
        size_t body = kSnapHeaderSize;
        // The new belief: the base's, with what goes in now.
        Slot& slot = sRing[seq % kRing];
        if (base != NULL)
        {
            sScratch = base->st;
        }
        else
        {
            sScratch.assign(n, WState());
        }
        for (size_t k = 0; k < sCand.size(); k++)
        {
            const Candidate& c = sCand[k];
            size_t sz = snap_entry_size(c.mask);
            if (out.count >= kMaxSnapEntries || body + sz + sSnapReserve > kSnapBodyMax)
            {
                complete = false;
                continue;
            }
            body += sz;
            SnapEntry& e = out.e[out.count++];
            memset(&e, 0, sizeof(e));
            e.id = c.idx;
            e.mask = c.mask;
            const WState& w = sCur[c.idx];
            memcpy(e.pos, w.pos, sizeof(e.pos));
            memcpy(e.rot, w.rot, sizeof(e.rot));
            e.anim = w.anim;
            e.anim_time = w.t;
            e.flags = w.flags;
            e.alpha = w.alpha;
            Overwrite(&sScratch[c.idx], w, c.mask, nowMs);
            sHost[c.idx].lastSentSeq = seq;
            sLastSent[c.idx] = sScratch[c.idx];
        }
        // Entries must be in ascending ID order.
        std::sort(out.e, out.e + out.count, [](const SnapEntry& a, const SnapEntry& b) { return a.id < b.id; });
        out.flags = complete ? SNAP_COMPLETE : 0;
        slot.st.swap(sScratch);
        slot.seq = seq;
        slot.hostMs = (S64)out.host_ms;
        sSeq = seq;
        sHostStarted = true;
        sDirty = true;

        S32 every = iCoopLogEvery();
        if (every > 0)
        {
            WatchList(sPartnerInLevel ? sPartnerPos : hostPos, &sCur);
            for (size_t k = 0; k < sWatch.size(); k++)
            {
                size_t i = sWatch[k].second;
                const F32* p = sCur[i].pos;
                printf("tssm: coop: world sent ms %u thing %d pos %.3f %.3f %.3f anim %08x flags %u target %u\n",
                       (unsigned)out.host_ms, (int)i, p[0], p[1], p[2], (unsigned)sCur[i].anim,
                       (unsigned)sCur[i].flags, (unsigned)sHost[i].target);
            }
            if (sSeq % 150 == 1)
            {
                // What changed, by kind and field (enemies, movers, breakables
                // x position, rotation, animation, flags, fade), of everything
                // that differed from the base.
                S32 by[3][5] = {};
                for (size_t k = 0; k < sCand.size(); k++)
                {
                    S32 kind = sThings[sCand[k].idx].kind - 1;
                    for (S32 b = 0; b < 5; b++)
                    {
                        if (sCand[k].mask & (1 << b))
                        {
                            by[kind][b]++;
                        }
                    }
                }
                S32 listedMoving = 0;
                for (size_t k = 0; k < sCand.size() && listedMoving < 8; k++)
                {
                    const Thing& th = sThings[sCand[k].idx];
                    if (th.kind == K_MOVER && (sCand[k].mask & SE_POS))
                    {
                        listedMoving++;
                        const F32* p = sCur[sCand[k].idx].pos;
                        printf("tssm: coop: world moving mover thing %d sub %u at %.2f %.2f %.2f\n", (int)sCand[k].idx,
                               (unsigned)th.ent->subType, p[0], p[1], p[2]);
                    }
                }
                printf("tssm: coop: world snapshot %u: %d entries, %d bytes, base %u%s; changed: enemies %d/%d/%d/%d/%d "
                       "movers %d/%d/%d/%d/%d breakables %d\n",
                       (unsigned)seq, (int)out.count, (int)body, (unsigned)out.base, complete ? "" : " (not complete)",
                       by[0][0], by[0][1], by[0][2], by[0][3], by[0][4], by[1][0], by[1][1], by[1][2], by[1][3],
                       by[1][4], by[2][3]);
            }
        }
        return &out;
    }

    // --- the guest: snapshots in (5.3, 5.5) --------------------------------------------------------

    // Where the two players are on this game (the guest's own, and the
    // host's from its newest state), for PushSample: near one of them, a
    // thing goes out in every snapshot it changes in (TierPeriod).
    xVec3 sNearRef[2];
    S32 sNearRefCount;

    bool NearAPlayer(const F32* p)
    {
        for (S32 i = 0; i < sNearRefCount; i++)
        {
            // Inside TierPeriod's 70, with room for the host's position
            // being a moment old here.
            if (Dist2(sNearRef[i], p) < 60.0f * 60.0f)
            {
                return true;
            }
        }
        return false;
    }

    void PushSample(GuestThing& g, S64 hostMs, const WState& w)
    {
        if (g.count > 0 && g.smp[g.count - 1].hostMs >= hostMs)
        {
            return;
        }
        // A thing nothing came for in the previous snapshot was still where
        // its last sample has it at that snapshot's moment: hold it there, so
        // it does not slide from long ago when it starts moving (which would
        // draw it ahead of the host). Near a player that holds whenever the
        // previous snapshot left it out (it would have gone out had it
        // changed); farther off, where things go out only every second or
        // fourth snapshot, only after it stood still a while.
        if (g.count > 0 && sAppliedHostMs > g.smp[g.count - 1].hostMs && sAppliedHostMs < hostMs &&
            (hostMs - g.smp[g.count - 1].hostMs > 400 || NearAPlayer(g.smp[g.count - 1].s.pos)))
        {
            Sample hold = g.smp[g.count - 1];
            hold.hostMs = sAppliedHostMs;
            if (g.count == kSamples)
            {
                memmove(g.smp, g.smp + 1, sizeof(Sample) * (kSamples - 1));
                g.count--;
            }
            g.smp[g.count++] = hold;
        }
        if (g.count == kSamples)
        {
            memmove(g.smp, g.smp + 1, sizeof(Sample) * (kSamples - 1));
            g.count--;
        }
        g.smp[g.count].hostMs = hostMs;
        g.smp[g.count].s = w;
        g.count++;
    }

    void GuestApply(const Snapshot& sn, U64 at)
    {
        if (sn.scene != sTableSceneId || sn.table != sTableHash || sn.things != sThings.size())
        {
            sMismatch++;
            if (!sMismatchLogged)
            {
                sMismatchLogged = true;
                printf("tssm: coop: the host's level is not this level as this game has it (table %08x, %u "
                       "things; here %08x, %u); its snapshots are ignored\n",
                       (unsigned)sn.table, (unsigned)sn.things, (unsigned)sTableHash, (unsigned)sThings.size());
                fflush(stdout);
            }
            return;
        }
        if (sApplied != 0 && !frame_newer(sn.seq, sApplied))
        {
            return; // older than what is shown: newest wins
        }
        const Slot* base = NULL;
        if (sn.base != 0)
        {
            base = FindSlot(sn.base);
            if (base == NULL)
            {
                sMissingBase++; // the host falls back to an older base, or none
                return;
            }
        }
        size_t n = sThings.size();
        if (base != NULL)
        {
            sScratch = base->st;
        }
        else
        {
            sScratch.assign(n, WState());
        }
        // The host's clock, unwrapped, and how far behind ours it arrives.
        if (!sHaveHostClock)
        {
            sHaveHostClock = true;
            sHostRaw = sn.host_ms;
            sHostMs = sn.host_ms;
        }
        else
        {
            sHostMs += (S32)(sn.host_ms - sHostRaw);
            sHostRaw = sn.host_ms;
        }
        // A stall between the last snapshot applied and this one (5.11): the
        // host's game was stopped (it said so), or this game was (its thread
        // took nothing in), or the link was. What this snapshot carries is
        // shown at once, not slid to from the place before the gap; what it
        // does not carry stays where it is. After the host's own stall its
        // animations were held, as the guest held them (Interp): they go on
        // from there.
        S64 gapMs = sApplied != 0 ? sHostMs - sAppliedHostMs : 0;
        bool gap = gapMs > kGapMs;
        if (gap)
        {
            // The animations were held at the old newest + kAnimHoldMs; the
            // first frame after this one draws the host's time
            // kInterpDelayMs before this snapshot.
            S64 shift = sHostStallSeen ? gapMs - kAnimHoldMs - kInterpDelayMs : 0;
            for (size_t i = 0; i < sGuest.size(); i++)
            {
                GuestThing& g = sGuest[i];
                if (g.count > 1)
                {
                    g.smp[0] = g.smp[g.count - 1];
                    g.count = 1;
                }
                if (g.count == 1 && shift > 0)
                {
                    g.smp[0].s.tMs += shift;
                }
            }
            if (sGapsLogged < 32)
            {
                sGapsLogged++;
                printf("tssm: coop: the host's snapshots go on after a gap of %d ms (%s); its newest is shown\n",
                       (int)gapMs, sHostStallSeen ? "its game was stalled" : "a stall here or on the link");
                fflush(stdout);
            }
        }
        sHostStallSeen = false;
        std::fill(sIncluded.begin(), sIncluded.end(), 0);
        for (unsigned k = 0; k < sn.count; k++)
        {
            const SnapEntry& e = sn.e[k];
            if (e.id >= n) // the parser checked it against sn.things, which is n
            {
                continue;
            }
            WState w;
            memset(&w, 0, sizeof(w));
            memcpy(w.pos, e.pos, sizeof(w.pos));
            memcpy(w.rot, e.rot, sizeof(w.rot));
            w.anim = e.anim;
            w.t = e.anim_time;
            w.flags = e.flags;
            w.alpha = e.alpha;
            Overwrite(&sScratch[e.id], w, (U8)(e.mask & sThings[e.id].fields), sHostMs);
            sIncluded[e.id] = 1;
        }
        Slot& slot = sRing[sn.seq % kRing];
        slot.st.swap(sScratch);
        slot.seq = sn.seq;
        slot.hostMs = sHostMs;
        sOffsets[sOffsetNext] = NowMs(at) - sHostMs;
        sOffsetNext = (sOffsetNext + 1) % kOffsets;
        if (sOffsetCount < kOffsets)
        {
            sOffsetCount++;
        }

        // Only what this snapshot carried moves on: a thing it left out (for
        // room, or because it is far and goes out less often) keeps what is
        // shown. The host sends a thing again whenever the guest may be
        // showing something else than it is (iCoopWorld's HostSnapshot).
        for (size_t i = 0; i < n; i++)
        {
            const WState& w = slot.st[i];
            const Thing& t = sThings[i];
            if (!sGuest[i].heard && (w.have & SE_FLAGS))
            {
                // Its first state from the host (a thing the host has out of
                // the world, like a tiki fallen through the floor, has no
                // position: it is known, and keeps still).
                sGuest[i].heard = true;
                sGuestKnown++;
            }
            // Known in full (the animation only when the host's copy has one
            // playing: it goes out when it starts).
            U8 need = (U8)(t.fields & ~SE_ANIM);
            if ((w.have & need) != need)
            {
                continue; // not known in full yet
            }
            if (!sIncluded[i])
            {
                continue;
            }
            GuestThing& g = sGuest[i];
            if (g.count > 0 && SameState(g.smp[g.count - 1].s, w))
            {
                continue;
            }
            if (gap)
            {
                g.count = 0; // the newer state at once (above)
            }
            PushSample(g, sHostMs, w);
        }
        sAppliedHostMs = sHostMs;
        sApplied = sn.seq;
        sAppliedAt = at;
        if (sGuestFirstAt == 0)
        {
            sGuestFirstAt = at;
        }
        sDirty = true;

        S32 every = iCoopLogEvery();
        if (every > 0)
        {
            xEnt* pl = Player();
            if (pl != NULL && pl->model != NULL)
            {
                WatchList(*(xVec3*)&pl->model->Mat->pos, NULL);
                for (unsigned k = 0; k < sn.count; k++)
                {
                    const SnapEntry& e = sn.e[k];
                    if (e.id >= n || !(e.mask & SE_POS))
                    {
                        continue;
                    }
                    for (size_t j = 0; j < sWatch.size(); j++)
                    {
                        if (sWatch[j].second == e.id)
                        {
                            printf("tssm: coop: world got ms %u thing %u pos %.3f %.3f %.3f\n", (unsigned)sn.host_ms,
                                   (unsigned)e.id, e.pos[0], e.pos[1], e.pos[2]);
                        }
                    }
                }
            }
        }
    }

    bool InterpPose(const GuestThing& g, S64 t, WState* out);

    // This game's animation state for `anim` on thing `th` (cached per thing).
    const AnimInfo& GuestInfo(const Thing& th, GuestThing& g, U32 anim)
    {
        if (g.infoAnim != anim || !g.info.ok)
        {
            g.infoAnim = anim;
            g.info.ok = false;
            xModelInstance* m = th.ent->model;
            if (th.hasAnim && m != NULL && m->Anim != NULL && m->Anim->Table != NULL)
            {
                g.info = InfoOf(xAnimTableGetStateID(m->Anim->Table, anim));
            }
        }
        return g.info;
    }

    // Where a thing is drawn at the host's time `t`.
    bool Interp(const Thing& th, GuestThing& g, S64 t, WState* out)
    {
        if (!InterpPose(g, t, out))
        {
            return false;
        }
        // The animation runs on from the newest time the host sent before
        // `t`, as it does on the host.
        if (th.hasAnim)
        {
            S32 a = 0;
            for (S32 i = g.count - 1; i >= 0; i--)
            {
                if (g.smp[i].hostMs <= t)
                {
                    a = i;
                    break;
                }
            }
            const WState& A = g.smp[a].s;
            out->anim = A.anim;
            out->have = (U8)((out->have & ~SE_ANIM) | (A.have & SE_ANIM));
            if (A.have & SE_ANIM)
            {
                // Well past the newest snapshot, the host's game is stalled (or
                // the link is): its animations are not running, so these hold
                // too, and go on from there (GuestApply, 5.11).
                S64 ta = t;
                if (sApplied != 0 && ta > sAppliedHostMs + kAnimHoldMs)
                {
                    ta = sAppliedHostMs + kAnimHoldMs;
                }
                out->t = PredictOf(A, (F32)(ta - A.tMs) / 1000.0f, GuestInfo(th, g, A.anim));
            }
        }
        return true;
    }

    // Position, rotation, flags and fade at the host's time `t`.
    bool InterpPose(const GuestThing& g, S64 t, WState* out)
    {
        if (g.count == 0)
        {
            return false;
        }
        S32 a = -1;
        for (S32 i = g.count - 1; i >= 0; i--)
        {
            if (g.smp[i].hostMs <= t)
            {
                a = i;
                break;
            }
        }
        if (a < 0)
        {
            *out = g.smp[0].s;
            return true;
        }
        if (a == g.count - 1)
        {
            *out = g.smp[a].s;
            return true;
        }
        const Sample& A = g.smp[a];
        const Sample& B = g.smp[a + 1];
        F32 f = B.hostMs > A.hostMs ? (F32)(t - A.hostMs) / (F32)(B.hostMs - A.hostMs) : 1.0f;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        *out = f < 0.5f ? A.s : B.s; // flags, fade
        F32 dx = B.s.pos[0] - A.s.pos[0], dy = B.s.pos[1] - A.s.pos[1], dz = B.s.pos[2] - A.s.pos[2];
        F32 d2 = dx * dx + dy * dy + dz * dz;
        F32 reach = kSnapSpeed * (F32)(B.hostMs - A.hostMs) / 1000.0f;
        // A death (an enemy put back at its spawn point, often less than
        // kSnapDist away and slow enough to pass for a step), a break, or a
        // thing shown or hidden between the two: what moved was the game
        // putting it somewhere, not a walk, so it is not slid either.
        bool state = ((A.s.flags ^ B.s.flags) & (WF_DEATHS | WF_BROKEN | WF_SHOWN)) != 0 &&
                     (A.s.have & SE_FLAGS) && (B.s.have & SE_FLAGS);
        if (state || d2 > kSnapDist * kSnapDist || (d2 > 1.0f && d2 > reach * reach))
        {
            *out = A.s; // a teleport: no slide
            return true;
        }
        for (int k = 0; k < 3; k++)
        {
            out->pos[k] = A.s.pos[k] + (B.s.pos[k] - A.s.pos[k]) * f;
        }
        // Rotation: along the short arc (normalised linear blend).
        F32 qa[4], qb[4];
        F32 dot = 0.0f;
        for (int k = 0; k < 4; k++)
        {
            qa[k] = A.s.rot[k];
            qb[k] = B.s.rot[k];
            dot += qa[k] * qb[k];
        }
        F32 sign = dot < 0.0f ? -1.0f : 1.0f;
        F32 q[4];
        F32 len = 0.0f;
        for (int k = 0; k < 4; k++)
        {
            q[k] = qa[k] + (qb[k] * sign - qa[k]) * f;
            len += q[k] * q[k];
        }
        len = sqrtf(len);
        if (len > 1.0f)
        {
            for (int k = 0; k < 4; k++)
            {
                F32 v = floorf(q[k] / len * 32767.0f + 0.5f);
                out->rot[k] = (S16)(v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v));
            }
        }
        return true;
    }

    // --- the guest: posing a host-owned thing (5.5) ----------------------------------------------

    void SetAnim(xModelInstance* m, U32 anim, F32 t)
    {
        xAnimPlay* play = m->Anim;
        if (play == NULL || play->Table == NULL || play->Single == NULL)
        {
            return;
        }
        xAnimSingle* single = play->Single;
        if (single->State == NULL || single->State->ID != anim)
        {
            xAnimState* st = xAnimTableGetStateID(play->Table, anim);
            if (st != NULL)
            {
                xAnimPlaySetState(single, st, t);
            }
            else if (sUnknownAnimLogged < 8)
            {
                sUnknownAnimLogged++;
                printf("tssm: coop: a shared thing has an unknown animation state %08x; it keeps its last pose\n",
                       (unsigned)anim);
            }
        }
        if (single->State != NULL && single->State->ID == anim)
        {
            if (single->State->Data != NULL && t > single->State->Data->Duration)
            {
                t = single->State->Data->Duration;
            }
            single->Time = t < 0.0f ? 0.0f : t;
        }
    }

    void ApplyFlags(const Thing& th, GuestThing& g, const WState& w)
    {
        xEnt* e = th.ent;
        if (w.flags & WF_ENABLED)
        {
            e->baseFlags |= 0x1;
        }
        else
        {
            e->baseFlags &= (U16)~0x1;
        }
        bool shown = (w.flags & WF_SHOWN) != 0;
        if (shown != ((e->flags & 0x1) != 0) || !g.shownSet)
        {
            if (shown)
            {
                xEntShow(e);
            }
            else
            {
                xEntHide(e);
            }
            g.shownSet = true;
        }
    }

    bool TikiType(xEnt* e)
    {
        U32 t = (U32) static_cast<zNMENPCWrapper*>(e)->SelfType();
        return t >= NME_TYPE_TIKI && t <= NME_TYPE_TIKI_STONE;
    }

    // A crate's own break, as zNMEGoalTikiDying::Exit plays it (its rumble is
    // the host player's).
    void TikiBreakFx(xEnt* e)
    {
        zNMETiki* tiki = (zNMETiki*)static_cast<zNMECommon*>(e);
        zNMETiki_ExplodeFX(tiki);
        zShrapnelAsset* shrap = zNMEShrapnelRetrieveShrapnel(tiki->SelfType());
        if (shrap != NULL && shrap->initCB != NULL)
        {
            xVec3 vel = {};
            shrap->initCB(shrap, tiki->model, &vel, NULL);
        }
    }

    // A copy the host shows but whose own goal left it dead (an enemy a
    // spawner brings back from its pool, which only the owner's update does;
    // one this game defeated while the host's world was not shown): its
    // collision back, as the goal's own Exit puts it, so the attacks and the
    // player find it. Its goal stays (this game runs no AI for it). Hidden by
    // the host again, it goes back as the dead goal leaves it.
    void Revive(const Thing& th, GuestThing& g, bool shown)
    {
        xEnt* e = th.ent;
        zNMECommon* npc = static_cast<zNMECommon*>(static_cast<zNMENPCWrapper*>(e));
        if (TikiType(e))
        {
            zNMETiki* tiki = (zNMETiki*)npc;
            if (shown &&
                ((tiki->tikiFlag & 0x600) || e->chkby == XENT_COLLTYPE_NONE || !tiki->flg_vuln))
            {
                tiki->tikiFlag &= ~0x600;
                tiki->RestoreColFlags();
                tiki->flg_vuln = tiki->DfltVulnFlags();
                if (e->model != NULL && e->model->Scale.x == 0.0f)
                {
                    xVec3 one = { 1.0f, 1.0f, 1.0f };
                    xModelSetScale(e->model, one);
                }
                if (!g.revived && iCoopLogEvery() > 0)
                {
                    printf(
                        "tssm: coop: thing %d: a crate this game broke is whole on the host; whole here again\n",
                        (int)(&th - &sThings[0]));
                }
                g.revived = true;
            }
            else if (!shown)
            {
                g.revived = false;
            }
            return;
        }
        xPsyche* psy = npc->psy_self;
        S32 gid = psy != NULL ? psy->GIDOfActive() : 0;
        bool deadGoal = gid == NME_GOAL_DEAD || gid == NME_GOAL_LIMBO || gid == NME_GOAL_AFTERLIFE;
        if (shown && deadGoal && !(e->moreFlags & 0x10))
        {
            U16 old = ((zNMEGoalDead*)psy->GetCurGoal())->old_moreFlags;
            npc->RestoreColFlags();
            e->moreFlags = (U16)(old | 0x10);
            if (e->model != NULL)
            {
                e->model->Flags |= 0x2 | 0x4;
            }
            npc->flg_upward &= ~0x1;
            if (!g.revived && iCoopLogEvery() > 0)
            {
                printf(
                    "tssm: coop: thing %d: shown by the host while this game's copy is in its dead goal (%08x); "
                    "its collision is back\n",
                    (int)(&th - &sThings[0]), (unsigned)gid);
            }
            g.revived = true;
        }
        else if (!shown && g.revived)
        {
            e->moreFlags = 0;
            e->pflags = 0;
            npc->flg_colCheck = 0;
            npc->flg_penCheck = 0;
            e->chkby = 0;
            e->penby = 0;
            g.revived = false;
        }
    }

    void Pose(const Thing& th, GuestThing& g, const WState& w, F32 dt)
    {
        xEnt* e = th.ent;
        xModelInstance* m = e->model;
        ApplyFlags(th, g, w);
        if (th.kind == K_ENEMY)
        {
            S8 deaths = (S8)((w.flags & WF_DEATHS) >> WF_DEATHS_SHIFT);
            U64 nowNs = iCoopNowNs();
            if (g.deaths >= 0 && deaths != g.deaths && nowNs - g.lastEffectAt >= kEffectEvery)
            {
                g.lastEffectAt = nowNs;
                // The host's enemy just played its death effect: so does this
                // copy (its own shrapnel, sound and bubbles; a crate's
                // splinters), where it is drawn.
                if (TikiType(e))
                {
                    TikiBreakFx(e);
                }
                else
                {
                    static_cast<zNMECommon*>(e)->DeathFXKick();
                }
                if (iCoopLogEvery() > 0)
                {
                    printf("tssm: coop: enemy thing %d died on the host; its death effect plays here\n",
                           (int)(&th - &sThings[0]));
                }
            }
            g.deaths = deaths;
            Revive(th, g, (w.flags & WF_SHOWN) != 0);
        }
        if (m == NULL || m->Mat == NULL)
        {
            return;
        }
        xMat4x3& mm = *(xMat4x3*)m->Mat;
        xVec3 was = mm.pos;
        F32 mine[3][3], len[3];
        bool mirror;
        RowsOf(mm, mine, len, &mirror);
        F32 q[4] = { (F32)w.rot[0], (F32)w.rot[1], (F32)w.rot[2], (F32)w.rot[3] };
        F32 r[3][3];
        RowsFromQuat(q, r);
        if (mirror)
        {
            r[0][0] = -r[0][0];
            r[0][1] = -r[0][1];
            r[0][2] = -r[0][2];
        }
        xVec3 pos = { w.pos[0], w.pos[1], w.pos[2] };
        xEntFrame* f = e->frame;
        U32 mode = 0;
        if (f != NULL)
        {
            mode = f->mode;
            f->oldmat = f->mat;
            f->oldvel = f->vel;
            f->mat.right.assign(r[0][0], r[0][1], r[0][2]);
            f->mat.up.assign(r[1][0], r[1][1], r[1][2]);
            f->mat.at.assign(r[2][0], r[2][1], r[2][2]);
            f->mat.pos = pos;
            if (dt > 1e-4f)
            {
                f->vel.x = (pos.x - f->oldmat.pos.x) / dt;
                f->vel.y = (pos.y - f->oldmat.pos.y) / dt;
                f->vel.z = (pos.z - f->oldmat.pos.z) / dt;
            }
            // xEntEndUpdate must not copy the frame (unscaled) over the model
            // matrix (scaled) set below; it still does the rest.
            f->mode = mode | 0x30000;
        }
        mm.right.assign(r[0][0] * len[0], r[0][1] * len[0], r[0][2] * len[0]);
        mm.up.assign(r[1][0] * len[1], r[1][1] * len[1], r[1][2] * len[1]);
        mm.at.assign(r[2][0] * len[2], r[2][1] * len[2], r[2][2] * len[2]);
        mm.pos = pos;
        if (th.hasAnim && (w.have & SE_ANIM))
        {
            SetAnim(m, w.anim, w.t);
        }
        if (th.kind == K_ENEMY && (w.flags & WF_SHOWN))
        {
            m->Flags |= 0x2; // evaluated, as the enemy's own update does when it is not culled
            if (th.hasAnim && (w.have & SE_ANIM) && zNMEFogger_PcBreathAnim(e, w.anim))
            {
                // The host's fogger breathes: its breath is seen here too,
                // harmless (the host sends this player the hits).
                zNMEFogger_PcBreathShown(e);
                if (sBreathLogged < 4 && iCoopLogEvery() > 0)
                {
                    sBreathLogged++;
                    printf(
                        "tssm: coop: enemy thing %d breathes on the host; its breath is drawn here\n",
                        (int)(&th - &sThings[0]));
                    fflush(stdout);
                }
            }
        }
        if (th.kind == K_ENEMY && e->bound.type == XBOUND_TYPE_BOX)
        {
            // A crate's box is made from its place only by its own update,
            // which a host-owned copy does not run: it moves with the copy
            // (a crate falling when the one under it breaks, one floating
            // along its path), so the attacks that hit what is drawn find it.
            xVec3 d = { pos.x - was.x, pos.y - was.y, pos.z - was.z };
            xBox& b = e->bound.box.box;
            b.lower.x += d.x;
            b.lower.y += d.y;
            b.lower.z += d.z;
            b.upper.x += d.x;
            b.upper.y += d.y;
            b.upper.z += d.z;
            e->bound.box.center.x += d.x;
            e->bound.box.center.y += d.y;
            e->bound.box.center.z += d.z;
        }
        // The collision models, the pose, the bound and the grid, as the
        // entity's own update ends.
        xEntEndUpdate(e, globals.sceneCur, dt);
        if (f != NULL)
        {
            f->mode = mode;
        }
        g.posed[0] = mm.pos.x;
        g.posed[1] = mm.pos.y;
        g.posed[2] = mm.pos.z;
        g.posedFrame = sFrameNo;
    }

    // Breakables (and movers with a destructible): the host's breaks and
    // respawns, as edges (5.5).
    void GuestBreakables()
    {
        const Slot* s = FindSlot(sApplied);
        if (s == NULL)
        {
            return;
        }
        for (size_t i = 0; i < sThings.size(); i++)
        {
            const Thing& th = sThings[i];
            if (th.dest == NULL)
            {
                continue;
            }
            GuestThing& g = sGuest[i];
            if (!(s->st[i].have & SE_FLAGS))
            {
                continue;
            }
            S8 broken = (s->st[i].flags & WF_BROKEN) ? 1 : 0;
            // Broken on the host is broken here, even after this game put it
            // back (the guest's own level reset); the host's coming back is
            // an edge, so one the guest broke itself stays broken.
            if (broken == g.hostBroken && !(broken && th.dest->active))
            {
                continue;
            }
            bool first = g.hostBroken < 0;
            U64 nowNs = iCoopNowNs();
            if (!first && nowNs - g.lastEffectAt < kEffectEvery)
            {
                continue; // again a little later
            }
            g.lastEffectAt = nowNs;
            g.hostBroken = broken;
            xEnt* e = th.ent;
            if (broken && th.dest->active)
            {
                // The game's own destroy: its effect plays once here too.
                zDestructibleAsset_EventCB(e, NULL, th.dest, eEventDestroy, NULL, NULL, 0);
                printf("tssm: coop: the host broke thing %d; broken here too\n", (int)i);
            }
            else if (!broken && !first && !th.dest->active)
            {
                // The host's came back (a respawn or a level reset).
                zEntEvent(e, e, eEventCollisionOn);
                zEntEvent(e, e, eEventVisible);
                zEntEvent(e, e, eEventReset);
                printf("tssm: coop: the host's thing %d came back; here too\n", (int)i);
            }
        }
    }

    // --- following the host (5.6) ------------------------------------------------------------------

    bool KnownLevel(U32 scene)
    {
        if (!scene_valid(scene))
        {
            return false;
        }
        S32 world = 0, task = 0;
        zDispatcher_FindWorldTask(scene, world, task);
        return world > 0;
    }

    void GuestFollow(U64 now, const PlayerState* p, U64 at)
    {
        zScene* sc = globals.sceneCur;
        bool goNow = iCoopTakeGoToHost();
        // M4 (docs/MULTIPLAYER.md 7.2): the host told this game where it goes
        // (EV_LEVEL): there at once, without waiting for its state to say so.
        U32 lead = iCoopFlowHostLeftTo(now);
        if (lead == sc->sceneID)
        {
            lead = 0;
        }
        U32 target = lead != 0 ? lead : (p != NULL ? p->scene : 0);
        if (lead == 0 && (p == NULL || iCoopAge(now, at) > 1000 * kNsPerMs || p->scene == sc->sceneID))
        {
            sFollowScene = 0;
            if (p != NULL && p->scene == sc->sceneID)
            {
                sFollowIssued = 0;
            }
            return;
        }
        if (sFollowScene != target)
        {
            sFollowScene = target;
            sFollowSince = now;
        }
        bool due = (iCoopFollowEnabled() && (lead != 0 || now - sFollowSince >= kFollowAfter)) || goNow;
        if (!due || sFollowIssued == target)
        {
            return;
        }
        char scene[5];
        SceneText(target, scene);
        if (!KnownLevel(target))
        {
            if (goNow)
            {
                printf("tssm: coop: the host's level %s is not one this game can go to\n", scene);
            }
            return;
        }
        if (gGameState != eGameState_Play || zGameIsPaused() || globals.cmgr != NULL || sc->pendingPortal != NULL ||
            globals.player.s == NULL || globals.player.s->Health == 0)
        {
            return; // later: not in the middle of a cutscene, a death or a pause
        }
        // A portal of our own (as the game's soak test makes one): the scene
        // the host is in, whatever this save has unlocked.
        memset(&sPortalAsset, 0, sizeof(sPortalAsset));
        sPortalAsset.sceneID = target;
        memset(&sPortal, 0, sizeof(sPortal));
        sPortal.passet = &sPortalAsset;
        gLevelChanged = (U8)(sc->sceneID >> 24) != (U8)(target >> 24);
        sc->pendingPortal = &sPortal;
        sFollowIssued = target;
        sPlaceOnArrival = true;
        sPlaceWhy = "arrived in the host's level";
        sNoSaveScene = target;
        sNoSaveArrived = false;
        iCoopFlowFollowIssued(target, lead != 0);
        printf("tssm: coop: following the host to %s%s\n", scene, lead != 0 ? " (it went there just now)" : "");
        fflush(stdout);
        iCoopAddNotice("going to the host's level");
    }

    void GuestPlaceOnArrival(const PlayerState* p, U64 now, U64 at)
    {
        if (!sPlaceOnArrival || p == NULL || iCoopAge(now, at) > 1000 * kNsPerMs || p->scene != globals.sceneCur->sceneID)
        {
            sPlainSince = 0;
            return;
        }
        // Not before this game's own opening of the level is over: its
        // fly-through ends by putting the player at the level's start, which
        // would undo the placing. Plain play for a second first.
        bool plain = gGameState == eGameState_Play && globals.cmgr == NULL && zcam_fly == 0 && !zGameIsPaused();
        if (!plain)
        {
            sPlainSince = 0;
            return;
        }
        if (sPlainSince == 0)
        {
            sPlainSince = now;
        }
        if (now - sPlainSince < kPlaceAfter)
        {
            return;
        }
        if (!(p->flags & PS_VISIBLE) || (p->flags & (PS_SPECIAL | PS_DEAD | PS_CUTSCENE | PS_PAUSED)))
        {
            return;
        }
        if (PlaceBeside(*p, sPlaceWhy))
        {
            sLocalPlaced = sLocalPlaced || sPlaceWhy == kLocalWhy;
            sPlaceOnArrival = false;
            sPlainSince = 0;
        }
    }

    bool PlaceBeside(const PlayerState& host, const char* why)
    {
        const PlayerState* p = &host;
        zPlayer* pl = xglobals->___player_ent_dont_use_directly;
        if (pl == NULL || pl->frame == NULL || pl->model == NULL)
        {
            return false;
        }
        // Beside the host, facing the same way: a step to one side of it,
        // behind it or in front of it, the first with floor at about the
        // host's height and nothing in between; on the host's own spot when
        // none has (the two players do not collide, and the host's floor is
        // known to be there).
        xEntFrame* f = pl->frame;
        F32 yaw = (F32)p->yaw / 65536.0f * 6.28318530718f;
        xVec3 spot = { p->pos[0], p->pos[1] + 0.1f, p->pos[2] };
        const char* where = "on the host's spot (no free floor beside it)";
        // Directions in the host's frame (yaw 0 faces +z): one side, the
        // other, behind, in front.
        static const F32 kDir[4][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } };
        static const char* const kDirName[4] = { "beside the host", "beside the host (the other side)",
                                                 "behind the host", "in front of the host" };
        const F32 kStep = 1.75f;
        F32 cy = cosf(yaw), sy = sinf(yaw);
        for (S32 k = 0; k < 4; k++)
        {
            // facing (sy, cy) in x/z; its side (cy, -sy)
            F32 dx = kDir[k][0] * cy + kDir[k][1] * sy;
            F32 dz = -kDir[k][0] * sy + kDir[k][1] * cy;
            xRay3 ray;
            xCollis coll;
            memset(&ray, 0, sizeof(ray));
            memset(&coll, 0, sizeof(coll));
            coll.flags = 0x1f00;
            ray.origin.assign(p->pos[0], p->pos[1] + 0.75f, p->pos[2]);
            ray.dir.assign(dx, 0.0f, dz);
            ray.min_t = 0.0f;
            ray.max_t = kStep + 0.5f;
            ray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;
            xRayHitsScene(xglobals->sceneCur, &ray, &coll);
            if ((coll.flags & k_HIT_IT) && coll.dist <= kStep + 0.5f)
            {
                continue; // a wall or a rock in between
            }
            memset(&ray, 0, sizeof(ray));
            memset(&coll, 0, sizeof(coll));
            coll.flags = 0x1f00;
            ray.origin.assign(p->pos[0] + dx * kStep, p->pos[1] + 0.75f, p->pos[2] + dz * kStep);
            ray.dir.assign(0.0f, -1.0f, 0.0f);
            ray.min_t = 0.0f;
            ray.max_t = 2.0f;
            ray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;
            xRayHitsScene(xglobals->sceneCur, &ray, &coll);
            if (!(coll.flags & k_HIT_IT) || coll.dist > 2.0f)
            {
                continue; // no floor near the host's height there
            }
            spot.assign(ray.origin.x, ray.origin.y - coll.dist + 0.1f, ray.origin.z);
            where = kDirName[k];
            break;
        }
        f->mat.pos = spot;
        f->rot.angle = yaw;
        f->rot.axis = xVec3::m_UnitAxisY;
        xMat3x3Euler(&f->mat, yaw, 0.0f, 0.0f);
        *(xMat4x3*)pl->model->Mat = f->mat;
        f->oldmat = f->mat;
        zCamReset(f->mat); // as the game's own same-level teleport (zSceneSwitch)
        pl->SetCamera();
        printf("tssm: coop: %s; placed %s at %.2f %.2f %.2f (the host at %.2f %.2f %.2f)\n", why, where, spot.x,
               spot.y, spot.z, p->pos[0], p->pos[1], p->pos[2]);
        fflush(stdout);
        return true;
    }
    // TSSM_COOP_LOG (tests): after a frame in which the host's things were
    // posed, each must still be where Pose put it; anything else moving one
    // (its own update, an event, a parent) is logged, at most 16 times.
    void GuestPoseCheck()
    {
        if (sPoseMovedLogged >= 16)
        {
            return;
        }
        U32 last = sFrameNo - 1; // the frame whose scene update just ran
        for (size_t i = 0; i < sThings.size(); i++)
        {
            const Thing& th = sThings[i];
            const GuestThing& g = sGuest[i];
            if (th.kind == K_BREAKABLE || g.count == 0 || g.posedFrame == 0 || th.ent->model == NULL)
            {
                continue;
            }
            const xVec3& p = *(const xVec3*)&th.ent->model->Mat->pos;
            F32 d = sqrtf(Dist2(p, g.posed));
            if (d > 0.05f)
            {
                sPoseMovedLogged++;
                printf("tssm: coop: pose check: thing %d is at %.3f %.3f %.3f, %.3f from where it was posed "
                       "(%.3f %.3f %.3f, %s)\n",
                       (int)i, p.x, p.y, p.z, d, g.posed[0], g.posed[1], g.posed[2],
                       g.posedFrame == last ? "this frame" : "an earlier frame");
                fflush(stdout);
                if (sPoseMovedLogged >= 16)
                {
                    return;
                }
            }
        }
    }

    // TSSM_COOP_SYNC_CAPTURE=<every_ms>:<count> (tests, tools/coop_test.py):
    // both copies write a frame at the same moments of the host's clock, as
    // sync_<host ms>.png in TSSM_CAPTURE_DIR, and log where the enemies near
    // the players are in it: the host where they are, the guest where it
    // draws them (`shown`: the host time its poses were made for).
    S32 sSyncEvery, sSyncLeft;
    U32 sSyncNext;
    std::vector<xVec3> sSyncPrev; // host: each thing's place on the frame before
    U32 sSyncPrevMs;

    void SyncRemember(U32 hostMs, bool hostSide)
    {
        if (!hostSide)
        {
            return;
        }
        for (size_t i = 0; i < sThings.size() && i < sSyncPrev.size(); i++)
        {
            const xEnt* e = sThings[i].ent;
            if (e->model != NULL && e->model->Mat != NULL)
            {
                sSyncPrev[i] = *(const xVec3*)&e->model->Mat->pos;
            }
        }
        sSyncPrevMs = hostMs;
    }
    const U32 kSyncLate = 100; // ms: a frame later than this is not that moment's
    bool sSyncParsed;

    void SyncCapture(U32 hostMs, const S64* shown)
    {
        if (!sSyncParsed)
        {
            sSyncParsed = true;
            const char* v = getenv("TSSM_COOP_SYNC_CAPTURE");
            int every = 0, count = 0;
            if (v != NULL && sscanf(v, "%d:%d", &every, &count) == 2 && every >= 100 && every <= 60000 && count > 0 &&
                count <= 1000)
            {
                sSyncEvery = every;
                sSyncLeft = count;
            }
        }
        if (sSyncLeft <= 0)
        {
            return;
        }
        // The host: each thing's place a frame ago, for its speed at a moment.
        bool hostSide = shown == NULL;
        if (hostSide && sSyncPrev.size() != sThings.size())
        {
            sSyncPrev.assign(sThings.size(), xVec3());
            sSyncPrevMs = 0;
        }
        U32 prevMs = sSyncPrevMs;
        if (sSyncNext == 0)
        {
            sSyncNext = (hostMs / (U32)sSyncEvery + 1) * (U32)sSyncEvery;
            SyncRemember(hostMs, hostSide);
            return;
        }
        if ((S32)(hostMs - sSyncNext) < 0)
        {
            SyncRemember(hostMs, hostSide);
            return;
        }
        if ((S32)(hostMs - sSyncNext) < 0)
        {
            return;
        }
        U32 at = sSyncNext;
        sSyncNext = (hostMs / (U32)sSyncEvery + 1) * (U32)sSyncEvery;
        if (hostMs - at > kSyncLate)
        {
            // The moment went by while this copy was not showing the shared
            // world (loading a level, before the host's world was shown):
            // not a frame of that moment, so none is written.
            return;
        }
        sSyncLeft--;
        char name[64];
        snprintf(name, sizeof(name), "sync_%010u.png", (unsigned)at);
        if (!iCaptureRequest(name))
        {
            return;
        }
        xEnt* pl = Player();
        if (pl == NULL || pl->model == NULL)
        {
            return;
        }
        const xVec3& me = *(xVec3*)&pl->model->Mat->pos;
        bool paused = zGameIsPaused() != 0;
        printf("tssm: coop: sync %u at host ms %u self %.3f %.3f %.3f%s\n", (unsigned)at, (unsigned)hostMs, me.x, me.y,
               me.z, paused ? " (paused)" : "");
        if (paused)
        {
            fflush(stdout);
            return; // its copies keep still while paused; the other game's go on
        }
        // Where each enemy and mover near a player is drawn in this frame:
        // the model's own matrix (on the guest, as posed from the snapshots).
        S32 logged = 0;
        for (size_t i = 0; i < sThings.size() && logged < 24; i++)
        {
            const Thing& th = sThings[i];
            if (th.kind == K_BREAKABLE || th.ent->model == NULL)
            {
                continue;
            }
            if (shown != NULL && sGuest[i].count == 0)
            {
                continue; // nothing from the host yet: not the host's thing on this screen
            }
            const xVec3& p = *(const xVec3*)&th.ent->model->Mat->pos;
            F32 near2 = Dist2(me, p);
            if (shown == NULL && sPartnerInLevel)
            {
                F32 d = Dist2(sPartnerPos, p);
                near2 = d < near2 ? d : near2;
            }
            if (near2 > kLogRadius * kLogRadius)
            {
                continue;
            }
            logged++;
            // The host's: how fast it moves at this moment, from its place a
            // frame ago (the two copies' frames are a few ms apart, which a
            // fast one shows).
            F32 speed = 0.0f;
            if (hostSide && prevMs != 0 && hostMs > prevMs && hostMs - prevMs < 200)
            {
                speed = sqrtf(Dist2(p, sSyncPrev[i])) / ((F32)(hostMs - prevMs) / 1000.0f);
                speed = speed == speed && speed < 1000.0f ? speed : 0.0f;
            }
            printf("tssm: coop: sync %u thing %d %s pos %.3f %.3f %.3f%s speed %.2f\n",
                   (unsigned)at, (int)i, th.kind == K_ENEMY ? "enemy" : "mover", p.x, p.y, p.z,
                   (th.ent->flags & 0x1) ? "" : " hidden", speed);
        }
        fflush(stdout);
    }
} // namespace

// --- iCoopInternal.h -------------------------------------------------------------------------

void iCoopWorldReset()
{
    iCoopHudReset();
    iCoopBossReset();
    iCoopCombatReset();
    iCoopFlowReset();
    if (!sDirty)
    {
        return;
    }
    sDirty = false;
    ResetSession();
    iCoopSetWorldView(false, 0, 0, false);
}

void iCoopWorldLocalStall(U64 now, U64 gap)
{
    // `gap` runs from this game's last frame before the stall: what arrived
    // after that frame arrived during the stall.
    LocalStall& st = sStalls[sStallNext];
    sStallNext = (sStallNext + 1) % kStallMemory;
    st.from = gap < now ? now - gap : 0;
    st.resume = now;
}

void iCoopWorldFrame(U64 now, const PlayerState* partner, U64 partnerAt)
{
    sFrameNo++;
    if (iCoopLocalWantsLevel() && globals.sceneCur != NULL)
    {
        iCoopLocalLevel(globals.sceneCur->sceneID, KnownLevel(globals.sceneCur->sceneID));
    }
    if (sNoSaveScene != 0 && globals.sceneCur != NULL && globals.sceneCur->sceneID == sNoSaveScene)
    {
        sNoSaveArrived = true;
    }
    if (!TableCurrent())
    {
        BuildTable();
    }
    sPartnerStalled = iCoopPartnerStalled(NULL);
    if (!iCoopConnected())
    {
        sGuestSync = false;
        sPartnerInLevel = sPartnerCullOk = sPartnerTargetOk = false;
        iCoopCombatOffline();
        return;
    }
    sDirty = true;
    iCoopFlowFrame(now, partner, partnerAt);
    bool guest = iCoopLocalSlot() == 1;
    if (guest && !sLocalPlaced && iCoopLocalPlayer2() && sLocalPlaceEpoch != iCoopSessionEpoch())
    {
        sLocalPlaceEpoch = iCoopSessionEpoch();
        iCoopWorldPlaceSoon(kLocalWhy);
    }
    if (!guest)
    {
        HostPartner(now, partner, partnerAt);
        bool shared = TableCurrent() && sPartnerInLevel && sSeq != 0;
        iCoopSetWorldView(shared, (S32)sThings.size(), 0, false);
        if (shared)
        {
            SyncCapture((U32)NowMs(now), NULL);
        }
        iCoopHudFrame(now, partner);
        iCoopCombatFrame(now, partner, partnerAt);
        return;
    }
    S64 rendered = sRenderHostMs; // what this frame's poses were made for

    // The players' places, for the snapshots' samples (PushSample).
    sNearRefCount = 0;
    xEnt* self = Player();
    if (self != NULL && self->model != NULL)
    {
        sNearRef[sNearRefCount++] = *(xVec3*)&self->model->Mat->pos;
    }
    if (partner != NULL && partner->scene == sTableSceneId)
    {
        xVec3 hp = { partner->pos[0], partner->pos[1], partner->pos[2] };
        sNearRef[sNearRefCount++] = hp;
    }

    // The guest: snapshots in, in order.
    U64 at;
    while (iCoopTakeSnapshot(&sInSnap, &at))
    {
        if (TableCurrent())
        {
            GuestApply(sInSnap, at);
        }
    }
    if (sPartnerStalled)
    {
        sHostStallSeen = true;
    }
    // The host is here while its newest state says so and is fresh, or while
    // its game reports a stall: then its things hold where they are (5.11)
    // instead of going back to the guest's own.
    bool hostHere = partner != NULL && partner->scene == sTableSceneId &&
                    (AgeSince(now, partnerAt) < kSyncStale || sPartnerStalled);
    // Not before every thing has a state from the host (or after a while
    // anyway): a copy shown at its own place among the host's would jump when
    // its first state comes.
    bool full = sGuestKnown >= sGuestPosable || (sGuestFirstAt != 0 && iCoopAge(now, sGuestFirstAt) >= kFullWait);
    if (hostHere && TableCurrent())
    {
        iCoopCombatHostHere(sTableSceneId);
    }
    bool wasSync = sGuestSync;
    sGuestSync = TableCurrent() && hostHere && sApplied != 0 &&
                 (AgeSince(now, sAppliedAt) < kSyncStale || sPartnerStalled) && full;
    if (sGuestSync && !sGuestFullLogged)
    {
        sGuestFullLogged = true;
        printf("tssm: coop: the host's world is shown: %d of %d things known, %d ms after its first snapshot\n",
               (int)sGuestKnown, (int)sGuestPosable, (int)(iCoopAge(now, sGuestFirstAt) / kNsPerMs));
        S32 listed = 0;
        for (size_t i = 0; i < sThings.size() && listed < 8; i++)
        {
            if (!sGuest[i].heard)
            {
                listed++;
                printf("tssm: coop: no state from the host yet for thing %d (kind %d, id %08x); it keeps still\n",
                       (int)i, (int)sThings[i].kind, (unsigned)sThings[i].id);
            }
        }
        fflush(stdout);
    }
    if (wasSync && iCoopLogEvery() > 0)
    {
        GuestPoseCheck();
    }
    if (sOffsetCount > 0)
    {
        S64 off = sOffsets[0];
        for (S32 i = 1; i < sOffsetCount; i++)
        {
            off = sOffsets[i] < off ? sOffsets[i] : off;
        }
        sRenderHostMs = NowMs(now) - off - kInterpDelayMs;
    }
    if (sGuestSync != wasSync && iCoopLogEvery() > 0)
    {
        printf("tssm: coop: posing the host's things %s at frame %u\n", sGuestSync ? "on" : "off",
               (unsigned)sFrameNo);
        fflush(stdout);
    }
    if (sGuestSync)
    {
        GuestBreakables();
        if (iCoopLogEvery() > 0 && sFrameNo % 300 == 0 && sOffsetCount > 0)
        {
            // How far back the host's world is drawn, and how old the newest
            // snapshot was when it arrived (both on this PC's clock; across
            // two PCs the second also holds the clocks' difference).
            S32 lag = (S32)(NowMs(now) - sRenderHostMs);
            S32 oneWay = (S32)sOffsets[(sOffsetNext + kOffsets - 1) % kOffsets];
            printf("tssm: coop: world lag %d ms, newest snapshot %d ms old on arrival\n", (int)lag, (int)oneWay);
        }
    }
    // The frame just made (and about to be shown) was posed from the host's
    // world only when it was shown during that frame's update: the first frame
    // after it turns on is still the guest's own world, so it is neither
    // captured nor logged as the host's.
    if (wasSync)
    {
        if (sHaveHostClock && sOffsetCount > 0 && rendered != 0)
        {
            SyncCapture((U32)(sHostRaw + (U32)(S32)(rendered - sHostMs)), &rendered);
        }
        S32 every = iCoopLogEvery();
        if (every > 0 && sFrameNo % (U32)every == 0 && !zGameIsPaused())
        {
            // Where the watched things are drawn in this frame, and for
            // which moment of the host's clock (not while this game is
            // paused: its copies keep still then, the host's go on).
            xEnt* pl = Player();
            if (pl != NULL && pl->model != NULL && sOffsetCount > 0 && rendered != 0)
            {
                WatchList(*(xVec3*)&pl->model->Mat->pos, NULL);
                U32 raw = (U32)(sHostRaw + (U32)(S32)(rendered - sHostMs));
                for (size_t k = 0; k < sWatch.size(); k++)
                {
                    size_t i = sWatch[k].second;
                    const xVec3& p = *(const xVec3*)&sThings[i].ent->model->Mat->pos;
                    printf("tssm: coop: world shown ms %u thing %d pos %.3f %.3f %.3f\n", (unsigned)raw, (int)i, p.x,
                           p.y, p.z);
                }
            }
        }
    }
    GuestFollow(now, partner, partnerAt);
    GuestPlaceOnArrival(partner, now, partnerAt);
    iCoopSetWorldView(sGuestSync, (S32)sThings.size(), sMismatch, sFollowIssued != 0);
    iCoopHudFrame(now, partner);
    iCoopCombatFrame(now, partner, partnerAt);
}

void iCoopWorldOutgoing(U64 now, const Snapshot** snap, U32* ack, const Pickups** pick,
                        const Progress** prog, const Counters** cnt)
{
    *snap = NULL;
    *ack = 0;
    *pick = NULL;
    *prog = NULL;
    *cnt = NULL;
    if (!iCoopConnected())
    {
        return;
    }
    if (iCoopLocalSlot() == 1)
    {
        *ack = sApplied;
        return;
    }
    // M3: the PICKUPS, PROGRESS, COUNTERS and the events waiting go in the
    // same datagram, so the snapshot leaves room for them.
    *pick = iCoopCombatOutgoing(now, sPartnerInLevel, &sSnapReserve, prog);
    *cnt = iCoopHudOutgoing(now, sPartnerInLevel, &sSnapReserve);
    *snap = HostSnapshot(now, sPartnerInLevel, sPartnerSending);
    sSnapReserve = 0;
}

// --- iCoop.h: the hooks (5.8) ----------------------------------------------------------------------

bool iCoopWorldGuestPose(xBase* b, F32 dt)
{
    if (!sGuestSync)
    {
        return false;
    }
    iCoopCostScope cost(ICOOP_COST_POSE);
    S32 i = Lookup(b);
    if (i < 0 || sThings[i].kind == K_BREAKABLE)
    {
        return false;
    }
    if (zGameIsPaused())
    {
        return true; // still, like everything else in a paused game
    }
    WState w;
    GuestThing& g = sGuest[i];
    if (Interp(sThings[i], g, sRenderHostMs, &w))
    {
        if (iCoopLogEvery() > 0 && g.posedFrame != 0 && sJumpLogged < 16)
        {
            // TSSM_COOP_LOG (tests): a pose far from the last one, with the
            // samples it was made from, to find where a wrong one comes from.
            F32 d = sqrtf(Dist2(*(const xVec3*)w.pos, g.posed));
            bool outside = false;
            for (S32 c = 0; c < 3; c++)
            {
                F32 lo = g.smp[0].s.pos[c], hi = lo;
                for (S32 k = 1; k < g.count; k++)
                {
                    lo = g.smp[k].s.pos[c] < lo ? g.smp[k].s.pos[c] : lo;
                    hi = g.smp[k].s.pos[c] > hi ? g.smp[k].s.pos[c] : hi;
                }
                outside = outside || w.pos[c] < lo - 0.05f || w.pos[c] > hi + 0.05f;
            }
            if (outside)
            {
                sJumpLogged++;
                printf("tssm: coop: pose outside its samples: thing %d (%.3f from the last) at %.3f %.3f %.3f at "
                       "host ms %lld (frame %u, last posed frame %u); samples:",
                       (int)i, d, w.pos[0], w.pos[1], w.pos[2], (long long)sRenderHostMs, (unsigned)sFrameNo,
                       (unsigned)g.posedFrame);
                for (S32 k = 0; k < g.count; k++)
                {
                    printf(" %lld:%.3f,%.3f,%.3f", (long long)g.smp[k].hostMs, g.smp[k].s.pos[0], g.smp[k].s.pos[1],
                           g.smp[k].s.pos[2]);
                }
                printf("\n");
                fflush(stdout);
            }
        }
        Pose(sThings[i], g, w, dt);
    }
    if (sThings[i].kind == K_ENEMY)
    {
        iCoopBossGuestShow(i, sThings[i].ent, dt);
    }
    return true; // nothing from the host yet: it keeps still
}

bool iCoopWorldGuestOwns(xBase* b)
{
    if (!sGuestSync)
    {
        return false;
    }
    S32 i = Lookup(b);
    return i >= 0 && sThings[i].kind != K_BREAKABLE;
}

bool iCoopWorldNoDamage(xBase* b)
{
    return iCoopWorldGuestOwns(b);
}

void iCoopStandInBegin(xBase* b)
{
    if (!sPartnerTargetOk || sStand.active)
    {
        return;
    }
    iCoopCostScope cost(ICOOP_COST_STANDIN);
    S32 i = Lookup(b);
    if (i < 0 || sThings[i].kind != K_ENEMY)
    {
        return;
    }
    xEnt* nme = sThings[i].ent;
    zPlayer* pl = xglobals->___player_ent_dont_use_directly;
    if (nme->model == NULL || pl == NULL || pl->model == NULL || pl->model->Mat == NULL || pl->frame == NULL ||
        globals.player.s == NULL)
    {
        return;
    }
    const xVec3& np = *(xVec3*)&nme->model->Mat->pos;
    xVec3 hostPos = *(xVec3*)&pl->model->Mat->pos;
    F32 dh = sqrtf(Dist2(np, hostPos));
    F32 dg = sqrtf(Dist2(np, sPartnerPos));
    HostThing& h = sHost[i];
    U8 was = h.target;
    // The player it has, unless the other is clearly nearer.
    if (h.target == 0 && dg < dh * 0.8f - 2.0f)
    {
        h.target = 1;
    }
    else if (h.target == 1 && dh < dg * 0.8f - 2.0f)
    {
        h.target = 0;
    }
    if (h.target != was && iCoopLogEvery() > 0)
    {
        printf("tssm: coop: enemy thing %d targets the %s (host %.1f, guest %.1f away)\n", (int)i,
               h.target ? "guest" : "host", dh, dg);
    }
    if (h.target == 0)
    {
        return;
    }
    StandInAt();
}

namespace
{
    // The host's player entity at the guest's place (5.4), until
    // iCoopStandInEnd. The caller checked the guest's position is believable.
    bool StandInAt()
    {
        zPlayer* pl = xglobals->___player_ent_dont_use_directly;
        if (sStand.active || pl == NULL || pl->model == NULL || pl->model->Mat == NULL || pl->frame == NULL ||
            globals.player.s == NULL)
        {
            return false;
        }
        const xVec3 hostPos = *(xVec3*)&pl->model->Mat->pos;
        S32 mats = pl->model->BoneCount + 1;
        if (mats < 1 || mats > kMaxBones)
        {
            return false;
        }
        xVec3 d = { sPartnerPos.x - hostPos.x, sPartnerPos.y - hostPos.y, sPartnerPos.z - hostPos.z };
        StandIn& s = sStand;
        s.player = pl;
        s.model = pl->model;
        s.mats = mats;
        memcpy(s.mat, pl->model->Mat, sizeof(RwMatrix) * mats);
        s.fmat = pl->frame->mat;
        s.foldmat = pl->frame->oldmat;
        s.bound = pl->bound;
        zPlayerGlobals& g = globals.player;
        s.predictRotate = g.PredictRotate;
        s.predictTranslate = g.PredictTranslate;
        s.predictCurrDir = g.PredictCurrDir;
        s.predictAngV = g.PredictAngV;
        s.predictCurrVel = g.PredictCurrVel;
        s.health = g.s->Health;
        s.damageTimer = g.DamageTimer;

        for (S32 k = 0; k < mats; k++)
        {
            pl->model->Mat[k].pos.x += d.x;
            pl->model->Mat[k].pos.y += d.y;
            pl->model->Mat[k].pos.z += d.z;
        }
        pl->frame->mat.pos.x += d.x;
        pl->frame->mat.pos.y += d.y;
        pl->frame->mat.pos.z += d.z;
        pl->frame->oldmat.pos.x += d.x;
        pl->frame->oldmat.pos.y += d.y;
        pl->frame->oldmat.pos.z += d.z;
        xBound& bd = pl->bound;
        bd.sph.center.x += d.x; // the center comes first in every kind of bound
        bd.sph.center.y += d.y;
        bd.sph.center.z += d.z;
        if (bd.type == XBOUND_TYPE_BOX)
        {
            bd.box.box.lower.x += d.x;
            bd.box.box.lower.y += d.y;
            bd.box.box.lower.z += d.z;
            bd.box.box.upper.x += d.x;
            bd.box.box.upper.y += d.y;
            bd.box.box.upper.z += d.z;
        }
        xBoundUpdate(&bd);
        // Where enemies that lead their aim expect the guest to go.
        F32 sp = sqrtf(sPartnerVel.x * sPartnerVel.x + sPartnerVel.z * sPartnerVel.z);
        g.PredictAngV = 0.0f;
        g.PredictCurrVel = sp;
        if (sp > 0.01f)
        {
            g.PredictCurrDir.assign(sPartnerVel.x / sp, 0.0f, sPartnerVel.z / sp);
        }
        g.PredictTranslate = sPartnerPos;
        g.s->Health = sPartnerHealth;
        // M3: the guest's own safe time after a hit, as the host last sent one
        // (enemies hold back from a player who was just hit).
        g.DamageTimer = iCoopCombatGuestDamageTimer();
        s.active = true;
        sStandIns++;
        return true;
    }
} // namespace

bool iCoopStandInGuest()
{
    return sPartnerCullOk && StandInAt();
}

bool iCoopStandInNear(const F32* pos)
{
    // Host, a hazard (a projectile) about to move and hit: the nearer player
    // is the one it can hit (M3).
    if (!sPartnerTargetOk || sStand.active || pos == NULL)
    {
        return false;
    }
    zPlayer* pl = xglobals->___player_ent_dont_use_directly;
    if (pl == NULL || pl->model == NULL || pl->model->Mat == NULL)
    {
        return false;
    }
    const xVec3& at = *(const xVec3*)pos;
    if (Dist2(sPartnerPos, at) < Dist2(*(const xVec3*)&pl->model->Mat->pos, at))
    {
        return StandInAt();
    }
    return false;
}

S32 iCoopWorldLookup(const xBase* b)
{
    return TableCurrent() ? Lookup(b) : -1;
}

xEnt* iCoopWorldEnemy(S32 id)
{
    if (!TableCurrent() || id < 0 || id >= (S32)sThings.size() || sThings[id].kind != K_ENEMY)
    {
        return NULL;
    }
    return sThings[id].ent;
}

xEnt* iCoopWorldBreakable(S32 id, zDestructible** dest)
{
    if (!TableCurrent() || id < 0 || id >= (S32)sThings.size() || sThings[id].dest == NULL)
    {
        return NULL;
    }
    *dest = sThings[id].dest;
    return sThings[id].ent;
}

bool iCoopWorldPartnerTarget(F32 pos[3])
{
    zPlayer* pl = xglobals->___player_ent_dont_use_directly;
    if (!sPartnerTargetOk || pl == NULL || pl->model == NULL)
    {
        return false;
    }
    // The guest's centre: its place and this player's own centre's height
    // over its place (the two have the same bound).
    const xVec3* c = xEntGetCenter(pl);
    const xVec3& p = *(const xVec3*)&pl->model->Mat->pos;
    pos[0] = sPartnerPos.x + (c->x - p.x);
    pos[1] = sPartnerPos.y + (c->y - p.y);
    pos[2] = sPartnerPos.z + (c->z - p.z);
    return true;
}

bool iCoopWorldTable(U32* scene, U32* hash, S32* count)
{
    if (!TableCurrent())
    {
        return false;
    }
    *scene = sTableSceneId;
    *hash = sTableHash;
    *count = (S32)sThings.size();
    return true;
}

bool iCoopWorldGuestSynced()
{
    return sGuestSync;
}

bool iCoopWorldPartnerHere(F32 pos[3])
{
    if (!sPartnerCullOk)
    {
        return false;
    }
    pos[0] = sPartnerPos.x;
    pos[1] = sPartnerPos.y;
    pos[2] = sPartnerPos.z;
    return true;
}

void iCoopWorldPlaceBeside(const PlayerState& host, const char* why)
{
    PlaceBeside(host, why);
}

void iCoopWorldPlaceSoon(const char* why)
{
    sPlaceOnArrival = true;
    sPlaceWhy = why;
    sPlainSince = 0;
}

void iCoopStandInEnd()
{
    StandIn& s = sStand;
    if (!s.active)
    {
        return;
    }
    iCoopCostScope cost(ICOOP_COST_STANDIN);
    s.active = false;
    // The player's globals go back whatever happened: the guest's health
    // must never stay on this player.
    zPlayerGlobals& g = globals.player;
    g.PredictRotate = s.predictRotate;
    g.PredictTranslate = s.predictTranslate;
    g.PredictCurrDir = s.predictCurrDir;
    g.PredictAngV = s.predictAngV;
    g.PredictCurrVel = s.predictCurrVel;
    if (g.s != NULL)
    {
        g.s->Health = s.health;
    }
    g.DamageTimer = s.damageTimer;
    zPlayer* pl = xglobals->___player_ent_dont_use_directly;
    if (pl != s.player || pl->model != s.model || pl->frame == NULL)
    {
        return; // cannot happen inside one enemy's update; no entity to put back into
    }
    memcpy(pl->model->Mat, s.mat, sizeof(RwMatrix) * s.mats);
    pl->frame->mat = s.fmat;
    pl->frame->oldmat = s.foldmat;
    pl->bound = s.bound;
}

bool iCoopStandInActive()
{
    return sStand.active;
}

bool iCoopStandInRealPos(F32 out[3])
{
    if (!sStand.active || sStand.mats < 1)
    {
        return false;
    }
    out[0] = sStand.mat[0].pos.x;
    out[1] = sStand.mat[0].pos.y;
    out[2] = sStand.mat[0].pos.z;
    return true;
}

void iCoopWorldDeathFx(xBase* b)
{
    if (!sPartnerInLevel || !TableCurrent())
    {
        return; // not hosting a guest in this level (the guest's own calls land here too)
    }
    S32 i = Lookup(b);
    if (i >= 0 && sThings[i].kind == K_ENEMY)
    {
        sHost[i].deaths = (U8)((sHost[i].deaths + 1) & 3);
        if (iCoopLogEvery() > 0)
        {
            printf("tssm: coop: enemy thing %d died (death effect %u)\n", (int)i, (unsigned)sHost[i].deaths);
        }
    }
}

bool iCoopCullNearPartner(const F32* pos, F32 distSq)
{
    if (!sPartnerCullOk)
    {
        return false;
    }
    return Dist2(sPartnerPos, pos) < distSq;
}

bool iCoopNoAutoSave()
{
    if (iCoopLocalPlayer2())
    {
        return true; // local co-op: only Player 1's game saves, from the start
    }
    zScene* sc = globals.sceneCur;
    if (sNoSaveScene != 0 && sc != NULL)
    {
        if (sc->sceneID == sNoSaveScene)
        {
            sNoSaveArrived = true;
        }
        else if (sNoSaveArrived)
        {
            sNoSaveScene = 0; // the guest has left the level it followed the host to
        }
    }
    if (sNoSaveScene != 0 && sNoSaveArrived)
    {
        return true;
    }
    // Joining again counts as joined (8.3): the guest's game has not left the
    // session, and in the minute it tries, it runs on with what the session
    // gave it.
    if (iCoopRunning() && iCoopLocalSlot() == 1 && (iCoopConnected() || iCoopJoiningAgain()))
    {
        return true;
    }
    // This game may save: never with the host's progress in it (13).
    iCoopCombatProgressDone("this game saves again");
    if (iCoopLogEvery() > 0)
    {
        printf(
            "tssm: coop: this game may autosave: its own progress, tokens %u, shiny objects %u\n",
            (unsigned)globals.player.InvGoofyGoober, (unsigned)globals.player.InvManlinessPoints);
        fflush(stdout);
    }
    return false;
}

bool iCoopKeepSceneSave(U32 scene)
{
    // A level the guest followed the host to, or whose pickups the host's
    // PICKUPS changed here: the save keeps what it had of it. Once: the save
    // into memory as the level is left.
    bool keep = scene != 0 && scene == sKeepSaveScene;
    sKeepSaveScene = 0;
    if (keep)
    {
        iCoopCombatPutOwnBlock(scene);
    }
    return keep;
}

void iCoopWorldSceneReset()
{
    if (iCoopRunning())
    {
        iCoopCombatSceneReset();
        iCoopBossSceneReset();
    }
}

bool iCoopWorldFollowedHere()
{
    zScene* sc = globals.sceneCur;
    return sNoSaveScene != 0 && sNoSaveArrived && sc != NULL && sc->sceneID == sNoSaveScene;
}

void iCoopWorldSceneExit()
{
    iCoopFlowSceneExit();
    iCoopPuppetSceneExit();
    iCoopLookSceneExit();
    zScene* sc = globals.sceneCur;
    sKeepSaveScene = 0;
    if (sc != NULL && ((sNoSaveScene != 0 && sNoSaveArrived && sc->sceneID == sNoSaveScene) ||
                       (iCoopLocalSlot() == 1 && iCoopCombatHostChangedLevel())))
    {
        sKeepSaveScene = sc->sceneID;
    }
    if (sNoSaveScene != 0 && sc != NULL)
    {
        if (sc->sceneID == sNoSaveScene)
        {
            sNoSaveArrived = true;
        }
        // Leaving the followed level any way but a portal back into it (to
        // another level, to the title, a game over): autosaves are the
        // guest's own again (a lost chance resets the level without leaving
        // it, and keeps this).
        bool same = sc->pendingPortal != NULL && sc->pendingPortal->passet != NULL &&
                    sc->pendingPortal->passet->sceneID == sNoSaveScene;
        if (sNoSaveArrived && !same)
        {
            sNoSaveScene = 0;
        }
    }
    if (sStand.active)
    {
        iCoopStandInEnd();
    }
    iCoopHudSceneExit();
    iCoopCombatSceneExit();
    sGuestSync = false;
    sPartnerInLevel = sPartnerCullOk = sPartnerTargetOk = false;
    if (sTableValid || !sThings.empty())
    {
        ClearTable();
    }
}
