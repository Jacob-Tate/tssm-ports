#include "zParticleSystems.h"
#include "zParticleSystemWaterfall.h"
#include "xMemMgr.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

struct zParticleSystemDummy : zParticleSystem
{
    zParticleSystemDummy() : zParticleSystem(0, 0)
    {
    }

    zParticleGenerator* create_generator(zParticleAsset& asset)
    {
        return NULL;
    }
    S32 get_asset_size() const
    {
        return 0;
    }
};

zParticleSystem** zParticleSystemCreateAll()
{
    zParticleSystem** systems =
        new (xMemAlloc(gActiveHeap, 4 * sizeof(zParticleSystem*), 0)) zParticleSystem*[4];

    systems[0] = new (xMemAlloc(gActiveHeap, sizeof(zParticleSystemDummy), 0)) zParticleSystemDummy;
    systems[1] =
        new (xMemAlloc(gActiveHeap, sizeof(zParticleSystemWaterfall), 0)) zParticleSystemWaterfall;
    systems[2] = new (xMemAlloc(gActiveHeap, sizeof(zParticleSystemWaterfallMist), 0))
        zParticleSystemWaterfallMist;
    systems[3] = new (xMemAlloc(gActiveHeap, sizeof(zParticleSystemWaterfallSplash), 0))
        zParticleSystemWaterfallSplash;

    return systems;
}
