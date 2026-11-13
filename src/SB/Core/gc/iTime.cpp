#include "iTime.h"

#include <dolphin/os.h>

iTime sStartupTime;
static F32 sGameTime;

inline void iTimeInit()
{
    sStartupTime = OSGetTime();
}

inline void iTimeExit()
{
}

iTime iTimeGet()
{
    return OSGetTime() - sStartupTime;
}

F32 iTimeDiffSec(iTime time)
{
    return (F32)time / OS_TIMER_CLOCK;
}

F32 iTimeDiffSec(iTime time, iTime t1)
{
    time = t1 - time;
    return (F32)time / OS_TIMER_CLOCK;
}

F32 iTimeGetGame()
{
    return sGameTime;
}

void iTimeGameAdvance(F32 elapsed)
{
    sGameTime += elapsed;
}

void iTimeSetGame(F32 time)
{
    sGameTime = time;
}

void iProfileClear(U32 sceneID)
{
}

void iFuncProfileDump()
{
}

void iFuncProfileFuncs(S32, S32, F32)
{
}
