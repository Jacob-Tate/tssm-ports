// The free orbit the mouse steers. See iCameraOrbit.h.

#include "iCameraOrbit.h"

#include "iCameraLook.h"
#include "iMath.h"
#include "xCamera.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xVec3.h"
#include "xVec3Inlines.h"

namespace
{
    F32 sYaw;
    F32 sPitch;
    S32 sOn;

    // The point being orbited, trailing the player, and the framing it is
    // orbited at, trailing the goals. Changes of framing -- the near camera,
    // lasso, a bounce -- arrive as a step in the goals and are eased here.
    xVec3 sPivot;
    F32 sRadius;
    F32 sAim;

    // How much of the radius the camera may use, after the collision pulled it
    // in. The pull is instant; going back out is eased, so passing a pillar
    // does not pop the view in and out.
    F32 sReach;

    // Where the orbit put the camera this frame, for comparing with where the
    // collision left it, and where it was left, for the next frame to tell
    // whether anything else has moved it since.
    xVec3 sPlaced;
    xVec3 sLeft;

    // Time constants, in seconds, of the trailing above.
    const F32 kFollowXZ = 0.04f;
    const F32 kFollowY = 0.12f;
    const F32 kFraming = 0.15f;
    const F32 kReachOut = 0.25f;

    // Further than this from the player in one frame and the pivot did not
    // trail him, he was moved: a respawn, a warp. It jumps with him.
    const F32 kSnap = 4.0f;

    // How far the camera may be swung above and below the aim point. The
    // game's own extremes (zcam_above_pitch, zcam_below_pitch) are 70 degrees
    // each way, about its own poses.
    const F32 kPitchMin = -1.0471976f; // -60 degrees
    const F32 kPitchMax = 1.3089969f; // 75 degrees

    // Further than this, in world units, from where the orbit left the camera
    // last frame and something else moved it -- a cutscene, a warp, a level
    // load -- and the orbit takes the camera's word for it. zCameraUpdate's own
    // vertical_lerp nudge is a fraction of one frame of the player's movement.
    const F32 kResync = 0.5f;

    // The fraction of the way a value trailing with time constant `tau`
    // closes on its target in `dt`.
    F32 Share(F32 dt, F32 tau)
    {
        return 1.0f - xexp(-dt / tau);
    }
} // namespace

bool iCameraOrbitSteering()
{
    return iCameraLookActive() != 0;
}

bool iCameraOrbit(xCamera* cam, F32 dt, bool allow, F32& dgoal, F32& hgoal, F32& pgoal,
                  F32& yaw_goal, F32& pitch_goal)
{
    // Taken whenever the on-foot camera runs, allowed or not, so a turn made
    // while it is briefly busy is dropped rather than kept for later, and so
    // the mouse can tell this camera from one that reads only the C-stick.
    F32 yaw = 0.0f;
    F32 pitch = 0.0f;
    if (!iCameraLookTake(&yaw, &pitch) || !allow || cam->tgt_mat == NULL)
    {
        sOn = 0;
        return false;
    }

    F32 aim = hgoal - dgoal * itan(pitch_goal);
    F32 rise = hgoal - aim;
    F32 radius = xsqrt(SQR(dgoal) + SQR(rise));
    if (radius < 0.01f)
    {
        sOn = 0;
        return false;
    }

    const xVec3& tgt = cam->tgt_mat->pos;

    if (sOn && (xVec3Dist(&sPivot, &tgt) > kSnap || xVec3Dist(&sLeft, &cam->mat.pos) > kResync))
    {
        sOn = 0;
    }

    if (!sOn)
    {
        sOn = 1;
        sPivot = tgt;
        sRadius = radius;
        sAim = aim;
        sReach = 1.0f;
        sYaw = xatan2(cam->mat.pos.x - tgt.x, cam->mat.pos.z - tgt.z);
        sPitch = CLAMP(pitch_goal, kPitchMin, kPitchMax);
    }

    // The orbit angle is where the camera sits around the player, so it runs
    // against the turn: the view turns right as the camera swings round to
    // the left.
    sYaw = xDangleClamp(sYaw - yaw);
    sPitch = CLAMP(sPitch + pitch, kPitchMin, kPitchMax);

    F32 xz = Share(dt, kFollowXZ);
    F32 y = Share(dt, kFollowY);
    sPivot.x += (tgt.x - sPivot.x) * xz;
    sPivot.y += (tgt.y - sPivot.y) * y;
    sPivot.z += (tgt.z - sPivot.z) * xz;

    F32 framing = Share(dt, kFraming);
    sRadius += (radius - sRadius) * framing;
    sAim += (aim - sAim) * framing;

    sReach += (1.0f - sReach) * Share(dt, kReachOut);

    // The camera on a sphere about the aim point. Positive pitch is above it,
    // looking down, which is the engine's sign for the look as well.
    xVec3 at;
    at.x = sPivot.x;
    at.y = sPivot.y + sAim;
    at.z = sPivot.z;

    F32 reach = sRadius * sReach;
    F32 flat = reach * icos(sPitch);

    sPlaced.x = at.x + flat * isin(sYaw);
    sPlaced.y = at.y + reach * isin(sPitch);
    sPlaced.z = at.z + flat * icos(sYaw);

    // As goals relative to the player, in the form xCameraMove takes them
    // under flag 0x20: a world-space angle, a flat distance and a height.
    F32 dx = sPlaced.x - tgt.x;
    F32 dz = sPlaced.z - tgt.z;
    dgoal = xsqrt(SQR(dx) + SQR(dz));
    hgoal = sPlaced.y - tgt.y;
    pgoal = xatan2(dx, dz);

    // Aimed from where the camera is put, not from where it was: with no ease
    // a frame-old direction slides the player off centre on every sweep.
    yaw_goal = xDangleClamp(sYaw + PI);
    pitch_goal = sPitch;

    return true;
}

void iCameraOrbitCollided(xCamera* cam)
{
    if (!sOn)
    {
        return;
    }

    // The collision sweeps from here; see _xCameraUpdate.
    xVec3 from = cam->tgt_mat->pos;
    from.y += 0.7f;

    F32 wanted = xVec3Dist(&sPlaced, &from);
    F32 got = xVec3Dist(&cam->mat.pos, &from);
    if (wanted > 0.01f && got < wanted)
    {
        sReach *= got / wanted;
    }

    sLeft = cam->mat.pos;
}
