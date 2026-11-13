#ifndef XRUMBLEEMITTER_H
#define XRUMBLEEMITTER_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xMath.h"
#include "xMath3.h"
#include "iMath.h"

#include <types.h>

namespace xRumble
{
    struct effectAsset : xDynAsset
    {
        F32 time;
        F32 intensity;
        U32 id;
        U8 priority;
        U8 type;
        U8 rumbleInPause;
        U8 pad;
        F32 param1;
        F32 param2;
        F32 shakeMagnitude;
        F32 shakeCycleMax;
        F32 shakeRotationalMagnitude;
        bool shakeY;

        F32 GetIntensity(F32 currentTime, F32 totalTime) const
        {
            switch (type)
            {
            case 0:
                return 1.0f;
            case 1:
            {
                F32 pulsePeriod = param1 + param2;
                if (currentTime - pulsePeriod * (U32)(currentTime / pulsePeriod) < param1)
                {
                    return 1.0f;
                }
                else
                {
                    return 0.0f;
                }
            }
            case 2:
                return (1.0f + isin(2.0f * PI * param1 * currentTime)) / 2.0f;
            case 3:
                if (totalTime - currentTime <= param1)
                {
                    return (totalTime - currentTime) / param1;
                }
                else
                {
                    return 1.0f;
                }
            case 4:
                if (currentTime < param1)
                {
                    return currentTime / param1;
                }
                else
                {
                    return 1.0f;
                }
            default:
                return 0.0f;
            }
        }
    };

    struct sphericalEmitterAsset : xDynAsset
    {
        U32 effectID;
        F32 radius;
        xVec3 position;
        U8 onlyRumbleOnY;
        U8 fallOff;
        U8 onlyOnFloor;
    };

    // The vtable pointer sits at 0x14, after pEffectAsset (see the GC
    // sphericalEmitter/boxEmitter Init code and __vt__Q27xRumble11emitterBase).
    struct emitterBase : xBase
    {
        effectAsset* pEffectAsset;

        virtual U8 GetIntensity(const xVec3& playerPos, F32& intensity) const = 0;
        virtual void SetIntensity(F32 intensity) const
        {
            pEffectAsset->intensity = intensity;
        }
    };

    struct sphericalEmitter : emitterBase
    {
        sphericalEmitterAsset* pAsset;
        F32 radiusSqrd;

        sphericalEmitter(sphericalEmitterAsset* pAsset);

        static void Init(xBase& data, xDynAsset& asset, size_t asset_size);

        virtual U8 GetIntensity(const xVec3& playerPos, F32& intensity) const;
        virtual void UpdatePosition(const xVec3& playerPos) const;
    };

    struct boxEmitterAsset : xDynAsset
    {
        U32 effectID;
        xVec3 minCorner;
        xVec3 maxCorner;
        U8 onlyOnFloor;
    };

    struct boxEmitter : emitterBase
    {
        boxEmitterAsset* pAsset;

        boxEmitter(boxEmitterAsset* pAsset);

        static void Init(xBase& data, xDynAsset& asset, size_t asset_size);

        virtual U8 GetIntensity(const xVec3& playerPos, F32& intensity) const;
    };

    // PS2 DWARF: struct effect : emitterBase (zNMEFrogfish::rumbleMelee).
    struct effect : emitterBase
    {
        effect(effectAsset* pAsset);

        static void Init(xBase& data, xDynAsset& asset, size_t asset_size);

        virtual U8 GetIntensity(const xVec3& playerPos, F32& intensity) const
        {
            intensity = pEffectAsset->intensity;
            return 1;
        }
    };
} // namespace xRumble

#endif
