#ifndef IPADTEST_H
#define IPADTEST_H

#include <types.h>

struct _tagxPad;
struct iPadHostState;

// PC-only: the input test. TSSM_INPUT_TEST=1 (or `test = log` in [input])
// prints one line for every frame in which anything the game reads from port 0
// changed -- the XPAD bits by name, both sticks in the game's units, the
// analog triggers, and which device it came from -- so "this button does
// nothing" can be told apart from "this button never reached the game".
// TSSM_INPUT_TEST=overlay also draws the controller on screen, lit as the game
// sees it (GL3 and Vulkan; see iTouchOverlay.h).

// The settings line, once, when the pad layer starts.
void iPadTestStart();

// Port 0, once per frame, after iPadUpdate has filled `pad`. `on` is the bits
// iPadUpdate returned; `host` may be NULL.
void iPadTestFrame(const _tagxPad* pad, U32 on, const iPadHostState* host);

// What port 0 held on the last frame, kept whether or not the test is on: the
// live view of a settings screen reads it (iPadControlsGetLive).
struct iPadTestSnapshot
{
    U32 on;
    S8 stick_x;
    S8 stick_y;
    S8 cstick_x;
    S8 cstick_y;
    F32 trigger_l;
    F32 trigger_r;
    U32 sources;
    bool connected;
};
const iPadTestSnapshot& iPadTestLast();

// The XPAD bits as the Xbox controller's names ("A X LT"), for the log and the
// tests. `out` is always terminated.
void iPadTestDescribeBits(U32 bits, char* out, S32 size);

#endif
