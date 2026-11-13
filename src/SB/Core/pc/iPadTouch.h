#ifndef IPADTOUCH_H
#define IPADTOUCH_H

#include <types.h>

struct iPadHostState;

// PC-only: on-screen controls for a touchscreen, standing in for a controller
// on port 0 the way the keyboard does. input.touch_controls turns them on; auto
// means Android.
//
// Two sticks and the buttons. The left stick moves; it centres where the thumb
// lands anywhere on the left half. The right stick is the controller's right
// stick (the camera), at the bottom right, left of A B X Y; it centres where
// the thumb lands near it. Buttons for A B X Y, LT RT, Back and black, and
// Start produce the bits the rows of the same names do (iPadBindTable.cpp).
// With input.touch_drag_look on, a drag on the rest of the right half also
// turns the camera, as the mouse does. Drawn by rw/touch_overlay.cpp; see
// iTouchOverlay.h. TSSM-ADAPTED from treedome.
//
// **Each finger belongs to what it landed on until it lifts.** The fingers
// come from SDL's touch events, in order, so a tap shorter than a frame still
// presses for one poll. A finger that landed on the left stick only ever moves
// the left stick; one that landed on A only ever presses A (while it stays
// near A); one that landed on empty screen does nothing. SDL's stand-in mouse
// for the first finger is left alone here, and iPadKeyboardSDL.cpp ignores it.

void iPadTouchInit();

// Whether the on-screen controls are on (input.touch_controls).
bool iPadTouchEnabled();

// Whether they are on screen: on, and not put away by a controller in use.
bool iPadTouchShowing();

// Reads the fingers on the screen and, while the controls are showing, adds
// what they hold to `s`. `padOnPort0` hides the controls until the screen is
// touched again.
void iPadTouchPoll(iPadHostState* s, bool padOnPort0);

// The letter on the on-screen button that presses the row `row` of
// kPadBindButtons ("a", "lt", "start"), or 0 when no button does. For the
// button prompts.
char iPadTouchButtonLabel(const char* row);

// For the input test. Where a control is now, as fractions of the window (0 to
// 1, y down): "left_stick" and "right_stick" (their resting rings), or the row
// of a button. FALSE for a name with no control, or before a window size is
// known.
bool iPadTouchControlAt(const char* name, F32* x, F32* y);

// For the input test: the safe area's insets in window pixels, in place of
// SDL's. Negative goes back to SDL's.
void iPadTouchTestInsets(F32 left, F32 right, F32 top, F32 bottom);

#endif
