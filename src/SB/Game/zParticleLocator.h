#ifndef ZPARTICLELOCATOR_H
#define ZPARTICLELOCATOR_H

#include "zParticleGenerator.h"
#include <PowerPC_EABI_Support/Runtime/MWCPlusPlusLib.h>

struct zParticleLocator
{
    // Defined in zParticleLocator.cpp
    struct activity_data;

    activity_data* activity;

    void get_center(xVec3& center) const;
    xMat4x3& get_frame() const;
    void make_locations(void* data, S32 stride, S32 elements, S32 loc_offset);
    void update(F32 dt);
    void deactivate();
    U8 activate(const zParticleAsset& asset, xEnt* attach_to);
    void restart();
    void create();
    static void scene_enter();
};

#endif
