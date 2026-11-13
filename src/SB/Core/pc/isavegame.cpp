// PC isavegame: treedome's pc/isavegame.cpp with TSSM's interface and file
// names; see isavegame.h.
#include "isavegame.h"
#include "iHost.h"
#include "iFile.h"
#include "iSaveFormat.h"
#include "iSaveStore.h"
#include "iSaveThumb.h"
#include "xsavegame.h"

#include <stddef.h>
#include <vector>

#include <types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Saves, on a host filesystem.
//
// The GameCube version of this file is 2048 lines, and most of that is memory
// card: mounting and unmounting, probing sector sizes, formatting, repairing a
// broken card, and building the 8 KB banner-and-icon blob that CARD requires be
// written at the head of every file so the console's own file manager can show
// it. None of that exists here.
//
// What does carry over is the shape the game was written against, because
// xsavegame.cpp is shared and asks all the same questions: how many targets are
// there, is this one formatted, is there room, what is on it. Each gets the
// answer that is true of a directory.
//
// The file names are retail's, so a save is named the same thing on both.
//
// And so are the contents. The game hands this layer its save in the host's
// byte order (xsavegame.cpp writes it with memcpy); this layer writes it to disk
// in the console's, and turns it back on the way in (iSaveFormat.h). A save
// file here is the payload of a GameCube save, so the two can be moved across
// with nothing but the card's wrapper put on or taken off (iSaveStore.h,
// docs/SAVES.md).

// Retail's, from iSG_cubeicon_size: every card file carries a banner and icon
// ahead of the payload, and iSGFileSize subtracts it back off. A host file is
// its payload, so the two cancel and neither appears below.

static S32 g_isginit;

// The directory holding the save targets. Chosen once, at iSGStartup, by
// iSaveStoreRoot -- the save store and the game have to agree on it.
static char g_saveroot[512];

// One target per memory card slot the console had, and the port exposes both.
//
// Reporting a single target did NOT remove the second one from the interface,
// which is what it was supposed to do: the save and load screens are UI assets
// with two buttons baked into them -- MNU3 LD MC1 and MC2, SV MC1 and MC2 --
// and the chooser is drawn whatever this number says. All reporting one did was
// make the second button dead, so choosing it fell into the single-target arm of
// zSaveLoad_CardCheckFormatted, found the physical slot did not match, and came
// back as an error.
//
// Two makes it a folder like the first, with its own three game slots. That is
// six saves rather than three, and it costs no screen the player was not
// already being shown.
//
// It is also what iSGTgtCount always reports. With the PC menus on, the PC save
// screen reaches further folders by index (iSGTargetMax); see isavegame.h.
#define ISG_HOST_TARGETS 2

// A target index this build accepts: retail's two, or every folder the PC save
// screen can reach.
static S32 sPCTargets;

static S32 iSG_targets()
{
    return sPCTargets ? ISG_MAX_TARGETS : ISG_HOST_TARGETS;
}

void iSGSetPCTargets(S32 on)
{
    sPCTargets = on;
}

static st_ISGSESSION g_isgdata_MAIN;

static bool iSG_mkdir_p(const char* path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char* p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            iHostMakeDir(tmp);
            *p = '/';
        }
    }

    return iHostMakeDir(tmp);
}

// config.ini's game.save_folder, pushed by iSystem.
void iSGSetSaveRoot(const char* dir)
{
    iSaveStoreSetConfigRoot(dir);
}

// TSSM_SAVE_DIR wins so a player can point saves anywhere; otherwise the
// setting, and otherwise wherever this host keeps per-user data, which is where
// its users expect to find it. With a mod folder in use, the default becomes
// "/tssm/mods/<name>/saves": a mod's levels and tasks differ from the disc's, so
// its saves and the disc's must not load into each other. The rule is
// iSaveStoreRoot's, so the command line and the in-game Saves tab find the same
// folder.
static void iSG_resolve_saveroot()
{
    snprintf(g_saveroot, sizeof(g_saveroot), "%s", iSaveStoreRoot());
    printf("tssm: saves: %s\n", g_saveroot);
    fflush(stdout);
}

// The first target is the save root ITSELF rather than a subdirectory of it,
// the second "second" inside it; the folders past those two exist only for the
// PC save screen, and are made the first time something is saved into one.
// See iSaveStoreTargetDir.
static void iSG_target_root(S32 slot, char* out, size_t outsize)
{
    iSaveStoreTargetDir(slot, out, outsize);
}

// Where the host keeps st_XSAVEGAME_LEADER's fields, for turning the leader
// block between its layout and the console's (iSavePayloadConvert).
static const iSaveHostLayout* iSG_host_layout()
{
    static const iSaveHostLayout layout = {
        (U32)sizeof(st_XSAVEGAME_LEADER), (U32)offsetof(st_XSAVEGAME_LEADER, progress),
        (U32)offsetof(st_XSAVEGAME_LEADER, gametime),
        (U32)offsetof(st_XSAVEGAME_LEADER, thumbIconIdx)
    };
    return &layout;
}

// The full path of one save file on the active target.
static bool iSG_path(st_ISGSESSION* isgdata, const char* fname, char* out, size_t outsize)
{
    if (isgdata == NULL || isgdata->slot < 0 || fname == NULL || fname[0] == '\0')
    {
        return false;
    }

    snprintf(out, outsize, "%s/%s", isgdata->mcdata[isgdata->slot].root, fname);
    return true;
}

// At startup, before the game asks about any save: every slot the game could
// not use is put right where it can be (iSaveStoreRecover -- a save left empty
// or cut short by a system crash during a write), and what each folder holds
// goes to the log, so a report of saves gone missing can be checked against
// what the game itself found.
static void iSG_check_saves()
{
    iSaveStoreChange change; // a repair
    for (S32 t = 0; t < ISG_MAX_TARGETS; t++)
    {
        char dir[512];
        iSG_target_root(t, dir, sizeof(dir));
        iHostFileInfo st;
        if (!iHostStat(dir, &st) || !st.is_dir)
        {
            continue;
        }

        char line[768] = "";
        size_t len = 0;
        S32 found = 0;
        for (S32 g = 0; g < ISG_NUM_FILES; g++)
        {
            char report[768];
            if (iSaveStoreRecover(t, g, report, sizeof(report)) || report[0] != '\0')
            {
                printf("tssm: saves: %s\n", report);
            }

            iSaveSlotInfo s;
            iSaveStoreSlot(t, g, &s);
            if (!s.exists || len >= sizeof(line))
            {
                continue;
            }

            const char* sep = found++ != 0 ? ", " : "";
            if (s.valid)
            {
                len += snprintf(line + len, sizeof(line) - len, "%s%s %d%% %s%s", sep, s.name,
                                (int)s.progress, s.scene, s.fromCard ? " (from a card)" : "");
            }
            else
            {
                len += snprintf(line + len, sizeof(line) - len, "%s%s damaged (%s)", sep, s.name,
                                s.problem);
            }
        }

        if (t < ISG_HOST_TARGETS || found != 0)
        {
            printf("tssm: saves: folder %c (%s): %s\n", 'A' + (int)t,
                   t == 0 ? "Memory Card Slot 1" :
                   t == 1 ? "Memory Card Slot 2" :
                            "PC menus",
                   found != 0 ? line : "no saves");
        }
    }
    fflush(stdout);
}

// iSGSetNoGamesHook's; iSGSelectGameDir calls it for an empty folder.
static void (*sNoGamesHook)(S32 target, S32 otherWithSaves);

// Whether folder `tgt` holds a save file with something in it -- the test
// iSGSelectGameDir makes -- asked of the disk directly, so that no session's
// active target moves to answer it.
static bool iSG_target_has_saves(S32 tgt)
{
    for (S32 g = 0; g < ISG_NUM_FILES; g++)
    {
        char path[512];
        iSaveStoreSlotPath(tgt, g, path, sizeof(path));

        iHostFileInfo st;
        if (iHostStat(path, &st) && st.is_file && st.size > 0)
        {
            return true;
        }
    }
    return false;
}

// PLATFORM_PC, for zSaveLoad_CardPick: over the two folders the load screen
// offers as memory card slots.
S32 iSGOnlyTargetWithGames()
{
    S32 found = -1;
    for (S32 t = 0; t < ISG_HOST_TARGETS; t++)
    {
        if (iSG_target_has_saves(t))
        {
            if (found >= 0)
            {
                return -1;
            }
            found = t;
        }
    }
    return found;
}

S32 iSGStartup()
{
    if (g_isginit++ != 0)
    {
        return g_isginit;
    }

    iSG_resolve_saveroot();

    // Retail's iSG_start_your_engines brings CARD up and loads the icon
    // artwork. The host equivalent is making sure the directory exists, so
    // that a first run has somewhere to save to before anything asks.
    for (S32 i = 0; i < ISG_HOST_TARGETS; i++)
    {
        char root[512];
        iSG_target_root(i, root, sizeof(root));
        iSG_mkdir_p(root);
    }

    iSG_check_saves();
    return g_isginit;
}

S32 iSGShutdown()
{
    if (g_isginit > 0)
    {
        g_isginit--;
    }

    return 1;
}

// Retail's rotating buffer, kept because callers hold on to several of these
// at once -- xSGTgtHaveRoom copies one while xSG_chdir_gamedir holds another --
// and would alias if there were only one.
char* iSGMakeName(en_NAMEGEN_TYPE type, const char* base, S32 idx)
{
    static S32 rotate = 0;
    static char rotatebuf[8][32] = { 0 };

    char* use_buf = rotatebuf[rotate++];
    if (rotate == 8)
    {
        rotate = 0;
    }

    *use_buf = '\0';

    switch (type)
    {
    case ISG_NGTYP_GAMEFILE:
        // TSSM numbers its save files from 1 ("SpongeBobMovie01"..), where
        // BfBB counts from 0 (gc/isavegame.cpp).
        snprintf(use_buf, 32, "%s%02d", base != NULL ? base : "SpongeBobMovie", idx + 1);
        break;

    // A memory card has no directories -- every file sits at the root -- so
    // retail returns an empty name for all three of these and the callers
    // treat that as "no subdirectory". The host layout is the same, one flat
    // directory per target, so the answer is the same.
    case ISG_NGTYP_GAMEDIR:
    case ISG_NGTYP_CONFIG:
    case ISG_NGTYP_ICONTHUM:
        break;
    }

    return use_buf;
}

st_ISGSESSION* iSGSessionBegin(void* cltdata, void (*chgfunc)(void*, en_CHGCODE), S32 monitor)
{
    memset(&g_isgdata_MAIN, 0, sizeof(st_ISGSESSION));

    g_isgdata_MAIN.slot = -1;
    g_isgdata_MAIN.chgfunc = chgfunc;
    g_isgdata_MAIN.cltdata = cltdata;
    g_isgdata_MAIN.monitor = monitor;

    return &g_isgdata_MAIN;
}

void iSGSessionEnd(st_ISGSESSION* isgdata)
{
    if (isgdata == NULL)
    {
        return;
    }

    memset(isgdata, 0, sizeof(st_ISGSESSION));
}

// Retail probes both card slots and counts the ones that answer. A directory
// is always there, so the count is however many targets this build exposes.
S32 iSGTgtCount(st_ISGSESSION* isgdata, S32* max)
{
    if (max != NULL)
    {
        *max = ISG_NUM_SLOTS;
    }

    return ISG_HOST_TARGETS;
}

S32 iSG_mcidx2slot(S32 tidx, S32* out_slot, S32* ready)
{
    *out_slot = -1;

    if (ready != NULL)
    {
        for (S32 i = 0; i < ISG_NUM_SLOTS; i++)
        {
            ready[i] = (i < ISG_HOST_TARGETS);
        }
    }

    if (tidx < 0 || tidx >= iSG_targets())
    {
        return 0;
    }

    // Every target this build exposes is present, so the index into the
    // present ones and the physical slot are the same number. On the console
    // they differ whenever slot A is empty and slot B is not.
    *out_slot = tidx;
    return 1;
}

S32 iSGTgtPhysSlotIdx(st_ISGSESSION* isgdata, S32 tidx)
{
    S32 idx = -1;
    if (iSG_mcidx2slot(tidx, &idx, NULL))
    {
        return idx;
    }

    return -1;
}

S32 iSGTgtSetActive(st_ISGSESSION* isgdata, S32 tgtidx)
{
    S32 slot = -1;
    if (!iSG_mcidx2slot(tgtidx, &slot, NULL))
    {
        return 0;
    }

    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[slot];

    if (!data->inuse)
    {
        data->inuse = 1;
        data->chan = slot;
        data->sectorSize = 0;
        iSG_target_root(slot, data->root, sizeof(data->root));
    }

    isgdata->slot = slot;
    return 1;
}

// The state bits, as xSGTgtIsFormat reads them: 0x1 present, 0x2 formatted,
// 0x4 needs formatting, 0x1000000 no such target. Retail also reports a card
// with an unreadable encoding (0x4000000) or an illegal sector size
// (0x8000004); a directory has neither failure mode.
U32 iSGTgtState(st_ISGSESSION* isgdata, S32 tgtidx, const char* dpath)
{
    S32 slot = -1;
    iSG_mcidx2slot(tgtidx, &slot, NULL);

    if (slot < 0)
    {
        return 0x1000000;
    }

    if (slot != isgdata->slot)
    {
        iSGTgtSetActive(isgdata, tgtidx);
    }

    char root[512];
    iSG_target_root(slot, root, sizeof(root));

    iHostFileInfo st;
    if (!iHostStat(root, &st) || !st.is_dir)
    {
        // Present as a target, but not yet usable -- the game offers to
        // format, which here means creating the directory.
        return 0x1 | 0x4;
    }

    return 0xF;
}

// "Formatting" a card is making it able to hold files. For a directory that is
// creating it.
S32 iSGTgtFormat(st_ISGSESSION* isgdata, S32 tgtidx, S32 async, S32* canRecover)
{
    if (canRecover != NULL)
    {
        *canRecover = 0;
    }

    S32 slot = -1;
    if (!iSG_mcidx2slot(tgtidx, &slot, NULL))
    {
        return 0;
    }

    char root[512];
    iSG_target_root(slot, root, sizeof(root));

    return iSG_mkdir_p(root) ? 1 : 0;
}

U8 iSGCheckMemoryCard(st_ISGSESSION* isgdata, S32 index)
{
    return (index >= 0 && index < iSG_targets()) ? 1 : 0;
}

S32 iSGTargetMax()
{
    return iSG_targets();
}

S32 iSGMakeTarget(S32 tgt)
{
    if (tgt < 0 || tgt >= iSG_targets())
    {
        return 0;
    }

    char root[512];
    iSG_target_root(tgt, root, sizeof(root));
    return iSG_mkdir_p(root) ? 1 : 0;
}

// Retail returns the slot holding a card from another region, which the TRC
// rules require be reported rather than silently ignored. No host equivalent.
S32 iSGCheckForWrongDevice()
{
    return -1;
}

static S32 iSG_free_bytes(const char* path)
{
    U64 avail;
    if (!iHostFreeBytes(path, &avail))
    {
        return 0;
    }

    // The game compares this against a save size in a S32 and displays it. A
    // modern disk overflows that, and reporting a negative number of free
    // bytes would read as "no room" -- clamp instead.
    if (avail > 0x7FFFFFFFULL)
    {
        return 0x7FFFFFFF;
    }

    return (S32)avail;
}

static S32 iSG_have_room(st_ISGSESSION* isgdata, S32 fsize, const char* fname, S32* bytesNeeded,
                         S32* availOnDisk, S32* needFile)
{
    if (isgdata->slot < 0)
    {
        return 0;
    }

    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[isgdata->slot];
    if (!data->inuse)
    {
        return 0;
    }

    S32 avail = iSG_free_bytes(data->root);

    // Overwriting a file that already exists costs only the difference, and
    // needs no new directory entry. Retail draws the same distinction, because
    // a card can be out of file slots while still having free blocks.
    S32 existing = 0;
    S32 need_entry = 1;

    if (fname != NULL && fname[0] != '\0')
    {
        char path[512];
        if (iSG_path(isgdata, fname, path, sizeof(path)))
        {
            iHostFileInfo st;
            if (iHostStat(path, &st))
            {
                existing = (S32)st.size;
                need_entry = 0;
            }
        }
    }

    S32 needed = fsize - existing;
    if (needed < 0)
    {
        needed = 0;
    }

    if (bytesNeeded != NULL)
    {
        *bytesNeeded = needed;
    }
    if (availOnDisk != NULL)
    {
        *availOnDisk = avail;
    }
    if (needFile != NULL)
    {
        *needFile = need_entry;
    }

    return needed <= avail;
}

// The unit the save UI displays in. See isavegame.h for why it is not blocks.
//
// U64 internally because iSGFormatFreeSpace has a whole disk to word, which the
// S32 the rest of this interface passes around cannot hold.
static void iSG_format_bytes(U64 bytes, char* out, U32 outsize)
{
    if (out == NULL || outsize == 0)
    {
        return;
    }

    // Rounded up: a save that needs one byte past a kilobyte needs the next one.
    U64 units = (bytes + 1023u) / 1024u;
    const char* name[] = { "KB", "MB", "GB", "TB" };
    S32 i = 0;

    // The remainder of the last division is the tenth, so it has to be kept as
    // the loop climbs rather than recovered from `units` afterwards.
    U64 rem = 0;
    while (units >= 1024 && i < 3)
    {
        rem = units % 1024;
        units /= 1024;
        i++;
    }

    if (i == 0)
    {
        snprintf(out, outsize, "%u KB", (unsigned)units);
        return;
    }

    // One decimal, in integers rather than through a float, and TRUNCATED where
    // the whole number is rounded up: 1.99 GB reading as "2.0 GB" would be a
    // lie in the one direction that matters when the question is whether
    // something fits.
    snprintf(out, outsize, "%u.%u %s", (unsigned)units, (unsigned)(rem * 10 / 1024), name[i]);
}

void iSGFormatSize(S32 bytes, char* out, U32 outsize)
{
    // A negative can only come of an overflow upstream. It is not free space.
    iSG_format_bytes(bytes > 0 ? (U64)bytes : 0, out, outsize);
}

// Free space, asked of the host directly rather than taken from the S32 the
// game carries it in.
//
// That S32 is iSG_have_room's answer, and it is clamped to 2 GB because the game
// compares it against a save size in a signed 32-bit int -- see iSG_free_bytes.
// The clamp is right for the comparison and useless for the display: every disk
// this will ever run on would read "2.0 GB". The save screen's free-space line
// asks a different question and gets a different number.
//
// The save root's volume, not the active target's: the second target lives
// inside the first, so there is only ever one volume to report on.
void iSGFormatFreeSpace(char* out, U32 outsize)
{
    U64 avail = 0;
    if (!iHostFreeBytes(g_saveroot, &avail))
    {
        avail = 0;
    }

    iSG_format_bytes(avail, out, outsize);
}

S32 iSGTgtHaveRoom(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                   const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile)
{
    return iSG_have_room(isgdata, fsize, fname, bytesNeeded, availOnDisk, needFile);
}

// Retail's startup variant differs only in how it accounts for the card's
// per-file overhead before any game files exist. There is no per-file overhead
// here, so the two are the same question.
S32 iSGTgtHaveRoomStartup(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                          const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile)
{
    return iSG_have_room(isgdata, fsize, fname, bytesNeeded, availOnDisk, needFile);
}

S32 iSGFileSize(st_ISGSESSION* isgdata, const char* fname)
{
    if (isgdata->slot < 0)
    {
        return -1;
    }

    char path[512];
    if (!iSG_path(isgdata, fname, path, sizeof(path)))
    {
        return -1;
    }

    iHostFileInfo st;
    if (!iHostStat(path, &st) || !st.is_file)
    {
        return -1;
    }

    return (S32)st.size;
}

char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname)
{
    return iSGFileModDate(isgdata, fname, NULL, NULL, NULL, NULL, NULL, NULL);
}

char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname, S32* sec, S32* min, S32* hr,
                     S32* mon, S32* day, S32* yr)
{
    static char datestr[0x40] = { 0 };

    char path[512];
    iHostFileInfo st;

    if (iSG_path(isgdata, fname, path, sizeof(path)) && iHostStat(path, &st))
    {
        iHostCalendar t;
        iHostLocalTimeOf(st.mtime, &t);

        S32 v_sec = t.sec;
        S32 v_min = t.min;
        S32 v_hr = t.hour;
        S32 v_mon = t.mon + 1;
        S32 v_day = t.mday;
        S32 v_yr = t.year + 1900;

        sprintf(datestr, "%02d/%02d/%04d %02d:%02d:%02d", v_mon, v_day, v_yr, v_hr, v_min, v_sec);

        if (sec != NULL)
        {
            *sec = v_sec;
        }
        if (min != NULL)
        {
            *min = v_min;
        }
        if (hr != NULL)
        {
            *hr = v_hr;
        }
        if (mon != NULL)
        {
            *mon = v_mon;
        }
        if (day != NULL)
        {
            *day = v_day;
        }
        if (yr != NULL)
        {
            *yr = v_yr;
        }
    }
    else
    {
        sprintf(datestr, "<Unknown Modification>");
    }

    return datestr;
}

void iSGMakeTimeStamp(char* str)
{
    iHostCalendar t;
    iHostLocalTime(&t);

    sprintf(str, "%02d/%02d/%04d %02d:%02d:%02d", t.mon + 1, t.mday, t.year + 1900, t.hour, t.min,
            t.sec);
}

// "Is there a game to load on this target." Retail answers it by counting the
// save files that exist, because a memory card has no directory to look for.
S32 iSGSelectGameDir(st_ISGSESSION* isgdata, const char* dname)
{
    if (isgdata->slot < 0 || !isgdata->mcdata[isgdata->slot].inuse)
    {
        return 0;
    }

    S32 count = 0;
    for (S32 idx = 0; idx < ISG_NUM_FILES; idx++)
    {
        const char* n = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, idx);
        if (iSGFileSize(isgdata, n) > 0)
        {
            count++;
        }
    }

    // None here, and the load screen is about to say so in the Xbox disc's
    // words -- "No game saves present on the Xbox Hard Disk", the one place an
    // Xbox keeps saves. Here a folder is one of two memory card slots, and the
    // other may hold every save the player has, so the hook has the message
    // name the slot and point at the other one.
    if (count == 0 && sNoGamesHook != NULL)
    {
        S32 other = -1;
        for (S32 t = 0; t < ISG_HOST_TARGETS && other < 0; t++)
        {
            if (t != isgdata->slot && iSG_target_has_saves(t))
            {
                other = t;
            }
        }
        sNoGamesHook(isgdata->slot, other);
    }

    return count != 0;
}

void iSGSetNoGamesHook(void (*hook)(S32 target, S32 otherWithSaves))
{
    sNoGamesHook = hook;
}

S32 iSGSetupGameDir(st_ISGSESSION* isgdata, const char* dname, S32 force_iconfix)
{
    // Retail returns 1 without doing anything: there is no directory to make on
    // a memory card. Here there is, and iSGStartup already made it -- but a
    // player can delete it between runs, so this is where it comes back.
    if (isgdata->slot < 0)
    {
        return 0;
    }

    return iSG_mkdir_p(isgdata->mcdata[isgdata->slot].root) ? 1 : 0;
}

static void iSG_fail(st_ISGSESSION* isgdata, en_ASYNC_OPERR err)
{
    isgdata->as_opstat = ISG_OPSTAT_FAILURE;
    isgdata->as_operr = err;
}

// Where a save's still comes from; see iSGSetThumbSource.
static S32 sThumbSource = ISG_THUMB_NOW;

void iSGSetThumbSource(S32 source)
{
    sThumbSource = source;
}

void iSGCaptureThumb()
{
    if (sPCTargets)
    {
        iSaveThumbCapture();
    }
}

S32 iSGThumbPath(S32 tgt, S32 game, char* out, U32 outsize)
{
    if (tgt < 0 || tgt >= iSG_targets() || game < 0 || game >= ISG_NUM_FILES)
    {
        return 0;
    }

    char root[512];
    iSG_target_root(tgt, root, sizeof(root));
    snprintf(out, outsize, "%s/%s.tga", root, iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, game));
    return 1;
}

// The still beside a save that has just been written, as SpongeBob00.tga next
// to SpongeBob00. PC menus only: retail's screens have nowhere to show one.
//
// A save with no still to go with it takes the old one away, unless it is an
// autosave with nothing to take right now -- then the one it had is still of
// the same game, only earlier, and better than the stock picture.
static void iSG_write_still(const char* savePath)
{
    if (!sPCTargets)
    {
        return;
    }

    char still[544];
    snprintf(still, sizeof(still), "%s.tga", savePath);

    switch (sThumbSource)
    {
    case ISG_THUMB_NOW:
        iSaveThumbCapture();
        if (!iSaveThumbHave())
        {
            return;
        }
        break;

    case ISG_THUMB_KEPT:
        if (!iSaveThumbHave())
        {
            iHostRemoveFile(still);
            return;
        }
        break;

    default:
        iHostRemoveFile(still);
        return;
    }

    if (!iSaveThumbWrite(still))
    {
        iHostRemoveFile(still);
    }
}

// In the console's byte order (iSavePayloadConvert), written to a temporary
// and renamed, so that losing power partway through leaves the previous save
// intact rather than a truncated one -- the console got this for free, CARD
// writes whole sectors and updates the directory entry last. The save it
// replaces is kept as backup/<name>.1 (iSaveStore.h).
S32 iSGSaveFile(st_ISGSESSION* isgdata, const char* fname, char* data, S32 n, S32 async,
                char* label)
{
    iSaveStoreChange change; // the save and its still
    char path[512];

    if (isgdata->slot < 0 || !iSG_path(isgdata, fname, path, sizeof(path)))
    {
        iSG_fail(isgdata, ISG_OPERR_NOCARD);
        return 0;
    }

    if (!iSG_mkdir_p(isgdata->mcdata[isgdata->slot].root))
    {
        iSG_fail(isgdata, ISG_OPERR_GAMEDIR);
        return 0;
    }

    std::vector<U8> canonical(n > 0 ? (size_t)n : 1);
    if (n > 0 && !iSavePayloadConvert(ISF_TO_CANONICAL, (const U8*)data, &canonical[0], (size_t)n,
                                      iSG_host_layout()))
    {
        printf("tssm: save %s: the game's data did not have the shape of a save; written as it "
               "was\n",
               fname);
        fflush(stdout);
    }

    char why[256];
    if (!iSaveStoreWriteFile(path, &canonical[0], n > 0 ? (size_t)n : 0, true, why, sizeof(why)))
    {
        printf("tssm: save %s failed: %s\n", fname, why);
        fflush(stdout);
        iSG_fail(isgdata, ISG_OPERR_SVWRITE);
        return 0;
    }

    printf("tssm: saved %s (%d bytes)\n", path, (int)n);
    fflush(stdout);

    iSG_write_still(path);

    isgdata->as_opstat = ISG_OPSTAT_SUCCESS;
    isgdata->as_operr = ISG_OPERR_NONE;
    return 1;
}

// Reads the first numbytes of a save, `whole` when that is all of it.
static S32 iSG_read(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 numbytes,
                    bool whole)
{
    char path[512];

    if (isgdata->slot < 0 || !iSG_path(isgdata, fname, path, sizeof(path)))
    {
        iSG_fail(isgdata, ISG_OPERR_NOCARD);
        return 0;
    }

    if (numbytes <= 0 || databuf == NULL)
    {
        iSG_fail(isgdata, ISG_OPERR_LDREAD);
        return 0;
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        iSG_fail(isgdata, ISG_OPERR_LDOPEN);
        return 0;
    }

    std::vector<U8> canonical((size_t)numbytes);
    size_t got = fread(&canonical[0], 1, (size_t)numbytes, f);
    fclose(f);

    // A short read means the file is smaller than its own header says, which
    // is what a truncated save looks like. Retail reads whole sectors and
    // would get padding, so the callers do not check -- say so here instead of
    // handing back a partly-filled buffer.
    if (got != (size_t)numbytes)
    {
        memset(databuf, 0, (size_t)numbytes);
        iSG_fail(isgdata, ISG_OPERR_CORRUPT);
        return 0;
    }

    // From the console's byte order to this host's. A whole file has its
    // checksum redone for the host's bytes if (only if) it held for the
    // console's, so xSG_ld_validate passes and fails the same saves it would
    // on the console.
    if (whole)
    {
        char why[160] = "";
        bool ok = iSavePayloadInspect(&canonical[0], (size_t)numbytes, NULL, why, sizeof(why));
        printf("tssm: load %s: %s%s\n", path, ok ? "ok" : "damaged, ", ok ? "" : why);
        fflush(stdout);
    }

    iSavePayloadConvert(ISF_TO_HOST, &canonical[0], (U8*)databuf, (size_t)numbytes,
                        iSG_host_layout());

    // A file shorter than its own header says. xSG_ld_validate takes the
    // header's size on trust and would checksum past the end of the buffer
    // (on the console the card file's length and the header agree). Made to
    // say what it is instead: a damaged save of the size there is, whose
    // checksum fails.
    U32 field[3];
    memcpy(field, databuf, sizeof(field));
    if (whole && numbytes >= ISF_PAYLOAD_HEADER && field[0] == ISF_TAG('G', 'D', 'A', 'T') &&
        field[2] > (U32)numbytes)
    {
        U32 size = (U32)numbytes;
        U32 crc = ~iSaveCRC(0xFFFFFFFFu, (const U8*)databuf + ISF_PAYLOAD_HEADER,
                            size - ISF_PAYLOAD_HEADER);
        memcpy(databuf + 8, &size, 4);
        memcpy(databuf + 12, &crc, 4);
    }

    isgdata->as_opstat = ISG_OPSTAT_SUCCESS;
    isgdata->as_operr = ISG_OPERR_NONE;
    return 1;
}

// The name is retail's: the save UI calls it to pull just the head off each
// slot -- the label, progress and play time it lists -- without reading the
// whole file.
S32 iSGReadLeader(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 numbytes, S32 async)
{
    return iSG_read(isgdata, fname, databuf, numbytes, false);
}

S32 iSGLoadFile(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 async)
{
    S32 numBytes = iSGFileSize(isgdata, fname);
    return iSG_read(isgdata, fname, databuf, numBytes, true);
}

// Retail's I/O is synchronous even where the interface is not -- every
// operation has already finished by the time it returns, and this reports what
// it left behind. Host file I/O is the same.
en_ASYNC_OPSTAT iSGPollStatus(st_ISGSESSION* isgdata, en_ASYNC_OPCODE* curop, S32 block)
{
    if (curop != NULL)
    {
        *curop = isgdata->as_curop;
    }

    return isgdata->as_opstat;
}

en_ASYNC_OPERR iSGOpError(st_ISGSESSION* isgdata, char* errmsg)
{
    // Retail's strings, in en_ASYNC_OPERR order. They survive in the shipping
    // binary only because the array is static; nothing renders them.
    static const char* errmsgs[] = {
        "No current error",
        "No operation in async queue",
        "Too many async ops queued simultaneously",
        "Init Failed",
        "Unable to access Save Game Directory",
        "Access Error - no card ?!? (eg yanked out)",
        "Access Error - no room on card (file handles free bytes, etc)",
        "Access Error - card is damaged or something bad",
        "Access Error - file being loaded appears to be corrupt (I-Level)",
        "Access Error - general problem",
        "Save Error - Not enough free space to save file",
        "Save Error - during initalization (async queue)",
        "Save Error - during write",
        "Save Error - opening file",
        "Load Error - during initalization (async queue)",
        "Load Error - during read",
        "Load Error - opening file",
        "Target problem (general error)",
        "Target Error - media removed or changed",
        "Target Error - Not ready for I/O (unformatted?)",
        "Operation encountered unknown error",
        NULL
    };

    if (errmsg == NULL)
    {
        return isgdata->as_operr;
    }

    if (isgdata->as_operr < ISG_OPERR_NOMORE)
    {
        strncpy(errmsg, errmsgs[isgdata->as_operr], 0x80);
    }
    else
    {
        strncpy(errmsg, errmsgs[ISG_OPERR_UNKNOWN], 0x80);
    }

    errmsg[0x7f] = '\0';
    return isgdata->as_operr;
}

// Retail checks each save file's card metadata -- the icon header and the
// directory entry -- and reports the ones that do not hold up, so that the boot
// check (zMain.cpp, after zMenuCardCheckStartup) stops on the
// text_mem_card_corrupt_file message and offers to delete them through CARD.
//
// A folder has no card metadata, and that screen does nothing useful here. On
// the PC it comes up black, with no text, and waits for a button; its delete
// goes to the CARD stubs, which delete nothing. It was reached by a save file
// that is empty or cannot be opened -- what a crash during a save leaves -- and
// held the game at a black screen at boot. So nothing is reported. iSGStartup
// has by then put back what it could (iSaveStoreRecover) and logged the rest;
// the save list shows such a file as an empty slot (no bytes) or a damaged save
// (iSGIsGameCorrupt), and the load refuses one whose checksum fails
// (xSG_ld_validate).
U8 iSGCheckForCorruptFiles(st_ISGSESSION* isgdata, char files[][64])
{
    memset(files, 0, ISG_NUM_FILES * 64);
    return 0;
}

void iSGAutoSave_Startup()
{
}

// iSaveStore.h. The cache's own flag rather than XSGAutoData::IsValid, which
// probes the target and can clear the cache: asking must not change it.
bool iSaveStoreAutoSaveSlot(S32* target, S32* game)
{
    XSGAutoData* asg = xSGAutoSave_GetCache();
    if (asg == NULL || (asg->flg_autodata & 1) == 0 || asg->lastTarg < 0 || asg->lastGame < 0)
    {
        return false;
    }
    if (target != NULL)
    {
        *target = asg->lastTarg;
    }
    if (game != NULL)
    {
        *game = asg->lastGame;
    }
    return true;
}

st_ISGSESSION* iSGAutoSave_Connect(S32 idx_target, void* cltdata, void (*chg)(void*, en_CHGCODE))
{
    st_ISGSESSION* isgdata = iSGSessionBegin(cltdata, chg, 1);
    if (isgdata == NULL)
    {
        return NULL;
    }

    if (iSGTgtSetActive(isgdata, idx_target) == 0)
    {
        iSGSessionEnd(isgdata);
        return NULL;
    }

    return isgdata;
}

void iSGAutoSave_Disconnect(st_ISGSESSION* isg)
{
    iSGSessionEnd(isg);
}

// Retail also clears globals.autoSaveFeature here when the card has gone, which
// is the platform layer reaching into game state. It is not reproduced: the
// caller, XSGAutoData::HWCheckConnect, already returns this value, and
// zSaveLoad.cpp turns the feature off itself on a false. A save directory
// cannot be pulled out mid-frame the way a card can, so the branch that
// mattered on the console cannot fire here anyway.
S32 iSGAutoSave_Monitor(st_ISGSESSION* isg, S32 idx_target)
{
    if (isg == NULL)
    {
        return 0;
    }

    U32 state = iSGTgtState(isg, idx_target, NULL);
    if (state == 0 || (state & 1) == 0)
    {
        return 0;
    }

    return 1;
}


// TSSM: whether game slot `idx` holds a save that will not load, for the save
// list to say "damaged" against it. Retail asks the card whether the file's
// entry and head are the game's (iSG_fileKosher) and leaves the checksum to the
// load. The host's equivalent of the first is the payload's own header: the
// magic, the version, and a size that is the file's. The checksum is left to
// the load here too.
U8 iSGIsGameCorrupt(st_ISGSESSION* isgdata, S32 idx)
{
    char path[512];
    if (isgdata == NULL || isgdata->slot < 0 ||
        !iSG_path(isgdata, iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, idx), path, sizeof(path)))
    {
        return 0;
    }

    iHostFileInfo st;
    if (!iHostStat(path, &st) || !st.is_file)
    {
        return 0;
    }

    U8 head[ISF_PAYLOAD_HEADER];
    FILE* f = fopen(path, "rb");
    size_t got = 0;
    if (f != NULL)
    {
        got = fread(head, 1, sizeof(head), f);
        fclose(f);
    }

    if (got != sizeof(head) || iSaveGetU32(head) != ISF_TAG('G', 'D', 'A', 'T') ||
        iSaveGetU32(head + 4) != 1 || (U64)iSaveGetU32(head + 8) != st.size)
    {
        return 1;
    }

    return 0;
}
