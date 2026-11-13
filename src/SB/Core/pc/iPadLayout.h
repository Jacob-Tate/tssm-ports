#ifndef IPADLAYOUT_H
#define IPADLAYOUT_H

#include <types.h>

// PC-only: which face button each of the player's MOVES reads.
//
// TSSM-ADAPTED from treedome. The two TSSM releases the port can be compared
// with disagree about exactly one thing, measured in their own code:
//
//     move                                   GameCube code   Xbox prompt   Xbox code
//     Karate Spin, Star Spin, Smash,
//       melee, car reverse                   TRIANGLE (B)    X             TRIANGLE (X)
//     SpongeBowl, Cartwheel, car horn,
//       trigger action                       O (X)           B             O (B)
//
// The Xbox's pad layer (DEFAULT.XBE) sets O for the B button and TRIANGLE for
// X -- the other way round from gc/iPad.cpp -- and its zUI::DoUpdate swaps the
// two events back (O raises PadPressTriangle, TRIANGLE raises PadPressO), as do
// its talk box (quit reads O), bungee dismount (reads O) and cheat entry (reads
// TRIANGLE|SQUARE). So on the Xbox the menus cancel on B and open options on X,
// exactly as on the GameCube, and only the moves changed button.
//
// The port runs the GameCube code, whose pad layer is letter for letter
// (A->X, B->TRIANGLE, X->O, Y->SQUARE). To get the Xbox's moves on top of it,
// the move reads in the game code are wrapped in XPAD_MOVE (xPad.h), which is
// this function: under the xbox layout it trades TRIANGLE and O, under the
// gamecube layout it is the identity. The menus, talk boxes, bungee dismount,
// cheat entry and screen adjust are not wrapped.
//
// The xbox layout is the default because the prompts the port draws are the
// Xbox's: "press X to Karate Spin" is true under it. The gamecube layout is
// for players who want the console's feel; iPadPrompts.cpp then points the six
// affected prompts (spin, Patrick spin, Smash, bowl, cartwheel, car reverse) at
// the other button picture, so they stay true too.

enum iPadLayoutKind
{
    IPAD_LAYOUT_XBOX,
    IPAD_LAYOUT_GAMECUBE
};

// input.layout: "auto", "xbox" or "gamecube". auto is gamecube when the
// preset in force is gamecube (a GameCube pad) and xbox otherwise.
void iPadLayoutSetChoice(const char* choice);
const char* iPadLayoutChoice();

// Re-resolves "auto" against the preset in force. The backend calls it when
// port 0 changes hands; iPadLayoutSetChoice calls it too.
void iPadLayoutRefresh();

iPadLayoutKind iPadLayoutActive();
const char* iPadLayoutName(iPadLayoutKind kind);

// The bits to read in place of `mask`, under the layout in force. Trades
// TRIANGLE and O wherever they appear in the mask (so 0xc0000, TRIANGLE |
// SQUARE, works too); leaves every other bit alone. A load and two tests, for
// the per-frame reads it sits in.
U32 iPadLayoutButton(U32 mask);

#endif
