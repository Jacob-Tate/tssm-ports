#ifndef ZPARTICLESYSTEMWATERFALL_H
#define ZPARTICLESYSTEMWATERFALL_H

#include "zParticleGenerator.h"
#include "zParticleLocator.h"
#include "xParticleBatch.h"
#include "xPtankPool.h"
#include "xstransvc.h"
#include "iMath.h"

struct zParticleSystemWaterfall : zParticleSystem
{
    // Asset data following the zParticleAsset header.
    struct asset_data
    {
        U32 flags;
        F32 width_min;
        F32 width_max;
        F32 vel_min;
        F32 vel_max;
        struct
        {
            F32 yaw;
            F32 pitch;
            F32 roll;
        } vel_dir;
        F32 vel_dir_vary;
        F32 height_start_min;
        F32 height_start_max;
        F32 height_vel;
        F32 height_accel;
        F32 height_max;
        F32 intensity_min;
        F32 intensity_max;
        xColor_tag color;
        F32 gravity;
        F32 kill_yoffset;
    };

    struct particle_instance
    {
        xVec3 loc;
        F32 width;
        xColor_tag color;
        xVec3 vel;
        F32 pad1;
        F32 pad2;
        F32 gravity;
        F32 ymin;
        F32 height;
        F32 height_vel;
        F32 height_accel;
        F32 height_max;
    };

    xParticleBatchGroup batch_group;

    zParticleSystemWaterfall() : zParticleSystem(1, 17)
    {
    }

    S32 get_asset_size() const;
    void update(F32 dt);
    void scene_enter();
    zParticleGenerator* create_generator(zParticleAsset& asset);

    static S32 update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
};

struct zParticleGeneratorWaterfall : zParticleGenerator
{
    zParticleSystemWaterfall* system;
    xEnt* attach_to;
    F32 emitted; //0x24
    zParticleLocator locator;
    S32 batch_id;
    xVec3 vel_dir;

    void deactivate();
    void activate();
    void reset();
    void create(zParticleSystemWaterfall& system, zParticleAsset& asset)
    {
        xParticleBatchSystem batch;

        zParticleGenerator::create(system, asset);

        this->system = &system;

        locator.create();

        attach_to = get_attach_entity(asset);
        batch.pool_type = PGT_POS_COLOR_SIZE;
        batch.rs.texture =
            asset.texture == 0 ? NULL : (RwTexture*)xSTFindAsset(asset.texture, NULL);
        batch.rs.src_blend = rwBLENDSRCALPHA;
        batch.rs.dst_blend = rwBLENDINVSRCALPHA;
        batch.rs.flags = 4;
        batch.order_group = 'HOBO';
        batch.order_index = 1;
        batch.stride = sizeof(zParticleSystemWaterfall::particle_instance);
        batch.update = zParticleSystemWaterfall::update_particles;
        batch.context = NULL;
        batch_id = system.batch_group.create_system(batch);

        zParticleSystemWaterfall::asset_data& a =
            *(zParticleSystemWaterfall::asset_data*)(&asset + 1);
        F32 yaw = a.vel_dir.yaw;
        F32 pitch = a.vel_dir.pitch;
        // Not PS2 DWARF locals: isin/icos are called once per angle before the results are
        // combined.
        F32 sin_yaw = isin(yaw);
        F32 cos_yaw = icos(yaw);
        F32 sin_pitch = isin(pitch);
        F32 cos_pitch = icos(pitch);
        vel_dir.x = sin_yaw * cos_pitch;
        vel_dir.y = -sin_pitch;
        vel_dir.z = cos_yaw * cos_pitch;

        reset();
    }
    void update(F32 dt);
};

struct zParticleSystemWaterfallMist : zParticleSystem
{
    struct asset_data
    {
        F32 life_min;
        F32 life_max;
        F32 size_birth;
        F32 size_death;
        F32 size_vary;
        F32 vel_min;
        F32 vel_max;
        F32 vel_pitch_max;
        F32 rot_vel_min;
        F32 rot_vel_max;
        F32 intensity_min;
        F32 intensity_max;
        xColor_tag color;
    };

    struct particle_instance
    {
        F32 age;
        F32 age_rate;
        F32 x;
        F32 y;
        F32 z;
        F32 w;
        F32 dx;
        F32 dy;
        F32 dz;
        F32 dw;
        xColor_tag color;
        F32 rot;
        F32 rot_vel;
    };

    xParticleBatchGroup batch_group;

    zParticleSystemWaterfallMist() : zParticleSystem(2, 17)
    {
    }

    S32 get_asset_size() const;
    void update(F32 dt);
    void scene_enter();
    zParticleGenerator* create_generator(zParticleAsset& asset);

    static S32 update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
};

struct zParticleGeneratorWaterfallMist : zParticleGenerator
{
    zParticleSystemWaterfallMist* system;
    xEnt* attach_to;
    F32 emitted;
    zParticleLocator locator;
    S32 batch_id;

    void deactivate();
    void activate();
    void reset();
    void create(zParticleSystemWaterfallMist& system, zParticleAsset& asset)
    {
        xParticleBatchSystem batch;

        zParticleGenerator::create(system, asset);

        this->system = &system;

        locator.create();

        attach_to = get_attach_entity(asset);
        batch.pool_type = PGT_POS_COLOR_SIZE_ROT;
        batch.rs.texture =
            asset.texture == 0 ? NULL : (RwTexture*)xSTFindAsset(asset.texture, NULL);
        batch.rs.src_blend = rwBLENDSRCALPHA;
        batch.rs.dst_blend = rwBLENDINVSRCALPHA;
        batch.rs.flags = 4;
        batch.order_group = 'HOBO';
        batch.order_index = 2;
        batch.stride = sizeof(zParticleSystemWaterfallMist::particle_instance);
        batch.update = zParticleSystemWaterfallMist::update_particles;
        batch.context = NULL;
        batch_id = system.batch_group.create_system(batch);

        reset();
    }
    void update(F32 dt);
};

struct zParticleSystemWaterfallSplash : zParticleSystem
{
    struct asset_data
    {
        F32 life_min;
        F32 life_max;
        F32 size_birth;
        F32 size_death;
        F32 size_vary;
        F32 vel_min;
        F32 vel_max;
        F32 vel_pitch_min;
        F32 vel_pitch_max;
        F32 intensity_min;
        F32 intensity_max;
        xColor_tag color;
        F32 gravity;
        F32 kill_yoffset;
    };

    struct particle_instance
    {
        F32 age;
        F32 age_rate;
        F32 x;
        F32 y;
        F32 z;
        F32 w;
        F32 dx;
        F32 dy;
        F32 dz;
        F32 dw;
        F32 ay;
        F32 ymin;
        xColor_tag color;
        F32 rot;
    };

    xParticleBatchGroup batch_group;

    zParticleSystemWaterfallSplash() : zParticleSystem(3, 17)
    {
    }

    S32 get_asset_size() const;
    void update(F32 dt);
    void scene_enter();
    zParticleGenerator* create_generator(zParticleAsset& asset);

    static S32 update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
};

struct zParticleGeneratorWaterfallSplash : zParticleGenerator
{
    zParticleSystemWaterfallSplash* system;
    xEnt* attach_to;
    F32 emitted;
    zParticleLocator locator;
    S32 batch_id;

    void deactivate();
    void activate();
    void reset();
    void create(zParticleSystemWaterfallSplash& system, zParticleAsset& asset)
    {
        xParticleBatchSystem batch;

        zParticleGenerator::create(system, asset);

        this->system = &system;

        locator.create();

        attach_to = get_attach_entity(asset);
        batch.pool_type = PGT_POS_COLOR_SIZE_ROT;
        batch.rs.texture =
            asset.texture == 0 ? NULL : (RwTexture*)xSTFindAsset(asset.texture, NULL);
        batch.rs.src_blend = rwBLENDSRCALPHA;
        batch.rs.dst_blend = rwBLENDINVSRCALPHA;
        batch.rs.flags = 4;
        batch.order_group = 'HOBO';
        batch.order_index = 3;
        batch.stride = sizeof(zParticleSystemWaterfallSplash::particle_instance);
        batch.update = zParticleSystemWaterfallSplash::update_particles;
        batch.context = NULL;
        batch_id = system.batch_group.create_system(batch);

        reset();
    }
    void update(F32 dt);
};

#endif
