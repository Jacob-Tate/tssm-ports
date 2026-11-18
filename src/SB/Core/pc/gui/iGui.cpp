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

#include "gui/iGuiFilePicker.h"
#include "gui/iGuiInternal.h"
#include "gui/iGuiRender.h"

#include "iConfig.h"
#include "iPadControls.h"
#include "iPadHost.h"
#include "iScreen.h"
#ifdef TSSM_UPDATE
#include "iUpdate.h"
#endif
#include "iWindow.h"

#ifdef __3DS__
#include "3ds/i3DS.h"
#endif

#include <float.h>
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
        TAB_ABOUT,
        TAB_COUNT
    };

    const char* const kTabNames[TAB_COUNT] = { "settings", "controls", "saves", "mods", "about" };
    const char* const kTabLabels[TAB_COUNT] = { "Settings", "Controls", "Saves", "Mods", "About" };

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

    // Buttons held when the overlay closed, hidden from the game until let go.
    U32 sLatch[IPAD_MAX_CONTROLLERS];
    bool sLatchArm[IPAD_MAX_CONTROLLERS];
    iPadHostState sQuiet[IPAD_MAX_CONTROLLERS];

    S32 sSelectTab = -1;
#if TSSM_GUI_CONSOLE
    // The tab drawn last frame, for L and R.
    S32 sShownTab;
#endif

#ifdef __3DS__
    // The bottom screen while the overlay is closed ([gui] bottom_hint_seconds):
    // one line for a while after start-up, then a fade to black, which stays
    // and costs nothing (no picture is drawn). A tap brings the line back for
    // as long again. -1 keeps it for good, 0 never shows it.
    enum HintState
    {
        HINT_SHOWN,
        HINT_FADE_OUT,
        HINT_BLACK,
        HINT_FADE_IN
    };
    HintState sHint = HINT_SHOWN;
    S32 sHintSeconds = 10;
    U64 sHintMs; // time in the state: the game's frames, not its loads (ClosedFrame)
    U64 sHintLastAt;
    bool sHintDue = true;
    bool sTapped;
    // The game started without the DSP firmware (i3DSNoSoundText): the
    // bottom screen says so above the line, and stays until a tap.
    bool sNoSoundNote;
    int sResumes;
    S32 sHintAge;
    // The black is drawn this many times, kBlackEvery frames apart, then no
    // more, in case a picture is lost (an older build's first one never
    // reached the screen in Azahar).
    S32 sBlackDraws;
    // A probe build's banner, put back once the bottom screen is black and
    // citro3d has long finished drawing there (after the menu, HOME, sleep).
    bool sBannerDue;
    const S32 kBlackDraws = 3;
    const S32 kBlackEvery = 30;
    const S32 kHintEvery = 60;
    const U64 kFadeOutMs = 1000;
    const U64 kFadeInMs = 250;
    // A longer gap between two frames (a load, HOME, sleep) does not count as
    // time the line was there to be read.
    const U64 kStallMs = 100;

    // START or SELECT pressed alone is held back from the game for a moment
    // (TSSM_3DS_COMBO_MS, 0: off): the game acts on each press by itself
    // (START pauses, SELECT opens the upgrade window), and the other button
    // of the combo may follow. `sAlone` is the one held back (1 START, 2
    // SELECT), since `sAloneAt`; a tap let go inside the moment reaches the
    // game for one frame, late.
    U32 sComboMs = 120;
    U32 sAlone;
    U64 sAloneAt;
    bool sPrevStart;
    bool sPrevSelect;

    // ImGui's heap, counted (the stats line and the close).
    size_t sHeapUsed;
    size_t sHeapPeak;

    // Frames since the last input while open (Frame()), and an input seen
    // by the event watch since the last frame.
    S32 sIdleFrames;
    bool sInputSeen;
    const S32 kBusyFrames = 4;
    const S32 kIdleEvery = 60;

    // TSSM_3DS_GUI_EVERY=N (a measuring switch, 1 by default): while in use,
    // ImGui is built on one frame in N only.
    S32 sBuildEvery = 1;
#endif

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

    // TSSM_GUI_SCRIPT: input played into the overlay, a step at a given frame
    // after it opened -- controller buttons, keys, taps, drags, captures --
    // so a test can drive a tab (the Saves tab's file picker, say) the way a
    // player would. TSSM_GUI_SCRIPT_SHOTS names the captures ("%s" = the
    // step's name).
    struct ScriptStep
    {
        S32 frame;
        char action[96];
    };
    ScriptStep sScript[96];
    S32 sScriptCount;
    S32 sScriptFrame;
    char sScriptShots[480];
    ImGuiKey sScriptKey = ImGuiKey_None;
    S32 sScriptKeyUp;
    S32 sScriptMouseUp;
    bool sScriptMouse; // the script has the mouse: its position wins every frame
    float sMousePos[2];
    float sDragFrom[2];
    float sDragTo[2];
    S32 sDragStart;
    S32 sDragFrames;

    // ---------------------------------------------------------------------------

    void SetOpen(bool open);
    void ReadConfig();
    void UpdateScale();

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

#ifdef __3DS__
    // START and SELECT as pressed, on any open pad.
    void PadStartSelect(bool* start, bool* select)
    {
        *start = false;
        *select = false;
        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        if (ids != NULL)
        {
            for (int i = 0; i < n; i++)
            {
                SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
                if (pad != NULL)
                {
                    *start = *start || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
                    *select = *select || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
                }
            }
            SDL_free(ids);
        }
    }

    // Any button held or stick pushed on an open pad: ImGui repeats a held
    // direction, so those frames are built.
    bool PadActive()
    {
        bool active = false;
        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        for (int i = 0; ids != NULL && i < n && !active; i++)
        {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
            if (pad == NULL)
            {
                continue;
            }
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !active; b++)
            {
                active = SDL_GetGamepadButton(pad, (SDL_GamepadButton)b);
            }
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT && !active; a++)
            {
                const int v = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)a);
                active = v > 8000 || v < -8000;
            }
        }
        SDL_free(ids);
        return active;
    }

    // The game's buttons a pad input is bound to ("start", "back"), so a
    // rebinding is followed.
    U32 BoundBits(const char* token)
    {
        S32 rows[8];
        const S32 n = iPadControlsWhoUses(IPAD_BIND_PAD, token, rows, 8);
        U32 bits = 0;
        for (S32 i = 0; i < n && i < 8; i++)
        {
            iPadControlsRowInfo info;
            if (iPadControlsRow(rows[i], &info))
            {
                bits |= info.mask;
            }
        }
        return bits;
    }

    void* HeapAlloc(size_t n, void*)
    {
        size_t* p = (size_t*)malloc(n + 8);
        if (p == NULL)
        {
            return NULL;
        }
        p[0] = n;
        sHeapUsed += n;
        if (sHeapUsed > sHeapPeak)
        {
            sHeapPeak = sHeapUsed;
        }
        return (char*)p + 8;
    }

    void HeapFree(void* ptr, void*)
    {
        if (ptr != NULL)
        {
            size_t* p = (size_t*)((char*)ptr - 8);
            sHeapUsed -= p[0];
            free(p);
        }
    }

    // The closed overlay's bottom screen.
    void DrawHint()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##hint", NULL, flags))
        {
            char text[64];
            snprintf(text, sizeof(text), "%s: menu", iGui::PadComboName());
            const ImVec2 size = ImGui::CalcTextSize(text);
            const char* note = sNoSoundNote ? i3DSNoSoundText() : NULL;
            if (note != NULL)
            {
                // The no-sound note, the tap that hides it, then the line.
                const char* const tap = "Tap the screen to hide this note.";
                const float margin = ImGui::GetFontSize();
                const float wrap = io.DisplaySize.x - 2.0f * margin;
                const float gap = ImGui::GetTextLineHeight();
                const ImVec2 noteSize = ImGui::CalcTextSize(note, NULL, false, wrap);
                const ImVec2 tapSize = ImGui::CalcTextSize(tap);
                const float total = noteSize.y + gap + tapSize.y + gap + size.y;
                const float top = (io.DisplaySize.y - total) * 0.5f;
                ImGui::SetCursorPos(ImVec2(margin, top > 0.0f ? top : 0.0f));
                ImGui::PushTextWrapPos(margin + wrap);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(250, 210, 80, 255));
                ImGui::TextUnformatted(note);
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - tapSize.x) * 0.5f,
                                           ImGui::GetCursorPosY() + gap));
                ImGui::TextUnformatted(tap);
                ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f,
                                           ImGui::GetCursorPosY() + gap));
                ImGui::TextDisabled("%s", text);
            }
            else
            {
                ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f,
                                           (io.DisplaySize.y - size.y) * 0.5f));
                ImGui::TextDisabled("%s", text);
            }
        }
        ImGui::End();
    }

    void HintEnter(HintState state, U64 ms)
    {
        static const char* const kNames[] = { "shown", "fading out", "black", "fading in" };
        sHint = state;
        sHintMs = ms;
        sHintDue = true;
        sBlackDraws = state == HINT_BLACK ? kBlackDraws : 0;
        printf("tssm: gui: bottom screen: %s (%.1f s)\n", kNames[state],
               (double)SDL_GetTicks() / 1000.0);
        fflush(stdout);
    }

    // 0 to 256: how much of the line and its background are seen.
    int HintBrightness()
    {
        switch (sHint)
        {
        case HINT_FADE_OUT:
            return sHintMs >= kFadeOutMs ? 0 : (int)(256 - sHintMs * 256 / kFadeOutMs);
        case HINT_BLACK:
            return 0;
        case HINT_FADE_IN:
            return sHintMs >= kFadeInMs ? 256 : (int)(sHintMs * 256 / kFadeInMs);
        default:
            return 256;
        }
    }

    // The closed overlay's frame: TRUE when it draws the bottom screen.
    bool ClosedFrame()
    {
        const U64 now = SDL_GetTicks();
        U64 dt = sHintLastAt != 0 ? now - sHintLastAt : 0;
        sHintLastAt = now;
        if (dt > kStallMs)
        {
            dt = 0;
        }

        // After HOME or sleep, whatever the system left on the screen.
        if (i3DSResumeCount() != sResumes)
        {
            sResumes = i3DSResumeCount();
            sHintDue = true;
            if (sHint == HINT_BLACK && i3DSProbeBannerShown())
            {
                sBannerDue = true;
                sHintAge = 0;
            }
            else if (sHint == HINT_BLACK && sBlackDraws == 0)
            {
                sBlackDraws = kBlackDraws;
            }
        }

        if (sTapped && sNoSoundNote)
        {
            // The note goes; the line stays as long as after start-up.
            sTapped = false;
            sNoSoundNote = false;
            printf("tssm: gui: bottom screen: a tap hid the no-sound note\n");
            if (sHintSeconds == 0)
            {
                HintEnter(HINT_FADE_OUT, 0);
            }
            else
            {
                sHintMs = 0;
                sHintDue = true;
            }
        }
        if (sTapped)
        {
            sTapped = false;
            if (sHintSeconds > 0)
            {
                if (sHint == HINT_SHOWN)
                {
                    printf("tssm: gui: bottom screen: a tap (the line stays %d s more)\n",
                           (int)sHintSeconds);
                    sHintMs = 0;
                }
                else if (sHint != HINT_FADE_IN)
                {
                    printf("tssm: gui: bottom screen: a tap\n");
                    // From the brightness it has now.
                    HintEnter(HINT_FADE_IN, (U64)HintBrightness() * kFadeInMs / 256);
                }
            }
        }

        sHintMs += dt;
        switch (sHint)
        {
        case HINT_SHOWN:
            if (!sNoSoundNote && sHintSeconds > 0 && sHintMs >= (U64)sHintSeconds * 1000)
            {
                HintEnter(HINT_FADE_OUT, 0);
            }
            break;
        case HINT_FADE_OUT:
            if (sHintMs >= kFadeOutMs)
            {
                HintEnter(HINT_BLACK, 0);
            }
            break;
        case HINT_FADE_IN:
            if (sHintMs >= kFadeInMs)
            {
                HintEnter(HINT_SHOWN, 0);
            }
            break;
        default:
            break;
        }

        // Shown, the line is drawn again every few seconds (a picture can be
        // lost); fading, every frame; black, a few times and then never.
        sHintAge++;
        bool draw = sHintDue;
        switch (sHint)
        {
        case HINT_SHOWN:
            draw = draw || sHintAge >= kHintEvery;
            break;
        case HINT_BLACK:
            if (sBannerDue && sBlackDraws == 0 && sHintAge >= kBlackEvery)
            {
                // Straight into the screen's two buffers, as at start-up.
                sBannerDue = false;
                sHintDue = false;
                sHintAge = 0;
                i3DSProbeBanner();
                return false;
            }
            draw = sBlackDraws > 0 && (draw || sHintAge >= kBlackEvery);
            break;
        default:
            draw = true;
            break;
        }
        if (!draw)
        {
            return false;
        }
        if (sHint == HINT_BLACK)
        {
            sBlackDraws--;
        }
        sHintDue = false;
        sHintAge = 0;

        const int bright = HintBrightness();
        iGuiRenderSetBrightness(bright);
        iGuiRenderReleaseVolatile(); // drawn even if ImGui's picture is the same
        ImGui::SetCurrentContext(sContext);
        UpdateScale();
        iGuiRenderNewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::GetIO().DisplaySize = ImVec2(320.0f, 240.0f);
        ImGui::NewFrame();
        // Black is the background colour alone: no window to build.
        if (bright > 0)
        {
            DrawHint();
        }
        ImGui::Render();
        return true;
    }
#endif

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

        if (sCaptureTab >= 0)
        {
            sSelectTab = sCaptureTab;
        }

        // "30:tab:saves 40:a 60:down 80:shot:list 200:quit": steps apart by
        // spaces or commas, each "<frame>:<action>". Actions: a b x y (the
        // controller's face buttons by position: a is the bottom one), up
        // down left right, l r, enter esc backspace tab space kup kdown,
        // click:X:Y, drag:X:Y:X2:Y2, wheel:X:Y:N, tab:<name>, shot:<name>,
        // quit.
        const char* script = getenv("TSSM_GUI_SCRIPT");
        for (const char* c = script; c != NULL && *c != '\0';)
        {
            while (*c == ' ' || *c == ',' || *c == '\n')
            {
                c++;
            }
            size_t len = strcspn(c, " ,\n");
            if (len == 0)
            {
                break;
            }
            const char* colon = (const char*)memchr(c, ':', len);
            if (colon != NULL && sScriptCount < (S32)(sizeof(sScript) / sizeof(sScript[0])))
            {
                ScriptStep& s = sScript[sScriptCount++];
                s.frame = atoi(c);
                snprintf(s.action, sizeof(s.action), "%.*s", (int)(len - (colon + 1 - c)),
                         colon + 1);
            }
            c += len;
        }
        const char* shots = getenv("TSSM_GUI_SCRIPT_SHOTS");
        snprintf(sScriptShots, sizeof(sScriptShots), "%s", shots != NULL ? shots : "");
        if (sScriptCount > 0)
        {
            printf("tssm: gui: TSSM_GUI_SCRIPT: %d steps\n", (int)sScriptCount);
        }
    }

    ImGuiKey ScriptKey(const char* name)
    {
        static const struct
        {
            const char* name;
            ImGuiKey key;
        } kKeys[] = {
            { "up", ImGuiKey_GamepadDpadUp },
            { "down", ImGuiKey_GamepadDpadDown },
            { "left", ImGuiKey_GamepadDpadLeft },
            { "right", ImGuiKey_GamepadDpadRight },
            // By position, as SDL reports them: "a" is the bottom button.
            { "a", ImGuiKey_GamepadFaceDown },
            { "b", ImGuiKey_GamepadFaceRight },
            { "x", ImGuiKey_GamepadFaceLeft },
            { "y", ImGuiKey_GamepadFaceUp },
            { "l", ImGuiKey_GamepadL1 },
            { "r", ImGuiKey_GamepadR1 },
            { "enter", ImGuiKey_Enter },
            { "esc", ImGuiKey_Escape },
            { "backspace", ImGuiKey_Backspace },
            { "tab", ImGuiKey_Tab },
            { "kdown", ImGuiKey_DownArrow },
            { "kup", ImGuiKey_UpArrow },
            { "space", ImGuiKey_Space },
        };
        for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
        {
            if (strcmp(name, kKeys[i].name) == 0)
            {
                return kKeys[i].key;
            }
        }
        return ImGuiKey_None;
    }

    // Between the backend's NewFrame and ImGui's: what the script presses
    // this frame goes into ImGui's input queue as if a device had said it.
    void PlayScript()
    {
        if (sScriptCount == 0)
        {
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        sScriptFrame++;
        // The backend drops this flag with no controller open; the script is one.
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        if (sScriptKey != ImGuiKey_None && sScriptFrame >= sScriptKeyUp)
        {
            io.AddKeyEvent(sScriptKey, false);
            sScriptKey = ImGuiKey_None;
        }
        // Once the script has clicked, its pointer is the one ImGui sees: the
        // backend reports the real cursor every frame, and this comes after.
        if (sDragFrames > 0)
        {
            const float t = (float)(sScriptFrame - sDragStart) / (float)sDragFrames;
            const float k = t > 1.0f ? 1.0f : t;
            sMousePos[0] = sDragFrom[0] + (sDragTo[0] - sDragFrom[0]) * k;
            sMousePos[1] = sDragFrom[1] + (sDragTo[1] - sDragFrom[1]) * k;
            if (t >= 1.0f)
            {
                sDragFrames = 0;
                sScriptMouseUp = sScriptFrame + 2;
            }
        }
        if (sScriptMouse)
        {
            io.AddMousePosEvent(sMousePos[0], sMousePos[1]);
        }
        if (sScriptMouseUp > 0 && sScriptFrame >= sScriptMouseUp)
        {
            io.AddMouseButtonEvent(0, false);
            sScriptMouseUp = 0;
        }

        for (S32 i = 0; i < sScriptCount; i++)
        {
            const ScriptStep& s = sScript[i];
            if (s.frame != sScriptFrame)
            {
                continue;
            }
            printf("tssm: gui: script %d: %s\n", (int)s.frame, s.action);
            fflush(stdout);

            float x = 0.0f;
            float y = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            if (strncmp(s.action, "shot:", 5) == 0)
            {
                char path[600];
                if (strstr(sScriptShots, "%s") != NULL)
                {
                    snprintf(path, sizeof(path), sScriptShots, s.action + 5);
                }
                else
                {
                    snprintf(path, sizeof(path), "%s_%s.png", sScriptShots, s.action + 5);
                }
                iGuiRenderRequestCapture(path);
            }
            else if (strncmp(s.action, "tab:", 4) == 0)
            {
                for (S32 t = 0; t < TAB_COUNT; t++)
                {
                    if (SDL_strcasecmp(s.action + 4, kTabNames[t]) == 0)
                    {
                        sSelectTab = t;
                    }
                }
            }
            else if (sscanf(s.action, "drag:%f:%f:%f:%f", &x, &y, &x2, &y2) == 4)
            {
                sScriptMouse = true;
                sMousePos[0] = x;
                sMousePos[1] = y;
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, true);
                sDragFrom[0] = x;
                sDragFrom[1] = y;
                sDragTo[0] = x2;
                sDragTo[1] = y2;
                sDragStart = sScriptFrame;
                sDragFrames = 12;
            }
            else if (sscanf(s.action, "click:%f:%f", &x, &y) == 2)
            {
                sScriptMouse = true;
                sMousePos[0] = x;
                sMousePos[1] = y;
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, true);
                sScriptMouseUp = sScriptFrame + 2;
            }
            else if (sscanf(s.action, "wheel:%f:%f:%f", &x, &y, &x2) == 3)
            {
                sScriptMouse = true;
                sMousePos[0] = x;
                sMousePos[1] = y;
                io.AddMousePosEvent(x, y);
                io.AddMouseWheelEvent(0.0f, x2);
            }
            else if (strcmp(s.action, "quit") == 0)
            {
                printf("tssm: gui: TSSM_GUI_SCRIPT done, exiting\n");
                fflush(stdout);
                exit(0);
            }
            else if (ScriptKey(s.action) != ImGuiKey_None)
            {
                if (sScriptKey != ImGuiKey_None)
                {
                    io.AddKeyEvent(sScriptKey, false);
                }
                sScriptKey = ScriptKey(s.action);
                io.AddKeyEvent(sScriptKey, true);
                sScriptKeyUp = sScriptFrame + 2;
            }
            else
            {
                printf("tssm: gui: script: no such step '%s'\n", s.action);
            }
        }
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
#ifdef __3DS__
        // The width of the screen, the text wrapped to it.
        ImGui::SetNextWindowPos(ImVec2(0.0f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.0f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0.0f));
        const ImGuiWindowFlags quitFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
#else
        const ImGuiWindowFlags quitFlags = ImGuiWindowFlags_AlwaysAutoResize;
#endif
        if (ImGui::BeginPopupModal("Quit the game?", NULL, quitFlags))
        {
#ifdef __3DS__
            ImGui::TextWrapped("Return to the %s? Progress since the last save is lost.",
                               i3DSMenuName());
#elif TSSM_GUI_CONSOLE
            ImGui::TextUnformatted("Return to the " TSSM_GUI_HOMEBREW
                                   "? Progress since the last save is "
                                   "lost.");
#else
            ImGui::TextUnformatted("Quit to the desktop? Progress since the last save is lost.");
#endif
            ImGui::Spacing();
            if (ImGui::Button("Quit"))
            {
                iWindowRequestClose();
                SetOpen(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || iGui::BackPressed())
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

#ifdef __3DS__
        // A screen of its own: the window is all of it, with no title bar
        // (About carries the title).
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
#else
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
#endif
        if (sJustOpened)
        {
            ImGui::SetNextWindowFocus();
            sJustOpened = false;
        }

        // The tab scrolls, not the window: its height is set to fit.
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar |
#ifdef __3DS__
                                       ImGuiWindowFlags_NoTitleBar |
#endif
                                       ImGuiWindowFlags_NoScrollWithMouse;

#if TSSM_GUI_CONSOLE
        const char* const title = "The SpongeBob SquarePants Movie###tssm_overlay";
#else
        const char* const title = "The SpongeBob SquarePants Movie  |  PC port###tssm_overlay";
#endif
        if (ImGui::Begin(title, NULL, flags))
        {
            if (iGui::SaveError() != NULL)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 110, 255));
                ImGui::TextWrapped("%s", iGui::SaveError());
                ImGui::PopStyleColor();
            }
#ifdef TSSM_UPDATE
            iGui::UpdatesNotice();
#endif
            if (iGui::AnyRestartPending())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(250, 210, 80, 255));
#ifdef __3DS__
                ImGui::TextWrapped("Changes marked \"restart\" apply the next time the game is "
                                   "started from the %s.",
                                   i3DSMenuName());
#elif TSSM_GUI_CONSOLE
                ImGui::TextWrapped("Changes marked \"restart\" apply the next time the game is "
                                   "started from the " TSSM_GUI_HOMEBREW
                                   ". Everything else has already "
                                   "taken effect.");
#else
                ImGui::TextWrapped("Changes marked \"restart\" apply the next time the game "
                                   "starts. Everything else has already taken effect.");
#endif
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
#if TSSM_GUI_CONSOLE
                        sShownTab = t;
#endif
                        ImGui::BeginChild("tab", ImVec2(0.0f, -footer), ImGuiChildFlags_NavFlattened);
#ifdef __3DS__
                        // Every line wraps at the screen's edge rather than run off it.
                        ImGui::PushTextWrapPos(0.0f);
#endif
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
                        default:
                            iGui::DrawAbout();
                            break;
                        }
#ifdef __3DS__
                        ImGui::PopTextWrapPos();
#endif
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            sSelectTab = -1;
#if TSSM_GUI_CONSOLE
            // L and R change the tab; the d-pad would have to climb past every
            // row of a long tab. Not while a popup (the file browser pages
            // with L and R) or an edit is open.
            if (!ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen(NULL,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            {
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
                {
                    sSelectTab = (sShownTab + TAB_COUNT - 1) % TAB_COUNT;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
                {
                    sSelectTab = (sShownTab + 1) % TAB_COUNT;
                }
            }
#endif

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

            // The GUI's own file browser, for the tabs that ask for a file
            // where the system has no dialog.
            iGui::DrawFilePicker();

            const char* keyName = SDL_GetScancodeName(sKey);
            ImGui::SameLine();
#ifdef __3DS__
            // The menu buttons by position, as in the game: the bottom one (B)
            // chooses, the right one (A) goes back.
            ImGui::TextDisabled("B choose  A back  L/R tab");
            (void)keyName;
#elif defined(__SWITCH__)
            // No keyboard to name. The menu buttons go by position, as in the
            // game: the bottom one (B) chooses, the right one (A) goes back.
            ImGui::TextDisabled("B choose   A back   L/R tab   %s close", iGui::PadComboName());
            (void)keyName;
#else
            ImGui::TextDisabled("%s, Esc%s%s: close. The game takes no input while this is open.",
                                (keyName != NULL && keyName[0] != '\0') ? keyName : "F1",
                                sPadCombo ? ", " : "", sPadCombo ? iGui::PadComboName() : "");
#endif
        }
        ImGui::End();

        if (sOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() &&
            !iGui::FilePickerTookEscape() &&
            !ImGui::IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            SetOpen(false);
        }
    }

    // One frame, at the present. See iGuiRender.h.
    bool Frame()
    {
        sPresents++;
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
#ifdef __3DS__
            // The bottom screen keeps its picture; the closed overlay draws
            // only when it changes or is due.
            return ClosedFrame();
#else
            return false;
#endif
        }

#ifdef __3DS__
        // An ImGui frame costs the 3DS's CPU a good part of the game's own
        // frame, and with nothing pressed or touched the picture does not
        // change: a few frames after the last input, it is built only every
        // two seconds (for what changes by itself), and the bottom screen
        // keeps its picture meanwhile.
        // Back from HOME or sleep counts as input: the bottom screen needs its
        // picture again now, not at the next idle build.
        if (i3DSResumeCount() != sResumes)
        {
            sResumes = i3DSResumeCount();
            sInputSeen = true;
        }
        if (sInputSeen || PadActive() || sScriptCount > 0 || (sCapturing && !sCapturesFinished))
        {
            sIdleFrames = 0;
        }
        sInputSeen = false;
        if (sIdleFrames++ >= kBusyFrames && (sIdleFrames - kBusyFrames) % kIdleEvery != 0)
        {
            return false;
        }
        if (sBuildEvery > 1 && sPresents % sBuildEvery != 0 && sScriptCount == 0 && !sCapturing)
        {
            return false;
        }
#endif

        ImGui::SetCurrentContext(sContext);
        ReadConfig();
        UpdateGamepads();
        UpdateScale();

        iGuiRenderNewFrame();
        ImGui_ImplSDL3_NewFrame();
#ifdef __3DS__
        // The bottom screen, whatever SDL makes of its window.
        ImGui::GetIO().DisplaySize = ImVec2(320.0f, 240.0f);
        ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#endif
        PlayScript();
        ImGui::NewFrame();

        iGuiRenderBackground();
        DrawOverlay();

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

#ifdef __3DS__
            sIdleFrames = 0;
            iGuiRenderSetBrightness(256);
            // No mouse: nothing is under a pointer until the screen is touched.
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
#else
            float x = 0.0f;
            float y = 0.0f;
            SDL_GetMouseState(&x, &y);
            io.AddMousePosEvent(x, y);
#endif

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
#ifdef __3DS__
            // Back to black: the line is not shown again until a tap. The
            // no-sound note comes back, until one.
            HintEnter(sHintSeconds < 0 || sNoSoundNote ? HINT_SHOWN : HINT_BLACK, 0);
            sHintLastAt = 0;
            sTapped = false;
            if (i3DSProbeBannerShown())
            {
                // One black picture over the menu's, then the banner again.
                sBlackDraws = 1;
                sBannerDue = true;
            }
            printf("tssm: gui: overlay closed (ImGui's heap: %u KB in use, peak %u KB)\n",
                   (unsigned)(sHeapUsed >> 10), (unsigned)(sHeapPeak >> 10));
#else
            printf("tssm: gui: overlay closed\n");
#endif
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

#ifdef __3DS__
        // The bottom screen's touches go to ImGui as they are, its own
        // pointer: SDL's touch-to-mouse is off (iWindow3DS.cpp).
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
            if (!sOpen)
            {
                // Closed, a tap brings the line back. Nothing else reads the
                // touch screen: iWindowPump drops its events.
                sTapped = sTapped || e->type == SDL_EVENT_FINGER_DOWN;
            }
            else
            {
                sInputSeen = true;
                ImGui::SetCurrentContext(sContext);
                ImGuiIO& io = ImGui::GetIO();
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                // SDL's 3DS driver divides by 319 and 239. A lift reports
                // (0, 0), the panel's "nothing touched": it lets go where the
                // finger last was.
                if (e->type != SDL_EVENT_FINGER_UP)
                {
                    io.AddMousePosEvent(e->tfinger.x * 319.0f, e->tfinger.y * 239.0f);
                }
                if (e->type != SDL_EVENT_FINGER_MOTION)
                {
                    io.AddMouseButtonEvent(0, e->type == SDL_EVENT_FINGER_DOWN);
                }
            }
            return true;
#else
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
#endif

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
#ifdef __3DS__
            switch (e->type)
            {
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                sInputSeen = true;
                break;
            default:
                break;
            }
#endif
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
#if TSSM_GUI_CONSOLE
        // Docked with no keyboard nothing else opens the overlay.
        sPadCombo = true;
#else
        sPadCombo = iConfigGetBool("gui.pad_combo", TRUE) != 0;
#endif
    }

#ifdef __3DS__
    // START or SELECT going down alone: hidden from the game for sComboMs, in
    // case the other one follows (then the overlay opens, and the game sees
    // neither). Otherwise it reaches the game late: still held, it shows; let
    // go already, it is pressed for one frame.
    const iPadHostState* HoldBack(const iPadHostState* host)
    {
        bool start = false;
        bool select = false;
        PadStartSelect(&start, &select);
        const bool startDown = start && !sPrevStart;
        const bool selectDown = select && !sPrevSelect;
        sPrevStart = start;
        sPrevSelect = select;
        const U64 now = SDL_GetTicks();

        iPadHostState* out = &sQuiet[0];
        *out = *host;

        if (sAlone == 0 && (startDown != selectDown) && !(start && select))
        {
            sAlone = startDown ? 1u : 2u;
            sAloneAt = now;
        }
        if (sAlone == 0)
        {
            return host;
        }

        const bool held = sAlone == 1 ? start : select;
        const bool inTime = now - sAloneAt < sComboMs;
        const U32 bits = BoundBits(sAlone == 1 ? "start" : "back");
        if (start && select)
        {
            // The combo: PadComboHeld opens the overlay on this frame.
            sAlone = 0;
            out->buttons &= ~(BoundBits("start") | BoundBits("back"));
            return out;
        }
        if (held && inTime)
        {
            out->buttons &= ~bits;
            return out;
        }
        if (!held && inTime)
        {
            // A tap: the press, one frame late, then the release.
            sAlone = 0;
            out->buttons |= bits;
            return out;
        }
        sAlone = 0;
        return host;
    }
#endif
} // namespace

// --- the overlay's interface ------------------------------------------------------

void iGuiInit()
{
    if (sReady)
    {
        return;
    }

#ifdef __3DS__
    // A console the overlay does not work on keeps it off for good.
    if (iConfigGetBool("gui.overlay", TRUE) == 0)
    {
        printf("tssm: gui: [gui] overlay = off: no overlay; START and SELECT go to the game\n");
        fflush(stdout);
        return;
    }
    // TSSM_3DS_GUI=off as well, for a test that must not touch the bottom
    // screen (the console probes).
    const char* gui = getenv("TSSM_3DS_GUI");
    if (gui != NULL && strcmp(gui, "off") == 0)
    {
        printf("tssm: gui: TSSM_3DS_GUI=off: no overlay; START and SELECT go to the game\n");
        fflush(stdout);
        return;
    }
    // Counted, for the stats line and the close.
    ImGui::SetAllocatorFunctions(HeapAlloc, HeapFree, NULL);
    const char* comboMs = getenv("TSSM_3DS_COMBO_MS");
    if (comboMs != NULL && comboMs[0] != '\0')
    {
        const int ms = atoi(comboMs);
        sComboMs = (U32)(ms < 0 ? 0 : (ms > 1000 ? 1000 : ms));
    }
    // The line on the closed overlay's bottom screen: seconds, -1 for good, 0
    // never. A probe's banner (i3DSProbeBanner) is left as it is.
    sHintSeconds = iConfigGetInt("gui.bottom_hint_seconds", 10);
    sHintSeconds = sHintSeconds < -1 ? -1 : (sHintSeconds > 3600 ? 3600 : sHintSeconds);
    if (i3DSProbeBannerShown())
    {
        sHintSeconds = 0;
        sHint = HINT_BLACK;
        sBlackDraws = 0;
    }
    else if (sHintSeconds == 0)
    {
        sHint = HINT_BLACK;
        sBlackDraws = kBlackDraws;
    }
    printf("tssm: gui: bottom screen: the line %s\n",
           i3DSProbeBannerShown() ? "is not drawn (the probe's banner stays)"
           : sHintSeconds < 0     ? "stays"
           : sHintSeconds == 0    ? "is not shown (black)"
                                  : "fades to black after a while");
    // No sound (no DSP firmware at start-up): the note, until a tap.
    if (!i3DSProbeBannerShown() && i3DSNoSoundText() != NULL)
    {
        sNoSoundNote = true;
        sHint = HINT_SHOWN;
        sHintMs = 0;
        sBlackDraws = 0;
        printf("tssm: gui: bottom screen: the no-sound note, until a tap\n");
    }
    const char* every = getenv("TSSM_3DS_GUI_EVERY");
    if (every != NULL && every[0] != '\0')
    {
        const int n = atoi(every);
        sBuildEvery = n < 1 ? 1 : (n > 4 ? 4 : n);
        printf("tssm: gui: TSSM_3DS_GUI_EVERY=%d: the menu is built on one frame in %d\n",
               (int)sBuildEvery, (int)sBuildEvery);
    }
#endif

    // The game's window. There is exactly one by now: iWindowSDL.cpp made it
    // under Direct3D, librw under OpenGL; on the 3DS it is the bottom
    // screen's (iWindow3DS.cpp).
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

#ifdef __3DS__
    bool platform = ImGui_ImplSDL3_InitForOther(sWindow);
    if (platform)
    {
        // Nothing may open the system keyboard: no field here takes text.
        ImGui::GetPlatformIO().Platform_SetImeDataFn = NULL;
    }
#else
    const bool gl = iScreenGetBackend() == iSCREENBACKEND_GL3;
    bool platform = gl ? ImGui_ImplSDL3_InitForOpenGL(sWindow, SDL_GL_GetCurrentContext()) :
                         ImGui_ImplSDL3_InitForD3D(sWindow);
#endif

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

#if TSSM_GUI_CONSOLE
    printf("tssm: gui: overlay ready (%s, Dear ImGui %s); %s opens it\n", renderer, IMGUI_VERSION,
           iGui::PadComboName());
#else
    printf("tssm: gui: overlay ready (%s, Dear ImGui %s); %s%s%s opens it\n", renderer,
           IMGUI_VERSION, SDL_GetScancodeName(sKey), sPadCombo ? " or " : "",
           sPadCombo ? iGui::PadComboName() : "");
#endif
    fflush(stdout);

    ReadDebugSwitches();

#ifdef TSSM_UPDATE
    // The Updates section's core, and the check at start-up when the player
    // turned it on.
    iGui::UpdatesStartup();
#endif

    const char* open = getenv("TSSM_GUI_OPEN");
    if (((open != NULL && atoi(open) != 0) || sCapturing) && sOpenAt <= 0)
    {
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

#ifdef TSSM_UPDATE
    iUpdateShutdown();
#endif

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
#ifdef __3DS__
        sAlone = 0;
#endif
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
#ifdef __3DS__
            sAlone = 0;
#endif
            return masked;
        }
    }

#ifdef __3DS__
    if (port == 0 && sPadCombo && sComboMs > 0)
    {
        return HoldBack(host);
    }
#endif
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
