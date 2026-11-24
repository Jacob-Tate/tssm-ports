// PC-only, online co-op: the boss fights (docs/MULTIPLAYER.md 16).
//
// A boss is a shared enemy like the others (iCoopWorld.cpp's table): the host's
// game runs the fight, with the guest's player standing in when the boss goes
// for the guest (iCoopStandInBegin); the guest's copy thinks nothing and is
// posed from the host's snapshots; the guest's hits on it are claims the host
// checks (iCoopCombat.cpp). What a snapshot cannot carry is the fight itself:
// the boss's state, stage, health, attack and camera. The host sends those as
// EV_BOSS whenever they change (and once a second, for a guest that arrives
// mid-fight), and the guest's copy shows them: its health meter, the sounds a
// new state starts with, and this game's own boss camera.
//
// Bosses so far: the Frogfish (B101). The others stay each game's own.
#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopWorldInternal.h"

#include "xEvent.h"
#include "zEntPlayer.h"
#include "zGame.h"
#include "zGlobals.h"
#include "zNMECommon.h"
#include "zNMENPCWrapper.h"
#include "zNMETypeBossFrogfish.h"
#include "zNPCTypes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

using namespace iNet;

namespace
{
    const U64 kResendEvery = 1000000000ull; // ns: the whole state again, for a guest that just arrived

    struct BossState
    {
        U8 kind; // BOSS_*, 0: none
        S16 state, health, attack;
        U8 stage, cam;
        bool alive; // host only, not sent: the fight is on (the shared health applies)
        bool operator!=(const BossState& o) const
        {
            return kind != o.kind || state != o.state || health != o.health || attack != o.attack ||
                   stage != o.stage || cam != o.cam;
        }
    };

    // Per net ID of the current table: the host's last sent, the guest's
    // last received.
    std::vector<BossState> sState;
    U32 sTableScene, sTableHash;
    U64 sLastSent;

    // --- one health for both players during a boss fight (16.3) ---------------------------
    const U64 kPoolHold = 3000000000ull; // ns: the guest keeps the pool this long after the host's last word
    // Host: the pool, the host's own health as last seen or set, and what was
    // last sent.
    bool sPoolOn;
    S32 sPool;
    U32 sHostLast;
    bool sHostKilling; // the pool ran out: the host's defeat is on its way
    S32 sPoolSent;
    U64 sPoolSentAt;
    // Guest: in the pool until this time; its own health as last seen or set.
    U64 sGuestPoolUntil;
    U32 sGuestLast;

    zPlayer* Me()
    {
        return xglobals->___player_ent_dont_use_directly;
    }

    S32 MaxOf()
    {
        S32 mx = (S32)globals.player.s->MaxHealth;
        return mx < 1 ? 1 : (mx > kPlayerHealthMax ? kPlayerHealthMax : mx);
    }

    void PoolLog(const char* what, S32 a, S32 b)
    {
        if (iCoopLogEvery() > 0)
        {
            printf("tssm: coop: shared health: %s %d -> %d\n", what, (int)a, (int)b);
            fflush(stdout);
        }
    }

    U8 KindOf(xEnt* e)
    {
        if (e == NULL)
        {
            return 0;
        }
        zNMENPCWrapper* w = static_cast<zNMENPCWrapper*>(e);
        switch ((U32)w->SelfType())
        {
        case NME_TYPE_FROGFISH: return BOSS_FROGFISH;
        default: return 0;
        }
    }

    S16 Clamp(S32 v, S32 hi)
    {
        return (S16)(v < 0 ? 0 : (v > hi ? hi : v));
    }

    bool Read(xEnt* e, U8 kind, BossState* out)
    {
        memset(out, 0, sizeof(*out));
        out->kind = kind;
        switch (kind)
        {
        case BOSS_FROGFISH:
        {
            zNMEFrogfish* f = static_cast<zNMEFrogfish*>(static_cast<zNMENPCWrapper*>(e));
            out->alive = f->state != ezFFBOSS_STATE_DYING && f->state != ezFFBOSS_STATE_DEAD;
            out->state = Clamp((S32)f->state, kBossStateMax);
            out->health = Clamp(f->ptsHealth, kBossHealthMax);
            out->attack = Clamp(f->attackCurr, kBossAttackMax);
            out->stage = (U8)Clamp(f->stageCurr, kBossStageMax);
            out->cam = (U8)Clamp((S32)f->camState, kBossCamMax);
            return true;
        }
        default: return false;
        }
    }

    // The table this vector belongs to; a new level (or a rebuilt table)
    // starts it again.
    bool TableNow(U32* scene, U32* hash, S32* count)
    {
        if (!iCoopWorldTable(scene, hash, count))
        {
            sState.clear();
            return false;
        }
        if (*scene != sTableScene || *hash != sTableHash || sState.size() != (size_t)*count)
        {
            sTableScene = *scene;
            sTableHash = *hash;
            BossState none;
            memset(&none, 0, sizeof(none));
            sState.assign((size_t)*count, none);
            sLastSent = 0;
        }
        return true;
    }
} // namespace

namespace
{
    void SendPool(U32 scene, S32 value, U64 now)
    {
        GameEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_POOL;
        ev.scene = scene;
        ev.dir[0] = (S16)value;
        if (iCoopPushEvent(ev))
        {
            sPoolSent = value;
            sPoolSentAt = now;
        }
    }

    // Host, once a frame: while a boss fight is on with the guest here, the
    // host's own health changes go into the pool (the guest's come as
    // EV_POOL_DELTA), and the host's player has the pool's health. The pool
    // running out defeats both.
    void HostPool(U32 scene, bool fight, U64 now)
    {
        zPlayer* me = Me();
        if (!fight || me == NULL || globals.player.s == NULL)
        {
            if (sPoolOn)
            {
                PoolLog("the fight is over; each player keeps their own, from", sPool, sPool);
            }
            sPoolOn = false;
            sHostKilling = false;
            return;
        }
        U32 h = globals.player.s->Health;
        if (!sPoolOn)
        {
            sPoolOn = true;
            sPool = (S32)h;
            sHostLast = h;
            sHostKilling = false;
            sPoolSent = -1;
            PoolLog("a boss fight: one health for both, starting at the host's", (S32)h, (S32)h);
        }
        else if (sHostKilling)
        {
            if (h == 0)
            {
                sHostKilling = false;
                sHostLast = 0;
            }
            return; // the defeat is on its way: nothing is counted meanwhile
        }
        else if (h != sHostLast)
        {
            S32 was = sPool;
            // Back from a defeat (the level's reset), the pool starts again
            // from the host's new health; otherwise the change counts.
            sPool = sHostLast == 0 ? (S32)h : sPool + ((S32)h - (S32)sHostLast);
            sPool = sPool < 0 ? 0 : (sPool > MaxOf() ? MaxOf() : sPool);
            PoolLog("the host's own change", was, sPool);
        }
        if (h > 0 && sPool == 0)
        {
            // The guest's hit emptied it: the host is defeated too, as the
            // game defeats its player.
            sHostKilling = true;
            sHostLast = h;
            zEntEvent(me, eEventKill);
            PoolLog("empty: both players are defeated", 0, 0);
        }
        else if (h > 0 && (S32)h != sPool)
        {
            globals.player.s->Health = (U32)sPool;
            h = (U32)sPool;
        }
        if (!sHostKilling)
        {
            sHostLast = h;
        }
        if (sPool != sPoolSent || now - sPoolSentAt >= kResendEvery)
        {
            SendPool(scene, sPool, now);
        }
    }

    // Guest, once a frame: its own health changes (a hit, a pickup) go to the
    // host while the pool is on. A defeat, and coming back from one, are not
    // changes: the pool decides those.
    void GuestPool(U64 now)
    {
        if (sGuestPoolUntil == 0 || now >= sGuestPoolUntil || Me() == NULL || globals.player.s == NULL)
        {
            sGuestPoolUntil = 0;
            return;
        }
        U32 h = globals.player.s->Health;
        if (iCoopCombatRespawning() || h == 0 || sGuestLast == 0)
        {
            sGuestLast = h;
            return;
        }
        if (h == sGuestLast)
        {
            return;
        }
        S32 d = (S32)h - (S32)sGuestLast;
        d = d > kPlayerHealthMax ? kPlayerHealthMax : (d < -kPlayerHealthMax ? -kPlayerHealthMax : d);
        GameEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_POOL_DELTA;
        ev.scene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        ev.dir[0] = (S16)d;
        if (ev.scene != 0 && iCoopPushEvent(ev))
        {
            PoolLog("this player's own change, sent to the host", (S32)sGuestLast, (S32)h);
            sGuestLast = h;
        }
    }
} // namespace

void iCoopBossFrame(U64 now, bool guest)
{
    U32 scene, hash;
    S32 count;
    if (guest)
    {
        GuestPool(now);
    }
    if (!TableNow(&scene, &hash, &count) || guest)
    {
        if (!guest)
        {
            HostPool(0, false, now);
        }
        return;
    }
    F32 at[3];
    if (!iCoopWorldPartnerHere(at))
    {
        HostPool(scene, false, now);
        return; // no guest in this level to tell
    }
    bool resend = sLastSent == 0 || now - sLastSent >= kResendEvery;
    bool fight = false;
    for (S32 id = 0; id < count; id++)
    {
        xEnt* e = iCoopWorldEnemy(id);
        U8 kind = KindOf(e);
        BossState st;
        if (kind == 0 || !Read(e, kind, &st))
        {
            continue;
        }
        fight = fight || st.alive;
        if (!resend && !(st != sState[id]))
        {
            continue;
        }
        GameEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_BOSS;
        ev.scene = scene;
        ev.table = hash;
        ev.id = (U16)id;
        ev.kind = st.kind;
        ev.level = st.stage;
        ev.sub = st.cam;
        ev.dir[0] = st.state;
        ev.dir[1] = st.health;
        ev.dir[2] = st.attack;
        if (iCoopPushEvent(ev))
        {
            if (st.state != sState[id].state && iCoopLogEvery() > 0)
            {
                printf("tssm: coop: boss %d: state %d, stage %d, health %d\n", (int)id, (int)st.state,
                       (int)st.stage, (int)st.health);
                fflush(stdout);
            }
            sState[id] = st;
        }
    }
    if (resend)
    {
        sLastSent = now;
    }
    HostPool(scene, fight, now);
}

void iCoopBossPoolEvent(const GameEvent& e, U64 now)
{
    zPlayer* me = Me();
    U32 scene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
    if (me == NULL || globals.player.s == NULL || e.scene != scene)
    {
        return;
    }
    if (e.type == EV_POOL_DELTA)
    {
        // Host: the guest's own change, while the fight is on and the host
        // stands (a defeat on its way counts nothing).
        if (!sPoolOn || sHostKilling || globals.player.s->Health == 0)
        {
            return;
        }
        S32 was = sPool;
        sPool += e.dir[0];
        sPool = sPool < 0 ? 0 : (sPool > MaxOf() ? MaxOf() : sPool);
        PoolLog("the guest's change", was, sPool);
        return;
    }
    // Guest: the pool's value.
    sGuestPoolUntil = now + kPoolHold;
    U32 h = globals.player.s->Health;
    if (iCoopCombatRespawning() || h == 0)
    {
        sGuestLast = h;
        return; // defeated already: the defeat plays out
    }
    if (e.dir[0] == 0)
    {
        sGuestLast = 0; // nothing more is reported until this player is back
        zEntEvent(me, eEventKill);
        PoolLog("empty: this player is defeated with the host", (S32)h, 0);
        return;
    }
    S32 v = e.dir[0] > MaxOf() ? MaxOf() : e.dir[0];
    if ((U32)v != h)
    {
        globals.player.s->Health = (U32)v;
    }
    sGuestLast = (U32)v;
}

void iCoopBossEvent(const GameEvent& e)
{
    U32 scene, hash;
    S32 count;
    if (!TableNow(&scene, &hash, &count) || e.scene != scene || e.table != hash || e.id >= (U16)count)
    {
        return; // another level or table: the next resend finds this one
    }
    // The kind must be what this game's table has at that net ID.
    if (KindOf(iCoopWorldEnemy(e.id)) != e.kind)
    {
        return;
    }
    BossState& st = sState[e.id];
    st.kind = e.kind;
    st.stage = e.level;
    st.cam = e.sub;
    st.state = e.dir[0];
    st.health = e.dir[1];
    st.attack = e.dir[2];
}

void iCoopBossGuestShow(S32 id, xEnt* e, F32 dt)
{
    if (id < 0 || (size_t)id >= sState.size())
    {
        return;
    }
    const BossState& st = sState[id];
    if (st.kind == 0 || st.kind != KindOf(e))
    {
        return; // nothing from the host yet
    }
    switch (st.kind)
    {
    case BOSS_FROGFISH:
        // The game's own ranges (the wire's are wider, for other bosses).
        if (st.state > ezFFBOSS_STATE_DEAD || st.stage >= exFFBOSS_STAGE_NOMORE ||
            st.cam >= ezFFBOSS_CAMERA_STATE_NOMORE)
        {
            return;
        }
        static_cast<zNMEFrogfish*>(static_cast<zNMENPCWrapper*>(e))
            ->CoopGuestShow(st.state, st.stage, st.cam, st.health, st.attack, dt);
        break;
    default: break;
    }
}

F32 iCoopBossReach(xEnt* e)
{
    if (KindOf(e) == 0 || e->model == NULL || e->model->Mat == NULL)
    {
        return 0.0f;
    }
    // A boss is far larger than the reach of a melee attack from its middle:
    // the guest may hit anywhere on its bound (the host's, as its update set
    // it). At most 30 units, whatever the bound says.
    const xVec3& o = *(const xVec3*)&e->model->Mat->pos;
    const xVec3& c = e->bound.sph.center;
    F32 dx = c.x - o.x, dy = c.y - o.y, dz = c.z - o.z;
    F32 r = sqrtf(dx * dx + dy * dy + dz * dz) + e->bound.sph.r;
    return r == r ? (r > 30.0f ? 30.0f : r) : 0.0f;
}

void iCoopBossReset()
{
    sPoolOn = false;
    sHostKilling = false;
    sGuestPoolUntil = 0;
    sGuestLast = 0;
    sState.clear();
    sTableScene = sTableHash = 0;
    sLastSent = 0;
}
