#include "zCamSB.h"

#include "zGlobals.h"
#include "zCamera.h"
#include "zEntPlayer.h"
#include "zMain.h"

void zCamSB::create()
{
    xCam::create();
    flags |= 0x2;
    coord_type = XCAM_COORD_CART;
    orient_type = XCAM_ORIENT_QUAT;
    cfg_common.priority = 0;
    cfg_common.blend_time = 1.0f;
    xCameraInit(&globals.oldSkoolCamera, FB_XRES, FB_YRES);
    zCameraReset(&globals.oldSkoolCamera);
    xCameraSetScene(&globals.oldSkoolCamera, globals.sceneCur);
}

void zCamSB::destroy()
{
    xCameraExit(&globals.oldSkoolCamera);
}

void zCamSB::start()
{
    fov = (PI / 180.0f) * globals.oldSkoolCamera.fov;
    xCam::start();
}

void zCamSB::update(xScene& scene, F32 dt)
{
    if (globals.sceneCur != NULL && globals.sceneCur->sceneID == 'MNUS')
    {
        return;
    }

    xCameraSetTargetMatrix(&globals.oldSkoolCamera,
                           xEntGetFrame(xglobals->___player_ent_dont_use_directly));
    xCameraSetTargetOMatrix(&globals.oldSkoolCamera, &sCameraLastMat);
    zcam_playervel = &xglobals->___player_ent_dont_use_directly->frame->vel;

    zCameraUpdate(&globals.oldSkoolCamera, dt);

    if (!zcam_flyJustStopped || globals.cam->size <= 1)
    {
        coord.cart = globals.oldSkoolCamera.mat.pos;
        xQuatFromMat(&orient.quat, &globals.oldSkoolCamera.mat);
        fov = (PI / 180.0f) * globals.oldSkoolCamera.fov;
    }

    refresh_mat();
}
