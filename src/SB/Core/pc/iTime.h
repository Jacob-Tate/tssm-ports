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
void iTimeGameAdvance(F32 elapsed);
void iTimeSetGame(F32 time);
F32 iTimeGetGame();

// PC-only: the share of a 60 fps frame that a game frame of `dt` seconds is, for
// game code that adds a fixed amount once a frame (or once a 1/60 s step)
// without a time factor. Neither console ever ran it above 60, so above 60 such
// code went faster than on either. dt * 60 while the pacer runs above 60 (or
// uncapped) and dt is shorter than 1/60 s; exactly 1 otherwise, so at a cap of
// 60 or below, frame time jitter included, the code is bit-identical.
// TSSM_TEST_FRAME_SHARE=0 makes it always 1 (the port before it), for checks
// that compare the two in one build.
F32 iTimeFrameShare(F32 dt);
void iProfileClear(U32 sceneID);
void iFuncProfileFuncs(S32, S32, F32);
void iFuncProfileDump();
void iFuncProfileParse(char* elfPath, S32 profile);

#endif
