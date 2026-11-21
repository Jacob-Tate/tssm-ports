// PC-only: tssm.exe's save tools on the command line. See iSaveCLI.h.
#include "iSaveCLI.h"
#include "iSaveFormat.h"
#include "iSaveStore.h"
#include "iConfig.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

static void iSC_usage()
{
#if defined(__3DS__) || defined(__SWITCH__)
    printf("The SpongeBob SquarePants Movie (port) -- save tools\n"
#else
    printf(TSSM_GAME_TITLE " (PC) -- save tools\n"
#endif
           "\n"
           "  tssm --saves [--all]                   list the save slots (--all: every folder)\n"
           "  tssm --import-save FILE [--slot S] [--pick N] [--force] [--dry-run]\n"
           "                                         import a GameCube save: a Dolphin .gci, a\n"
           "                                         .gcs, a raw memory card (.raw/.gcp), a zip\n"
           "                                         from --export-save all, or a save file\n"
           "  tssm --export-save S FILE.gci          a slot as a .gci for Dolphin or a console\n"
           "  tssm --export-save S FOLDER            the same, named as Dolphin names it\n"
           "  tssm --export-save all FILE.zip        every save in one file, for another install\n"
           "  tssm --export-save all FOLDER          every save as a .gci (Dolphin's GCI folder)\n"
           "       [--region E|P]                    the game code to write (default: the save's\n"
           "                                         own, or GGVE)\n"
           "  tssm --import-save FOLDER ...          import Xbox saves: a save's folder, the\n"
           "                                         game's (UDATA\\54510027), a whole UDATA,\n"
           "                                         Cxbx-Reloaded's data folder, or a zip of one\n"
           "  tssm --export-save S FOLDER --xbox emu|real\n"
           "       [--xbe FILE] [--keys FILE] [--as N] [--replace]\n"
           "                                         a slot as an Xbox save under FOLDER.\n"
           "                                         emu: for Cxbx-Reloaded and other copies of\n"
           "                                         this port; needs DEFAULT.XBE from your own\n"
           "                                         Xbox game (--xbe, or it is with the game's\n"
           "                                         files). real: for a real Xbox or xemu; also\n"
           "                                         needs --keys, your keys.bin from your own\n"
           "                                         Xbox. --as N: which of the Xbox's saves 1-3\n"
           "  tssm --export-save all FOLDER --xbox emu|real [--slot B]\n"
           "                                         folder A's three saves (or --slot B's)\n"
           "  tssm --import-save FILE|FOLDER ...     import PlayStation 2 saves: a .psu, an\n"
           "                                         Action Replay .max, a PlayStation 3 .psv,\n"
           "                                         a memory card file (PCSX2's .ps2, a PS3's\n"
           "                                         .VM2), PCSX2's folder-type card, or the\n"
           "                                         game's own folder\n"
           "  tssm --export-save S DEST --ps2 auto|psu|psv|folder|card\n"
           "       [--ps2-icon PATH] [--pal] [--as N] [--replace]\n"
           "                                         a slot as a PlayStation 2 save. psu: a .psu\n"
           "                                         file for uLaunchELF or mymc. folder: into\n"
           "                                         PCSX2's folder-type card DEST. card: into\n"
           "                                         the memory card file DEST, which is copied\n"
           "                                         to DEST.bak first (a new name ending .ps2\n"
           "                                         makes a new card). psv: as --psv standin.\n"
           "                                         auto: by what DEST is.\n"
           "                                         --ps2-icon: PLAT.HIP from your own PS2 disc\n"
           "                                         or a save the game made, for the game's own\n"
           "                                         icon (without it, the port's). --pal: the\n"
           "                                         European game's folder\n"
           "  tssm --export-save all DEST --ps2 ... [--slot B]\n"
           "                                         folder A's three saves (or --slot B's)\n"
           "  tssm --export-save S DEST --psv standin|real [--psv-keys FILE]\n"
           "       [--ps2-icon PATH] [--pal] [--as N] [--replace]\n"
           "                                         a slot as a PlayStation 3 export (.psv;\n"
           "                                         into a folder: named as a PS3 names it).\n"
           "                                         standin: for other copies of this port and\n"
           "                                         PC save tools; a PS3 needs it re-signed\n"
           "                                         first. real: the PS3's own signature, made\n"
           "                                         with --psv-keys, your PS3 key file (32\n"
           "                                         bytes, docs/SAVES.md); not yet confirmed on\n"
           "                                         a PS3. A PS3's virtual memory card (.VM2)\n"
           "                                         is a card: --ps2 card (a new name ending\n"
           "                                         .vm2 makes a new one)\n"
           "  tssm --export-save all DEST --psv ... [--slot B]\n"
           "                                         folder A's three saves in one .psv\n"
           "  tssm --check-save FILE                 say what FILE holds; import nothing\n"
           "       [--xbe FILE] [--keys FILE]        (here and with --import-save: the files to\n"
           "       [--psv-keys FILE]                 tell an Xbox save's or a .psv's signature\n"
           "                                         by; it is read whatever its signature)\n"
           "  tssm --restore-save S [N]              put backup N (1 = newest) back in slot S\n"
           "  tssm --delete-save S                   empty slot S (the save becomes backup 1)\n"
           "  tssm --save-dir DIR ...                use DIR as the save folder\n"
           "\n"
           "S is a slot: A1..A3 are the first save folder's three games (the console's\n"
           "Memory Card Slot A), B1..B3 the second's; 1..3 alone means folder A. --slot B\n"
           "imports into folder B under the numbers the saves carry.\n"
           "\n"
           "Saves are in %s\n",
           iSaveStoreRoot());
}

static void iSC_date(S64 t, char* out, size_t outsize)
{
    if (t <= 0)
    {
        snprintf(out, outsize, "-");
        return;
    }
    iHostCalendar c;
    iHostLocalTimeOf(t, &c);
    snprintf(out, outsize, "%04d-%02d-%02d %02d:%02d", (int)c.year + 1900, (int)c.mon + 1,
             (int)c.mday, (int)c.hour, (int)c.min);
}

static bool iSC_isDir(const char* path)
{
    iHostFileInfo st;
    return iHostStat(path, &st) && st.is_dir;
}

static bool iSC_endsWith(const char* s, const char* suffix)
{
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && iHostStrCaseCmp(s + n - m, suffix) == 0;
}

static S32 iSC_list(bool all)
{
    printf("Saves in %s\n\n", iSaveStoreRoot());

    std::vector<iSaveSlotInfo> slots;
    iSaveStoreList(slots, all);

    for (size_t i = 0; i < slots.size(); i++)
    {
        const iSaveSlotInfo& s = slots[i];
        if (!s.exists)
        {
            // Folders past B are listed only when something is in them.
            if (s.target < 2 || s.backups > 0)
            {
                printf("  %s  (empty)%s\n", s.name,
                       s.backups > 0 ? "  [has backups: --restore-save]" : "");
            }
            continue;
        }

        char date[32];
        iSC_date(s.mtime, date, sizeof(date));

        if (!s.valid)
        {
            printf("  %s  DAMAGED  %s  (%s)\n", s.name, date, s.problem);
            continue;
        }

        printf("  %s  %-24s %3d%%  %-4s  %s  %3d KB%s%s\n", s.name, s.label, (int)s.progress,
               s.scene, date, (int)((s.size + 1023) / 1024), s.fromCard ? "  [from a card]" : "",
               s.backups > 0 ? "  [backups]" : "");
    }

    return 0;
}

static void iSC_printItems(const std::vector<iSaveImportItem>& items)
{
    for (size_t i = 0; i < items.size(); i++)
    {
        printf("  %s: %s\n", items[i].source.c_str(), items[i].message.c_str());
    }
}

static bool iSC_region(const char* text, char* code)
{
    if (text == NULL)
    {
        return false;
    }

    size_t n = strlen(text);
    if (n == 1)
    {
        snprintf(code, 5, "GGV%c", text[0] >= 'a' ? text[0] - 'a' + 'A' : text[0]);
        return true;
    }
    if (n == 4 && strncmp(text, "GGV", 3) == 0)
    {
        memcpy(code, text, 5);
        return true;
    }
    return false;
}

// A slot, or a folder's three, as Xbox saves under `dest` (iSaveStore.h).
static S32 iSC_exportXbox(const char* slot, const char* dest, const iSaveXboxExportOptions* x,
                          S32 folder)
{
    char err[768] = "";
    const char* forWhat = (x->signature == ISS_XBOX_REAL) ?
                              "a real Xbox or xemu" :
                              "Cxbx-Reloaded and other copies of this port";

    if (iSC_endsWith(dest, ".gci") || iSC_endsWith(dest, ".zip"))
    {
        printf("tssm: an Xbox save is a folder; give --export-save a folder to put it in\n");
        return 2;
    }

    if (iHostStrCaseCmp(slot, "all") == 0)
    {
        std::vector<iSaveExportItem> items;
        S32 count = iSaveStoreExportXboxFolder(folder < 0 ? 0 : folder, x, dest, items);
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].ok)
            {
                printf("  %s -> %s\n", items[i].name, items[i].path.c_str());
            }
            else
            {
                printf("  %s: %s\n", items[i].name, items[i].message.c_str());
            }
        }
        if (count == 0)
        {
            printf("tssm: nothing exported\n");
            return 1;
        }
        printf("tssm: %d Xbox save%s written, signed for %s\n", (int)count, count == 1 ? "" : "s",
               forWhat);
        return (size_t)count == items.size() ? 0 : 1;
    }

    S32 target;
    S32 game;
    if (!iSaveStoreParseSlot(slot, &target, &game))
    {
        printf("tssm: '%s' is not a slot (A1..A3, B1..B3, ... or all)\n", slot);
        return 2;
    }

    std::string where;
    if (!iSaveStoreExportXbox(target, game, x, dest, &where, err, sizeof(err)))
    {
        printf("tssm: export failed: %s\n", err);
        return 1;
    }

    printf("tssm: slot %s exported to %s as an Xbox save, signed for %s\n", slot, where.c_str(),
           forWhat);
    return 0;
}

// What is said of a .psv's signature after it is written ("" when `where` is
// not a .psv).
static const char* iSC_psvWords(const std::string& where, const iSavePS2ExportOptions* p)
{
    if (!iSC_endsWith(where.c_str(), ".psv"))
    {
        return "";
    }
    return p->psvSignature == ISS_PSV_REAL ?
               ", in a .psv with the PS3's own signature from your PS3 key file (this signing is "
               "not yet confirmed on a PS3)" :
               ", in a .psv with the stand-in signature (a PS3 needs it re-signed first, for "
               "example by a save tool on a modded PS3)";
}

// A slot, or a folder's three, as PlayStation 2 saves in `dest` (iSaveStore.h).
static S32 iSC_exportPS2(const char* slot, const char* dest, const iSavePS2ExportOptions* p,
                         S32 folder)
{
    char err[768] = "";

    if (iSC_endsWith(dest, ".gci") || iSC_endsWith(dest, ".zip"))
    {
        printf("tssm: a PlayStation 2 save goes to a .psu file, a .psv, a memory card file (.ps2, "
               ".VM2) or PCSX2's folder-type card\n");
        return 2;
    }

    // Which icon goes with it, when the folder is new.
    std::vector<U8> icon;
    std::string iconFrom;
    if (!iSaveStorePS2Icon(p->icon, icon, &iconFrom, err, sizeof(err)))
    {
        printf("tssm: export failed: %s\n", err);
        return 1;
    }

    std::string where;
    if (iHostStrCaseCmp(slot, "all") == 0)
    {
        std::vector<iSaveExportItem> items;
        S32 count = iSaveStoreExportPS2Folder(folder < 0 ? 0 : folder, p, dest, items, &where, err,
                                              sizeof(err));
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].ok)
            {
                printf("  %s -> %s\n", items[i].name, items[i].path.c_str());
            }
            else
            {
                printf("  %s: %s\n", items[i].name, items[i].message.c_str());
            }
        }
        if (count == 0)
        {
            printf("tssm: nothing exported%s%s\n", err[0] != 0 ? ": " : "", err);
            return 1;
        }
        printf("tssm: %d PlayStation 2 save%s written%s (the icon, if the folder is new: %s)\n",
               (int)count, count == 1 ? "" : "s", iSC_psvWords(where, p),
               iconFrom.empty() ? "the port's own" : iconFrom.c_str());
        return (size_t)count == items.size() ? 0 : 1;
    }

    S32 target;
    S32 game;
    if (!iSaveStoreParseSlot(slot, &target, &game))
    {
        printf("tssm: '%s' is not a slot (A1..A3, B1..B3, ... or all)\n", slot);
        return 2;
    }

    if (!iSaveStoreExportPS2(target, game, p, dest, &where, err, sizeof(err)))
    {
        printf("tssm: export failed: %s\n", err);
        return 1;
    }

    printf("tssm: slot %s exported to %s as a PlayStation 2 save%s (the icon, if the folder is "
           "new: %s)\n",
           slot, where.c_str(), iSC_psvWords(where, p),
           iconFrom.empty() ? "the port's own" : iconFrom.c_str());
    return 0;
}

static S32 iSC_export(const char* slot, const char* dest, const iSaveExportOptions* o)
{
    char err[512] = "";

    if (iHostStrCaseCmp(slot, "all") == 0)
    {
        if (iSC_endsWith(dest, ".zip"))
        {
            S32 n = iSaveStoreExportZip(o, dest, err, sizeof(err));
            if (n < 0)
            {
                printf("tssm: export failed: %s\n", err);
                return 1;
            }
            printf("tssm: %d save%s exported to %s\n", (int)n, n == 1 ? "" : "s", dest);
            return 0;
        }

        // Into a folder, the way Dolphin keeps a card's files (iSaveStore.h).
        std::vector<iSaveExportItem> items;
        S32 count = iSaveStoreExportFolder(o, dest, items);
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].ok)
            {
                printf("  %s -> %s\n", items[i].name, items[i].path.c_str());
            }
            else
            {
                printf("  %s: %s\n", items[i].name, items[i].message.c_str());
            }
        }

        if (count == 0)
        {
            printf("tssm: nothing exported\n");
            return 1;
        }
        return 0;
    }

    S32 target;
    S32 game;
    if (!iSaveStoreParseSlot(slot, &target, &game))
    {
        printf("tssm: '%s' is not a slot (A1..A3, B1..B3, ... or all)\n", slot);
        return 2;
    }

    if (iSC_endsWith(dest, ".zip"))
    {
        printf("tssm: a zip holds every save: --export-save all %s\n", dest);
        return 2;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s", dest);

    std::vector<U8> gci;
    if (!iSaveStoreExportGCIData(target, game, o, gci, err, sizeof(err)))
    {
        printf("tssm: export failed: %s\n", err);
        return 1;
    }

    if (iSC_isDir(dest))
    {
        iSaveGCI parsed;
        iSaveGCIParse(&gci[0], gci.size(), &parsed, NULL, 0);
        char name[64];
        iSaveGCIDolphinName(parsed.gamecode, game, name, sizeof(name));
        snprintf(path, sizeof(path), "%s/%s", dest, name);
    }

    if (!iSaveStoreWriteFile(path, &gci[0], gci.size(), false, err, sizeof(err)))
    {
        printf("tssm: export failed: %s\n", err);
        return 1;
    }

    printf("tssm: slot %s exported to %s\n", slot, path);
    return 0;
}

static bool iSC_isCommand(const char* a)
{
    static const char* commands[] = { "--saves",      "--import-save",  "--export-save",
                                      "--check-save", "--restore-save", "--delete-save",
                                      "--saves-help" };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    {
        if (strcmp(a, commands[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

S32 iSaveCLIRunArgs(S32 argc, char** argv)
{
    const char* command = NULL;
    const char* operand[2] = { NULL, NULL };
    S32 operands = 0;

    // --save-dir counts for the game too: it is where a game started with it
    // saves.
    for (S32 i = 1; i < argc; i++)
    {
        if (iSC_isCommand(argv[i]))
        {
            if (command != NULL)
            {
                printf("tssm: one save command at a time (%s and %s)\n", command, argv[i]);
                return 2;
            }
            command = argv[i];
        }
        else if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc)
        {
            iSaveStoreSetRoot(argv[++i]);
        }
    }

    iSaveImportOptions imp;
    iSaveImportDefaults(&imp);
    iSaveExportOptions exp;
    iSaveExportDefaults(&exp);
    iSaveXboxExportOptions xbox;
    iSaveXboxExportDefaults(&xbox);
    bool toXbox = false;
    iSavePS2ExportOptions ps2;
    iSavePS2ExportDefaults(&ps2);
    bool toPS2 = false;
    S32 ps2Kind = -1; // what --ps2 said
    bool toPsv = false; // --psv was given
    bool all = false;

    for (S32 i = 1; command != NULL && i < argc; i++)
    {
        const char* a = argv[i];

        if (a == command)
        {
            continue;
        }
        else if (strcmp(a, "--save-dir") == 0 && i + 1 < argc)
        {
            i++;
        }
        else if (strcmp(a, "--slot") == 0 && i + 1 < argc)
        {
            const char* s = argv[++i];
            if (((s[0] >= 'A' && s[0] < 'A' + ISS_MAX_TARGETS) ||
                 (s[0] >= 'a' && s[0] < 'a' + ISS_MAX_TARGETS)) &&
                s[1] == '\0')
            {
                imp.target = (s[0] >= 'a') ? s[0] - 'a' : s[0] - 'A';
                imp.game = -1;
            }
            else if (!iSaveStoreParseSlot(s, &imp.target, &imp.game))
            {
                printf("tssm: '%s' is not a slot (A1..A3, B1..B3, ...)\n", s);
                return 2;
            }
        }
        else if (strcmp(a, "--pick") == 0 && i + 1 < argc)
        {
            imp.pick = atoi(argv[++i]);
            if (imp.pick < 1)
            {
                printf("tssm: --pick counts from 1\n");
                return 2;
            }
        }
        else if (strcmp(a, "--region") == 0 && i + 1 < argc)
        {
            if (!iSC_region(argv[++i], exp.gamecode))
            {
                printf("tssm: --region takes E, P or a game code (GGVE, GGVP)\n");
                return 2;
            }
        }
        else if (strcmp(a, "--xbox") == 0 && i + 1 < argc)
        {
            const char* kind = argv[++i];
            if (iHostStrCaseCmp(kind, "emu") == 0 || iHostStrCaseCmp(kind, "emulator") == 0)
            {
                xbox.signature = ISS_XBOX_EMULATOR;
            }
            else if (iHostStrCaseCmp(kind, "real") == 0)
            {
                xbox.signature = ISS_XBOX_REAL;
            }
            else
            {
                printf("tssm: --xbox takes emu (Cxbx-Reloaded, other copies of this port) or "
                       "real (a real Xbox or xemu; needs --keys)\n");
                return 2;
            }
            toXbox = true;
        }
        else if (strcmp(a, "--ps2") == 0 && i + 1 < argc)
        {
            const char* kind = argv[++i];
            if (iHostStrCaseCmp(kind, "auto") == 0)
            {
                ps2.kind = ISS_PS2_AUTO;
            }
            else if (iHostStrCaseCmp(kind, "psu") == 0)
            {
                ps2.kind = ISS_PS2_PSU;
            }
            else if (iHostStrCaseCmp(kind, "folder") == 0)
            {
                ps2.kind = ISS_PS2_FOLDER_CARD;
            }
            else if (iHostStrCaseCmp(kind, "card") == 0)
            {
                ps2.kind = ISS_PS2_CARD;
            }
            else if (iHostStrCaseCmp(kind, "psv") == 0)
            {
                ps2.kind = ISS_PS2_PSV;
            }
            else
            {
                printf("tssm: --ps2 takes psu (a .psu file), psv (a PlayStation 3 export), folder "
                       "(PCSX2's folder-type card), card (a memory card file, .ps2 or .VM2) or "
                       "auto (by what the destination is)\n");
                return 2;
            }
            ps2Kind = ps2.kind;
            toPS2 = true;
        }
        else if (strcmp(a, "--psv") == 0 && i + 1 < argc)
        {
            const char* kind = argv[++i];
            if (iHostStrCaseCmp(kind, "standin") == 0 || iHostStrCaseCmp(kind, "stand-in") == 0)
            {
                ps2.psvSignature = ISS_PSV_STANDIN;
            }
            else if (iHostStrCaseCmp(kind, "real") == 0)
            {
                ps2.psvSignature = ISS_PSV_REAL;
            }
            else
            {
                printf("tssm: --psv takes standin (other copies of this port, PC save tools; a "
                       "PS3 needs it re-signed) or real (the PS3's own signature; needs "
                       "--psv-keys)\n");
                return 2;
            }
            toPsv = true;
            toPS2 = true;
        }
        else if (strcmp(a, "--psv-keys") == 0 && i + 1 < argc)
        {
            snprintf(ps2.psvKeys, sizeof(ps2.psvKeys), "%s", argv[++i]);
        }
        else if (strcmp(a, "--ps2-icon") == 0 && i + 1 < argc)
        {
            snprintf(ps2.icon, sizeof(ps2.icon), "%s", argv[++i]);
        }
        else if (strcmp(a, "--pal") == 0)
        {
            ps2.pal = true;
        }
        else if (strcmp(a, "--xbe") == 0 && i + 1 < argc)
        {
            snprintf(xbox.xbe, sizeof(xbox.xbe), "%s", argv[++i]);
        }
        else if (strcmp(a, "--keys") == 0 && i + 1 < argc)
        {
            snprintf(xbox.keys, sizeof(xbox.keys), "%s", argv[++i]);
        }
        else if (strcmp(a, "--as") == 0 && i + 1 < argc)
        {
            xbox.number = atoi(argv[++i]) - 1;
            ps2.number = xbox.number;
            if (xbox.number < 0 || xbox.number >= ISS_GAMES)
            {
                printf("tssm: --as takes 1, 2 or 3: which of the console's three saves it "
                       "becomes\n");
                return 2;
            }
        }
        else if (strcmp(a, "--replace") == 0)
        {
            xbox.replace = true;
            ps2.replace = true;
        }
        else if (strcmp(a, "--force") == 0)
        {
            imp.force = true;
        }
        else if (strcmp(a, "--dry-run") == 0)
        {
            imp.dryRun = true;
        }
        else if (strcmp(a, "--all") == 0)
        {
            all = true;
        }
        else if ((a[0] != '-' || a[1] == '\0') && operands < 2)
        {
            operand[operands++] = a;
        }
        else
        {
            printf("tssm: %s: '%s' is not understood (--saves-help)\n", command, a);
            return 2;
        }
    }

    if (command == NULL)
    {
        return -1;
    }

    // config.ini's save folder, which iSystem would push later on.
    iSaveStoreSetConfigRoot(iConfigGetString("game.save_folder", ""));

    // The commands that change the saves take the save folder's claim first, as
    // the game does when it starts (iSystem.cpp, ClaimSaveFolder). A game
    // running on the same folder holds it: the slot changed behind its back
    // would be saved over at its next autosave, or a save the player makes
    // there would land in a slot that was just emptied. Listing, exporting and
    // checking only read, and run beside the game.
    if ((strcmp(command, "--import-save") == 0 && !imp.dryRun) ||
        strcmp(command, "--restore-save") == 0 || strcmp(command, "--delete-save") == 0)
    {
        U32 holder = 0;
        if (iSaveStoreClaimFolder(&holder) == 0)
        {
            printf("tssm: the game is running (process %u) with the saves in %s; close it and "
                   "run %s again\n",
                   (unsigned)holder, iSaveStoreRoot(), command);
            return 1;
        }
    }

    char err[512] = "";

    if (strcmp(command, "--saves-help") == 0)
    {
        iSC_usage();
        return 0;
    }

    if (strcmp(command, "--saves") == 0)
    {
        return iSC_list(all);
    }

    if (strcmp(command, "--import-save") == 0 || strcmp(command, "--check-save") == 0)
    {
        if (operands != 1)
        {
            printf("tssm: %s takes one file or folder (--saves-help)\n", command);
            return 2;
        }

        bool check = strcmp(command, "--check-save") == 0;
        if (check)
        {
            imp.dryRun = true;
            imp.force = true;
        }

        // --psv-keys the same, for a .psv.
        if (ps2.psvKeys[0] != '\0')
        {
            char found[600];
            char why[768] = "";
            imp.psvKeys = ps2.psvKeys;
            if (!iSaveStorePsvKeysUsable(ps2.psvKeys, found, sizeof(found), why, sizeof(why)))
            {
                printf("tssm: --psv-keys: %s; a .psv's signature is not checked with it\n", why);
            }
        }

        // --xbe and --keys here are for saying which signature an Xbox save
        // carries. The save is imported whatever they are; a file that cannot
        // serve is said, not passed over.
        if (xbox.xbe[0] != '\0' || xbox.keys[0] != '\0')
        {
            char found[600];
            char why[768] = "";
            imp.xboxXbe = xbox.xbe;
            imp.xboxKeys = xbox.keys;
            if (xbox.xbe[0] != '\0' &&
                !iSaveStoreXboxProgramUsable(xbox.xbe, found, sizeof(found), why, sizeof(why)))
            {
                printf("tssm: --xbe: %s; an Xbox save's signature is not checked with it\n", why);
            }
            if (xbox.keys[0] != '\0' &&
                !iSaveStoreXboxKeysUsable(xbox.keys, found, sizeof(found), why, sizeof(why)))
            {
                printf("tssm: --keys: %s; an Xbox save's signature is not checked with it\n",
                       why);
            }
        }

        std::vector<iSaveImportItem> items;
        bool ok = iSaveStoreImport(operand[0], &imp, items, err, sizeof(err));

        if (items.empty())
        {
            printf("tssm: %s\n", err);
            return 1;
        }

        printf("%s:\n", operand[0]);
        if (check)
        {
            for (size_t i = 0; i < items.size(); i++)
            {
                printf("  %d. %s: %s\n", (int)i + 1, items[i].source.c_str(),
                       items[i].message.c_str());
            }
            return ok ? 0 : 1;
        }

        iSC_printItems(items);
        if (!ok && err[0] != '\0')
        {
            printf("tssm: %s\n", err);
        }
        return ok ? 0 : 1;
    }

    if (strcmp(command, "--export-save") == 0)
    {
        if (operands != 2)
        {
            printf(
                "tssm: --export-save takes a slot (or all) and a file or folder (--saves-help)\n");
            return 2;
        }
        if (toXbox && toPS2)
        {
            printf("tssm: --xbox or --ps2, one at a time\n");
            return 2;
        }
        if (toPsv)
        {
            if (ps2Kind >= 0 && ps2Kind != ISS_PS2_PSV && ps2Kind != ISS_PS2_AUTO)
            {
                printf("tssm: --psv writes a .psv; it does not go with that --ps2\n");
                return 2;
            }
            ps2.kind = ISS_PS2_PSV;
        }
        if (ps2.psvKeys[0] != '\0' && !(toPsv && ps2.psvSignature == ISS_PSV_REAL))
        {
            printf("tssm: --psv-keys goes with --psv real (the stand-in signature needs no key "
                   "file)\n");
            return 2;
        }
        if (xbox.number >= 0 && iHostStrCaseCmp(operand[0], "all") == 0)
        {
            printf("tssm: --as goes with one slot; all writes each save under its own number\n");
            return 2;
        }
        if (toXbox)
        {
            return iSC_exportXbox(operand[0], operand[1], &xbox, imp.target);
        }
        if (toPS2)
        {
            return iSC_exportPS2(operand[0], operand[1], &ps2, imp.target);
        }
        return iSC_export(operand[0], operand[1], &exp);
    }

    S32 target;
    S32 game;
    if (operands < 1 || !iSaveStoreParseSlot(operand[0], &target, &game))
    {
        printf("tssm: %s takes a slot: A1..A3, B1..B3, ... (--saves-help)\n", command);
        return 2;
    }

    if (strcmp(command, "--restore-save") == 0)
    {
        S32 which = operands > 1 ? atoi(operand[1]) : 1;

        if (operands < 2)
        {
            std::vector<iSaveBackupInfo> backups;
            iSaveStoreBackups(target, game, backups);
            for (size_t i = 0; i < backups.size(); i++)
            {
                char date[32];
                iSC_date(backups[i].mtime, date, sizeof(date));
                printf("  backup %d: %s, %d%%, %s%s\n", (int)backups[i].which, backups[i].label,
                       (int)backups[i].progress, date, backups[i].valid ? "" : " (damaged)");
            }
        }

        if (!iSaveStoreRestore(target, game, which, err, sizeof(err)))
        {
            printf("tssm: %s\n", err);
            return 1;
        }
        printf("tssm: backup %d is back in slot %s; what was there is now backup 1\n", (int)which,
               operand[0]);
        return 0;
    }

    if (strcmp(command, "--delete-save") == 0)
    {
        if (!iSaveStoreDelete(target, game, err, sizeof(err)))
        {
            printf("tssm: %s\n", err);
            return 1;
        }
        printf("tssm: slot %s is empty; --restore-save %s brings the save back\n", operand[0],
               operand[0]);
        return 0;
    }

    return 2;
}

S32 iSaveCLIRun()
{
#ifdef __3DS__
    // The 3DS starts the game with no command line of the player's, so no save
    // command reaches it, and the commands' code (with the export to other
    // consoles it calls) stays out of its memory.
    return -1;
#endif
    S32 argc = 0;
    char** argv = NULL;
    if (!iHostCommandLine(&argc, &argv) || argv == NULL)
    {
        return -1;
    }
    return iSaveCLIRunArgs(argc, argv);
}
