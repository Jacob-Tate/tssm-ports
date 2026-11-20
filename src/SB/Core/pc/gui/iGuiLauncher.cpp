// The launcher. See iGui.h.
//
// It runs before the game has a window or a device, so it cannot draw the way
// the overlay does. It opens a small window of its own and draws with SDL's
// renderer (whatever SDL picks: Direct3D 11 or 12 on Windows, OpenGL or Vulkan
// elsewhere, OpenGL ES on Android) through ImGui's SDL_Renderer backend, and
// takes all of it down again before iSystemInit carries on -- so by the time
// the game opens its own window, SDL's video and gamepad subsystems are back
// where they were, and nothing of the launcher is left behind but config.ini.
//
// When it shows: [gui] launcher = auto (the default) shows it only when the
// game's files are not where the settings say, which turns the first run's
// "files not found" error box into a folder picker; always shows it on every
// start; never keeps the error box. TSSM_GUI_LAUNCHER overrides the setting.
//
// **Every tab.** It shows the overlay's tabs, all of them, from the one list
// both use (iGui::TabAt, gui/iGuiTabs.cpp): every Settings section, Controls,
// Saves, Mods, About, and whatever tab is added later. Each works here as it
// does in the game, on what the game has not started yet:
//
//   * settings are written to config.ini at once, and the game reads them
//     when it starts (iSystem.cpp's ApplyConfig, and each module's own init);
//   * the input layer is brought up for the launcher (iPadHostInit, and
//     iPadHostPollDevices once a pass) so Controls can name the controllers
//     and take a key or a button for a binding, and taken down again before
//     the game brings it up for itself;
//   * Mods reads the mods folder (iModRescan); the game reads it again at
//     iFileInit;
//   * Saves works on the save folder config.ini names -- unless another copy
//     of the game is running on it, which a tab's notBeforeGame says, greying
//     the tab with that one line.
//
// SDL's event subsystem is kept up from here to the end of the process: the
// input layer's event watches, added once, must outlive the launcher's video.

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"
#include "iMod.h"
#include "iPadHost.h"
#include "iWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    enum Result
    {
        LAUNCHER_STAY,
        LAUNCHER_PLAY,
        LAUNCHER_QUIT
    };

    // The tab to bring to the front on the next frame, or -1.
    S32 sSelectTab = -1;
    bool sModsScanned;

    // The folder the game will read: TSSM_ASSETS while it is set, the setting
    // otherwise, and the working folder when both are empty.
    const char* EffectiveRoot()
    {
        const char* root = iFileAssetRoot();
        return root[0] != '\0' ? root : ".";
    }

    bool EnvOverrides()
    {
        const char* env = getenv("TSSM_ASSETS");
        return env != NULL && env[0] != '\0';
    }

    // One tab's content, or the line that says why it cannot be used yet.
    void DrawTabBody(const iGui::Tab& tab, const char* notYet)
    {
        if (notYet != NULL)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f), "%s", notYet);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::BeginDisabled(true);
            tab.draw();
            ImGui::EndDisabled();
            return;
        }

        // The Mods tab's list is read when it is first shown, as the game
        // reads it when it starts.
        if (strcmp(tab.name, "mods") == 0 && !sModsScanned)
        {
            sModsScanned = true;
            iModRescan();
        }
        tab.draw();
    }

    Result Draw(bool* playable)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);

        Result result = LAUNCHER_STAY;
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("launcher", NULL, flags);

        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::TextColored(ImVec4(0.96f, 0.82f, 0.25f, 1.0f), TSSM_GAME_TITLE);
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("PC port. Every setting is here, the same as in the game (F1 or "
                            "Back+Start opens them there). Choose, then Play.");
        ImGui::PopTextWrapPos();

        // The setting's own folder may be fine while TSSM_ASSETS points
        // somewhere that is not; Play then drops the variable for this run.
        char why[512];
        const char* configured = iConfigGetString("assets.path", "");
        const bool configuredOk =
            iGui::CheckAssetFolder(configured[0] != '\0' ? configured : ".", why, sizeof(why));
        const bool effectiveOk = iGui::CheckAssetFolder(EffectiveRoot(), why, sizeof(why));
        *playable = effectiveOk || (EnvOverrides() && configuredOk);

        if (iGui::SaveError() != NULL)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", iGui::SaveError());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();

        // The tabs, the overlay's own list, above the row with Play.
        const float footer = ImGui::GetFrameHeight() * 1.6f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
        if (ImGui::BeginTabBar("tabs"))
        {
            for (S32 t = 0; t < iGui::TabCount(); t++)
            {
                const iGui::Tab& tab = iGui::TabAt(t);
                const char* notYet = tab.notBeforeGame != NULL ? tab.notBeforeGame() : NULL;
                const ImGuiTabItemFlags tabFlags =
                    sSelectTab == t ? ImGuiTabItemFlags_SetSelected : 0;

                if (notYet != NULL)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                }
                const bool open = ImGui::BeginTabItem(tab.label, NULL, tabFlags);
                if (notYet != NULL)
                {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", notYet);
                    }
                }
                if (open)
                {
                    ImGui::BeginChild("tab", ImVec2(0.0f, -footer), ImGuiChildFlags_NavFlattened);
                    DrawTabBody(tab, notYet);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        sSelectTab = -1;

        ImGui::Separator();

        // Play first and largest, in the game's yellow.
        const ImVec2 play(ImGui::GetFontSize() * 10.0f, ImGui::GetFrameHeight() * 1.6f);
        const ImVec2 quit(ImGui::GetFontSize() * 6.0f, ImGui::GetFrameHeight() * 1.6f);
        ImGui::BeginDisabled(!*playable);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.76f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.85f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.64f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.10f, 0.16f, 1.0f));
        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.25f);
        const bool pressed = ImGui::Button("Play", play);
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        // Enter plays, unless a field, a list or a binding being changed has it.
        // A field has let go by the frame its Enter confirms what was typed,
        // so the frame before counts too.
        static bool sFieldBefore;
        const bool field = ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput;
        const bool fieldBefore = sFieldBefore;
        sFieldBefore = field;
        if (pressed || (*playable && ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
                        !field && !fieldBefore &&
                        !ImGui::IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId |
                                                      ImGuiPopupFlags_AnyPopupLevel)))
        {
            result = LAUNCHER_PLAY;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Quit", quit))
        {
            result = LAUNCHER_QUIT;
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (!*playable)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f),
                               "Choose the folder with the game's files to play:");
            ImGui::TextDisabled("Settings, Game files.");
        }
        else
        {
            ImGui::TextDisabled("Changes are saved as you make them,");
            ImGui::TextDisabled("and the game starts with them.");
        }
        ImGui::EndGroup();

        ImGui::End();
        return result;
    }

    // TSSM_GUI_CAPTURE for the launcher: the frame as SDL rendered it, named
    // "launcher" or, stepping through tabs (TSSM_GUI_TAB), "launcher_<tab>".
    void Capture(SDL_Renderer* renderer, const char* pattern, const char* tab)
    {
        char name[64];
        snprintf(name, sizeof(name), "%s%s%s", "launcher", tab != NULL ? "_" : "",
                 tab != NULL ? tab : "");

        char path[512];
        if (strstr(pattern, "%s") != NULL)
        {
            snprintf(path, sizeof(path), pattern, name);
        }
        else if (tab != NULL)
        {
            const char* dot = strrchr(pattern, '.');
            snprintf(path, sizeof(path), "%.*s_%s%s", (int)(dot != NULL ? dot - pattern : strlen(pattern)),
                     pattern, name, dot != NULL ? dot : ".png");
        }
        else
        {
            snprintf(path, sizeof(path), "%s", pattern);
        }

        SDL_Surface* shot = SDL_RenderReadPixels(renderer, NULL);
        SDL_Surface* rgba = shot != NULL ? SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32) : NULL;
        if (rgba != NULL && iGui::WritePNG(path, (const unsigned char*)rgba->pixels, rgba->w,
                                           rgba->h, rgba->pitch))
        {
            printf("tssm: gui: captured the launcher (%dx%d) -> %s\n", rgba->w, rgba->h, path);
        }
        else
        {
            printf("tssm: gui: could not capture the launcher to %s (%s)\n", path, SDL_GetError());
        }
        fflush(stdout);
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(shot);
    }

    // The window no larger than the display's usable area (the taskbar left
    // out) less its own frame, and centred in it, so the title bar and the row
    // with Play stay on screen at any size and scale. The tab body scrolls when
    // it is shorter.
    void FitToDisplay(SDL_Window* window, SDL_DisplayID display, float content)
    {
        SDL_Rect usable;
        if (!SDL_GetDisplayUsableBounds(display, &usable) || usable.w <= 0 || usable.h <= 0)
        {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            return;
        }

        // The frame, which some systems only know once the window shows: a
        // title bar's worth then.
        int top = 0, left = 0, bottom = 0, right = 0;
        if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) || top <= 0)
        {
            top = (int)(32 * content);
        }
        left = left > 0 ? left : 0;
        right = right > 0 ? right : 0;
        bottom = bottom > 0 ? bottom : 0;

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        const int roomW = usable.w - left - right;
        const int roomH = usable.h - top - bottom;
        if (roomW > 0 && w > roomW)
        {
            w = roomW;
        }
        if (roomH > 0 && h > roomH)
        {
            h = roomH;
        }
        SDL_SetWindowSize(window, w, h);

        // SDL places the client area; the frame is around it.
        const int x = usable.x + left + (roomW > w ? (roomW - w) / 2 : 0);
        const int y = usable.y + top + (roomH > h ? (roomH - h) / 2 : 0);
        SDL_SetWindowPosition(window, x, y);

        printf("tssm: gui: launcher window %dx%d at %d,%d (usable %dx%d at %d,%d, frame %d/%d/%d/%d)\n",
               w, h, x, y, usable.w, usable.h, usable.x, usable.y, top, left, bottom, right);
        fflush(stdout);
    }
} // namespace

void iGuiLauncherRun()
{
    const char* forced = getenv("TSSM_GUI_LAUNCHER");
    const char* when = (forced != NULL && forced[0] != '\0') ? forced :
                                                               iConfigGetString("gui.launcher", "auto");

    char why[512];
    const bool ready = iGui::CheckAssetFolder(EffectiveRoot(), why, sizeof(why));

    if (SDL_strcasecmp(when, "never") == 0 || (SDL_strcasecmp(when, "always") != 0 && ready))
    {
        return;
    }

    printf("tssm: gui: launcher (%s)%s%s\n", ready ? "asked for" : "the game's files were not found",
           ready ? "" : ": ", ready ? "" : why);
    fflush(stdout);

    // Held to the end of the process (the comment at the top of this file).
    SDL_InitSubSystem(SDL_INIT_EVENTS);

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("tssm: gui: no launcher, SDL video did not start: %s\n", SDL_GetError());
        fflush(stdout);
        return;
    }

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    float content = SDL_GetDisplayContentScale(display);
    if (content <= 0.0f)
    {
        content = 1.0f;
    }

    SDL_Window* window =
        SDL_CreateWindow(TSSM_GAME_TITLE, (int)(960 * content), (int)(720 * content),
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window != NULL ? SDL_CreateRenderer(window, NULL) : NULL;
    if (renderer == NULL)
    {
        printf("tssm: gui: no launcher, the window could not be made: %s\n", SDL_GetError());
        fflush(stdout);
        if (window != NULL)
        {
            SDL_DestroyWindow(window);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        return;
    }
    iWindowSetIcon(window);
    SDL_SetRenderVSync(renderer, 1);
    FitToDisplay(window, display, content);
    SDL_ShowWindow(window);

    // The input layer, for the Controls tab: its controllers, and a key or a
    // button pressed for a binding. The game starts it again for itself.
    iPadHostInit();

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    iGui::SetWindow(window);
    iGui::SetInLauncher(true);
    iGui::SetupContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    printf("tssm: gui: launcher open (renderer %s)\n", SDL_GetRendererName(renderer));
    fflush(stdout);

    // Without the game's files there is one thing to do first: the folder,
    // on the Settings tab, which leads with it in the launcher.
    if (!ready)
    {
        sSelectTab = 0;
    }

    const char* capture = getenv("TSSM_GUI_CAPTURE");
    const char* exitAfter = getenv("TSSM_GUI_EXIT");
    const char* after = getenv("TSSM_GUI_CAPTURE_FRAMES");
    const int captureAt = (after != NULL && atoi(after) > 0) ? atoi(after) : 30;
    const char* autoPlay = getenv("TSSM_GUI_LAUNCHER_PLAY");

    // TSSM_GUI_TAB (one tab, a list or "all") with TSSM_GUI_CAPTURE: each tab
    // in turn, captureAt frames apart, as launcher_<tab>.
    S32 tabList[16];
    S32 tabCount = 0;
    {
        const char* tabs = getenv("TSSM_GUI_TAB");
        if (tabs != NULL && tabs[0] != '\0')
        {
            if (SDL_strcasecmp(tabs, "all") == 0)
            {
                for (S32 i = 0; i < iGui::TabCount() && tabCount < 16; i++)
                {
                    tabList[tabCount++] = i;
                }
            }
            else
            {
                const char* c = tabs;
                while (*c != '\0' && tabCount < 16)
                {
                    const char* comma = strchr(c, ',');
                    const size_t len = comma != NULL ? (size_t)(comma - c) : strlen(c);
                    const S32 i = iGui::TabFind(c, len);
                    if (i >= 0)
                    {
                        tabList[tabCount++] = i;
                    }
                    c = comma != NULL ? comma + 1 : c + len;
                }
            }
        }
    }
    const bool stepTabs = capture != NULL && capture[0] != '\0' && tabCount > 0;
    S32 tabStep = 0;
    if (stepTabs)
    {
        sSelectTab = tabList[0];
    }
    bool capturesDone = false;

    float scale = 0.0f;
    int frame = 0;
    int tabFrame = 0;
    Result result = LAUNCHER_STAY;

    while (result == LAUNCHER_STAY)
    {
        // The controllers' events are the input layer's (iPadHostPollDevices
        // takes that range); the rest are the launcher's.
        SDL_PumpEvents();
        iPadHostPollDevices();
        iGui::UseOpenGamepads();

        SDL_Event e;
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                              SDL_EVENT_JOYSTICK_AXIS_MOTION - 1) > 0 ||
               SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FINGER_DOWN, SDL_EVENT_LAST) > 0)
        {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)))
            {
                result = LAUNCHER_QUIT;
            }
        }

        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0)
        {
            SDL_Delay(20);
            continue;
        }

        const float want = iGui::AutoScale(window);
        if (want != scale)
        {
            scale = want;
            iGui::ApplyScale(scale);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        bool playable = false;
        const Result chosen = Draw(&playable);
        if (result == LAUNCHER_STAY)
        {
            result = chosen;
        }

        ImGui::Render();

        const ImGuiIO& io = ImGui::GetIO();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 10, 26, 43, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        frame++;
        tabFrame++;
        if (capture != NULL && capture[0] != '\0' && !capturesDone && tabFrame == captureAt)
        {
            if (stepTabs)
            {
                Capture(renderer, capture, iGui::TabAt(tabList[tabStep]).name);
                tabFrame = 0;
                if (++tabStep < tabCount)
                {
                    sSelectTab = tabList[tabStep];
                }
                else
                {
                    capturesDone = true;
                }
            }
            else
            {
                Capture(renderer, capture, NULL);
                capturesDone = true;
            }
            if (capturesDone && exitAfter != NULL && atoi(exitAfter) != 0)
            {
                printf("tssm: gui: launcher captured; TSSM_GUI_EXIT set, exiting\n");
                fflush(stdout);
                exit(0);
            }
        }
        if (autoPlay != NULL && atoi(autoPlay) > 0 && frame >= atoi(autoPlay) && playable &&
            (!stepTabs || capturesDone))
        {
            result = LAUNCHER_PLAY;
        }

        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    // The input layer goes down with the launcher, and the game brings it up
    // for itself (xPadInit).
    iPadHostExit();

    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    // Nothing the launcher's window or controllers left queued reaches the game.
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    iGui::SetWindow(NULL);
    iGui::SetInLauncher(false);

    if (result == LAUNCHER_QUIT)
    {
        printf("tssm: gui: launcher closed without playing; exiting\n");
        fflush(stdout);
        exit(0);
    }

    // The setting's folder is good and TSSM_ASSETS's is not: this run uses the
    // setting, which is what the player just chose.
    if (EnvOverrides() && !iGui::CheckAssetFolder(EffectiveRoot(), why, sizeof(why)))
    {
        printf("tssm: gui: TSSM_ASSETS does not hold the game's files; using [assets] path "
               "for this run\n");
        iHostSetEnv("TSSM_ASSETS", NULL);
    }

    printf("tssm: gui: launcher done; game files: %s\n", EffectiveRoot());
    fflush(stdout);
}
