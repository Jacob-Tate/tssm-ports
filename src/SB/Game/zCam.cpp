#include "zCam.h"

#include "xDynAsset.h"
#include "xEvent.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "zEntPlayerOOBState.h"
#include "zEntTrigger.h"
#include "zGlobals.h"
#include "zMain.h"
#include "zScene.h"
#include "xstransvc.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

namespace
{
    enum mode_enum
    {
        MODE_INVALID = -1,
        MODE_FOLLOW,
        MAX_MODE
    };

    S32 input_disabled;
    mode_enum current_mode = MODE_INVALID;
    xCam* mode_cams[MAX_MODE];
    bool have_default_orientation;
    xVec3 default_orientation = { 0.0f, 0.0f, 0.0f };
    zCamSB* follow_cam;
    xCamScreen global_screen;
    xCamGroup global_cam;

    inline void set_mode(mode_enum mode)
    {
        if (current_mode != mode)
        {
            globals.cam->add(*mode_cams[mode], false);

            if (current_mode != MODE_INVALID)
            {
                globals.cam->remove(*mode_cams[current_mode], false);
            }

            current_mode = mode;
        }
    }
} // namespace

// Dynamic "pointer" asset (a position plus yaw/pitch/roll in degrees). zDispatcher.cpp, later in
// this wad, uses this definition too.
struct pointer_asset : xDynAsset
{
    xVec3 loc;
    F32 yaw;
    F32 pitch;
    F32 roll;

    static const char* type_name()
    {
        return "pointer";
    }
};

static void zCamNotifyTriggers();

void zCamSceneEnter(zScene& scene, const xMat4x3& mat)
{
    xCam::scene_enter();

    have_default_orientation = false;
    input_disabled = 0;
    globals.screen = &global_screen;
    globals.screen->create(FB_XRES, FB_YRES);
    globals.screen->set_scene(scene);
    globals.screen->set_fov(DEG2RAD(75.0f));

    globals.cam = &global_cam;
    globals.cam->create();
    globals.cam->fov_default = globals.screen->fov;

    follow_cam = new (xMemAllocSize(sizeof(zCamSB))) zCamSB;
    follow_cam->create();
    follow_cam->owner = 'ZCAM';

    current_mode = MODE_INVALID;
    mode_cams[MODE_FOLLOW] = follow_cam;

    zCamReset(mat);
}

void zCamSceneExit()
{
    globals.cam->destroy();
    globals.screen->destroy();
    follow_cam = NULL;
    xCam::scene_exit();
}

void zCamReset(const xMat4x3& mat)
{
    input_disabled = 0;

    globals.cam->reset();
    xMat3x3Euler(&globals.cam->mat, &default_orientation);
    globals.cam->mat.pos = mat.pos;
    xCameraMove(&globals.oldSkoolCamera, globals.oldSkoolCamera.flags, globals.oldSkoolCamera.dgoal,
                globals.oldSkoolCamera.hgoal, PI + xatan2(mat.at.x, mat.at.z), 0.0f, 0.0f, 0.0f);

    current_mode = MODE_INVALID;
    set_mode(MODE_FOLLOW);
}

void zCamUpdate(F32 dt)
{
    if (globals.pad0->on & XPAD_BUTTON_R2)
    {
        globals.cam->analog = globals.pad0->analog[0];
    }
    else
    {
        globals.cam->analog = globals.pad0->analog[1];
    }

    globals.cam->update(*globals.sceneCur, dt);

    if (globals.screen->fov != globals.cam->fov)
    {
        globals.screen->set_fov(globals.cam->fov);
    }

    if (!oob_state::update_camera(&globals.cam->mat, dt))
    {
        globals.screen->set_world_matrix(globals.cam->mat);
    }

    zCamNotifyTriggers();
}

static void zCamNotifyTriggers()
{
    xScene& s = *globals.sceneCur;

    xSphere camSphere;
    camSphere.center = globals.cam->mat.pos;
    camSphere.r = 0.1f;

    xVec3 dummyDir;
    dummyDir.assign(0.0f, 0.0f, 0.0f);

    zEntTrigger** it = (zEntTrigger**)s.trigs;
    zEntTrigger** end = it + s.num_trigs;
    for (; it != end; ++it)
    {
        zEntTrigger& trig = **it;

        if (xBaseIsEnabled(&trig) && !(zEntTriggerAsset(trig)->flags & 0x1))
        {
            bool want_enter = false;
            bool want_exit = false;
            xLinkAsset* link = trig.link;
            xLinkAsset* end_link = link + trig.linkCount;
            for (; link != end_link; ++link)
            {
                if (link->srcEvent == eEventEnterCamera)
                {
                    want_enter = true;
                }
                else if (link->srcEvent == eEventExitCamera)
                {
                    want_exit = true;
                }
            }
            want_enter = want_enter && !(trig.entered & 0x4);
            want_exit = want_exit && (trig.entered & 0x4);

            if (want_enter || want_exit)
            {
                bool inside = zEntTriggerHitsSphere(trig, camSphere, dummyDir);
                if (inside)
                {
                    trig.entered |= 0x4;
                }
                else
                {
                    trig.entered &= ~0x4;
                }

                if (want_enter && inside)
                {
                    zEntEvent(&trig, eEventEnterCamera);
                }
                else if (want_exit && !inside)
                {
                    zEntEvent(&trig, eEventExitCamera);
                }
            }
        }
    }
}

void zCamPrepareRender()
{
    globals.screen->prepare_render();
}

void zCamBeginRender()
{
    globals.screen->begin_render(false);
}

void zCamEndRender()
{
    globals.screen->end_render();
}

void zCamAdd(xCam& cam, bool force_cut)
{
    globals.cam->add(cam, force_cut);
}

void zCamRemove(xCam& cam, bool force_cut)
{
    globals.cam->remove(cam, force_cut);
}

inline void zCamSetStartOrientation(F32 yaw, F32 pitch, F32 roll)
{
    have_default_orientation = true;
    default_orientation.x = yaw;
    default_orientation.y = pitch;
    default_orientation.z = roll;
}

inline void zCamSetStartOrientation(U32 pointer_id)
{
    U32 size;
    void* a = xSTFindAsset(pointer_id, &size);
    const pointer_asset& pa = *(const pointer_asset*)a;

    zCamSetStartOrientation(pa.yaw, pa.pitch, pa.roll);
}

xMat4x3& zCamGetMatrix()
{
    return globals.cam->mat;
}

zCamSB* zCamGetDefault()
{
    return follow_cam;
}
