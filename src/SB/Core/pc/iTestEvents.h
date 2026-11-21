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
//                             frame:touch:x:y, frame:lift
//                                                        a finger put down at
//                                                        x, y of the menus'
//                                                        640 by 480, and lifted
//                                                        (the Switch's touch
//                                                        screen; a list of only
//                                                        these leaves the mouse
//                                                        as it is)
//                           The log then says who has the pointer as the real
//                           mouse's does ("tssm: mouse: free (Escape)"); the
//                           real pointer is not touched.
//                           It works beside TSSM_INPUT's scripted controller.
//
//   TSSM_TEST_KEYS=<list>   a keyboard of the test's own, in place of the real
//                           one from the first frame (iPadKeyboard.h): entries
//                           "frame:key[+key...][/frames]", comma separated, e.g.
//                           "300:space/4,700:tab". Each holds its keys from that
//                           frame for the given number of frames (6 when not
//                           given), as TSSM_INPUT does. Key names are config.ini's
//                           [keyboard] names (tab, space, escape, c, w, mouse1,
//                           ...). The keys go through the [keyboard] bindings
//                           and everything after them, as real keys do, whether
//                           or not the window has the focus; the real keyboard
//                           and mouse are not read. Leave TSSM_INPUT unset: a
//                           scripted controller ignores the keyboard.
//
//   TSSM_TEST_UILOG=<n>     every n frames, logs the menu widgets a pointer could
//                           mean (iMenuMouse.h): each one's name, rectangle,
//                           state, text and pad links, which are items of a list
//                           or hints, and what the pointer is on.
//
//   TSSM_TEST_CHEAT=<list>  asks for cheats at given frames as the Cheats tab does, a
//                           comma-separated list of "frame:what" (iCheats.h,
//                           docs/CHEATS.md): code:<id>, extra:<word>, refill,
//                           port:<key>:on|off, port:alloff, hit:<n> (a hit of n
//                           health units), host:... (a co-op host's state) and
//                           release.
//
//   TSSM_TEST_CHEATLOG=<n>  every n frames, logs the cheats' state: the game's cheat
//                           word, points multiplier, health, moves, the developers'
//                           switches, the Extras items that are on, the task
//                           counters and the port's cheats in force.
//
//   TSSM_TEST_HUDLOG=<n>    every n frames, logs the HUD widgets drawn
//                           (iHudAnchor.h): each one's name, the rectangle it
//                           was authored at and is at now (640x480 units), the
//                           edges it belongs to and where its top left corner
//                           lands on the screen in pixels ("tssm: test: frame
//                           1900: hud 5A1D6F0E 'HUD_FONT_NITRO' authored 520 405
//                           30 36 now 520 405 30 36 right bottom at pixel 902.5
//                           607.5").
//
//   TSSM_TEST_CRASH=<frame> a write through a null pointer at that frame (a
//                           number above 1), for the crash handler, crash.log
//                           and the log. Any other value crashes at startup,
//                           before main (tssm_main.cpp).
//
// A level load is one frame to all of the above, however many frames of the
// loading screen are shown in it, so the load has hooks of its own, read by
// iAsync.cpp:
//
//   TSSM_TEST_LOADLOG=1     one line per load ("tssm: load 1: 106.7 ms, 3 loading
//                           frames (0 skipped), 2.8 ms drawing them (longest 2.47
//                           ms), longest gap 66.2 ms ended by the load's end, 608
//                           pumps; frames by pump: start 1 file read 1 textures
//                           1"). 2 also logs every stretch of 5 ms or more in
//                           which the load gave the screen no chance of a frame.
//
//   TSSM_CAPTURE_LOAD=<n>   saves the first n loading frames of every load to
//                           TSSM_CAPTURE_DIR as load<load>_<frame>.png. The time
//                           the saving takes is not counted as the frame's, but
//                           it is part of the load's time and of the gaps
//                           TSSM_TEST_LOADLOG reports.
//
//   TSSM_TEST_LOADSLOW=<ms> a slow machine's load: every package step waits that
//                           long, asking for loading frames the way a long read
//                           does.
//
//   TSSM_TEST_LOADEVENT=<frame>:<what>
//                           at that loading frame of the first load, "mode"
//                           switches between windowed and borderless as Alt+Enter
//                           does, and "close" asks the window to close.
//
// Nothing happens unless a variable is set.

// Called once per presented frame by the capture hook (iCapture.cpp), with
// the frame's number (1 is the first frame presented).
void iTestEventsFrame(S32 frame);

#endif
