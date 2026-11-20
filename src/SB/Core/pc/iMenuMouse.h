#ifndef IMENUMOUSE_H
#define IMENUMOUSE_H

#include <types.h>

// PC-only: the mouse in the game's own menus (the title and main menu, the
// pause screen, options, the save and load screens, and whatever a mod builds
// with the game's UI widgets). Adapted from treedome's iMenuMouse, for this
// game's UI system (zUI.h: text, image, box, model and user-string widgets,
// and the links between them).
//
// While a menu is up the pointer is free (iMouseLook.h). Pointing at an item
// gives it the highlight, a left click accepts it, a right click is the back
// button and the wheel steps through a list as the d-pad does.
//
// **Selection goes through the menu's own links.** A widget with focus answers
// a pad press by sending events along its links, and the links of a menu's
// items are what move the highlight from one to the next. Pointing at an item
// finds the shortest chain of those presses from the widgets that have focus
// now to the one under the pointer and plays them, as zUIMgrUpdate would
// deliver them, so every sound, every unselect and every side effect is the
// one the pad would have made. An item no chain reaches is not one a pad could
// select either, and is left alone.
//
// **A click is the accept button** (A, or Start where that is all anything
// answers), added to the frame's pad, so it reaches the item the way a
// controller's does. A right click is B, the game's back.
//
// **A hint is a button.** Text that leads with a button's picture --
// "{i:button_picture_04} Options", "{i:button_picture_03} Return to Game" --
// presses that button when clicked. So is an arrow: a small widget that a
// direction press lights up (the pause screen's level arrows).
//
// **A value is clicked toward.** On a row that left and right change rather
// than leave (vibration, a volume), a click to the right of the row is the
// press toward the side clicked.
//
// A left click on none of these is still the button the left mouse button is
// bound to, which is what the hints on screen show for it.
//
// Nothing here is in the game's UI code. A controller in use puts the pointer
// away and the menus are the pad's alone, as are the keyboard's keys at all
// times.

// For iPad.cpp, once a frame on port 0 before the game sees the buttons:
// `inMenu` is whether a menu is up (xPadUpdate's own test: zMenuRunning or
// zGameIsPaused) and `on` the XPAD_BUTTON_* bits the devices hold. Returns the
// bits the game is to see.
U32 iMenuMouseUpdate(S32 inMenu, U32 on);

// Menu units (640x480 across the 4:3 UI box) to window pixels, for a test's
// pointer (iTestEvents.h).
void iMenuMouseToWindow(F32 mx, F32 my, F32* wx, F32* wy);

// For unattended runs (TSSM_TEST_UILOG): logs the widgets a pointer could
// mean, with their rectangles, state, text and pad links.
void iMenuMouseLog(S32 frame);

#endif
