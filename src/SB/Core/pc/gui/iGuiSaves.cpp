// The Saves tab: the save store (iSaveStore.h) on screen.
//
// Everything here is a call into the store, which is also what tssm.exe's
// --saves / --import-save / --export-save command line calls, so a save
// imported here and one imported from the command line are the same thing on
// disk. The tab lists the slots (level, progress, when, backups), imports
// GameCube saves and card images and other installs' saves through the
// system's file dialog, exports a slot as a .gci, every save as a zip or into
// a Dolphin GCI folder, restores a backup and deletes (which is itself
// undoable: the save becomes backup 1).
//
// Xbox saves go both ways too (docs/SAVES.md section 7). One is a folder, so
// importing picks a folder (or a zip of one) and exporting writes one. An
// exported save has to be signed, and the tab offers the two signatures there
// are: for Cxbx-Reloaded and other copies of this port, which needs only the
// player's DEFAULT.XBE; or for a real Xbox or xemu, which also needs the
// keys.bin made from the player's own Xbox. Both files are picked, never typed,
// and their paths kept in config.ini ([game] xbox_xbe, xbox_keys). The game
// holds no key and shows none.
//
// PlayStation 2 saves too (docs/SAVES.md section 8): Import takes a .psu, .max
// or .psv file or a memory card file, and the folder import takes PCSX2's
// folder-type card; "Export to a PlayStation 2" writes a .psu, or into a card
// file (which is copied first) or a folder card. The icon is the game's own
// when the player points at their PlayStation 2 disc's PLAT.HIP or a save the
// game made ([game] ps2_icon), else the port's.
//
// A PlayStation 3's .psv too (docs/SAVES.md section 9), in the Xbox export's
// shape: "Export to a PlayStation 3" signs it with the stand-in signature
// (other copies of this port and PC save tools; a PS3 needs it re-signed) or
// with the PS3's own, made from a key file the player picks ([game]
// psv_keys), which is not yet confirmed on a PS3. A PS3's virtual memory card
// (.VM2) is a memory card file to the PlayStation 2 export.
//
// The game in progress keeps writing to the slot it last saved to or loaded
// from -- its autosave. Importing or restoring over that slot is allowed, but
// only after saying that the next autosave will write over it again.

#include "imgui.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iHost.h"
#include "iSaveFormat.h"
#include "iSavePS2.h"
#include "iSaveStore.h"

// Export to an Xbox, a PlayStation 2 or a PlayStation 3 (docs/SAVES.md
// sections 7 to 9) is left out of the phone: its folders need a folder picker,
// which SDL does not have on Android (SDL_androiddialog.c), and its files are
// written by plain path (iSaveStore.cpp's <path>.tmp), which the picker's
// content:// address is not. A PC with the same saves does it. Importing those
// saves from a file or a .zip stays.
#ifdef __ANDROID__
#define IGS_EXPORT_OTHER_CONSOLES 0
#else
#define IGS_EXPORT_OTHER_CONSOLES 1
#endif

#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{
    const ImVec4 kRed(1.0f, 0.47f, 0.43f, 1.0f);
    const ImVec4 kYellow(0.98f, 0.82f, 0.31f, 1.0f);
    const ImVec4 kGreen(0.55f, 0.85f, 0.55f, 1.0f);

    const iGui::FileFilter kImportFilters[] = {
        { "GameCube, PlayStation 2 and Xbox saves, memory cards and save zips",
          "gci;gcs;raw;gcp;zip;xsv;psu;max;psv;ps2;vm2" },
        { "All files", "*" },
    };
    const iGui::FileFilter kGciFilter[] = { { "GameCube save (.gci)", "gci" } };
    const iGui::FileFilter kZipFilter[] = { { "Zip of saves", "zip" } };
    const iGui::FileFilter kXbeFilter[] = { { "Xbox program (DEFAULT.XBE)", "xbe" },
                                            { "All files", "*" } };
    const iGui::FileFilter kKeysFilter[] = { { "Xbox keys file (keys.bin)", "bin" },
                                             { "All files", "*" } };
    const iGui::FileFilter kPsuFilter[] = { { "PlayStation 2 save (.psu)", "psu" } };
    const iGui::FileFilter kCardFilter[] = { { "PlayStation 2 memory card (.ps2, or a PS3's .VM2)",
                                               "ps2;vm2" },
                                             { "All files", "*" } };
    const iGui::FileFilter kPsvFilter[] = { { "PlayStation 3 export (.psv)", "psv" } };
    const iGui::FileFilter kPsvKeysFilter[] = { { "PS3 key file (32 bytes)", "bin" },
                                                { "All files", "*" } };
    const iGui::FileFilter kPs2IconFilter[] = {
        { "PLAT.HIP, or a PlayStation 2 save", "hip;psu;max;psv;ps2;ico" }, { "All files", "*" }
    };

    // --- the listing ---------------------------------------------------------------

    // Read again after anything that changes it, and every two seconds while
    // the tab is shown: the game saves while the overlay is open too.
    std::vector<iSaveSlotInfo> sSlots;
    U64 sListedAt;
    bool sStale = true;
    bool sAllFolders;

    void Refresh()
    {
        const U64 now = SDL_GetTicks();
        if (sStale || now - sListedAt > 2000)
        {
            iSaveStoreList(sSlots, sAllFolders);
            sListedAt = now;
            sStale = false;
        }
    }

    // --- a line saying what the last action did ------------------------------------

    std::string sStatus;
    bool sStatusBad;

    void Say(bool bad, const char* fmt, ...)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        sStatus = buf;
        sStatusBad = bad;
        printf("tssm: gui: saves: %s\n", buf);
        fflush(stdout);
    }

    // The store's sentences name the command line's switches; here there is
    // a check box or a combo for each instead.
    std::string Clean(const std::string& message)
    {
        std::string s = message;
        static const char* const kCut[] = { "; add --force to replace it (it is kept as a backup)",
                                            "; name one with --slot" };
        for (size_t i = 0; i < sizeof(kCut) / sizeof(kCut[0]); i++)
        {
            size_t at = s.find(kCut[i]);
            if (at != std::string::npos)
            {
                s.erase(at, strlen(kCut[i]));
            }
        }
        return s;
    }

    // --- small things ------------------------------------------------------------------

    void SlotName(S32 target, S32 game, char* out, size_t size)
    {
        iSaveStoreSlotName(target, game, out, size);
    }

    bool IsAutoSaveSlot(S32 target, S32 game)
    {
        S32 t;
        S32 g;
        return iSaveStoreAutoSaveSlot(&t, &g) && t == target && g == game;
    }

    void When(S64 unixSeconds, char* out, size_t size)
    {
        if (unixSeconds <= 0)
        {
            snprintf(out, size, "-");
            return;
        }
        iHostCalendar c;
        iHostLocalTimeOf(unixSeconds, &c);
        snprintf(out, size, "%04d-%02d-%02d %02d:%02d", (int)c.year + 1900, (int)c.mon + 1,
                 (int)c.mday, (int)c.hour, (int)c.min);
    }

    // A save's level name as the game wrote it. A save made before the first
    // level (New Game's "create a save?") carries "????".
    const char* Level(const char* label)
    {
        if (label == NULL || label[0] == '\0' || strncmp(label, "????", 4) == 0)
        {
            return "New game";
        }
        return label;
    }

    const char* BaseName(const char* path)
    {
        const char* base = path;
        for (const char* c = path; *c != '\0'; c++)
        {
            if (*c == '/' || *c == '\\')
            {
                base = c + 1;
            }
        }
        return base;
    }

    // A file name from a save dialog, with `ext` added when it has none.
    //
    // An answer that is a URI (Android's content://) is kept as it is. Its last
    // part is the storage app's id for the document the dialog has just made,
    // not a file name, and the game may write that document and no other:
    // "<id>.zip" is a document nobody made, and the one made stays empty.
    std::string WithExtension(const char* path, const char* ext)
    {
        std::string s = path;
        const char* base = BaseName(path);
        if (strstr(path, "://") == NULL && strchr(base, '.') == NULL)
        {
            s += ext;
        }
        return s;
    }

    // --- export ----------------------------------------------------------------------

    // 0: as the save says (North America for one made here), 1: North
    // America, 2: Europe.
    int sRegion;

    void ExportOptions(iSaveExportOptions* o)
    {
        iSaveExportDefaults(o);
        if (sRegion == 1)
        {
            snprintf(o->gamecode, sizeof(o->gamecode), "GGVE");
        }
        else if (sRegion == 2)
        {
            snprintf(o->gamecode, sizeof(o->gamecode), "GGVP");
        }
    }

    S32 sExportTarget = -1;
    S32 sExportGame = -1;

    void StartExportGCI(const iSaveSlotInfo& slot)
    {
        iSaveExportOptions o;
        ExportOptions(&o);

        // The name Dolphin would give it, from the .gci it will be.
        std::vector<U8> gci;
        char err[256];
        char name[64] = "SpongeBobMovie.gci";
        if (iSaveStoreExportGCIData(slot.target, slot.game, &o, gci, err, sizeof(err)))
        {
            iSaveGCI parsed;
            if (iSaveGCIParse(&gci[0], gci.size(), &parsed, NULL, 0))
            {
                iSaveGCIDolphinName(parsed.gamecode, slot.game, name, sizeof(name));
            }
        }

        sExportTarget = slot.target;
        sExportGame = slot.game;
        iGui::PickSaveFile("save-export-gci", name, kGciFilter, 1);
    }

    void FinishExportGCI(const char* picked)
    {
        if (sExportTarget < 0)
        {
            return;
        }
        std::string path = WithExtension(picked, ".gci");

        iSaveExportOptions o;
        ExportOptions(&o);
        std::vector<U8> gci;
        char err[512] = "";
        char slot[4];
        SlotName(sExportTarget, sExportGame, slot, sizeof(slot));
        if (!iSaveStoreExportGCIData(sExportTarget, sExportGame, &o, gci, err, sizeof(err)))
        {
            Say(true, "%s was not exported: %s", slot, err);
        }
        else if (!iGui::WriteWholeFile(path.c_str(), &gci[0], gci.size(), err, sizeof(err)))
        {
            Say(true, "%s", err);
        }
        else
        {
            Say(false, "%s exported to %s (%u bytes).", slot, path.c_str(), (unsigned)gci.size());
        }
        sExportTarget = -1;
    }

    void FinishExportZip(const char* picked)
    {
        std::string path = WithExtension(picked, ".zip");
        iSaveExportOptions o;
        ExportOptions(&o);
        std::vector<U8> zip;
        char err[512] = "";
        const S32 n = iSaveStoreExportZipData(&o, zip, err, sizeof(err));
        if (n < 0)
        {
            Say(true, "Nothing exported: %s", err);
        }
        else if (!iGui::WriteWholeFile(path.c_str(), &zip[0], zip.size(), err, sizeof(err)))
        {
            Say(true, "%s", err);
        }
        else
        {
            Say(false, "%d save%s exported to %s.", (int)n, n == 1 ? "" : "s", path.c_str());
        }
    }

    void FinishExportFolder(const char* dir)
    {
        iSaveExportOptions o;
        ExportOptions(&o);
        std::vector<iSaveExportItem> items;
        const S32 n = iSaveStoreExportFolder(&o, dir, items);

        std::string skipped;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (!items[i].ok)
            {
                skipped += std::string(skipped.empty() ? "" : "; ") + items[i].name + ": " +
                           items[i].message;
            }
        }

        if (n <= 0 && items.empty())
        {
            Say(true, "There are no saves to export.");
        }
        else if (skipped.empty())
        {
            Say(false, "%d save%s exported to %s as Dolphin names them.", (int)n,
                n == 1 ? "" : "s", dir);
        }
        else
        {
            Say(n <= 0, "%d save%s exported to %s; not exported: %s", (int)n, n == 1 ? "" : "s",
                dir, skipped.c_str());
        }
    }

    // --- export to an Xbox -------------------------------------------------------------

    // 0: Cxbx-Reloaded and other copies of this port, 1: a real Xbox or xemu.
    int sXboxFor;
    bool sXboxReplace;
    int sXboxFolder; // which save folder "every save" takes: 0 is A
    S32 sXboxTarget = -1; // the one slot being exported, or -1 for a folder's saves
    S32 sXboxGame = -1;

    void XboxOptions(iSaveXboxExportOptions* o)
    {
        iSaveXboxExportDefaults(o);
        o->signature = (sXboxFor == 1) ? ISS_XBOX_REAL : ISS_XBOX_EMULATOR;
        o->replace = sXboxReplace;
    }

    const char* XboxForWhat()
    {
        return sXboxFor == 1 ? "a real Xbox or xemu" :
                               "Cxbx-Reloaded and other copies of this port";
    }

    // Where the two files an Xbox export reads would come from, looked up
    // once a second while the tab is shown (and at once after one is chosen),
    // not every frame: each look is a few file-system calls.
    struct XboxFiles
    {
        U64 at;
        bool valid;
        char xbe[600]; // "" when there is none
        bool xbeBad; // there, but not this game's Xbox program
        char xbeWhy[768];
        char keys[600];
        bool keysBad; // there, but not a keys.bin
        char keysWhy[768];
    };
    XboxFiles sXboxFiles;

    // Whether a file found is one an export can use, kept until the file
    // changes: the program is a few megabytes, too much to read every second.
    struct XboxChecked
    {
        char path[600];
        U64 size;
        S64 mtime;
        bool ok;
        char why[768];
    };
    XboxChecked sXbeChecked;
    XboxChecked sKeysChecked;

    typedef bool (*XboxUsable)(const char*, char*, size_t, char*, size_t);

    void XboxCheck(XboxChecked& c, const char* path, XboxUsable usable, bool* bad, char* why,
                   size_t whySize)
    {
        iHostFileInfo st;
        memset(&st, 0, sizeof(st));
        if (!iHostStat(path, &st))
        {
            st.size = 0;
            st.mtime = 0;
        }

        if (strcmp(c.path, path) != 0 || c.size != st.size || c.mtime != st.mtime)
        {
            char found[600];
            snprintf(c.path, sizeof(c.path), "%s", path);
            c.size = st.size;
            c.mtime = st.mtime;
            c.why[0] = '\0';
            c.ok = usable(path, found, sizeof(found), c.why, sizeof(c.why));
        }

        *bad = !c.ok;
        if (!c.ok)
        {
            snprintf(why, whySize, "%s", c.why);
        }
    }

    const XboxFiles& XboxLook()
    {
        const U64 now = SDL_GetTicks();
        if (!sXboxFiles.valid || now - sXboxFiles.at > 1000)
        {
            sXboxFiles.at = now;
            sXboxFiles.valid = true;
            sXboxFiles.xbeWhy[0] = '\0';
            sXboxFiles.keysWhy[0] = '\0';
            sXboxFiles.xbeBad = false;
            sXboxFiles.keysBad = false;
            if (!iSaveStoreXboxProgram(NULL, sXboxFiles.xbe, sizeof(sXboxFiles.xbe),
                                       sXboxFiles.xbeWhy, sizeof(sXboxFiles.xbeWhy)))
            {
                sXboxFiles.xbe[0] = '\0';
            }
            else
            {
                XboxCheck(sXbeChecked, sXboxFiles.xbe, iSaveStoreXboxProgramUsable,
                          &sXboxFiles.xbeBad, sXboxFiles.xbeWhy, sizeof(sXboxFiles.xbeWhy));
            }
            if (!iSaveStoreXboxKeys(NULL, sXboxFiles.keys, sizeof(sXboxFiles.keys),
                                    sXboxFiles.keysWhy, sizeof(sXboxFiles.keysWhy)))
            {
                sXboxFiles.keys[0] = '\0';
            }
            else
            {
                XboxCheck(sKeysChecked, sXboxFiles.keys, iSaveStoreXboxKeysUsable,
                          &sXboxFiles.keysBad, sXboxFiles.keysWhy, sizeof(sXboxFiles.keysWhy));
            }
        }
        return sXboxFiles;
    }

    // Whether the files the chosen signature needs are there and are what
    // they should be; `why` says what is missing or wrong.
    bool XboxReady(char* why, size_t whySize)
    {
        const XboxFiles& f = XboxLook();
        if (f.xbe[0] == '\0' || f.xbeBad)
        {
            snprintf(why, whySize, "%s", f.xbeWhy);
            return false;
        }
        if (sXboxFor == 1 && (f.keys[0] == '\0' || f.keysBad))
        {
            snprintf(why, whySize, "%s", f.keysWhy);
            return false;
        }
        return true;
    }

    void FinishExportXbox(const char* dir)
    {
        iSaveXboxExportOptions o;
        XboxOptions(&o);
        char err[768] = "";

        if (sXboxTarget >= 0)
        {
            char slot[4];
            SlotName(sXboxTarget, sXboxGame, slot, sizeof(slot));
            std::string where;
            if (iSaveStoreExportXbox(sXboxTarget, sXboxGame, &o, dir, &where, err, sizeof(err)))
            {
                Say(false, "%s exported as an Xbox save for %s: %s", slot, XboxForWhat(),
                    where.c_str());
            }
            else
            {
                Say(true, "%s was not exported: %s", slot, err);
            }
            sXboxTarget = -1;
            return;
        }

        std::vector<iSaveExportItem> items;
        const S32 n = iSaveStoreExportXboxFolder(sXboxFolder, &o, dir, items);
        std::string skipped;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (!items[i].ok)
            {
                skipped += std::string(skipped.empty() ? "" : "; ") + items[i].name + ": " +
                           items[i].message;
            }
        }

        if (items.empty())
        {
            Say(true, "Folder %c has no saves to export.", 'A' + sXboxFolder);
        }
        else if (skipped.empty())
        {
            Say(false, "%d save%s exported to %s as Xbox saves for %s.", (int)n, n == 1 ? "" : "s",
                dir, XboxForWhat());
        }
        else
        {
            Say(n <= 0, "%d save%s exported to %s; not exported: %s", (int)n, n == 1 ? "" : "s",
                dir, skipped.c_str());
        }
    }

    // --- export to a PlayStation 2 -------------------------------------------------------

    // 0: a .psu file, 1: into a memory card file, 2: into PCSX2's folder card.
    int sPS2As;
    bool sPS2Replace;
    bool sPS2Pal;
    int sPS2Folder;
    S32 sPS2Target = -1; // the one slot being exported, or -1 for a folder's saves
    S32 sPS2Game = -1;

    // Which icon an export would write: looked up when the setting changes,
    // not every frame (the place named may be a whole memory card).
    struct PS2Icon
    {
        bool valid;
        std::string setting;
        std::string from; // "" for the port's own
        char why[512];
    };
    PS2Icon sPS2Icon;

    const PS2Icon& PS2IconLook()
    {
        const char* setting = iConfigGetString("game.ps2_icon", "");
        if (!sPS2Icon.valid || sPS2Icon.setting != setting)
        {
            sPS2Icon.valid = true;
            sPS2Icon.setting = setting;
            sPS2Icon.why[0] = '\0';
            std::vector<U8> icon;
            if (!iSaveStorePS2Icon(NULL, icon, &sPS2Icon.from, sPS2Icon.why, sizeof(sPS2Icon.why)))
            {
                sPS2Icon.from.clear();
            }
        }
        return sPS2Icon;
    }

    void StartExportPS2(S32 target, S32 game)
    {
        sPS2Target = target;
        sPS2Game = game;
        if (sPS2As == 0)
        {
            iGui::PickSaveFile("save-export-ps2-psu",
                               sPS2Pal ? "BESLES-52895SBMovie.psu" : "BASLUS-20904SBMovie.psu",
                               kPsuFilter, 1);
        }
        else if (sPS2As == 1)
        {
            iGui::PickOpenFile("save-export-ps2-card", NULL, kCardFilter, 2);
        }
        else
        {
            iGui::PickFolder("save-export-ps2-folder", NULL);
        }
    }

    void FinishExportPS2(const char* dest, S32 kind)
    {
        iSavePS2ExportOptions o;
        iSavePS2ExportDefaults(&o);
        o.kind = kind;
        o.pal = sPS2Pal;
        o.replace = sPS2Replace;

        char err[768] = "";
        std::string where;

        if (sPS2Target >= 0)
        {
            char slot[4];
            SlotName(sPS2Target, sPS2Game, slot, sizeof(slot));
            if (iSaveStoreExportPS2(sPS2Target, sPS2Game, &o, dest, &where, err, sizeof(err)))
            {
                Say(false, "%s exported as a PlayStation 2 save: %s", slot, where.c_str());
            }
            else
            {
                Say(true, "%s was not exported: %s", slot, err);
            }
            sPS2Target = -1;
            return;
        }

        std::vector<iSaveExportItem> items;
        const S32 n = iSaveStoreExportPS2Folder(sPS2Folder, &o, dest, items, &where, err,
                                                sizeof(err));
        std::string skipped;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (!items[i].ok)
            {
                skipped += std::string(skipped.empty() ? "" : "; ") + items[i].name + ": " +
                           items[i].message;
            }
        }

        if (items.empty())
        {
            Say(true, "Folder %c has no saves to export.", 'A' + sPS2Folder);
        }
        else if (skipped.empty())
        {
            Say(false, "%d save%s exported as PlayStation 2 saves: %s", (int)n, n == 1 ? "" : "s",
                where.c_str());
        }
        else
        {
            Say(true, "%d save%s exported; not exported: %s", (int)n, n == 1 ? "" : "s",
                skipped.c_str());
        }
    }

    // --- export to a PlayStation 3 ---------------------------------------------------------

    // 0: the stand-in signature, 1: the real one, from the player's key file.
    int sPsvFor;
    bool sPsvReplace;
    bool sPsvPal;
    int sPsvFolder;
    S32 sPsvTarget = -1; // the one slot being exported, or -1 for a folder's saves
    S32 sPsvGame = -1;

    const char* PsvForWhat()
    {
        return sPsvFor == 1 ? "the PS3's own signature from your key file (not yet confirmed on "
                              "a PS3)" :
                              "the stand-in signature (a PS3 needs it re-signed first)";
    }

    // Where the key file is and whether it serves, looked up once a second
    // as the Xbox files are.
    struct PsvFiles
    {
        U64 at;
        bool valid;
        char keys[600]; // "" when there is none
        bool keysBad;
        char keysWhy[768];
    };
    PsvFiles sPsvFiles;
    XboxChecked sPsvKeysChecked;

    const PsvFiles& PsvLook()
    {
        const U64 now = SDL_GetTicks();
        if (!sPsvFiles.valid || now - sPsvFiles.at > 1000)
        {
            sPsvFiles.at = now;
            sPsvFiles.valid = true;
            sPsvFiles.keysWhy[0] = '\0';
            sPsvFiles.keysBad = false;
            if (!iSaveStorePsvKeys(NULL, sPsvFiles.keys, sizeof(sPsvFiles.keys), sPsvFiles.keysWhy,
                                   sizeof(sPsvFiles.keysWhy)))
            {
                sPsvFiles.keys[0] = '\0';
            }
            else
            {
                XboxCheck(sPsvKeysChecked, sPsvFiles.keys, iSaveStorePsvKeysUsable,
                          &sPsvFiles.keysBad, sPsvFiles.keysWhy, sizeof(sPsvFiles.keysWhy));
            }
        }
        return sPsvFiles;
    }

    // Whether the chosen signature has what it needs; `why` says what not.
    bool PsvReady(char* why, size_t whySize)
    {
        const PS2Icon& icon = PS2IconLook();
        if (icon.why[0] != '\0')
        {
            snprintf(why, whySize, "%s", icon.why);
            return false;
        }
        if (sPsvFor == 1)
        {
            const PsvFiles& f = PsvLook();
            if (f.keys[0] == '\0' || f.keysBad)
            {
                snprintf(why, whySize, "%s", f.keysWhy);
                return false;
            }
        }
        return true;
    }

    void StartExportPsv(S32 target, S32 game)
    {
        sPsvTarget = target;
        sPsvGame = game;
        const std::string name = iSavePS2PsvName(sPsvPal ? IPS2_FOLDER_PAL : IPS2_FOLDER_NTSC);
        iGui::PickSaveFile("save-export-psv", name.c_str(), kPsvFilter, 1);
    }

    void FinishExportPsv(const char* dest)
    {
        iSavePS2ExportOptions o;
        iSavePS2ExportDefaults(&o);
        o.kind = ISS_PS2_PSV;
        o.pal = sPsvPal;
        o.replace = sPsvReplace;
        o.psvSignature = sPsvFor == 1 ? ISS_PSV_REAL : ISS_PSV_STANDIN;

        char err[768] = "";
        std::string where;

        if (sPsvTarget >= 0)
        {
            char slot[4];
            SlotName(sPsvTarget, sPsvGame, slot, sizeof(slot));
            if (iSaveStoreExportPS2(sPsvTarget, sPsvGame, &o, dest, &where, err, sizeof(err)))
            {
                Say(false, "%s exported as a PlayStation 3 save with %s: %s", slot, PsvForWhat(),
                    where.c_str());
            }
            else
            {
                Say(true, "%s was not exported: %s", slot, err);
            }
            sPsvTarget = -1;
            return;
        }

        std::vector<iSaveExportItem> items;
        const S32 n =
            iSaveStoreExportPS2Folder(sPsvFolder, &o, dest, items, &where, err, sizeof(err));
        std::string skipped;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (!items[i].ok)
            {
                skipped += std::string(skipped.empty() ? "" : "; ") + items[i].name + ": " +
                           items[i].message;
            }
        }

        if (items.empty())
        {
            Say(true, "Folder %c has no saves to export.", 'A' + sPsvFolder);
        }
        else if (skipped.empty())
        {
            Say(false, "%d save%s exported as a PlayStation 3 save with %s: %s", (int)n,
                n == 1 ? "" : "s", PsvForWhat(), where.c_str());
        }
        else
        {
            Say(true, "%d save%s exported; not exported: %s", (int)n, n == 1 ? "" : "s",
                skipped.c_str());
        }
    }

    // A file the Xbox export reads, as a row like the Settings tab's folders:
    // where it is, Browse... and Clear. `found` is where it would be read from
    // now ("" for nowhere).
    void XboxFileRow(const char* tag, const char* key, const char* found, const char* emptyText,
                     const iGui::FileFilter* filters, int filterCount)
    {
        const char* value = iConfigGetString(key, "");

        ImGui::PushID(tag);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttons = ImGui::CalcTextSize("Browse...").x + ImGui::CalcTextSize("Clear").x +
                              style.FramePadding.x * 4.0f + style.ItemSpacing.x * 2.0f;

        char shown[1024];
        snprintf(shown, sizeof(shown), "%s", found[0] != '\0' ? found : emptyText);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
        ImGui::BeginDisabled(found[0] == '\0');
        ImGui::InputText("##path", shown, sizeof(shown), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (found[0] != '\0' && ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted(found);
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(iGui::PickingFolder());
        if (ImGui::Button("Browse..."))
        {
            iGui::PickOpenFile(tag, value[0] != '\0' ? value : NULL, filters, filterCount);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(value[0] == '\0');
        if (ImGui::Button("Clear"))
        {
            iGui::Commit(key, "");
            sXboxFiles.valid = false;
            sPS2Icon.valid = false;
            sPsvFiles.valid = false;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    // --- import ----------------------------------------------------------------------

    struct ImportRow
    {
        std::string source;
        std::string label;
        S32 progress;
        bool usable; // the save itself is one the game loads
        std::string problem;
        bool use;
        int target;
        int game;
    };

    struct ImportState
    {
        std::string name; // the file's name, for the store's messages
        std::vector<unsigned char> data;
        std::string error; // the file as a whole
        std::vector<ImportRow> rows;
        bool replace;
        bool done;
        std::vector<std::string> results;
        std::vector<bool> resultOk;

        // Every slot, as it was when the dialog came up and after each import.
        std::vector<iSaveSlotInfo> slots;
    };

    ImportState sImport;
    bool sOpenImport;

    const iSaveSlotInfo* ImportSlot(int target, int game)
    {
        const size_t i = (size_t)target * ISS_GAMES + (size_t)game;
        return (target >= 0 && game >= 0 && i < sImport.slots.size()) ? &sImport.slots[i] : NULL;
    }

    // `folder`: an Xbox save's folder (or one holding several: the game's,
    // a UDATA, an emulator's data folder) or a PlayStation 2 one (PCSX2's
    // folder-type card, or the game's folder), whose saves are gathered into one
    // bundle; the same when the file picked is a save's GameData.xsv, so that
    // the SaveMeta.xbx beside it says which save it is.
    void StartImport(const char* path, bool folder = false)
    {
        sImport = ImportState();
        sImport.name = BaseName(path);
        iSaveStoreList(sImport.slots, true);

        std::string parent;
        if (!folder && iHostStrCaseCmp(BaseName(path), "GameData.xsv") == 0 &&
            BaseName(path) != path && strstr(path, "://") == NULL)
        {
            parent.assign(path, (size_t)(BaseName(path) - path) - 1);
        }

        char why[512] = "";
        std::vector<U8> bundle;
        if (folder)
        {
            if (iSaveStoreReadFolder(path, bundle, why, sizeof(why)))
            {
                sImport.data.assign(bundle.begin(), bundle.end());
            }
            else
            {
                sImport.error = why;
            }
        }
        else if (!parent.empty() &&
                 iSaveStoreReadFolder(parent.c_str(), bundle, NULL, 0, false))
        {
            sImport.data.assign(bundle.begin(), bundle.end());
        }
        else if (!iGui::ReadWholeFile(path, sImport.data, why, sizeof(why)))
        {
            sImport.error = why;
        }

        if (sImport.error.empty() && sImport.data.empty())
        {
            sImport.error = sImport.name + " is empty.";
        }
        else if (sImport.error.empty())
        {
            // Look only, as if everything may be replaced: where each save in
            // the file would go, and whether it is one the game loads.
            iSaveImportOptions o;
            iSaveImportDefaults(&o);
            o.dryRun = true;
            o.force = true;
            std::vector<iSaveImportItem> items;
            char err[512] = "";
            iSaveStoreImportData(&sImport.data[0], sImport.data.size(), sImport.name.c_str(), &o,
                                 items, err, sizeof(err));
            if (items.empty())
            {
                sImport.error = err[0] != '\0' ? err : "There is no save in that file.";
            }

            for (size_t i = 0; i < items.size(); i++)
            {
                const iSaveImportItem& it = items[i];
                ImportRow row;
                row.source = it.source;
                row.label = it.label;
                row.progress = it.progress;
                row.usable = it.ok;
                row.target = it.target >= 0 && it.target < ISS_MAX_TARGETS ? it.target : 0;
                row.game = it.game;

                if (!it.ok && it.game < 0 && it.message.find("which slot") != std::string::npos)
                {
                    // A save with nothing saying which slot it was (a bare
                    // file renamed): ask what it is, as if for slot 1, and
                    // offer the first empty slot.
                    iSaveImportOptions one = o;
                    one.pick = (S32)i + 1;
                    one.game = 0;
                    std::vector<iSaveImportItem> again;
                    iSaveStoreImportData(&sImport.data[0], sImport.data.size(),
                                         sImport.name.c_str(), &one, again, err, sizeof(err));
                    if (again.size() == 1 && again[0].ok)
                    {
                        row.usable = true;
                        row.label = again[0].label;
                        row.progress = again[0].progress;
                    }
                    row.game = 0;
                    for (int g = 0; g < ISS_GAMES; g++)
                    {
                        const iSaveSlotInfo* s = ImportSlot(row.target, g);
                        if (s != NULL && !s->exists)
                        {
                            row.game = g;
                            break;
                        }
                    }
                }

                if (!row.usable)
                {
                    std::string m = Clean(it.message);
                    const char* prefix = "not imported: ";
                    if (m.compare(0, strlen(prefix), prefix) == 0)
                    {
                        m.erase(0, strlen(prefix));
                    }
                    row.problem = m;
                }
                if (row.game < 0)
                {
                    row.game = 0;
                }
                row.use = row.usable;
                sImport.rows.push_back(row);
            }
        }
        sOpenImport = true;
    }

    void DoImport()
    {
        sImport.results.clear();
        sImport.resultOk.clear();

        S32 imported = 0;
        for (size_t i = 0; i < sImport.rows.size(); i++)
        {
            const ImportRow& row = sImport.rows[i];
            if (!row.use)
            {
                continue;
            }

            iSaveImportOptions o;
            iSaveImportDefaults(&o);
            o.target = row.target;
            o.game = row.game;
            o.pick = (S32)i + 1;
            o.force = sImport.replace;

            std::vector<iSaveImportItem> items;
            char err[512] = "";
            iSaveStoreImportData(&sImport.data[0], sImport.data.size(), sImport.name.c_str(), &o,
                                 items, err, sizeof(err));
            if (items.empty())
            {
                sImport.results.push_back(row.source + ": " + err);
                sImport.resultOk.push_back(false);
                continue;
            }
            for (size_t k = 0; k < items.size(); k++)
            {
                sImport.results.push_back(items[k].source + ": " + Clean(items[k].message));
                sImport.resultOk.push_back(items[k].ok);
                imported += items[k].ok ? 1 : 0;
            }
        }

        sImport.done = true;
        sStale = true;
        iSaveStoreList(sImport.slots, true);
        Say(imported == 0, "%d save%s imported from %s.", (int)imported, imported == 1 ? "" : "s",
            sImport.name.c_str());
    }

    // What the rows ask for, checked: duplicates, occupied slots, the
    // autosave slot.
    struct ImportCheck
    {
        S32 used;
        bool duplicate;
        bool occupied;
        bool autosave;
        char autosaveSlot[4];
    };

    ImportCheck CheckImport()
    {
        ImportCheck c;
        memset(&c, 0, sizeof(c));
        for (size_t i = 0; i < sImport.rows.size(); i++)
        {
            const ImportRow& r = sImport.rows[i];
            if (!r.use)
            {
                continue;
            }
            c.used++;
            for (size_t k = 0; k < i; k++)
            {
                const ImportRow& o = sImport.rows[k];
                if (o.use && o.target == r.target && o.game == r.game)
                {
                    c.duplicate = true;
                }
            }
            const iSaveSlotInfo* s = ImportSlot(r.target, r.game);
            if (s != NULL && s->exists)
            {
                c.occupied = true;
            }
            if (IsAutoSaveSlot(r.target, r.game))
            {
                c.autosave = true;
                SlotName(r.target, r.game, c.autosaveSlot, sizeof(c.autosaveSlot));
            }
        }
        return c;
    }

    void FolderCombo(const char* id, int* target)
    {
        const int folders = sAllFolders || *target >= 2 ? ISS_MAX_TARGETS : 2;
        char current[16];
        snprintf(current, sizeof(current), "%c", 'A' + *target);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
        if (ImGui::BeginCombo(id, current))
        {
            for (int t = 0; t < folders; t++)
            {
                char label[16];
                snprintf(label, sizeof(label), "%c", 'A' + t);
                if (ImGui::Selectable(label, t == *target))
                {
                    *target = t;
                }
            }
            ImGui::EndCombo();
        }
    }

    void GameCombo(const char* id, int* game)
    {
        char current[8];
        snprintf(current, sizeof(current), "%d", *game + 1);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
        if (ImGui::BeginCombo(id, current))
        {
            for (int g = 0; g < ISS_GAMES; g++)
            {
                char label[8];
                snprintf(label, sizeof(label), "%d", g + 1);
                if (ImGui::Selectable(label, g == *game))
                {
                    *game = g;
                }
            }
            ImGui::EndCombo();
        }
    }

    void DrawAutoSaveConfirm(const char* id, const char* slot, const char* what, bool* yes)
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextColored(kYellow, "Slot %s is the slot the game in progress autosaves to.", slot);
        ImGui::TextWrapped("%s over it now, and the game's next autosave writes the game in "
                           "progress over it again. To keep what goes in, quit to the title "
                           "screen first and load it from there, or choose another slot.",
                           what);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        char label[64];
        snprintf(label, sizeof(label), "%s anyway", what);
        if (ImGui::Button(label))
        {
            *yes = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawImportPopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        // A fixed width (the display's, up to 56 lines of text across) and a
        // height that fits: long sources wrap inside it rather than widen it.
        const float display = ImGui::GetIO().DisplaySize.x * 0.92f;
        const float wanted = ImGui::GetFontSize() * 56.0f;
        ImGui::SetNextWindowSize(ImVec2(wanted < display ? wanted : display, 0.0f), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal("Import saves", NULL, ImGuiWindowFlags_NoResize))
        {
            return;
        }

        ImGui::Text("From %s", sImport.name.c_str());
        ImGui::Spacing();

        bool close = false;
        if (!sImport.error.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kRed, "%s", sImport.error.c_str());
            ImGui::PopTextWrapPos();
            close = ImGui::Button("Close");
        }
        else if (sImport.done)
        {
            ImGui::PushTextWrapPos(0.0f);
            for (size_t i = 0; i < sImport.results.size(); i++)
            {
                ImGui::TextColored(sImport.resultOk[i] ? kGreen : kRed, "%s",
                                   sImport.results[i].c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            close = ImGui::Button("Close");
        }
        else
        {
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("import", 4, flags))
            {
                const ImGuiStyle& style = ImGui::GetStyle();
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::GetFrameHeight() + style.CellPadding.x * 2.0f);
                ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("Goes to", ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::GetFontSize() * 7.0f + style.ItemSpacing.x +
                                            style.CellPadding.x * 2.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < sImport.rows.size(); i++)
                {
                    ImportRow& r = sImport.rows[i];
                    ImGui::PushID((int)i);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::BeginDisabled(!r.usable);
                    ImGui::Checkbox("##use", &r.use);
                    ImGui::EndDisabled();

                    ImGui::TableSetColumnIndex(1);
                    if (r.usable)
                    {
                        ImGui::Text("%s, %d%%", Level(r.label.c_str()), (int)r.progress);
                    }
                    else
                    {
                        ImGui::TextUnformatted("(cannot be imported)");
                    }
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("%s", r.source.c_str());
                    ImGui::PopTextWrapPos();

                    ImGui::TableSetColumnIndex(2);
                    ImGui::BeginDisabled(!r.usable || !r.use);
                    FolderCombo("##folder", &r.target);
                    ImGui::SameLine();
                    GameCombo("##game", &r.game);
                    ImGui::EndDisabled();

                    ImGui::TableSetColumnIndex(3);
                    if (!r.usable)
                    {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextColored(kRed, "%s", r.problem.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    else if (r.use)
                    {
                        const iSaveSlotInfo* s = ImportSlot(r.target, r.game);
                        ImGui::PushTextWrapPos(0.0f);
                        if (s != NULL && s->exists)
                        {
                            ImGui::TextColored(kYellow, "replaces %s",
                                               s->valid ? Level(s->label) : "a damaged save");
                        }
                        else
                        {
                            ImGui::TextDisabled("empty slot");
                        }
                        if (IsAutoSaveSlot(r.target, r.game))
                        {
                            ImGui::TextColored(kYellow, "the game autosaves here");
                        }
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            const ImportCheck c = CheckImport();
            if (c.occupied)
            {
                ImGui::Checkbox("Replace the saves already there (each one replaced is kept as "
                                "backup 1)",
                                &sImport.replace);
            }
            if (c.duplicate)
            {
                ImGui::TextColored(kRed, "Two saves are going to the same slot.");
            }
            if (c.autosave)
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kYellow,
                                   "Slot %s is the slot the game in progress autosaves to: its "
                                   "next autosave writes over what is imported there.",
                                   c.autosaveSlot);
                ImGui::PopTextWrapPos();
            }

            ImGui::Spacing();
            const bool can = c.used > 0 && !c.duplicate && (!c.occupied || sImport.replace);
            ImGui::BeginDisabled(!can);
            if (ImGui::Button(c.used > 1 ? "Import them" : "Import"))
            {
                if (c.autosave)
                {
                    ImGui::OpenPopup("Import over the autosave slot?");
                }
                else
                {
                    DoImport();
                }
            }
            ImGui::EndDisabled();
            if (!can && c.occupied && !sImport.replace)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("Choose empty slots, or tick Replace.");
            }
            ImGui::SameLine();
            close = ImGui::Button("Cancel");

            bool yes = false;
            DrawAutoSaveConfirm("Import over the autosave slot?", c.autosaveSlot, "Import", &yes);
            if (yes)
            {
                DoImport();
            }
        }

        if (close || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                      !ImGui::IsPopupOpen("Import over the autosave slot?")))
        {
            sImport = ImportState();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- backups, restore, delete -----------------------------------------------------

    S32 sBackupTarget = -1;
    S32 sBackupGame = -1;
    std::vector<iSaveBackupInfo> sBackups;
    S32 sRestoreWhich;
    bool sOpenBackups;

    S32 sDeleteTarget = -1;
    S32 sDeleteGame = -1;
    bool sOpenDelete;

    void DrawBackupsPopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Backups", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        char slot[4];
        SlotName(sBackupTarget, sBackupGame, slot, sizeof(slot));
        ImGui::Text("Slot %s keeps the last %d saves it held before this one.", slot, ISS_BACKUPS);
        ImGui::TextDisabled("Restoring one puts it back in the slot; what is there now becomes "
                            "backup 1.");
        ImGui::Spacing();

        bool close = false;
        if (sBackups.empty())
        {
            ImGui::TextUnformatted("No backups.");
        }
        else if (ImGui::BeginTable("backups", 4,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            for (size_t i = 0; i < sBackups.size(); i++)
            {
                const iSaveBackupInfo& b = sBackups[i];
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Backup %d", (int)b.which);
                ImGui::TableSetColumnIndex(1);
                if (b.valid)
                {
                    ImGui::Text("%s, %d%%", Level(b.label), (int)b.progress);
                }
                else
                {
                    ImGui::TextColored(kRed, "damaged");
                }
                ImGui::TableSetColumnIndex(2);
                char when[32];
                When(b.mtime, when, sizeof(when));
                ImGui::TextUnformatted(when);
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(!b.valid);
                if (ImGui::SmallButton("Restore"))
                {
                    sRestoreWhich = b.which;
                    if (IsAutoSaveSlot(sBackupTarget, sBackupGame))
                    {
                        ImGui::OpenPopup("Restore over the autosave slot?");
                    }
                    else
                    {
                        sRestoreWhich = -sRestoreWhich; // restore now, below
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        bool yes = false;
        DrawAutoSaveConfirm("Restore over the autosave slot?", slot, "Restore", &yes);
        if (yes || sRestoreWhich < 0)
        {
            const S32 which = sRestoreWhich < 0 ? -sRestoreWhich : sRestoreWhich;
            sRestoreWhich = 0;
            char err[256] = "";
            if (iSaveStoreRestore(sBackupTarget, sBackupGame, which, err, sizeof(err)))
            {
                Say(false, "Backup %d is back in slot %s; what was there is backup 1.", (int)which,
                    slot);
            }
            else
            {
                Say(true, "Backup %d of %s was not restored: %s", (int)which, slot, err);
            }
            sStale = true;
            close = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("Close") ||
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
             !ImGui::IsPopupOpen("Restore over the autosave slot?")))
        {
            close = true;
        }
        if (close)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawDeletePopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Delete the save?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        char slot[4];
        SlotName(sDeleteTarget, sDeleteGame, slot, sizeof(slot));
        iSaveSlotInfo info;
        iSaveStoreSlot(sDeleteTarget, sDeleteGame, &info);

        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::Text("Empty slot %s (%s)?", slot,
                    info.valid ? Level(info.label) : (info.exists ? "a damaged save" : "nothing there"));
        ImGui::TextDisabled("The save becomes backup 1 of the slot, so Backups can put it back.");
        if (IsAutoSaveSlot(sDeleteTarget, sDeleteGame))
        {
            ImGui::TextColored(kYellow, "This is the slot the game in progress autosaves to; its "
                                        "next autosave writes the slot again.");
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (ImGui::Button("Delete"))
        {
            char err[256] = "";
            if (iSaveStoreDelete(sDeleteTarget, sDeleteGame, err, sizeof(err)))
            {
                Say(false, "Slot %s emptied; the save is backup 1.", slot);
            }
            else
            {
                Say(true, "Slot %s was not emptied: %s", slot, err);
            }
            sStale = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- the dialogs' answers -------------------------------------------------------------

    void PollDialogs()
    {
        char picked[1024];
        if (iGui::PollFolder("save-import", picked, sizeof(picked)))
        {
            StartImport(picked);
        }
        if (iGui::PollFolder("save-export-gci", picked, sizeof(picked)))
        {
            FinishExportGCI(picked);
        }
        if (iGui::PollFolder("save-export-zip", picked, sizeof(picked)))
        {
            FinishExportZip(picked);
        }
        if (iGui::PollFolder("save-export-folder", picked, sizeof(picked)))
        {
            FinishExportFolder(picked);
        }
#ifndef __ANDROID__
        if (iGui::PollFolder("save-import-xbox", picked, sizeof(picked)))
        {
            StartImport(picked, true);
        }
#endif
#if IGS_EXPORT_OTHER_CONSOLES
        if (iGui::PollFolder("save-export-xbox", picked, sizeof(picked)))
        {
            FinishExportXbox(picked);
        }
        if (iGui::PollFolder("save-xbox-xbe", picked, sizeof(picked)))
        {
            iGui::Commit("game.xbox_xbe", picked);
            sXboxFiles.valid = false;
        }
        if (iGui::PollFolder("save-xbox-keys", picked, sizeof(picked)))
        {
            iGui::Commit("game.xbox_keys", picked);
            sXboxFiles.valid = false;
        }
        if (iGui::PollFolder("save-export-ps2-psu", picked, sizeof(picked)))
        {
            FinishExportPS2(picked, ISS_PS2_PSU);
        }
        if (iGui::PollFolder("save-export-ps2-card", picked, sizeof(picked)))
        {
            FinishExportPS2(picked, ISS_PS2_CARD);
        }
        if (iGui::PollFolder("save-export-ps2-folder", picked, sizeof(picked)))
        {
            FinishExportPS2(picked, ISS_PS2_FOLDER_CARD);
        }
        if (iGui::PollFolder("save-ps2-icon", picked, sizeof(picked)))
        {
            iGui::Commit("game.ps2_icon", picked);
            sPS2Icon.valid = false;
        }
        if (iGui::PollFolder("save-export-psv", picked, sizeof(picked)))
        {
            FinishExportPsv(picked);
        }
        if (iGui::PollFolder("save-psv-keys", picked, sizeof(picked)))
        {
            iGui::Commit("game.psv_keys", picked);
            sPsvFiles.valid = false;
        }
#endif
    }

    // --- the slot table ----------------------------------------------------------------------

    void DrawSlots()
    {
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable("slots", 5, flags))
        {
            return;
        }

        // Columns as wide as what they hold, measured in the font in use.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad = style.CellPadding.x * 2.0f;
        const float button = style.FramePadding.x * 2.0f;
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Slot").x + pad);
        ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("0000-00-00 00:00").x + pad);
        ImGui::TableSetupColumn("Backups", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Backups").x + pad);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Export...").x + ImGui::CalcTextSize("Delete").x +
                                    button * 2.0f + style.ItemSpacing.x + pad);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < sSlots.size(); i++)
        {
            const iSaveSlotInfo& s = sSlots[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(s.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            if (!s.exists)
            {
                ImGui::TextDisabled("(empty)");
            }
            else if (!s.valid)
            {
                ImGui::TextColored(kRed, "damaged: %s", s.problem);
            }
            else
            {
                ImGui::Text("%s, %d%%", Level(s.label), (int)s.progress);
                if (s.scene[0] != '\0' && ImGui::BeginItemTooltip())
                {
                    ImGui::Text("Resumes in %s (world %d). %d bytes.", s.scene,
                                (int)s.levelIndex + 1, (int)s.size);
                    if (s.fromCard)
                    {
                        ImGui::TextUnformatted("Came off a memory card and has not been saved "
                                               "over since: it exports as the same .gci.");
                    }
                    ImGui::EndTooltip();
                }
            }
            if (IsAutoSaveSlot(s.target, s.game))
            {
                ImGui::SameLine();
                ImGui::TextColored(kYellow, "autosave");
            }
            if (s.exists && s.fromCard)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(card)");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            char when[32];
            When(s.exists ? s.mtime : 0, when, sizeof(when));
            ImGui::TextUnformatted(when);

            ImGui::TableSetColumnIndex(3);
            ImGui::AlignTextToFramePadding();
            if (s.backups > 0)
            {
                char label[24];
                snprintf(label, sizeof(label), "%d...", (int)s.backups);
                if (ImGui::SmallButton(label))
                {
                    sBackupTarget = s.target;
                    sBackupGame = s.game;
                    iSaveStoreBackups(s.target, s.game, sBackups);
                    sRestoreWhich = 0;
                    sOpenBackups = true;
                }
                if (ImGui::BeginItemTooltip())
                {
                    ImGui::TextUnformatted("The saves this slot held before: restore one.");
                    ImGui::EndTooltip();
                }
            }
            else
            {
                ImGui::TextDisabled("0");
            }

            ImGui::TableSetColumnIndex(4);
            if (s.exists)
            {
                ImGui::BeginDisabled(!s.valid || iGui::PickingFolder());
                if (ImGui::SmallButton("Export..."))
                {
                    ImGui::OpenPopup("export-as");
                }
                ImGui::EndDisabled();
                if (ImGui::BeginItemTooltip())
                {
#if IGS_EXPORT_OTHER_CONSOLES
                    ImGui::TextUnformatted("This save as a .gci, for Dolphin's Import or a "
                                           "console's memory card manager; or as an Xbox, "
                                           "PlayStation 2 or PlayStation 3 save.");
#else
                    ImGui::TextUnformatted("This save as a .gci, for Dolphin's Import or a "
                                           "console's memory card manager.");
#endif
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopup("export-as"))
                {
                    if (ImGui::MenuItem("GameCube save (.gci)..."))
                    {
                        StartExportGCI(s);
                    }
#if IGS_EXPORT_OTHER_CONSOLES
                    char why[768] = "";
                    const bool ready = XboxReady(why, sizeof(why));
                    if (ImGui::MenuItem("Xbox save, into a folder...", NULL, false, ready))
                    {
                        sXboxTarget = s.target;
                        sXboxGame = s.game;
                        iGui::PickFolder("save-export-xbox", NULL);
                    }
                    if (!ready)
                    {
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                        ImGui::TextDisabled("Xbox: not ready (%s). See \"Export to an Xbox\" "
                                            "below.",
                                            why);
                        ImGui::PopTextWrapPos();
                    }
                    else
                    {
                        ImGui::TextDisabled("Xbox: signed for %s.", XboxForWhat());
                    }
                    static const char* const kPS2Item[] = {
                        "PlayStation 2 save, as a .psu file...",
                        "PlayStation 2 save, into a memory card file...",
                        "PlayStation 2 save, into PCSX2's folder card..."
                    };
                    if (ImGui::MenuItem(kPS2Item[sPS2As]))
                    {
                        StartExportPS2(s.target, s.game);
                    }
                    ImGui::TextDisabled("PlayStation 2: see \"Export to a PlayStation 2\" "
                                        "below.");
                    char psvWhy[768] = "";
                    const bool psvReady = PsvReady(psvWhy, sizeof(psvWhy));
                    if (ImGui::MenuItem("PlayStation 3 save (.psv)...", NULL, false, psvReady))
                    {
                        StartExportPsv(s.target, s.game);
                    }
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                    if (!psvReady)
                    {
                        ImGui::TextDisabled("PlayStation 3: not ready (%s). See \"Export to a "
                                            "PlayStation 3\" below.",
                                            psvWhy);
                    }
                    else
                    {
                        ImGui::TextDisabled("PlayStation 3: with %s.", PsvForWhat());
                    }
                    ImGui::PopTextWrapPos();
#endif
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    sDeleteTarget = s.target;
                    sDeleteGame = s.game;
                    sOpenDelete = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace

void iGui::DrawSaves()
{
    PollDialogs();
    Refresh();

    // TSSM_GUI_IMPORT=<file>: the import dialog for that file, the first time
    // the tab is drawn, as if it had been picked (docs/GUI.md, debug switches).
    static bool sDebugImportDone;
    if (!sDebugImportDone)
    {
        sDebugImportDone = true;
        const char* file = getenv("TSSM_GUI_IMPORT");
        if (file != NULL && file[0] != '\0')
        {
            StartImport(file);
        }
    }

    const char* root = iSaveStoreRoot();

    ImGui::SeparatorText("Save folder");
    ImGui::TextWrapped("%s", root);
    if (ImGui::Button("Open the save folder"))
    {
        iHostMakeDir(root);
        OpenFolder(root);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(PickingFolder());
    if (ImGui::Button("Import..."))
    {
        PickOpenFile("save-import", NULL, kImportFilters, 2);
    }
    ImGui::EndDisabled();
#ifdef __ANDROID__
    // No folder picker here (SDL_androiddialog.c): an Xbox save comes in as a .zip.
    Note("Import takes a Dolphin .gci, a GameShark .gcs, a memory card image (.raw, .gcp), a "
         ".zip from Export, or a save file from another install. An Xbox save comes in as a "
         ".zip of its folder: the save's own folder (it holds GameData.xsv), the game's "
         "(UDATA\\54510027), a whole UDATA or Cxbx-Reloaded's data folder. Xbox saves are read "
         "whatever their signature. A PlayStation 2 save is a .psu, an Action Replay .max, a "
         "PlayStation 3's .psv or a memory card file (PCSX2's .ps2, a PS3's .VM2), which is "
         "only read. Xbox and PlayStation 2 saves need the game's own files to convert. Every "
         "save replaced is kept as a backup.");
#else
    ImGui::SameLine();
    ImGui::BeginDisabled(PickingFolder());
    if (ImGui::Button("Import a save folder (Xbox, PCSX2)..."))
    {
        PickFolder("save-import-xbox", NULL);
    }
    ImGui::EndDisabled();
    Note("Import takes a Dolphin .gci, a GameShark .gcs, a memory card image (.raw, .gcp), a "
         ".zip from Export, or a save file from another install. An Xbox save is a folder: "
         "pick the save's own folder (it holds GameData.xsv), the game's (UDATA\\54510027), a "
         "whole UDATA or Cxbx-Reloaded's data folder, or a .zip of one. Xbox saves are read "
         "whatever their signature. A PlayStation 2 save is a .psu, an Action Replay .max, a "
         "PlayStation 3's .psv or a memory card file (PCSX2's .ps2, a PS3's .VM2), which is "
         "only read; or, "
         "as a folder, PCSX2's folder-type card or the game's folder inside it. Xbox and "
         "PlayStation 2 saves need the game's own files to convert. Every save replaced is "
         "kept as a backup.");
#endif

    ImGui::SeparatorText("Saves");
    if (ImGui::Checkbox("Folders C to J too (the PC save screen's)", &sAllFolders))
    {
        sStale = true;
        Refresh();
    }

    S32 autoTarget;
    S32 autoGame;
    if (iSaveStoreAutoSaveSlot(&autoTarget, &autoGame))
    {
        char slot[4];
        SlotName(autoTarget, autoGame, slot, sizeof(slot));
        ImGui::TextColored(kYellow, "The game in progress autosaves to slot %s.", slot);
    }
    else
    {
        ImGui::TextDisabled("The game has no autosave slot yet (it gets one at the first save "
                            "or load).");
    }

    if (!sStatus.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(sStatusBad ? kRed : kGreen, "%s", sStatus.c_str());
        ImGui::PopTextWrapPos();
    }

    DrawSlots();

    ImGui::SeparatorText("Export");
    {
        static const char* const kRegions[] = { "As each save says (North America if made here)",
                                                "North America (GGVE)", "Europe (GGVP)" };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Region");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(kRegions[0]).x + ImGui::GetFrameHeight() +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##region", &sRegion, kRegions, 3);
        ImGui::SameLine();
        HelpMarker("The game code a .gci is written with. A console or Dolphin only lists a save "
                   "for its own region's disc. Europe also writes the date the PAL way.");
    }
    ImGui::BeginDisabled(PickingFolder());
    if (ImGui::Button("Every save as a .zip..."))
    {
        PickSaveFile("save-export-zip", "SpongeBobMovie-saves.zip", kZipFilter, 1);
    }
#ifdef __ANDROID__
    // SDL has no folder picker on Android (SDL_androiddialog.c answers
    // "unsupported"), so the GCI-folder export is left out there; each save's
    // own .gci export, above, is a file picker and works.
    ImGui::EndDisabled();
    Note("A .zip carries every save to another install of the port; Import takes it back slot "
         "for slot. For Dolphin or a console, export one save at a time as a .gci (its "
         "Export... button in the list). Xbox, PlayStation 2 and PlayStation 3 saves are made "
         "on a PC with the same saves.");
#else
    ImGui::SameLine();
    if (ImGui::Button("Every save into a GCI folder..."))
    {
        PickFolder("save-export-folder", NULL);
    }
    ImGui::EndDisabled();
    Note("A .zip carries every save to another install of the port; Import takes it back slot "
         "for slot. A GCI folder is the folder Dolphin keeps a memory card in when it uses GCI "
         "folders (Card A): folder A's saves go in the folder chosen, folder B's in B/ inside "
         "it.");
#endif

#if IGS_EXPORT_OTHER_CONSOLES
    ImGui::SeparatorText("Export to an Xbox");
    {
        static const char* const kFor[] = {
            "Cxbx-Reloaded and other copies of this port",
            "A real Xbox or xemu (needs your keys.bin from your own Xbox)"
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("For");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(kFor[1]).x + ImGui::GetFrameHeight() +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##xboxfor", &sXboxFor, kFor, 2);
        ImGui::SameLine();
        HelpMarker("An Xbox save is signed, and a console only loads a save whose signature it "
                   "can make itself. The first choice is the signature Cxbx-Reloaded makes when "
                   "it has no keys.bin: it needs nothing but the Xbox game's program. The second "
                   "is the one a real Xbox makes, and needs the keys.bin from your own Xbox as "
                   "well (the same 32-byte file Cxbx-Reloaded reads). The game has no key of "
                   "its own, and nothing is typed in: both are files you choose.");

        const XboxFiles files = XboxLook();
        ImGui::TextUnformatted("The Xbox game's program (DEFAULT.XBE, from your own copy)");
        XboxFileRow("save-xbox-xbe", "game.xbox_xbe", files.xbe,
                    "(not found: choose your DEFAULT.XBE)", kXbeFilter, 2);
        bool said = false; // the reason is under its row already
        if (files.xbeBad ||
            (files.xbe[0] == '\0' && iConfigGetString("game.xbox_xbe", "")[0] != '\0'))
        {
            said = true;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kRed, "%s.", files.xbeWhy);
            ImGui::PopTextWrapPos();
        }

        if (sXboxFor == 1)
        {
            ImGui::TextUnformatted("Your keys.bin (made from your own Xbox)");
            XboxFileRow("save-xbox-keys", "game.xbox_keys", files.keys,
                        "(none: choose your keys.bin)", kKeysFilter, 2);
            if (files.keysBad ||
                (files.keys[0] == '\0' && iConfigGetString("game.xbox_keys", "")[0] != '\0'))
            {
                said = true;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kRed, "%s.", files.keysWhy);
                ImGui::PopTextWrapPos();
            }
        }

        ImGui::Checkbox("Replace a save of the same name already in the folder", &sXboxReplace);

        static const char* const kFolders[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J" };
        const int folders = sAllFolders ? ISS_MAX_TARGETS : 2;
        if (sXboxFolder >= folders)
        {
            sXboxFolder = 0;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Save folder");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("A").x + ImGui::GetFrameHeight() * 2.0f +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##xboxfolder", &sXboxFolder, kFolders, folders);
        ImGui::SameLine();

        char missing[768] = "";
        const bool ready = XboxReady(missing, sizeof(missing));
        ImGui::BeginDisabled(!ready || PickingFolder());
        if (ImGui::Button("Its saves as Xbox saves..."))
        {
            sXboxTarget = -1;
            PickFolder("save-export-xbox", NULL);
        }
        ImGui::EndDisabled();
        if (!ready && !said)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kYellow, "%s.", missing);
            ImGui::PopTextWrapPos();
        }
    }
    Note("An Xbox holds three saves, as one save folder does; a slot's own Export... writes "
         "one. Pick Cxbx-Reloaded's data folder (the one holding EmuDisk) and the saves go "
         "where it keeps them; pick any other folder and it gets UDATA\\54510027 with the "
         "saves inside, ready to copy to an Xbox's E: drive. The title's own files there are "
         "made from your DEFAULT.XBE when they are missing.");

    ImGui::SeparatorText("Export to a PlayStation 2");
    {
        static const char* const kAs[] = {
            "A .psu file (uLaunchELF on a console, or mymc)",
            "Into a memory card file (PCSX2's .ps2, a PS3's .VM2; it is copied first)",
            "Into PCSX2's folder-type memory card"
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("As");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(kAs[1]).x + ImGui::GetFrameHeight() +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##ps2as", &sPS2As, kAs, 3);
        ImGui::SameLine();
        HelpMarker("A .psu file is what uLaunchELF on a PlayStation 2 and tools like mymc take. "
                   "A memory card file is PCSX2's .ps2 card, or a PS3's virtual memory card "
                   "for PlayStation 2 games (.VM2): the save is added to it, "
                   "everything else on the card stays, and before anything is written the "
                   "card is copied beside itself as .bak. A card PCSX2 has just made is blank "
                   "until a game or the console's browser formats it. PCSX2's folder-type card "
                   "is a folder named like a card file. Close PCSX2 first, or it will not see "
                   "the change.");

        const PS2Icon& icon = PS2IconLook();
        ImGui::TextUnformatted("The game's own icon (PLAT.HIP from your own PlayStation 2 disc, "
                               "or a save the game made)");
        XboxFileRow("save-ps2-icon", "game.ps2_icon", icon.from.c_str(),
                    "(none chosen: the port's own icon is written)", kPs2IconFilter, 2);
        if (icon.why[0] != '\0')
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kRed, "%s.", icon.why);
            ImGui::PopTextWrapPos();
        }

        ImGui::Checkbox("Replace a save of the same number already there", &sPS2Replace);
        ImGui::SameLine();
        ImGui::Checkbox("For the European (PAL) game", &sPS2Pal);

        static const char* const kFolders[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J" };
        const int folders = sAllFolders ? ISS_MAX_TARGETS : 2;
        if (sPS2Folder >= folders)
        {
            sPS2Folder = 0;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Save folder");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("A").x + ImGui::GetFrameHeight() * 2.0f +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##ps2folder", &sPS2Folder, kFolders, folders);
        ImGui::SameLine();
        ImGui::BeginDisabled(icon.why[0] != '\0' || PickingFolder());
        if (ImGui::Button("Its saves as PlayStation 2 saves..."))
        {
            StartExportPS2(-1, -1);
        }
        ImGui::EndDisabled();
    }
    Note("A PlayStation 2 card keeps the game's saves in one folder, three at most, as one "
         "save folder here does; a slot's own Export... writes one. With the save go the "
         "files the game keeps beside it when they are not there yet: icon.sys, the icon "
         "and a filler file. A save for a real console goes over as a .psu with uLaunchELF.");

    ImGui::SeparatorText("Export to a PlayStation 3");
    {
        static const char* const kFor[] = {
            "Stand-in signature (this port and PC save tools; a PS3 needs it re-signed)",
            "Real signature from your own file (not yet confirmed on a PS3)"
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Signature");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(kFor[0]).x + ImGui::GetFrameHeight() +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##psvfor", &sPsvFor, kFor, 2);
        ImGui::SameLine();
        HelpMarker("A PlayStation 3 keeps PlayStation 2 saves on virtual memory cards and moves "
                   "them in and out as .psv files, which it signs; it only takes a .psv whose "
                   "signature it can make itself. The stand-in signature makes a valid file that "
                   "other copies of this port and PC tools that read .psv files take, but a PS3 "
                   "needs it re-signed first, for example by a save tool on a modded PS3. The "
                   "real signature is the PS3's own, made from a key file you provide (32 "
                   "bytes; docs/SAVES.md says what it holds). The port follows the public "
                   "description of how a PS3 signs, which has not yet been tried on a PS3. The "
                   "game has no key of its own, and nothing is typed in.");

        bool said = false;
        if (sPsvFor == 1)
        {
            const PsvFiles files = PsvLook();
            ImGui::TextUnformatted("Your PS3 key file (32 bytes: the key, then its IV)");
            XboxFileRow("save-psv-keys", "game.psv_keys", files.keys,
                        "(none: choose your PS3 key file)", kPsvKeysFilter, 2);
            if (files.keysBad ||
                (files.keys[0] == '\0' && iConfigGetString("game.psv_keys", "")[0] != '\0'))
            {
                said = true;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kRed, "%s.", files.keysWhy);
                ImGui::PopTextWrapPos();
            }
        }

        ImGui::Checkbox("Replace a save of the same number already in the .psv", &sPsvReplace);
        ImGui::SameLine();
        ImGui::Checkbox("For the European (PAL) game##psvpal", &sPsvPal);

        static const char* const kFolders[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J" };
        const int folders = sAllFolders ? ISS_MAX_TARGETS : 2;
        if (sPsvFolder >= folders)
        {
            sPsvFolder = 0;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Save folder");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("A").x + ImGui::GetFrameHeight() * 2.0f +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::Combo("##psvfolder", &sPsvFolder, kFolders, folders);
        ImGui::SameLine();

        char missing[768] = "";
        const bool ready = PsvReady(missing, sizeof(missing));
        ImGui::BeginDisabled(!ready || PickingFolder());
        if (ImGui::Button("Its saves as one .psv..."))
        {
            StartExportPsv(-1, -1);
        }
        ImGui::EndDisabled();
        if (!ready && !said)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kYellow, "%s.", missing);
            ImGui::PopTextWrapPos();
        }
    }
    Note("A .psv holds the game's folder as the PlayStation 3 exports it from a virtual "
         "memory card: up to three saves, with icon.sys, the icon (the one chosen under "
         "\"Export to a PlayStation 2\") and the filler. Choosing an existing .psv adds to it. "
         "A PS3's virtual memory card itself (.VM2) is written to as a memory card file, "
         "above.");
#endif

    // TSSM_GUI_SAVES_SCROLL=<0..1>: how far down the tab is scrolled, for a
    // capture of what is below the first screenful (docs/GUI.md, debug
    // switches).
    {
        static float sDebugScroll = -1.0f;
        if (sDebugScroll < 0.0f)
        {
            const char* scroll = getenv("TSSM_GUI_SAVES_SCROLL");
            sDebugScroll = (scroll != NULL && scroll[0] != '\0') ? (float)atof(scroll) : 0.0f;
        }
        if (sDebugScroll > 0.0f)
        {
            ImGui::SetScrollY(ImGui::GetScrollMaxY() * (sDebugScroll > 1.0f ? 1.0f : sDebugScroll));
        }
    }

    if (sOpenImport)
    {
        sOpenImport = false;
        ImGui::OpenPopup("Import saves");
    }
    DrawImportPopup();

    if (sOpenBackups)
    {
        sOpenBackups = false;
        ImGui::OpenPopup("Backups");
    }
    DrawBackupsPopup();

    if (sOpenDelete)
    {
        sOpenDelete = false;
        ImGui::OpenPopup("Delete the save?");
    }
    DrawDeletePopup();
}
