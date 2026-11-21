// The mouse pointed at the camera. See iMouseLook.h.
//
// TSSM-ADAPTED from treedome's iMouseLook.cpp and the capture and pointer
// halves of its iPadMouseSDL.cpp: the device is read by iPadKeyboardSDL.cpp
// here, the mouse yields to a controller in use rather than to a setting, and
// the cameras that read only the right stick get a stick deflection.

#include "iMouseLook.h"

#include "iCameraLook.h"
#include "iHost.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadKeyboard.h"
#include "iPadSettings.h"
#include "iPadTokens.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>

namespace
{
    // Radians of turn per count at sensitivity 1, the same on both axes so a
    // diagonal sweep goes where it is pointed. A 400-count sweep -- about
    // 1.3 cm on an 800 dpi mouse -- turns the camera 80 degrees. (treedome's.)
    const F32 kRadPerCount = 0.0035f;

    // For the cameras that read the right stick: counts to stick units past
    // the game's deadzone of 32, up to its 110.
    const F32 kStickPerCount = 2.0f;

    // The way the right stick is pushed to turn the view right, in the game's
    // units (zCameraUpdate adds a stick pushed right to the camera's orbit
    // angle).
    const F32 kStickLookRight = -1.0f;

    // A right stick (or the camera keys) this far off centre has the camera.
    // With each stick curve's own deadzone a push short of this is still
    // inside the game's deadzone of 32, so none that would have turned the
    // game's camera is lost.
    const F32 kStickTakes = 0.3f;

    // A pointer that has not moved for this long is put away, so it does not
    // sit on a movie or a loading screen.
    const Uint64 kPointerIdleNS = 3000000000ull;

    bool sAllowed = true;
    bool sReleased;
    bool sPointerWanted;
    // The menus have said once whether they want a pointer: nothing is held
    // before their first answer.
    bool sPointerAsked;
    // What they last asked for, whether or not there was a pointer to give.
    bool sMenuAsks;
    // Escape let the pointer go: it stays free until a menu that came up has
    // gone again, or a click in the window takes it back.
    bool sEscaped;
    // Escape went down where nothing was held at the last poll; the next poll
    // says whether it would have been.
    bool sEscapePending;
    // Held at the last poll.
    bool sHold;
    // Mouse buttons (bit 0 left, 1 right, 2 middle) whose press was used for
    // something that is not the game's: out of play until let go.
    U32 sSpent;
    bool sEngaged;
    // The keyboard and mouse are what the player has in hand. Not until one of
    // them is used, so nothing is held at start-up, or ever for a player who
    // only touches a controller.
    bool sKbmLive;
    bool sMouseRead;
    bool sScripted;
    bool sFocus;

    F32 sOwedX;
    F32 sOwedY;
    Uint64 sLastNS;

    SDL_Window* sCaptureWindow;
    bool sKeyWasDown;
    S32 sRawApplied = -1;

    // The pointer.
    bool sPollMoved;
    U32 sPollPressed;
    U32 sPollHeld;
    S32 sPollWheel;
    Uint64 sPointerMovedNS;
    bool sCursorHidden;
    bool sHand;
    S32 sHandShown = -1;
    SDL_Cursor* sCursors[2];

    // The test's mouse.
    bool sTest;
    F32 sTestDX;
    F32 sTestDY;
    F32 sTestPX;
    F32 sTestPY;
    bool sTestMoved;
    bool sTestEscape;
    U32 sTestPressed;
    S32 sTestWheel;

    void ApplyCapture(SDL_Window* want)
    {
        if (want == sCaptureWindow)
        {
            return;
        }

        if (sCaptureWindow != NULL)
        {
            SDL_SetWindowRelativeMouseMode(sCaptureWindow, false);
        }

        sCaptureWindow = want;

        if (sCaptureWindow != NULL)
        {
            SDL_SetWindowRelativeMouseMode(sCaptureWindow, true);
        }
    }

    // The desktop's pointer speed and acceleration on the counts, or not.
    void ApplyRaw(S32 raw)
    {
        if (raw != sRawApplied)
        {
            sRawApplied = raw;
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, raw ? "0" : "1");
        }
    }

    bool ReleaseKeyDown(const char* name)
    {
        if (name == NULL || name[0] == '\0')
        {
            return false;
        }

        S32 count;
        const iPadBindToken* tokens = iPadKeyTokens(&count);
        for (S32 i = 0; i < count; i++)
        {
            if (iHostStrCaseCmp(tokens[i].name, name) == 0)
            {
                const bool* keys = SDL_GetKeyboardState(NULL);
                S32 id = tokens[i].id;
                return keys != NULL && id > 0 && id < SDL_SCANCODE_COUNT && keys[id];
            }
        }
        return false;
    }

    F32 StickUnits(F32 counts, F32 sensitivity)
    {
        if (counts == 0.0f)
        {
            return 0.0f;
        }

        F32 mag = 33.0f + fabsf(counts) * kStickPerCount * sensitivity;
        if (mag > 110.0f)
        {
            mag = 110.0f;
        }
        return counts < 0.0f ? -mag : mag;
    }

    void Drop()
    {
        sOwedX = 0.0f;
        sOwedY = 0.0f;
        iCameraLookSet(iCAMERALOOK_MOUSE, 0.0f, 0.0f);
    }

    U32 ThreeButtons(U32 sdlMask)
    {
        return ((sdlMask & SDL_BUTTON_LMASK) ? 1u : 0u) | ((sdlMask & SDL_BUTTON_RMASK) ? 2u : 0u) |
               ((sdlMask & SDL_BUTTON_MMASK) ? 4u : 0u);
    }

    // The pointer put away while it idles over the game, and shown when it
    // moves, or always once it was freed by hand. Relative mode hides it by
    // itself. Over something a click does something to, it is the system's
    // hand.
    void ApplyCursor(Uint64 now)
    {
        S32 hand = (sPointerWanted && sHand) ? 1 : 0;
        if (hand != sHandShown)
        {
            if (sCursors[hand] == NULL)
            {
                sCursors[hand] = SDL_CreateSystemCursor(hand ? SDL_SYSTEM_CURSOR_POINTER :
                                                               SDL_SYSTEM_CURSOR_DEFAULT);
            }
            if (sCursors[hand] != NULL)
            {
                SDL_SetCursor(sCursors[hand]);
            }
            sHandShown = hand;
        }

        bool hide = sFocus && sAllowed && !sReleased && !sEscaped &&
                    now - sPointerMovedNS > kPointerIdleNS;
        if (hide != sCursorHidden)
        {
            sCursorHidden = hide;
            if (hide)
            {
                SDL_HideCursor();
            }
            else
            {
                SDL_ShowCursor();
            }
        }
    }
} // namespace

void iMouseLookAllow(bool allowed)
{
    sAllowed = allowed;
    if (!allowed)
    {
        ApplyCapture(NULL);
    }
}

bool iMouseLookSteering()
{
    return sEngaged;
}

void iMouseLookUpdate(iPadHostState* s, bool mouseRead, bool padInUse, bool scripted)
{
    const iPadSettings& cfg = iPadSettingsGet();

    Uint64 now = SDL_GetTicksNS();
    F32 dt = (sLastNS != 0) ? (F32)(now - sLastNS) / 1e9f : 0.0f;
    sLastNS = now;
    if (dt > 0.1f)
    {
        dt = 0.1f;
    }

    iCameraLookPoll();

    F32 dx = 0.0f;
    F32 dy = 0.0f;
    U32 held = 0;
    U32 pressed = 0;
    S32 wheel = 0;

    if (sTest)
    {
        dx = sTestDX;
        dy = sTestDY;
        sTestDX = 0.0f;
        sTestDY = 0.0f;
        sPollMoved = sTestMoved || dx != 0.0f || dy != 0.0f;
        sPollPressed = sTestPressed;
        sPollHeld = sTestPressed;
        sPollWheel = sTestWheel;
        sTestMoved = false;
        sTestPressed = 0;
        sTestWheel = 0;
        sFocus = true;
    }
    else
    {
        if (mouseRead && !scripted)
        {
            iPadKeyboardMouse(&dx, &dy, &held, &pressed, &wheel);
        }
        sPollMoved = dx != 0.0f || dy != 0.0f;
        sPollPressed = ThreeButtons(pressed);
        sPollHeld = ThreeButtons(held);
        sPollWheel = wheel;
        sFocus = iPadKeyboardIsTest() || SDL_GetKeyboardFocus() != NULL;
    }
    if (sPollMoved || sPollPressed != 0 || sPollWheel != 0)
    {
        sPointerMovedNS = now;
    }
    sSpent &= sPollHeld | sPollPressed;

    sMouseRead = mouseRead;
    sScripted = scripted;

    // Which of the two the player has in hand, for the menu pointer.
    if (padInUse)
    {
        sKbmLive = false;
    }
    if (sTest || (s->sources & (IPAD_HOST_SOURCE_KEYBOARD | IPAD_HOST_SOURCE_MOUSE)) != 0)
    {
        sKbmLive = true;
    }

    // A menu that was refused a pointer is asked for again here, so the mouse
    // first moved in a menu is not held for the frame before the menu's turn.
    if (sMenuAsks && !sPointerWanted)
    {
        iMouseLookSetPointer(true);
    }

    // The real pointer is this file's only while the real mouse is.
    const bool real = !sTest && !scripted && !iPadKeyboardIsTest();
    const bool on = cfg.mouse_look && (sTest || (mouseRead && !scripted));

    if (real && sFocus)
    {
        bool down = ReleaseKeyDown(cfg.mouse_release_key);
        if (down && !sKeyWasDown)
        {
            // The key takes back a pointer Escape freed.
            if (sEscaped)
            {
                sEscaped = false;
            }
            else
            {
                sReleased = !sReleased;
            }
        }
        sKeyWasDown = down;
    }

    // Held only for the player who has the keyboard and mouse in hand: with a
    // controller the pointer stays free (and is put away while it idles), so
    // the window's title bar is always in reach. Free in any menu, whether or
    // not the menu was given a pointer to click with (mouse_menus).
    const bool would =
        on && sFocus && sAllowed && !sReleased && sPointerAsked && !sMenuAsks && sKbmLive;

    // Escape frees it whatever is on screen. Where it is free already (a
    // menu, the overlay) Escape changes nothing.
    bool escape = sEscapePending || sTestEscape;
    sEscapePending = false;
    sTestEscape = false;
    if (!sTest && iPadKeyboardTakeEscape())
    {
        escape = true;
    }
    if (escape && would)
    {
        sEscaped = true;
    }
    else if (sEscaped && would && sPollPressed != 0)
    {
        // A click in the window takes it back, and is not the game's.
        sEscaped = false;
        sSpent |= sPollPressed;
    }

    const bool hold = would && !sEscaped;
    sHold = hold;
    if (real)
    {
        ApplyRaw(cfg.mouse_raw);
        ApplyCapture(hold ? SDL_GetKeyboardFocus() : NULL);
        ApplyCursor(now);
    }
    if (real || sTest)
    {
        // Said when it changes, so a log shows who had the pointer.
        static const char* sSaid;
        const char* why = hold           ? "held for mouse look" :
                          !on            ? "free (mouse look is off)" :
                          !sFocus        ? "free (the window does not have the keyboard)" :
                          !sAllowed      ? "free (the settings overlay is open)" :
                          sReleased      ? "free (the release key)" :
                          !sPointerAsked ? sSaid :
                          !sKbmLive      ? "free (the keyboard and mouse are not in use)" :
                          sPointerWanted ? "free (a menu's pointer)" :
                          sMenuAsks      ? "free (a menu)" :
                                           "free (Escape)";
        if (why != sSaid)
        {
            sSaid = why;
            printf("tssm: mouse: %s\n", why);
            fflush(stdout);
        }
    }

    if (!on)
    {
        sEngaged = false;
        iCameraLookSetActive(iCAMERALOOK_MOUSE, FALSE);
        Drop();
        return;
    }

    // Who has the camera: a controller in use or a right stick off centre (the
    // camera keys push it too) takes it, with the game's own camera; the mouse
    // takes it back when it next moves.
    const bool stick =
        s->substick_x * s->substick_x + s->substick_y * s->substick_y > kStickTakes * kStickTakes;
    if (padInUse || stick)
    {
        sEngaged = false;
    }
    else if (hold && (dx != 0.0f || dy != 0.0f))
    {
        sEngaged = true;
    }
    iCameraLookSetActive(iCAMERALOOK_MOUSE, sEngaged);

    if (!hold || !sEngaged)
    {
        // Taken and dropped rather than left, so what piles up while the
        // pointer is free does not spin the camera when it is held again.
        Drop();
        return;
    }

    if (cfg.mouse_invert_x)
    {
        dx = -dx;
    }
    if (cfg.mouse_invert_y)
    {
        dy = -dy;
    }

    // Smoothing spreads each count over the next few frames. 0 is none: every
    // count turns the camera in the frame it arrived.
    sOwedX += dx;
    sOwedY += dy;

    F32 tau = cfg.mouse_smoothing;
    F32 share = (tau > 0.0f) ? 1.0f - expf(-dt / tau) : 1.0f;
    F32 turnX = sOwedX * share;
    F32 turnY = sOwedY * share;
    sOwedX -= turnX;
    sOwedY -= turnY;

    if (iCameraLookTaken())
    {
        // Mouse right turns the view right; mouse forward (up the desk, a
        // negative count) looks up, and the engine's pitch is positive-down.
        iCameraLookSet(iCAMERALOOK_MOUSE, turnX * kRadPerCount * cfg.mouse_sensitivity_x,
                       turnY * kRadPerCount * cfg.mouse_sensitivity_y);
    }
    else
    {
        // A camera that reads only the right stick has the view. In the
        // game's units the stick pushed left (negative) turns the view right,
        // and pushed up (negative) raises the camera, which looks down.
        iCameraLookSet(iCAMERALOOK_MOUSE, 0.0f, 0.0f);
        s->camera_x += kStickLookRight * StickUnits(turnX, cfg.mouse_sensitivity_x);
        s->camera_y -= StickUnits(turnY, cfg.mouse_sensitivity_y);
    }
}

bool iMouseLookSetPointer(bool wanted)
{
    const iPadSettings& cfg = iPadSettingsGet();

    sPointerAsked = true;
    if (sMenuAsks && !wanted)
    {
        // Play goes on after a menu: the pointer Escape freed is taken back.
        sEscaped = false;
    }
    sMenuAsks = wanted;

    bool can = wanted && cfg.mouse_menus && sAllowed && sFocus && sKbmLive &&
               (sTest || (sMouseRead && !sScripted));
    if (can != sPointerWanted)
    {
        sPointerWanted = can;
        sPointerMovedNS = SDL_GetTicksNS();
        if (can)
        {
            // Let go first: relative mode hides the pointer and pins it.
            ApplyCapture(NULL);
        }
    }

    if (!can)
    {
        sHand = false;
    }
    return can;
}

void iMouseLookSetHand(bool hand)
{
    sHand = hand;
}

U32 iMouseLookBound(S32 button)
{
    S32 id = button == 2 ? KEY_MOUSE_RIGHT : (button == 3 ? KEY_MOUSE_MIDDLE : KEY_MOUSE_LEFT);
    return iPadKeyboardBitsOf(id) & ~iPadKeyboardHeldWithout(id);
}

void iMouseLookSpend(U32 buttons)
{
    sSpent |= buttons & (sPollHeld | sPollPressed);
}

U32 iMouseLookSpentOut(U32 on)
{
    for (S32 b = 1; b <= 3; b++)
    {
        if (sSpent & (1u << (b - 1)))
        {
            on &= ~iMouseLookBound(b);
        }
    }
    return on;
}

void iMouseLookPump()
{
    if (sTest || !iPadKeyboardTakeEscape())
    {
        return;
    }

    // Between polls (a loading screen, a movie that cannot be skipped): let
    // go now, and the next poll finds it so.
    if (sHold)
    {
        sHold = false;
        sEscaped = true;
        ApplyCapture(NULL);
    }
    else
    {
        sEscapePending = true;
    }
}

bool iMouseLookHeld()
{
    return sHold;
}

bool iMouseLookPointer(F32* x, F32* y, bool* moved, U32* pressed, U32* held, S32* wheel)
{
    if (!sPointerWanted)
    {
        return false;
    }

    *moved = sPollMoved;
    *pressed = sPollPressed;
    *held = sPollHeld;
    *wheel = sPollWheel;

    if (sTest)
    {
        *x = sTestPX;
        *y = sTestPY;
        return true;
    }

    SDL_Window* focus = SDL_GetKeyboardFocus();
    if (focus == NULL)
    {
        return false;
    }

    // SDL reports the pointer in window coordinates, which are points and not
    // pixels on a scaled display.
    F32 mx = 0.0f;
    F32 my = 0.0f;
    SDL_GetMouseState(&mx, &my);

    S32 pw = 0;
    S32 ph = 0;
    S32 lw = 0;
    S32 lh = 0;
    if (SDL_GetWindowSizeInPixels(focus, &pw, &ph) && SDL_GetWindowSize(focus, &lw, &lh) &&
        lw > 0 && lh > 0)
    {
        mx *= (F32)pw / (F32)lw;
        my *= (F32)ph / (F32)lh;
    }

    *x = mx;
    *y = my;
    return true;
}

void iMouseLookTestMove(F32 dx, F32 dy)
{
    sTest = true;
    sTestDX += dx;
    sTestDY += dy;
}

void iMouseLookTestPointer(F32 x, F32 y)
{
    sTest = true;
    sTestPX = x;
    sTestPY = y;
    sTestMoved = true;
}

void iMouseLookTestPress(U32 buttons)
{
    sTest = true;
    sTestPressed |= buttons;
}

void iMouseLookTestWheel(S32 notches)
{
    sTest = true;
    sTestWheel = notches;
}

void iMouseLookTestEscape()
{
    sTest = true;
    sTestEscape = true;
}
