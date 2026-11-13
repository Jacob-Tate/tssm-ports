#include "xRumbleEmitter.h"
#include "xRumble.h"
#include "xRumbleManager.h"
#include "xCollide.h"
#include "xEvent.h"
#include "xGlobals.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"
#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

namespace
{
    void emitter_EventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32);

    inline U8 inBox(const xVec3& v, const xVec3& min, const xVec3& max)
    {
        return v.x >= min.x && v.y >= min.y && v.z >= min.z && v.x <= max.x && v.y <= max.y &&
               v.z <= max.z;
    }
} // namespace

namespace xRumble
{

    inline sphericalEmitter::sphericalEmitter(sphericalEmitterAsset* pAsset)
    {
        U32 size;

        xBaseInit(this, pAsset);
        this->pAsset = pAsset;
        eventFunc = emitter_EventCB;
        pEffectAsset = (effectAsset*)xSTFindAsset(pAsset->effectID, &size);
        radiusSqrd = pAsset->radius * pAsset->radius;
    }

    inline boxEmitter::boxEmitter(boxEmitterAsset* pAsset)
    {
        U32 size;

        xBaseInit(this, pAsset);
        pEffectAsset = (effectAsset*)xSTFindAsset(pAsset->effectID, &size);
        this->pAsset = pAsset;
        eventFunc = emitter_EventCB;
    }

    inline effect::effect(effectAsset* pAsset)
    {
        xBaseInit(this, pAsset);
        pEffectAsset = pAsset;
        eventFunc = emitter_EventCB;
    }

} // namespace xRumble

namespace
{

    void emitter_EventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32)
    {
        switch (toEvent)
        {
        case eEventStartRumbleEffect:
        {
            U32 whichController = toParam[0];
            xRumble::emitterBase* pEffect = (xRumble::emitterBase*)to;
            xRumble::Manager::Get().Add(xglobals->pad[whichController], pEffect);
            break;
        }
        case eEventStopRumbleEffect:
        {
            xRumble::emitterBase* pEffect = (xRumble::emitterBase*)to;
            xRumble::Manager::Get().Stop(xglobals->pad[(U32)toParam[0]], pEffect);
            break;
        }
        }
    }
} // namespace

namespace xRumble
{

    void effect::Init(xBase& data, xDynAsset& asset, size_t asset_size)
    {
        new (&data) effect((effectAsset*)&asset);
    }

    void boxEmitter::Init(xBase& data, xDynAsset& asset, size_t asset_size)
    {
        new (&data) boxEmitter((boxEmitterAsset*)&asset);
    }

    U8 boxEmitter::GetIntensity(const xVec3& playerPos, F32& intensity) const
    {
        intensity = 0.0f;

        if (pAsset->onlyOnFloor &&
            !(xglobals->___player_ent_dont_use_directly->collis->colls[0].flags & k_HIT_IT))
        {
            return 0;
        }

        if (inBox(playerPos, pAsset->minCorner, pAsset->maxCorner))
        {
            intensity = pEffectAsset->intensity;
            return 1;
        }

        return 0;
    }

    void sphericalEmitter::Init(xBase& data, xDynAsset& asset, size_t asset_size)
    {
        new (&data) sphericalEmitter((sphericalEmitterAsset*)&asset);
    }

    U8 sphericalEmitter::GetIntensity(const xVec3& playerPos, F32& intensity) const
    {
        intensity = 0.0f;

        F32 distSqrd = xVec3Dist2(&playerPos, &pAsset->position);
        if (distSqrd > radiusSqrd)
        {
            return 0;
        }

        if (pAsset->onlyRumbleOnY && !xeq(pAsset->position.y / playerPos.y, 1.0f, 0.1f))
        {
            return 0;
        }

        if (pAsset->onlyOnFloor &&
            !(xglobals->___player_ent_dont_use_directly->collis->colls[0].flags & k_HIT_IT))
        {
            return 0;
        }

        if (pAsset->fallOff)
        {
            intensity = pEffectAsset->intensity * (1.0f - xsqrt(distSqrd) / pAsset->radius);
        }
        else
        {
            intensity = pEffectAsset->intensity;
        }

        return 1;
    }

    void sphericalEmitter::UpdatePosition(const xVec3& playerPos) const
    {
        pAsset->position = playerPos;
    }
} // namespace xRumble
