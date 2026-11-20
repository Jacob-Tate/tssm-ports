// PC iTime: treedome's pc/iTime.cpp (host monotonic clock, microsecond ticks,
// suspend/resume) with TSSM's additions from gc/iTime.cpp -- iTimeGetGame and
// iFuncProfileFuncs -- and the tick rate spelled OS_TIMER_CLOCK as TSSM's
// shared code spells it (see iTime.h).
#include "iTime.h"
#include "iHost.h"
#include "iWindow.h"

#include <types.h>

#include <stdlib.h>
#include <string.h>

static iTime sStartupTime;
static F32 sGameTime;

// Time spent suspended, taken off the host clock, and when the current
// suspension began.
static U64 sSuspendedNs;
static U64 sSuspendStartNs;
static bool sSuspended;

// iTimeFrameAtLeast: time added to the clock, and where the last frame ended.
static U64 sSkippedNs;
static U64 sFrameEndNs;
static bool sFrameEndSet;

// TSSM_FIXED_STEP=<fps> (iTimeFixedStepFrame): the clock at the last frame.
// Between frames it holds still, however long a frame takes (a level load can
// take seconds on a slow machine), until it has been read kStepSpinReads times:
// then something is waiting on it without presenting (the legal screen waits
// 5 s that way), and it runs with the host's from there. Reads from every
// thread count, without atomics: a lost count only delays that by a read.
static U64 sStepNs;
static bool sStepChecked;
static bool sStepOn;
static U64 sStepBaseNs;
static U32 sStepReads;
static bool sStepRunning;
static U64 sStepRunFromNs;
static const U32 kStepSpinReads = 20000;

// A monotonic clock, not the wall clock: retail reads the GameCube timebase,
// which no user action can move. Wall-clock jumps (NTP, DST, the user editing
// the clock) would otherwise show up as a single enormous frame delta. Which
// clock that is on this host is iHost's problem.
static U64 iClockNs()
{
    U64 now = sSuspended ? sSuspendStartNs : iHostMonotonicNs();
    return now - sSuspendedNs + sSkippedNs;
}

static iTime iTicksNow()
{
    U64 t = iClockNs();
    if (sStepOn)
    {
        if (!sStepRunning && ++sStepReads > kStepSpinReads)
        {
            sStepRunning = true;
            sStepRunFromNs = t;
        }
        t = sStepBaseNs + (sStepRunning ? t - sStepRunFromNs : 0);
    }
    return (iTime)(t / (1000000000ULL / ITIME_TICKS_PER_SECOND));
}

void iTimeFixedStepFrame()
{
    if (!sStepChecked)
    {
        sStepChecked = true;
        const char* e = getenv("TSSM_FIXED_STEP");
        const int fps = e != NULL ? atoi(e) : 0;
        sStepNs = fps > 0 ? 1000000000ULL / (U64)fps : 0;
    }
    if (sStepNs == 0)
    {
        return;
    }
    const U64 real = iClockNs();
    if (!sStepOn)
    {
        sStepOn = true;
        sStepBaseNs = real;
    }
    else
    {
        sStepBaseNs += sStepNs + (sStepRunning ? real - sStepRunFromNs : 0);
    }
    sStepReads = 0;
    sStepRunning = false;
}

void iTimeSuspend()
{
    if (!sSuspended)
    {
        sSuspendStartNs = iHostMonotonicNs();
        sSuspended = true;
    }
}

void iTimeResume()
{
    if (sSuspended)
    {
        sSuspendedNs += iHostMonotonicNs() - sSuspendStartNs;
        sSuspended = false;
    }
}

void iTimeFrameAtLeast(U64 periodNs)
{
    if (sSuspended)
    {
        return;
    }
    U64 now = iHostMonotonicNs() - sSuspendedNs + sSkippedNs;
    if (sFrameEndSet && now < sFrameEndNs + periodNs)
    {
        sSkippedNs += sFrameEndNs + periodNs - now;
        now = sFrameEndNs + periodNs;
    }
    sFrameEndNs = now;
    sFrameEndSet = true;
}

void iTimeInit()
{
    sStartupTime = iTicksNow();
}

void iTimeExit()
{
}

iTime iTimeGet()
{
    return iTicksNow() - sStartupTime;
}

F32 iTimeDiffSec(iTime time)
{
    return (F32)time / OS_TIMER_CLOCK;
}

F32 iTimeDiffSec(iTime t0, iTime t1)
{
    return iTimeDiffSec(t1 - t0);
}

void iTimeGameAdvance(F32 elapsed)
{
    sGameTime += elapsed;
}

void iTimeSetGame(F32 time)
{
    sGameTime = time;
}

F32 iTimeGetGame()
{
    return sGameTime;
}

F32 iTimeFrameShare(F32 dt)
{
    // TSSM_TEST_FRAME_SHARE=0: always 1, the port before the share existed, for
    // comparing the two in one build (docs/EXTRAS.md).
    static S32 sOff = -1;
    if (sOff < 0)
    {
        const char* e = getenv("TSSM_TEST_FRAME_SHARE");
        sOff = (e != NULL && e[0] == '0') ? 1 : 0;
    }
    if (sOff)
    {
        return 1.0f;
    }

    const S32 paced = iWindowGetPacedRate();
    if (paced > 0 && paced <= 60)
    {
        return 1.0f;
    }
    const F32 share = dt * 60.0f;
    return share > 0.0f && share < 1.0f ? share : 1.0f;
}

// The GameCube build reads the calendar through OSTicksToCalendarTime; on a
// host it is whatever reentrant local-time call that host has. These four live
// in iSystem.cpp on the GameCube side purely because that is where the OS calls
// were -- the declarations have always been in iTime.h, so they sit with the
// rest of the clock here.

static iHostCalendar iCalendarNow()
{
    iHostCalendar out;
    iHostLocalTime(&out);
    return out;
}

S32 iGetMinute()
{
    return iCalendarNow().min;
}

S32 iGetHour()
{
    return iCalendarNow().hour;
}

S32 iGetDay()
{
    return iCalendarNow().mday;
}

S32 iGetMonth()
{
    return iCalendarNow().mon + 1;
}

static const char* months[] = { "January ",   "February ", "March ",    "April ",
                                "May ",       "June ",     "July ",     "August ",
                                "September ", "October ",  "November ", "December " };

static const char* dotw[] = { "Sunday ",   "Monday ", "Tuesday ", "Wednesday ",
                              "Thursday ", "Friday ", "Saturday " };

U32 iGetCurrFormattedDate(char* str)
{
    char* start = str;
    iHostCalendar td = iCalendarNow();

    strcpy(str, dotw[td.wday]);
    strcat(str, months[td.mon]);
    str += strlen(str);

    S32 mday = td.mday;
    if (mday >= 10)
    {
        *str++ = (mday / 10) + '0';
    }

    *str++ = (mday % 10) + '0';
    *str++ = ',';
    *str++ = ' ';

    // Retail writes the third digit as (year / 10) % 100, which is only the
    // tens digit while year < 2100 -- 2135 would emit '=' where '3' belongs.
    // Ported as % 10 because the port has no matching obligation and every
    // caller displays this to a player.
    S32 year = td.year + 1900;
    *str++ = (year / 1000) + '0';
    *str++ = ((year / 100) % 10) + '0';
    *str++ = ((year / 10) % 10) + '0';
    *str++ = (year % 10) + '0';
    *str++ = '\0';

    return str - start;
}

U32 iGetCurrFormattedTime(char* str)
{
    char* start = str;
    S32 am = 0;
    iHostCalendar td = iCalendarNow();

    S32 hour = td.hour;
    if (hour < 12)
    {
        am = 1;
    }
    else
    {
        hour -= 12;
    }

    if (hour == 0)
    {
        hour = 12;
    }

    if (hour >= 10)
    {
        *str++ = (hour / 10) + '0';
    }

    *str++ = (hour % 10) + '0';
    *str++ = ':';
    *str++ = (td.min / 10) + '0';
    *str++ = (td.min % 10) + '0';
    *str++ = ':';
    *str++ = (td.sec / 10) + '0';
    *str++ = (td.sec % 10) + '0';
    *str++ = ' ';

    if (am)
    {
        *str++ = 'A';
        *str++ = '.';
        *str++ = 'M';
        *str++ = '.';
    }
    else
    {
        *str++ = 'P';
        *str++ = '.';
        *str++ = 'M';
        *str++ = '.';
    }

    *str++ = '\0';

    return str - start;
}

// Retail's profiler was stripped from the shipping build ("Redacted. :}").
// Nothing is missing here that the GameCube build had.
void iProfileClear(U32 sceneID)
{
}

void iFuncProfileDump()
{
}

void iFuncProfileParse(char* elfPath, S32 profile)
{
}

// TSSM's per-function profiler hook, also empty in retail (gc/iTime.cpp).
void iFuncProfileFuncs(S32, S32, F32)
{
}
