// PC-only: the counters a level's texts show and the texts its logic for them
// writes, in online co-op (iCoop.h, docs/MULTIPLAYER.md 13). Compiled with
// the game units.
//
// The combat arenas count their waves with counters ("{counter:NAME} left")
// that the host's enemies' births and deaths drive, and show "Wave 1
// Complete!" and the next wave's countdown from scripts those counters run.
// Both games make the same table of them from the level: every counter a
// loaded text shows, and every text box the links of those counters, or the
// scripts they run, write to.
//
// In the host's level the guest's game shows the host's: the host sends the
// counts that changed and what each box shows (COUNTERS), the guest's copies
// take them and their own event function drops this game's events to them
// (iCoopCounterHeld): the count and the counter's own Expired and Count1..20
// links are held, so its own copy of the challenge never ends a wave, ends,
// restarts, teleports or rewards by itself. Enable and Disable, and links on
// the incoming event itself (zEntEvent), still run; Build() logs those. A box
// takes the host's text when the host's changes, so the guest's own talks in
// it are left alone. This game's own counts and
// boxes come back when it leaves the level, when the host has been in
// another level for 3 s, and when the session is over.

#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopWorldInternal.h"

#include "xCounter.h"
#include "xEvent.h"
#include "xGroup.h"
#include "xString.h"
#include "xTextAsset.h"
#include "xstransvc.h"
#include "zBase.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zScript.h"
#include "zTextBox.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

using namespace iNet;

namespace
{
    const U64 kNsPerMs = 1000000ull;
    const U64 kNsPerSec = 1000000000ull;
    // Log lines of counter and text changes per level, at most.
    const S32 kLogMax = 600;
    const S32 kRefusedLogMax = 8;
    // Host: a change goes out with every state this long (a lost datagram is
    // made up by the next), and everything once a second anyway.
    const U64 kRepeatFor = 300 * kNsPerMs;
    const U64 kRefreshEvery = 1000 * kNsPerMs;
    // Guest: COUNTERS taken at most this many a second (the host sends one
    // with each state, about 33).
    const S32 kCountersPerSec = 60;
    // The host in another level this long: this level's own counts again
    // (as the shared progress, iCoopCombat.cpp).
    const U64 kUnshareAfter = 3000 * kNsPerMs;

    struct Cnt
    {
        _xCounter* c;
        U32 id;
        S16 start;
        S16 lo, hi; // what the level's own data can make of it
        S16 logged; // the value last logged
        bool byHost; // guest: the host's count was put in this frame
        // Host: the count last seen, and when it changed.
        S16 last;
        U64 changedAt;
        // Guest: the host's newest, and this game's own at the start.
        S16 hostVal;
        bool hostHave;
        S16 ownCount;
        U8 ownState;
    };

    struct Box
    {
        ztextbox* t;
        U32 id;
        U8 n;
        U32 text[kMaxBoxTexts]; // sorted
        const char* str[kMaxBoxTexts];
        U8 logged; // the state last logged: 0 not shown, k its text k
        bool byHost;
        U8 out; // host: the state last seen
        // Guest: the host's newest state and the one last put here (-1 none),
        // and this game's own at the start.
        S16 host, applied;
        bool touched;
        bool ownActive;
        U32 ownSize;
        const char* ownSeg[16];
    };

    std::vector<Cnt> sCnt; // sorted by asset ID: the same on both games
    std::vector<Box> sBox; // the same
    U32 sHash;
    zScene* sBuiltScene;
    U32 sBuiltId;
    bool sBuiltLogged;
    S32 sLogged;

    // Host.
    Counters sOut;
    U64 sSentAt;
    U64 sBoxChangedAt;
    bool sPartnerWasHere;

    // Guest.
    bool sMirror; // the host's counts and texts are shown here; this level's own counting held
    bool sHostHave;
    U64 sAwaySince;
    U64 sRateFrom;
    S32 sRateCount;
    U32 sRateDropped;
    S32 sRefusedLogged;
    S32 sHeldLogged;

    U64 ClockMs(U64 now)
    {
        return now / kNsPerMs;
    }

    bool Guest()
    {
        return iCoopLocalSlot() == 1;
    }

    bool LogOk()
    {
        return sLogged++ < kLogMax;
    }

    bool TextEvent(U32 ev)
    {
        return ev == eEventVisible || ev == eEventInvisible || ev == eEventSetText ||
               ev == eEventAddText || ev == eEventClearText;
    }

    S32 CntIndex(const xBase* b)
    {
        U32 id = b->id;
        size_t lo = 0, hi = sCnt.size();
        while (lo < hi)
        {
            size_t mid = (lo + hi) / 2;
            if (sCnt[mid].id < id)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        return lo < sCnt.size() && sCnt[lo].id == id && (const xBase*)sCnt[lo].c == b ? (S32)lo :
                                                                                        -1;
    }

    S32 BoxIndex(U32 id)
    {
        for (size_t i = 0; i < sBox.size(); i++)
        {
            if (sBox[i].id == id)
            {
                return (S32)i;
            }
        }
        return -1;
    }

    // Each event the level's data sends: every link of every object, and every
    // event of every script.
    template <typename F> void ForEachSent(zScene* s, F f)
    {
        for (U32 i = 0; i < s->num_base; i++)
        {
            xBase* b = s->base[i];
            if (b == NULL)
            {
                continue;
            }
            for (U32 k = 0; k < b->linkCount && b->link != NULL; k++)
            {
                f(b, b->link[k].dstAssetID, (U32)b->link[k].dstEvent, b->link[k].param,
                  b->link[k].paramWidgetAssetID);
            }
            if (b->baseType == eBaseTypeScript)
            {
                _zScript* sc = (_zScript*)b;
                if (sc->tasset == NULL)
                {
                    continue;
                }
                const xScriptEventAsset* ev = (const xScriptEventAsset*)(sc->tasset + 1);
                for (U32 k = 0; k < sc->tasset->eventCount && k < 4096; k++)
                {
                    f(b, ev[k].widget, ev[k].paramEvent, ev[k].param, ev[k].paramWidget);
                }
            }
        }
    }

    // A counter's name in "{counter:NAME}" (either case, as the text tags take it).
    void FindShown(const char* s, U32 len, std::vector<U32>* ids)
    {
        static const char kTag[] = "{counter:";
        const U32 tagLen = sizeof(kTag) - 1;
        for (U32 i = 0; i + tagLen < len; i++)
        {
            U32 k = 0;
            while (k < tagLen && tolower((unsigned char)s[i + k]) == kTag[k])
            {
                k++;
            }
            if (k < tagLen)
            {
                continue;
            }
            U32 from = i + tagLen, to = from;
            while (to < len && to - from < 64 && s[to] != '}' && s[to] != '\0')
            {
                to++;
            }
            if (to < len && s[to] == '}' && to > from)
            {
                U32 id = xStrHash(s + from, to - from);
                if (id != 0)
                {
                    ids->push_back(id);
                }
            }
            i = to;
        }
    }

    void Widen(Cnt& c, S32 v)
    {
        v = v < -(S32)kCounterLimit ? -(S32)kCounterLimit :
                                      (v > (S32)kCounterLimit ? (S32)kCounterLimit : v);
        c.lo = (S16)(v < c.lo ? v : c.lo);
        c.hi = (S16)(v > c.hi ? v : c.hi);
    }

    // What `ev` can make of counter `c`'s count (xCounterEventCB): a decrement
    // stops at 0 (it expires there), an increment has no top of its own.
    void RangeFrom(Cnt& c, U32 ev, const F32* param, U32 widget)
    {
        if (ev == eEventIncrement)
        {
            Widen(c, (S32)kCounterLimit);
        }
        else if (ev >= eEventCount1 && ev <= eEventCount20)
        {
            Widen(c, (S32)(ev - (eEventCount1 - 1)));
        }
        else if (ev == eEventSetCount)
        {
            if (widget != 0)
            {
                Widen(c, -(S32)kCounterLimit);
                Widen(c, (S32)kCounterLimit);
            }
            else if (param != NULL && param[0] == param[0])
            {
                Widen(c, param[0] < -(F32)kCounterLimit ?
                             -(S32)kCounterLimit :
                             (param[0] > (F32)kCounterLimit ? (S32)kCounterLimit : (S32)param[0]));
            }
        }
    }

    void Clear()
    {
        sCnt.clear();
        sBox.clear();
        sHash = 0;
        sLogged = 0;
        sBuiltLogged = false;
        sMirror = false;
        sHostHave = false;
        sAwaySince = 0;
        sSentAt = 0;
        sBoxChangedAt = 0;
        sRefusedLogged = 0;
        sHeldLogged = 0;
    }

    void Build()
    {
        Clear();
        zScene* s = globals.sceneCur;
        sBuiltScene = s;
        sBuiltId = s != NULL ? s->sceneID : 0;
        if (s == NULL || s->base == NULL)
        {
            return;
        }
        // The counters a loaded text shows.
        std::vector<U32> shown;
        S32 texts = xSTAssetCountByType('TEXT');
        for (S32 i = 0; i < texts; i++)
        {
            U32 size = 0;
            const xTextAsset* t = (const xTextAsset*)xSTFindAssetByType('TEXT', i, &size);
            if (t == NULL || size < sizeof(xTextAsset))
            {
                continue;
            }
            U32 len = t->len < size - sizeof(xTextAsset) ? t->len : size - (U32)sizeof(xTextAsset);
            FindShown(xTextAssetGetText(t), len, &shown);
        }
        std::sort(shown.begin(), shown.end());
        shown.erase(std::unique(shown.begin(), shown.end()), shown.end());
        for (size_t i = 0; i < shown.size(); i++)
        {
            xBase* b = zSceneFindObject(shown[i]);
            if (b == NULL || b->baseType != eBaseTypeCounter)
            {
                continue;
            }
            _xCounter* c = (_xCounter*)b;
            if (c->asset == NULL)
            {
                continue;
            }
            Cnt k;
            memset(&k, 0, sizeof(k));
            k.c = c;
            k.id = b->id;
            k.start = c->asset->count;
            k.lo = k.start < 0 ? k.start : 0;
            k.hi = k.start > 0 ? k.start : 0;
            k.logged = k.last = c->count;
            sCnt.push_back(k);
            if (sCnt.size() >= kMaxCounters)
            {
                break;
            }
        }
        if (sCnt.empty())
        {
            return;
        }
        // Held is the count and its own Expired and Count1..20 links; a link
        // on any other event fires on this game's events even so (zEntEvent).
        S32 unheld = 0;
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            const xBase* b = sCnt[i].c;
            for (U32 k = 0; k < b->linkCount && b->link != NULL; k++)
            {
                U32 ev = b->link[k].srcEvent;
                if (ev == eEventExpired || (ev >= eEventCount1 && ev <= eEventCount20) ||
                    unheld++ >= 16)
                {
                    continue;
                }
                printf("tssm: coop: hud: counter %08x's link on event %u to %08x is not held on a "
                       "guest\n",
                       (unsigned)b->id, (unsigned)ev, (unsigned)b->link[k].dstAssetID);
            }
        }
        if (unheld > 0)
        {
            fflush(stdout);
        }
        // Their ranges, from every event the level sends them (a group's
        // too, for the counters in it).
        ForEachSent(s, [](xBase*, U32 dst, U32 ev, const F32* param, U32 widget) {
            xBase* to = zSceneFindObject(dst);
            if (to == NULL)
            {
                return;
            }
            if (to->baseType == eBaseTypeCounter)
            {
                S32 i = CntIndex(to);
                if (i >= 0)
                {
                    RangeFrom(sCnt[i], ev, param, widget);
                }
            }
            else if (to->baseType == eBaseTypeGroup)
            {
                xGroup* g = (xGroup*)to;
                U32 n = xGroupGetCount(g);
                for (U32 k = 0; k < n && k < 256; k++)
                {
                    xBase* item = xGroupGetItemPtr(g, k);
                    S32 i =
                        item != NULL && item->baseType == eBaseTypeCounter ? CntIndex(item) : -1;
                    if (i >= 0)
                    {
                        RangeFrom(sCnt[i], ev, param, widget);
                    }
                }
            }
        });
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            Widen(sCnt[i], sCnt[i].c->count); // a level loaded from a save
        }
        // The text boxes their links write, and the scripts' those links run
        // (and those scripts run).
        std::vector<xBase*> scripts;
        std::vector<U32> boxes;
        auto visit = [&](U32 dst, U32 ev) {
            xBase* to = zSceneFindObject(dst);
            if (to == NULL)
            {
                return;
            }
            if (ev == eEventRun && to->baseType == eBaseTypeScript &&
                std::find(scripts.begin(), scripts.end(), to) == scripts.end())
            {
                scripts.push_back(to);
            }
            else if (TextEvent(ev) && to->baseType == eBaseTypeTextBox &&
                     std::find(boxes.begin(), boxes.end(), to->id) == boxes.end())
            {
                boxes.push_back(to->id);
            }
        };
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            xBase* b = sCnt[i].c;
            for (U32 k = 0; k < b->linkCount && b->link != NULL; k++)
            {
                visit(b->link[k].dstAssetID, b->link[k].dstEvent);
            }
        }
        for (size_t i = 0; i < scripts.size() && i < 64; i++)
        {
            _zScript* sc = (_zScript*)scripts[i];
            if (sc->tasset == NULL)
            {
                continue;
            }
            const xScriptEventAsset* ev = (const xScriptEventAsset*)(sc->tasset + 1);
            for (U32 k = 0; k < sc->tasset->eventCount && k < 4096; k++)
            {
                visit(ev[k].widget, ev[k].paramEvent);
            }
        }
        std::sort(boxes.begin(), boxes.end());
        for (size_t i = 0; i < boxes.size() && sBox.size() < kMaxTextBoxes; i++)
        {
            Box x;
            memset(&x, 0, sizeof(x));
            x.t = (ztextbox*)zSceneFindObject(boxes[i]);
            x.id = boxes[i];
            x.host = x.applied = -1;
            sBox.push_back(x);
        }
        // Each box's texts: every one the level's data puts in it, and its own.
        std::vector<std::vector<U32> > texts2(sBox.size());
        for (size_t i = 0; i < sBox.size(); i++)
        {
            if (sBox[i].t->asset != NULL && sBox[i].t->asset->text != 0)
            {
                texts2[i].push_back(sBox[i].t->asset->text);
            }
        }
        ForEachSent(s, [&](xBase*, U32 dst, U32 ev, const F32* param, U32) {
            S32 i = (ev == eEventVisible || ev == eEventSetText || ev == eEventAddText) &&
                            param != NULL ?
                        BoxIndex(dst) :
                        -1;
            if (i < 0)
            {
                return;
            }
            U32 id; // the text's asset ID, as the box takes it (cb_dispatch)
            memcpy(&id, &param[0], sizeof(id));
            if (id != 0)
            {
                texts2[i].push_back(id);
            }
        });
        for (size_t i = 0; i < sBox.size(); i++)
        {
            std::vector<U32>& v = texts2[i];
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
            for (size_t k = 0; k < v.size() && sBox[i].n < kMaxBoxTexts; k++)
            {
                const char* str = xTextFindString(v[k], NULL);
                if (str != NULL)
                {
                    sBox[i].text[sBox[i].n] = v[k];
                    sBox[i].str[sBox[i].n] = str;
                    sBox[i].n++;
                }
            }
        }
        U32 h = 2166136261u;
        auto mix = [&h](U32 v) {
            for (int k = 0; k < 4; k++)
            {
                h = (h ^ (U8)(v >> (8 * k))) * 16777619u;
            }
        };
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            mix(sCnt[i].id);
            mix((U32)(U16)sCnt[i].start | (U32)(U16)sCnt[i].lo << 16);
            mix((U32)(U16)sCnt[i].hi);
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            mix(sBox[i].id);
            for (U8 k = 0; k < sBox[i].n; k++)
            {
                mix(sBox[i].text[k]);
            }
        }
        sHash = h;
    }

    void LogTable()
    {
        if (sBuiltLogged || sCnt.empty())
        {
            return;
        }
        sBuiltLogged = true;
        printf(
            "tssm: coop: hud: the level's texts show %d counters; their logic writes %d text boxes (table "
            "%08x)\n",
            (int)sCnt.size(), (int)sBox.size(), (unsigned)sHash);
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            printf("tssm: coop: hud: counter %d is %08x, from %d, %d..%d\n", (int)i,
                   (unsigned)sCnt[i].id, (int)sCnt[i].start, (int)sCnt[i].lo, (int)sCnt[i].hi);
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            printf("tssm: coop: hud: text box %d is %08x, %d texts\n", (int)i, (unsigned)sBox[i].id,
                   (int)sBox[i].n);
        }
        fflush(stdout);
    }

    // A box's state: 0 not shown (or showing a text not in its list), k its text k.
    U8 BoxState(const Box& b)
    {
        const ztextbox* t = b.t;
        if (!t->flag.active || t->segments_size == 0)
        {
            return 0;
        }
        for (U8 k = 0; k < b.n; k++)
        {
            if (t->segments[0] == b.str[k])
            {
                return (U8)(k + 1);
            }
        }
        return 0;
    }

    // Each change of a counter or box, as this game has it (tests compare the
    // two games' lines by the clock both copies on one PC share); on the
    // guest, whether the host's put it there.
    void LogChanges(U64 now, bool guest)
    {
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            Cnt& c = sCnt[i];
            const char* by = !guest ? "" : (c.byHost ? " (the host's)" : " (this game's own)");
            c.byHost = false;
            if (c.c->count != c.logged)
            {
                c.logged = c.c->count;
                if (LogOk())
                {
                    printf("tssm: coop: hud: counter %d is %d%s (clock ms %llu)\n", (int)i,
                           (int)c.logged, by, (unsigned long long)ClockMs(now));
                }
            }
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            Box& b = sBox[i];
            const char* by = !guest ? "" : (b.byHost ? " (the host's)" : " (this game's own)");
            b.byHost = false;
            U8 st = BoxState(b);
            if (st != b.logged)
            {
                b.logged = st;
                if (!LogOk())
                {
                    continue;
                }
                if (st == 0)
                {
                    printf("tssm: coop: hud: text box %d is hidden%s (clock ms %llu)\n", (int)i, by,
                           (unsigned long long)ClockMs(now));
                }
                else
                {
                    printf("tssm: coop: hud: text box %d shows text %08x%s (clock ms %llu)\n",
                           (int)i, (unsigned)b.text[st - 1], by, (unsigned long long)ClockMs(now));
                }
            }
        }
        fflush(stdout);
    }

    // --- the host --------------------------------------------------------------------------

    void HostFrame(U64 now)
    {
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            Cnt& c = sCnt[i];
            if (c.c->count != c.last)
            {
                c.last = c.c->count;
                c.changedAt = now;
            }
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            U8 st = BoxState(sBox[i]);
            if (st != sBox[i].out)
            {
                sBox[i].out = st;
                sBoxChangedAt = now;
            }
        }
    }

    // --- the guest -------------------------------------------------------------------------

    void Refused(const char* why)
    {
        if (sRefusedLogged++ < kRefusedLogMax)
        {
            printf("tssm: coop: hud: the host's COUNTERS refused: %s\n", why);
            fflush(stdout);
        }
    }

    // The host's message against this game's own table and level data.
    bool Valid(const Counters& in)
    {
        if (in.table != sHash || in.counters != sCnt.size() || in.boxes != sBox.size())
        {
            Refused("not this level's table");
            return false;
        }
        for (U8 i = 0; i < in.count; i++)
        {
            const Cnt& c = sCnt[in.e[i].index]; // below the table's size (iNetProto.cpp)
            if (in.e[i].value < c.lo || in.e[i].value > c.hi)
            {
                Refused("a count this level's data cannot make");
                return false;
            }
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            if (in.text[i] > sBox[i].n)
            {
                Refused("a text this box is never given");
                return false;
            }
        }
        return true;
    }

    bool RateOk(U64 now)
    {
        if (sRateFrom == 0 || now - sRateFrom >= kNsPerSec)
        {
            sRateFrom = now;
            sRateCount = 0;
        }
        if (sRateCount++ < kCountersPerSec)
        {
            return true;
        }
        if (sRateDropped++ % 100 == 0)
        {
            printf("tssm: coop: hud: COUNTERS past %d a second; %u ignored\n", (int)kCountersPerSec,
                   (unsigned)sRateDropped);
            fflush(stdout);
        }
        return false;
    }

    void StartMirror(U64 now)
    {
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            sCnt[i].ownCount = sCnt[i].c->count;
            sCnt[i].ownState = sCnt[i].c->state;
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            Box& b = sBox[i];
            b.ownActive = b.t->flag.active;
            b.ownSize = b.t->segments_size < 16 ? b.t->segments_size : 16;
            memcpy(b.ownSeg, b.t->segments, sizeof(b.ownSeg));
            b.touched = false;
            b.applied = -1;
        }
        sMirror = true;
        printf(
            "tssm: coop: hud: the host's counters and challenge texts are shown here; this level's own "
            "counting is held (clock ms %llu)\n",
            (unsigned long long)ClockMs(now));
        fflush(stdout);
    }

    // This game's own counts back, and its own boxes when `boxes` (not while
    // the level ends: its boxes are reset with it).
    void StopMirror(const char* why, bool boxes)
    {
        if (!sMirror)
        {
            return;
        }
        sMirror = false;
        sHostHave = false;
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            Cnt& c = sCnt[i];
            c.c->count = c.ownCount;
            c.c->state = c.ownState;
            c.hostHave = false;
        }
        for (size_t i = 0; i < sBox.size() && boxes; i++)
        {
            Box& b = sBox[i];
            if (b.touched && b.ownActive)
            {
                memcpy(b.t->segments, b.ownSeg, sizeof(b.ownSeg));
                b.t->segments_size = b.ownSize;
                b.t->flag.dirty = true;
                b.t->activate(false);
            }
            else if (b.touched && BoxState(b) != 0)
            {
                b.t->deactivate();
            }
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            sBox[i].host = sBox[i].applied = -1;
            sBox[i].touched = false;
        }
        printf("tssm: coop: hud: this level's own counts and texts are back (%s)\n", why);
        fflush(stdout);
    }

    void Apply()
    {
        for (size_t i = 0; i < sCnt.size(); i++)
        {
            Cnt& c = sCnt[i];
            if (c.hostHave && c.c->count != c.hostVal)
            {
                c.c->count =
                    c.hostVal; // every frame: this game's own reset of the level puts its own back
                c.byHost = true;
            }
        }
        for (size_t i = 0; i < sBox.size(); i++)
        {
            Box& b = sBox[i];
            if (b.host < 0 || b.host == b.applied)
            {
                continue;
            }
            if (b.host > 0)
            {
                b.t->set_text(b.text[b.host - 1]);
                b.t->activate(false);
            }
            else if (BoxState(b) != 0)
            {
                b.t->deactivate(); // only one of its list: this game's own talk in it stays
            }
            b.applied = b.host;
            b.touched = true;
            b.byHost = true;
        }
    }

    void GuestFrame(U64 now, const PlayerState* partner)
    {
        Counters in;
        while (iCoopTakeCounters(&in))
        {
            if (!RateOk(now) || in.scene != sBuiltId || !Valid(in))
            {
                continue; // another level's is dropped as the levels change
            }
            for (U8 i = 0; i < in.count; i++)
            {
                Cnt& c = sCnt[in.e[i].index];
                c.hostVal = in.e[i].value;
                c.hostHave = true;
            }
            for (size_t i = 0; i < sBox.size(); i++)
            {
                sBox[i].host = in.text[i];
            }
            sHostHave = true;
        }
        bool sameLevel = partner != NULL && partner->scene == sBuiltId;
        if (sMirror && !sameLevel)
        {
            if (sAwaySince == 0)
            {
                sAwaySince = now;
            }
            else if (now - sAwaySince >= kUnshareAfter)
            {
                StopMirror("the host is in another level", true);
            }
        }
        else
        {
            sAwaySince = 0;
        }
        if (sameLevel && sHostHave && iCoopWorldGuestSynced())
        {
            if (!sMirror)
            {
                StartMirror(now);
            }
            Apply();
        }
    }
} // namespace

// --- iCoopWorldInternal.h ---------------------------------------------------------------------

void iCoopHudFrame(U64 now, const PlayerState* partner)
{
    zScene* s = globals.sceneCur;
    if (s != sBuiltScene || (s != NULL && s->sceneID != sBuiltId))
    {
        Build();
    }
    if (sCnt.empty())
    {
        return;
    }
    LogTable();
    bool guest = Guest();
    if (guest)
    {
        GuestFrame(now, partner);
    }
    else
    {
        HostFrame(now);
    }
    LogChanges(now, guest);
}

const Counters* iCoopHudOutgoing(U64 now, bool partnerHere, size_t* reserve)
{
    bool here = partnerHere && !Guest() && !sCnt.empty() && globals.sceneCur == sBuiltScene;
    if (here && !sPartnerWasHere)
    {
        sSentAt = 0; // everything at once for a guest that comes
    }
    sPartnerWasHere = here;
    if (!here)
    {
        return NULL;
    }
    bool refresh = sSentAt == 0 || now - sSentAt >= kRefreshEvery;
    Counters& o = sOut;
    memset(&o, 0, sizeof(o));
    o.scene = sBuiltId;
    o.table = sHash;
    o.counters = (U8)sCnt.size();
    o.boxes = (U8)sBox.size();
    for (size_t i = 0; i < sCnt.size(); i++)
    {
        if (refresh || (sCnt[i].changedAt != 0 && now - sCnt[i].changedAt < kRepeatFor))
        {
            o.e[o.count].index = (U8)i;
            o.e[o.count].value = sCnt[i].last;
            o.count++;
        }
    }
    for (size_t i = 0; i < sBox.size(); i++)
    {
        o.text[i] = sBox[i].out;
    }
    if (!refresh && o.count == 0 && (sBoxChangedAt == 0 || now - sBoxChangedAt >= kRepeatFor))
    {
        return NULL;
    }
    if (!counters_valid(o))
    {
        return NULL; // a count past the wire's limit: never sent
    }
    if (refresh)
    {
        sSentAt = now;
    }
    *reserve += counters_msg_size(o.count, o.boxes);
    return &o;
}

void iCoopHudReset()
{
    if (!(iCoopRunning() && (iCoopConnected() || iCoopJoiningAgain())))
    {
        StopMirror("the session is over", true);
    }
    sSentAt = 0;
    sPartnerWasHere = false;
    sRateFrom = 0;
    sRateCount = 0;
}

void iCoopHudSceneExit()
{
    // Before the level's own save into memory (zSceneSave): its counters
    // as this game's own level had them.
    StopMirror("leaving the level", false);
    Clear();
    sBuiltScene = NULL;
    sBuiltId = 0;
}

// --- iCoop.h ------------------------------------------------------------------------------------

bool iCoopCounterHeld(xBase* b, U32 event)
{
    if (!sMirror || !iCoopRunning())
    {
        return false;
    }
    S32 i = CntIndex(b);
    if (i < 0)
    {
        return false;
    }
    if (sHeldLogged++ < 16)
    {
        printf(
            "tssm: coop: hud: this game's own event %u to counter %d held: the host's count is shown\n",
            (unsigned)event, (int)i);
        fflush(stdout);
    }
    return true;
}
