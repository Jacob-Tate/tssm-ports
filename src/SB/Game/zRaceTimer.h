#ifndef ZRACETIMER_H
#define ZRACETIMER_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xFont.h"
#include "xstransvc.h"
#include "zBase.h"

enum eRaceTimerState
{
    eRaceTimerIdle,
    eRaceTimerRun,
    eRaceTimerVictory,
    eRaceTimerWarning1,
    eRaceTimerWarning2,
    eRaceTimerWarning3
};

struct zRaceTimerType : xDynAsset
{
    U8 countDown;
    S32 startTime;
    S32 victoryTime;
    F32 warnTime1;
    F32 warnTime2;
    F32 warnTime3;
};

struct zRaceTimer : xBase
{
    eRaceTimerState state;
    F32 elapsedTime;
    F32 bestTime;
    F32 startTime;
    F32 victoryTime;
    F32 totalTime;
    F32 warnTime1;
    F32 warnTime2;
    F32 warnTime3;
    U8 countDown;
    U8 paused;
    zRaceTimerType* raceTimerAsset;

    void Init(zRaceTimerType& asset)
    {
        xBaseInit(this, &asset);
        baseType = eBaseTypeRaceTimer;
        raceTimerAsset = &asset;
        eventFunc = EventHandler;

        if (linkCount)
        {
            link = (xLinkAsset*)(&asset + 1);
        }

        bestTime = (raceTimerAsset->countDown & 1) ? 0.0f : 32768.0f;
        ResetTimer();
    }

    void Reset()
    {
        xBaseReset(this, raceTimerAsset);
        ResetTimer();
    }

    void ResetTimer()
    {
        state = eRaceTimerIdle;
        startTime = (raceTimerAsset->startTime < 0) ? bestTime : raceTimerAsset->startTime;
        victoryTime = (raceTimerAsset->victoryTime < 0) ? bestTime : raceTimerAsset->victoryTime;
        countDown = raceTimerAsset->countDown & 1;
        warnTime1 = raceTimerAsset->warnTime1;
        warnTime2 = raceTimerAsset->warnTime2;
        warnTime3 = raceTimerAsset->warnTime3;
        totalTime = ((raceTimerAsset->countDown & 1) ? 1 : -1) * (startTime - victoryTime);
        elapsedTime = 0.0f;
        // k_XBASE_IS_PERSISTENT (a U16 flag in Ratatouille's xBase.h)
        baseFlags |= (U16)0x2;
        paused = 0;
    }

    void Start()
    {
        if (state != eRaceTimerIdle)
        {
            paused = 0;
        }
        else
        {
            state = eRaceTimerRun;
        }
    }

    void Resume()
    {
        paused = 0;
        if (state == eRaceTimerIdle)
        {
            xSTAssetName(id);
            Start();
        }
    }

    void SetBestTime()
    {
        if (countDown)
        {
            if (totalTime - elapsedTime > bestTime)
            {
                bestTime = totalTime - elapsedTime;
            }
        }
        else if (elapsedTime < bestTime)
        {
            bestTime = elapsedTime;
        }
    }

    void Update(F32 dt);
    static void ParseTagRaceTimer(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                                  const xtextbox::split_tag& splitTag);
    static void EventHandler(xBase* from, xBase* to, U32 event, const F32* argf,
                             xBase* toParamWidget, U32 toParamWidgetID);
    static void Load(xBase& data, xDynAsset& asset, size_t size);
};

#endif
