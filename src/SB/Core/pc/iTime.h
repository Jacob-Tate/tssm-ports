#ifndef ITIME_H
#define ITIME_H

// gc/iTime.h's interface, plus what a host needs (see iTime.cpp):
//
//   ITIME_TICKS_PER_SECOND  the rate iTimeGet() counts at
//   OS_TIMER_CLOCK          the Dolphin name for that rate. Two shared files
//                           (xFont.cpp, zGame.cpp) convert ticks to seconds
//                           with it inline instead of calling iTimeDiffSec, so
//                           it must agree with iTimeGet() or both are wrong by
//                           the ratio. On the console it is the bus clock / 4.
//   iTimeSuspend/Resume     stop the clock while the app is backgrounded
//                           (treedome; Android).

#include <types.h>

#define JANUARY 1
#define FEBRUARY 2
#define MARCH 3
#define APRIL 4
#define MAY 5
#define JUNE 6
#define JULY 7
#define AUGUST 8
#define SEPTEMBER 9
#define OCTOBER 10
#define NOVEMBER 11
#define DECEMBER 12

typedef S64 iTime;

// A microsecond tick keeps F32 precision on par with retail's 40.5 MHz one while
// staying exactly representable (treedome).
#define ITIME_TICKS_PER_SECOND 1000000

#ifndef OS_TIMER_CLOCK
#define OS_TIMER_CLOCK ITIME_TICKS_PER_SECOND
#endif

S32 iGetMinute();
S32 iGetHour();
S32 iGetDay();
S32 iGetMonth();
U32 iGetCurrFormattedDate(char* str);
U32 iGetCurrFormattedTime(char* str);
void iTimeInit();
void iTimeExit();
iTime iTimeGet();
F32 iTimeDiffSec(iTime t0, iTime t1);
F32 iTimeDiffSec(iTime time);
void iTimeSuspend();
void iTimeResume();
// The 3DS spike (iSpike.h) runs its frames back to back but wants the game's
// time step of a 30 fps display: called once per presented game-loop frame, it
// moves the clock forward so that frame counts as at least `periodNs`, as a
// frame limiter's wait would have. A frame that took longer keeps its own
// length. The clock only ever moves forward, and only here, between frames, so
// an interval measured inside one frame is still real time.
void iTimeFrameAtLeast(U64 periodNs);
// Tests (TSSM_FIXED_STEP=<fps>, iCapture.h): called once per presented
// frame, it moves the clock on by exactly 1/fps, so frame N is the same moment
// of the game on any machine at any speed. Off without the variable.
void iTimeFixedStepFrame();
void iTimeGameAdvance(F32 elapsed);
void iTimeSetGame(F32 time);
F32 iTimeGetGame();
void iProfileClear(U32 sceneID);
void iFuncProfileFuncs(S32, S32, F32);
void iFuncProfileDump();
void iFuncProfileParse(char* elfPath, S32 profile);

#endif
