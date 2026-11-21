#ifndef IMOUSELOOK_H
#define IMOUSELOOK_H

#include <types.h>

struct iPadHostState;

// PC-only: the mouse pointed at the camera. Adapted from treedome, the BfBB PC
// port. iPadKeyboard.h reads the device; this decides what its movement means,
// reads the [input] mouse settings (iPadSettings.h), holds and lets go of the
// pointer, and hands the turn to iCameraLook.h.
//
// **Not the right stick.** The mouse turns the on-foot camera's two angles
// directly (iCameraOrbit.h): the camera stays where it is pointed, and does
// not ease back or swing behind the player. The stick's path is not touched,
// and takes the camera back, exactly as the game has it, the moment a
// controller or the camera keys are used; the mouse takes it again when it
// next moves.
//
// The cameras that are not the on-foot one -- a boss fight's, say -- read only
// the right stick, so while one of those has the view the mouse is given to
// them as a stick deflection, as far past the stick's deadzone as the hand
// moved (iCameraLookTaken says which is running). The Patty Wagon's camera
// takes no camera input from any device.
//
// **Holding the pointer.** The pointer is held (hidden, and kept in the
// window: SDL's relative mode) only while mouse look is on, the window has the
// keyboard, the keyboard and mouse are what the player has in hand, no menu
// wants a pointer (iMenuMouse.h) and the settings overlay is closed.
// input.mouse_release_key lets it go and takes it back by hand, to reach the
// title bar or another window. Escape always lets it go, whatever the key is
// bound to and whatever is on screen: it is taken back when play goes on after
// a menu (the pause menu Escape opens by default), or by a click in the
// window, which is then not the game's. With a controller in use it is not held at all,
// and is put away while it idles. The log says who has it when that changes.

// Called once per iPadHostPoll, after the keyboard and mouse have been read
// into `s` (port 0). `mouseRead`: the keyboard and mouse were read this poll
// (input.keyboard lets them play). `padInUse`: a controller on port 0 has a
// button down or a stick off centre. `scripted`: port 0 is played by a script
// (TSSM_INPUT), so the real mouse is neither read nor held.
void iMouseLookUpdate(iPadHostState* s, bool mouseRead, bool padInUse, bool scripted);

// FALSE while something else needs the pointer (the settings overlay).
void iMouseLookAllow(bool allowed);

// The menu pointer (iMenuMouse.h): while wanted, the pointer is let go.
// Answers whether there is a pointer to show: mouse_menus is on, the window
// has the keyboard, the overlay is closed and the keyboard and mouse are in
// use rather than a controller.
bool iMouseLookSetPointer(bool wanted);

// The pointer for a menu: where it is in the window's pixels, whether it moved
// since the last poll, the buttons that went down in it and the ones held (bit
// 0 left, bit 1 right, bit 2 middle) and the wheel (1 up, -1 down). FALSE when
// there is no pointer.
bool iMouseLookPointer(F32* x, F32* y, bool* moved, U32* pressed, U32* held, S32* wheel);

// The pointer as a hand: it is over something a click does something to.
void iMouseLookSetHand(bool hand);

// The XPAD_BUTTON_* bits the bindings give mouse button `button` (1 left, 2
// right, 3 middle) by itself, less the ones a key or a controller is holding as well:
// what a click the menu took is taken out of.
U32 iMouseLookBound(S32 button);

// A mouse button's press was used for something that is not the game's (bit 0
// left, 1 right, 2 middle): a click that closed a menu, or took the pointer
// back. iMouseLookSpentOut takes what those buttons are bound to out of `on`
// until they are let go, so play does not see the same click as a new press.
void iMouseLookSpend(U32 buttons);
U32 iMouseLookSpentOut(U32 on);

// Called from the window's event pump: Escape frees the pointer at once, also
// where the pad is not polled.
void iMouseLookPump();

// Whether the pointer is held for mouse look (as of the last poll).
bool iMouseLookHeld();

// Whether the mouse is steering the camera (mouse look on, and the mouse the
// last thing to turn it).
bool iMouseLookSteering();

// For unattended runs (iTestEvents.h, TSSM_TEST_MOUSE): a mouse of the test's
// own. Motion is in counts for the next poll; a pointer position (window
// pixels) and presses are for the menu pointer. Any call makes the test mouse
// the only mouse for the rest of the run.
void iMouseLookTestMove(F32 dx, F32 dy);
void iMouseLookTestPointer(F32 x, F32 y);
void iMouseLookTestPress(U32 buttons);
void iMouseLookTestWheel(S32 notches);
// Escape went down (the key itself, not what it is bound to).
void iMouseLookTestEscape();

#endif
