// PC-only: the game-side half of co-op (iCoop.h, docs/MULTIPLAYER.md 2.3 and
// 4.2). Compiled with the game units, because it reads the player and draws a
// model; iCoop.cpp, which owns the network, never sees a game type.
//
// Each frame (iCoopFrame, after the player update):
//   - this player's state goes to the I/O thread, 30 times a second;
//   - the partner's states that arrived are added to a short history, and the
//     partner is placed 100 ms in the past between the two states around that
//     moment (or a little past the newest, along its velocity);
//   - the characters and costumes (iCoopLook.cpp, M4).
//
// The partner is drawn (iCoopRenderPartner, right after the player) with a
// model of its own (iCoopLook.cpp): this game's copy of the model the
// partner's game says its player is drawn with (the PLAYER_STATE's `model`),
// built once per level apart from this player's own, so both players may be
// the same character, in the same or different costumes, or both drive or
// slide. (M1 borrowed the character model this game had loaded and was not
// using; that allowed only different characters.) The puppet runs none of the
// player's logic: its animation state is set from the network and its time
// written directly, it is never updated (no transitions, callbacks, sounds or
// particles), and its state's BeforeAnimMatrices callback, which reads
// globals.player, is off while its pose is evaluated.

#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopWorldInternal.h"

#include "xAnim.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xMath3.h"
#include "xModel.h"
#include "xModelBucket.h"
#include "zCamera.h"
#include "zEntCruiseBubble.h"
#include "zEntPlayer.h"
#include "zEntPlayerBungeeState.h"
#include "zEntPlayerOOBState.h"
#include "zGlobals.h"
#include "zScene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const F32 kTwoPi = 6.28318530718f;
    const U64 kNsPerMs = 1000000ull;
    const U64 kInterpDelay = 100 * kNsPerMs;  // drawn this far in the past
    const U64 kExtrapolateMax = 100 * kNsPerMs; // then along the velocity at most this long
    const U64 kStale = 3000 * kNsPerMs;       // no state for this long: not drawn
    const U64 kSendEvery = 30 * kNsPerMs;     // ~30 states a second at 60 fps
    const F32 kSnap = 10.0f;                  // a jump this long between two states is not slid
    // Stalls (docs/MULTIPLAYER.md 5.11): two states this far apart in arrival
    // had a stall between them (the partner's game, or the link): no slide
    // across it, the newer one is shown at once.
    const U64 kGap = 400 * kNsPerMs;
    // This game ran no frame for this long: a stall of its own (logged, and
    // the shared world takes it into account).
    const U64 kLocalStall = 250 * kNsPerMs;
    const F32 kTagHeight = 2.1f;              // the name tag above the partner's feet
    const S32 kHistory = 24;
    const S32 kMaxParts = 16;

    U32 sFrame;
    U64 sLastSend;
    U64 sLastFrameAt;    // when iCoopFrame last ran (a stall of this game)
    bool sPartnerStalled; // the partner's game reports a stall (this frame)
    U32 sPartnerStallMs;
    bool sWaitShown;     // "waiting for the other player's game" was due

    iCoopSample sHist[kHistory];
    S32 sHistCount;
    U32 sSeenSeq;
    U32 sEpoch = ~0u;

    // The partner as drawn this frame.
    struct View
    {
        bool have;      // a state to place
        bool fresh;     // newer than kStale
        bool sameLevel; // in this game's scene
        bool body;      // drawn with a model
        bool tag;       // has a name tag
        iNet::PlayerState s; // the newest state
        xVec3 pos;
        F32 yaw;
        U32 anim;
        F32 animTime;
    };
    View sView;

    U32 sLevelScene;
    char sLevelName[64];

    // The model the puppet is drawn with this frame (iCoopLook.cpp's, one per
    // level and model), and the last one logged.
    xModelInstance* sPuppet;
    S32 sPuppetModel; // which of the game's models it is (ePlayerEnum)
    xModelInstance* sPuppetLogged;
    F32 sDrawnLogTokens = 10.0f; // the "drawn as" log line's ration
    U64 sDrawnLogAt;
    U32 sUnknownAnim;
    S32 sUnknownLogged; // at most a few lines a session: the IDs come from the network

    xEnt* Player()
    {
        return xglobals->___player_ent_dont_use_directly;
    }

    bool InGameplay()
    {
        return globals.sceneCur != NULL && Player() != NULL && Player()->model != NULL;
    }

    // The 4-character scene ID, as the game writes it ('BB02').
    void SceneText(U32 id, char out[5])
    {
        for (int i = 0; i < 4; i++)
        {
            char c = (char)(id >> (24 - 8 * i));
            out[i] = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '?';
        }
        out[4] = '\0';
    }

    // Bungee, Cruise Bubble, out of bounds, sliding, Spongeball: moves where
    // the player is not drawn as its plain model, so the puppet is hidden.
    // (M4: the Patty Wagon and the slide sled are plain models of their
    // levels, drawn as the partner like the characters.)
    bool LocalSpecialMode()
    {
        bool character = gCurrentPlayer == eCurrentPlayerSpongeBob || gCurrentPlayer == eCurrentPlayerPatrick;
        return bungee_state::active() || cruise_bubble::active() || oob_state::enabled() ||
               (character && zEntPlayerIsInSlidingMode()) || player_is_sponge_ball() ||
               (!character && gCurrentPlayer != eCurrentPlayerCar && gCurrentPlayer != eCurrentPlayerSpongePat);
    }

    U8 LocalCharacter()
    {
        return gCurrentPlayer == eCurrentPlayerPatrick ? iNet::CHAR_PATRICK : iNet::CHAR_SPONGEBOB;
    }

    void ReadLocal(F32 dt, S32 paused, iNet::PlayerState* s)
    {
        xEnt* ent = Player();
        memset(s, 0, sizeof(*s));
        s->slot = iCoopLocalSlot();
        s->frame = sFrame;
        s->scene = globals.sceneCur->sceneID;
        s->character = LocalCharacter();

        U8 flags = 0;
        if (globals.player.Visible)
        {
            flags |= iNet::PS_VISIBLE;
        }
        if (LocalSpecialMode())
        {
            flags |= iNet::PS_SPECIAL;
        }
        if (paused)
        {
            flags |= iNet::PS_PAUSED;
        }
        if (globals.cmgr != NULL || zCameraIsFlyCamRunning())
        {
            flags |= iNet::PS_CUTSCENE; // M4: a camera fly-through too
        }
        if ((globals.player.s != NULL && globals.player.s->Health == 0) || iCoopCombatRespawning())
        {
            flags |= iNet::PS_DEAD;
        }
        s->flags = flags;

        const xMat4x3& m = *(const xMat4x3*)ent->model->Mat;
        s->pos[0] = m.pos.x;
        s->pos[1] = m.pos.y;
        s->pos[2] = m.pos.z;
        F32 yaw = atan2f(m.at.x, m.at.z);
        if (!(yaw == yaw))
        {
            yaw = 0.0f;
        }
        S32 y = (S32)floorf(yaw / kTwoPi * 65536.0f + 0.5f);
        s->yaw = (U16)(y & 0xFFFF);

        if (!paused && dt > 1e-4f && ent->frame != NULL)
        {
            const xVec3& a = ent->frame->oldmat.pos;
            const xVec3& b = ent->frame->mat.pos;
            F32 v[3] = { (b.x - a.x) / dt, (b.y - a.y) / dt, (b.z - a.z) / dt };
            for (int i = 0; i < 3; i++)
            {
                F32 c = v[i] * 100.0f;
                if (!(c == c))
                {
                    c = 0.0f;
                }
                c = c > 32767.0f ? 32767.0f : (c < -32767.0f ? -32767.0f : c);
                s->vel[i] = (S16)c;
            }
        }

        xAnimPlay* play = ent->model->Anim;
        if (play != NULL && play->Single != NULL && play->Single->State != NULL)
        {
            s->anim = play->Single->State->ID;
            F32 t = play->Single->Time;
            s->anim_time = (t == t && t >= 0.0f) ? (t > iNet::kAnimTimeLimit ? iNet::kAnimTimeLimit : t) : 0.0f;
        }

        U32 health = globals.player.s != NULL ? globals.player.s->Health : 0;
        s->health = (U8)(health > iNet::kPlayerHealthMax ? iNet::kPlayerHealthMax : health);

        U16 parts = 0;
        S32 i = 0;
        for (xModelInstance* p = ent->model; p != NULL && i < kMaxParts; p = p->Next, i++)
        {
            if (p->Flags & 0x1)
            {
                parts |= (U16)(1u << i);
            }
        }
        s->parts = parts;
        s->model = iCoopLookLocalModel();

        // Never hand the wire something it would refuse.
        for (int k = 0; k < 3; k++)
        {
            if (!(s->pos[k] >= -iNet::kWorldLimit && s->pos[k] <= iNet::kWorldLimit))
            {
                s->pos[k] = 0.0f;
            }
        }
    }

    void LogState(const char* who, const iNet::PlayerState& s)
    {
        char scene[5];
        SceneText(s.scene, scene);
        printf("tssm: coop: %s frame %u scene %s char %u pos %.3f %.3f %.3f yaw %u anim %08x t %.3f flags %u model %u\n",
               who, (unsigned)s.frame, scene, (unsigned)s.character, s.pos[0], s.pos[1], s.pos[2], (unsigned)s.yaw,
               (unsigned)s.anim, s.anim_time, (unsigned)s.flags, (unsigned)s.model);
    }

    F32 YawOf(U16 y)
    {
        return (F32)y / 65536.0f * kTwoPi;
    }

    F32 Dist(const iNet::PlayerState& a, const iNet::PlayerState& b)
    {
        F32 dx = a.pos[0] - b.pos[0], dy = a.pos[1] - b.pos[1], dz = a.pos[2] - b.pos[2];
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }

    // Where the partner is drawn, from the history, at `t` (now less the
    // interpolation delay).
    void Place(U64 t)
    {
        const iCoopSample& newest = sHist[sHistCount - 1];
        sView.s = newest.s;

        if (sPartnerStalled)
        {
            // The partner's game is not running (a held window, a hitch, a
            // load): it stands where its newest state has it, and goes on
            // from there when it runs again.
            const iNet::PlayerState& s = newest.s;
            sView.pos.assign(s.pos[0], s.pos[1], s.pos[2]);
            sView.yaw = YawOf(s.yaw);
            sView.anim = s.anim;
            sView.animTime = s.anim_time;
            return;
        }

        S32 a = -1;
        for (S32 i = sHistCount - 1; i >= 0; i--)
        {
            if (sHist[i].at <= t)
            {
                a = i;
                break;
            }
        }

        if (a < 0)
        {
            // Everything is newer than t (the partner just arrived): the oldest.
            const iNet::PlayerState& s = sHist[0].s;
            sView.pos.assign(s.pos[0], s.pos[1], s.pos[2]);
            sView.yaw = YawOf(s.yaw);
            sView.anim = s.anim;
            sView.animTime = s.anim_time;
            return;
        }

        const iNet::PlayerState& sa = sHist[a].s;
        if (a == sHistCount - 1)
        {
            // Past the newest: along its velocity for a moment, then hold.
            U64 ahead = t - sHist[a].at;
            if (ahead > kExtrapolateMax)
            {
                ahead = kExtrapolateMax;
            }
            F32 sec = (F32)ahead / 1e9f;
            if (sa.flags & iNet::PS_PAUSED)
            {
                sec = 0.0f;
            }
            sView.pos.assign(sa.pos[0] + sa.vel[0] / 100.0f * sec, sa.pos[1] + sa.vel[1] / 100.0f * sec,
                          sa.pos[2] + sa.vel[2] / 100.0f * sec);
            sView.yaw = YawOf(sa.yaw);
            sView.anim = sa.anim;
            sView.animTime = sa.anim_time;
            return;
        }

        const iCoopSample& A = sHist[a];
        const iCoopSample& B = sHist[a + 1];
        const iNet::PlayerState& sb = B.s;
        if (B.at - A.at > kGap)
        {
            // A stall between them: the newer one at once, no slow slide
            // from where the partner was before it.
            sView.pos.assign(sb.pos[0], sb.pos[1], sb.pos[2]);
            sView.yaw = YawOf(sb.yaw);
            sView.anim = sb.anim;
            sView.animTime = sb.anim_time;
            return;
        }
        if (sa.scene != sb.scene || Dist(sa, sb) > kSnap)
        {
            // A teleport (a checkpoint, a level change): no slide.
            sView.pos.assign(sa.pos[0], sa.pos[1], sa.pos[2]);
            sView.yaw = YawOf(sa.yaw);
            sView.anim = sa.anim;
            sView.animTime = sa.anim_time;
            return;
        }
        F32 f = B.at > A.at ? (F32)(t - A.at) / (F32)(B.at - A.at) : 1.0f;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        sView.pos.assign(sa.pos[0] + (sb.pos[0] - sa.pos[0]) * f, sa.pos[1] + (sb.pos[1] - sa.pos[1]) * f,
                      sa.pos[2] + (sb.pos[2] - sa.pos[2]) * f);
        S16 dy = (S16)(U16)(sb.yaw - sa.yaw); // the short way round
        sView.yaw = YawOf(sa.yaw) + (F32)dy / 65536.0f * kTwoPi * f;
        if (sa.anim == sb.anim && sb.anim_time >= sa.anim_time)
        {
            sView.anim = sa.anim;
            sView.animTime = sa.anim_time + (sb.anim_time - sa.anim_time) * f;
        }
        else
        {
            const iNet::PlayerState& pick = f < 0.5f ? sa : sb;
            sView.anim = pick.anim;
            sView.animTime = pick.anim_time;
        }
    }

} // namespace

void iCoopPuppetSceneExit()
{
    // The partner's model is built from the level's memory, which goes with
    // the level. The title menu draws its scene (and so iCoopRenderPartner)
    // without running iCoopFrame, which would otherwise pick a new one: it
    // must not draw the last level's.
    if (sPuppet != NULL)
    {
        printf("tssm: coop: the partner's model goes with the level (model %d)\n", (int)sPuppetModel);
        fflush(stdout);
    }
    sPuppet = NULL;
    sPuppetLogged = NULL;
    sView.body = false;
    sView.tag = false;
    iCoopSetNameTag(false, 0.0f, 0.0f, false);
}

namespace
{
    // M5: co-op's own cost (iCoopWorldInternal.h), summed over 5 s.
    U64 sCostSum[ICOOP_COST_KINDS], sCostFrameMax[ICOOP_COST_KINDS], sCostThisFrame[ICOOP_COST_KINDS];
    U64 sCostFrom;
    U32 sCostFrames;
    U64 sCostGapSum, sCostGapMax;

    // Called at the start of each co-op frame: the last frame's sums, and the
    // log line every 5 s (TSSM_COOP_LOG).
    void CostFrame(U64 now, U64 gap)
    {
        for (S32 k = 0; k < ICOOP_COST_KINDS; k++)
        {
            sCostSum[k] += sCostThisFrame[k];
            sCostFrameMax[k] = sCostThisFrame[k] > sCostFrameMax[k] ? sCostThisFrame[k] : sCostFrameMax[k];
            sCostThisFrame[k] = 0;
        }
        if (gap != 0 && gap < 1000 * kNsPerMs)
        {
            sCostFrames++;
            sCostGapSum += gap;
            sCostGapMax = gap > sCostGapMax ? gap : sCostGapMax;
        }
        if (sCostFrom == 0)
        {
            sCostFrom = now;
        }
        if (now - sCostFrom < 5000 * kNsPerMs)
        {
            return;
        }
        if (iCoopLogEvery() > 0 && sCostFrames > 0)
        {
            F64 n = (F64)sCostFrames;
            printf("tssm: coop: cost per frame (us, mean/max): frame %.0f/%.0f, drawing the partner %.0f/%.0f, "
                   "posing the host's things %.0f/%.0f, stand-ins %.0f/%.0f; %u frames, %.1f ms each (max %.1f)\n",
                   sCostSum[ICOOP_COST_FRAME] / n / 1e3, sCostFrameMax[ICOOP_COST_FRAME] / 1e3,
                   sCostSum[ICOOP_COST_DRAW] / n / 1e3, sCostFrameMax[ICOOP_COST_DRAW] / 1e3,
                   sCostSum[ICOOP_COST_POSE] / n / 1e3, sCostFrameMax[ICOOP_COST_POSE] / 1e3,
                   sCostSum[ICOOP_COST_STANDIN] / n / 1e3, sCostFrameMax[ICOOP_COST_STANDIN] / 1e3,
                   (unsigned)sCostFrames, sCostGapSum / n / 1e6, sCostGapMax / 1e6);
            fflush(stdout);
        }
        for (S32 k = 0; k < ICOOP_COST_KINDS; k++)
        {
            sCostSum[k] = sCostFrameMax[k] = 0;
        }
        sCostFrom = now;
        sCostFrames = 0;
        sCostGapSum = sCostGapMax = 0;
    }
} // namespace

void iCoopCostAdd(S32 kind, U64 ns)
{
    if (kind >= 0 && kind < ICOOP_COST_KINDS)
    {
        sCostThisFrame[kind] += ns;
    }
}

void iCoopFrame(F32 dt, S32 paused)
{
    if (!iCoopRunning())
    {
        sPuppet = NULL;
        iCoopWorldReset(); // nothing to do when it already is
        return;
    }
    if (!InGameplay())
    {
        iCoopSetNameTag(false, 0.0f, 0.0f, false);
        iCoopNoteScene(0);
        return;
    }
    iCoopNoteScene(globals.sceneCur->sceneID);
    sFrame++;
    U64 now = iCoopNowNs();
    CostFrame(now, sLastFrameAt != 0 ? now - sLastFrameAt : 0);
    iCoopCostScope cost(ICOOP_COST_FRAME);

    // A stall of this game (its window held, a hitch, a load): logged, and
    // the shared world goes on from the partner's newest (5.11).
    if (sLastFrameAt != 0 && now - sLastFrameAt >= kLocalStall)
    {
        if (iCoopConnected())
        {
            printf("tssm: coop: this game ran no frame for %d ms (a stall); going on from the partner's newest"
                   " (clock ms %llu)\n",
                   (int)((now - sLastFrameAt) / kNsPerMs), (unsigned long long)(now / kNsPerMs));
            fflush(stdout);
        }
        iCoopWorldLocalStall(now, now - sLastFrameAt);
    }
    sLastFrameAt = now;
    sPartnerStalled = iCoopPartnerStalled(&sPartnerStallMs);
    {
        // The partner's game stalled long enough to be noticed (the HUD says
        // so while it lasts), and when it runs again.
        bool wait = sPartnerStalled && sPartnerStallMs >= 2000;
        if (sWaitShown && !sPartnerStalled)
        {
            sWaitShown = false;
            iCoopAddNotice("the other player's game is running again");
            printf("tssm: coop: the partner's game runs again\n");
            fflush(stdout);
        }
        else if (wait && !sWaitShown)
        {
            sWaitShown = true;
            printf("tssm: coop: the partner's game has run no frame for %d ms; waiting for it\n",
                   (int)sPartnerStallMs);
            fflush(stdout);
        }
    }

    // A new session: forget the last partner.
    U32 epoch = iCoopSessionEpoch();
    if (epoch != sEpoch)
    {
        sEpoch = epoch;
        sHistCount = 0;
        sSeenSeq = 0;
        sUnknownAnim = 0;
        sUnknownLogged = 0;
        sPuppetLogged = NULL;
        sView.have = false;
        iCoopWorldReset();
        iCoopLookReset();
    }

    // This player, out.
    S32 logEvery = iCoopLogEvery();
    bool logFrame = logEvery > 0 && sFrame % (U32)logEvery == 0;
    if (iCoopConnected() && (now - sLastSend >= kSendEvery || logFrame) && iNet::scene_valid(globals.sceneCur->sceneID))
    {
        iNet::PlayerState s;
        ReadLocal(dt, paused, &s);
        // The host's snapshot of the shared world, or the guest's
        // acknowledgement, in the same datagram (iCoopWorld.cpp).
        const iNet::Snapshot* snap = NULL;
        U32 ack = 0;
        const iNet::Pickups* pick = NULL;
        const iNet::Progress* prog = NULL;
        const iNet::Counters* cnt = NULL;
        iCoopWorldOutgoing(now, &snap, &ack, &pick, &prog, &cnt);
        iCoopPostLocal(s, snap, ack, pick, prog, cnt);
        sLastSend = now;
        if (logFrame)
        {
            LogState("self", s);
        }
    }

    // The partner, in.
    iCoopSample fresh[kHistory];
    S32 n = iCoopTakeSamples(fresh, kHistory, &sSeenSeq);
    for (S32 i = 0; i < n; i++)
    {
        if (logEvery > 0 && fresh[i].s.frame % (U32)logEvery == 0)
        {
            LogState("partner", fresh[i].s);
        }
        if (sHistCount == kHistory)
        {
            memmove(sHist, sHist + 1, sizeof(iCoopSample) * (kHistory - 1));
            sHistCount--;
        }
        sHist[sHistCount++] = fresh[i];
    }

    sView.have = sHistCount > 0 && iCoopConnected();
    // The shared world: the guest's snapshots in, the host's stand-in and
    // culling, following the host's level.
    iCoopWorldFrame(now, sView.have ? &sHist[sHistCount - 1].s : NULL, sView.have ? sHist[sHistCount - 1].at : 0);
    // M4: characters and costumes (also with nobody connected yet: the
    // shortcut works while co-op is on).
    iCoopLookFrame(now, sView.have ? &sHist[sHistCount - 1].s : NULL);
    if (!sView.have)
    {
        sView.fresh = sView.sameLevel = sView.body = sView.tag = false;
        iCoopSetPartnerView(false, 0, 0, false, "");
        iCoopSetNameTag(false, 0.0f, 0.0f, false);
        sPuppet = NULL;
        return;
    }

    Place(now > kInterpDelay ? now - kInterpDelay : 0);
    const iNet::PlayerState& s = sView.s;
    // A partner whose game is stalled stays where it stands (5.11).
    sView.fresh = iCoopAge(now, sHist[sHistCount - 1].at) < kStale || sPartnerStalled;
    sView.sameLevel = s.scene == globals.sceneCur->sceneID;

    if (s.scene != sLevelScene)
    {
        sLevelScene = s.scene;
        snprintf(sLevelName, sizeof(sLevelName), "%s", zSceneGetLevelName(s.scene));
    }
    iCoopSetPartnerView(sView.fresh, s.scene, s.character, sView.sameLevel, sLevelName, s.model);

    bool hidden = !sView.fresh || !sView.sameLevel || !(s.flags & iNet::PS_VISIBLE) ||
                  (s.flags & (iNet::PS_SPECIAL | iNet::PS_CUTSCENE)) || globals.cmgr != NULL;
    sView.tag = sView.fresh && sView.sameLevel && !(s.flags & iNet::PS_CUTSCENE) && globals.cmgr == NULL;
    // The partner's own model (built here, in the update, not while drawing).
    sPuppet = hidden ? NULL : iCoopLookPuppetModel(s.model, s.character, &sPuppetModel);
    sView.body = sPuppet != NULL;
    // The tag's place is worked out where the scene is drawn (the camera is
    // set up there); until then, and on a frame that draws no scene, the last
    // one stands.
    if (!sView.tag)
    {
        iCoopSetNameTag(false, 0.0f, 0.0f, false);
    }
}

void iCoopRenderPartner()
{
    if (!iCoopRunning() || !sView.have || !InGameplay())
    {
        return;
    }
    iCoopCostScope cost(ICOOP_COST_DRAW);

    xVec3 tagAt = { sView.pos.x, sView.pos.y + kTagHeight, sView.pos.z };
    if (sView.tag && globals.screen != NULL && globals.screen->icam != NULL)
    {
        // The game camera's view matrix maps the world to the picture: x/z and
        // y/z are fractions of it, 0..1, y down (librw's camera sync).
        xMat4x3 view = *(xMat4x3*)&globals.screen->icam->viewMatrix;
        xVec3 c;
        xMat4x3Toworld(&c, &view, &tagAt);
        bool inFront = c.z > 0.01f;
        F32 z = inFront ? c.z : (c.z < -0.01f ? -c.z : 0.01f);
        iCoopSetNameTag(true, c.x / z, c.y / z, inFront);
        S32 every = iCoopLogEvery();
        if (every > 0 && sFrame % (U32)every == 0)
        {
            printf("tssm: coop: tag frame %u at %.3f %.3f%s\n", (unsigned)sFrame, c.x / z, c.y / z,
                   inFront ? "" : " (behind the camera)");
        }
    }

    if (!sView.body || sPuppet == NULL)
    {
        return;
    }
    xModelInstance* m = sPuppet;

    xAnimPlay* play = m->Anim;
    xAnimSingle* single = play->Single;
    if (single->State == NULL || single->State->ID != sView.anim)
    {
        xAnimState* st = xAnimTableGetStateID(play->Table, sView.anim);
        if (st != NULL)
        {
            xAnimPlaySetState(single, st, sView.animTime);
        }
        else if (sUnknownAnim != sView.anim && sUnknownLogged < 8)
        {
            sUnknownAnim = sView.anim;
            sUnknownLogged++;
            printf("tssm: coop: unknown animation state %08x; the partner keeps its last pose\n",
                   (unsigned)sView.anim);
        }
    }
    if (single->State == NULL)
    {
        return;
    }
    if (single->State->ID == sView.anim)
    {
        // An unknown state leaves the last pose as it was, time included.
        F32 t = sView.animTime;
        if (single->State->Data != NULL && t > single->State->Data->Duration)
        {
            t = single->State->Data->Duration;
        }
        single->Time = t < 0.0f ? 0.0f : t;
    }

    // The root: the network's position and heading.
    xMat4x3& root = *(xMat4x3*)m->Mat;
    xMat3x3Euler(&root, sView.yaw, 0.0f, 0.0f);
    root.pos = sView.pos;

    // The partner's visible parts, the player's light kit, and the pose
    // without the player's own callbacks; everything put back afterwards.
    U16 flags[kMaxParts];
    xLightKit* kits[kMaxParts];
    S32 count = 0;
    U16 parts = iCoopLookPartsFor(sView.s.parts, sView.s.model, sPuppetModel);
    S32 logEvery = iCoopLogEvery();
    if (parts != sView.s.parts && logEvery > 0 && sFrame % (U32)logEvery == 0)
    {
        printf("tssm: coop: the partner's parts %04x on model %u are %04x on model %d\n", (unsigned)sView.s.parts,
               (unsigned)sView.s.model, (unsigned)parts, (int)sPuppetModel);
    }
    for (xModelInstance* p = m; p != NULL && count < kMaxParts; p = p->Next, count++)
    {
        flags[count] = p->Flags;
        kits[count] = p->LightKit;
        if (parts & (1u << count))
        {
            p->Flags |= 0x1;
        }
        else
        {
            p->Flags &= (U16)~0x1;
        }
        p->LightKit = globals.player.pPlayerLightKit;
    }

    xAnimStateBeforeAnimMatricesCallback stateCb = single->State->BeforeAnimMatrices;
    void (*playCb)(xAnimPlay*, xQuat*, xVec3*, S32) = play->BeforeAnimMatrices;
    single->State->BeforeAnimMatrices = NULL;
    play->BeforeAnimMatrices = NULL;
    xModelEval(m);
    single->State->BeforeAnimMatrices = stateCb;
    play->BeforeAnimMatrices = playCb;

    S32 bucket = xModelBucketEnabled;
    xModelBucketEnabled = 0;
    xModelRender(m);
    xModelBucketEnabled = bucket;
    if (sPuppetLogged != m)
    {
        sPuppetLogged = m;
        // Rationed (a burst of ten, then one a second): the model follows the
        // partner's state, so a peer that keeps changing it cannot fill the log.
        U64 now = iCoopNowNs();
        sDrawnLogTokens += (F32)((now - sDrawnLogAt) / 1000000ull) / 1000.0f;
        sDrawnLogAt = now;
        sDrawnLogTokens = sDrawnLogTokens > 10.0f ? 10.0f : sDrawnLogTokens;
        if (sDrawnLogTokens >= 1.0f)
        {
            sDrawnLogTokens -= 1.0f;
            printf("tssm: coop: the partner is drawn as %s (model %d)\n",
                   sView.s.character == iNet::CHAR_PATRICK ? "Patrick" : "SpongeBob", (int)sPuppetModel);
            fflush(stdout);
        }
    }

    S32 i = 0;
    for (xModelInstance* p = m; p != NULL && i < count; p = p->Next, i++)
    {
        p->Flags = flags[i];
        p->LightKit = kits[i];
    }
}

void iCoopSceneName(U32 scene, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    if (scene == 0)
    {
        return;
    }
    char id[5];
    for (int i = 0; i < 4; i++)
    {
        char c = (char)(scene >> (24 - 8 * i));
        id[i] = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '?';
    }
    id[4] = '\0';
    if (id[0] == 'M' && id[1] == 'N')
    {
        snprintf(out, outSize, "%s", "title screen");
        return;
    }
    // The ID came over the network; the game's own table names it, or not.
    const char* name = zSceneGetLevelName(scene);
    if (name == NULL || strcmp(name, "????") == 0)
    {
        snprintf(out, outSize, "%s", id);
        return;
    }
    snprintf(out, outSize, "%s", name);
}
