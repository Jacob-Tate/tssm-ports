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
// None on the 3DS: its files folder is fixed, and a missing file gets the
// error box (3ds/i3DS.cpp).

#ifdef __3DS__
#include "gui/iGui.h"

void iGuiLauncherRun()
{
}
#else

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>

#include "gui/iGuiFilePicker.h"
#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"

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
        ImGui::TextColored(ImVec4(0.96f, 0.82f, 0.25f, 1.0f), "The SpongeBob SquarePants Movie");
        ImGui::PopFont();
#ifdef __SWITCH__
        ImGui::TextDisabled("Choose the folder with the game's files on the SD card (normally "
                            "/switch/tssm/assets), check the settings, then Play.");
#elif defined(__3DS__)
        ImGui::TextDisabled("Choose where the game's files are, check the settings, then Play.");
#else
        ImGui::TextDisabled("PC port. Choose where the game's files are, check the settings, "
                            "then Play.");
#endif
        ImGui::Spacing();

        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
        ImGui::BeginChild("body", ImVec2(0.0f, -footer), ImGuiChildFlags_NavFlattened);

        iGui::SettingsGameFiles();

        // The setting's own folder may be fine while TSSM_ASSETS points
        // somewhere that is not; Play then drops the variable for this run.
        char why[512];
        const char* configured = iConfigGetString("assets.path", "");
        const bool configuredOk =
            iGui::CheckAssetFolder(configured[0] != '\0' ? configured : ".", why, sizeof(why));
        const bool effectiveOk = iGui::CheckAssetFolder(EffectiveRoot(), why, sizeof(why));
        *playable = effectiveOk || (EnvOverrides() && configuredOk);

        ImGui::Spacing();
        iGui::SettingsDisplay();
        ImGui::Spacing();
        iGui::SettingsAudio();
        ImGui::Spacing();
        iGui::SettingsGame();
        ImGui::Spacing();
        iGui::SettingsInterface();
        if (iGui::SaveError() != NULL)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", iGui::SaveError());
        }
        ImGui::EndChild();

        ImGui::Separator();
        const ImVec2 button(ImGui::GetFontSize() * 7.0f, ImGui::GetFrameHeight() * 1.2f);
        ImGui::BeginDisabled(!*playable);
        if (ImGui::Button("Play", button) ||
            (*playable && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive()))
        {
            result = LAUNCHER_PLAY;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Quit", button))
        {
            result = LAUNCHER_QUIT;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!*playable)
        {
            ImGui::TextDisabled("Choose the folder with the game's files to play.");
        }
        else
        {
#ifdef __SWITCH__
            ImGui::TextDisabled("In the game, - and + open these settings again.");
#else
            ImGui::TextDisabled("In the game, F1 or %s opens these settings again.",
                                iGui::PadComboName());
#endif
        }

        iGui::DrawFilePicker();
        ImGui::End();
        return result;
    }

    // TSSM_GUI_CAPTURE for the launcher: the frame as SDL rendered it.
    void Capture(SDL_Renderer* renderer, const char* pattern)
    {
        char path[512];
        if (strstr(pattern, "%s") != NULL)
        {
            snprintf(path, sizeof(path), pattern, "launcher");
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
} // namespace

void iGuiLauncherRun()
{
    const char* forced = getenv("TSSM_GUI_LAUNCHER");
    const char* when = (forced != NULL && forced[0] != '\0') ? forced :
                                                               iConfigGetString("gui.launcher", "auto");
#ifdef __SWITCH__
    // Every start would put one more screen before the game, with nothing the
    // overlay does not have.
    if ((forced == NULL || forced[0] == '\0') && SDL_strcasecmp(when, "always") == 0)
    {
        when = "auto";
    }
#endif

    char why[512];
    const bool ready = iGui::CheckAssetFolder(EffectiveRoot(), why, sizeof(why));

    if (SDL_strcasecmp(when, "never") == 0 || (SDL_strcasecmp(when, "always") != 0 && ready))
    {
        return;
    }

    printf("tssm: gui: launcher (%s)%s%s\n", ready ? "asked for" : "the game's files were not found",
           ready ? "" : ": ", ready ? "" : why);
    fflush(stdout);

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("tssm: gui: no launcher, SDL video did not start: %s\n", SDL_GetError());
        fflush(stdout);
        return;
    }

    float content = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (content <= 0.0f)
    {
        content = 1.0f;
    }

    int windowW = (int)(820 * content);
    int windowH = (int)(680 * content);
#ifdef __SWITCH__
    // The console stretches any other size to the screen.
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (desktop != NULL && desktop->w > 0 && desktop->h > 0)
    {
        windowW = desktop->w;
        windowH = desktop->h;
    }
#endif
    SDL_Window* window =
        SDL_CreateWindow("The SpongeBob SquarePants Movie", windowW, windowH,
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
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

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

    const char* capture = getenv("TSSM_GUI_CAPTURE");
    const char* exitAfter = getenv("TSSM_GUI_EXIT");
    const char* after = getenv("TSSM_GUI_CAPTURE_FRAMES");
    const int captureAt = (after != NULL && atoi(after) > 0) ? atoi(after) : 30;
    const char* autoPlay = getenv("TSSM_GUI_LAUNCHER_PLAY");

    float scale = 0.0f;
    int frame = 0;
    Result result = LAUNCHER_STAY;

    while (result == LAUNCHER_STAY)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
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
        if (capture != NULL && capture[0] != '\0' && frame == captureAt)
        {
            Capture(renderer, capture);
            if (exitAfter != NULL && atoi(exitAfter) != 0)
            {
                printf("tssm: gui: launcher captured; TSSM_GUI_EXIT set, exiting\n");
                fflush(stdout);
                exit(0);
            }
        }
        if (autoPlay != NULL && atoi(autoPlay) > 0 && frame >= atoi(autoPlay) && playable)
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
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
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
#endif
