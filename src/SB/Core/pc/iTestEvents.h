#ifndef ITESTEVENTS_H
#define ITESTEVENTS_H

#include <types.h>

// PC-only debug events for unattended runs: things a test wants to happen at a
// given frame that no button press can ask for. Compiled with the game units
// (tssm_game), because it calls game code; iCapture.cpp's frame hook calls it.
//
//   TSSM_TEST_WARP=<list>   screen warps (xScreenWarp::emit) at the player, a
//                           comma-separated list of
//                           "frame:type[:radius[:seconds[:intensity]]]".
//                           type is warble, lens, ripple, shockwave, heat or
//                           static; radius in world units (4 by default, the
//                           player's shockwave), seconds 1 and intensity 1 by
//                           default. Each is logged ("tssm: test: ...").
//
//   TSSM_TEST_PLACE=<list>  puts the player down at a given frame, a
//                           comma-separated list of "frame:x:y:z[:yaw]" (world
//                           units; yaw in degrees, 0 faces +z, 90 faces +x), as
//                           the game's checkpoint restore does (frame, model
//                           matrix, camera). For reaching a spot no button
//                           script can walk to; logged with where the player
//                           was.
//
//   TSSM_TEST_HOLD=<a>-<b>  from frame a to b, puts the player (and camera) back at
//                           the latest TSSM_TEST_PLACE spot every frame.
//
//   TSSM_TEST_POWERUP=<frame>:<level>
//                           sets every move's power-up to level at that frame.
//
//   TSSM_TEST_CAMLOG=<n>    every n frames, logs the camera's position and
//                           whether a level fly-through is playing ("tssm: test:
//                           frame 120: camera at (x, y, z), fly-through on"),
//                           for tools/regress.py's fly-through check.
//
//   TSSM_TEST_CRASH=<frame> a write through a null pointer at that frame (a
//                           number above 1), for the crash handler, crash.log
//                           and the log. Any other value crashes at startup,
//                           before main (tssm_main.cpp).
//
// Nothing happens unless a variable is set.

// Called once per presented frame by the capture hook (iCapture.cpp), with
// the frame's number (1 is the first frame presented).
void iTestEventsFrame(S32 frame);

#endif
