// xDebugTweak.h first: retail .rodata of this unit opens with its seven tweak_callback templates.
#include "xDebugTweak.h"
#include "zFlameEmitter.h"

#include "xEvent.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xRand.h"
#include "zBase.h"
#include "zFire.h"
#include "zGlobals.h"
#include "zScene.h"

// Spawn position jitter (a tweakable static in the Ratatouille build); zero in retail, so the
// multiply folds away and only the xurand() calls survive.
static const F32 rand_offset = 0.0f;

void zFlameEmitter::load(xBase& data, xDynAsset& asset, size_t)
{
    ((zFlameEmitter&)data).load((asset_type&)asset);
}

void zFlameEmitter::get_bounding_box(xBox& box) const
{
    xBox obb;
    obb.lower = obb.upper = asset->scale;
    obb.lower *= -0.5f;
    obb.upper *= 0.5f;
    xBoxInitBoundOBB(&box, &obb, &mat);
}

void zFlameEmitter::scene_enter()
{
}

void zFlameEmitter::scene_exit()
{
    zScene& scene = *globals.sceneCur;
    zFlameEmitter* it = (zFlameEmitter*)scene.baseList[eBaseTypeFlameEmitter];
    zFlameEmitter* end = it + scene.baseCount[eBaseTypeFlameEmitter];

    for (; it != end; ++it)
    {
        it->destroy();
    }
}

void zFlameEmitter::setup_all()
{
    zScene& scene = *globals.sceneCur;
    zFlameEmitter* it = (zFlameEmitter*)scene.baseList[eBaseTypeFlameEmitter];
    zFlameEmitter* end = it + scene.baseCount[eBaseTypeFlameEmitter];

    for (; it != end; ++it)
    {
        it->setup();
    }
}

void zFlameEmitter::update_all(F32 dt)
{
    zScene& scene = *globals.sceneCur;
    zFlameEmitter* it = (zFlameEmitter*)scene.baseList[eBaseTypeFlameEmitter];
    zFlameEmitter* end = it + scene.baseCount[eBaseTypeFlameEmitter];

    for (; it != end; ++it)
    {
        it->update(dt);
    }
}

void zFlameEmitter::render_all()
{
    zScene& scene = *globals.sceneCur;
    zFlameEmitter* it = (zFlameEmitter*)scene.baseList[eBaseTypeFlameEmitter];
    zFlameEmitter* end = it + scene.baseCount[eBaseTypeFlameEmitter];

    for (; it != end; ++it)
    {
        it->render();
    }
}

void zFlameEmitter::fire_reset()
{
    zScene& scene = *globals.sceneCur;
    zFlameEmitter* it = (zFlameEmitter*)scene.baseList[eBaseTypeFlameEmitter];
    zFlameEmitter* end = it + scene.baseCount[eBaseTypeFlameEmitter];

    for (; it != end; ++it)
    {
        it->dump_flames();
    }
}

void zFlameEmitter::create_flames(S32 flags)
{
    if (!zFire::_enabled)
    {
        return;
    }

    F32 radius;
    F32 width;
    S32 ux;
    S32 uz;

    if (asset->scale.x < asset->scale.z)
    {
        width = range_limit(asset->scale.x, 1.0f, 5.0f);
        ux = 1;
        radius = 0.5f * width;
        uz = (S32)(asset->scale.z / width);
        if (uz < 1)
        {
            uz = 1;
        }
    }
    else
    {
        width = range_limit(asset->scale.z, 1.0f, 5.0f);
        uz = 1;
        radius = 0.5f * width;
        ux = (S32)(asset->scale.x / width);
        if (ux < 1)
        {
            ux = 1;
        }
    }

    flames_size = ux * uz;

    if (flames_allocated >= 0)
    {
        if (flames_size > flames_allocated)
        {
            flames_size = flames_allocated;
        }
    }
    else
    {
        flames_allocated = flames_size;
        flames =
            (zFire::handle*)xMemAlloc(gActiveHeap, flames_allocated * sizeof(zFire::handle), 0);
    }

    if (flames_size <= 0)
    {
        return;
    }

    zFire::spawn_data sd = {
        (U32)(flags | 0x10c), {}, mat.up, radius, 0.0f, 0.0f, 0.0f, NULL, &props
    };
    sd.flags |= 0x80;

    if (asset->flags & 0x1)
    {
        sd.flags |= 0x1;
    }

    const xVec3& center = asset->loc;
    xVec3 dx = { mat.right.x * width, mat.right.y * width, mat.right.z * width };
    xVec3 dz = { mat.at.x * width, mat.at.y * width, mat.at.z * width };
    F32 hx = 0.5f * (ux - 1);
    F32 hz = 0.5f * (uz - 1);
    xVec3 corner = { center.x - dx.x * hx - dz.x * hz, center.y - dx.y * hx - dz.y * hz,
                     center.z - dx.z * hx - dz.z * hz };

    zFire::handle* flame = flames;
    zFire::handle* end_flame = flames + flames_allocated;

    for (U32 ix = 0; ix < ux; ++ix)
    {
        xVec3 v = corner;

        for (U32 iz = 0; iz < uz; ++iz)
        {
            sd.loc = v;
            sd.loc += mat.right * (rand_offset * xurand());
            sd.loc += mat.at * (rand_offset * xurand());

            *flame = zFire::emit(sd, NULL);

            if (flame->node)
            {
                ++flame;
                if (flame == end_flame)
                {
                    // goto: retail jumps straight out of both loops
                    goto done;
                }
            }

            v += dz;
        }

        corner += dx;
    }

done:
    flames_size = flame - flames;
}

void zFlameEmitter::cb_dispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32)
{
    zFlameEmitter& e = *(zFlameEmitter*)to;

    switch (event)
    {
    case eEventEnable:
        xBaseEnable(to);
        break;
    case eEventDisable:
        xBaseDisable(to);
        break;
    case eEventReset:
        e.reset();
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        e.hide();
        break;
    case eEventVisible:
    case eEventFastVisible:
        e.show();
        break;
    case eEventOn:
        e.spawn();
        break;
    case eEventOff:
        e.kill();
        break;
    }
}
