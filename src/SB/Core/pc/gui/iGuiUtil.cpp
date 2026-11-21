// What the GUI's files share: the look, the small widgets, the folder picker,
// the check of the game's folder and the PNG writer. See iGuiInternal.h.

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iHost.h"
#include "iSaveStore.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include "android/iAndroidDocument.h"
#endif

namespace
{
    SDL_Window* sWindow;
    bool sInLauncher;

    ImGuiStyle sBaseStyle;

    // The system dialog's answer (a folder, a file to open or a file to save
    // to), written by SDL's callback -- on another thread on some systems --
    // and read by the frame. One dialog at a time.
    SDL_Mutex* sPickLock;
    char sPickTag[64];
    char sPickResult[1024];
    bool sPicking;
    bool sPickReady;

    void SDLCALL OnFolderPicked(void*, const char* const* files, int)
    {
        SDL_LockMutex(sPickLock);
        if (files == NULL)
        {
            printf("tssm: gui: the system's file dialog failed: %s\n", SDL_GetError());
            fflush(stdout);
            sPickResult[0] = '\0';
        }
        else if (files[0] == NULL)
        {
            sPickResult[0] = '\0'; // cancelled
        }
        else
        {
            snprintf(sPickResult, sizeof(sPickResult), "%s", files[0]);
            for (char* c = sPickResult; *c != '\0'; c++)
            {
                if (*c == '\\')
                {
                    *c = '/';
                }
            }
        }
        sPicking = false;
        sPickReady = true;
        SDL_UnlockMutex(sPickLock);
    }

    bool SameNoCase(const char* a, const char* b)
    {
        return SDL_strcasecmp(a, b) == 0;
    }

    // Whether `dir` has a file called `name`, in any case: a real Xbox
    // extraction has font.HIP where the game asks for FONT.HIP.
    bool HasFile(const char* dir, const char* name, char* found, size_t foundSize)
    {
        iHostDir* d = iHostDirOpen(dir);
        if (d == NULL)
        {
            return false;
        }

        bool has = false;
        for (const char* entry = iHostDirNext(d); entry != NULL; entry = iHostDirNext(d))
        {
            if (SameNoCase(entry, name))
            {
                if (found != NULL)
                {
                    snprintf(found, foundSize, "%s", entry);
                }
                has = true;
                break;
            }
        }
        iHostDirClose(d);
        return has;
    }

    // The two-letter platform id out of a package's PLAT chunk. The same walk
    // iFile.cpp does (HIPA, then PACK, then PLAT, big-endian chunk sizes),
    // repeated here because the launcher runs before iFileInit and asks about
    // folders that are not the configured one.
    bool HipPlatform(const char* path, char* id, size_t idSize)
    {
        id[0] = '\0';

        FILE* fp = fopen(path, "rb");
        if (fp == NULL)
        {
            return false;
        }

        unsigned char head[512];
        size_t got = fread(head, 1, sizeof(head), fp);
        fclose(fp);

        if (got < 16 || memcmp(head, "HIPA", 4) != 0)
        {
            return false;
        }

        size_t o = 8;
        size_t end = got;
        for (int depth = 0; depth < 2; depth++)
        {
            bool descended = false;
            while (o + 8 <= end)
            {
                const unsigned char* c = head + o;
                size_t size = ((size_t)c[4] << 24) | ((size_t)c[5] << 16) | ((size_t)c[6] << 8) | c[7];
                size_t body = o + 8;

                if (memcmp(c, "PACK", 4) == 0)
                {
                    o = body;
                    end = (body + size < got) ? body + size : got;
                    descended = true;
                    break;
                }
                if (memcmp(c, "PLAT", 4) == 0)
                {
                    size_t stop = (body + size < got) ? body + size : got;
                    snprintf(id, idSize, "%.*s", (int)(stop - body), (const char*)head + body);
                    return id[0] != '\0';
                }
                if (size == 0)
                {
                    break;
                }
                o = body + size;
            }
            if (!descended)
            {
                break;
            }
        }
        return false;
    }

    // --- PNG ----------------------------------------------------------------------

    unsigned int sCrc[256];
    bool sCrcReady;

    unsigned int Crc(unsigned int crc, const unsigned char* p, size_t n)
    {
        if (!sCrcReady)
        {
            for (unsigned int i = 0; i < 256; i++)
            {
                unsigned int c = i;
                for (int k = 0; k < 8; k++)
                {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                sCrc[i] = c;
            }
            sCrcReady = true;
        }
        crc = ~crc;
        for (size_t i = 0; i < n; i++)
        {
            crc = sCrc[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }

    void PutBE(unsigned char* p, unsigned int v)
    {
        p[0] = (unsigned char)(v >> 24);
        p[1] = (unsigned char)(v >> 16);
        p[2] = (unsigned char)(v >> 8);
        p[3] = (unsigned char)v;
    }

    bool Chunk(FILE* f, const char* type, const unsigned char* data, unsigned int len)
    {
        unsigned char head[8];
        PutBE(head, len);
        memcpy(head + 4, type, 4);
        unsigned int crc = Crc(0, head + 4, 4);
        crc = Crc(crc, data, len);
        unsigned char tail[4];
        PutBE(tail, crc);
        return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
               fwrite(tail, 1, 4, f) == 4;
    }
} // namespace

// --- where the GUI is drawing ---------------------------------------------------------

SDL_Window* iGui::Window()
{
    return sWindow;
}

void iGui::SetWindow(SDL_Window* window)
{
    sWindow = window;
}

bool iGui::InLauncher()
{
    return sInLauncher;
}

void iGui::SetInLauncher(bool on)
{
    sInLauncher = on;
}

void iGui::UseOpenGamepads()
{
    SDL_Gamepad* pads[8];
    int count = 0;

    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    if (ids != NULL)
    {
        for (int i = 0; i < n && count < 8; i++)
        {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
            if (pad != NULL)
            {
                pads[count++] = pad;
            }
        }
        SDL_free(ids);
    }

    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, pads, count);
}

bool iGui::SaveFolderInUse(U32* holder)
{
    if (holder != NULL)
    {
        *holder = 0;
    }
    if (!InLauncher())
    {
        // In the game, this copy is the one that holds it.
        return false;
    }

    // What the game will set at startup (iSystem.cpp's ApplyConfig, through
    // iSGSetSaveRoot), so the tab shows the folder the game will use.
    static char sApplied[1024] = "\x01";
    static Uint64 sAskedAt;
    static bool sInUse;
    static U32 sHolder;
    const char* folder = iConfigGetString("game.save_folder", "");
    const Uint64 now = SDL_GetTicks();
    bool ask = sAskedAt == 0 || now - sAskedAt >= 1000;
    if (strcmp(sApplied, folder) != 0)
    {
        snprintf(sApplied, sizeof(sApplied), "%s", folder);
        iSaveStoreSetConfigRoot(folder);
        ask = true;
    }

    // Once a second, not every frame: on Linux and macOS the question takes
    // the folder's lock for a moment, which a copy starting at that moment
    // would take for the folder being in use.
    if (ask)
    {
        sAskedAt = now != 0 ? now : 1;
        sInUse = iSaveStoreFolderHolder(&sHolder);
    }
    if (holder != NULL)
    {
        *holder = sInUse ? sHolder : 0;
    }
    return sInUse;
}

// --- look -----------------------------------------------------------------------------

float iGui::AutoScale(SDL_Window* window)
{
    const char* setting = iConfigGetString("gui.scale", "auto");
    if (setting != NULL && SDL_strcasecmp(setting, "auto") != 0)
    {
        float fixed = (float)atof(setting);
        if (fixed >= 0.5f && fixed <= 4.0f)
        {
            return fixed;
        }
    }

    if (window == NULL)
    {
        return 1.0f;
    }

    // The display's own scaling, in the window's coordinates: on a system that
    // hands out pixels (Windows) that is the whole 150% or 200%; on one that
    // hands out points (macOS) the points already carry it.
    float display = SDL_GetWindowDisplayScale(window);
    float density = SDL_GetWindowPixelDensity(window);
    if (display <= 0.0f)
    {
        display = 1.0f;
    }
    if (density <= 0.0f)
    {
        density = 1.0f;
    }
    float scale = display / density;

    // A game is played further from the screen than a desktop is used at, and
    // a 4K television reports 100%. Never smaller than a 1080-line window
    // would make it.
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    const float byHeight = h / 1080.0f;
    if (byHeight > scale)
    {
        scale = byHeight;
    }

    if (scale < 1.0f)
    {
        scale = 1.0f;
    }

    // ...but never larger than the window can hold: the overlay is laid out
    // for about 900 x 620 at scale 1, and a small window on a high-DPI display
    // would otherwise get a window's worth of scroll bars. Shrinks below 1 for
    // a window smaller than that, down to where text is still legible.
    if (w > 0 && h > 0)
    {
        float fit = w / 900.0f;
        if (h / 620.0f < fit)
        {
            fit = h / 620.0f;
        }
        if (fit < scale)
        {
            scale = fit;
        }
    }
    if (scale < 0.75f)
    {
        scale = 0.75f;
    }
    if (scale > 4.0f)
    {
        scale = 4.0f;
    }

    // Steps of an eighth, so a window being dragged larger does not rebuild
    // the style on every frame of the drag.
    return (float)(int)(scale * 8.0f + 0.5f) / 8.0f;
}

void iGui::SetupContext()
{
    ImGuiIO& io = ImGui::GetIO();

    // Nothing written beside the game: window positions are fixed, and a
    // settings file of the GUI's own would be a second config.ini.
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
#ifdef __ANDROID__
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
#endif

    // The scalable default font, so text is drawn at its size rather than
    // scaled up from 13 pixels.
    io.Fonts->AddFontDefaultVector();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 16.0f;
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(10.0f, 7.0f);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
#ifdef __ANDROID__
    style.TouchExtraPadding = ImVec2(6.0f, 6.0f);
#endif

    // Deep water with a sponge-yellow accent.
    ImVec4* c = style.Colors;
    const ImVec4 yellow(0.96f, 0.82f, 0.25f, 1.00f);
    const ImVec4 yellowDim(0.80f, 0.66f, 0.16f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.10f, 0.17f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.12f, 0.20f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.36f, 0.48f, 0.60f);
    c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.18f, 0.29f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.24f, 0.38f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.21f, 0.32f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.30f, 0.44f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.36f, 0.52f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.11f, 0.30f, 0.46f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.40f, 0.60f, 1.00f);
    c[ImGuiCol_ButtonActive] = yellowDim;
    c[ImGuiCol_Header] = ImVec4(0.11f, 0.30f, 0.46f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.40f, 0.60f, 0.90f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.46f, 0.68f, 1.00f);
    c[ImGuiCol_Tab] = ImVec4(0.08f, 0.22f, 0.35f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.17f, 0.40f, 0.60f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.36f, 0.55f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = yellow;
    c[ImGuiCol_CheckMark] = yellow;
    c[ImGuiCol_SliderGrab] = yellowDim;
    c[ImGuiCol_SliderGrabActive] = yellow;
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.36f, 0.48f, 0.60f);
    c[ImGuiCol_NavCursor] = yellow;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.96f, 0.82f, 0.25f, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.03f, 0.08f, 0.55f);

    sBaseStyle = style;
}

void iGui::ApplyScale(float scale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = sBaseStyle;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

// --- small widgets --------------------------------------------------------------------

void iGui::HelpMarker(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool iGui::BackPressed()
{
#ifdef __SWITCH__
    // ImGui's own back button (the right face button) closes no modal, and
    // the Switch has no Esc.
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
    {
        return true;
    }
#endif
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

bool iGui::TouchWords()
{
    return ImGui::GetCurrentContext() != NULL &&
           (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) != 0;
}

void iGui::Note(const char* fmt, ...)
{
    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void iGui::RestartTag(const char* key)
{
    if (sInLauncher || !ChangedSinceStartup(key))
    {
        return;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f), "restart");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::TextUnformatted("Saved. Takes effect the next time the game starts.");
        ImGui::EndTooltip();
    }
}

// --- the game's files -----------------------------------------------------------------

namespace
{
    bool CheckAssetFolderNow(const char* dir, char* why, size_t whySize);

    // The check reads directories and a file header; the GUI asks it every
    // frame. A second's memory of the last few answers is plenty: a folder that
    // was just extracted into is seen a second later.
    struct CheckMemo
    {
        char dir[512];
        char why[512];
        bool ok;
        U64 at;
    };

    CheckMemo sMemo[4];
    S32 sMemoNext;
} // namespace

bool iGui::CheckAssetFolder(const char* dir, char* why, size_t whySize)
{
    const U64 now = SDL_GetTicks();
    const char* key = dir != NULL ? dir : "";
    for (S32 i = 0; i < 4; i++)
    {
        if (sMemo[i].at != 0 && now - sMemo[i].at < 1000 && strcmp(sMemo[i].dir, key) == 0)
        {
            snprintf(why, whySize, "%s", sMemo[i].why);
            return sMemo[i].ok;
        }
    }

    CheckMemo& m = sMemo[sMemoNext];
    sMemoNext = (sMemoNext + 1) % 4;
    snprintf(m.dir, sizeof(m.dir), "%s", key);
    m.ok = CheckAssetFolderNow(dir, m.why, sizeof(m.why));
    m.at = now != 0 ? now : 1;
    snprintf(why, whySize, "%s", m.why);
    return m.ok;
}

namespace
{
bool CheckAssetFolderNow(const char* dir, char* why, size_t whySize)
{
    if (dir == NULL || dir[0] == '\0')
    {
        snprintf(why, whySize, "No folder chosen yet.");
        return false;
    }

    iHostFileInfo info;
    if (!iHostStat(dir, &info) || !info.is_dir)
    {
        // The disc image kinds Xbox tools and Dolphin write.
        static const char* const kImages[] = { ".iso", ".xiso", ".rvz", ".gcm", ".gcz",
                                               ".wia", ".ciso", ".wbfs" };
        const size_t n = strlen(dir);
        bool image = false;
        for (size_t i = 0; i < sizeof(kImages) / sizeof(kImages[0]); i++)
        {
            const size_t k = strlen(kImages[i]);
            image = image || (n > k && SameNoCase(dir + n - k, kImages[i]));
        }
        if (image)
        {
            snprintf(why, whySize, "That is a disc image. Extract the disc (a GameCube disc "
                                   "with Dolphin: right-click the game, Extract Entire Disc) "
                                   "and choose the folder the files came out into.");
        }
        else
        {
            snprintf(why, whySize, "That folder does not exist.");
        }
        return false;
    }

    char boot[256];
    char font[256];
    bool haveBoot = HasFile(dir, "boot.HIP", boot, sizeof(boot));
    bool haveFont = HasFile(dir, "font.HIP", font, sizeof(font));

    // A GameCube disc extracted with Dolphin is files\ and sys\: the game reads
    // the files\ folder of it (iFile.cpp), so the folder above is fine too.
    char inFiles[1024] = "";
    char filesName[256];
    if ((!haveBoot || !haveFont) && HasFile(dir, "files", filesName, sizeof(filesName)))
    {
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", dir, filesName);
        if (HasFile(sub, "boot.HIP", boot, sizeof(boot)) && HasFile(sub, "font.HIP", font, sizeof(font)))
        {
            snprintf(inFiles, sizeof(inFiles), "%s", sub);
            haveBoot = haveFont = true;
        }
    }

    if (!haveBoot || !haveFont)
    {
        // The usual mistake: the folder ABOVE the game's files.
        iHostDir* d = iHostDirOpen(dir);
        if (d != NULL)
        {
            char sub[1024];
            for (const char* entry = iHostDirNext(d); entry != NULL; entry = iHostDirNext(d))
            {
                snprintf(sub, sizeof(sub), "%s/%s", dir, entry);
                if (HasFile(sub, "boot.HIP", NULL, 0) && HasFile(sub, "font.HIP", NULL, 0))
                {
                    snprintf(why, whySize,
                             "The game's files are one folder further in, in \"%s\". "
                             "Choose that folder.",
                             entry);
                    iHostDirClose(d);
                    return false;
                }
            }
            iHostDirClose(d);
        }

        snprintf(why, whySize,
                 "No %s here. Choose the folder that directly contains boot.HIP, font.HIP "
                 "and FMV, extracted from your own Xbox or GameCube disc.",
                 !haveBoot && !haveFont ? "boot.HIP or font.HIP" :
                 !haveBoot              ? "boot.HIP" :
                                          "font.HIP");
        return false;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", inFiles[0] != '\0' ? inFiles : dir, boot);
    char plat[16];
    const bool known = HipPlatform(path, plat, sizeof(plat));
    const bool gamecube = known && strcmp(plat, "GC") == 0;
    if (known && !gamecube && strcmp(plat, "BX") != 0 && strcmp(plat, "XB") != 0)
    {
        snprintf(why, whySize, "These are the %s release's files. The port reads the Xbox "
                               "and GameCube releases'.",
                 strcmp(plat, "PS2") == 0 ? "PlayStation 2" : plat);
        return false;
    }

    snprintf(why, whySize, "Found %s and %s%s from the %s release.", boot, font,
             inFiles[0] != '\0' ? " in files" : "", gamecube ? "GameCube" : "Xbox");
    return true;
}
} // namespace

// --- folders ----------------------------------------------------------------------------

namespace
{
    // Claims the one dialog there can be at a time for `tag`. FALSE while
    // another is still open.
    bool BeginPick(const char* tag)
    {
        if (sPickLock == NULL)
        {
            sPickLock = SDL_CreateMutex();
        }

        SDL_LockMutex(sPickLock);
        if (sPicking)
        {
            SDL_UnlockMutex(sPickLock);
            return false;
        }
        snprintf(sPickTag, sizeof(sPickTag), "%s", tag);
        sPicking = true;
        sPickReady = false;
        SDL_UnlockMutex(sPickLock);
        return true;
    }

    // The filters as SDL takes them. SDL keeps the array until the dialog is
    // answered, so it lives here rather than on the caller's stack.
    SDL_DialogFileFilter sFilters[8];

    int Filters(const iGui::FileFilter* filters, int count)
    {
        int n = 0;
        for (int i = 0; filters != NULL && i < count && n < 8; i++)
        {
            sFilters[n].name = filters[i].name;
            sFilters[n].pattern = filters[i].pattern;
            n++;
        }
        return n;
    }

    // Where a dialog starts, as the system's own dialog takes it. NULL for
    // nowhere in particular.
    //
    // Windows' dialog is given a whole path with backslashes: handed
    // config.ini's forward slashes, a relative path or a folder that is gone,
    // it refuses, and SDL then falls back to the old "Browse For Folder"
    // tree, which starts in the user's folder. SDL also takes what follows
    // the last separator as a file's name, so a folder ends in one. A folder
    // that is not there starts in the nearest one above it that is.
    const char* StartAt(const char* start, bool folder, char* out, size_t size)
    {
        if (start == NULL || start[0] == '\0')
        {
            return NULL;
        }

#ifdef _WIN32
        // A file's name by itself is only the name offered.
        if (!folder && strpbrk(start, "/\\") == NULL)
        {
            return start;
        }

        wchar_t wide[1024];
        wchar_t full[1024];
        if (MultiByteToWideChar(CP_UTF8, 0, start, -1, wide, 1024) <= 0)
        {
            return NULL;
        }
        DWORD n = GetFullPathNameW(wide, 1024, full, NULL);
        if (n == 0 || n >= 1000)
        {
            return NULL;
        }

        // The name is kept; its folder is what has to be there.
        wchar_t name[260] = L"";
        if (!folder)
        {
            wchar_t* slash = wcsrchr(full, L'\\');
            if (slash == NULL)
            {
                return NULL;
            }
            wcsncpy(name, slash + 1, 259);
            name[259] = L'\0';
            *slash = L'\0';
        }

        for (;;)
        {
            size_t len = wcslen(full);
            if (len > 0 && full[len - 1] == L'\\')
            {
                full[--len] = L'\0';
            }
            // A drive by itself ("E:") is asked about as its root.
            wchar_t probe[1024];
            wcscpy(probe, full);
            if (len == 2 && full[1] == L':')
            {
                wcscat(probe, L"\\");
            }
            DWORD attr = GetFileAttributesW(probe);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                break;
            }
            wchar_t* slash = wcsrchr(full, L'\\');
            if (slash == NULL)
            {
                return NULL;
            }
            *slash = L'\0';
        }

        if (wcslen(full) + wcslen(name) + 2 > 1024)
        {
            return NULL;
        }
        wcscat(full, L"\\");
        wcscat(full, name);
        if (WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int)size, NULL, NULL) <= 0)
        {
            return NULL;
        }
        return out;
#else
        (void)folder;
        (void)out;
        (void)size;
        return start;
#endif
    }
} // namespace

void iGui::PickFolder(const char* tag, const char* start)
{
    if (!BeginPick(tag))
    {
        return;
    }

    char at[2048];
    SDL_ShowOpenFolderDialog(OnFolderPicked, NULL, sWindow, StartAt(start, true, at, sizeof(at)),
                             false);
}

void iGui::PickOpenFile(const char* tag, const char* start, const FileFilter* filters, int count)
{
    if (!BeginPick(tag))
    {
        return;
    }

    const int n = Filters(filters, count);
    char at[2048];
    SDL_ShowOpenFileDialog(OnFolderPicked, NULL, sWindow, n > 0 ? sFilters : NULL, n,
                           StartAt(start, false, at, sizeof(at)), false);
}

void iGui::PickSaveFile(const char* tag, const char* start, const FileFilter* filters, int count)
{
    if (!BeginPick(tag))
    {
        return;
    }

    const int n = Filters(filters, count);
    char at[2048];
#ifdef __ANDROID__
    // The app's own picker: SDL's drops the suggested name (iAndroidDocument.h).
    if (iAndroidShowSaveDocument(OnFolderPicked, NULL, start, n > 0 ? sFilters[0].pattern : NULL))
    {
        return;
    }
#endif
    SDL_ShowSaveFileDialog(OnFolderPicked, NULL, sWindow, n > 0 ? sFilters : NULL, n,
                           StartAt(start, false, at, sizeof(at)));
}

bool iGui::ReadWholeFile(const char* path, std::vector<unsigned char>& out, char* why,
                         size_t whySize)
{
    // Through SDL rather than stdio: on Android the system's picker answers
    // with a content:// URI, which only SDL's file streams open.
    out.clear();
    size_t n = 0;
#ifdef __ANDROID__
    // A picker's document through the app's own Java (iAndroidDocument.h).
    if (iAndroidIsDocument(path))
    {
        char refused[256] = "";
        void* doc = iAndroidReadDocument(path, &n, refused, sizeof(refused));
        if (doc == NULL)
        {
            snprintf(why, whySize, "Could not read %s: %s", path, refused);
            return false;
        }
        out.assign((const unsigned char*)doc, (const unsigned char*)doc + n);
        SDL_free(doc);
        return true;
    }
#endif
    void* data = SDL_LoadFile(path, &n);
    if (data == NULL)
    {
        snprintf(why, whySize, "Could not read %s: %s", path, SDL_GetError());
        return false;
    }
    out.assign((const unsigned char*)data, (const unsigned char*)data + n);
    SDL_free(data);
    return true;
}

bool iGui::WriteWholeFile(const char* path, const unsigned char* data, size_t n, char* why,
                          size_t whySize)
{
#ifdef __ANDROID__
    // A picker's document through the app's own Java (iAndroidDocument.h).
    if (iAndroidIsDocument(path))
    {
        char refused[256] = "";
        if (!iAndroidWriteDocument(path, data, n, refused, sizeof(refused)))
        {
            snprintf(why, whySize, "Could not write %s: %s", path, refused);
            return false;
        }
        return true;
    }
#endif
    if (!SDL_SaveFile(path, data, n))
    {
        snprintf(why, whySize, "Could not write %s: %s", path, SDL_GetError());
        return false;
    }
    return true;
}

bool iGui::PickingFolder()
{
    if (sPickLock == NULL)
    {
        return false;
    }
    SDL_LockMutex(sPickLock);
    const bool picking = sPicking;
    SDL_UnlockMutex(sPickLock);
    return picking;
}

bool iGui::PollFolder(const char* tag, char* out, size_t outSize)
{
    if (sPickLock == NULL)
    {
        return false;
    }

    bool got = false;
    SDL_LockMutex(sPickLock);
    if (sPickReady && strcmp(sPickTag, tag) == 0)
    {
        sPickReady = false;
        if (sPickResult[0] != '\0')
        {
            snprintf(out, outSize, "%s", sPickResult);
            got = true;
        }
    }
    SDL_UnlockMutex(sPickLock);
    return got;
}

void iGui::OpenFolder(const char* path)
{
    if (path == NULL || path[0] == '\0')
    {
        return;
    }

    char url[2048];
    size_t o = 0;
    const char* prefix = (path[0] == '/') ? "file://" : "file:///";
    o += (size_t)snprintf(url, sizeof(url), "%s", prefix);
    for (const char* c = path; *c != '\0' && o + 4 < sizeof(url); c++)
    {
        if (*c == '\\')
        {
            url[o++] = '/';
        }
        else if (*c == ' ')
        {
            memcpy(url + o, "%20", 3);
            o += 3;
        }
        else
        {
            url[o++] = *c;
        }
    }
    url[o] = '\0';

    if (!SDL_OpenURL(url))
    {
        printf("tssm: gui: could not open %s: %s\n", path, SDL_GetError());
        fflush(stdout);
    }
}

// --- PNG ----------------------------------------------------------------------------------

bool iGui::WritePNG(const char* path, const unsigned char* rgba, int width, int height, int stride)
{
    if (path == NULL || rgba == NULL || width <= 0 || height <= 0)
    {
        return false;
    }

    // Filter byte 0 (none) and RGB per row.
    const size_t rowBytes = 1 + (size_t)width * 3;
    const size_t rawSize = rowBytes * height;
    unsigned char* raw = (unsigned char*)malloc(rawSize);
    if (raw == NULL)
    {
        return false;
    }
    for (int y = 0; y < height; y++)
    {
        unsigned char* row = raw + rowBytes * y;
        const unsigned char* src = rgba + (size_t)y * stride;
        row[0] = 0;
        for (int x = 0; x < width; x++)
        {
            row[1 + x * 3 + 0] = src[x * 4 + 0];
            row[1 + x * 3 + 1] = src[x * 4 + 1];
            row[1 + x * 3 + 2] = src[x * 4 + 2];
        }
    }

    // A zlib stream of stored blocks.
    const size_t blocks = rawSize / 65535 + 1;
    const size_t zSize = 2 + rawSize + blocks * 5 + 4;
    unsigned char* z = (unsigned char*)malloc(zSize);
    if (z == NULL)
    {
        free(raw);
        return false;
    }

    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    unsigned int a = 1;
    unsigned int b = 0;
    size_t left = rawSize;
    const unsigned char* src = raw;
    do
    {
        const unsigned int n = left > 65535 ? 65535 : (unsigned int)left;
        z[o++] = left == n ? 1 : 0;
        z[o++] = (unsigned char)n;
        z[o++] = (unsigned char)(n >> 8);
        z[o++] = (unsigned char)~n;
        z[o++] = (unsigned char)(~n >> 8);
        memcpy(z + o, src, n);
        o += n;
        for (unsigned int i = 0; i < n; i++)
        {
            a = (a + src[i]) % 65521;
            b = (b + a) % 65521;
        }
        src += n;
        left -= n;
    } while (left > 0);
    PutBE(z + o, (b << 16) | a);
    o += 4;
    free(raw);

    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        free(z);
        return false;
    }

    static const unsigned char kSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    unsigned char ihdr[13];
    PutBE(ihdr, (unsigned int)width);
    PutBE(ihdr + 4, (unsigned int)height);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    bool ok = fwrite(kSignature, 1, 8, f) == 8 && Chunk(f, "IHDR", ihdr, 13) &&
              Chunk(f, "IDAT", z, (unsigned int)o) && Chunk(f, "IEND", NULL, 0);
    free(z);
    ok = fclose(f) == 0 && ok;
    return ok;
}
