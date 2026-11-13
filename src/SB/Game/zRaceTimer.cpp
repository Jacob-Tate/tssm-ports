#include "zRaceTimer.h"

#include "zBase.h"
#include "zScene.h"
#include "xEvent.h"
#include "xMath.h"
#include "xString.h"
#include "xstransvc.h"

#include <stdio.h>
#include <string.h>

static const xtextbox::tag_type RaceTimerTag = { { "RaceTimer", 9 },
                                                 zRaceTimer::ParseTagRaceTimer,
                                                 NULL,
                                                 NULL };

void zRaceTimer::Load(xBase& data, xDynAsset& asset, size_t)
{
    ((zRaceTimer&)data).Init((zRaceTimerType&)asset);
}

void zRaceTimer::EventHandler(xBase* from, xBase* to, U32 event, const F32* argf,
                              xBase* toParamWidget, U32 toParamWidgetID)
{
    zRaceTimer& raceTimer = *(zRaceTimer*)to;

    switch (event)
    {
    case eEventRun:
        raceTimer.Start();
        break;
    case eEventTimerAdd:
        if (argf)
        {
            raceTimer.elapsedTime -= argf[0];
        }
        break;
    case eEventRaceTimerReset:
        raceTimer.Reset();
        break;
    case eEventStop:
        raceTimer.state = eRaceTimerIdle;
        break;
    case eEventRaceTimerSetBestTime:
        raceTimer.SetBestTime();
        break;
    case eEventRaceTimerPause:
        raceTimer.paused = 1;
        break;
    case eEventRaceTimerResume:
        raceTimer.Resume();
        break;
    case eEventExpired:
        break;
    }
}

void zRaceTimer::ParseTagRaceTimer(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                                   const xtextbox::split_tag& splitTag)
{
    char* c;
    bool bestTimeTag;
    bool victoryTimeTag;
    char* bestTime;
    char* victoryTime;
    U32 id;
    zRaceTimer* raceTimer;
    char buffer[64];
    U32 sec;
    U32 mn;

    j.reset_flags();
    j.flag.invisible = j.flag.ethereal = true;

    if (splitTag.action.size != 1 || splitTag.action.text[0] != ':' || splitTag.value.size == 0)
    {
        return;
    }

    bestTimeTag = false;
    victoryTimeTag = false;

    for (c = (char*)splitTag.value.text; c < splitTag.value.text + splitTag.value.size; c++)
    {
        if (*c == '.')
        {
            bestTime = "BestTime";
            bestTimeTag = imemcmp(c + 1, bestTime, MIN(splitTag.value.size, strlen(bestTime))) == 0;

            if (!bestTimeTag)
            {
                victoryTime = "VictoryTime";
                victoryTimeTag =
                    imemcmp(c + 1, victoryTime, MIN(splitTag.value.size, strlen(victoryTime))) == 0;
            }

            break;
        }
    }

    id = xStrHash(splitTag.value.text, c - splitTag.value.text);

    if (id)
    {
        raceTimer = (zRaceTimer*)zSceneFindObject(id);

        if (raceTimer)
        {
            j.flag.insert = j.flag.dynamic = true;

            sec = raceTimer->elapsedTime;

            if (bestTimeTag)
            {
                sec = raceTimer->bestTime;
            }
            else if (victoryTimeTag)
            {
                sec = raceTimer->totalTime;
            }
            else if (raceTimer->countDown)
            {
                sec = (raceTimer->totalTime - raceTimer->elapsedTime < 0.0f) ?
                          raceTimer->victoryTime :
                          raceTimer->totalTime - raceTimer->elapsedTime;
            }

            mn = sec / 60;

            if (mn)
            {
                sprintf(buffer, "%d:%02d", mn, sec % 60);
            }
            else
            {
                sprintf(buffer, "%02d", sec);
            }

            sprintf((char*)j.context, "%.*s", 31, buffer);
            j.context_size = 32;
        }
    }
}

void zRaceTimer::Update(F32 dt)
{
    if (paused)
    {
        return;
    }

    switch (state)
    {
    case eRaceTimerRun:
        elapsedTime += dt;
        if (countDown)
        {
            if ((elapsedTime >= warnTime1 && elapsedTime < warnTime2) ||
                (warnTime1 == 0.0f && warnTime2 == 0.0f))
            {
                state = eRaceTimerWarning1;
                zEntEvent(this, this, eEventRaceTimerWarning1);
            }
        }
        else
        {
            if ((elapsedTime >= victoryTime - warnTime1 && elapsedTime < victoryTime - warnTime2) ||
                (warnTime1 == 0.0f && warnTime2 == 0.0f))
            {
                state = eRaceTimerWarning1;
                zEntEvent(this, this, eEventRaceTimerWarning1);
            }
        }
        break;
    case eRaceTimerWarning1:
        elapsedTime += dt;
        if (countDown)
        {
            if ((elapsedTime >= warnTime2 && elapsedTime < warnTime3) ||
                (warnTime2 == 0.0f && warnTime3 == 0.0f))
            {
                state = eRaceTimerWarning2;
                zEntEvent(this, this, eEventRaceTimerWarning2);
            }
        }
        else
        {
            if ((elapsedTime >= victoryTime - warnTime2 && elapsedTime < victoryTime - warnTime3) ||
                (warnTime2 == 0.0f && warnTime3 == 0.0f))
            {
                state = eRaceTimerWarning2;
                zEntEvent(this, this, eEventRaceTimerWarning2);
            }
        }
        break;
    case eRaceTimerWarning2:
        elapsedTime += dt;
        if (countDown)
        {
            if ((elapsedTime > warnTime3 && elapsedTime < startTime) || warnTime3 == 0.0f)
            {
                state = eRaceTimerWarning3;
                zEntEvent(this, this, eEventRaceTimerWarning3);
            }
        }
        else
        {
            if ((elapsedTime > victoryTime - warnTime3 && elapsedTime < victoryTime) ||
                warnTime3 == 0.0f)
            {
                state = eRaceTimerWarning3;
                zEntEvent(this, this, eEventRaceTimerWarning3);
            }
        }
        break;
    case eRaceTimerWarning3:
        elapsedTime += dt;
        if (countDown)
        {
            if (elapsedTime >= startTime)
            {
                state = eRaceTimerVictory;
                zEntEvent(this, this, eEventExpired);
            }
        }
        else
        {
            if (elapsedTime >= victoryTime)
            {
                state = eRaceTimerVictory;
                zEntEvent(this, this, eEventExpired);
            }
        }
        break;
    case eRaceTimerIdle:
    case eRaceTimerVictory:
        break;
    }
}
