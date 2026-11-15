// The 3DS's stereo hook into the game (docs/3DS.md, "Stereoscopic 3D"): once a
// frame the renderer asks where the eyes converge. A game unit (it reads the
// game's globals), built for the 3DS only; the renderer side is
// 3ds/c3d/c3dstereo.cpp.

#include "iStereo3DS.h"

#include "c3d/rwc3d.h"
#include "xEnt.h"
#include "zCamera.h"
#include "zGameState.h"
#include "zGlobals.h"

void iStereo3DSFocus()
{
    // The camera the game draws the world with (zCamSB.cpp keeps it up to
    // date through zCameraUpdate), and the player's model.
    RwCamera* cam = xglobals->oldSkoolCamera.lo_cam;
    const xEnt* player = (const xEnt*)xglobals->___player_ent_dont_use_directly;
    const bool hasPlayer = player != NULL && player->model != NULL;
    float pos[3];
    if (hasPlayer)
    {
        const xVec3* p = xEntGetPos(player);
        pos[0] = p->x;
        pos[1] = p->y;
        pos[2] = p->z;
    }
    // The camera does not follow the player through a fly-through, a cutscene
    // or with tracking stopped: the plane is held there.
    const int fixed =
        zcam_fly != 0 || zcam_cutscene != 0 || zCameraIsTrackingDisabled() != 0 || !hasPlayer;
    // Outside play (the title, the pause menu, the options, the galleries)
    // the 3D models the menus show stay at the screen with their text.
    const int flat = zGameModeGet() != eGameMode_Game;
    rw::c3d::stereoFocus((rw::Camera*)cam, hasPlayer ? pos : NULL, fixed, flat);
}
