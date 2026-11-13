#ifndef IPADHOST_H
#define IPADHOST_H

#include <types.h>

// PC-only. There is no GameCube counterpart to this file: the console reads
// its controllers through one API that is always present, so iPad.cpp there
// talks to PADRead directly. A host has several possible input sources and
// none of them is guaranteed to exist at build time, so the part of iPad that
// touches hardware is behind this seam and the rest -- the mapping onto
// _tagxPad, which is the part with the game's semantics in it -- is not.
//
// TSSM-ADAPTED from treedome: the sticks arrive raw (the stick curve is
// iPad.cpp's), the triggers' analog values are kept, the mouse can turn the
// camera, and the keyboard can play alongside a controller.
//
// Buttons are reported in XPAD_BUTTON_* terms, already mapped through the
// bindings (iPadBind.h).

#define IPAD_MAX_CONTROLLERS 4

struct iPadHostState
{
    bool connected;

    // XPAD_BUTTON_* bits, from xPad.h.
    U32 buttons;

    // Full deflection at magnitude 1, NO deadzone taken out: iPad.cpp applies
    // the stick curve the player picked (input.stick_curve). Y is up-positive,
    // as the GameCube's stick reports it -- iPadUpdate negates it for the game,
    // which wants screen coordinates.
    F32 stick_x;
    F32 stick_y;
    F32 substick_x;
    F32 substick_y;

    // The analog triggers, 0 released to 1 fully pulled. The game reads only
    // the digital click (LT/RT rows, at input.trigger_threshold); these are for
    // the input test and a settings screen.
    F32 trigger_l;
    F32 trigger_r;

    // Camera turn already in the game's C-stick units (-127..127), added after
    // the stick curve. The mouse writes this: a mouse moves by a distance, and
    // the camera turns by an amount proportional to how far the C-stick is past
    // its deadzone, so the distance is turned into that.
    F32 camera_x;
    F32 camera_y;

    // Which device produced the input this frame, for the input test and for
    // input.button_icons = auto: IPAD_HOST_SOURCE_* bits.
    U32 sources;
};

#define IPAD_HOST_SOURCE_PAD 0x1
#define IPAD_HOST_SOURCE_KEYBOARD 0x2
#define IPAD_HOST_SOURCE_MOUSE 0x4
#define IPAD_HOST_SOURCE_TOUCH 0x8

void iPadHostInit();
void iPadHostExit();

// Called once per iPadUpdate on port 0, mirroring where the GameCube build
// calls PADRead: one read per frame feeds every port.
void iPadHostPoll();

const iPadHostState* iPadHostGet(S32 port);

// 0 stops the motor, 1 runs it. The GameCube's motor has no intermediate
// setting, and PADControlMotor is the only thing retail ever calls.
void iPadHostRumble(S32 port, S32 on);

// A key that is not the game's, read each poll whether or not a pad holds
// port 0: the backend calls `fn` with F8's state every frame once one is set.
typedef void (*iPadHostHotkeyFn)(S32 down);
void iPadHostSetHotkey(iPadHostHotkeyFn fn);

// Names the backend that was linked in, for the startup log.
const char* iPadHostName();

// The name of the controller on `port` ("Xbox Series X Controller"), "keyboard"
// when the keyboard alone is playing port 0, or NULL when nothing is.
const char* iPadHostDeviceName(S32 port);

// The device input bound to one XPAD_BUTTON_* on the pad, by the name
// config.ini writes it as -- "a", "lt". NULL when nothing is bound to it, and
// NULL when what is bound is a chord.
const char* iPadHostBoundInput(U32 xpadButton);

// What sort of controller is on port 0, as a preset name: "xbox",
// "playstation", "switch", "gamecube". NULL when there is no pad, or when the
// backend cannot tell one kind from another.
const char* iPadHostPadKind();

// Which device input carries the letter `letter` ('A', 'B', 'X' or 'Y') on the
// controller on port 0, by the name config.ini writes it as. NULL when there is
// no pad, or none of its buttons is printed with that letter. A pad printed
// with shapes answers on the usual equivalence: cross A, circle B, square X,
// triangle Y.
const char* iPadHostInputForLabel(char letter);

// Re-read [pad] and [keyboard] (after iConfigSet has changed one), and the
// input settings (trigger threshold, keyboard mode, mouse).
void iPadHostReloadBindings();

// The pad's parsed bindings, one per row of kPadBindButtons, and a number that
// changes whenever they (or the keyboard's) are reloaded -- for the prompts,
// which draw what is bound.
struct iPadBind;
const iPadBind* iPadHostPadBinds();
U32 iPadHostBindGeneration();

// The next key (or mouse button), or the next input on the controller on port
// 0, to go down, written as its binding name ("space", "lb"). Called every
// frame while waiting; `prime` TRUE on the first call forgets whatever is
// already held, so the press that opened the wait does not answer it. FALSE
// until one goes down.
S32 iPadHostCaptureKey(S32 prime, char* out, S32 size);
S32 iPadHostCaptureButton(S32 prime, char* out, S32 size);

// With `on`, port 0 holds exactly `buttons` (XPAD_BUTTON_*) and every real
// controller and the keyboard are ignored. For scripted runs.
void iPadHostScript(S32 on, U32 buttons);

#endif
