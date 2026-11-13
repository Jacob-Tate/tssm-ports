#ifndef XCAM_H
#define XCAM_H

// xCam, xCamBlend and xCamGroup have their own header (xCam.h in the PS2 DWARF); xGlobals.h only
// holds pointers to them. It includes the tweak system: retail units that include it also have the
// tweak_callback templates. Inline helpers without symbols take their names from the Ratatouille
// GC DWARF.
#include "xDebugTweak.h"
#include "xBase.h"
#include "xMath3.h"
#include "xMath2.h"
#include "xMathInlines.h"
#include "xMemMgr.h"

#include <rwcore.h>

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

struct xScene;

enum xCamCoordType
{
    XCAM_COORD_CART,
    XCAM_COORD_CYLINDER,
    XCAM_COORD_SPHERE,
    XCAM_COORD_MAX,
    XCAM_COORD_INVALID = 0xffffffff,
};

enum xCamOrientType
{
    XCAM_ORIENT_QUAT,
    XCAM_ORIENT_EULER,
    XCAM_ORIENT_MAX,
    XCAM_ORIENT_INVALID = 0xffffffff,
};

struct xCamConfigCommon
{
    U8 priority;
    U8 pad1;
    U8 pad2;
    U8 pad3;
    F32 blend_time;
};

struct xCamBlend;
struct xCamGroup;
struct xCam;

struct xCamCoordCylinder
{
    xVec3 origin;
    F32 dist;
    F32 height;
    F32 theta;
};

struct xCamCoordSphere
{
    xVec3 origin;
    F32 dist;
    xQuat dir;
};

struct xCamOrientEuler
{
    F32 yaw;
    F32 pitch;
    F32 roll;
};

// Layout from the PS2 DWARF; the nested zone_data is from Ratatouille's xCam.h.
struct xCamConfigFollow
{
    struct zone_data
    {
        xVec3 offset;
        xVec3 face;
    };

    zone_data zone_rest;
    zone_data zone_above;
    zone_data zone_below;
    F32 speed_zone_offset;
    F32 speed_zone_face;
    F32 speed_move_orbit;
};

struct xCam
{
    struct _class_0
    {
        union
        {
            xVec3 cart;
            xCamCoordCylinder cylinder;
            xCamCoordSphere sphere;
        };
    };

    struct _class_1
    {
        union
        {
            xQuat quat;
            xCamOrientEuler euler;
        };
    };

    xMat4x3 mat;
    F32 fov;
    S32 flags;
    U32 owner;
    xCamGroup* group;
    analog_data analog;
    F32 motion_factor;
    xCamCoordType coord_type;
    xCamOrientType orient_type;
    _class_0 coord;
    _class_1 orient;
    xCamConfigCommon cfg_common;

    // GC build: the vtable pointer sits at 0xAC (between cfg_common and group_index),
    // see __vt__4xCam and the stores in zCamMarkerSceneEnter / xCamBlend::stop.
    virtual void create();
    virtual void destroy()
    {
    }
    virtual void start();
    virtual void stop();
    virtual void update(xScene& scene, F32 dt) = 0;
    virtual xCam* get_next()
    {
        return NULL;
    }
    virtual xCamConfigFollow* config_follow()
    {
        return NULL;
    }

    S32 group_index;
    S32 group_flags;
    xCamBlend* blender;

    // No symbols (inlined into refresh_mat).
    static xVec3& convert(xVec3& o, const xCamCoordCylinder& i)
    {
        o.x = i.dist * isin(i.theta) + i.origin.x;
        o.y = i.origin.y + i.height;
        o.z = i.dist * icos(i.theta) + i.origin.z;
        return o;
    }

    static xVec3& convert(xVec3& o, const xCamCoordSphere& i)
    {
        xVec3 dir;
        xQuatToLookVec(&i.dir, &dir);
        o.x = i.dist * dir.x + i.origin.x;
        o.y = i.dist * dir.y + i.origin.y;
        o.z = i.dist * dir.z + i.origin.z;
        return o;
    }

    // No symbols (inlined into xCamBlend::blend_coord).
    static xCamCoordCylinder& convert(xCamCoordCylinder& o, const xCamCoordSphere& i)
    {
        xVec3 offset;
        o.origin = i.origin;
        xQuatToLookVec(&i.dir, &offset);
        offset *= i.dist;
        o.dist = xsqrt(offset.x * offset.x + offset.z * offset.z);
        o.height = offset.y;
        o.theta = xatan2(offset.x, offset.z);
        return o;
    }

    static xCamCoordSphere& convert(xCamCoordSphere& o, const xCamCoordCylinder& i)
    {
        xMat3x3 mat;
        o.origin = i.origin;
        xVec3 offset = { i.dist * -isin(i.theta), -i.height, i.dist * -icos(i.theta) };
        o.dist = xMat3x3LookVec(&mat, &offset);
        xQuatFromMat(&o.dir, &mat);
        return o;
    }

    // No symbol and no caller in TSSM: the inverse of convert(xVec3&, const xCamCoordSphere&).
    // Retail still has its unused zero 'offset' template, numbered before convert's, in every
    // unit that includes this header. Declared after that convert, as in Ratatouille, to keep
    // that order.
    static void coord_to_local(xCamCoordSphere& c, const xVec3& loc, const xVec3& origin)
    {
        c.origin = origin;
        xVec3 offset = { origin.x - loc.x, origin.y - loc.y, origin.z - loc.z };
        xMat3x3 dir_mat;
        c.dist = xMat3x3LookVec(&dir_mat, &offset);
        xQuatFromMat(&c.dir, &dir_mat);
    }

    static void blend(F32& o, F32 a, F32 b, F32 s)
    {
        o = a * (1.0f - s) + b * s;
    }

    static void blend(xVec3& o, const xVec3& a, const xVec3& b, F32 s)
    {
        blend(o.x, a.x, b.x, s);
        blend(o.y, a.y, b.y, s);
        blend(o.z, a.z, b.z, s);
    }

    static void blend(xCamCoordCylinder& o, const xCamCoordCylinder& a, const xCamCoordCylinder& b,
                      F32 s)
    {
        blend(o.origin, a.origin, b.origin, s);
        blend(o.dist, a.dist, b.dist, s);
        blend(o.height, a.height, b.height, s);
        blend_radian(o.theta, a.theta, b.theta, s);
    }

    static void blend(xCamCoordSphere& o, const xCamCoordSphere& a, const xCamCoordSphere& b, F32 s)
    {
        blend(o.origin, a.origin, b.origin, s);
        blend(o.dist, a.dist, b.dist, s);
        blend(o.dir, a.dir, b.dir, s);
    }

    // No symbols (inlined into xCamBlend::blend_orient).
    static xQuat& convert(xQuat& o, const xCamOrientEuler& i)
    {
        xMat3x3 mat;
        xMat3x3Euler(&mat, (const xVec3*)&i);
        xQuatFromMat(&o, &mat);
        return o;
    }

    static xCamOrientEuler& convert(xCamOrientEuler& o, const xQuat& i)
    {
        xMat3x3 mat;
        xQuatToMat(&i, &mat);
        xMat3x3GetEuler(&mat, (xVec3*)&o);
        return o;
    }

    static void blend(xQuat& o, const xQuat& a, const xQuat& b, F32 s)
    {
        xQuatSlerp(&o, &a, &b, s);
    }

    // 'diff' is const: a non-const diff changes the registers of the inlined xrmod calls.
    static void blend_radian(F32& o, F32 a, F32 b, F32 s)
    {
        const F32 diff = xrmod(PI + (b - a)) - PI;
        o = xrmod(a + diff * s);
    }

    static void blend(xCamOrientEuler& o, const xCamOrientEuler& a, const xCamOrientEuler& b, F32 s)
    {
        blend_radian(o.yaw, a.yaw, b.yaw, s);
        blend_radian(o.pitch, a.pitch, b.pitch, s);
        blend_radian(o.roll, a.roll, b.roll, s);
    }

    // Critically damped spring from s0 towards s1 with decay factor e = exp(-speed * dt).
    // No symbol (inlined into the spring_interp<T,S> instances).
    static void spring_interp(F32& s, F32& v, F32 s0, F32 s1, F32 dt, F32 speed, F32 e)
    {
        F32 ds = s0 - s1;
        xDampSpring(ds, v, dt, speed, e);
        s = s1 + ds;
    }

    // Spring on the distance between s0 and s1 (the velocity may only pull towards s1).
    // No symbol (inlined into spring_interp<xVec3,f>).
    static void spring_interp(xVec3& s, F32& v, const xVec3& s0, const xVec3& s1, F32 dt, F32 speed,
                              F32 e)
    {
        F32 dx = s0.x - s1.x;
        F32 dy = s0.y - s1.y;
        F32 dz = s0.z - s1.z;
        F32 sdist = xsqrt(dx * dx + dy * dy + dz * dz);

        if (sdist < 0.0001f)
        {
            s = s1;
            v = 0.0f;
            return;
        }

        F32 dist = sdist;
        xDampSpring(dist, v, dt, speed, e);

        if (v > 0.0f)
        {
            v = 0.0f;
        }

        if (dist <= 0.0f)
        {
            s = s1;
        }
        else
        {
            F32 u = dist / sdist;
            s.x = u * dx + s1.x;
            s.y = u * dy + s1.y;
            s.z = u * dz + s1.z;
        }
    }

    // Spring on the angle between s0 and s1, applied with a slerp from s1 towards s0.
    // No symbol (inlined into spring_interp<xQuat,f>).
    static void spring_interp(xQuat& s, F32& v, const xQuat& s0, const xQuat& s1, F32 dt, F32 speed,
                              F32 e)
    {
        F32 a = s0.v.dot(s1.v) + s0.s * s1.s;

        if (a > -a)
        {
            a = xacos(a);
        }
        else
        {
            a = xacos(-a);
        }

        F32 sa = xabs(xrmod(a + PI) - PI);

        if (sa < 0.0001f)
        {
            s = s1;
            v = 0.0f;
            return;
        }

        // A separate sdist ('a = sa; xDampSpring(a, ...)' compiles differently).
        F32 sdist = sa;
        xDampSpring(sdist, v, dt, speed, e);

        if (v > 0.0f)
        {
            v = 0.0f;
        }

        if (sdist <= 0.0f)
        {
            s = s1;
        }
        else
        {
            xQuatSlerp(&s, &s1, &s0, sdist / sa);
        }
    }

    // Weak template instances, defined below the class: an in-class definition would be inlined
    // into the callers, while retail calls them. Each T needs its own 7-argument overload.
    template <class T, class S>
    static void spring_interp(T& s, S& v, const T& s0, const T& s1, F32 dt, F32 speed);

    U8 active();
    void refresh_mat();
    static void scene_exit();
    static void scene_enter();
};

template <class T, class S>
void xCam::spring_interp(T& s, S& v, const T& s0, const T& s1, F32 dt, F32 speed)
{
    F32 e = xexp(-speed * dt);
    spring_interp(s, v, s0, s1, dt, speed, e);
}

struct xCamBlend : xCam
{
    xCam* src;
    xCam* dst;
    F32 time;

    // No symbol (inlined into xCamGroup::get_blend). Turns the blend around, keeping the progress
    // made so far.
    void reverse()
    {
        xCam* temp = src;
        src = dst;
        dst = temp;
        time = cfg_common.blend_time - time;
    }

    // No symbols (inlined into blend_coord): the camera's position in the requested form, converted
    // into c if needed.
    const xVec3& convert(const xCam& cam, xVec3& c) const
    {
        switch (cam.coord_type)
        {
        case XCAM_COORD_CART:
            return cam.coord.cart;
        case XCAM_COORD_CYLINDER:
            return xCam::convert(c, cam.coord.cylinder);
        case XCAM_COORD_SPHERE:
            return xCam::convert(c, cam.coord.sphere);
        }
        return c;
    }

    const xCamCoordCylinder& convert(const xCam& cam, xCamCoordCylinder& c) const
    {
        switch (cam.coord_type)
        {
        case XCAM_COORD_CYLINDER:
            return cam.coord.cylinder;
        case XCAM_COORD_SPHERE:
            return xCam::convert(c, cam.coord.sphere);
        }
        return c;
    }

    const xCamCoordSphere& convert(const xCam& cam, xCamCoordSphere& c) const
    {
        switch (cam.coord_type)
        {
        case XCAM_COORD_CYLINDER:
            return xCam::convert(c, cam.coord.cylinder);
        case XCAM_COORD_SPHERE:
            return cam.coord.sphere;
        }
        return c;
    }

    // No symbols (inlined into blend_orient): the camera's orientation in the requested form,
    // converted into c if needed.
    const xQuat& convert(const xCam& cam, xQuat& c) const
    {
        switch (cam.orient_type)
        {
        case XCAM_ORIENT_QUAT:
            return cam.orient.quat;
        case XCAM_ORIENT_EULER:
            return xCam::convert(c, cam.orient.euler);
        }
        return c;
    }

    const xCamOrientEuler& convert(const xCam& cam, xCamOrientEuler& c) const
    {
        switch (cam.orient_type)
        {
        case XCAM_ORIENT_QUAT:
            return xCam::convert(c, cam.orient.quat);
        case XCAM_ORIENT_EULER:
            return cam.orient.euler;
        }
        return c;
    }

    void blend_orient(F32 s);
    void blend_coord(F32 s);
    xCam* get_next();
    void update(xScene& scene, F32 dt);
    void stop();
    void start();
    void destroy();
    void create();
};

struct xCamGroup
{
    xMat4x3 mat;
    xVec3 vel;
    F32 fov;
    F32 fov_default;
    S32 flags;
    xCam* primary;
    analog_data analog;
    xCam* owned[32];
    S32 size;
    S32 primary_index;
    S32 child_flags;
    S32 child_flags_mask;
    xCamBlend* blend_cam[4];

    // No symbol (inlined into create).
    xCamBlend* create_blend()
    {
        xCamBlend* cam = new (xMemAllocSize(sizeof(xCamBlend))) xCamBlend;
        if (cam == NULL)
        {
            return NULL;
        }

        cam->create();
        cam->owner = 'XCBL';
        cam->cfg_common.priority = 0xff;
        return cam;
    }

    // No symbol (inlined into get_blend).
    xCamBlend* grab_blend_cam()
    {
        xCamBlend** it = blend_cam;
        xCamBlend** end = it + 4;
        for (; it != end; ++it)
        {
            if ((*it)->group == NULL)
            {
                return *it;
            }
        }
        return NULL;
    }

    // No symbol and original name unknown: inlined into remove and update.
    void remove_index(S32 index)
    {
        size--;
        xCam* cam = owned[index];
        cam->group = NULL;
        cam->group_index = -1;
        owned[index] = owned[size];
        owned[index]->group_index = index;
    }

    // No symbol and original name unknown: inlined into update. Removes the owned cameras that were
    // marked for removal and are no longer in use. The same loop written in update swaps registers.
    void remove_released()
    {
        for (S32 i = 0; i < size; i++)
        {
            if (owned[i] != primary && !(owned[i]->group_flags & 0x10) &&
                (owned[i]->group_flags & 0x5))
            {
                if (owned[i]->flags & 0x1)
                {
                    owned[i]->stop();
                }

                remove_index(i);
                i--;
            }
        }
    }

    // No symbol (inlined into xCamBlend::stop).
    static void notify_released(xCam& cam)
    {
        cam.group_flags &= ~0x10;
        cam.blender = NULL;
    }

    // No GC symbol (inlined into update); a separate function in the PS2 build.
    void set_primary(S32 index)
    {
        if (primary != NULL && (primary->flags & 0x1) && !(primary->group_flags & 0x10))
        {
            primary->stop();
            mat = primary->mat;
        }

        if (index == -1)
        {
            primary = NULL;
        }
        else
        {
            primary = owned[index];
            primary->group = this;
            primary->mat = mat;
            primary->fov = fov_default;
            primary->start();
        }
    }

    S32 get_blend(S32 to_index);
    S32 get_next() const
    {
        S32 next;
        S32 min_priority;
        S32 i;
        S32 priority;

        if (primary == NULL || (primary->group_flags & 0x4))
        {
            min_priority = 0;
            next = -1;
        }
        else
        {
            next = primary->group_index;
            min_priority = primary->cfg_common.priority + 1;
        }

        for (i = 0; i < size; i++)
        {
            if (owned[i]->group_flags & 0x4)
            {
                continue;
            }

            priority = owned[i]->cfg_common.priority;
            if (priority >= min_priority)
            {
                next = i;
                min_priority = priority + 1;
            }
        }

        return next;
    }
    void remove(xCam& cam, bool force_cut);
    void add(xCam& cam, bool force_cut);
    void update(xScene& scene, F32 dt);
    void priority_force_cut();
    void reset();
    void destroy();
    void create();
};

#endif
