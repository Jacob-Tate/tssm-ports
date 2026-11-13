// The Mods tab: the mod manager, on the mod system's own interface (iMod.h).
//
// The list is the mods folder's, in load order: a mod further down wins where
// two replace the same thing. Each can be switched on or off and moved (the
// arrows, or by dragging its name); every change rebuilds the override index
// at once and is written to mods.ini, so the list here and the file are the
// same thing. What is already loaded keeps its assets until the game loads it
// again, which iModRestartAdvised says. The conflicts are the mod system's
// own, and the log is what it found and applied.
//
// The single mod folder of the earlier design ([assets] mod / TSSM_MOD, a copy
// of the game's folder) still works and is kept at the bottom.

#include "imgui.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"
#include "iMod.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

namespace
{
    const ImVec4 kRed(1.0f, 0.47f, 0.43f, 1.0f);
    const ImVec4 kYellow(0.98f, 0.82f, 0.31f, 1.0f);
    const ImVec4 kGreen(0.55f, 0.85f, 0.55f, 1.0f);

    S32 sSelected = -1;
    char sFilter[96];
    bool sFollowLog = true;
    S32 sLogSeen;

    std::string sStatus;
    bool sStatusBad;

    void Say(bool bad, const char* fmt, ...)
    {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        sStatus = buf;
        sStatusBad = bad;
    }

    // Every change to the list goes to mods.ini at once.
    void SaveList()
    {
        if (!iModSaveList())
        {
            Say(true, "The load order could not be written to mods.ini (see the log).");
        }
    }

    // "3 textures, 1 sound".
    void Summary(const iModInfo& m, char* out, size_t size)
    {
        static const char* const kPlural[IMOD_KIND_COUNT] = { "files",  "assets", "textures",
                                                               "sounds", "movies", "INI overlays" };
        static const char* const kSingle[IMOD_KIND_COUNT] = { "file",  "asset", "texture",
                                                               "sound", "movie", "INI overlay" };
        size_t o = 0;
        out[0] = '\0';
        for (S32 k = 0; k < IMOD_KIND_COUNT; k++)
        {
            if (m.counts[k] <= 0)
            {
                continue;
            }
            o += (size_t)snprintf(out + o, o < size ? size - o : 0, "%s%d %s", o > 0 ? ", " : "",
                                  (int)m.counts[k], m.counts[k] == 1 ? kSingle[k] : kPlural[k]);
        }
        if (o == 0)
        {
            snprintf(out, size, "nothing");
        }
    }

    void Move(S32 from, S32 to)
    {
        if (to < 0 || to >= iModCount() || from == to)
        {
            return;
        }
        if (iModMove(from, to))
        {
            if (sSelected == from)
            {
                sSelected = to;
            }
            SaveList();
            const iModInfo* m = iModGet(to);
            Say(false, "%s is now number %d.", m != NULL ? m->name : "The mod", (int)to + 1);
        }
    }

    void DrawFolder()
    {
        const char* folder = iModFolder();
        if (folder[0] != '\0')
        {
            ImGui::TextWrapped("%s", folder);
        }
        else
        {
            ImGui::TextWrapped("There is no mods folder yet. Open it to make one at %s.",
                               iModDefaultFolder());
        }

        if (ImGui::Button("Open the mods folder"))
        {
            const char* dir = folder[0] != '\0' ? folder : iModDefaultFolder();
            if (folder[0] == '\0')
            {
                iHostMakeDir(dir);
                iModRescan();
            }
            iGui::OpenFolder(dir);
        }
        ImGui::SameLine();
        if (ImGui::Button("Rescan"))
        {
            const S32 n = iModRescan();
            sSelected = -1;
            Say(false, "%d mod%s found.", (int)n, n == 1 ? "" : "s");
        }
        if (ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted("Read the mods folder again: mods added, removed or edited.");
            ImGui::EndTooltip();
        }

        // The global switch. TSSM_MODS wins over the file and is shown so.
        const char* env = getenv("TSSM_MODS");
        const bool forced = env != NULL && env[0] != '\0';
        bool on = iModsEnabled() != 0;
        ImGui::SameLine();
        ImGui::BeginDisabled(forced);
        if (ImGui::Checkbox("Use mods", &on))
        {
            iGui::Commit("assets.mods", on ? "on" : "off");
            iModInit(); // reads the switch again and rebuilds the index
            Say(false, "Mods are %s.", on ? "on" : "off");
        }
        ImGui::EndDisabled();
        if (forced && ImGui::BeginItemTooltip())
        {
            ImGui::Text("TSSM_MODS=%s is set, which decides this.", env);
            ImGui::EndTooltip();
        }

        if (iModRestartAdvised())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kYellow, "The list changed after the game loaded its files. What "
                                        "is loaded already keeps the old assets until the game "
                                        "loads it again; restart the game to see every change.");
            ImGui::PopTextWrapPos();
        }
    }

    void DrawList()
    {
        const S32 n = iModCount();
        if (n == 0)
        {
            ImGui::TextDisabled("No mods. A mod is a folder in the mods folder: docs/MODDING.md "
                                "says what goes in it.");
            return;
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable("mods", 5, flags))
        {
            return;
        }
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.6f);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.8f);
        ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Replaces", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 3.6f);
        ImGui::TableHeadersRow();

        S32 dragFrom = -1;
        S32 dragTo = -1;

        for (S32 i = 0; i < n; i++)
        {
            const iModInfo* m = iModGet(i);
            if (m == NULL)
            {
                continue;
            }
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d", (int)i + 1);

            ImGui::TableSetColumnIndex(1);
            bool on = m->enabled != 0;
            if (ImGui::Checkbox("##on", &on))
            {
                iModSetEnabled(i, on ? 1 : 0);
                SaveList();
                Say(false, "%s %s.", m->name, on ? "switched on" : "switched off");
            }

            ImGui::TableSetColumnIndex(2);
            char label[200];
            snprintf(label, sizeof(label), "%s%s%s###sel", m->name, m->version[0] != '\0' ? "  " : "",
                     m->version);
            if (ImGui::Selectable(label, sSelected == i, ImGuiSelectableFlags_AllowOverlap))
            {
                sSelected = (sSelected == i) ? -1 : i;
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("TSSM_MOD", &i, sizeof(i));
                ImGui::Text("Move %s", m->name);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("TSSM_MOD"))
                {
                    dragFrom = *(const S32*)p->Data;
                    dragTo = i;
                }
                ImGui::EndDragDropTarget();
            }
            if (m->author[0] != '\0')
            {
                ImGui::TextDisabled("by %s", m->author);
            }
            if (m->problem[0] != '\0')
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kRed, "%s", m->problem);
                ImGui::PopTextWrapPos();
            }

            ImGui::TableSetColumnIndex(3);
            char summary[200];
            Summary(*m, summary, sizeof(summary));
            ImGui::TextWrapped("%s", summary);
            if (m->conflicts > 0)
            {
                ImGui::TextColored(kYellow, "%d conflict%s", (int)m->conflicts,
                                   m->conflicts == 1 ? "" : "s");
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up))
            {
                Move(i, i - 1);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i == n - 1);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down))
            {
                Move(i, i + 1);
            }
            ImGui::EndDisabled();

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (dragFrom >= 0 && dragTo >= 0)
        {
            Move(dragFrom, dragTo);
        }
    }

    void DrawDetails()
    {
        const iModInfo* m = iModGet(sSelected);
        if (m == NULL)
        {
            sSelected = -1;
            return;
        }

        ImGui::SeparatorText(m->name);
        if (m->description[0] != '\0')
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(m->description);
            ImGui::PopTextWrapPos();
        }
        ImGui::TextDisabled("Folder %s, priority %d%s", m->folder, (int)m->priority,
                            m->enabled ? "" : ", switched off");
        if (ImGui::BeginTable("kinds", IMOD_KIND_COUNT, ImGuiTableFlags_SizingFixedFit))
        {
            for (S32 k = 0; k < IMOD_KIND_COUNT; k++)
            {
                ImGui::TableSetupColumn(iModKindName(k));
            }
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            for (S32 k = 0; k < IMOD_KIND_COUNT; k++)
            {
                ImGui::TableSetColumnIndex(k);
                ImGui::Text("%d", (int)m->counts[k]);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Open its folder"))
        {
            iGui::OpenFolder(m->path);
        }
    }

    void DrawConflicts()
    {
        const S32 n = iModConflictCount();
        char title[64];
        snprintf(title, sizeof(title), "Conflicts (%d)###conflicts", (int)n);
        ImGui::SeparatorText(title);
        if (n == 0)
        {
            ImGui::TextDisabled("No two switched-on mods replace the same thing.");
            return;
        }

        ImGui::TextDisabled("Two mods replace the same thing; the one further down the list "
                            "is used.");
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
        const float height = ImGui::GetTextLineHeightWithSpacing() * (float)(n < 8 ? n + 1 : 9) +
                             ImGui::GetStyle().CellPadding.y * 4.0f;
        if (ImGui::BeginTable("conflicts", 4, flags, ImVec2(0.0f, height)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFontSize() * 4.5f);
            ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Over", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (S32 i = 0; i < n; i++)
            {
                const iModConflict* c = iModGetConflict(i);
                if (c == NULL)
                {
                    continue;
                }
                const iModInfo* w = iModGet(c->winner);
                const iModInfo* l = iModGet(c->loser);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(iModKindName(c->kind));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(c->key);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(w != NULL ? w->name : "?");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", l != NULL ? l->name : "?");
            }
            ImGui::EndTable();
        }
    }

    void DrawLog()
    {
        const S32 n = iModLogCount();
        char title[64];
        snprintf(title, sizeof(title), "Log (%d)###log", (int)n);
        ImGui::SeparatorText(title);

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        ImGui::InputTextWithHint("##filter", "filter", sFilter, sizeof(sFilter));
        ImGui::SameLine();
        ImGui::Checkbox("Follow", &sFollowLog);
        ImGui::SameLine();
        if (ImGui::Button("Copy"))
        {
            std::string all;
            for (S32 i = 0; i < n; i++)
            {
                const char* line = iModLogLine(i);
                if (sFilter[0] == '\0' || SDL_strcasestr(line, sFilter) != NULL)
                {
                    all += line;
                    all += '\n';
                }
            }
            ImGui::SetClipboardText(all.c_str());
            Say(false, "The log is on the clipboard.");
        }
        ImGui::SameLine();
        const char* folder = iModFolder();
        ImGui::BeginDisabled(folder[0] == '\0');
        if (ImGui::Button("Open mods.log"))
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/mods.log", folder);
            iGui::OpenFolder(path);
        }
        ImGui::EndDisabled();

        if (ImGui::BeginChild("log", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10.0f),
                              ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (S32 i = 0; i < n; i++)
            {
                const char* line = iModLogLine(i);
                if (sFilter[0] != '\0' && SDL_strcasestr(line, sFilter) == NULL)
                {
                    continue;
                }
                const bool bad = SDL_strcasestr(line, "cannot") != NULL ||
                                 SDL_strcasestr(line, "ignored") != NULL ||
                                 SDL_strcasestr(line, "conflict") != NULL;
                if (bad)
                {
                    ImGui::TextColored(kYellow, "%s", line);
                }
                else
                {
                    ImGui::TextUnformatted(line);
                }
            }
            if (sFollowLog && n != sLogSeen)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            sLogSeen = n;
        }
        ImGui::EndChild();
    }

    // [assets] mod: one folder laid out like the game's, over everything.
    void DrawLegacyFolder()
    {
        const char* using_ = iFileModName();
        if (using_[0] != '\0')
        {
            ImGui::Text("In use now: %s", using_);
        }
        else
        {
            ImGui::TextUnformatted("None in use.");
        }

        char picked[1024];
        if (iGui::PollFolder("mod", picked, sizeof(picked)))
        {
            iGui::Commit("assets.mod", picked);
        }

        const char* folder = iConfigGetString("assets.mod", "");
        ImGui::TextWrapped("[assets] mod: %s", folder[0] != '\0' ? folder : "(none)");
        ImGui::BeginDisabled(iGui::PickingFolder());
        if (ImGui::Button("Choose a folder..."))
        {
            iGui::PickFolder("mod", folder);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(folder[0] == '\0');
        if (ImGui::Button("None"))
        {
            iGui::Commit("assets.mod", "");
        }
        ImGui::EndDisabled();

        // What is in use is what iFileInit took at startup: a change shows at
        // the next start.
        char leaf[512];
        snprintf(leaf, sizeof(leaf), "%s", folder);
        size_t len = strlen(leaf);
        while (len > 0 && (leaf[len - 1] == '/' || leaf[len - 1] == '\\'))
        {
            leaf[--len] = '\0';
        }
        const char* name = leaf;
        for (const char* c = leaf; *c != '\0'; c++)
        {
            if (*c == '/' || *c == '\\' || *c == ':')
            {
                name = c + 1;
            }
        }
        if (SDL_strcasecmp(name, using_) != 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, "restart");
        }

        iGui::Note("A folder with the same layout as the game's; every file in it replaces the "
                   "game's file at the same place, over every mod above. Saves made with it are "
                   "kept apart from the game's. TSSM_MOD overrides this setting.");
    }
} // namespace

void iGui::DrawMods()
{
    ImGui::SeparatorText("Mods folder");
    DrawFolder();

    if (!sStatus.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(sStatusBad ? kRed : kGreen, "%s", sStatus.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::SeparatorText("Load order");
    Note("Further down wins where two mods replace the same thing. Drag a name, or use the "
         "arrows, to move a mod.");
    DrawList();
    if (sSelected >= 0)
    {
        DrawDetails();
    }

    DrawConflicts();
    DrawLog();

    if (ImGui::CollapsingHeader("Single mod folder ([assets] mod)"))
    {
        DrawLegacyFolder();
    }
}
