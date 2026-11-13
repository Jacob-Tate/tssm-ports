#include "zParticleLocator.h"

#include "xMath.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xModel.h"
#include "xRand.h"
#include "iMath.h"
#include "iModel.h"

extern U32 gActiveHeap;

namespace
{
    enum volume_type_enum
    {
        VOLUME_POINT,
        VOLUME_SPHERE,
        VOLUME_SPHERE_EDGE,
        VOLUME_CIRCLE,
        VOLUME_CIRCLE_EDGE,
        VOLUME_LINE,
        VOLUME_CAPSULE,
        VOLUME_CAPSULE_EDGE,
        VOLUME_CYLINDER,
        VOLUME_CYLINDER_EDGE,
        VOLUME_MODEL_VERT,
        VOLUME_MODEL_SURFACE,
        MAX_VOLUME
    };

    struct volume_data
    {
        volume_type_enum type;
        F32 radius;
        union
        {
            struct
            {
                F32 ang_delta;
            } circle;
            struct
            {
                F32 length;
                U32 cap_weight;
            } line;
            struct
            {
                S32 vertices[8];
                xModelInstance* models[8];
                S32 models_size;
                S32 total_vertices;
            } model_vert;
            struct
            {
            } model_surface;
        };
    };

    struct attach_data
    {
        struct
        {
            xMat4x3 mat;
            S32 bone;
        } entity;
    };
} // namespace

struct zParticleLocator::activity_data
{
    zParticleLocator* owner;
    const zParticleAsset* asset;
    void (*update_attach)(activity_data&, F32);
    void (*get_offset)(xVec3&, volume_data&);
    attach_data attach;
    volume_data volume;
    xEnt* attach_to;
    xMat4x3 buffer_mat;
    xMat4x3* attach_mat;
};

namespace
{
    zParticleLocator::activity_data* activities;
    S32 activities_used;

    void get_offset_model_vert_expand(xVec3& offset, volume_data& data);
    void get_offset_model_vert(xVec3& offset, volume_data& data);
    void get_offset_cylinder_edge(xVec3& offset, volume_data& data);
    void get_offset_cylinder(xVec3& offset, volume_data& data);
    void get_offset_capsule_edge(xVec3& offset, volume_data& data);
    void get_offset_capsule(xVec3& offset, volume_data& data);
    void get_offset_line(xVec3& offset, volume_data& data);
    void get_offset_circle_edge(xVec3& offset, volume_data& data);
    void get_offset_circle(xVec3& offset, volume_data& data);
    void get_offset_sphere_edge(xVec3& offset, volume_data& data);
    void get_offset_sphere(xVec3& offset, volume_data& data);
    void get_offset_point(xVec3& offset, volume_data& data);
    void update_attach_entity_bone(zParticleLocator::activity_data& activity, F32);
    void update_attach_entity(zParticleLocator::activity_data& activity, F32);
    U8 setup_volume_model(zParticleLocator::activity_data& activity, const zParticleAsset& asset);
    U8 setup_volume_line(zParticleLocator::activity_data& activity, const zParticleAsset& asset);
    U8 setup_volume_circle(zParticleLocator::activity_data& activity, const zParticleAsset& asset);
    U8 setup_volume_sphere(zParticleLocator::activity_data& activity, const zParticleAsset& asset);
    U8 setup_volume_point(zParticleLocator::activity_data& activity, const zParticleAsset&);
    U8 setup_attach_entity_tag(zParticleLocator::activity_data&, const zParticleAsset&);
    U8 setup_attach_entity(zParticleLocator::activity_data& activity, const zParticleAsset& asset);
    U8 setup_attach_fixed(zParticleLocator::activity_data& activity, const zParticleAsset& asset);

    // Takes the next unused activity from the pool.
    inline zParticleLocator::activity_data* alloc_activity()
    {
        zParticleLocator::activity_data* activity = &activities[activities_used];
        activities_used++;
        return activity;
    }

    // Returns an activity to the pool by moving the last used activity into its slot.
    inline void free_activity(zParticleLocator::activity_data* activity)
    {
        activities_used--;
        zParticleLocator::activity_data& last = activities[activities_used];

        if (activity != &last)
        {
            *activity = last;
            activity->owner->activity = activity;

            if (last.attach_mat == &last.buffer_mat)
            {
                activity->attach_mat = &activity->buffer_mat;
            }
        }
    }
} // namespace

namespace
{
    U8 setup_attach_fixed(zParticleLocator::activity_data& activity, const zParticleAsset& asset)
    {
        activity.attach_mat = &activity.buffer_mat;
        xMat3x3Euler(activity.attach_mat, asset.axis.yaw, asset.axis.pitch, asset.axis.roll);
        activity.attach_mat->pos = asset.location;
        activity.update_attach = NULL;
        return true;
    }

    void update_attach_entity(zParticleLocator::activity_data& activity, F32)
    {
        xMat4x3Mul(activity.attach_mat, &activity.attach.entity.mat,
                   (xMat4x3*)activity.attach_to->model->Mat);
    }

    void update_attach_entity_bone(zParticleLocator::activity_data& activity, F32)
    {
        xMat4x3 bone_mat;
        xModelGetBoneMat(bone_mat, *activity.attach_to->model, activity.attach.entity.bone);
        xMat4x3Mul(activity.attach_mat, &activity.attach.entity.mat, &bone_mat);
    }

    U8 setup_attach_entity(zParticleLocator::activity_data& activity, const zParticleAsset& asset)
    {
        xEnt* attach_to = activity.attach_to;

        if (attach_to == NULL || attach_to->model == NULL)
        {
            return setup_attach_fixed(activity, asset);
        }

        activity.attach_mat = (xMat4x3*)attach_to->model->Mat;

        // Bitwise test: any non-zero bit in the location offset or the axis rotation.
        if (*(const U32*)&asset.location.x | *(const U32*)&asset.location.y |
            *(const U32*)&asset.location.z | *(const U32*)&asset.axis.yaw |
            *(const U32*)&asset.axis.pitch | *(const U32*)&asset.axis.roll)
        {
            activity.attach.entity.bone =
                MIN(asset.attach.entity.bone, attach_to->model->BoneCount);

            if (activity.attach.entity.bone == 0)
            {
                activity.update_attach = update_attach_entity;
            }
            else
            {
                activity.update_attach = update_attach_entity_bone;
            }

            activity.attach_mat = &activity.buffer_mat;
            xMat3x3Euler(&activity.attach.entity.mat, asset.axis.yaw, asset.axis.pitch,
                         asset.axis.roll);
            activity.attach.entity.mat.pos = asset.location;
        }
        else
        {
            activity.update_attach = NULL;
        }

        return true;
    }

    U8 setup_attach_entity_tag(zParticleLocator::activity_data&, const zParticleAsset&)
    {
        return false;
    }

    void get_offset_point(xVec3& offset, volume_data&)
    {
        offset.x = offset.y = offset.z = 0.0f;
    }

    U8 setup_volume_point(zParticleLocator::activity_data& activity, const zParticleAsset&)
    {
        activity.get_offset = get_offset_point;
        return true;
    }

    // The get_offset_* functions scale xrand_RandomInt32() by 2^-32 directly (as get_offset_line
    // must) rather than calling xurand().
    void get_offset_sphere(xVec3& offset, volume_data& data)
    {
        F32 ang = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();
        F32 uz = 2.0f / 4294967296.0f * xrand_RandomInt32() - 1.0f;
        F32 rr = xsqrt(1.0f - uz * uz);
        // 1 - u^3 biases the distance towards the surface.
        F32 r = 2.3283064e-10f * xrand_RandomInt32();
        r = 1.0f - r * r * r;
        r *= data.radius;
        offset.assign(r * rr * icos(ang), r * rr * isin(ang), r * uz);
    }

    void get_offset_sphere_edge(xVec3& offset, volume_data& data)
    {
        F32 ang = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();
        F32 uz = 2.0f / 4294967296.0f * xrand_RandomInt32() - 1.0f;
        F32 r = data.radius * xsqrt(1.0f - uz * uz);
        offset.assign(r * icos(ang), r * isin(ang), uz * data.radius);
    }

    U8 setup_volume_sphere(zParticleLocator::activity_data& activity, const zParticleAsset& asset)
    {
        activity.get_offset =
            (asset.volume_flags & 0x1) ? get_offset_sphere_edge : get_offset_sphere;
        activity.volume.radius = asset.volume.sphere.radius;
        return true;
    }

    void get_offset_circle(xVec3& offset, volume_data& data)
    {
        F32 ang = data.circle.ang_delta * xrand_RandomInt32();
        F32 rr = data.radius * xsqrt(2.3283064e-10f * xrand_RandomInt32());
        offset.assign(rr * icos(ang), rr * isin(ang), 0.0f);
    }

    void get_offset_circle_edge(xVec3& offset, volume_data& data)
    {
        F32 ang = data.circle.ang_delta * xrand_RandomInt32();
        F32 rr = data.radius;
        offset.assign(rr * icos(ang), rr * isin(ang), 0.0f);
    }

    U8 setup_volume_circle(zParticleLocator::activity_data& activity, const zParticleAsset& asset)
    {
        activity.volume.radius = asset.volume.circle.radius;
        activity.get_offset =
            (asset.volume_flags & 0x1) ? get_offset_circle_edge : get_offset_circle;

        if (asset.volume.circle.arc_length >= 2.0f * PI - 0.001f)
        {
            activity.volume.circle.ang_delta = 2.0f * PI / 4294967296.0f;
        }
        else
        {
            activity.volume.circle.ang_delta = 2.3283064e-10f * asset.volume.circle.arc_length;
        }

        return true;
    }

    void get_offset_line(xVec3& offset, volume_data& data)
    {
        F32 d = 2.3283064e-10f * data.line.length * xrand_RandomInt32();
        offset.assign(0.0f, 0.0f, d);
    }

    void get_offset_capsule(xVec3& offset, volume_data& data)
    {
        F32 ang = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();

        if (xrand_RandomInt32() <= data.line.cap_weight)
        {
            F32 uz = 2.0f / 4294967296.0f * xrand_RandomInt32() - 1.0f;
            F32 rr = xsqrt(1.0f - uz * uz);
            // 1 - u^3 biases the distance towards the surface.
            F32 r = 2.3283064e-10f * xrand_RandomInt32();
            r = 1.0f - r * r * r;
            r *= data.radius;
            offset.assign(r * rr * icos(ang), r * rr * isin(ang), r * uz);

            if (uz > 0.0f)
            {
                offset.z += data.line.length;
            }
        }
        else
        {
            F32 d = 2.3283064e-10f * xrand_RandomInt32();
            F32 rr = data.radius * xsqrt(2.3283064e-10f * xrand_RandomInt32());
            // Same discarded isin/icos pair as get_offset_cylinder_edge.
            isin(ang);
            icos(ang);
            offset.assign(rr * isin(ang), rr * icos(ang), d * data.line.length);
        }
    }

    void get_offset_capsule_edge(xVec3& offset, volume_data& data)
    {
        F32 ang = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();

        if (xrand_RandomInt32() <= data.line.cap_weight)
        {
            F32 uz = 2.0f / 4294967296.0f * xrand_RandomInt32() - 1.0f;
            F32 rr = xsqrt(1.0f - uz * uz);
            F32 r = data.radius;
            offset.assign(r * rr * icos(ang), r * rr * isin(ang), r * uz);

            if (uz > 0.0f)
            {
                offset.z += data.line.length;
            }
        }
        else
        {
            F32 d = 2.3283064e-10f * xrand_RandomInt32();
            F32 rr = data.radius;
            // Same discarded isin/icos pair as get_offset_cylinder_edge.
            isin(ang);
            icos(ang);
            offset.assign(rr * isin(ang), rr * icos(ang), d * data.line.length);
        }
    }

    void get_offset_cylinder(xVec3& offset, volume_data& data)
    {
        F32 ang = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();
        F32 d = 2.3283064e-10f * data.line.length * xrand_RandomInt32();
        F32 rr = data.radius * xsqrt(2.3283064e-10f * xrand_RandomInt32());
        // Same discarded isin/icos pair as get_offset_cylinder_edge.
        isin(ang);
        icos(ang);
        offset.assign(rr * isin(ang), rr * icos(ang), d);
    }

    void get_offset_cylinder_edge(xVec3& offset, volume_data& data)
    {
        F32 ang = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();
        F32 d = 2.3283064e-10f * data.line.length * xrand_RandomInt32();
        F32 rr = data.radius;
        // Retail (and the PS2 build) calls isin/icos once more here and discards the results.
        isin(ang);
        icos(ang);
        offset.assign(rr * isin(ang), rr * icos(ang), d);
    }

    U8 setup_volume_line(zParticleLocator::activity_data& activity, const zParticleAsset& asset)
    {
        const volume_line_data& line = asset.volume.line;
        S32 edge_only = asset.volume_flags & 0x1;
        volume_data& ovolume = activity.volume;

        ovolume.radius = line.radius;
        ovolume.line.length = line.length;

        if (line.radius <= 0.0f)
        {
            activity.get_offset = get_offset_line;
        }
        else if (line.flags & 0x1)
        {
            activity.get_offset = edge_only ? get_offset_cylinder_edge : get_offset_cylinder;
        }
        else
        {
            activity.get_offset = edge_only ? get_offset_capsule_edge : get_offset_capsule;

            F32 r = line.radius;
            F32 d = line.length;
            F32 rr = r * (edge_only ? 2.0f : 4.0f / 3.0f);
            ovolume.line.cap_weight = 4294967296.0f * (rr / (d + rr));
        }

        return true;
    }

    void get_offset_model_vert(xVec3& offset, volume_data& data)
    {
        S32 vert_index = xrand_RandomInt32() % data.model_vert.total_vertices;
        xModelInstance** models = data.model_vert.models;
        S32* vertices = data.model_vert.vertices;

        while (true)
        {
            if (vert_index < *vertices)
            {
                break;
            }

            vert_index -= *vertices;
            models++;
            vertices++;
        }

        iModelVertEval((*models)->Data, vert_index, 1, (*models)->Mat, NULL, &offset);
    }

    void get_offset_model_vert_expand(xVec3&, volume_data&)
    {
    }

    U8 setup_volume_model(zParticleLocator::activity_data& activity, const zParticleAsset& asset)
    {
        xEnt* attach_to = activity.attach_to;
        const volume_model_data& model = asset.volume.model;

        if (model.flags & 0x1)
        {
            return false;
        }

        activity.get_offset =
            model.expand <= 0.0f ? get_offset_model_vert : get_offset_model_vert_expand;

        if (attach_to == NULL)
        {
            return false;
        }

        S32 model_index = 0;
        S32 models_size = 0;
        S32 total_vertices = 0;
        xModelInstance* m = attach_to->model;

        while (m != NULL)
        {
            if (!(model.exclude & (1 << model_index)))
            {
                S32 size = m->Data->geometry->numVertices;
                activity.volume.model_vert.models[models_size] = m;
                activity.volume.model_vert.vertices[models_size] = size;
                models_size++;
                total_vertices += size;
            }

            m = m->Next;
            model_index++;
        }

        if (total_vertices <= 0)
        {
            return false;
        }

        activity.volume.model_vert.total_vertices = total_vertices;
        activity.volume.model_vert.models_size = models_size;
        return true;
    }
} // namespace

void zParticleLocator::scene_enter()
{
    SPtr tmpBlock;

    tmpBlock = (SPtr)xMemAlloc(gActiveHeap, 0x8810, 0);
    activities = (activity_data*)__construct_new_array((void*)tmpBlock, 0x0, 0x0, 0x110, 0x80);
    activities_used = 0;
}

void zParticleLocator::create()
{
    activity = 0;
}

void zParticleLocator::restart()
{
}

U8 zParticleLocator::activate(const zParticleAsset& asset, xEnt* attach_to)
{
    static U8 (*const setup_attach_table[3])(activity_data&, const zParticleAsset&) = {
        setup_attach_fixed,
        setup_attach_entity,
        setup_attach_entity_tag,
    };
    static U8 (*const setup_volume_table[5])(activity_data&, const zParticleAsset&) = {
        setup_volume_point, setup_volume_sphere, setup_volume_circle,
        setup_volume_line,  setup_volume_model,
    };

    if (activity != NULL)
    {
        return true;
    }

    if (activities_used >= 128)
    {
        return false;
    }

    activity = alloc_activity();
    activity->owner = this;
    activity->asset = &asset;
    activity->attach_to = attach_to;

    if (setup_attach_table[asset.attach_type](*activity, asset) &&
        setup_volume_table[asset.volume_type](*activity, asset))
    {
        return true;
    }

    free_activity(activity);
    activity = NULL;
    return false;
}

void zParticleLocator::deactivate()
{
    if (activity == NULL)
    {
        return;
    }

    free_activity(activity);
    activity = NULL;
}

void zParticleLocator::update(F32 dt)
{
    if (activity->update_attach != NULL)
    {
        activity->update_attach(*activity, dt);
    }
}

void zParticleLocator::make_locations(void* data, S32 stride, S32 elements, S32 loc_offset)
{
    xMat4x3& amat = get_frame();
    void (*get_offset)(xVec3&, volume_data&) = activity->get_offset;
    volume_data& volume = activity->volume;
    U8* p = (U8*)data;
    U8* endp = p + stride * elements;

    while (p != endp)
    {
        xVec3& loc = *(xVec3*)(p + loc_offset);
        xVec3 offset;
        get_offset(offset, volume);
        xMat4x3Toworld(&loc, &amat, &offset);
        p += stride;
    }
}

xMat4x3& zParticleLocator::get_frame() const
{
    return *activity->attach_mat;
}

void zParticleLocator::get_center(xVec3& center) const
{
    xMat4x3& mat = *activity->attach_mat;

    // Asset volume types index setup_volume_table: point, sphere, circle, line, model.
    switch (activity->asset->volume_type)
    {
    case 3:
    {
        F32 hlen = 0.5f * activity->volume.line.length;
        center.x = mat.at.x * hlen + mat.pos.x;
        center.y = mat.at.y * hlen + mat.pos.y;
        center.z = mat.at.z * hlen + mat.pos.z;
        break;
    }
    case 4:
        center = activity->attach_to == NULL ? mat.pos : *xEntGetCenter(activity->attach_to);
        break;
    case 0:
    case 1:
    case 2:
    default:
        center = mat.pos;
        break;
    }
}
