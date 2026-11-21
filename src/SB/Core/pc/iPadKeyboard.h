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
// so on), and the mouse buttons and wheel are keys. What the mouse's movement
// does is iMouseLook.h's (the camera) and iMenuMouse.h's (a menu's pointer).

void iPadKeyboardInit();

// The keyboard's parsed bindings, one per row of kPadBindButtons.
struct iPadBind;
const iPadBind* iPadKeyboardBinds();

// Adds what the keys and the mouse buttons hold right now to `s`: buttons are
// OR'd in, and a stick is taken over when the keys push it further than the
// pad does. Nothing at all when the game does not have focus. TRUE when
// anything on the keyboard or mouse was in use this poll.
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

// The mouse as the last iPadKeyboardPoll read it: the motion in counts (right
// and down positive), the buttons held and the ones that went down since the
// poll before (SDL_BUTTON_MASK bits), and the wheel (1 up, -1 down, 0). All
// zero when the game did not have focus.
void iPadKeyboardMouse(F32* dx, F32* dy, U32* held, U32* pressed, S32* wheel);

// The XPAD_BUTTON_* bits the bindings give one key held by itself (a scancode,
// or KEY_MOUSE_*), for the menu pointer, which takes a click on an item away
// from the button the mouse button is bound to (iMenuMouse.h).
U32 iPadKeyboardBitsOf(S32 id);

// The XPAD_BUTTON_* bits held at the last poll without that key: by the other
// keys, and by whatever had port 0 before the keyboard was read.
U32 iPadKeyboardHeldWithout(S32 id);

// Whether the stand-in of iPadKeyboardSetTestSource is in place.
bool iPadKeyboardIsTest();

// The motion (pixels) of the real mice since the last call, and the buttons
// they hold now (SDL_BUTTON_MASK bits), as the poll reads them. SDL's stand-in
// mice for a touch screen's first finger and for a pen are not counted: a
// finger on the touch controls is not a mouse. Read from the mouse events as
// they are posted, so the input test can post its own.
void iPadKeyboardTakeMouse(F32* dx, F32* dy, U32* buttons);

// What the real mice did since the last poll -- motion, clicks and wheel -- is
// thrown away: it was made somewhere that is not the game (another window,
// the launcher).
void iPadKeyboardForgetMouse();

// Whether the Escape key went down since the last call, read from its event
// as it is posted (so between polls too).
bool iPadKeyboardTakeEscape();

// The first finger on a touch screen, read from the mouse SDL makes of it
// (the one the real mice leave out): where it last was, in its window's
// pixels, and whether it is on the screen now. Returns what it did since the
// last call: bit 0 it went down, bit 1 it lifted, bit 2 it moved. For a
// menu's finger (iMouseLook.h).
U32 iPadKeyboardTouch(F32* x, F32* y, bool* down);

#endif
