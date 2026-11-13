#include "xCamera.h"
#include "xCam.h"

void xCam::start()
{
    flags |= 0x1;
}

void xCam::stop()
{
    flags &= ~0x1;
}

void xCam::scene_enter()
{
}

void xCam::scene_exit()
{
}

void xCam::refresh_mat()
{
    switch (coord_type)
    {
    case XCAM_COORD_CART:
        mat.pos = coord.cart;
        break;
    case XCAM_COORD_CYLINDER:
        convert(mat.pos, coord.cylinder);
        break;
    case XCAM_COORD_SPHERE:
        convert(mat.pos, coord.sphere);
        break;
    }

    switch (orient_type)
    {
    case XCAM_ORIENT_QUAT:
        xQuatToMat(&orient.quat, &mat);
        break;
    case XCAM_ORIENT_EULER:
        xMat3x3Euler(&mat, orient.euler.yaw, orient.euler.pitch, orient.euler.roll);
        break;
    }
}

void xCam::create()
{
    flags = 0;
    coord_type = XCAM_COORD_INVALID;
    orient_type = XCAM_ORIENT_INVALID;
    group = NULL;
}

U8 xCam::active()
{
    return group_index != -1;
}

void xCamGroup::create()
{
    xCamBlend** it = blend_cam;
    xCamBlend** end = it + 4;

    size = 0;

    for (; it != end; ++it)
    {
        *it = create_blend();
    }
}

void xCamGroup::destroy()
{
    xCamBlend** it = blend_cam;
    xCamBlend** end = it + 4;
    for (; it != end; ++it)
    {
        (*it)->destroy();
    }
}

void xCamGroup::reset()
{
    xCam** it = owned;
    xCam** end = it + size;
    for (; it != end; ++it)
    {
        xCam& c = **it;
        c.group = NULL;
        if (c.flags & 0x1)
        {
            c.stop();
        }
    }

    size = 0;
    primary = NULL;
    flags = 0;
    fov = fov_default;
    child_flags_mask = -1;
    child_flags = 0;
}

void xCamGroup::priority_force_cut()
{
    S32 next_index = get_next();

    if (primary == NULL || next_index == -1 || next_index == primary->group_index)
    {
        return;
    }

    owned[next_index]->group_flags |= 0x8;
}

void xCamGroup::update(xScene& scene, F32 dt)
{
    xVec3 prev_pos = mat.pos;
    S32 next_index = get_blend(get_next());
    xCam* next;

    if (primary == NULL || next_index != primary->group_index)
    {
        set_primary(next_index);
    }

    if (primary == NULL)
    {
        return;
    }

    primary->flags &= child_flags_mask;
    primary->flags |= child_flags;
    primary->analog = analog;
    primary->update(scene, dt);
    mat = primary->mat;
    fov = primary->fov;

    if (primary->flags & 0x4)
    {
        next = primary->get_next();
        if (next != primary)
        {
            primary->stop();
            remove_index(primary->group_index);

            if (next == NULL)
            {
                primary = NULL;
            }
            else
            {
                if (next->group_index == -1)
                {
                    next->group = this;
                    next->group_index = size;
                    owned[size] = next;
                    size++;
                }

                primary = next;
                primary->group = this;
                primary->group_flags &= ~0x10;
            }
        }
    }

    vel = (mat.pos - prev_pos) * (1.0f / dt);

    remove_released();
}

void xCamGroup::add(xCam& cam, bool force_cut)
{
    if (cam.group == this)
    {
        cam.group_flags &= ~0xC;
        if (force_cut)
        {
            cam.group_flags |= 0x8;
        }
        return;
    }

    if (cam.group != NULL)
    {
        return;
    }

    if (size >= 32)
    {
        return;
    }

    cam.group_flags = 0;
    if (force_cut)
    {
        cam.group_flags |= 0x8;
    }

    cam.group = this;
    cam.group_index = size;
    owned[size] = &cam;
    size++;
}

void xCamGroup::remove(xCam& cam, bool force_cut)
{
    if (cam.group != this)
    {
        return;
    }

    if (&cam == primary || (cam.group_flags & 0x10))
    {
        cam.group_flags &= ~0x8;
        cam.group_flags |= 0x4;
        if (force_cut)
        {
            cam.group_flags |= 0x8;
        }
        return;
    }

    remove_index(cam.group_index);
}

S32 xCamGroup::get_blend(S32 to_index)
{
    xCam* to;
    xCamBlend* parent;

    if (primary == NULL || to_index == -1 || to_index == primary->group_index)
    {
        return to_index;
    }

    to = owned[to_index];

    xCam& timing = (primary->group_flags & 0x4) ? *primary : *to;
    F32 blend_time = (timing.group_flags & 0x8) ? 0.0f : timing.cfg_common.blend_time;
    if (blend_time <= 0.0f)
    {
        return to_index;
    }

    if (to->group_flags & 0x10)
    {
        // Already blending into this camera: reverse every blend on the way up to the primary.
        for (parent = to->blender; parent != NULL; parent = parent->blender)
        {
            if (parent->dst != to)
            {
                parent->reverse();
            }
            to = parent;
        }

        return primary->group_index;
    }

    if (size >= 32)
    {
        return to_index;
    }

    parent = grab_blend_cam();
    if (parent == NULL)
    {
        return to_index;
    }

    to->blender = parent;
    primary->blender = parent;
    parent->src = primary;
    parent->dst = to;
    parent->cfg_common.blend_time = blend_time;
    primary->group_flags |= 0x10;
    to->group_flags |= 0x10;
    parent->group_flags = 0x1;
    parent->group = this;
    parent->group_index = size;
    owned[size] = parent;
    size++;
    return size - 1;
}

void xCamScreen::create(S32 w, S32 h)
{
    icam = iCameraCreate(w, h, 1);
}

void xCamScreen::destroy()
{
    iCameraDestroy(icam);
}

void xCamScreen::set_scene(xScene& scene)
{
    iCameraAssignEnv(icam, scene.env->geom);
}

void xCamScreen::prepare_render()
{
}

void xCamScreen::begin_render(bool clear)
{
    iCameraBegin(icam, clear);
    iCameraUpdateFog(icam, 0);
}

void xCamScreen::end_render()
{
    iCameraEnd(icam);
}

void xCamScreen::set_world_matrix(const xMat4x3& mat)
{
    iCameraUpdatePos(icam, (xMat4x3*)&mat);
}

void xCamScreen::set_fov(F32 fov)
{
    this->fov = fov;
    iCameraSetFOV(icam, (180.0f / PI) * fov);
}

void xCamBlend::create()
{
    xCam::create();
    flags |= 0x4;
    coord_type = XCAM_COORD_INVALID;
    orient_type = XCAM_ORIENT_INVALID;
}

void xCamBlend::destroy()
{
}

void xCamBlend::start()
{
    static const xCamCoordType coord_table[3][3] = {
        { XCAM_COORD_CART, XCAM_COORD_CART, XCAM_COORD_CART },
        { XCAM_COORD_CART, XCAM_COORD_CYLINDER, XCAM_COORD_SPHERE },
        { XCAM_COORD_CART, XCAM_COORD_SPHERE, XCAM_COORD_SPHERE },
    };
    static const xCamOrientType orient_table[2][2] = {
        { XCAM_ORIENT_QUAT, XCAM_ORIENT_QUAT },
        { XCAM_ORIENT_QUAT, XCAM_ORIENT_EULER },
    };

    if (!(src->flags & 0x1))
    {
        src->mat = mat;
        src->fov = fov;
        src->start();
    }

    if (!(dst->flags & 0x1))
    {
        dst->mat = mat;
        dst->fov = fov;
        dst->start();
    }

    cfg_common.priority = MAX(src->cfg_common.priority, dst->cfg_common.priority);

    coord_type = coord_table[src->coord_type][dst->coord_type];
    orient_type = orient_table[src->orient_type][dst->orient_type];

    time = 0.0f;
    xCam::start();
}

void xCamBlend::stop()
{
    src->stop();
    if (time < cfg_common.blend_time)
    {
        dst->stop();
    }

    xCamGroup::notify_released(*src);
    xCamGroup::notify_released(*dst);
    xCam::stop();
}

void xCamBlend::update(xScene& scene, F32 dt)
{
    F32 t;

    time += dt;
    t = time / cfg_common.blend_time;
    if (t > 1.0f)
    {
        t = 1.0f;
    }

    if (t <= 0.5f)
    {
        t = 2.0f * t * t;
    }
    else
    {
        t = 1.0f - t;
        t = 1.0f - 2.0f * t * t;
    }

    src->analog = dst->analog = analog;
    src->update(scene, dt);
    dst->update(scene, dt);

    blend_coord(t);
    blend_orient(t);

    fov = src->fov * (1.0f - t) + dst->fov * t;
    refresh_mat();
}

xCam* xCamBlend::get_next()
{
    if (time >= cfg_common.blend_time)
    {
        return dst;
    }
    return this;
}

void xCamBlend::blend_coord(F32 s)
{
    switch (coord_type)
    {
    case XCAM_COORD_CART:
    {
        xVec3 c1;
        xVec3 c2;
        blend(coord.cart, convert(*src, c1), convert(*dst, c2), s);
        break;
    }
    case XCAM_COORD_CYLINDER:
    {
        xCamCoordCylinder c1;
        xCamCoordCylinder c2;
        blend(coord.cylinder, convert(*src, c1), convert(*dst, c2), s);
        break;
    }
    case XCAM_COORD_SPHERE:
    {
        xCamCoordSphere c1;
        xCamCoordSphere c2;
        blend(coord.sphere, convert(*src, c1), convert(*dst, c2), s);
        break;
    }
    }
}

void xCamBlend::blend_orient(F32 s)
{
    switch (orient_type)
    {
    case XCAM_ORIENT_QUAT:
    {
        xQuat c1;
        xQuat c2;
        blend(orient.quat, convert(*src, c1), convert(*dst, c2), s);
        break;
    }
    case XCAM_ORIENT_EULER:
    {
        xCamOrientEuler c1;
        xCamOrientEuler c2;
        blend(orient.euler, convert(*src, c1), convert(*dst, c2), s);
        break;
    }
    }
}

// Dead-stripped in retail; restored because its vtable fixes the .data order of
// __vt__xCamCutscene (see xCutscene.cpp). Layout from the PS2 DWARF; methods from Ratatouille's
// xCamDebug, adapted to TSSM's xCam (refresh_input, debug_render and add_tweaks are left out, as
// TSSM's xCam has no such virtuals). The bodies must not reorder literals shared with live code.
struct xCamDebug : xCam
{
    struct config : xCamConfigFollow
    {
        xVec2 stick_vel;
        xVec3 move_vel;
        F32 slow_turn;
        F32 slow_move;
    };

    config cfg;
    analog_data* analog_move;
    analog_data* analog_turn;
    xVec2 dpad;
    F32 slow_input;

    void create();
    void start();
    void update(xScene& scene, F32 dt);
};

void xCamDebug::create()
{
    xCam::create();
    coord_type = XCAM_COORD_CART;
    orient_type = XCAM_ORIENT_EULER;
}

void xCamDebug::start()
{
    xCam::start();
    coord.cart = mat.pos;
    xMat3x3GetEuler(&mat, (xVec3*)&orient.euler);
    dpad = 0.0f;
    slow_input = 0.0f;
    orient.euler.roll = 0.0f;
}

void xCamDebug::update(xScene& scene, F32 dt)
{
    F32 turn_mult = 1.0f + (cfg.slow_turn - 1.0f) * slow_input;
    F32 move_mult = 1.0f + (cfg.slow_move - 1.0f) * slow_input;

    F32 dyaw = dt * (turn_mult * (cfg.stick_vel.x * analog_turn->offset.x));
    // Ratatouille wraps the yaw with xrmod here; TSSM did not, as its literals would then come
    // before xCamBlend's in .sdata2.
    orient.euler.yaw += dyaw;

    F32 dpitch = dt * (turn_mult * (cfg.stick_vel.y * analog_turn->offset.y));
    orient.euler.pitch += dpitch;
    // Ratatouille's clamp; its literals are only used here, so they are stripped with it.
    orient.euler.pitch = range_limit<F32>(orient.euler.pitch, -DEG2RAD(89), DEG2RAD(89));

    xMat3x3Euler(&mat, orient.euler.yaw, orient.euler.pitch, orient.euler.roll);

    xVec3 dright = mat.right;
    dright *= dt * (move_mult * (cfg.move_vel.x * analog_move->offset.x));
    coord.cart += dright;

    F32 dy = dt * (move_mult * (-dpad.y * cfg.move_vel.y));
    coord.cart.y += dy;

    xVec3 dat = mat.at;
    dat *= dt * (move_mult * (cfg.move_vel.z * analog_move->offset.y));
    coord.cart += dat;

    mat.pos = coord.cart;
}
