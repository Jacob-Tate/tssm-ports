#ifndef ICAMERAORBIT_H
#define ICAMERAORBIT_H

#include <types.h>

struct xCamera;

// PC-only: the free orbit the mouse (or a touchscreen drag) steers, for
// zCameraUpdate, the on-foot camera. From treedome, the BfBB PC port, which
// keeps it inside zCamera.cpp; here it is a file of the port's, so the game's
// zCamera.cpp carries only the calls.
//
// **What it replaces, and what it keeps.** zCameraUpdate builds the camera out
// of a distance, a height and a yaw, and every problem a mouse has with it is
// in how those three move:
//
//   - `pgoal = cam->pcur` every frame, so the yaw is wherever the camera
//     drifted to. Nobody owns it.
//   - pitch is not an angle. `pitch_s` is a blend weight between a camera
//     pinned above the player and one pinned below, which eases back to the
//     middle when the stick is let go.
//   - the overrotation block swings the yaw around behind the player's
//     direction of travel on its own, seconds after the stick was let go.
//
// So this owns the two angles and rebuilds the three goals from them.
// Everything else -- the framing the goals came from (near camera, lasso, the
// bounces), the collision, the look-at -- is the game's own.
//
// The aim point is read back out of the goals the game made this frame, so a
// change of framing is followed. The turn is exact, since a mouse that turns
// late feels like lag; the point orbited trails the player a little, so a jump
// or a step does not jolt the view.

// Whether a source is steering (iCameraLook.h): no swing behind the player
// then.
bool iCameraOrbitSteering();

// Takes this frame's turn. When a source is steering and the game allows it
// (`allow`: camera input on, no wall jump, no recentring in progress),
// replaces the goals with the orbit's and answers TRUE: the caller then moves
// and aims the camera with no ease. FALSE leaves the goals alone, and the
// orbit starts again from wherever the camera is when it next runs.
bool iCameraOrbit(xCamera* cam, F32 dt, bool allow, F32& dgoal, F32& hgoal, F32& pgoal,
                  F32& yaw_goal, F32& pitch_goal);

// After xCameraUpdate, whose collision may have pulled the camera in toward
// the player: shortens the orbit's reach to match.
void iCameraOrbitCollided(xCamera* cam);

#endif
