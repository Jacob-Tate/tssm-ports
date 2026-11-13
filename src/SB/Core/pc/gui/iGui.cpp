// The overlay: its ImGui context, how it opens and closes, what it does to the
// game's input, and the window it draws. See iGui.h.
//
// **Where it runs.** The game's loop is retail's, and the port's per-frame
// obligations hang off the present for that reason (rw/camera.cpp says so at
// length). The overlay does the same: iGuiRender.cpp hooks librw's present, and
// Frame() below builds one ImGui frame there, after the game has finished
// drawing its own. Nothing runs per frame while the overlay is closed except
// that hook asking whether it is open.
//
// **How events reach it.** SDL has one event queue per process and two readers
// already -- iWindowPump takes the window's range, iPadHostPoll the
// controllers' -- each taking only its own (iWindowSDL.cpp explains why). A
// third reader would have to steal from one of them. So the overlay does not
// read the queue: it WATCHES it (SDL_AddEventWatch), seeing every event as it
// is queued, on the main thread, during whichever SDL_PumpEvents queued it, and
// leaves the queue exactly as it was for the readers that own it.

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"
#include "gui/iGuiRender.h"

#include "iConfig.h"
#include "iCoop.h"
#include "iPadControls.h"
#include "iPadHost.h"
#include "iScreen.h"
#include "iWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// gui/iGuiSelfTest.cpp: presents frames of a test picture with the overlay
// open, for TSSM_GUI_SELFTEST. Separate because it speaks RenderWare.
void iGuiSelfTestRun(S32 frames);

namespace
{
    enum
    {
        TAB_SETTINGS,
        TAB_CONTROLS,
        TAB_SAVES,
        TAB_MODS,
        TAB_COOP,
        TAB_ABOUT,
        TAB_COUNT
    };

    const char* const kTabNames[TAB_COUNT] = { "settings", "controls", "saves", "mods", "coop", "about" };
    const char* const kTabLabels[TAB_COUNT] = { "Settings", "Controls", "Saves", "Mods", "Co-op", "About" };

    ImGuiContext* sContext;
    SDL_Window* sWindow;
    SDL_ThreadID sMainThread;

    bool sReady;
    bool sOpen;
    bool sJustOpened;

    // Asked for by a key or a tap inside the event watch, and applied at the
    // next frame, where the ImGui frame is not half built.
    bool sToggle;

    SDL_Scancode sKey = SDL_SCANCODE_F1;
    bool sPadCombo = true;
    bool sComboHeld;

    // Three fingers down within a moment of each other toggle the overlay on a
    // touch screen. "Within a moment" so that a thumb resting on the on-screen
    // stick and two buttons pressed beside it -- three fingers, held -- does
    // not.
    S32 sFingers;
    U64 sFirstFingerAt;
    const U64 kFingerWindowNs = 250000000ull;

    float sScale;

    // The co-op HUD layer's own clock, for the frames it draws alone.
    U64 sHudLastNs;

    // Buttons held when the overlay closed, hidden from the game until let go.
    U32 sLatch[IPAD_MAX_CONTROLLERS];
    bool sLatchArm[IPAD_MAX_CONTROLLERS];
    iPadHostState sQuiet[IPAD_MAX_CONTROLLERS];

    S32 sSelectTab = -1;

    iGuiPanelFn sPanelFn[IGUI_PANEL_COUNT];
    void* sPanelUser[IGUI_PANEL_COUNT];
    iGuiAudioFn sAudio;

    // --- TSSM_GUI_* debug switches; docs/GUI.md lists them -------------------
    bool sCycleTabs;
    S32 sCaptureTab = -1;
    // TSSM_GUI_TAB as a list ("saves,mods"), captured in that order.
    S32 sTabList[TAB_COUNT];
    S32 sTabListCount;
    S32 sTabListPos;
    // TSSM_GUI_OPEN_AT: open at this present instead of at startup, so a
    // capture can show the overlay over the game rather than the boot.
    S32 sOpenAt;
    S32 sPresents;
    S32 sOpenFrames;
    S32 sCaptureAfter = 30;
    char sCapturePattern[480];
    bool sCapturing;
    bool sCapturesFinished;
    bool sExitWhenCaptured;
    // TSSM_GUI_CLOSE=1: close the overlay once its captures are written, so
    // the game goes on in view (tools/coop_test.py).
    bool sCloseWhenCaptured;
    // TSSM_GUI_HUD_CAPTURE=<n>:<file.png>: the window with the co-op HUD layer
    // (overlay closed) at present n.
    S32 sHudCaptureAt;
    char sHudCapturePath[480];
    // iGuiTestShot: one picture at a time, a few frames after it is asked
    // for, so what the test just did is on screen.
    char sShotPath[480];
    bool sShotHud;
    bool sShotArmed;
    S32 sShotWait;

    // ---------------------------------------------------------------------------

    void SetOpen(bool open);
    void ReadConfig();

    // The controllers the input backend has open, handed to ImGui's SDL3
    // backend so it navigates with them without opening or closing anything
    // itself (its "manual" mode). Refreshed every frame the overlay is open,
    // which is what picks up a controller plugged in meanwhile.
    void UpdateGamepads()
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

    // Back and Start held together on any open controller. The physical
    // buttons, not the game's bindings: rebinding Start to something else must
    // not leave the overlay unreachable.
    bool PadComboHeld()
    {
        bool held = false;

        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        if (ids != NULL)
        {
            for (int i = 0; i < n && !held; i++)
            {
                SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
                if (pad != NULL && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) &&
                    SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))
                {
                    held = true;
                }
            }
            SDL_free(ids);
        }

        return held;
    }

    void UpdateScale()
    {
        const float scale = iGui::AutoScale(sWindow);
        if (scale != sScale)
        {
            sScale = scale;
            iGui::ApplyScale(scale);
        }
    }

    // --- the debug switches -------------------------------------------------

    void CapturePathFor(S32 tab, char* out, size_t size)
    {
        if (strstr(sCapturePattern, "%s") != NULL)
        {
            snprintf(out, size, sCapturePattern, kTabNames[tab]);
            return;
        }

        if (!sCycleTabs)
        {
            snprintf(out, size, "%s", sCapturePattern);
            return;
        }

        // One file per tab: "name.png" becomes "name_settings.png".
        const char* dot = strrchr(sCapturePattern, '.');
        if (dot == NULL)
        {
            snprintf(out, size, "%s_%s.png", sCapturePattern, kTabNames[tab]);
        }
        else
        {
            snprintf(out, size, "%.*s_%s%s", (int)(dot - sCapturePattern), sCapturePattern,
                     kTabNames[tab], dot);
        }
    }

    void ReadDebugSwitches()
    {
        // One tab, "all", or a list: "saves,mods".
        const char* tab = getenv("TSSM_GUI_TAB");
        if (tab != NULL && tab[0] != '\0')
        {
            if (SDL_strcasecmp(tab, "all") == 0)
            {
                for (S32 i = 0; i < TAB_COUNT; i++)
                {
                    sTabList[sTabListCount++] = i;
                }
            }
            else
            {
                const char* c = tab;
                while (*c != '\0' && sTabListCount < TAB_COUNT)
                {
                    const char* comma = strchr(c, ',');
                    const size_t len = comma != NULL ? (size_t)(comma - c) : strlen(c);
                    for (S32 i = 0; i < TAB_COUNT; i++)
                    {
                        if (strlen(kTabNames[i]) == len && SDL_strncasecmp(c, kTabNames[i], len) == 0)
                        {
                            sTabList[sTabListCount++] = i;
                        }
                    }
                    c = comma != NULL ? comma + 1 : c + len;
                }
            }
            if (sTabListCount > 0)
            {
                sTabListPos = 0;
                sCaptureTab = sTabList[0];
                sCycleTabs = sTabListCount > 1;
            }
        }

        const char* openAt = getenv("TSSM_GUI_OPEN_AT");
        if (openAt != NULL && atoi(openAt) > 0)
        {
            sOpenAt = atoi(openAt);
        }

        const char* capture = getenv("TSSM_GUI_CAPTURE");
        if (capture != NULL && capture[0] != '\0')
        {
            snprintf(sCapturePattern, sizeof(sCapturePattern), "%s", capture);
            sCapturing = true;
        }

        const char* after = getenv("TSSM_GUI_CAPTURE_FRAMES");
        if (after != NULL && atoi(after) > 0)
        {
            sCaptureAfter = atoi(after);
        }

        const char* exitWhen = getenv("TSSM_GUI_EXIT");
        sExitWhenCaptured = exitWhen != NULL && atoi(exitWhen) != 0;
        const char* closeWhen = getenv("TSSM_GUI_CLOSE");
        sCloseWhenCaptured = closeWhen != NULL && atoi(closeWhen) != 0;

        const char* hud = getenv("TSSM_GUI_HUD_CAPTURE");
        const char* colon = hud != NULL ? strchr(hud, ':') : NULL;
        if (colon != NULL && atoi(hud) > 0 && colon[1] != '\0')
        {
            sHudCaptureAt = atoi(hud);
            snprintf(sHudCapturePath, sizeof(sHudCapturePath), "%s", colon + 1);
        }

        if (sCaptureTab >= 0)
        {
            sSelectTab = sCaptureTab;
        }
    }

    // Before a frame is drawn (`hud`: with the overlay closed): arms the test
    // shot that is due, and lets it go once written.
    void ShotStep(bool hud)
    {
        if (sShotArmed)
        {
            if (iGuiRenderCaptureDone())
            {
                sShotArmed = false;
                sShotPath[0] = '\0';
                iGuiRenderRequestCapture(NULL);
            }
            return;
        }
        // Not over another capture: the debug one waits for its frame.
        if (sShotPath[0] == '\0' || sShotHud != hud || sHudCaptureAt < 0 ||
            (!hud && sCapturing && !sCapturesFinished))
        {
            return;
        }
        if (sShotWait > 0)
        {
            sShotWait--;
            return;
        }
        iGuiRenderRequestCapture(sShotPath);
        sShotArmed = true;
    }

    // After each frame the overlay drew: take the capture that is due, move to
    // the next tab, and stop when every tab asked for has been written.
    void DebugStep()
    {
        if (!sCapturing || sCapturesFinished)
        {
            return;
        }

        sOpenFrames++;

        if (iGuiRenderCaptureDone())
        {
            iGuiRenderRequestCapture(NULL);
            if (sCycleTabs && sTabListPos + 1 < sTabListCount)
            {
                sCaptureTab = sTabList[++sTabListPos];
                sSelectTab = sCaptureTab;
                sOpenFrames = 0;
            }
            else
            {
                sCapturesFinished = true;
                if (sExitWhenCaptured)
                {
                    printf("tssm: gui: captures written; TSSM_GUI_EXIT set, exiting\n");
                    fflush(stdout);
                    exit(0);
                }
                if (sCloseWhenCaptured)
                {
                    printf("tssm: gui: captures written; TSSM_GUI_CLOSE set, closing the overlay\n");
                    fflush(stdout);
                    sToggle = sOpen;
                }
            }
            return;
        }

        // The frame after this one is the one written: requested now, drawn and
        // read back at the next present.
        if (sOpenFrames == sCaptureAfter)
        {
            char path[512];
            CapturePathFor(sCaptureTab >= 0 ? sCaptureTab : 0, path, sizeof(path));
            iGuiRenderRequestCapture(path);
        }
    }

    // --- the window ---------------------------------------------------------------

    void DrawPanel(iGuiPanel panel, void (*fallback)())
    {
        if (sPanelFn[panel] != NULL)
        {
            sPanelFn[panel](sPanelUser[panel]);
        }
        else
        {
            fallback();
        }
    }

    void DrawQuitPopup()
    {
        if (ImGui::BeginPopupModal("Quit the game?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Quit to the desktop? Progress since the last save is lost.");
            ImGui::Spacing();
            if (ImGui::Button("Quit"))
            {
                iWindowRequestClose();
                SetOpen(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void DrawOverlay()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();

        // The game, dimmed, so the window reads as in front of it.
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize,
                                                      IM_COL32(0, 10, 25, 130));

        const float margin = 12.0f * sScale;
        ImVec2 size(io.DisplaySize.x - 2.0f * margin, io.DisplaySize.y - 2.0f * margin);
        if (size.x > 1040.0f * sScale)
        {
            size.x = 1040.0f * sScale;
        }
        if (size.y > 720.0f * sScale)
        {
            size.y = 720.0f * sScale;
        }

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        if (sJustOpened)
        {
            ImGui::SetNextWindowFocus();
            sJustOpened = false;
        }

        // The tab scrolls, not the window: its height is set to fit.
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("The SpongeBob SquarePants Movie  |  PC port###tssm_overlay", NULL, flags))
        {
            if (iGui::SaveError() != NULL)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 110, 255));
                ImGui::TextWrapped("%s", iGui::SaveError());
                ImGui::PopStyleColor();
            }
            if (iGui::AnyRestartPending())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(250, 210, 80, 255));
                ImGui::TextWrapped("Changes marked \"restart\" apply the next time the game "
                                   "starts. Everything else has already taken effect.");
                ImGui::PopStyleColor();
            }

            // Below the tab's child: a separator and the row of buttons.
            const float footer = ImGui::GetFrameHeight() + 2.0f * style.ItemSpacing.y + 4.0f;

            if (ImGui::BeginTabBar("tabs"))
            {
                for (S32 t = 0; t < TAB_COUNT; t++)
                {
                    const ImGuiTabItemFlags tabFlags =
                        (sSelectTab == t) ? ImGuiTabItemFlags_SetSelected : 0;
                    if (ImGui::BeginTabItem(kTabLabels[t], NULL, tabFlags))
                    {
                        ImGui::BeginChild("tab", ImVec2(0.0f, -footer), ImGuiChildFlags_NavFlattened);
                        switch (t)
                        {
                        case TAB_SETTINGS:
                            iGui::DrawSettings();
                            break;
                        case TAB_CONTROLS:
                            DrawPanel(IGUI_PANEL_CONTROLS, iGui::DrawControls);
                            break;
                        case TAB_SAVES:
                            DrawPanel(IGUI_PANEL_SAVES, iGui::DrawSaves);
                            break;
                        case TAB_MODS:
                            DrawPanel(IGUI_PANEL_MODS, iGui::DrawMods);
                            break;
                        case TAB_COOP:
                            iGui::DrawCoop();
                            break;
                        default:
                            iGui::DrawAbout();
                            break;
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            sSelectTab = -1;

            ImGui::Separator();

            if (ImGui::Button("Resume"))
            {
                SetOpen(false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit game"))
            {
                ImGui::OpenPopup("Quit the game?");
            }
            DrawQuitPopup();

            const char* keyName = SDL_GetScancodeName(sKey);
            ImGui::SameLine();
            ImGui::TextDisabled("%s, Esc%s: close. The game takes no input while this is open.",
                                (keyName != NULL && keyName[0] != '\0') ? keyName : "F1",
                                sPadCombo ? ", Back+Start" : "");
        }
        ImGui::End();

        if (sOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            SetOpen(false);
        }
    }

    // With the overlay closed: the co-op HUD layer alone, while co-op is on
    // (iCoop.h). Nothing else, and no input: the SDL backend is left alone (it
    // would capture the mouse and change the cursor under the game's mouse
    // camera), so the display size and the time step are set here.
    bool HudFrame()
    {
        if (!iCoopHudActive())
        {
            return false;
        }

        ImGui::SetCurrentContext(sContext);
        UpdateScale();
        ImGuiIO& io = ImGui::GetIO();
        int w = 0, h = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(sWindow, &w, &h);
        SDL_GetWindowSizeInPixels(sWindow, &pw, &ph);
        if (w <= 0 || h <= 0)
        {
            return false;
        }
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DisplayFramebufferScale = ImVec2((float)pw / (float)w, (float)ph / (float)h);
        const U64 now = SDL_GetTicksNS();
        io.DeltaTime = sHudLastNs != 0 && now > sHudLastNs ? (float)((now - sHudLastNs) / 1e9) : 1.0f / 60.0f;
        if (io.DeltaTime <= 0.0f || io.DeltaTime > 1.0f)
        {
            io.DeltaTime = 1.0f / 60.0f;
        }
        sHudLastNs = now;

        if (sHudCaptureAt > 0 && sPresents >= sHudCaptureAt && !sShotArmed)
        {
            sHudCaptureAt = -1;
            iGuiRenderRequestCapture(sHudCapturePath);
        }
        else if (sHudCaptureAt < 0 && iGuiRenderCaptureDone())
        {
            // Written: cleared, so TSSM_GUI_CAPTURE's own capture later does
            // not take this one for its own.
            sHudCaptureAt = 0;
            iGuiRenderRequestCapture(NULL);
        }
        else
        {
            ShotStep(true);
        }

        iGuiRenderNewFrame();
        ImGui::NewFrame();
        iGuiRenderBackground();
        iGui::DrawCoopHud();
        ImGui::Render();
        return true;
    }

    // One frame, at the present. See iGuiRender.h.
    bool Frame()
    {
        sPresents++;
        iGui::CoopFrame();
        if (sOpenAt > 0 && sPresents == sOpenAt)
        {
            printf("tssm: gui: TSSM_GUI_OPEN_AT=%d reached\n", (int)sOpenAt);
            SetOpen(true);
        }

        if (sToggle)
        {
            sToggle = false;
            SetOpen(!sOpen);
        }
        if (!sOpen)
        {
            return HudFrame();
        }

        ImGui::SetCurrentContext(sContext);
        ReadConfig();
        UpdateGamepads();
        UpdateScale();
        ShotStep(false);

        iGuiRenderNewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        iGuiRenderBackground();
        DrawOverlay();
        if (iCoopHudActive())
        {
            iGui::DrawCoopHud();
        }

        ImGui::Render();

        DebugStep();

        // Closed by a button on this frame: the frame is finished, but there
        // is nothing to show.
        return sOpen;
    }

    void SetOpen(bool open)
    {
        if (!sReady || open == sOpen)
        {
            return;
        }

        sOpen = open;
        ImGui::SetCurrentContext(sContext);
        ImGuiIO& io = ImGui::GetIO();

        // The mouse camera holds the cursor in relative mode; the overlay
        // needs it back while it is open (iPadControls.h).
        iPadControlsSetMouseCapture(!open);

        if (open)
        {
            // Whatever ImGui last heard is stale: it has been deaf since it
            // closed. Start from nothing held and the mouse where it is.
            io.ClearEventsQueue();
            io.ClearInputKeys();
            io.ClearInputMouse();

            float x = 0.0f;
            float y = 0.0f;
            SDL_GetMouseState(&x, &y);
            io.AddMousePosEvent(x, y);

            sJustOpened = true;
            printf("tssm: gui: overlay open\n");
        }
        else
        {
            for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
            {
                sLatchArm[p] = true;
            }

            SDL_StopTextInput(sWindow);
            SDL_SetCursor(SDL_GetDefaultCursor());
            iGuiRenderReleaseVolatile();
            printf("tssm: gui: overlay closed\n");
        }
        fflush(stdout);
    }

    bool SDLCALL EventWatch(void*, SDL_Event* e)
    {
        // Events another thread pushes are watched on that thread. Nothing the
        // overlay wants comes from one, and nothing here is thread-safe.
        if (SDL_GetCurrentThreadID() != sMainThread)
        {
            return true;
        }

        switch (e->type)
        {
        case SDL_EVENT_KEY_DOWN:
            if (!e->key.repeat && e->key.scancode == sKey)
            {
                sToggle = !sToggle;
                return true;
            }
            break;

        case SDL_EVENT_FINGER_DOWN:
            if (sFingers == 0)
            {
                sFirstFingerAt = SDL_GetTicksNS();
            }
            sFingers++;
            if (sFingers == 3 && SDL_GetTicksNS() - sFirstFingerAt < kFingerWindowNs)
            {
                sToggle = !sToggle;
            }
            break;

        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            if (sFingers > 0)
            {
                sFingers--;
            }
            break;

        // Every change after which librw may reset a D3D9 device.
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            iGuiRenderReleaseVolatile();
            break;

        default:
            break;
        }

        if (sOpen)
        {
            ImGui::SetCurrentContext(sContext);
            ImGui_ImplSDL3_ProcessEvent(e);
        }
        return true;
    }

    void ReadConfig()
    {
        const char* key = iConfigGetString("gui.key", "f1");
        SDL_Scancode code = SDL_GetScancodeFromName(key);
        if (code == SDL_SCANCODE_UNKNOWN)
        {
            printf("tssm: gui: [gui] key '%s' is not a key SDL knows; using F1\n", key);
            code = SDL_SCANCODE_F1;
        }
        sKey = code;
        sPadCombo = iConfigGetBool("gui.pad_combo", TRUE) != 0;
    }
} // namespace

// --- the overlay's interface ------------------------------------------------------

void iGuiInit()
{
    if (sReady)
    {
        return;
    }

    // The game's window. There is exactly one by now: iWindowSDL.cpp made it
    // under Direct3D, librw under OpenGL.
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    sWindow = (windows != NULL && count > 0) ? windows[0] : NULL;
    SDL_free(windows);
    if (sWindow == NULL)
    {
        printf("tssm: gui: no window to draw the overlay on; overlay off\n");
        fflush(stdout);
        return;
    }

    sMainThread = SDL_GetCurrentThreadID();

    IMGUI_CHECKVERSION();
    sContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(sContext);
    iGui::SetWindow(sWindow);
    iGui::SetInLauncher(false);
    iGui::SetupContext();

    const bool gl = iScreenGetBackend() == iSCREENBACKEND_GL3;
    bool platform = gl ? ImGui_ImplSDL3_InitForOpenGL(sWindow, SDL_GL_GetCurrentContext()) :
                         ImGui_ImplSDL3_InitForD3D(sWindow);

    const char* renderer = "none";
    if (!platform || !iGuiRenderInit(Frame, &renderer))
    {
        printf("tssm: gui: the overlay cannot draw on %s yet; overlay off\n", renderer);
        fflush(stdout);
        if (platform)
        {
            ImGui_ImplSDL3_Shutdown();
        }
        ImGui::DestroyContext(sContext);
        sContext = NULL;
        sWindow = NULL;
        return;
    }

    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);
    SDL_AddEventWatch(EventWatch, NULL);

    ReadConfig();
    iGui::SnapshotStartup();
    iGui::ApplyAudio();
    sReady = true;

    printf("tssm: gui: overlay ready (%s, Dear ImGui %s); %s%s opens it\n", renderer,
           IMGUI_VERSION, SDL_GetScancodeName(sKey), sPadCombo ? " or Back+Start" : "");
    fflush(stdout);

    ReadDebugSwitches();

    const char* open = getenv("TSSM_GUI_OPEN");
    if (((open != NULL && atoi(open) != 0) || sCapturing) && sOpenAt <= 0)
    {
        SetOpen(true);
    }

    // Started to join with no password ([coop] mode = join, --join): the
    // overlay opens on the Co-op tab to ask for it.
    if (iCoopNeedsPassword() && !sOpen)
    {
        sSelectTab = TAB_COOP;
        SetOpen(true);
    }

    const char* selftest = getenv("TSSM_GUI_SELFTEST");
    if (selftest != NULL && atoi(selftest) > 0)
    {
        if (!sOpen)
        {
            SetOpen(true);
        }
        iGuiSelfTestRun(atoi(selftest));
    }
}

void iGuiShutdown()
{
    if (!sReady)
    {
        return;
    }

    SDL_RemoveEventWatch(EventWatch, NULL);
    ImGui::SetCurrentContext(sContext);
    iGuiRenderShutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(sContext);

    sContext = NULL;
    sWindow = NULL;
    sReady = false;
    sOpen = false;
}

bool iGuiIsOpen()
{
    return sOpen;
}

void iGuiSetOpen(bool open)
{
    SetOpen(open);
}

void iGuiTestShot(const char* path, bool hud)
{
    if (path == NULL || sShotArmed)
    {
        return;
    }
    snprintf(sShotPath, sizeof(sShotPath), "%s", path);
    sShotHud = hud;
    sShotWait = 4;
}

const iPadHostState* iGuiFilterPad(S32 port, const iPadHostState* host)
{
    if (!sReady || host == NULL || port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return host;
    }

    if (port == 0 && sPadCombo)
    {
        const bool held = PadComboHeld();
        if (held && !sComboHeld)
        {
            SetOpen(!sOpen);
        }
        sComboHeld = held;
    }

    if (sOpen)
    {
        // Present and idle: the game neither moves nor reports the pad gone.
        iPadHostState* quiet = &sQuiet[port];
        memset(quiet, 0, sizeof(*quiet));
        quiet->connected = host->connected;
        return quiet;
    }

    if (sLatchArm[port])
    {
        sLatchArm[port] = false;
        sLatch[port] = host->buttons;
    }

    if (sLatch[port] != 0)
    {
        sLatch[port] &= host->buttons;
        if (sLatch[port] != 0)
        {
            iPadHostState* masked = &sQuiet[port];
            *masked = *host;
            masked->buttons &= ~sLatch[port];
            return masked;
        }
    }

    return host;
}

void iGuiSetPanel(iGuiPanel panel, iGuiPanelFn draw, void* user)
{
    if (panel < 0 || panel >= IGUI_PANEL_COUNT)
    {
        return;
    }
    sPanelFn[panel] = draw;
    sPanelUser[panel] = draw != NULL ? user : NULL;
}

void iGuiSetAudioListener(iGuiAudioFn fn)
{
    sAudio = fn;
    iGui::ApplyAudio();
}

// --- shared with the other GUI files ----------------------------------------------

iGuiPanelFn iGui::Panel(iGuiPanel panel, void** user)
{
    if (panel < 0 || panel >= IGUI_PANEL_COUNT)
    {
        return NULL;
    }
    if (user != NULL)
    {
        *user = sPanelUser[panel];
    }
    return sPanelFn[panel];
}

void iGui::ApplyAudio()
{
    if (sAudio == NULL)
    {
        return;
    }

    sAudio(iConfigGetInt("audio.master_volume", 100) / 100.0f,
           iConfigGetInt("audio.music_volume", 100) / 100.0f,
           iConfigGetInt("audio.sfx_volume", 100) / 100.0f,
           iConfigGetInt("audio.movie_volume", 100) / 100.0f);
}
