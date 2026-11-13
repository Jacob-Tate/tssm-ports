#include "zParticleGenerator.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>

#include "xEvent.h"
#include "xMath3.h"
#include "xMemMgr.h"
#include "xRand.h"
#include "xString.h"
#include "xstransvc.h"

#include "zBase.h"
#include "zParticleLocator.h"
#include "zParticleSystems.h"

void zSceneObjHashtableAdd(U32 id, xBase* base);

namespace
{
    zParticleSystem** system_handler_set[7];
    S32 system_handler_set_size[7];
    S32 max_system_asset_size;
    zParticleSystem** systems;
    zParticleGenerator** generators;
    S32 max_generators;
    S32 generators_used;

    S32 compare_generator_systems_function(const void* e1, const void* e2);
    void create_handler_sets();
} // namespace

namespace
{
    inline void create_generators()
    {
        U32 type = xStrHash("Effect:particle_generator");
        S32 asses = xSTAssetCountByType('DYNA');
        S32 total = 0;

        for (S32 i = 0; i < asses; ++i)
        {
            U32 asset_size;
            zParticleAsset* asset = (zParticleAsset*)xSTFindAssetByType('DYNA', i, &asset_size);
            if (asset->type == type)
            {
                ++total;
            }
        }

        max_generators = total + 128;
        generators = new (gActiveHeap) zParticleGenerator*[max_generators];

        zParticleGenerator** gen = generators;

        for (S32 i = 0; i < asses; ++i)
        {
            zParticleAsset* asset = (zParticleAsset*)xSTFindAssetByType('DYNA', i, NULL);
            if (asset->type == type)
            {
                *gen = systems[asset->system_type]->create_generator(*asset);
                if (*gen != NULL)
                {
                    ++gen;
                }
            }
        }

        generators_used = gen - generators;
    }

    inline void sort_generators()
    {
        qsort(generators, generators_used, sizeof(zParticleGenerator*),
              compare_generator_systems_function);

        zParticleSystem** sys = systems;
        zParticleSystem** end_sys = sys + 4;
        zParticleGenerator** first_gen = generators;
        zParticleGenerator** last_gen = first_gen + generators_used;

        for (; sys != end_sys; ++sys)
        {
            if (first_gen == last_gen)
            {
                break;
            }

            S32 system_type = sys - systems;
            zParticleGenerator** found_gen;

            for (found_gen = first_gen; found_gen < last_gen; ++found_gen)
            {
                if (system_type != (*found_gen)->asset->system_type)
                {
                    break;
                }
            }

            (*sys)->generators = first_gen;
            (*sys)->generators_size = found_gen - first_gen;
            first_gen = found_gen;

            if (found_gen == last_gen)
            {
                break;
            }
        }
    }
} // namespace

namespace
{
    void create_handler_sets()
    {
        S32 total_size = 0;

        for (S32 handler = 0; handler < 7; ++handler)
        {
            S32 size = 0;
            zParticleSystem** sys = systems;
            zParticleSystem** end_sys = sys + 4;

            for (; sys != end_sys; ++sys)
            {
                if ((*sys)->need & (1 << handler))
                {
                    ++size;
                }
            }

            system_handler_set_size[handler] = size;
            total_size += size;
        }

        if (total_size <= 0)
        {
            return;
        }

        zParticleSystem** set =
            (zParticleSystem**)xMemAlloc(gActiveHeap, total_size * sizeof(zParticleSystem*), 0);

        for (S32 handler = 0; handler < 7; ++handler)
        {
            system_handler_set[handler] = set;

            zParticleSystem** sys = systems;
            zParticleSystem** end_sys = sys + 4;

            for (; sys != end_sys; ++sys)
            {
                if ((*sys)->need & (1 << handler))
                {
                    *set = *sys;
                    ++set;
                }
            }
        }
    }

    S32 compare_generator_systems_function(const void* e1, const void* e2)
    {
        zParticleGenerator* g1 = *(zParticleGenerator**)e1;
        zParticleGenerator* g2 = *(zParticleGenerator**)e2;

        if (g1->asset->system_type != g2->asset->system_type)
        {
            return g1->asset->system_type - g2->asset->system_type;
        }

        return (S32)(SPtr)g1 - (S32)(SPtr)g2;
    }
} // namespace

void zParticleSceneEnter()
{
    zParticleLocator::scene_enter();

    systems = zParticleSystemCreateAll();
    max_system_asset_size = 0;

    zParticleSystem** sys = systems;
    zParticleSystem** end_sys = sys + 4;

    for (; sys != end_sys; ++sys)
    {
        (*sys)->generators = NULL;
        (*sys)->generators_size = 0;

        S32 size = (*sys)->get_asset_size();
        if (max_system_asset_size < size)
        {
            max_system_asset_size = size;
        }
    }

    create_handler_sets();

    zParticleSystem** set = system_handler_set[0];
    zParticleSystem** end_set = set + system_handler_set_size[0];

    for (; set != end_set; ++set)
    {
        (*set)->scene_enter();
    }

    create_generators();
    sort_generators();
}

void zParticleSceneExit()
{
    zParticleGenerator** gen = generators;
    zParticleGenerator** end_gen = gen + generators_used;

    for (; gen != end_gen; ++gen)
    {
        systems[(*gen)->asset->system_type]->destroy_generator(*gen);
    }

    zParticleSystem** set = system_handler_set[1];
    zParticleSystem** end_set = set + system_handler_set_size[1];

    for (; set != end_set; ++set)
    {
        (*set)->scene_exit();
    }
}

void zParticleSetup()
{
    zParticleSystem** set = system_handler_set[2];
    zParticleSystem** end_set = set + system_handler_set_size[2];

    for (; set != end_set; ++set)
    {
        (*set)->setup();
    }
}

void zParticleReset()
{
    zParticleSystem** set = system_handler_set[3];
    zParticleSystem** end_set = set + system_handler_set_size[3];

    for (; set != end_set; ++set)
    {
        (*set)->reset();
    }
}

void zParticleUpdate(F32 dt)
{
    zParticleSystem** set = system_handler_set[4];
    zParticleSystem** end_set = set + system_handler_set_size[4];

    for (; set != end_set; ++set)
    {
        (*set)->update(dt);
    }
}

void zParticlePreRender()
{
    zParticleSystem** set = system_handler_set[5];
    zParticleSystem** end_set = set + system_handler_set_size[5];

    for (; set != end_set; ++set)
    {
        (*set)->pre_render();
    }
}

void zParticleRender()
{
    zParticleSystem** set = system_handler_set[6];
    zParticleSystem** end_set = set + system_handler_set_size[6];

    for (; set != end_set; ++set)
    {
        (*set)->render();
    }
}

void zParticleGenerator::create(zParticleSystem& system, zParticleAsset& asset)
{
    xBaseInit(this, &asset);
    this->asset = &asset;
    baseType = eBaseTypeParticleGenerator;
    flags = 0;
    eventFunc = event_handler;

    if (linkCount != 0)
    {
        link = (xLinkAsset*)((U8*)&asset + system.get_asset_size());
    }

    zSceneObjHashtableAdd(id, this);
}

void zParticleGenerator::reset()
{
    if (asset->flags & 0x1)
    {
        flags |= 0x1;
    }
    else
    {
        flags &= ~0x1;
    }
}

void zParticleGenerator::activate()
{
    flags |= 0x1;
}

void zParticleGenerator::deactivate()
{
    flags &= ~0x1;
}

void zParticleGenerator::event_handler(xBase*, xBase* to, U32 event, const F32*, xBase*, U32)
{
    zParticleGenerator& gen = *(zParticleGenerator*)to;

    switch (event)
    {
    case eEventReset:
        gen.reset();
        break;
    case eEventOn:
        gen.activate();
        break;
    case eEventOff:
        gen.deactivate();
        break;
    }
}

xEnt* zParticleGenerator::get_attach_entity(const zParticleAsset& asset)
{
    U32 id;

    switch (asset.attach_type)
    {
    case 1:
        id = asset.attach.entity.entity;
        break;
    case 2:
        id = asset.attach.entity_tag.entity;
        break;
    case 0: // fixed
    default:
        return NULL;
    }

    xEnt* ent = (xEnt*)xSTFindAsset(id, NULL);
    if (ent == NULL)
    {
        return NULL;
    }

    if (!(ent->baseFlags & 0x20))
    {
        return NULL;
    }

    if (ent->model == NULL)
    {
        return NULL;
    }

    return ent;
}

void zParticleGenerator::perturb_dir(xVec3& dir, F32 angle)
{
    F32 rand_scale = 2.3283064e-10f * angle;
    F32 ang_min = 0.5f * angle;
    xVec3 ang = { rand_scale * xrand_RandomInt32() - ang_min,
                  rand_scale * xrand_RandomInt32() - ang_min,
                  rand_scale * xrand_RandomInt32() - ang_min };
    xMat3x3 mat;
    xMat3x3Euler(&mat, &ang);
    xMat3x3LMulVec(&dir, &mat, &dir);
}
