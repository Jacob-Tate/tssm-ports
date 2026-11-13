#ifndef IPADPROMPTS_H
#define IPADPROMPTS_H

#include <types.h>

#include "iPadLayout.h"

struct iPadHostState;

// PC-only: keeping the game's button prompts true.
//
// **How a prompt is drawn.** Every button prompt is xtextbox markup in a TEXT
// asset. A move's prompt includes a picture by name -- button_spinattack_text
// is `{i:button_picture_04}` on the Xbox disc -- and the picture is
// `{tex:pad_button4;...}`, a 32x32 texture in font.HIP. Nothing draws a
// button any other way.
//
// **Two things can make one false, and this fixes both without touching the
// game code or the player's files:**
//
//   1. The move layout (iPadLayout.h). Under the gamecube layout the six move
//      prompts the Xbox disc points at X or B must point at the other one.
//      Those six TEXT assets hold exactly `{i:button_picture_0N}`; the digit is
//      rewritten in place in memory, which keeps the length and needs no
//      allocation. They are looked up with xSTFindAsset, the same way the text
//      box finds them.
//
//   2. The device. The pictures are an Xbox pad's, which is wrong for a
//      keyboard, a PlayStation, Switch or GameCube pad, or a rebound button.
//      The pad_button textures' rasters are swapped for ones this file draws:
//      the key's name on a keycap, the PlayStation's shapes, the Switch's and
//      GameCube's letters -- chosen through the BINDING, so a rebound button
//      draws what now presses it. input.button_icons picks the set; auto
//      follows the device last used. The Xbox set on an Xbox pad with the
//      default rows changes nothing.
//
//   3. The verb. The Xbox disc says "Pull RT to talk"; with a key, or a
//      GameCube pad's R, it says "Press". push_trigger_text is rewritten in
//      place when the longer word fits the asset.
//
// All are re-applied every half second and whenever the layout, the bindings
// or the device change, because a level load brings in fresh copies.

// Once per frame, from iPadUpdate for port 0.
void iPadPromptsUpdate(const iPadHostState* host);

// Puts every raster back and frees the drawn ones.
void iPadPromptsShutdown();

// input.button_icons: "auto", "xbox", "playstation", "switch", "gamecube",
// "keyboard" or "off".
void iPadPromptsSetIcons(const char* choice);
const char* iPadPromptsIconsChoice();

// The set being drawn right now ("keyboard", "playstation", ...), or NULL when
// the disc's own glyphs are showing.
const char* iPadPromptsActiveSet();

// Forget what was applied, so the next update re-applies everything (after a
// rebinding).
void iPadPromptsInvalidate();

// ---------------------------------------------------------------------------
// The pieces, for the tests.

// The six move prompts, by the name their TEXT asset has.
struct iPadPromptMove
{
    const char* name;
    // The digit the Xbox disc has, '3' (pad_button3, B) or '4' (pad_button4, X).
    char xbox;
};
extern const iPadPromptMove kPadPromptMoves[];
extern const S32 kPadPromptMoveCount;

// Rewrites one move prompt's text for `layout`. `text` is the asset's string
// (after the length word), `size` the bytes it may use. TRUE if it changed.
S32 iPadPromptsPatchMove(const iPadPromptMove& move, char* text, U32 size, iPadLayoutKind layout);

// "Pull" <-> "Press" (and lower case) in push_trigger_text, in place when it
// fits `capacity` (the bytes after the length word), updating the length.
// TRUE if it changed.
S32 iPadPromptsPatchVerb(U32* len, char* text, U32 capacity, bool press);

// Which XPAD bits a pad_button texture's picture stands for on the Xbox disc,
// by texture name; 0 for a name that is not a prompt picture.
U32 iPadPromptsTextureBits(const char* name);

// The label a glyph set draws for the input that `row` (a kPadBindButtons
// name) is bound to, or NULL when it keeps the disc's picture. For the tests
// and the input test.
const char* iPadPromptsLabelFor(const char* set, const char* row);

#endif
