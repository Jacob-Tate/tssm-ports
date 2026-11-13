#include "zCamMarker.h"

#include "xGlobals.h"
#include "xCam.h"
#include "xMemMgr.h"
#include "xEnt.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

namespace
{
    struct zCamWidget : xCam
    {
        U8 transitioning;
        const xCamAsset* asset;
        F32 time;
        F32 end_time;
        F32 start_fov;
        F32 end_fov;
        xVec3 start_loc;
        xQuat start_dir;
        xQuat end_dir;
        F32 last_s;
        xEnt* target_ent;
        xMat4x3 end_mat;

        void create();
        void start();
        void stop();
        void update(xScene& scene, F32 dt);
        void transition(const xCamAsset* asset, F32 time, xEnt* target_ent);

        // The helpers below have no symbols (always inlined); their names are guesses.
        void update_end()
        {
            if (!target_ent)
            {
                end_mat.right = asset->right;
                end_mat.up = asset->up;
                end_mat.at = asset->at;
            }
            else
            {
                const xVec3* target = xEntGetPos(target_ent);
                xVec3 dir = { coord.cart.x - target->x, coord.cart.y - target->y,
                              coord.cart.z - target->z };
                xMat3x3LookVec(&end_mat, &dir);
            }
            end_mat.pos = asset->pos;
            xQuatFromMat(&end_dir, &end_mat);
        }

        void snap_to_end()
        {
            mat = end_mat;
            fov = (PI / 180.0f) * asset->fov;
            coord.cart = mat.pos;
            xQuatFromMat(&orient.quat, &mat);
        }

        void begin_transition()
        {
            if (end_time <= 0.0f)
            {
                snap_to_end();
            }
            else
            {
                start_fov = fov;
                start_loc = coord.cart;
                start_dir = orient.quat;
                end_fov = (PI / 180.0f) * asset->fov;
            }
        }
    };

    U8 activated;
    zCamWidget* widget_cam;
    xCamAsset static_cam_asset;
} // namespace

namespace
{
    void zCamWidget::update(xScene& scene, F32 dt)
    {
        if (end_time <= 0.0f)
        {
            return;
        }

        time += dt;
        update_end();

        if (time >= end_time)
        {
            snap_to_end();
            return;
        }

        F32 s = time / end_time;
        if (s <= 0.5f)
        {
            s = 2.0f * s * s;
        }
        else
        {
            F32 t = 1.0f - s;
            s = 1.0f - 2.0f * t * t;
        }

        F32 si = 1.0f - s;
        fov = start_fov * si + end_fov * s;
        coord.cart.x = start_loc.x * si + end_mat.pos.x * s;
        coord.cart.y = start_loc.y * si + end_mat.pos.y * s;
        coord.cart.z = start_loc.z * si + end_mat.pos.z * s;
        xQuatSlerp(&orient.quat, &start_dir, &end_dir, (s - last_s) / (1.0f - last_s));
        start_dir = orient.quat;
        last_s = s;
        refresh_mat();
    }

    void zCamWidget::stop()
    {
        activated = false;
        transitioning = false;
        target_ent = NULL;
        xCam::stop();
    }

    void zCamWidget::start()
    {
        xCam::start();
        activated = true;
        coord.cart = mat.pos;
        xQuatFromMat(&orient.quat, &mat);

        if (transitioning)
        {
            begin_transition();
        }
    }

    void zCamWidget::transition(const xCamAsset* asset, F32 time, xEnt* target_ent)
    {
        this->asset = asset;
        this->target_ent = target_ent;
        end_time = (time > 0.0f) ? time : asset->trans_time;
        this->time = 0.0f;
        last_s = 0.0f;
        update_end();

        if (flags & 0x1)
        {
            begin_transition();
        }

        transitioning = true;
    }

    void activate_camera(const xCamAsset& a, xEnt* ent)
    {
        if (!activated)
        {
            widget_cam->cfg_common.blend_time = 0.0f;
            globals.cam->add(*widget_cam, true);
            activated = true;
        }
        widget_cam->transition(&a, 0.0f, ent);
    }

    void zCamWidget::create()
    {
        xCam::create();
        coord_type = XCAM_COORD_CART;
        orient_type = XCAM_ORIENT_QUAT;
        transitioning = false;
        asset = NULL;
        target_ent = NULL;
        cfg_common.priority = 0xc0;
        cfg_common.blend_time = 0.0f;
    }
} // namespace

void zCamMarkerSceneEnter()
{
    widget_cam = new (xMemAllocSize(sizeof(zCamWidget))) zCamWidget;
    widget_cam->create();
    activated = false;
}

void zCamMarkerReset()
{
    if (activated)
    {
        globals.cam->remove(*widget_cam, true);
    }
    activated = false;
}

void zCamMarkerInit(xBase* b, xCamAsset* asset)
{
    xBaseInit(b, asset);

    if (b->linkCount)
    {
        b->link = (xLinkAsset*)(asset + 1);
    }

    b->eventFunc = zCamMarkerEventCB;
    ((zCamMarker*)b)->asset = asset;
}

void zCamMarkerSave(zCamMarker* m, xSerial* s)
{
    xBaseSave(m, s);
}

void zCamMarkerLoad(zCamMarker* m, xSerial* s)
{
    xBaseLoad(m, s);
}

void zCamMarkerEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* b3, U32)
{
    switch (toEvent)
    {
    case eEventOn:
    case eEventSwitch:
        activate_camera(*((zCamMarker*)to)->asset, NULL);
        break;
    case eEventEndConversation:
        if (activated)
        {
            widget_cam->cfg_common.blend_time = 0.0f;
            globals.cam->remove(*widget_cam, true);
        }
        break;
    }
}

// Placeholder for dead-stripped code (probably zStaticCameraAsset_EventCB, body unknown): retail
// interns this 75.0f while compiling this file, although no surviving function here uses it.
static void __deadstripped_zCamMarker(F32* fov)
{
    *fov = 75.0f;
}
