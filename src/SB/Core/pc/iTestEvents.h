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
//   TSSM_TEST_SCENE=<frame>:<SCEN>
//                           goes to another level (four letters and digits,
//                           like DE01) at that frame, through a portal of its
//                           own as the game's soak test does, whatever the save
//                           has unlocked. For tools/coop_test.py's following
//                           test. Waits for plain play (no pause, task or save
//                           screen): asked for while the game is paused, the
//                           level change would never start.
//
//   TSSM_TEST_KILL=<frame>:<radius>
//                           defeats the nearest living enemy within radius of
//                           the player at that frame, through the game's own
//                           damage (an instant kill), so its death plays out.
//                           For tools/coop_test.py.
//
//   TSSM_TEST_PLACE_ON=<frame>:<id>[:<dy>[:<yaw>]]
//                           puts the player on top of the entity with that asset
//                           ID (hex) at that frame, wherever it is then: above
//                           the middle of its bound, dy units over its top (1 by
//                           default), as TSSM_TEST_PLACE does. For
//                           tools/coop_test.py's riding test.
//
//   TSSM_TEST_COOP_GO=<frame>
//                           at that frame, what the Co-op tab's "Go to the
//                           host's level" button does (iCoopGoToHost). For
//                           tools/coop_test.py, with following off.
//
//   TSSM_TEST_AUTOSAVE=<frame>[,<frame>...]
//                           at each frame (at most 4), the game's own autosave
//                           as a token pickup asks for it, into the first save
//                           slot when none was picked. Logged, with whether the
//                           slot was kept (saved). For tools/coop_test.py's
//                           check that a co-op guest's game does not save.
//
//   TSSM_TEST_STALL=<frame>:<seconds>[,<frame>:<seconds>...]
//                           at each frame (at most 4), the game thread sleeps
//                           that long (0.05 to 120 s), as when its window is
//                           held or dragged: nothing of the game runs, while
//                           co-op's I/O thread goes on. Logged before and
//                           after, with the clock co-op's logs use. For
//                           tools/coop_test.py's stall variants
//                           (docs/MULTIPLAYER.md 5.11).
//
//   TSSM_TEST_SAVE=<frame>  at that frame, the save screen as the pause menu
//                           opens it, saving straight into the first slot of
//                           the first folder (as if the player had picked
//                           them); the game's own prompts follow. Logged, and
//                           again when the game is back in play. For
//                           tools/coop_test.py's check that a co-op guest's
//                           save by hand is refused (docs/MULTIPLAYER.md 7.6).
//
//   TSSM_TEST_QUIT=<frame>  at that frame, the game is left for the title
//                           menu as the pause menu's quit leaves it (the
//                           game's own exit event). Logged. For
//                           tools/coop_test.py's title variant (co-op's
//                           review of M4: the title draws nothing of the
//                           last level's partner).
//
//   TSSM_TEST_EVENT=<frame>:<id>:<what>[,...]
//                           at that frame, the game's own event to the level's
//                           object with that asset ID (hex): teleport (a
//                           portal, as walking into it), cutscene (a cutscene
//                           manager's start, as a trigger sends it) or run (a
//                           camera fly-through's). For tools/coop_test.py's
//                           level change and cutscene tests (docs/MULTIPLAYER.md 7).
//                           A teleport waits for plain play, as TSSM_TEST_SCENE
//                           does (a portal can only be walked into then).
//
//   TSSM_TEST_PLACE_CAMERA=reset
//                           also puts the camera behind the placed player, as
//                           the game's same-level teleport does (zCamReset);
//                           without it a long move can leave the camera behind.
//
//   TSSM_TEST_HOLD=<a>-<b>  from frame a to b, puts the player (and camera) back at
//                           the latest TSSM_TEST_PLACE spot every frame.
//
//   TSSM_TEST_POWERUP=<frame>:<level>
//                           sets every move's power-up to level at that frame.
//
//   TSSM_TEST_FACE=<first>-<last>/<every>:<id>:<dist>
//                           every `every` frames from first to last, puts the
//                           player dist units from the entity with that asset ID
//                           (hex; an enemy too), on the player's side of it and
//                           facing it, so a scripted attack reaches an enemy that
//                           moves (tools/coop_test.py's fight).
//
//   TSSM_TEST_HEALTH=<frame>:<n>
//                           sets the player's health to n (1 to its maximum) at
//                           that frame: one hit from defeat, for
//                           tools/coop_test.py's co-op respawn test.
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
