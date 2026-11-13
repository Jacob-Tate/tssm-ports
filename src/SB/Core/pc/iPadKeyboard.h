#ifndef IPADKEYBOARD_H
#define IPADKEYBOARD_H

#include <types.h>

struct iPadHostState;

// PC-only: the keyboard and mouse playing port 0.
//
// TSSM-ADAPTED from treedome. The keyboard used to stand in only when no
// controller was on port 0; here input.keyboard decides (always, fallback or
// off), and with `always` -- the default -- keys and a pad play together, the
// way people expect on a PC. The sticks are rows too ([keyboard] move_up and
// so on), the mouse buttons and wheel are keys, and the mouse turns the camera.

void iPadKeyboardInit();

// The keyboard's parsed bindings, one per row of kPadBindButtons.
struct iPadBind;
const iPadBind* iPadKeyboardBinds();

// Adds what the keys and the mouse hold right now to `s`: buttons are OR'd in,
// a stick is taken over when the keys push it further than the pad does, and
// the mouse adds its camera turn. Nothing at all when the game does not have
// focus. TRUE when anything on the keyboard or mouse was in use this poll.
bool iPadKeyboardPoll(iPadHostState* s);

// iPadHostCaptureKey's half; see iPadHost.h. Mouse buttons and the wheel
// answer too.
S32 iPadKeyboardCapture(S32 prime, char* out, S32 size);

// For the headless input test: a stand-in for the keyboard and mouse. `down`
// answers for a scancode, KEY_EITHER_*, KEY_MOUSE_* or KEY_WHEEL_* id; the
// mouse motion is consumed by the next poll. Focus is taken as given. NULL
// goes back to the real devices.
struct iPadKeyboardTestSource
{
    bool (*down)(S32 id);
    F32 mouse_dx;
    F32 mouse_dy;
};
void iPadKeyboardSetTestSource(iPadKeyboardTestSource* source);

// FALSE leaves the mouse alone (no relative mode, no camera) until TRUE again.
void iPadKeyboardSetMouseCapture(bool allowed);

// The motion (pixels) of the real mice since the last call, and the buttons
// they hold now (SDL_BUTTON_MASK bits), as the poll reads them. SDL's stand-in
// mice for a touch screen's first finger and for a pen are not counted: a
// finger on the touch controls is not a mouse. Read from the mouse events as
// they are posted, so the input test can post its own.
void iPadKeyboardTakeMouse(F32* dx, F32* dy, U32* buttons);

#endif
