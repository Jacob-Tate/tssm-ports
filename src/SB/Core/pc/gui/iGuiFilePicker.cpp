// The GUI's own file browser. See iGuiFilePicker.h.
//
// One request at a time, as with the system's dialogs: iGuiUtil.cpp claims it
// (BeginPick), this file shows the folder list in a modal popup, and the
// answer -- a path, or nothing for Cancel -- goes back through iGui::AnswerPick
// to the same PollFolder the system dialog's answer would have reached.
//
// Paths are '/'-separated, as everywhere above the iHost seam. A folder's "top"
// is "/" (the SD card on the Switch) or a drive's "C:/".

#include "imgui.h"

#include <SDL3/SDL.h>

#include "gui/iGuiFilePicker.h"
#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadHost.h"
#include "iPadTokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    const ImVec4 kRed(1.0f, 0.47f, 0.43f, 1.0f);
    const ImVec4 kYellow(0.98f, 0.82f, 0.31f, 1.0f);

    const char* const kPopupId = "###tssm_file_picker";

    // --- which picker -------------------------------------------------------------

    SDL_AtomicInt sSystemFailed; // a system dialog failed: the browser from now on
    SDL_AtomicInt sTakeOver;     // ... and takes over the request it failed on

    // The frame Esc closed the browser on: the overlay does not close on the
    // same press.
    int sEscapeFrame = -1;

    // --- paths ----------------------------------------------------------------------

    std::string Slashes(const char* path)
    {
        std::string s = path != NULL ? path : "";
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '\\')
            {
                s[i] = '/';
            }
        }
        return s;
    }

    // Where the path's top folder ends: 1 for "/x", 3 for "C:/x", 0 for a
    // relative path.
    size_t TopLength(const std::string& p)
    {
        if (p.size() >= 3 && p[1] == ':' && p[2] == '/')
        {
            return 3;
        }
        if (!p.empty() && p[0] == '/')
        {
            return 1;
        }
        return 0;
    }

    // No trailing '/' except on a top folder.
    std::string Tidy(const char* path)
    {
        std::string p = Slashes(path);
        if (p.size() == 2 && p[1] == ':')
        {
            p += '/';
        }
        const size_t top = TopLength(p);
        while (p.size() > top && p.size() > 1 && p[p.size() - 1] == '/')
        {
            p.erase(p.size() - 1);
        }
        return p;
    }

    std::string Top(const std::string& p)
    {
        const size_t n = TopLength(p);
        return n > 0 ? p.substr(0, n) : std::string("/");
    }

    bool IsTop(const std::string& p)
    {
        const size_t n = TopLength(p);
        return n > 0 && p.size() == n;
    }

    // The folder holding `p`; FALSE at the top.
    bool Parent(const std::string& p, std::string* out)
    {
        if (IsTop(p) || p.empty())
        {
            return false;
        }
        const size_t slash = p.rfind('/');
        if (slash == std::string::npos)
        {
            return false;
        }
        const size_t top = TopLength(p);
        *out = slash < top ? p.substr(0, top) : p.substr(0, slash > 0 ? slash : 1);
        if (out->empty())
        {
            *out = "/";
        }
        return true;
    }

    std::string Join(const std::string& dir, const char* name)
    {
        if (!dir.empty() && dir[dir.size() - 1] == '/')
        {
            return dir + name;
        }
        return dir + "/" + name;
    }

    const char* Leaf(const std::string& p)
    {
        const size_t slash = p.rfind('/');
        return slash == std::string::npos ? p.c_str() : p.c_str() + slash + 1;
    }

    bool IsDir(const std::string& p)
    {
        iHostFileInfo info;
        return iHostStat(p.c_str(), &info) && info.is_dir;
    }

    // Makes every folder of `path` that is missing.
    void MakeDirs(const std::string& path)
    {
        const size_t top = TopLength(path);
        for (size_t i = top + 1; i <= path.size(); i++)
        {
            if (i == path.size() || path[i] == '/')
            {
                iHostMakeDir(path.substr(0, i).c_str());
            }
        }
    }

    // --- the request and the listing ---------------------------------------------------

    struct Entry
    {
        std::string name;
        bool dir;
        U64 size;
        S64 mtime;
    };

    struct Request
    {
        iGui::PickMode mode;
        std::string start;
        std::string filterName;
        std::vector<std::string> exts; // lower case, no dot; empty takes every file
        bool canShowAll;               // the dialog offered "All files" as well
    };

    Request sRemembered;

    struct Browser
    {
        bool active;     // a request is being answered here
        bool openPopup;  // OpenPopup at the next draw
        Request req;
        bool showAll;

        std::string dir;
        std::string startDir;
        std::vector<std::string> back; // the folders before this one
        std::vector<Entry> entries;
        std::string error;

        int focused;           // the row the cursor is on, -1 for none
        int focusNext;         // the row to put the cursor on at the next draw
        bool resetScroll;      // a new folder: the list back at its top
        bool dragArmed;        // pressed on the list's rows: a drag scrolls
        bool dragging;         // a finger or the mouse is scrolling the list
        float dragScroll;      // the list's scroll when that drag began
    };

    Browser sB;

    Request MakeRequest(iGui::PickMode mode, const char* start, const iGui::FileFilter* filters,
                        int count)
    {
        Request r;
        r.mode = mode;
        r.start = start != NULL ? start : "";
        r.canShowAll = false;

        // The first filter is what the list shows; a later "*" is the "show
        // every file" check box.
        for (int i = 0; filters != NULL && i < count; i++)
        {
            const char* pattern = filters[i].pattern != NULL ? filters[i].pattern : "*";
            if (strcmp(pattern, "*") == 0)
            {
                if (i > 0)
                {
                    r.canShowAll = true;
                }
                continue;
            }
            if (i > 0)
            {
                continue;
            }
            r.filterName = filters[i].name != NULL ? filters[i].name : "";
            std::string ext;
            for (const char* c = pattern;; c++)
            {
                if (*c == ';' || *c == '\0')
                {
                    if (!ext.empty())
                    {
                        r.exts.push_back(ext);
                    }
                    ext.clear();
                    if (*c == '\0')
                    {
                        break;
                    }
                }
                else if (*c != '.' && *c != '*')
                {
                    ext += (char)SDL_tolower((unsigned char)*c);
                }
            }
        }
        return r;
    }

    bool Takes(const char* name)
    {
        if (sB.showAll || sB.req.exts.empty())
        {
            return true;
        }
        const char* dot = strrchr(name, '.');
        if (dot == NULL)
        {
            return false;
        }
        for (size_t i = 0; i < sB.req.exts.size(); i++)
        {
            if (SDL_strcasecmp(dot + 1, sB.req.exts[i].c_str()) == 0)
            {
                return true;
            }
        }
        return false;
    }

    bool EntryBefore(const Entry& a, const Entry& b)
    {
        if (a.dir != b.dir)
        {
            return a.dir;
        }
        const int c = SDL_strcasecmp(a.name.c_str(), b.name.c_str());
        return c != 0 ? c < 0 : a.name < b.name;
    }

    void List()
    {
        sB.entries.clear();
        sB.error.clear();

        iHostDir* d = iHostDirOpen(sB.dir.c_str());
        if (d == NULL)
        {
            sB.error = "This folder cannot be opened.";
            return;
        }
        for (const char* name = iHostDirNext(d); name != NULL; name = iHostDirNext(d))
        {
            if (name[0] == '.' || name[0] == '\0')
            {
                continue; // hidden, as the system's dialogs hide them
            }
            Entry e;
            e.name = name;
            iHostFileInfo info;
            if (!iHostStat(Join(sB.dir, name).c_str(), &info))
            {
                continue;
            }
            e.dir = info.is_dir;
            e.size = info.size;
            e.mtime = info.mtime;
            if (e.dir || (sB.req.mode == iGui::PICK_OPEN_FILE && info.is_file && Takes(name)))
            {
                sB.entries.push_back(e);
            }
        }
        iHostDirClose(d);
        std::sort(sB.entries.begin(), sB.entries.end(), EntryBefore);
    }

    // Shows `dir`, with the cursor on the row called `select` if there is
    // one (the folder just come up out of), else the first.
    void Go(const std::string& dir, bool remember, const char* select)
    {
        if (remember && dir != sB.dir)
        {
            sB.back.push_back(sB.dir);
        }
        sB.dir = dir;
        List();
        sB.focusNext = sB.entries.empty() ? -1 : 0;
        for (size_t i = 0; select != NULL && i < sB.entries.size(); i++)
        {
            if (sB.entries[i].name == select)
            {
                sB.focusNext = (int)i;
            }
        }
        sB.focused = -1;
        sB.resetScroll = true;
    }

    void Up()
    {
        std::string parent;
        if (Parent(sB.dir, &parent))
        {
            const std::string from = Leaf(sB.dir);
            Go(parent, true, from.c_str());
        }
    }

    void Finish(const char* path)
    {
        printf("tssm: gui: file picker: %s\n", path != NULL ? path : "cancelled");
        fflush(stdout);
        sB.active = false;
        sB.openPopup = false;
        iGui::AnswerPick(path);
        ImGui::CloseCurrentPopup();
    }

    // The back button: the folder before this one, or Cancel in the first.
    void Back()
    {
        if (sB.back.empty())
        {
            Finish(NULL);
            return;
        }
        const std::string from = sB.dir;
        const std::string to = sB.back.back();
        sB.back.pop_back();
        // Coming back up out of a folder: the cursor goes back onto it.
        std::string parent;
        const bool up = Parent(from, &parent) && parent == to;
        Go(to, false, up ? Leaf(from) : NULL);
    }

    // The first folder that exists on the way up from `path`.
    std::string Existing(const std::string& path)
    {
        std::string p = path;
        while (!p.empty() && !IsDir(p))
        {
            std::string parent;
            if (!Parent(p, &parent))
            {
                return std::string();
            }
            p = parent;
        }
        return p;
    }

    void Open(const Request& req)
    {
        sB.active = true;
        sB.openPopup = true;
        sB.req = req;
        sB.showAll = false;
        sB.back.clear();
        sB.dragging = false;
        sB.dragArmed = false;

        // Where the caller says (a setting's folder: the nearest folder on the
        // way up from it that is there); with nothing said, a file to open
        // starts in the import folder, which is where the player is told to
        // put saves, and a folder in the port's own folder.
        std::string start;
        if (!req.start.empty())
        {
            start = Existing(Tidy(req.start.c_str()));
        }
        if (start.empty())
        {
            char dir[1024];
            iGui::TransferDir(req.mode == iGui::PICK_OPEN_FILE ? "import" : "", dir, sizeof(dir));
            start = Tidy(dir);
        }
        if (!IsDir(start))
        {
            start = Top(start);
        }
        sB.startDir = start;
        sB.dir = start;
        List();
        sB.focusNext = sB.entries.empty() ? -1 : 0;
        sB.focused = -1;
        sB.resetScroll = true;
    }

    // --- drawing ---------------------------------------------------------------------------

    // A controller button's label on the pad in hand ("A", "Cross", "B" on a
    // Switch pad's bottom button), or NULL with no controller.
    const char* PadLabel(SDL_GamepadButton button)
    {
        // TSSM_GUI_PAD_HINTS=switch (or xbox, playstation): the hints for
        // that controller with none plugged in, for a capture.
        const char* forced = getenv("TSSM_GUI_PAD_HINTS");
        if (forced != NULL && forced[0] != '\0')
        {
            return iPadInputLabel(iPadInputFromSDLButton(button), forced);
        }
        // The Switch (or a PC labelled as one) names its own buttons, pad or
        // none: its handheld controls are always there.
        if (iPadSwitchLabels())
        {
            return iPadInputLabel(iPadInputFromSDLButton(button), "switch");
        }
        if (!SDL_HasGamepad())
        {
            return NULL;
        }
        return iPadInputLabel(iPadInputFromSDLButton(button), iPadHostPadKind());
    }

    void Size(U64 bytes, char* out, size_t size)
    {
        if (bytes < 1024)
        {
            snprintf(out, size, "%u bytes", (unsigned)bytes);
        }
        else if (bytes < 1024 * 1024)
        {
            snprintf(out, size, "%.1f KB", bytes / 1024.0);
        }
        else
        {
            snprintf(out, size, "%.1f MB", bytes / (1024.0 * 1024.0));
        }
    }

    // `text`, cut in the middle with "..." to fit `width`: a long file name
    // keeps its start and its extension.
    std::string Fit(const std::string& text, float width)
    {
        if (ImGui::CalcTextSize(text.c_str()).x <= width)
        {
            return text;
        }
        size_t keepEnd = text.size() / 3;
        if (keepEnd > 12)
        {
            keepEnd = 12;
        }
        const std::string tail = text.substr(text.size() - keepEnd);
        size_t lo = 0;
        size_t hi = text.size() - keepEnd;
        while (lo < hi)
        {
            const size_t mid = (lo + hi + 1) / 2;
            const std::string s = text.substr(0, mid) + "..." + tail;
            if (ImGui::CalcTextSize(s.c_str()).x <= width)
            {
                lo = mid;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return text.substr(0, lo) + "..." + tail;
    }

    void Icon(ImDrawList* dl, ImVec2 at, float size, bool dir)
    {
        const float w = size;
        const float h = size * 0.78f;
        const ImVec2 o(at.x, at.y + (size - h) * 0.5f);
        if (dir)
        {
            const ImU32 c = IM_COL32(245, 200, 80, 255);
            dl->AddRectFilled(ImVec2(o.x, o.y), ImVec2(o.x + w * 0.45f, o.y + h * 0.3f), c, 2.0f);
            dl->AddRectFilled(ImVec2(o.x, o.y + h * 0.18f), ImVec2(o.x + w, o.y + h), c, 2.0f);
        }
        else
        {
            const ImU32 c = IM_COL32(200, 215, 235, 255);
            const float fw = w * 0.72f;
            const ImVec2 f(o.x + (w - fw) * 0.5f, at.y);
            dl->AddRect(f, ImVec2(f.x + fw, f.y + size), c, 2.0f, 1.5f);
            for (int i = 1; i <= 3; i++)
            {
                const float y = f.y + size * (0.25f * i);
                dl->AddLine(ImVec2(f.x + fw * 0.22f, y), ImVec2(f.x + fw * 0.78f, y), c, 1.0f);
            }
        }
    }

    // One "<button> what" pair of the hint line.
    void Hint(const char* button, const char* what, bool first = false)
    {
        if (button == NULL)
        {
            return;
        }
        if (!first)
        {
            ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.2f);
        }
        ImGui::TextColored(kYellow, "%s", button);
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.35f);
        ImGui::TextDisabled("%s", what);
    }

    void DrawList(float height)
    {
        const ImGuiStyle& style = ImGui::GetStyle();

        ImGui::BeginChild("list", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
        if (sB.resetScroll)
        {
            ImGui::SetScrollY(0.0f);
            sB.resetScroll = false;
        }

        // A finger (or the mouse) dragged on the list scrolls it, as a phone's
        // list does; the row under it is not chosen when it lets go.
        const bool wasDragging = sB.dragging;
        // A press on the scroll bar is the scroll bar's own.
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            sB.dragScroll = ImGui::GetScrollY();
            sB.dragArmed = ImGui::GetMousePos().x <
                           ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - style.ScrollbarSize;
        }
        if (sB.dragArmed && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, ImGui::GetFontSize() * 0.6f))
        {
            sB.dragging = true;
        }
        if (sB.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImGui::SetScrollY(sB.dragScroll - ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            sB.dragging = false;
            sB.dragArmed = false;
        }

        if (!sB.error.empty())
        {
            ImGui::TextColored(kRed, "%s", sB.error.c_str());
        }
        else if (sB.entries.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            if (sB.req.mode == iGui::PICK_OPEN_FILE)
            {
                ImGui::TextDisabled("Nothing here that can be imported.");
                char dir[1024];
                iGui::TransferDir("import", dir, sizeof(dir));
                ImGui::TextDisabled("Put the saves (.gci, .gcs, .raw, .gcp or a .zip of saves) in "
                                    "%s/ on the %s, then choose them here.",
                                    dir,
#ifdef __SWITCH__
                                    "SD card"
#else
                                    "computer"
#endif
                );
            }
            else
            {
                ImGui::TextDisabled("No folders in here.");
            }
            ImGui::PopTextWrapPos();
        }

        const float rowH = ImGui::GetFrameHeight() * 1.3f;
        const float icon = ImGui::GetFontSize() * 1.1f;
        const float infoW = ImGui::CalcTextSize("0000-00-00 00:00   000.0 KB").x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int chosen = -1;

        ImGuiListClipper clipper;
        clipper.Begin((int)sB.entries.size(), rowH + style.ItemSpacing.y);
        if (sB.focusNext >= 0 && sB.focusNext < (int)sB.entries.size())
        {
            clipper.IncludeItemByIndex(sB.focusNext);
        }
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                const Entry& e = sB.entries[(size_t)i];
                ImGui::PushID(i);
                if (i == sB.focusNext)
                {
                    ImGui::SetKeyboardFocusHere();
                    // With a controller in hand the cursor shows at once, so
                    // the first press of the accept button acts even when the
                    // picker was opened by a tap or a click.
                    if (PadLabel(SDL_GAMEPAD_BUTTON_SOUTH) != NULL)
                    {
                        ImGui::SetNavCursorVisible(true);
                    }
                }
                const ImVec2 at = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##row", i == sB.focused, 0, ImVec2(0.0f, rowH)) &&
                    !wasDragging && !sB.dragging)
                {
                    chosen = i;
                }
                if (ImGui::IsItemFocused())
                {
                    sB.focused = i;
                }
                if (i == sB.focusNext)
                {
                    ImGui::SetScrollHereY(0.5f);
                    sB.focusNext = -1;
                }

                const float width = ImGui::GetItemRectSize().x;
                const float textY = at.y + (rowH - ImGui::GetFontSize()) * 0.5f;
                Icon(dl, ImVec2(at.x + style.FramePadding.x, textY), icon, e.dir);
                const float nameX = at.x + style.FramePadding.x * 2.0f + icon;
                const bool info = !e.dir && width > infoW * 2.0f;
                const float nameW = width - (nameX - at.x) - (info ? infoW + style.ItemSpacing.x : 0.0f) -
                                    style.FramePadding.x;
                const std::string shown = Fit(e.dir ? e.name + "/" : e.name, nameW);
                dl->AddText(ImVec2(nameX, textY), ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());
                if (info)
                {
                    char line[64];
                    char size[24];
                    Size(e.size, size, sizeof(size));
                    if (e.mtime > 0)
                    {
                        iHostCalendar c;
                        iHostLocalTimeOf(e.mtime, &c);
                        snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d   %s",
                                 (int)c.year + 1900, (int)c.mon + 1, (int)c.mday, (int)c.hour,
                                 (int)c.min, size);
                    }
                    else
                    {
                        snprintf(line, sizeof(line), "%s", size);
                    }
                    const float w = ImGui::CalcTextSize(line).x;
                    dl->AddText(ImVec2(at.x + width - w - style.FramePadding.x, textY),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), line);
                }
                ImGui::PopID();
            }
        }
        clipper.End();

        // Shoulder buttons and Page Up/Down move the cursor a page at a time.
        const int page = (int)(height / (rowH + style.ItemSpacing.y)) - 1;
        if (sB.focused >= 0 && page > 0 && !sB.entries.empty())
        {
            int to = sB.focused;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1) || ImGui::IsKeyPressed(ImGuiKey_PageUp))
            {
                to -= page;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1) || ImGui::IsKeyPressed(ImGuiKey_PageDown))
            {
                to += page;
            }
            to = to < 0 ? 0 : (to >= (int)sB.entries.size() ? (int)sB.entries.size() - 1 : to);
            if (to != sB.focused)
            {
                sB.focusNext = to;
            }
        }

        ImGui::EndChild();

        if (chosen >= 0)
        {
            const Entry e = sB.entries[(size_t)chosen];
            const std::string path = Join(sB.dir, e.name.c_str());
            if (e.dir)
            {
                Go(path, true, NULL);
            }
            else
            {
                Finish(path.c_str());
            }
        }
    }
} // namespace

// --- which picker -----------------------------------------------------------------------

bool iGui::UseBuiltinPicker()
{
#ifdef __SWITCH__
    return true;
#else
    const char* env = getenv("TSSM_FILE_PICKER");
    const char* how = (env != NULL && env[0] != '\0') ? env : iConfigGetString("gui.file_picker", "auto");
    if (SDL_strcasecmp(how, "builtin") == 0)
    {
        return true;
    }
    if (SDL_strcasecmp(how, "system") == 0)
    {
        return false;
    }
    return SDL_GetAtomicInt(&sSystemFailed) != 0;
#endif
}

void iGui::NoteSystemDialogFailed()
{
    // A failed folder dialog takes over only its own request: Android's SDL
    // has file dialogs but no folder dialog, and its Import and Export keep
    // the system's file dialogs. A failed file dialog means none work.
    // (sRemembered was written before the dialog was asked for.)
    if (sRemembered.mode != PICK_FOLDER)
    {
        SDL_SetAtomicInt(&sSystemFailed, 1);
    }
    SDL_SetAtomicInt(&sTakeOver, 1);
}

void iGui::TransferDir(const char* which, char* out, size_t size)
{
    std::string base;
    const char* env = getenv("TSSM_FILE_PICKER_DIR");
    if (env != NULL && env[0] != '\0')
    {
        base = Tidy(env);
    }
    else
    {
        char data[512];
        if (iHostUserDataDir(data, sizeof(data)))
        {
            base = Tidy(data) + "/tssm";
        }
        else
        {
            base = ".";
        }
    }
    const std::string dir = (which != NULL && which[0] != '\0') ? base + "/" + which : base;

    // Made once a run (the Saves tab asks every frame), and again should it
    // have gone.
    static std::vector<std::string> sMade;
    if (std::find(sMade.begin(), sMade.end(), dir) == sMade.end() || !IsDir(dir))
    {
        MakeDirs(dir);
        if (std::find(sMade.begin(), sMade.end(), dir) == sMade.end())
        {
            sMade.push_back(dir);
        }
    }
    snprintf(out, size, "%s", dir.c_str());
}

void iGui::UniquePath(const char* dir, const char* name, char* out, size_t size)
{
    const std::string d = Tidy(dir);
    const std::string n = name != NULL && name[0] != '\0' ? name : "file";
    const size_t dot = n.rfind('.');
    const std::string stem = dot != std::string::npos && dot > 0 ? n.substr(0, dot) : n;
    const std::string ext = dot != std::string::npos && dot > 0 ? n.substr(dot) : std::string();

    std::string path = Join(d, n.c_str());
    for (int i = 2; iHostPathExists(path.c_str()) && i < 1000; i++)
    {
        char suffix[16];
        snprintf(suffix, sizeof(suffix), "-%d", i);
        path = Join(d, (stem + suffix + ext).c_str());
    }
    snprintf(out, size, "%s", path.c_str());
}

void iGui::FilePickerRemember(PickMode mode, const char* start, const FileFilter* filters, int count)
{
    sRemembered = MakeRequest(mode, start, filters, count);
}

void iGui::FilePickerStart(PickMode mode, const char* start, const FileFilter* filters, int count)
{
    const Request req = MakeRequest(mode, start, filters, count);
    if (mode == PICK_SAVE_FILE)
    {
        // Nothing to ask: the export folder, under the name suggested.
        char dir[1024];
        char path[1200];
        TransferDir("export", dir, sizeof(dir));
        const char* name = start != NULL ? start : "";
        for (const char* c = name; *c != '\0'; c++)
        {
            if (*c == '/' || *c == '\\')
            {
                name = c + 1;
            }
        }
        UniquePath(dir, name, path, sizeof(path));
        printf("tssm: gui: file picker: saving to %s\n", path);
        fflush(stdout);
        AnswerPick(path);
        return;
    }
    Open(req);
}

bool iGui::FilePickerTookEscape()
{
    return sEscapeFrame == ImGui::GetFrameCount();
}

void iGui::DrawFilePicker()
{
    if (SDL_GetAtomicInt(&sTakeOver) != 0)
    {
        SDL_SetAtomicInt(&sTakeOver, 0);
        printf("tssm: gui: the system's file dialog is not available; using the built-in one\n");
        fflush(stdout);
        if (sRemembered.mode == PICK_SAVE_FILE)
        {
            FilePickerStart(PICK_SAVE_FILE, sRemembered.start.c_str(), NULL, 0);
        }
        else
        {
            Open(sRemembered);
        }
    }

    if (!sB.active)
    {
        return;
    }
    if (sB.openPopup)
    {
        ImGui::OpenPopup(kPopupId);
        sB.openPopup = false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    ImVec2 size(io.DisplaySize.x * 0.94f, io.DisplaySize.y * 0.9f);
    if (size.x > fs * 62.0f)
    {
        size.x = fs * 62.0f;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    char title[160];
    snprintf(title, sizeof(title), "%s%s",
             sB.req.mode == PICK_FOLDER ? "Choose a folder" : "Choose a file to import", kPopupId);
    if (!ImGui::BeginPopupModal(title, NULL,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        // Not up (the overlay was closed under it, say): ask again, the
        // request is still waiting for its answer.
        sB.openPopup = true;
        return;
    }

    // --- where ---
    const bool atTop = IsTop(sB.dir);
    ImGui::BeginDisabled(atTop);
    if (ImGui::Button("Up"))
    {
        Up();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
#ifdef __SWITCH__
    const char* topLabel = "SD card";
#else
    const char* topLabel = "Top";
#endif
    ImGui::BeginDisabled(atTop);
    if (ImGui::Button(topLabel))
    {
        Go(Top(sB.dir), true, NULL);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sB.dir == sB.startDir);
    if (ImGui::Button(sB.req.mode == PICK_OPEN_FILE ? "Import folder" : "Where it started"))
    {
        Go(sB.startDir, true, NULL);
    }
    ImGui::EndDisabled();
    if (sB.req.mode == PICK_OPEN_FILE && sB.req.canShowAll)
    {
        ImGui::SameLine();
        if (ImGui::Checkbox("Every file", &sB.showAll))
        {
            Go(sB.dir, false, NULL);
        }
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(sB.dir.c_str());
    if (sB.req.mode == PICK_OPEN_FILE && !sB.req.filterName.empty())
    {
        std::string exts;
        for (size_t i = 0; i < sB.req.exts.size(); i++)
        {
            exts += (i > 0 ? " ." : ".") + sB.req.exts[i];
        }
        ImGui::TextDisabled("Showing %s (%s)%s", sB.req.filterName.c_str(), exts.c_str(),
                            sB.showAll ? ", and every other file" : "");
    }
    ImGui::PopTextWrapPos();

    // --- the list, and below it the row it is on and the buttons ---
    // Below the list: the row's whole name, the buttons, the hint line.
    const float below = ImGui::GetTextLineHeightWithSpacing() * 2.0f +
                        ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    float listH = ImGui::GetContentRegionAvail().y - below;
    if (listH < ImGui::GetFrameHeight() * 4.0f)
    {
        listH = ImGui::GetFrameHeight() * 4.0f;
    }
    DrawList(listH);
    if (!sB.active)
    {
        ImGui::EndPopup();
        return;
    }

    // The whole name of the row the cursor is on: the list cuts long ones.
    const Entry* at = (sB.focused >= 0 && sB.focused < (int)sB.entries.size()) ? &sB.entries[(size_t)sB.focused] : NULL;
    ImGui::PushTextWrapPos(0.0f);
    if (at != NULL)
    {
        ImGui::Text("%s%s", at->name.c_str(), at->dir ? "/" : "");
    }
    else
    {
        ImGui::TextDisabled(" ");
    }
    ImGui::PopTextWrapPos();

    // --- buttons ---
    const char* backWhat = sB.back.empty() ? "Cancel" : "Back";
    bool choose = false;
    bool back = false;
    if (sB.req.mode == PICK_FOLDER)
    {
        if (ImGui::Button("Choose this folder"))
        {
            choose = true;
        }
        ImGui::SameLine();
    }
    else if (at != NULL)
    {
        if (ImGui::Button(at->dir ? "Open" : "Import this file"))
        {
            sB.focusNext = -1;
            const std::string path = Join(sB.dir, at->name.c_str());
            if (at->dir)
            {
                Go(path, true, NULL);
            }
            else
            {
                Finish(path.c_str());
                ImGui::EndPopup();
                return;
            }
        }
        ImGui::SameLine();
    }
    if (!sB.back.empty() && ImGui::Button("Back"))
    {
        back = true;
    }
    if (!sB.back.empty())
    {
        ImGui::SameLine();
    }
    const bool cancel = ImGui::Button("Cancel");

    // What the controller's (or keyboard's) buttons do here.
    const char* accept = PadLabel(SDL_GAMEPAD_BUTTON_SOUTH);
    if (accept != NULL)
    {
        Hint(accept, sB.req.mode == PICK_FOLDER ? "Open" : "Open / choose", true);
        Hint(PadLabel(SDL_GAMEPAD_BUTTON_EAST), backWhat);
        Hint(PadLabel(SDL_GAMEPAD_BUTTON_NORTH), "Up");
        if (sB.req.mode == PICK_FOLDER)
        {
            Hint(PadLabel(SDL_GAMEPAD_BUTTON_WEST), "Choose this folder");
        }
        Hint("L/R", "Page");
    }
    else
    {
        Hint("Enter", sB.req.mode == PICK_FOLDER ? "Open" : "Open / choose", true);
        Hint("Backspace", backWhat);
        Hint("Esc", "Cancel");
    }

    // The controller's and the keyboard's own presses, while nothing in the
    // window is being typed into.
    if (!ImGui::IsAnyItemActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        {
            back = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false))
        {
            Up();
        }
        if (sB.req.mode == PICK_FOLDER && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false))
        {
            choose = true;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        sEscapeFrame = ImGui::GetFrameCount();
        Finish(NULL);
    }
    else if (cancel)
    {
        Finish(NULL);
    }
    else if (choose)
    {
        Finish(sB.dir.c_str());
    }
    else if (back)
    {
        Back();
    }
    ImGui::EndPopup();
}
