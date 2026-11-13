#ifndef ITESTSEND_H
#define ITESTSEND_H

#include <types.h>

// PC-only test aid for unattended runs (iCapture.cpp's frame hook calls it):
//
//   TSSM_TEST_SEND=<frame>:<name>[:<event>][,...]
//                  at that frame, the game's own event (a number from
//                  xEvent.h, eEventRun (18) when not given) to the current
//                  scene's object with that name, as a link in the level
//                  would send it. E.g. 1200:AUTOSAVE_DONE_SCRIPT starts the
//                  end of SC02, DE02 or GG02 (BB01: 700): the level's story
//                  movie, then the next level. Logged ("tssm: test: ...").
//
// Compiled with the game units, because it calls game code. Nothing happens
// unless the variable is set.
void iTestSendFrame(S32 frame);

#endif
