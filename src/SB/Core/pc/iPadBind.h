#ifndef IPADBIND_H
#define IPADBIND_H

#include <types.h>

// PC-only: the control bindings from config.ini, parsed. There is no GameCube
// counterpart -- a console's controller is the one the game was built around,
// and every button it has already means something.
//
// TSSM-ADAPTED from treedome. The rows are TSSM's, and they are named after
// the XBOX controller, because the port runs the Xbox release's files and every
// button prompt those files draw is an Xbox button (font.HIP's pad_button
// textures: A, B, X, Y, the triggers, white, black, Back and Start). A row is a
// button the game reads; `a = space` therefore reads "what the prompts call A
// is pressed with the space bar". docs/CONTROLS.md has the whole table, and how
// each row was established.
//
// The right-hand side is written in the DEVICE's names, which differ per
// backend, so this file does not know any of them. A backend passes its own
// token table in. What is shared is the grammar:
//
//     a  = x                one input
//     a  = x, ls            either input on its own
//     black = rt+rb         both at once
//     rt = rt+!rb           held, and something else not held
//     white =               nothing; the button can never be pressed
//
// Negation is for the GameCube controller, which has three shoulders where the
// game wants four and reads Z+L / Z+R as the other two (gc/iPad.cpp). Without
// `!` the gamecube preset could not say that holding Z turns R into black.
//
// An input may appear in any number of rows: `rt = rt` and `black = rt+rb`
// share a trigger, and nothing stops a key from pressing two rows at once.

#define IPAD_BIND_ALTS 4
#define IPAD_BIND_CHORD 3

// Enough rows for kPadBindButtons, so a backend can hold one parsed binding
// per row in a plain array. The table is a compile-time list and the count
// below is not a constant expression, so the two are checked against each
// other at startup rather than by the compiler.
#define IPAD_BIND_MAX_BUTTONS 32

// One parsed binding. Empty -- `alts` of 0 -- is a button nothing presses.
struct iPadBind
{
    S16 id[IPAD_BIND_ALTS][IPAD_BIND_CHORD];
    U8 negate[IPAD_BIND_ALTS][IPAD_BIND_CHORD];
    S8 length[IPAD_BIND_ALTS];
    S8 alts;
};

// A device input's name and whatever the backend wants to call it by. `id` is
// opaque here: SDL numbers its own buttons, the keyboard uses scancodes, and
// neither has to explain itself.
struct iPadBindToken
{
    const char* name;
    S16 id;
};

// Rows that are not game buttons: digital stand-ins for the two sticks, and a
// walk modifier. A keyboard has no stick, so these are how WASD moves; a pad
// leaves them empty by default (its sticks are read directly) but may bind
// them too.
enum iPadBindAxis
{
    IPAD_AXIS_NONE,
    IPAD_AXIS_MOVE_UP,
    IPAD_AXIS_MOVE_DOWN,
    IPAD_AXIS_MOVE_LEFT,
    IPAD_AXIS_MOVE_RIGHT,
    IPAD_AXIS_CAMERA_UP,
    IPAD_AXIS_CAMERA_DOWN,
    IPAD_AXIS_CAMERA_LEFT,
    IPAD_AXIS_CAMERA_RIGHT,
    // Held: the move axes reach half deflection, which the game walks at.
    IPAD_AXIS_WALK,
    IPAD_AXIS_COUNT
};

// One row: a button the game reads, or one of the axis rows above, and what
// presses it out of the box.
//
// This table is the single source of truth for three things that must agree:
// the keys config.ini accepts, the values a generated config.ini is written
// with, and the defaults a missing config.ini runs on.
struct iPadBindButton
{
    // As it appears left of the '=': the Xbox controller's name for it.
    const char* name;

    // XPAD_BUTTON_* bits, which is what the game actually tests. 0 for an
    // axis row.
    U32 mask;

    // Default bindings, in each backend's own token names. The pad column is
    // the base every preset starts from; see kPadBindPresets.
    const char* pad;
    const char* key;

    // What the row does under the xbox move layout, for the trailing comment
    // in a generated file and for a settings screen. NULL where there is
    // nothing worth saying. iPadBindDescribe gives the text for the layout in
    // force.
    const char* does;

    // IPAD_AXIS_* for an axis row, IPAD_AXIS_NONE for a button.
    S8 axis;
};

extern const iPadBindButton kPadBindButtons[];
extern const S32 kPadBindButtonCount;

// The row for `name`, or NULL. Case-insensitive.
const iPadBindButton* iPadBindFind(const char* name);

// What a row does under the move layout in force (iPadLayout.h): the xbox
// layout's text, or the gamecube layout's where the two differ.
const char* iPadBindDescribe(const iPadBindButton* button);

// ---------------------------------------------------------------------------
// Presets: what the physical controller is.
//
// A preset decides two things: which input carries each row on that kind of
// controller, and (input.button_icons = auto) which glyphs the prompts are
// drawn with. It does NOT decide which face button attacks -- that is the move
// layout, input.layout, in iPadLayout.h.
//
// The face rows name a LETTER, marked with '#', and the port finds the button
// printed with that letter on the pad in hand (iPadHostInputForLabel). `#B` is
// "the button printed B" -- east on an Xbox pad, west on a GameCube one, south
// on a Switch pad -- so cancel is on the button the prompt draws wherever you
// play it. A PlayStation pad answers on the usual equivalence: cross A, circle
// B, square X, triangle Y.
//
//   xbox          Xbox, Xbox 360/One/Series and XInput clones; the base
//   playstation   DualShock / DualSense; the same rows as xbox
//   switch        Switch Pro and Joy-Cons; the same rows, found by letter
//   gamecube      GameCube adapters: Z is the Back button, Z+L white, Z+R black
//   3ds           the New 3DS's own buttons; the faces by position, not letter

struct iPadBindPreset
{
    const char* name;

    // What it does, for the comment above the setting in a generated file.
    const char* does;

    // One entry per row of kPadBindButtons, in that order. NULL falls back to
    // the row's own `pad`, so a preset spells out only what it changes.
    const char* pad[IPAD_BIND_MAX_BUTTONS];
};

extern const iPadBindPreset kPadBindPresets[];
extern const S32 kPadBindPresetCount;

// What preset `preset` binds row `row` to, as the table spells it: a token
// list, or '#' and a letter for "the button printed with this letter". The
// row's own `pad` for a preset that leaves it alone or a name no preset has.
// NULL for a row out of range.
const char* iPadBindPresetEntry(const char* preset, S32 row);

// The default binding for one row on the pad, under the preset in force.
// iConfig.cpp answers `pad.*` with this and writes a generated file from it.
const char* iPadBindPadDefault(const iPadBindButton* button);

// The preset actually in force: input.preset, with "auto" resolved against the
// controller on port 0. Never NULL -- an unplaceable pad answers "xbox", which
// is the release the port's assets came off.
const char* iPadBindActivePreset();

// The single device input a binding names, or -1 when it does not name exactly
// one -- `black = rt+rb` is two, and an empty binding is none. A negated member
// does not count against it: `rt+!rb` is still the right trigger.
S16 iPadBindSoleInput(const iPadBind& bind);

// A device input's name, given the backend's own token table -- the reverse of
// what the parser does. NULL for an id the table does not have.
const char* iPadBindTokenName(S16 id, const iPadBindToken* tokens, S32 tokenCount);

// Parse one binding. `what` names the setting for the diagnostics -- an unknown
// token, too many alternatives, a chord of nothing but negations -- which are
// printed rather than returned, as the rest of the config layer does it.
//
// A binding that does not parse comes back EMPTY rather than half-applied, and
// false is returned: a typo should cost one button, not leave it bound to the
// part of the line that happened to be readable.
bool iPadBindParse(const char* text, const iPadBindToken* tokens, S32 tokenCount, const char* what,
                   iPadBind* out);

// The same, with the first problem written into `err` instead of printed. For
// a settings screen that wants to show why a binding was refused.
bool iPadBindParseQuiet(const char* text, const iPadBindToken* tokens, S32 tokenCount,
                        iPadBind* out, char* err, S32 errSize);

// A parsed binding written back in the grammar above, with the token table's
// names. `out` is always terminated.
void iPadBindFormat(const iPadBind& bind, const iPadBindToken* tokens, S32 tokenCount, char* out,
                    S32 size);

// Is the binding satisfied right now? `held` answers for one device input id.
bool iPadBindHeld(const iPadBind& bind, bool (*held)(S16 id));

// Which config.ini section a device reads, and which column of the table it
// defaults from.
enum iPadBindDevice
{
    IPAD_BIND_PAD,
    IPAD_BIND_KEYBOARD
};

// Every row's binding for one device, read from config.ini and parsed with
// that device's token table. `out` must hold IPAD_BIND_MAX_BUTTONS.
void iPadBindLoad(iPadBindDevice device, const iPadBindToken* tokens, S32 tokenCount,
                  iPadBind* out);

// Every row evaluated against `held`: the XPAD_BUTTON_* bits that are down,
// and (when `axes` is not NULL) which axis rows are down, as 1 << IPAD_AXIS_*.
U32 iPadBindEvaluate(const iPadBind* binds, bool (*held)(S16 id), U32* axes);

#endif
