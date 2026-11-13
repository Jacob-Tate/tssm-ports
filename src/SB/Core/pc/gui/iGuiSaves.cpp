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
// The game in progress keeps writing to the slot it last saved to or loaded
// from -- its autosave. Importing or restoring over that slot is allowed, but
// only after saying that the next autosave will write over it again.

#include "imgui.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"

#include "iHost.h"
#include "iSaveFormat.h"
#include "iSaveStore.h"

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
        { "GameCube saves, memory cards and save zips", "gci;gcs;raw;gcp;zip" },
        { "All files", "*" },
    };
    const iGui::FileFilter kGciFilter[] = { { "GameCube save (.gci)", "gci" } };
    const iGui::FileFilter kZipFilter[] = { { "Zip of saves", "zip" } };

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

    void StartImport(const char* path)
    {
        sImport = ImportState();
        sImport.name = BaseName(path);
        iSaveStoreList(sImport.slots, true);

        char why[512] = "";
        if (!iGui::ReadWholeFile(path, sImport.data, why, sizeof(why)))
        {
            sImport.error = why;
        }
        else if (sImport.data.empty())
        {
            sImport.error = sImport.name + " is empty.";
        }
        else
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
                    StartExportGCI(s);
                }
                ImGui::EndDisabled();
                if (ImGui::BeginItemTooltip())
                {
                    ImGui::TextUnformatted("This save as a .gci, for Dolphin's Import or a "
                                           "console's memory card manager.");
                    ImGui::EndTooltip();
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
    Note("Import takes a Dolphin .gci, a GameShark .gcs, a memory card image (.raw, .gcp), a "
         ".zip from Export, or a save file from another install. Every save replaced is kept "
         "as a backup.");

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
