// The Cheats tab's game side; see iCheats.h. Port-only, compiled with the game
// units because it reads and writes the game's state.

#include "iCheats.h"

#include "iConfig.h"
#include "iTime.h"
#include "xCounter.h"
#include "xGroup.h"
#include "xPad.h"
#include "xString.h"
#include "zEntPlayer.h"
#include "zGameExtras.h"
#include "zGlobals.h"
#include "zMenuSB.h"
#include "zScene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // Frames of zGameLoop so far, and how the tab tells a level is running: a
    // frame of the tab with no frame of zGameLoop since the one before is a
    // miss, and three in a row are the title screen or a movie. So is a last
    // frame of zGameLoop long ago: the tab opened again at the title screen.
    U32 sGameFrames;
    U32 sFramesSeen;
    S32 sMisses = 3;
    iTime sGameFrameAt;

    // The developers' switches the port's cheats hold, while this file has
    // changed them: the original's values go back when a cheat is turned off,
    // and nothing is written while none was ever on.
    bool sNoDamageHeld;
    bool sFlyHeld;

    bool sTableChecked;

    // --- the tables against the game's ----------------------------------------------

    // cheatList's row as iCheatCode spells it: the last eight presses.
    void CodeText(const GECheat* rec, char* out)
    {
        for (S32 i = 0; i < 8; i++)
        {
            const U32 button = rec->key_code[8 + i];
            out[i] = button == XPAD_BUTTON_O ? 'X' : button == XPAD_BUTTON_SQUARE ? 'Y' : '?';
        }
        out[8] = '\0';
    }

    void CheckTable()
    {
        S32 rows = 0;
        while (zGameExtras_PCCheat(rows) != NULL)
        {
            rows++;
        }

        S32 wrong = rows == iCheatCodeCount() ? 0 : 1;
        for (S32 i = 0; i < rows && i < iCheatCodeCount(); i++)
        {
            const GECheat* rec = zGameExtras_PCCheat(i);
            const iCheatCode* code = iCheatCodeAt(i);
            char text[9];
            CodeText(rec, text);
            if (strcmp(text, code->code) != 0 || (U32)rec->flg_keep != code->flag ||
                ((rec->flg_mode & 1) != 0) != code->toggles)
            {
                printf("tssm: cheats: code %d (%s) is %s, bit 0x%X, mode %d in the game\n", (int)i,
                       code->id, text, (unsigned)rec->flg_keep, (int)rec->flg_mode);
                wrong++;
            }
        }

        if (wrong == 0)
        {
            printf("tssm: cheats: the tab's %d button codes are the game's\n", (int)rows);
        }
        else
        {
            printf("tssm: cheats: the tab's button codes are NOT the game's (%d in the game)\n",
                   (int)rows);
        }
        fflush(stdout);
    }

    // --- the game's own cheats ----------------------------------------------------------

    // The Extras screen opens a row once the treasure chests reach its count
    // (zUICustomExtrasManager::SelectCurrentIcon); the tab does too, unless the
    // port's unlock_extras says otherwise.
    bool ExtraOpen(S32 chestsNeeded)
    {
        return (U32)chestsNeeded <= globals.player.InvTreasureChestTotal ||
               iCheatsInForce(iCHEAT_UNLOCK_EXTRAS);
    }

    void TakeRequest(const iCheatRequest& request)
    {
        switch (request.kind)
        {
        case iCHEAT_REQUEST_CODE:
        {
            const iCheatCode* code = iCheatCodeAt(request.value);
            if (code == NULL || zGameExtras_PCCheat(request.value) == NULL)
            {
                break;
            }
            zGameExtras_PCEnterCheat(request.value, true);
            printf("tssm: cheats: code %s entered from the tab, cheat word 0x%08X\n", code->id,
                   (unsigned)zGameExtras_CheatFlags());
            break;
        }
        case iCHEAT_REQUEST_EXTRA:
        {
            S32 type, id, chests, status;
            if (!zMenuSB_PCExtra(request.value, &type, &id, &chests, &status) ||
                (type != 4 && type != 5))
            {
                break;
            }
            const iCheatExtra* extra = iCheatExtraFind(type, id);
            const char* word = extra != NULL ? extra->word : "?";
            // Locked, it can still be switched off (it was switched on while
            // unlock_extras was on).
            if (!ExtraOpen(chests) && status == 0)
            {
                printf("tssm: cheats: extra %s stays locked (%d treasure chests needed, %u "
                       "held)\n",
                       word, (int)chests, (unsigned)globals.player.InvTreasureChestTotal);
                break;
            }
            zMenuSB_PCToggleExtra(request.value);
            zMenuSB_PCExtra(request.value, &type, &id, &chests, &status);
            printf("tssm: cheats: extra %s switched %s from the tab\n", word,
                   status != 0 ? "on" : "off");
            break;
        }
        case iCHEAT_REQUEST_REFILL:
            if (globals.player.s != NULL && globals.player.s->Health != 0 &&
                globals.player.s->Health < globals.player.s->MaxHealth)
            {
                globals.player.s->Health = globals.player.s->MaxHealth;
                printf("tssm: cheats: health refilled to %u\n",
                       (unsigned)globals.player.s->Health);
            }
            break;
        }
        fflush(stdout);
    }

    // --- a host's state -----------------------------------------------------------------

    // Holds the game to the host's: a code the host has on and this game does
    // not is entered here too (so its callback runs), then the cheat word, the
    // Extras rows and the points multiplier are the host's.
    void FollowHost(const iCheatsShared& host)
    {
        const U32 mine = (U32)zGameExtras_CheatFlags();
        if (mine != host.gameFlags)
        {
            for (S32 i = 0; i < iCheatCodeCount(); i++)
            {
                const U32 flag = iCheatCodeAt(i)->flag;
                if ((host.gameFlags & flag) != 0 && (mine & flag) == 0 &&
                    zGameExtras_PCCheat(i) != NULL)
                {
                    zGameExtras_PCEnterCheat(i, false);
                }
            }
            zGameExtras_PCSetCheatFlags((S32)host.gameFlags);
            printf("tssm: cheats: cheat word 0x%08X taken from the host (was 0x%08X)\n",
                   (unsigned)host.gameFlags, (unsigned)mine);
            fflush(stdout);
        }

        const S32 rows = zMenuSB_PCExtraCount();
        for (S32 row = 0; row < rows && row < ICHEATS_SHARED_EXTRAS; row++)
        {
            S32 type, id, chests, status;
            zMenuSB_PCExtra(row, &type, &id, &chests, &status);
            const S32 want = (host.extrasOn[row / 32] >> (row % 32)) & 1;
            if ((type == 4 || type == 5) && (status != 0) != (want != 0))
            {
                zMenuSB_PCSetExtraStatus(row, want);
            }
        }

        if (host.pointsMultiplier >= 1 && host.pointsMultiplier <= 2 &&
            globals.player.InvManlinessPointsMultiplier != host.pointsMultiplier)
        {
            globals.player.InvManlinessPointsMultiplier = host.pointsMultiplier;
        }
    }

    // --- the port's cheats -----------------------------------------------------------------

    void HoldPortCheats()
    {
        // zMainReadINI sets TakeDamage to 1 and CheatFlyToggle to 0 and nothing
        // in the game changes either.
        if (iCheatsInForce(iCHEAT_NO_DAMAGE))
        {
            if (!sNoDamageHeld)
            {
                printf("tssm: cheats: no damage on\n");
                fflush(stdout);
            }
            globals.player.g.TakeDamage = 0;
            sNoDamageHeld = true;
        }
        else if (sNoDamageHeld)
        {
            globals.player.g.TakeDamage = 1;
            sNoDamageHeld = false;
            printf("tssm: cheats: no damage off\n");
            fflush(stdout);
        }

        // Turned off in mid-air, the buttons keep working until the player has
        // used them to land: zEntPlayer_Update's own way out of flight.
        if (iCheatsInForce(iCHEAT_FLY) || (sFlyHeld && globals.player.cheat_mode != 0))
        {
            if (!sFlyHeld)
            {
                printf("tssm: cheats: flight on\n");
                fflush(stdout);
            }
            globals.player.g.CheatFlyToggle = 1;
            sFlyHeld = true;
        }
        else if (sFlyHeld)
        {
            globals.player.g.CheatFlyToggle = 0;
            sFlyHeld = false;
            printf("tssm: cheats: flight off\n");
            fflush(stdout);
        }
    }
} // namespace

void iCheatsGameFrame()
{
    sGameFrames++;
    sGameFrameAt = iTimeGet();

    if (!sTableChecked)
    {
        sTableChecked = true;
        CheckTable();
    }

    iCheatRequest request;
    while (iCheatsNextRequest(&request))
    {
        TakeRequest(request);
    }

    const iCheatsShared* host = iCheatsSharedFollowed();
    if (host != NULL)
    {
        FollowHost(*host);
    }

    HoldPortCheats();
}

void iCheatsSharedGet(iCheatsShared* out)
{
    memset(out, 0, sizeof(*out));
    out->version = ICHEATS_SHARED_VERSION;
    for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
    {
        if (iCheatsInForce((iCheatPort)i))
        {
            out->port |= 1u << i;
        }
    }
    out->gameFlags = (U32)zGameExtras_CheatFlags();
    const S32 rows = zMenuSB_PCExtraCount();
    for (S32 row = 0; row < rows && row < ICHEATS_SHARED_EXTRAS; row++)
    {
        S32 type, id, chests, status;
        zMenuSB_PCExtra(row, &type, &id, &chests, &status);
        if ((type == 4 || type == 5) && status != 0)
        {
            out->extrasOn[row / 32] |= 1u << (row % 32);
        }
    }
    out->pointsMultiplier = globals.player.InvManlinessPointsMultiplier;
}

void iCheatsGetStatus(iCheatsStatus* out)
{
    if (sGameFrames != sFramesSeen)
    {
        sFramesSeen = sGameFrames;
        sMisses = 0;
    }
    else if (sMisses < 3)
    {
        sMisses++;
    }

    memset(out, 0, sizeof(*out));
    out->inGame = sMisses < 3 && iTimeDiffSec(sGameFrameAt, iTimeGet()) < 1.0f;
    if (!out->inGame)
    {
        // Nothing asked for in one game is left for the next.
        iCheatRequest stale;
        while (iCheatsNextRequest(&stale))
        {
        }
    }
    out->gameFlags = (U32)zGameExtras_CheatFlags();
    out->pointsMultiplier = globals.player.InvManlinessPointsMultiplier;
    out->chests = globals.player.InvTreasureChestTotal;
    out->flying = globals.player.cheat_mode != 0;
    if (globals.player.s != NULL)
    {
        out->health = globals.player.s->Health;
        out->maxHealth = globals.player.s->MaxHealth;
    }
}

S32 iCheatsExtraRows()
{
    return zMenuSB_PCExtraCount();
}

bool iCheatsExtraRow(S32 row, S32* type, S32* id, S32* chestsNeeded, bool* on)
{
    S32 status = 0;
    if (!zMenuSB_PCExtra(row, type, id, chestsNeeded, &status))
    {
        return false;
    }
    *on = status != 0;
    return true;
}

// --- TSSM_TEST_CHEAT, TSSM_TEST_CHEATLOG --------------------------------------------------

namespace
{
    struct TestEvent
    {
        S32 frame;
        char what[96];
    };

    const S32 kMaxTestEvents = 48;
    TestEvent sTestEvents[kMaxTestEvents];
    S32 sTestEventCount;
    S32 sTestLogEvery;
    bool sTestParsed;

    void ParseTest()
    {
        const char* log = getenv("TSSM_TEST_CHEATLOG");
        if (log != NULL && atoi(log) > 0)
        {
            sTestLogEvery = atoi(log);
        }

        const char* list = getenv("TSSM_TEST_CHEAT");
        if (list == NULL || list[0] == '\0')
        {
            return;
        }

        static char buf[4096];
        snprintf(buf, sizeof(buf), "%s", list);
        for (char* entry = strtok(buf, ","); entry != NULL; entry = strtok(NULL, ","))
        {
            char* end;
            long frame = strtol(entry, &end, 10);
            if (end == entry || frame < 1 || *end != ':' || sTestEventCount >= kMaxTestEvents)
            {
                printf("tssm: test: TSSM_TEST_CHEAT: '%s' is not frame:what\n", entry);
                continue;
            }
            sTestEvents[sTestEventCount].frame = (S32)frame;
            snprintf(sTestEvents[sTestEventCount].what, sizeof(sTestEvents[0].what), "%s",
                     end + 1);
            sTestEventCount++;
        }
        fflush(stdout);
    }

    // The row of the Extras list holding this type and id, or -1.
    S32 ExtraRow(S32 type, S32 id)
    {
        const S32 rows = zMenuSB_PCExtraCount();
        for (S32 row = 0; row < rows; row++)
        {
            S32 t, i, chests, status;
            zMenuSB_PCExtra(row, &t, &i, &chests, &status);
            if (t == type && i == id)
            {
                return row;
            }
        }
        return -1;
    }

    void RunTest(S32 frame, const char* what)
    {
        bool ok = false;
        if (strncmp(what, "code:", 5) == 0)
        {
            const S32 index = iCheatCodeFind(what + 5);
            ok = index >= 0 && iCheatsRequest(iCHEAT_REQUEST_CODE, index);
        }
        else if (strncmp(what, "extra:", 6) == 0)
        {
            const iCheatExtra* extra = iCheatExtraFindWord(what + 6);
            const S32 row = extra != NULL ? ExtraRow(extra->type, extra->id) : -1;
            ok = row >= 0 && iCheatsRequest(iCHEAT_REQUEST_EXTRA, row);
        }
        else if (strcmp(what, "refill") == 0)
        {
            ok = iCheatsRequest(iCHEAT_REQUEST_REFILL, 0);
        }
        else if (strncmp(what, "port:", 5) == 0)
        {
            // port:<word>:on|off, or port:alloff. Set as the tab sets them, less
            // the save.
            char word[48];
            snprintf(word, sizeof(word), "%s", what + 5);
            char* colon = strchr(word, ':');
            if (strcmp(word, "alloff") == 0)
            {
                iCheatsAllPortOff();
                ok = true;
            }
            else if (colon != NULL)
            {
                *colon = '\0';
                const S32 cheat = iCheatPortFind(word);
                if (cheat >= 0 && (strcmp(colon + 1, "on") == 0 || strcmp(colon + 1, "off") == 0))
                {
                    iConfigSet(iCheatPortAt((iCheatPort)cheat)->key, colon + 1);
                    iCheatsApply();
                    ok = true;
                }
            }
        }
        else if (strncmp(what, "hit:", 4) == 0)
        {
            // A hit of that many health units, as an enemy's.
            const U32 before = globals.player.s != NULL ? globals.player.s->Health : 0;
            const S32 result = zEntPlayer_Damage(NULL, (U32)atoi(what + 4));
            printf("tssm: test: frame %d: hit %d: zEntPlayer_Damage returned %d, health %u to "
                   "%u\n",
                   (int)frame, atoi(what + 4), (int)result, (unsigned)before,
                   (unsigned)(globals.player.s != NULL ? globals.player.s->Health : 0));
            ok = true;
        }
        else if (strncmp(what, "host:", 5) == 0)
        {
            // host:<port>:<cheat word>:<extras low>:<extras high>:<multiplier>,
            // all hexadecimal: a host's state arriving, through the wire form.
            iCheatsShared in;
            memset(&in, 0, sizeof(in));
            in.version = ICHEATS_SHARED_VERSION;
            unsigned port, flags, lo, hi, mult;
            if (sscanf(what + 5, "%x:%x:%x:%x:%x", &port, &flags, &lo, &hi, &mult) == 5)
            {
                in.port = port;
                in.gameFlags = flags;
                in.extrasOn[0] = lo;
                in.extrasOn[1] = hi;
                in.pointsMultiplier = mult;
                U8 wire[ICHEATS_SHARED_BYTES];
                iCheatsShared out;
                iCheatsSharedPack(&in, wire);
                if (iCheatsSharedUnpack(wire, sizeof(wire), &out))
                {
                    iCheatsSharedSet(&out);
                    ok = true;
                }
            }
        }
        else if (strcmp(what, "release") == 0)
        {
            iCheatsSharedRelease();
            ok = true;
        }

        printf("tssm: test: frame %d: cheat %s%s\n", (int)frame, what,
               ok ? "" : ": not understood, or refused");
        fflush(stdout);
    }

    void LogTest(S32 frame)
    {
        // The task counters, as zMenuSB_SaveMenuCounters walks them: how many
        // are "not given" (1), "given" (2) and "completed" (3).
        S32 tasks[4] = { 0, 0, 0, 0 };
        if (globals.sceneCur != NULL)
        {
            xGroup* group = (xGroup*)zSceneFindObject(xStrHash("TASK COUNTER GROUP"));
            const U32 n = group != NULL ? xGroupGetCount(group) : 0;
            for (U32 i = 0; i < n; i++)
            {
                const _xCounter* counter = (const _xCounter*)xGroupGetItemPtr(group, i);
                if (counter != NULL && counter->count >= 0 && counter->count <= 3)
                {
                    tasks[counter->count]++;
                }
            }
        }

        iCheatsShared now;
        iCheatsSharedGet(&now);
        const zPlayerGlobals& p = globals.player;
        printf("tssm: test: frame %d: cheats word 0x%08X multiplier %u health %u/%u max sb %u "
               "pat %u powerups %u,%u,%u,%u,%u,%u,%u,%u,%u,%u takedamage %u flytoggle %u "
               "flying %d extras 0x%08X%08X chests %u tasks %d/%d/%d port 0x%X following %d "
               "y %.2f\n",
               (int)frame, (unsigned)now.gameFlags, (unsigned)now.pointsMultiplier,
               (unsigned)(p.s != NULL ? p.s->Health : 0),
               (unsigned)(p.s != NULL ? p.s->MaxHealth : 0), (unsigned)p.sb.MaxHealth,
               (unsigned)p.patrick.MaxHealth, p.g.PowerUp[0], p.g.PowerUp[1], p.g.PowerUp[2],
               p.g.PowerUp[3], p.g.PowerUp[4], p.g.PowerUp[5], p.g.PowerUp[6], p.g.PowerUp[7],
               p.g.PowerUp[8], p.g.PowerUp[9], (unsigned)p.g.TakeDamage,
               (unsigned)p.g.CheatFlyToggle, (int)p.cheat_mode, (unsigned)now.extrasOn[1],
               (unsigned)now.extrasOn[0], (unsigned)p.InvTreasureChestTotal, (int)tasks[1],
               (int)tasks[2], (int)tasks[3], (unsigned)now.port, iCheatsFollowing() ? 1 : 0,
               xglobals->___player_ent_dont_use_directly != NULL &&
                       xglobals->___player_ent_dont_use_directly->frame != NULL ?
                   xglobals->___player_ent_dont_use_directly->frame->mat.pos.y :
                   0.0f);
        fflush(stdout);
    }
} // namespace

void iCheatsTestFrame(S32 frame)
{
    if (!sTestParsed)
    {
        sTestParsed = true;
        ParseTest();
    }

    for (S32 i = 0; i < sTestEventCount; i++)
    {
        if (sTestEvents[i].frame == frame)
        {
            RunTest(frame, sTestEvents[i].what);
        }
    }

    if (sTestLogEvery > 0 && frame % sTestLogEvery == 0)
    {
        LogTest(frame);
    }
}
