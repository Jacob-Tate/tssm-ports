// The SDL input backend, and the default one.
//
// TSSM-ADAPTED from treedome's. What changed: the sticks are passed on raw
// (iPad.cpp applies the stick curve), the triggers' analog values are kept and
// click at input.trigger_threshold, the keyboard and mouse can play alongside a
// controller (input.keyboard), the pad kinds are xbox / playstation / switch /
// gamecube, and the probe is TSSM_INPUT_TEST in iPad.cpp.
//
// It exists for the devices XInput cannot see: a DualSense, a Switch Pro
// controller, a GameCube adapter or any generic USB pad reports nothing through
// XInput. SDL enumerates all of them, and carries a controller database that
// maps every pad it knows onto one standard layout, so the token names below
// mean the same physical position everywhere.
//
// Only SDL_INIT_GAMEPAD is initialised here. iWindowSDL.cpp owns the window
// and the video subsystem; the keyboard is read through SDL's keyboard state
// (iPadKeyboardSDL.cpp), which that window's event pump keeps current.

#include "iPadHost.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadBind.h"
#include "iMouseLook.h"
#include "iPadKeyboard.h"
#include "iPadLayout.h"
#include "iPadSettings.h"
#include "iPadTokens.h"
#include "iPadTouch.h"
#include "iPerf.h"
#include "iTime.h"
#include "xPad.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// What each slot is reporting, before any of it is assigned to a game port.
// "Slot" is the way the machine counts controllers, as opposed to the way the
// game counts ports, so that input.controller means one thing.
static iPadHostState sSlot[IPAD_MAX_CONTROLLERS];
static S32 sPortSlot[IPAD_MAX_CONTROLLERS];

static iPadHostState sState[IPAD_MAX_CONTROLLERS];
static bool sKeyboardOnly;

#ifdef __ANDROID__
// The system back button, as a press of Start. It arrives as SDL_SCANCODE_AC_BACK
// and is caught by a watch, because a tap can go down and up inside one pump and
// never show in the key state. The watch runs on whichever thread posted the
// event, hence the atomic.
static SDL_AtomicInt sBackPresses;
static S32 sBackFrames;

static bool SDLCALL WatchBackButton(void*, SDL_Event* e)
{
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_AC_BACK && !e->key.repeat)
    {
        SDL_AddAtomicInt(&sBackPresses, 1);
    }
    return true;
}
#endif

// Android and iOS block the game thread while the app is in the background.
// The game clock stops for that time, so the frame after it is an ordinary
// frame and not the whole absence at once.
static bool SDLCALL WatchBackground(void*, SDL_Event* e)
{
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND)
    {
        iTimeSuspend();
    }
    else if (e->type == SDL_EVENT_DID_ENTER_FOREGROUND)
    {
        iTimeResume();
    }
    return true;
}

static S32 sPinnedSlot = -1;
static bool sReady;

// The open gamepad in each slot, and the SDL instance id that put it there. A
// gamepad takes the lowest free slot when it arrives and gives that slot back
// when it leaves, which is what makes a slot number stable enough for someone
// to name one in a settings file.
static SDL_Gamepad* sGamepad[IPAD_MAX_CONTROLLERS];
static SDL_JoystickID sInstance[IPAD_MAX_CONTROLLERS];

// Port 0 as a script holds it; see iPadHostScript.
static bool sScripted;
static U32 sScriptButtons;

// ---------------------------------------------------------------------------
// Buttons

static iPadBind sPadBind[IPAD_BIND_MAX_BUTTONS];
static U32 sBindGeneration;

static void LoadPadBindings()
{
    S32 count;
    const iPadBindToken* tokens = iPadPadTokens(&count);
    iPadBindLoad(IPAD_BIND_PAD, tokens, count, sPadBind);
    sBindGeneration++;
}

const iPadBind* iPadHostPadBinds()
{
    return sPadBind;
}

U32 iPadHostBindGeneration()
{
    return sBindGeneration;
}

static const char* PadTokenName(S16 id)
{
    S32 count;
    const iPadBindToken* tokens = iPadPadTokens(&count);
    return iPadBindTokenName(id, tokens, count);
}

static U32 sPadHeld;

static bool PadInputHeld(S16 id)
{
    return (sPadHeld & (1u << id)) != 0;
}

// Every input on one pad that a binding can name, as PADIN_* bits. The
// triggers are axes and click at input.trigger_threshold.
static U32 ReadInputs(SDL_Gamepad* pad, F32* triggerL, F32* triggerR)
{
    U32 held = 0;

    for (S32 b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
    {
        S32 input = iPadInputFromSDLButton(b);
        if (input >= 0 && SDL_GetGamepadButton(pad, (SDL_GamepadButton)b))
        {
            held |= 1u << input;
        }
    }

    F32 l = (F32)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    F32 r = (F32)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
    l = l < 0.0f ? 0.0f : (l > 1.0f ? 1.0f : l);
    r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);

    const F32 threshold = iPadSettingsGet().trigger_threshold;
    if (l >= threshold)
    {
        held |= 1u << PADIN_LT;
    }
    if (r >= threshold)
    {
        held |= 1u << PADIN_RT;
    }

    if (triggerL != NULL)
    {
        *triggerL = l;
    }
    if (triggerR != NULL)
    {
        *triggerR = r;
    }
    return held;
}

static F32 AxisRow(U32 axes, S32 negative, S32 positive)
{
    F32 v = 0.0f;
    if (axes & (1u << negative))
    {
        v -= 1.0f;
    }
    if (axes & (1u << positive))
    {
        v += 1.0f;
    }
    return v;
}

static void ReadPad(SDL_Gamepad* pad, iPadHostState* s)
{
    sPadHeld = ReadInputs(pad, &s->trigger_l, &s->trigger_r);

    U32 axes = 0;
    s->buttons = iPadBindEvaluate(sPadBind, PadInputHeld, &axes);

    // Raw, Y up-positive: SDL reports Y down-positive, as a screen does, and
    // iPadHost.h asks for the GameCube's sense. The stick curve is iPad.cpp's.
    s->stick_x = (F32)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    s->stick_y = -(F32)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    s->substick_x = (F32)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    s->substick_y = -(F32)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;

    // Axis rows bound on the pad (move_up = dpup, say) push the sticks to the
    // rim, the way the keyboard's do.
    if (axes != 0)
    {
        F32 scale = (axes & (1u << IPAD_AXIS_WALK)) ? 0.5f : 1.0f;
        F32 mx = AxisRow(axes, IPAD_AXIS_MOVE_LEFT, IPAD_AXIS_MOVE_RIGHT);
        F32 my = AxisRow(axes, IPAD_AXIS_MOVE_DOWN, IPAD_AXIS_MOVE_UP);
        if (mx != 0.0f || my != 0.0f)
        {
            s->stick_x = mx * scale;
            s->stick_y = my * scale;
        }
        else if (scale != 1.0f)
        {
            s->stick_x *= scale;
            s->stick_y *= scale;
        }

        F32 cx = AxisRow(axes, IPAD_AXIS_CAMERA_LEFT, IPAD_AXIS_CAMERA_RIGHT);
        F32 cy = AxisRow(axes, IPAD_AXIS_CAMERA_DOWN, IPAD_AXIS_CAMERA_UP);
        if (cx != 0.0f || cy != 0.0f)
        {
            s->substick_x = cx;
            s->substick_y = cy;
        }
    }

    s->connected = true;
    s->sources = IPAD_HOST_SOURCE_PAD;
}

// ---------------------------------------------------------------------------

static void ClearState(iPadHostState* s)
{
    s->connected = false;
    s->buttons = 0;
    s->stick_x = 0.0f;
    s->stick_y = 0.0f;
    s->substick_x = 0.0f;
    s->substick_y = 0.0f;
    s->trigger_l = 0.0f;
    s->trigger_r = 0.0f;
    s->camera_x = 0.0f;
    s->camera_y = 0.0f;
    s->sources = 0;
}

// Whether another copy of the game is the one being played. The controller
// reaches this process's window when it is not in front (the hint in
// iPadHostInit, so a pad keeps working across alt-tab); with a second copy of
// the game in front, both copies would take every press. One copy per save
// folder is enforced at startup (iSystem.cpp), so this is the rarer case of
// copies on different folders. Asked of the system at most four times a
// second, and only while none of this process's windows has the keyboard.
static bool OtherCopyInFront()
{
    if (SDL_GetKeyboardFocus() != NULL)
    {
        return false;
    }

    static Uint64 sNextAsk;
    static bool sOther;
    const Uint64 now = SDL_GetTicks();
    if (now >= sNextAsk)
    {
        const bool other = iHostOtherCopyInFront();
        if (other != sOther)
        {
            printf("tssm: input: %s\n", other ? "another copy of the game is in front; the "
                                                "controllers are left to it" :
                                                "the controllers are this copy's again");
            fflush(stdout);
        }
        sOther = other;
        sNextAsk = now + 250;
    }
    return sOther;
}

// A controller that is there but left alone: connected (so the game does not
// stop on its "controller removed" screen), with nothing pressed or moved.
static void IdleState(iPadHostState* s)
{
    const bool connected = s->connected;
    const U32 sources = s->sources;
    ClearState(s);
    s->connected = connected;
    s->sources = sources;
}

static void ChooseController()
{
    const char* v = iConfigGetString("input.controller", "auto");

    if (v == NULL || v[0] == '\0' || iHostStrCaseCmp(v, "auto") == 0)
    {
        sPinnedSlot = -1;
        return;
    }

    S32 n = atoi(v);
    if (n >= 1 && n <= IPAD_MAX_CONTROLLERS)
    {
        sPinnedSlot = n - 1;
        return;
    }

    printf("tssm: input.controller is '%s', which is neither auto nor 1 to %d; using auto\n", v,
           (int)IPAD_MAX_CONTROLLERS);
    fflush(stdout);
    sPinnedSlot = -1;
}

static void OpenGamepad(SDL_JoystickID id)
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sInstance[i] == id)
        {
            return;
        }
    }

    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sGamepad[i] != NULL)
        {
            continue;
        }

        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (pad == NULL)
        {
            // Almost always another process holding it open, which on Windows
            // is exclusive.
            const char* name = SDL_GetGamepadNameForID(id);
            printf("tssm: %s is plugged in but could not be opened (%s). Another program "
                   "probably has it.\n",
                   name != NULL ? name : "a controller", SDL_GetError());
            fflush(stdout);
            return;
        }

        sGamepad[i] = pad;
        sInstance[i] = id;

        const char* name = SDL_GetGamepadName(pad);
        printf("tssm: controller %d connected: %s\n", (int)(i + 1),
               name != NULL ? name : "unnamed");
        fflush(stdout);
        return;
    }

    // More controllers than the game has ports. Nothing is wrong.
}

// Everything plugged in that is not already in a slot. Wanted when a slot frees
// up: a controller passed over for want of a slot gets no second event.
static void OpenWaitingGamepads()
{
    S32 count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == NULL)
    {
        return;
    }

    for (S32 i = 0; i < count; i++)
    {
        OpenGamepad(ids[i]);
    }
    SDL_free(ids);
}

static void CloseGamepad(SDL_JoystickID id)
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sInstance[i] != id)
        {
            continue;
        }

        if (sGamepad[i] != NULL)
        {
            SDL_CloseGamepad(sGamepad[i]);
        }

        printf("tssm: controller %d disconnected\n", (int)(i + 1));
        fflush(stdout);

        sGamepad[i] = NULL;
        sInstance[i] = 0;
        ClearState(&sSlot[i]);

        OpenWaitingGamepads();
        return;
    }
}

// gamecontrollerdb.txt, if someone has put one beside the executable: where a
// controller SDL does not know gets a layout. Optional, and silent when absent.
static void LoadMappings()
{
    char dir[512];
    if (!iHostExeDir(dir, sizeof(dir)))
    {
        return;
    }

    char path[600];
    snprintf(path, sizeof(path), "%s/gamecontrollerdb.txt", dir);

    S32 added = SDL_AddGamepadMappingsFromFile(path);
    if (added > 0)
    {
        printf("tssm: %d controller layouts from gamecontrollerdb.txt\n", (int)added);
        fflush(stdout);
    }
    else if (added < 0 && iHostPathExists(path))
    {
        printf("tssm: gamecontrollerdb.txt could not be read (%s); no extra layouts\n",
               SDL_GetError());
        fflush(stdout);
    }
}

// A device SDL can see but has no layout for. Silence reads as "the port cannot
// see my controller" when what happened is "SDL does not know this one".
static void ReportIfUnmapped(SDL_JoystickID id)
{
    if (SDL_IsGamepad(id))
    {
        return;
    }

    const char* name = SDL_GetJoystickNameForID(id);
    printf("tssm: %s (%04x:%04x) is plugged in, but SDL has no button layout for it, so it "
           "cannot be played on. A line for it in gamecontrollerdb.txt beside the exe would "
           "give it one.\n",
           name != NULL ? name : "an unnamed device", (unsigned)SDL_GetJoystickVendorForID(id),
           (unsigned)SDL_GetJoystickProductForID(id));
    fflush(stdout);
}

void iPadHostInit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        ClearState(&sState[i]);
        ClearState(&sSlot[i]);
        sPortSlot[i] = -1;
        sGamepad[i] = NULL;
        sInstance[i] = 0;
    }

    sKeyboardOnly = false;

    iPadSettingsLoad();
    ChooseController();
    LoadPadBindings();
    iPadKeyboardInit();
    iPadTouchInit();

    // Once per process: the launcher starts the input layer before the game
    // does (iPadHostPollDevices), and SDL's event watches outlive both.
    static bool sWatching;
    if (!sWatching)
    {
        sWatching = true;
#ifdef __ANDROID__
        SDL_AddEventWatch(WatchBackButton, NULL);
#endif
        SDL_AddEventWatch(WatchBackground, NULL);
    }

    // A controller that went dead on alt-tab would be a change in behaviour,
    // not a fix: the keyboard already stops when the window loses focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        printf("tssm: SDL could not start its gamepad subsystem (%s); no controllers will be "
               "seen and the keyboard covers port 0\n",
               SDL_GetError());
        fflush(stdout);
        return;
    }

    sReady = true;

    // After the init, which has already enumerated and queued its events, so a
    // device that only the file knows about is NOT among them. The sweep below
    // picks that one up.
    LoadMappings();
    OpenWaitingGamepads();

    // The layout's "auto" depends on the pad on port 0.
    iPadLayoutRefresh();
}

void iPadHostExit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sGamepad[i] != NULL)
        {
            // Leave no motor running. The device outlives the process.
            SDL_RumbleGamepad(sGamepad[i], 0, 0, 0);
            SDL_CloseGamepad(sGamepad[i]);
            sGamepad[i] = NULL;
            sInstance[i] = 0;
        }
    }

    if (sReady)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        sReady = false;
    }

    // What the mouse did meanwhile (over the launcher, which brings the input
    // layer up and down before the game does) is not the game's to read: not
    // its movement, and not the click on Play either.
    iPadKeyboardForgetMouse();
}

// Which slot each game port reads.
static void MapPortsToSlots()
{
    S32 previous = sPortSlot[0];

    for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
    {
        sPortSlot[p] = -1;
    }

    if (sPinnedSlot >= 0)
    {
        sPortSlot[0] = sPinnedSlot;

        S32 port = 1;
        for (S32 slot = 0; slot < IPAD_MAX_CONTROLLERS; slot++)
        {
            if (slot != sPinnedSlot)
            {
                sPortSlot[port++] = slot;
            }
        }
    }
    else
    {
        S32 port = 0;
        for (S32 slot = 0; slot < IPAD_MAX_CONTROLLERS; slot++)
        {
            if (sSlot[slot].connected)
            {
                sPortSlot[port++] = slot;
            }
        }
    }

    if (sPortSlot[0] != previous)
    {
        if (sPortSlot[0] >= 0)
        {
            const char* kind = iPadHostPadKind();
            printf("tssm: playing on controller %d (%s preset)\n", (int)(sPortSlot[0] + 1),
                   kind != NULL ? kind : "xbox");
            fflush(stdout);
        }

        // Port 0 changed hands, so re-read the bindings: under input.preset =
        // auto the defaults come from what KIND of pad this is, and that is
        // what just changed. Anything [pad] spells out still wins.
        LoadPadBindings();
        iPadLayoutRefresh();
    }
}

static iPadHostHotkeyFn sHotkey;

void iPadHostScript(S32 on, U32 buttons)
{
    sScripted = on != 0;
    sScriptButtons = buttons;
}

void iPadHostReloadBindings()
{
    iPadSettingsLoad();
    iPadKeyboardInit();
    LoadPadBindings();
    iPadLayoutRefresh();
}

S32 iPadHostCaptureKey(S32 prime, char* out, S32 size)
{
    return iPadKeyboardCapture(prime, out, size);
}

S32 iPadHostCaptureButton(S32 prime, char* out, S32 size)
{
    static U32 sWasHeld;

    S32 slot = sPortSlot[0];
    SDL_Gamepad* pad = (slot >= 0) ? sGamepad[slot] : NULL;
    U32 held = 0;
    if (pad != NULL)
    {
        SDL_UpdateGamepads();
        held = ReadInputs(pad, NULL, NULL);
    }

    const U32 pressed = prime ? 0 : (held & ~sWasHeld);
    sWasHeld = held;

    for (S32 input = 0; input < PADIN_COUNT; input++)
    {
        if (pressed & (1u << input))
        {
            const char* name = PadTokenName((S16)input);
            if (name != NULL)
            {
                snprintf(out, (size_t)size, "%s", name);
                return TRUE;
            }
        }
    }
    return FALSE;
}

void iPadHostSetHotkey(iPadHostHotkeyFn fn)
{
    sHotkey = fn;
}

void iPadHostPollDevices()
{
    if (!sReady)
    {
        return;
    }

    SDL_PumpEvents();

    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION,
                          SDL_EVENT_FINGER_DOWN - 1) > 0)
    {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED)
        {
            OpenGamepad(e.gdevice.which);
        }
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            CloseGamepad(e.gdevice.which);
        }
        else if (e.type == SDL_EVENT_JOYSTICK_ADDED)
        {
            ReportIfUnmapped(e.jdevice.which);
        }
    }

    // Which controller is controller 1 follows what is connected.
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sGamepad[i] == NULL)
        {
            ClearState(&sSlot[i]);
        }
        else
        {
            sSlot[i].connected = true;
        }
    }
    MapPortsToSlots();
}

void iPadHostPoll()
{
    if (sReady)
    {
        // Pumping the queue refreshes the gamepad state and reports a
        // controller arriving or leaving, so hot-plugging needs no timer.
        //
        // Only the joystick and gamepad range is taken: SDL has one queue per
        // process and iWindowSDL.cpp reads the rest of it.
        iPerfPumpBegin();
        SDL_PumpEvents();
        iPerfPumpEnd();

        SDL_Event e;
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION,
                              SDL_EVENT_FINGER_DOWN - 1) > 0)
        {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED)
            {
                OpenGamepad(e.gdevice.which);
            }
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
            {
                CloseGamepad(e.gdevice.which);
            }
            else if (e.type == SDL_EVENT_JOYSTICK_ADDED)
            {
                ReportIfUnmapped(e.jdevice.which);
            }
        }

        iPerfInputRead();
        const bool leftAlone = OtherCopyInFront();
        for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
        {
            if (sGamepad[i] == NULL)
            {
                ClearState(&sSlot[i]);
                continue;
            }

            ReadPad(sGamepad[i], &sSlot[i]);
            if (leftAlone)
            {
                IdleState(&sSlot[i]);
            }
        }
    }

    MapPortsToSlots();

    for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
    {
        if (sPortSlot[p] >= 0)
        {
            sState[p] = sSlot[sPortSlot[p]];
        }
        else
        {
            ClearState(&sState[p]);
        }
    }

    // The keyboard and mouse on port 0.
    const bool padOnPort0 = sState[0].connected;
    const bool padInUse =
        padOnPort0 &&
        (sState[0].buttons != 0 ||
         sState[0].stick_x * sState[0].stick_x + sState[0].stick_y * sState[0].stick_y > 0.09f ||
         sState[0].substick_x * sState[0].substick_x + sState[0].substick_y * sState[0].substick_y >
             0.09f);
    const iPadKeyboardMode mode = iPadSettingsGet().keyboard;
    bool mouseRead = false;
    if (mode == IPAD_KEYBOARD_ALWAYS || (mode == IPAD_KEYBOARD_FALLBACK && !padOnPort0))
    {
        iPadKeyboardPoll(&sState[0]);
        mouseRead = true;
    }
    else
    {
        // Not left to pile up and turn the camera in one frame when the
        // controller goes.
        iPadKeyboardForgetMouse();
    }

    // The mouse's movement: the camera's, unless a controller has it.
    iMouseLookUpdate(&sState[0], mouseRead, padInUse, sScripted);

    // With no pad, port 0 is the keyboard's whether or not a key is down, so
    // the game does not stop on its "no controller" screen.
    sKeyboardOnly = !padOnPort0;
    if (sKeyboardOnly && mode != IPAD_KEYBOARD_OFF)
    {
        sState[0].connected = true;
    }

    // The on-screen controls add to whatever holds port 0, and stand aside
    // while a controller there is in use.
    iPadTouchPoll(&sState[0], padOnPort0);

#ifdef __ANDROID__
    // Held for one poll and released for one, so the game sees a press edge for
    // each tap however quickly the taps come.
    if (sBackFrames == 0 && SDL_GetAtomicInt(&sBackPresses) > 0)
    {
        SDL_AddAtomicInt(&sBackPresses, -1);
        sBackFrames = 2;
    }
    if (sBackFrames == 2)
    {
        sState[0].connected = true;
        sState[0].buttons |= XPAD_BUTTON_START;
    }
    if (sBackFrames > 0)
    {
        sBackFrames--;
    }
#endif

    // A script plays port 0 alone: every real device is ignored.
    if (sScripted)
    {
        // Only a test's own mouse turns the camera then (iMouseLook.h), and
        // what it gave the right stick is kept.
        const F32 cameraX = sState[0].camera_x;
        const F32 cameraY = sState[0].camera_y;
        for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
        {
            ClearState(&sState[p]);
        }
        sState[0].connected = true;
        sState[0].buttons = sScriptButtons;
        sState[0].camera_x = cameraX;
        sState[0].camera_y = cameraY;
        return;
    }

    if (sHotkey != NULL)
    {
        const bool* keys = SDL_GetKeyboardState(NULL);
        sHotkey(SDL_GetKeyboardFocus() != NULL && keys != NULL && keys[SDL_SCANCODE_F8]);
    }
}

const iPadHostState* iPadHostGet(S32 port)
{
    if (port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return NULL;
    }

    return &sState[port];
}

void iPadHostRumble(S32 port, S32 on)
{
    if (!sReady || port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return;
    }

    S32 slot = sPortSlot[port];
    if (slot < 0 || sGamepad[slot] == NULL)
    {
        return;
    }

    // Both motors together: the GameCube's motor has one setting and
    // PADControlMotor is all retail calls. Duration 0 means "until told
    // otherwise", which is what the game means, since it is the one that turns
    // rumble off. input.rumble scales it, and 0 turns it off.
    Uint16 strength = 0;
    if (on)
    {
        strength = (Uint16)(65535.0f * iPadSettingsGet().rumble);
    }
    SDL_RumbleGamepad(sGamepad[slot], strength, strength, 0);
}

const char* iPadHostName()
{
    return sReady ? "sdl (every controller SDL knows, keyboard and mouse)"
                  : "sdl (keyboard only -- SDL's gamepad subsystem did not start)";
}

const char* iPadHostDeviceName(S32 port)
{
    if (port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return NULL;
    }

    S32 slot = sPortSlot[port];
    if (slot >= 0 && sGamepad[slot] != NULL)
    {
        const char* name = SDL_GetGamepadName(sGamepad[slot]);
        return name != NULL ? name : "controller";
    }

    if (port == 0 && sKeyboardOnly)
    {
        return "keyboard";
    }
    return NULL;
}

const char* iPadHostBoundInput(U32 xpadButton)
{
    for (S32 i = 0; i < kPadBindButtonCount && i < IPAD_BIND_MAX_BUTTONS; i++)
    {
        if (kPadBindButtons[i].mask != xpadButton)
        {
            continue;
        }

        S16 id = iPadBindSoleInput(sPadBind[i]);
        return (id >= 0) ? PadTokenName(id) : NULL;
    }

    return NULL;
}

const char* iPadHostPadKind()
{
    S32 slot = sPortSlot[0];
    if (slot < 0 || sGamepad[slot] == NULL)
    {
        return NULL;
    }

    switch (SDL_GetGamepadType(sGamepad[slot]))
    {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return "playstation";
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return "xbox";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return "switch";
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return "gamecube";
    default:
        return NULL;
    }
}

const char* iPadHostInputForLabel(char letter)
{
    S32 slot = sPortSlot[0];
    if (slot < 0 || sGamepad[slot] == NULL)
    {
        return NULL;
    }

    // The four face positions and the token each one is called by. Walking
    // positions and asking SDL what is PRINTED on each is the only way round:
    // there is no call that goes the other way.
    struct Face
    {
        SDL_GamepadButton button;
        const char* token;
    };

    static const Face kFaces[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, "a" },
        { SDL_GAMEPAD_BUTTON_EAST, "b" },
        { SDL_GAMEPAD_BUTTON_WEST, "x" },
        { SDL_GAMEPAD_BUTTON_NORTH, "y" },
    };

    for (S32 i = 0; i < (S32)(sizeof(kFaces) / sizeof(kFaces[0])); i++)
    {
        SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabel(sGamepad[slot], kFaces[i].button);
        char printed = '\0';

        switch (label)
        {
        case SDL_GAMEPAD_BUTTON_LABEL_A:
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            printed = 'A';
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_B:
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
            printed = 'B';
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_X:
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
            printed = 'X';
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_Y:
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
            printed = 'Y';
            break;
        default:
            break;
        }

        if (printed == letter)
        {
            return kFaces[i].token;
        }
    }

    return NULL;
}
