// PC-only: co-op's playing through together (docs/MULTIPLAYER.md 7.1-7.3 and
// 7.6). Compiled with the game units.
//
//   - Level changes. The host leads: when its game leaves a level through a
//     portal, the guest is told at once (EV_LEVEL) and its game goes there too
//     (iCoopWorld.cpp's following, without M2's wait). Both games then load the
//     level, and each waits for the other between the load and the level's
//     start (EV_READY, EV_GO; at most 10 s), so its opening fly-through starts
//     on both at the same moment. A portal the guest walks into while the host
//     is in its level does nothing (the host decides when both leave).
//   - Cutscenes and fly-throughs. When one starts or ends on the host, the
//     guest is told (EV_SHOW: which one of the level's own, by its place in
//     the level) and its game starts or ends its own copy of it, with the
//     game's own events. While the host watches one, only the host can skip
//     it; the guest's skip button does nothing.
//   - Saving. The host's save is the only one written while a guest is in the
//     session (iCoopNoAutoSave, and a guest's save by hand is refused).
//
// Network values: the scene of EV_LEVEL (A-Z 0-9 by the parser, and a level
// of the game's own world table before anything goes there), and EV_SHOW's
// index, checked against this level's own table of cutscenes and fly-throughs
// whose hash must match the host's.

#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopLocal.h"
#include "iCoopWorldInternal.h"
#include "iWindow.h"

#include "xBase.h"
#include "xEvent.h"
#include "xScreenFade.h"
#include "xString.h"
#include "zBase.h"
#include "zCamera.h"
#include "zCameraFly.h"
#include "zCutsceneMgr.h"
#include "zDispatcher.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zScene.h"

#include <stdio.h>
#include <string.h>

#include <chrono>
#include <thread>
#include <vector>

// zCamera.cpp (PLATFORM_PC): ends the running fly-through as the game's own
// skip does, so it stops the game's own way.
void zCameraFlyFinishPC();

using namespace iNet;

namespace
{
    const U64 kNsPerMs = 1000000ull;
    const U64 kWaitMax = 10000 * kNsPerMs;      // a level's start waits at most this long for the other game
    const U64 kLeadFresh = 60000 * kNsPerMs;    // an EV_LEVEL is acted on this long
    const U64 kRefuseNoticeEvery = 5000 * kNsPerMs;
    // The host's own state has said this long, fresh, that it is in no show
    // while its show still plays here: it ended there without its end.
    const U64 kHostOutOfShow = 1000 * kNsPerMs;
    const U64 kStateFresh = 500 * kNsPerMs;
    const S32 kShowsPerSecond = 20;             // EV_SHOW taken at most this often (more are logged and dropped)
    const S32 kLevelsPerSecond = 4;             // EV_LEVEL likewise

    void SceneText(U32 id, char out[5])
    {
        for (int i = 0; i < 4; i++)
        {
            char c = (char)(id >> (24 - 8 * i));
            out[i] = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '?';
        }
        out[4] = '\0';
    }

    bool Guest()
    {
        return iCoopLocalSlot() == 1;
    }

    // --- the level's shows: its cutscene managers and camera fly-throughs ---------------

    zScene* sShowScene;
    U32 sShowSceneId;
    std::vector<xBase*> sShows;
    U32 sShowHash;

    void BuildShows()
    {
        zScene* sc = globals.sceneCur;
        if (sc == sShowScene && sc != NULL && sc->sceneID == sShowSceneId)
        {
            return;
        }
        sShowScene = sc;
        sShowSceneId = sc != NULL ? sc->sceneID : 0;
        sShows.clear();
        U32 h = 2166136261u;
        if (sc != NULL)
        {
            for (U16 i = 0; i < sc->num_base && sShows.size() < kMaxShows; i++)
            {
                xBase* b = sc->base[i];
                if (b != NULL && (b->baseType == eBaseTypeCutsceneMgr || b->baseType == eBaseTypeCameraFly))
                {
                    sShows.push_back(b);
                    U32 v[2] = { (U32)b->baseType, b->id };
                    for (int k = 0; k < 2; k++)
                    {
                        for (int j = 0; j < 4; j++)
                        {
                            h ^= (v[k] >> (8 * j)) & 0xFF;
                            h *= 16777619u;
                        }
                    }
                }
            }
        }
        sShowHash = h;
    }

    S32 ShowIndex(const xBase* b)
    {
        BuildShows();
        for (size_t i = 0; i < sShows.size(); i++)
        {
            if (sShows[i] == b)
            {
                return (S32)i;
            }
        }
        return -1;
    }

    xBase* FlyRunning()
    {
        if (!zCameraIsFlyCamRunning())
        {
            return NULL;
        }
        BuildShows();
        for (size_t i = 0; i < sShows.size(); i++)
        {
            xBase* b = sShows[i];
            if (b->baseType == eBaseTypeCameraFly && ((zCameraFly*)b)->casset->flyID == zcam_flyasset_current)
            {
                return b;
            }
        }
        return NULL;
    }

    // --- state ------------------------------------------------------------------------------

    // Host: the level it went to while the guest was in its level, and when.
    U32 sLeftTo;
    U64 sLeftAt;
    bool sLeftWithGuest;
    // Guest: the host's newest EV_LEVEL, and the load following it.
    U32 sHostLeftTo;
    U64 sHostLeftAt;
    U32 sFollowLoad;
    // Guest: the show the host started here, or found already playing here
    // when it said it started it (only the host skips it). Cleared when the
    // host says it ended, when it ends here (iCoopShowCutsceneEnd,
    // iCoopShowFlyStop) and at the first frame it no longer plays here, so a
    // later replay of the same show by this game's own trigger is this
    // player's.
    xBase* sHostShow;
    // Guest: the show this game started by itself (its own trigger, or the
    // level's opening), and when: the host's word on it may still be coming.
    xBase* sLocalShow;
    U64 sLocalShowAt;
    // The newest partner state (game thread), for the hooks.
    PlayerState sPartner;
    bool sHavePartner;
    U64 sPartnerAt;
    U64 sRefuseNoticeAt;
    U64 sSkipRefusedAt;
    // Host: the level both just started together (the barrier), and when:
    // its opening shows go to the guest although its state still says the
    // last level.
    U32 sTogetherScene;
    U64 sTogetherAt;
    // Guest: a show ended here because the host left the level (logged once
    // per EV_LEVEL).
    bool sEndedForLead;
    bool sEndedForLeadLogged;
    // Guest: since when the host's fresh state has said it is in no show
    // while its show plays here (0: not so).
    U64 sHostOutSince;
    // Rate limits on the host's events.
    U64 sRateAt;
    S32 sShowsThisSecond;
    S32 sLevelsThisSecond;
    S32 sDroppedLogged;

    bool PartnerHereNow(U64 now)
    {
        return iCoopConnected() && sHavePartner && globals.sceneCur != NULL &&
               sPartner.scene == globals.sceneCur->sceneID && iCoopAge(now, sPartnerAt) < 3000 * kNsPerMs;
    }

    bool RateOk(U64 now, S32* counter, S32 limit)
    {
        if (now - sRateAt >= 1000 * kNsPerMs)
        {
            sRateAt = now;
            sShowsThisSecond = 0;
            sLevelsThisSecond = 0;
        }
        if (*counter >= limit)
        {
            if (sDroppedLogged < 8)
            {
                sDroppedLogged++;
                printf("tssm: coop: too many level or show events from the host this second; one was dropped\n");
                fflush(stdout);
            }
            return false;
        }
        (*counter)++;
        return true;
    }

    void PushShow(xBase* b, U8 kind, U8 flags)
    {
        S32 idx = ShowIndex(b);
        if (idx < 0 || globals.sceneCur == NULL || !scene_valid(globals.sceneCur->sceneID))
        {
            return;
        }
        GameEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_SHOW;
        e.scene = globals.sceneCur->sceneID;
        e.table = sShowHash;
        e.id = (U16)idx;
        e.kind = kind;
        e.level = kind == SHOW_CUTSCENE_START ? flags : 0;
        if (iCoopPushEvent(e))
        {
            static const char* const kText[SHOW_END] = { "", "cutscene start", "cutscene end", "fly-through start",
                                                         "fly-through end" };
            printf("tssm: coop: show %d (%s) sent to the guest\n", (int)idx, kText[kind]);
            fflush(stdout);
        }
    }

    void HostShow(U64 now, xBase* b, U8 kind, U8 flags)
    {
        if (Guest() || !iCoopConnected())
        {
            return;
        }
        bool arriving = globals.sceneCur != NULL && globals.sceneCur->sceneID == sTogetherScene &&
                        now - sTogetherAt < 15000 * kNsPerMs;
        if (!PartnerHereNow(now) && !arriving)
        {
            return;
        }
        PushShow(b, kind, flags);
    }

    // Waits up to kWaitMax for `done`, keeping the window answering. TRUE
    // when it came.
    template <class F> bool Wait(F done, U64* waited)
    {
        U64 from = iCoopNowNs();
        U64 lastPump = 0;
        for (;;)
        {
            U64 now = iCoopNowNs();
            *waited = now - from;
            if (done())
            {
                return true;
            }
            if (!iCoopConnected() || now - from >= kWaitMax)
            {
                return false;
            }
            if (now - lastPump >= 16 * kNsPerMs)
            {
                lastPump = now;
                iWindowPump();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
} // namespace

// --- for iCoopWorld.cpp ----------------------------------------------------------------------

void iCoopFlowReset()
{
    sLeftTo = 0;
    sLeftWithGuest = false;
    sHostLeftTo = 0;
    sFollowLoad = 0;
    sHostShow = NULL;
    sLocalShow = NULL;
    sHostOutSince = 0;
    sHavePartner = false;
}

void iCoopFlowFrame(U64 now, const PlayerState* partner, U64 partnerAt)
{
    if (partner != NULL)
    {
        sPartner = *partner;
        sHavePartner = true;
        sPartnerAt = partnerAt;
    }
    BuildShows();
    if (sHostShow != NULL && globals.cmgr != (zCutsceneMgr*)sHostShow && FlyRunning() != sHostShow)
    {
        sHostShow = NULL; // over
    }
    // The host's show can end there without its end: a level reset (the
    // host's defeat) ends a cutscene the game's own way, past the hook that
    // sends it. The guest cannot skip the host's show, so it would watch it
    // to its end alone. Once the host's own state, fresh (not a stalled host:
    // its state goes stale and this waits), has said for a second that it is
    // in no show, the copy here ends as the host's end would end it.
    if (Guest() && sHostShow != NULL && globals.sceneCur != NULL && sHavePartner &&
        iCoopAge(now, sPartnerAt) < kStateFresh && sPartner.scene == globals.sceneCur->sceneID &&
        !(sPartner.flags & PS_CUTSCENE))
    {
        if (sHostOutSince == 0)
        {
            sHostOutSince = now;
        }
        else if (now - sHostOutSince >= kHostOutOfShow)
        {
            S32 idx = ShowIndex(sHostShow);
            if (globals.cmgr == (zCutsceneMgr*)sHostShow)
            {
                globals.cmgr->stop = 1;
            }
            else if (FlyRunning() == sHostShow)
            {
                zCameraFlyFinishPC();
            }
            printf("tssm: coop: the host's show %d ended there without its end (the host is in no show); it ends "
                   "here too\n",
                   (int)idx);
            fflush(stdout);
            sHostShow = NULL;
            sHostOutSince = 0;
        }
    }
    else
    {
        sHostOutSince = 0;
    }
    // The lead goes stale once the guest is there (or after a while).
    if (sHostLeftTo != 0 && (now - sHostLeftAt > kLeadFresh ||
                             (globals.sceneCur != NULL && globals.sceneCur->sceneID == sHostLeftTo)))
    {
        sHostLeftTo = 0;
        sEndedForLead = false;
        sEndedForLeadLogged = false;
    }
    // The host has left this level: a cutscene or fly-through playing here
    // ends (as a skip ends it), so this game can go along.
    if (Guest() && sHostLeftTo != 0 && iCoopFollowEnabled())
    {
        if (globals.cmgr != NULL && !globals.cmgr->stop)
        {
            globals.cmgr->stop = 1;
            sEndedForLead = true;
        }
        if (zCameraIsFlyCamRunning())
        {
            zCameraFlyFinishPC();
            sEndedForLead = true;
        }
        if (sEndedForLead && !sEndedForLeadLogged)
        {
            sEndedForLeadLogged = true;
            printf("tssm: coop: the host left the level during a cutscene or fly-through; it ends here too\n");
            fflush(stdout);
        }
    }
}

void iCoopFlowEvent(const GameEvent& e, U64 now)
{
    if (!Guest())
    {
        return;
    }
    char scene[5];
    SceneText(e.scene, scene);
    if (e.type == EV_LEVEL)
    {
        if (!RateOk(now, &sLevelsThisSecond, kLevelsPerSecond))
        {
            return;
        }
        sHostLeftTo = e.scene;
        sHostLeftAt = now;
        printf("tssm: coop: the host is going to %s (EV_LEVEL)\n", scene);
        fflush(stdout);
        S32 world = 0, task = 0;
        zDispatcher_FindWorldTask(e.scene, world, task);
        if (!iCoopFollowEnabled() || world <= 0)
        {
            // Not going along (following is off in the Co-op tab, or no such
            // level here): the host is told at once, so it does not wait.
            GameEvent r;
            memset(&r, 0, sizeof(r));
            r.type = EV_READY;
            r.scene = globals.sceneCur != NULL && scene_valid(globals.sceneCur->sceneID) ? globals.sceneCur->sceneID
                                                                                          : 0;
            if (r.scene != 0 && r.scene != e.scene)
            {
                iCoopPushEvent(r);
            }
            sHostLeftTo = 0;
            printf("tssm: coop: not going along (%s); the host is told\n",
                   world <= 0 ? "not a level of this game" : "following is off");
            fflush(stdout);
        }
        return;
    }
    if (e.type != EV_SHOW || !RateOk(now, &sShowsThisSecond, kShowsPerSecond))
    {
        return;
    }
    BuildShows();
    if (globals.sceneCur == NULL || e.scene != globals.sceneCur->sceneID || e.table != sShowHash ||
        e.id >= sShows.size())
    {
        printf("tssm: coop: a show of %s for another level or table was ignored\n", scene);
        fflush(stdout);
        return;
    }
    xBase* b = sShows[e.id];
    bool cutscene = e.kind == SHOW_CUTSCENE_START || e.kind == SHOW_CUTSCENE_END;
    if (cutscene != (b->baseType == eBaseTypeCutsceneMgr))
    {
        printf("tssm: coop: show %d is not a %s here; ignored\n", (int)e.id, cutscene ? "cutscene" : "fly-through");
        fflush(stdout);
        return;
    }
    switch (e.kind)
    {
    case SHOW_CUTSCENE_START:
        if (globals.cmgr == (zCutsceneMgr*)b)
        {
            // The same one already plays here (both games' triggers started
            // it): it is the host's now, so only the host skips it.
            sHostShow = b;
            printf("tssm: coop: the host's cutscene %d: it already plays here\n", (int)e.id);
        }
        else if (globals.cmgr != NULL)
        {
            printf("tssm: coop: the host's cutscene %d starts while one plays here; not started\n", (int)e.id);
        }
        else
        {
            // As the host's game started it (the game's own start event).
            F32 param[4] = { (e.level & SHOWF_PAUSE_MUSIC) ? 0.0f : 1.0f, (e.level & SHOWF_LOAD_NOW) ? 1.0f : 0.0f,
                             0.0f, 0.0f };
            zEntEvent(b, 0xbe, param);
            if (globals.cmgr == (zCutsceneMgr*)b)
            {
                sHostShow = b;
                printf("tssm: coop: the host's cutscene %d plays here too\n", (int)e.id);
            }
            else
            {
                printf("tssm: coop: the host's cutscene %d did not start here\n", (int)e.id);
            }
        }
        break;
    case SHOW_CUTSCENE_END:
        // The host's show is over: a later replay of it here that this
        // game's own trigger starts is this player's to skip.
        if (sHostShow == b)
        {
            sHostShow = NULL;
        }
        if (globals.cmgr == (zCutsceneMgr*)b)
        {
            ((zCutsceneMgr*)b)->stop = 1;
            printf("tssm: coop: the host's cutscene %d ended; it ends here too\n", (int)e.id);
        }
        break;
    case SHOW_FLY_START:
        if (FlyRunning() == b)
        {
            // The same one already plays here (a level's opening starts on
            // both together): it is the host's now, so only the host skips it.
            sHostShow = b;
            printf("tssm: coop: the host's fly-through %d: one already plays here\n", (int)e.id);
        }
        else if (zCameraIsFlyCamRunning() || globals.cmgr != NULL)
        {
            printf("tssm: coop: the host's fly-through %d: another show plays here; not started\n", (int)e.id);
        }
        else
        {
            zEntEvent(b, eEventRun);
            if (FlyRunning() == b)
            {
                sHostShow = b;
                printf("tssm: coop: the host's fly-through %d plays here too\n", (int)e.id);
            }
        }
        break;
    case SHOW_FLY_END:
        if (sHostShow == b)
        {
            sHostShow = NULL;
        }
        if (FlyRunning() == b)
        {
            zCameraFlyFinishPC();
            printf("tssm: coop: the host's fly-through %d ended; it ends here too\n", (int)e.id);
        }
        break;
    }
    fflush(stdout);
}

void iCoopFlowSceneExit()
{
    zScene* sc = globals.sceneCur;
    U64 now = iCoopNowNs();
    if (!Guest() && iCoopConnected() && sc != NULL && sc->pendingPortal != NULL && sc->pendingPortal->passet != NULL)
    {
        U32 to = sc->pendingPortal->passet->sceneID;
        S32 world = 0, task = 0;
        if (scene_valid(to) && to != sc->sceneID)
        {
            zDispatcher_FindWorldTask(to, world, task);
        }
        if (world > 0)
        {
            GameEvent e;
            memset(&e, 0, sizeof(e));
            e.type = EV_LEVEL;
            e.scene = to;
            bool here = PartnerHereNow(now);
            if (iCoopPushEvent(e))
            {
                sLeftTo = to;
                sLeftAt = now;
                sLeftWithGuest = here;
                iCoopClearLevelSignal(EV_READY);
                char t[5];
                SceneText(to, t);
                printf("tssm: coop: going to %s; the guest is told (EV_LEVEL)%s\n", t,
                       here ? " and its arrival will be waited for" : "");
                fflush(stdout);
            }
        }
    }
    sShowScene = NULL;
    sShows.clear();
    sHostShow = NULL;
    sLocalShow = NULL;
}

U32 iCoopFlowHostLeftTo(U64 now)
{
    if (sHostLeftTo != 0 && now - sHostLeftAt <= kLeadFresh)
    {
        return sHostLeftTo;
    }
    return 0;
}

void iCoopFlowFollowIssued(U32 scene, bool lead)
{
    // The host's EV_GO for this level may have come already (it waited for
    // this game no longer): it is kept, the newest wins.
    sFollowLoad = lead ? scene : 0;
}

// --- the game's hooks ------------------------------------------------------------------------

void iCoopLevelStartBarrier(U32 scene)
{
    if (!iCoopRunning() || !iCoopConnected())
    {
        sLeftTo = sFollowLoad = 0;
        return;
    }
    char t[5];
    SceneText(scene, t);
    U64 waited = 0;
    U64 now = iCoopNowNs();
    if (!Guest())
    {
        if (sLeftTo != scene || now - sLeftAt > kLeadFresh)
        {
            sLeftTo = 0;
            return;
        }
        if (!sLeftWithGuest)
        {
            // The guest was elsewhere (still on its way here, or stalled):
            // not waited for. Its game still follows the EV_LEVEL and then
            // waits for this level's start, so the start is said at once
            // (else it would wait its whole 10 s for an EV_GO never sent).
            sLeftTo = 0;
            GameEvent go;
            memset(&go, 0, sizeof(go));
            go.type = EV_GO;
            go.scene = scene;
            iCoopPushEvent(go);
            printf("tssm: coop: %s is loaded; the guest was not in the last level, so it starts now (EV_GO)\n", t);
            fflush(stdout);
            return;
        }
        sLeftTo = 0;
        printf("tssm: coop: %s is loaded; waiting for the guest's game to load it too\n", t);
        fflush(stdout);
        // Done when the guest has loaded this level, or says it stays where it
        // is (its EV_READY names another level).
        bool ok = Wait([]() { return iCoopPeerLevelScene(EV_READY) != 0; }, &waited);
        bool stays = ok && iCoopPeerLevelScene(EV_READY) != scene;
        ok = ok && !stays;
        GameEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_GO;
        e.scene = scene;
        iCoopPushEvent(e);
        sTogetherScene = scene;
        sTogetherAt = iCoopNowNs();
        if (ok)
        {
            printf("tssm: coop: the guest has loaded %s (waited %d ms); both start it now (EV_GO) at clock ms %llu\n",
                   t, (int)(waited / kNsPerMs), (unsigned long long)(iCoopNowNs() / kNsPerMs));
        }
        else if (stays)
        {
            printf("tssm: coop: the guest stays in its level (following is off); %s starts now\n", t);
        }
        else
        {
            printf("tssm: coop: the guest has not loaded %s after %d ms; starting without waiting longer\n", t,
                   (int)(waited / kNsPerMs));
            iCoopAddNotice("started the level without waiting longer for the other player");
        }
        fflush(stdout);
        return;
    }
    if (sFollowLoad != scene)
    {
        sFollowLoad = 0;
        return;
    }
    sFollowLoad = 0;
    GameEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_READY;
    e.scene = scene;
    iCoopPushEvent(e);
    printf("tssm: coop: %s is loaded; told the host (EV_READY), waiting for its start\n", t);
    fflush(stdout);
    bool ok = Wait([scene]() { return iCoopPeerLevelSignal(EV_GO, scene); }, &waited);
    printf(ok ? "tssm: coop: the host starts %s (EV_GO, waited %d ms) at clock ms %llu\n"
              : "tssm: coop: no start from the host for %s after %d ms; starting anyway at clock ms %llu\n",
           t, (int)(waited / kNsPerMs), (unsigned long long)(iCoopNowNs() / kNsPerMs));
    fflush(stdout);
}

bool iCoopRefusePortal(U32 toScene)
{
    if (!iCoopRunning() || !Guest() || globals.sceneCur == NULL || toScene == globals.sceneCur->sceneID)
    {
        return false;
    }
    U64 now = iCoopNowNs();
    if (!PartnerHereNow(now))
    {
        return false;
    }
    if (now - sRefuseNoticeAt >= kRefuseNoticeEvery)
    {
        sRefuseNoticeAt = now;
        char t[5];
        SceneText(toScene, t);
        printf("tssm: coop: a portal to %s was not taken: the host leads the way to other levels\n", t);
        fflush(stdout);
        iCoopAddNotice("the host leads the way to other levels; this exit is theirs to take");
    }
    // The level's own end faded the screen down for this portal (a main
    // task's token: its fade's end sends the player through) and waits
    // there: it comes up now, and the host's level end takes this player
    // along when it comes.
    zScene* sc = globals.sceneCur;
    xScreenFade* fade = (xScreenFade*)sc->baseList[eBaseTypeScreenFade];
    for (U32 i = 0; fade != NULL && i < sc->baseCount[eBaseTypeScreenFade]; i++)
    {
        if (fade[i].state == eScreenFadeStateWait && fade[i].asset != NULL)
        {
            printf(
                "tssm: coop: the screen this exit faded down comes up again (its own wait was %.1f s)\n",
                fade[i].asset->waitTime);
            fflush(stdout);
            fade[i].state = eScreenFadeStateFadeUp;
            fade[i].time_passed = 0.0f;
        }
    }
    return true;
}

void iCoopShowCutsceneStart(xBase* mgr, const F32* param)
{
    if (!iCoopRunning())
    {
        return;
    }
    if (Guest() && sHostShow != mgr)
    {
        sLocalShow = mgr;
        sLocalShowAt = iCoopNowNs();
    }
    U8 flags = 0;
    if (param != NULL && param[0] == 0.0f)
    {
        flags |= SHOWF_PAUSE_MUSIC;
    }
    if (param == NULL || param[1] != 0.0f)
    {
        flags |= SHOWF_LOAD_NOW;
    }
    HostShow(iCoopNowNs(), mgr, SHOW_CUTSCENE_START, flags);
}

void iCoopShowCutsceneEnd(xBase* mgr)
{
    if (!iCoopRunning())
    {
        return;
    }
    if (sHostShow == mgr)
    {
        sHostShow = NULL;
    }
    HostShow(iCoopNowNs(), mgr, SHOW_CUTSCENE_END, 0);
}

void iCoopShowFlyStart(xBase* fly)
{
    if (!iCoopRunning() || !zCameraIsFlyCamRunning())
    {
        return;
    }
    if (Guest() && sHostShow != fly)
    {
        sLocalShow = fly;
        sLocalShowAt = iCoopNowNs();
    }
    HostShow(iCoopNowNs(), fly, SHOW_FLY_START, 0);
}

void iCoopShowFlyStop()
{
    if (!iCoopRunning())
    {
        return;
    }
    xBase* fly = FlyRunning();
    if (fly == NULL)
    {
        return;
    }
    if (sHostShow == fly)
    {
        sHostShow = NULL;
    }
    HostShow(iCoopNowNs(), fly, SHOW_FLY_END, 0);
}

bool iCoopMaySkipShow()
{
    if (!iCoopRunning() || !Guest())
    {
        return true;
    }
    U64 now = iCoopNowNs();
    // The show playing here: the host's (it said it started it, and that one
    // plays here), or this game's own.
    xBase* playing = globals.cmgr != NULL ? (xBase*)globals.cmgr : FlyRunning();
    bool hosts = playing != NULL && playing == sHostShow;
    // One this game started by itself a moment ago while the host watches
    // one too (a level's opening, which both start together): the host's
    // word on it may still be on its way, so for that long it counts as the
    // host's.
    bool soon = playing != NULL && playing == sLocalShow && now - sLocalShowAt < 1500 * kNsPerMs &&
                PartnerHereNow(now) && (sPartner.flags & PS_CUTSCENE);
    // Only the host's show is the host's to skip: one that only this game's
    // own trigger started is this player's (the host's skip would not end it).
    if (hosts || soon)
    {
        if (now - sSkipRefusedAt >= 1000 * kNsPerMs)
        {
            sSkipRefusedAt = now;
            printf("tssm: coop: a skip by this player was not taken: the host is watching too and skips for both\n");
            fflush(stdout);
        }
        return false;
    }
    if (playing != NULL && iCoopConnected() && now - sSkipRefusedAt >= 1000 * kNsPerMs)
    {
        sSkipRefusedAt = now;
        printf("tssm: coop: a skip by this player was taken: the show is this game's own\n");
        fflush(stdout);
    }
    return true;
}

bool iCoopNoSave()
{
    if (!iCoopNoAutoSave())
    {
        return false;
    }
    if (iCoopLocalPlayer2())
    {
        printf("tssm: coop: local: a save by hand was refused: Player 2's game does not save\n");
        fflush(stdout);
        iCoopAddNotice("Player 2's game does not save; Player 1's game saves for both");
        return true;
    }
    printf("tssm: coop: a save by hand was refused: in co-op only the host's game saves\n");
    fflush(stdout);
    iCoopAddNotice("in co-op only the host's game saves; your save was not written");
    return true;
}
