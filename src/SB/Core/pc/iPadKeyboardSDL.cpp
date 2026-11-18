// The keyboard and mouse playing port 0. What they do and why is in
// iPadKeyboard.h; the default keys are the [keyboard] column of
// iPadBindTable.cpp and docs/CONTROLS.md lists them.
//
// TSSM-ADAPTED from treedome's keyboard backend.
//
// **Keys are SCANCODES, not characters.** SDL names a scancode after the key in
// that position on a US layout, so "w" here means the key above "s" whatever is
// printed on it. That is what a game wants: WASD is a shape under the left
// hand, and on an AZERTY keyboard it stays under ZQSD without rebinding.

#include "iPadKeyboard.h"

#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadSettings.h"
#include "iPadTokens.h"
#include "xPad.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static iPadBind sKeyBind[IPAD_BIND_MAX_BUTTONS];
static iPadKeyboardTestSource* sTest;

// The mouse's state for the poll in progress: buttons held, and the wheel as
// one poll of "held" per notch so a binding sees a press and a release.
static SDL_MouseButtonFlags sMouseButtons;
static S32 sWheelUpPolls;
static S32 sWheelDownPolls;
static F32 sWheelAccum;
static bool sRelative;

// **Real mice only.** SDL also makes a mouse of the first finger on a touch
// screen (SDL_TOUCH_MOUSEID; on by default) and of a pen (SDL_PEN_MOUSEID),
// and SDL_GetMouseState and SDL_GetRelativeMouseState count them with the
// rest. Read that way, a thumb on the touch controls' left stick turned the
// camera, held the left button (which attacks) and made the prompts show keys.
// So the mouse is read from its events, which say which mouse each came from:
// a watch adds up the motion, buttons and wheel of every other mouse, on
// whichever thread posts them (Android's UI thread posts touch), hence the
// atomics. The stand-in mice are left to the touch controls and the overlay.
//
// Motion in thousandths of a pixel, wheel in thousandths of a notch.
static SDL_AtomicInt sMotionX;
static SDL_AtomicInt sMotionY;
static SDL_AtomicInt sButtonsHeld;
static SDL_AtomicInt sWheelNotches;
static bool sMouseWatch;

static bool RealMouse(SDL_MouseID which)
{
    return which != SDL_TOUCH_MOUSEID && which != SDL_PEN_MOUSEID;
}

static bool SDLCALL WatchMouse(void*, SDL_Event* e)
{
    switch (e->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        if (RealMouse(e->motion.which))
        {
            SDL_AddAtomicInt(&sMotionX, (int)(e->motion.xrel * 1000.0f));
            SDL_AddAtomicInt(&sMotionY, (int)(e->motion.yrel * 1000.0f));
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (RealMouse(e->button.which) && e->button.button >= 1 && e->button.button <= 31)
        {
            const int bit = (int)SDL_BUTTON_MASK(e->button.button);
            int was;
            do
            {
                was = SDL_GetAtomicInt(&sButtonsHeld);
            } while (!SDL_CompareAndSwapAtomicInt(&sButtonsHeld, was,
                                                  e->button.down ? (was | bit) : (was & ~bit)));
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (RealMouse(e->wheel.which))
        {
            F32 y = e->wheel.y;
            if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            {
                y = -y;
            }
            SDL_AddAtomicInt(&sWheelNotches, (int)(y * 1000.0f));
        }
        break;

    default:
        break;
    }
    return true;
}

void iPadKeyboardTakeMouse(F32* dx, F32* dy, U32* buttons)
{
    *dx = (F32)SDL_SetAtomicInt(&sMotionX, 0) / 1000.0f;
    *dy = (F32)SDL_SetAtomicInt(&sMotionY, 0) / 1000.0f;
    *buttons = (U32)SDL_GetAtomicInt(&sButtonsHeld);
}

static bool ScancodeDown(S32 code)
{
    const bool* state = SDL_GetKeyboardState(NULL);

    if (state == NULL || code < 0 || code >= SDL_SCANCODE_COUNT)
    {
        return false;
    }

    return state[code];
}

// An Enter pressed while Alt is held is the windowed toggle (iWindowSDL.cpp),
// not a game button. It stays swallowed until released, so letting go of Alt
// first does not then press Start.
static bool sEnterSwallowed[2];

static bool EnterDown(S32 code, bool* swallowed)
{
    bool down = ScancodeDown(code);
    if (!down)
    {
        *swallowed = false;
    }
    else if (ScancodeDown(SDL_SCANCODE_LALT) || ScancodeDown(SDL_SCANCODE_RALT))
    {
        *swallowed = true;
    }
    return down && !*swallowed;
}

static bool RealDown(S32 id)
{
    switch (id)
    {
    case SDL_SCANCODE_RETURN:
        return EnterDown(id, &sEnterSwallowed[0]);
    case SDL_SCANCODE_KP_ENTER:
        return EnterDown(id, &sEnterSwallowed[1]);
    case KEY_EITHER_SHIFT:
        return ScancodeDown(SDL_SCANCODE_LSHIFT) || ScancodeDown(SDL_SCANCODE_RSHIFT);
    case KEY_EITHER_CTRL:
        return ScancodeDown(SDL_SCANCODE_LCTRL) || ScancodeDown(SDL_SCANCODE_RCTRL);
    case KEY_EITHER_ALT:
        return ScancodeDown(SDL_SCANCODE_LALT) || ScancodeDown(SDL_SCANCODE_RALT);
    case KEY_MOUSE_LEFT:
        return (sMouseButtons & SDL_BUTTON_LMASK) != 0;
    case KEY_MOUSE_RIGHT:
        return (sMouseButtons & SDL_BUTTON_RMASK) != 0;
    case KEY_MOUSE_MIDDLE:
        return (sMouseButtons & SDL_BUTTON_MMASK) != 0;
    case KEY_MOUSE_X1:
        return (sMouseButtons & SDL_BUTTON_X1MASK) != 0;
    case KEY_MOUSE_X2:
        return (sMouseButtons & SDL_BUTTON_X2MASK) != 0;
    case KEY_WHEEL_UP:
        return sWheelUpPolls == 2;
    case KEY_WHEEL_DOWN:
        return sWheelDownPolls == 2;
    default:
        return ScancodeDown(id);
    }
}

static bool KeyDown(S16 id)
{
    if (sTest != NULL)
    {
        return sTest->down != NULL && sTest->down(id);
    }
    return RealDown(id);
}

const iPadBind* iPadKeyboardBinds()
{
    return sKeyBind;
}

static bool sMouseAllowed = true;
static void SetRelative(bool on);

void iPadKeyboardSetMouseCapture(bool allowed)
{
    sMouseAllowed = allowed;
    if (!allowed && sTest == NULL)
    {
        SetRelative(false);
    }
}

void iPadKeyboardSetTestSource(iPadKeyboardTestSource* source)
{
    sTest = source;
}

void iPadKeyboardInit()
{
    if (!sMouseWatch)
    {
        SDL_AddEventWatch(WatchMouse, NULL);
        sMouseWatch = true;
    }

    S32 count;
    const iPadBindToken* tokens = iPadKeyTokens(&count);
    iPadBindLoad(IPAD_BIND_KEYBOARD, tokens, count, sKeyBind);
}

// The window that has the keyboard, which is the one place relative mouse mode
// can be switched on. iWindowSDL.cpp owns it; asking SDL keeps this file from
// needing a way in.
static void SetRelative(bool on)
{
    if (sRelative == on)
    {
        return;
    }

    SDL_Window* w = SDL_GetKeyboardFocus();
    if (w == NULL && on)
    {
        return;
    }

    if (w != NULL)
    {
        SDL_SetWindowRelativeMouseMode(w, on);
    }
    sRelative = on;
}

// Mouse motion in pixels this poll, and the buttons and wheel.
static void ReadMouse(F32* dx, F32* dy)
{
    *dx = 0.0f;
    *dy = 0.0f;

    if (sTest != NULL)
    {
        *dx = sTest->mouse_dx;
        *dy = sTest->mouse_dy;
        sTest->mouse_dx = 0.0f;
        sTest->mouse_dy = 0.0f;
        return;
    }

    // Counted by WatchMouse as the events are posted, so iWindowPump draining
    // the mouse events costs nothing here.
    U32 buttons = 0;
    iPadKeyboardTakeMouse(dx, dy, &buttons);
    sMouseButtons = (SDL_MouseButtonFlags)buttons;

    sWheelAccum += (F32)SDL_SetAtomicInt(&sWheelNotches, 0) / 1000.0f;

    // Held for one poll and released for one, so each notch is one press:
    // 2 is the poll it goes down, 1 the poll it is let go.
    if (sWheelUpPolls > 0)
    {
        sWheelUpPolls--;
    }
    if (sWheelDownPolls > 0)
    {
        sWheelDownPolls--;
    }

    if (sWheelUpPolls == 0 && sWheelAccum >= 1.0f)
    {
        sWheelAccum -= 1.0f;
        sWheelUpPolls = 2;
    }
    if (sWheelDownPolls == 0 && sWheelAccum <= -1.0f)
    {
        sWheelAccum += 1.0f;
        sWheelDownPolls = 2;
    }
}

static F32 Axis(U32 axes, S32 negative, S32 positive)
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

// The mouse in C-stick units. The camera turns by an amount proportional to
// how far the C-stick is past 32 (zCameraUpdate), up to 110; a distance moved
// becomes a deflection past 32 in proportion, so a quick flick and a slow drag
// turn the camera as far as the hand went. Below 32 the game does nothing, so
// the first pixel of movement starts at the deadzone edge.
static F32 MouseToStick(F32 pixels, F32 sensitivity)
{
    const F32 perPixel = 2.0f;

    if (pixels == 0.0f)
    {
        return 0.0f;
    }

    F32 mag = 33.0f + fabsf(pixels) * perPixel * sensitivity;
    if (mag > 110.0f)
    {
        mag = 110.0f;
    }
    return pixels < 0.0f ? -mag : mag;
}

bool iPadKeyboardPoll(iPadHostState* s)
{
    const iPadSettings& cfg = iPadSettingsGet();

    if (sTest == NULL)
    {
        // SDL_GetKeyboardState reads a table the event pump maintains, so it is
        // only as fresh as the last pump: iPadHostPoll's (iPadHostSDL.cpp, the
        // only caller), which has just run.

        // Focus is elsewhere: nothing held, so the character stops rather than
        // running on with a key the game never sees released. Still connected.
        if (SDL_GetKeyboardFocus() == NULL)
        {
            SetRelative(false);
            sMouseButtons = 0;

            // Movement made elsewhere is not the game's to turn by.
            F32 dx;
            F32 dy;
            U32 buttons;
            iPadKeyboardTakeMouse(&dx, &dy, &buttons);
            return false;
        }
    }

    F32 mdx;
    F32 mdy;
    ReadMouse(&mdx, &mdy);

    U32 axes = 0;
    U32 on = iPadBindEvaluate(sKeyBind, KeyDown, &axes);

    bool used = on != 0 || axes != 0 || mdx != 0.0f || mdy != 0.0f || sMouseButtons != 0;

    s->buttons |= on;

    F32 scale = (axes & (1u << IPAD_AXIS_WALK)) ? 0.5f : 1.0f;
    F32 mx = Axis(axes, IPAD_AXIS_MOVE_LEFT, IPAD_AXIS_MOVE_RIGHT) * scale;
    F32 my = Axis(axes, IPAD_AXIS_MOVE_DOWN, IPAD_AXIS_MOVE_UP) * scale;
    if (mx * mx + my * my > s->stick_x * s->stick_x + s->stick_y * s->stick_y)
    {
        s->stick_x = mx;
        s->stick_y = my;
    }

    F32 cx = Axis(axes, IPAD_AXIS_CAMERA_LEFT, IPAD_AXIS_CAMERA_RIGHT);
    F32 cy = Axis(axes, IPAD_AXIS_CAMERA_DOWN, IPAD_AXIS_CAMERA_UP);
    if (cx * cx + cy * cy > s->substick_x * s->substick_x + s->substick_y * s->substick_y)
    {
        s->substick_x = cx;
        s->substick_y = cy;
    }

    if (cfg.mouse_camera && sMouseAllowed)
    {
        if (sTest == NULL)
        {
            SetRelative(true);
        }

        // A mouse moved right looks right, which is the C-stick pushed LEFT in
        // this camera (it orbits); moved down looks down, which is the C-stick
        // pushed UP (the game's "above" pose). Game units: y positive is down.
        F32 sx = -MouseToStick(mdx, cfg.mouse_sensitivity);
        F32 sy = -MouseToStick(mdy, cfg.mouse_sensitivity);
        s->camera_x += cfg.camera_invert_x ? -sx : sx;
        s->camera_y += cfg.camera_invert_y ? -sy : sy;
    }
    else if (sTest == NULL)
    {
        SetRelative(false);
    }

    if (used)
    {
        s->connected = true;
        s->sources |= IPAD_HOST_SOURCE_KEYBOARD;
        if (mdx != 0.0f || mdy != 0.0f || sMouseButtons != 0)
        {
            s->sources |= IPAD_HOST_SOURCE_MOUSE;
        }
    }

    return used;
}

S32 iPadKeyboardCapture(S32 prime, char* out, S32 size)
{
    static bool sWasDown[SDL_SCANCODE_COUNT];
    static SDL_MouseButtonFlags sWasMouse;

    S32 count;
    const iPadBindToken* tokens = iPadKeyTokens(&count);

    // No pump of its own: the overlay asks once a presented frame, which has
    // pumped (iWindowSDL.cpp), and the launcher once a pass of its own event
    // loop.
    S32 found = -1;
    for (S32 code = 0; code < SDL_SCANCODE_COUNT; code++)
    {
        const bool down = (sTest != NULL) ? KeyDown((S16)code) : ScancodeDown(code);
        if (down && !sWasDown[code] && !prime && found < 0 &&
            iPadBindTokenName((S16)code, tokens, count) != NULL)
        {
            found = code;
        }
        sWasDown[code] = down;
    }

    // Mouse buttons, so a settings screen can bind one by clicking it. In
    // SDL's order: left, middle, right, X1, X2.
    static const S32 kMouseIds[5] = { KEY_MOUSE_LEFT, KEY_MOUSE_MIDDLE, KEY_MOUSE_RIGHT,
                                      KEY_MOUSE_X1, KEY_MOUSE_X2 };

    SDL_MouseButtonFlags mouse = 0;
    if (sTest == NULL)
    {
        // Real mice only, as for play: a tap on the overlay is not a click.
        mouse = (SDL_MouseButtonFlags)SDL_GetAtomicInt(&sButtonsHeld);
    }
    else
    {
        for (S32 b = 0; b < 5; b++)
        {
            if (KeyDown((S16)kMouseIds[b]))
            {
                mouse |= SDL_BUTTON_MASK(b + 1);
            }
        }
    }
    for (S32 b = 0; b < 5 && found < 0; b++)
    {
        SDL_MouseButtonFlags bit = SDL_BUTTON_MASK(b + 1);
        if ((mouse & bit) && !(sWasMouse & bit) && !prime)
        {
            found = kMouseIds[b];
        }
    }
    sWasMouse = mouse;

    if (found < 0)
    {
        return FALSE;
    }

    snprintf(out, (size_t)size, "%s", iPadBindTokenName((S16)found, tokens, count));
    return TRUE;
}
