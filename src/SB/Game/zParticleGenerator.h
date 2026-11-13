#ifndef ZPARTICLEGENERATOR_H
#define ZPARTICLEGENERATOR_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xEnt.h"

struct _class_0
{
    F32 yaw;
    F32 pitch;
    F32 roll;
};

struct attach_fixed_data
{
};

struct attach_entity_data
{
    U32 entity;
    U8 bone;
    U8 pad1;
    U8 pad2;
    U8 pad3;
};

struct attach_entity_tag_data
{
    U32 entity;
    xVec3 tag;
};

struct motion_none_data
{
};

struct motion_spiral_data
{
    U8 flags;
    U8 points;
    U8 pad1;
    U8 pad2;
    F32 radius_inner;
    F32 radius_outer;
    F32 duration;
    F32 frequency;
};

struct volume_point_data
{
};

struct volume_sphere_data
{
    F32 radius;
};

struct volume_circle_data
{
    F32 radius;
    F32 arc_length;
};

struct volume_line_data
{
    U8 flags;
    U8 pad1;
    U8 pad2;
    U8 pad3;
    F32 radius;
    F32 length;
};

struct volume_model_data
{
    U8 flags;
    U8 exclude;
    U8 pad1;
    U8 pad2;
    F32 expand;
};

struct zParticleAsset : xDynAsset
{
    struct attach_data
    {
        union
        {
            attach_fixed_data fixed;
            attach_entity_data entity;
            attach_entity_tag_data entity_tag;
        };
    };

    struct motion_data
    {
        union
        {
            motion_none_data none;
            motion_spiral_data spiral;
        };
    };

    struct volume_data
    {
        union
        {
            volume_point_data point;
            volume_sphere_data sphere;
            volume_circle_data circle;
            volume_line_data line;
            volume_model_data model;
        };
    };

    U8 flags;
    U8 attach_flags;
    U8 motion_flags;
    U8 volume_flags;
    F32 rate;
    U32 texture;
    U8 attach_type;
    U8 motion_type;
    U8 volume_type;
    U8 system_type;
    xVec3 location;
    _class_0 axis;
    attach_data attach;
    motion_data motion;
    volume_data volume;
};

struct zParticleSystem;

struct zParticleGenerator : xBase
{
    zParticleAsset* asset;
    S32 flags;

    virtual void reset();
    virtual void activate();
    virtual void deactivate();
    void create(zParticleSystem& system, zParticleAsset& asset);
    static void perturb_dir(xVec3& dir, F32 angle);
    static xEnt* get_attach_entity(const zParticleAsset& asset);
    static void event_handler(xBase*, xBase* to, U32 event, const F32*, xBase*, U32);
};

struct relative_ordering
{
    S32 other;
    U8 before_other;
};

struct zParticleSystem
{
    S32 type;
    S32 need;
    zParticleGenerator** generators;
    S32 generators_size;
    S32 generators_active;

    zParticleSystem(S32 type, S32 need)
    {
        this->type = type;
        this->need = need;
    }

    virtual zParticleGenerator* create_generator(zParticleAsset& asset) = 0;
    virtual void destroy_generator(zParticleGenerator* gen)
    {
    }
    virtual void scene_enter()
    {
    }
    virtual void scene_exit()
    {
    }
    virtual void setup()
    {
    }
    virtual void reset()
    {
    }
    virtual void update(F32 dt)
    {
    }
    virtual void pre_render()
    {
    }
    virtual void render()
    {
    }
    virtual relative_ordering* get_ordering(S32& size, S32)
    {
        size = 0;
        return NULL;
    }
    virtual S32 get_asset_size() const = 0;
};

void zParticleSceneEnter();
void zParticleSceneExit();
void zParticleSetup();
void zParticleReset();
void zParticleUpdate(F32 dt);
void zParticlePreRender();
void zParticleRender();

#endif
