#ifndef XFUNCTIONGENERATOR_H
#define XFUNCTIONGENERATOR_H

#include "xBase.h"
#include "xDynAsset.h"

struct xFunctionGeneratorAsset : xDynAsset
{
    F32 startCycleWidth; // 0x10
    F32 middleCycleWidth; // 0x14
    F32 endCycleWidth; // 0x18
    F32 startPulseWidth; // 0x1C
    F32 middlePulseWidth; // 0x20
    F32 endPulseWidth; // 0x24
    F32 middleTime; // 0x28
    F32 endTime; // 0x2C
    U8 middleEnabled; // 0x30
    U8 endEnabled; // 0x31
};

struct xFunctionGenerator : xBase
{
    xFunctionGeneratorAsset* asset; // 0x10
    U8 started; // 0x14
    U8 currentState; // 0x15
    U8 override; // 0x16
    F32 currentCycleWidth; // 0x18
    F32 currentPulseWidth; // 0x1C
    F32 currentTime; // 0x20
    F32 lastUpTime; // 0x24
    F32 lastDownTime; // 0x28

    xFunctionGenerator(xFunctionGeneratorAsset* asset);

    void Update(F32 dt);
    void HandleEvent(xBase*, U32 toEvent, const F32* toParam, xBase*, U32);

    void Start()
    {
        currentCycleWidth = asset->startCycleWidth;
        currentPulseWidth = asset->startPulseWidth;
        currentTime = 0.0f;
        lastUpTime = 0.0f;
        lastDownTime = 0.0f;
        currentState = false;
        started = true;
        override = false;
    }

    void Stop()
    {
        started = false;
    }

    void Reset()
    {
        started = false;
    }

    U8 IsEnabled() const
    {
        return xBaseIsEnabled(this) && started;
    }
};

void xFunctionGenerator_Init(xBase& data, xDynAsset& asset, size_t);

#endif
