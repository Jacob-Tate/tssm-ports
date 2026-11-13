#ifndef IPADTOKENS_H
#define IPADTOKENS_H

#include "iPadBind.h"

// PC-only: the device inputs a binding's right-hand side names, for both
// devices. TSSM-ADAPTED from treedome: the extra pad buttons SDL3 reports
// (share/capture, the four paddles, the touchpad, the GameCube trigger clicks)
// and the mouse buttons and wheel on the keyboard side.

// The controller's inputs. The token names are POSITIONS, as SDL3 names its
// face buttons -- `a` is south on every pad, whatever is printed on it.
enum iPadInput
{
    PADIN_A,
    PADIN_B,
    PADIN_X,
    PADIN_Y,
    PADIN_LB,
    PADIN_RB,
    PADIN_LT,
    PADIN_RT,
    PADIN_LS,
    PADIN_RS,
    PADIN_BACK,
    PADIN_START,
    PADIN_DPUP,
    PADIN_DPDOWN,
    PADIN_DPLEFT,
    PADIN_DPRIGHT,
    PADIN_MISC1,
    PADIN_PADDLE1,
    PADIN_PADDLE2,
    PADIN_PADDLE3,
    PADIN_PADDLE4,
    PADIN_TOUCHPAD,
    PADIN_LTCLICK,
    PADIN_RTCLICK,
    PADIN_COUNT
};

// SDL has a scancode for each side of a modifier and none for "either", which
// is what the bare names have always meant. These ids stand for the pair; they
// are above SDL_SCANCODE_COUNT so they can never collide with a real one.
#define KEY_EITHER_SHIFT 1000
#define KEY_EITHER_CTRL 1001
#define KEY_EITHER_ALT 1002

// The mouse, on the keyboard side: it plays alongside the keys, never alone.
#define KEY_MOUSE_LEFT 1010
#define KEY_MOUSE_RIGHT 1011
#define KEY_MOUSE_MIDDLE 1012
#define KEY_MOUSE_X1 1013
#define KEY_MOUSE_X2 1014
#define KEY_WHEEL_UP 1015
#define KEY_WHEEL_DOWN 1016

// Where a trigger clicks by default, as a fraction of its travel. The GameCube
// code tests `triggerLeft >= 0x18` after PADClamp, whose trigger range is 30
// (dead) to 180 (full): 0x18 above the dead 30 is raw 54, and 54 of 180 is 30%
// of the pull. input.trigger_threshold overrides it.
#define IPAD_TRIGGER_THRESHOLD_DEFAULT 0.30f

const iPadBindToken* iPadPadTokens(S32* count);

// Ids are SDL scancodes, KEY_EITHER_* or KEY_MOUSE_* / KEY_WHEEL_*. Letters and
// digits are included.
const iPadBindToken* iPadKeyTokens(S32* count);

// The PADIN_* an SDL_GamepadButton reports as, or -1 for a button no binding
// can name. The triggers are axes, not buttons, and are not here.
S32 iPadInputFromSDLButton(S32 button);

// A short label for a pad input on a given kind of controller ("A", "Cross",
// "ZR"), for the input test and a settings screen. `kind` is a preset name or
// NULL for the Xbox's.
const char* iPadInputLabel(S32 input, const char* kind);

// The console whose printed letters everything on screen shows: "switch" (the
// Switch, or TSSM_PAD_LABELS=switch on a PC), "3ds" (the 3DS, or
// TSSM_PAD_LABELS=3ds), or NULL. Both play the Xbox layout by position and
// print the same face letters there (B bottom, A right, Y left, X top); the
// 3DS names its small buttons START and SELECT and its sticks Circle Pad and
// C-Stick.
const char* iPadPrintedSet();

// TRUE where everything on screen names the Switch's buttons: on the Switch
// (__SWITCH__), and on a PC started with TSSM_PAD_LABELS=switch, which previews
// it for captures (PC only; nothing else on a PC changes). The Switch plays the
// Xbox layout by POSITION (its bottom button, printed B, is the Xbox's A), so a
// label names the letter printed where the Xbox button would be: Xbox A is "B",
// B is "A", X is "Y", Y is "X", the triggers ZL/ZR, white and black L/R, Back
// and Start - and +.
bool iPadSwitchLabels();

// A game button (a kPadBindButtons row: "a", "lt", "white", "start") named by
// the Switch button in its default position ("B", "ZL", "L", "+"), or NULL for
// a row that is not one of the ten (the d-pad, the keyboard's stick rows).
const char* iPadSwitchRowLabel(const char* row);

// The same by iPadPrintedSet: the Switch's labels there, the 3DS's ("B",
// "ZL", "L", "START") on a 3DS, NULL where no console's letters are shown.
const char* iPadPrintedRowLabel(const char* row);

#endif
