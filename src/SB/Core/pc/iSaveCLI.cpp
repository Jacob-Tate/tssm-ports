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
    printf("The SpongeBob SquarePants Movie (PC) -- save tools\n"
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
           "  tssm --check-save FILE                 say what FILE holds; import nothing\n"
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
            printf("tssm: %s takes one file (--saves-help)\n", command);
            return 2;
        }

        bool check = strcmp(command, "--check-save") == 0;
        if (check)
        {
            imp.dryRun = true;
            imp.force = true;
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
    S32 argc = 0;
    char** argv = NULL;
    if (!iHostCommandLine(&argc, &argv) || argv == NULL)
    {
        return -1;
    }
    return iSaveCLIRunArgs(argc, argv);
}
