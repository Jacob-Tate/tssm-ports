#ifndef IPADCONTROLS_H
#define IPADCONTROLS_H

#include <types.h>

#include "iPadBind.h"

// PC-only: the binding API a settings screen calls -- written for the Controls
// tab of the planned Dear ImGui overlay, and used by the headless input test.
//
// Everything here goes through config.ini, so a change made on screen is the
// same as a line typed into the file: iConfigSet holds it (in effect at once),
// iPadControlsSave writes it, and the file stays the one place a binding lives.
// Nothing here draws anything.
//
// Rows are kPadBindButtons (iPadBind.h): the buttons the game's prompts show,
// plus the keyboard's stick rows. Devices are IPAD_BIND_PAD and
// IPAD_BIND_KEYBOARD.

S32 iPadControlsRowCount();

struct iPadControlsRowInfo
{
    // As config.ini spells it: "a", "lt", "move_up".
    const char* name;
    // What it does under the move layout in force; NULL for nothing to say.
    const char* does;
    // The XPAD bits it sets; 0 for a stick row.
    U32 mask;
    bool stick;
};
bool iPadControlsRow(S32 row, iPadControlsRowInfo* out);

// The row called `name`, or -1.
S32 iPadControlsFindRow(const char* name);

// The binding in force, in config.ini's grammar ("mouse1, f"). `isDefault`
// (may be NULL) says whether it is the preset's or the file's own.
void iPadControlsGet(iPadBindDevice device, S32 row, char* out, S32 size, bool* isDefault);

// Replace a binding. The text is checked first: FALSE leaves the binding as it
// was and puts the reason in `err`. An empty string unbinds the row.
bool iPadControlsSet(iPadBindDevice device, S32 row, const char* text, char* err, S32 errSize);

// Add one input (a token from iPadControlsCapture, or a chord "rt+rb") as
// another alternative, or remove alternative `index` (0-based).
bool iPadControlsAdd(iPadBindDevice device, S32 row, const char* input, char* err, S32 errSize);
bool iPadControlsRemove(iPadBindDevice device, S32 row, S32 index);

// Back to the default (the preset's, for the pad), for one row or all.
void iPadControlsReset(iPadBindDevice device, S32 row);
void iPadControlsResetAll(iPadBindDevice device);

// The next key or mouse button (keyboard), or the next pad input, to go down,
// as a token. Call every frame while waiting, `prime` TRUE on the first call.
S32 iPadControlsCapture(iPadBindDevice device, S32 prime, char* out, S32 size);

// A token's name for people: "Space", "Left mouse", "A", "Cross" (on a
// PlayStation pad), "ZR" (on a Switch pad).
const char* iPadControlsInputLabel(iPadBindDevice device, const char* token);

// Other rows that already use `token` on `device`, for a "this is also..."
// hint. Writes up to `max` row indices; returns how many there are.
S32 iPadControlsWhoUses(iPadBindDevice device, const char* token, S32* rows, S32 max);

// The [input] settings, by name ("layout", "stick_curve", "trigger_threshold"
// ...). Set checks the value against the setting's domain and applies it at
// once.
const char* iPadControlsGetSetting(const char* name);
bool iPadControlsSetSetting(const char* name, const char* value, char* err, S32 errSize);

// Write config.ini. FALSE if it could not be written.
bool iPadControlsSave();

// What port 0 holds right now, for a live view.
struct iPadControlsLive
{
    U32 on;
    S8 stick_x;
    S8 stick_y;
    S8 cstick_x;
    S8 cstick_y;
    F32 trigger_l;
    F32 trigger_r;
    bool connected;
    // "Xbox Series X Controller", "keyboard", or NULL.
    const char* device;
    const char* preset;
    const char* layout;
    // The glyph set the prompts are drawn with, or NULL for the disc's own.
    const char* icons;
};
void iPadControlsGetLive(iPadControlsLive* out);

// While FALSE the mouse is left alone (not captured for the camera), for an
// overlay that needs the cursor. TRUE again hands it back to the camera.
void iPadControlsSetMouseCapture(bool allowed);

#endif
