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
//   TSSM_TEST_CARLOG=<a>-<b>
//                           from frame a to b, logs the Patty Wagon every frame
//                           ("tssm: test: frame 1500: car dt 0.008333 share
//                           0.5000 at (x, y, z) vel (x, y, z)"): the frame's
//                           measured time, its step share (zCar.cpp) and the
//                           car, for the car's frame-rate checks.
//
//   TSSM_TEST_SPOTLOG=<n>   every n frames, logs the scene's spotlights ("tssm:
//                           test: frame 300: spotlights 12 on of 12, 9 drawn,
//                           1 flares, camera inside 1, nearest beam -0.80;
//                           715F7AAB+ ..."): how many are on, how many of those
//                           are in view, how many of those have a lamp flare
//                           that shows with spotlights = xbox (the lamp faces
//                           the camera), in how many beams the camera is
//                           (zSpotlight's light volume "inside"), how far the
//                           camera is outside the side of the nearest beam
//                           (below 0: inside), and the IDs of the lights that
//                           are on, "+" after one the camera is inside. For the
//                           spotlight checks of [extras] spotlights
//                           (docs/EXTRAS.md, tools/regress.py). Also, when it
//                           changes, how many cone colours the Xbox light
//                           volumes keep ("light volumes: 3 cone colours
//                           kept"), which a new level starts again.
//
//   TSSM_TEST_CROWDLOG=<n>  every n frames, logs the scene's crowd objects (the
//                           Xbox's skinned crowd models) and how many of them
//                           moved on in their idle cycle since the line before
//                           ("tssm: test: frame 2040: crowd 22 objects, speed
//                           1.00, 22 moved since frame 1980"): the crowd check
//                           of [extras] crowds where the camera is not still
//                           enough to compare frames (BB01).
//
//   TSSM_TEST_LOOKLOG=1     with each TSSM_TEST_CAMLOG line, a second one with the
//                           on-foot camera's angles and whether the mouse is
//                           steering it ("tssm: test: frame 2310: look orbit
//                           5.9350 yaw 2.7935 pitch 0.1745 at (...) right (...)
//                           mouse steering"), for the mouse look checks.
//
//   TSSM_TEST_MOUSE=<list>  a mouse of the test's own, in place of the real one
//                           for the whole run (iMouseLook.h): a comma-separated
//                           list of
//                             frame:move:dx:dy[/frames]  counts moved, in each of
//                                                        that many frames (1)
//                             frame:at:x:y               the menu pointer put at
//                                                        x, y of the menus' 640
//                                                        by 480
//                             frame:click, frame:rclick  a left or right press
//                             frame:wheel:n              a notch up (n above 0)
//                                                        or down
//                             frame:esc                  the Escape key going
//                                                        down, for the pointer
//                                                        (what it is bound to
//                                                        is TSSM_INPUT's)
//                           The log then says who has the pointer as the real
//                           mouse's does ("tssm: mouse: free (Escape)"); the
//                           real pointer is not touched.
//                           It works beside TSSM_INPUT's scripted controller.
//
//   TSSM_TEST_UILOG=<n>     every n frames, logs the menu widgets a pointer could
//                           mean (iMenuMouse.h): each one's name, rectangle,
//                           state, text and pad links, which are items of a list
//                           or hints, and what the pointer is on.
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
