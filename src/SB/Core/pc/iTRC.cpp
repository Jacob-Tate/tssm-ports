#include "iTRC.h"

#include <stdlib.h>

// PC iTRC: TSSM's 40-function interface (gc/iTRC.cpp, 773 lines) with the
// host's answers, after treedome's pc/iTRC.cpp for BfBB.
//
// On the console this module polls the DVD drive, puts up a disc-error screen
// with the ROM font, suspends sound, movies and the async load thread while it
// is up, and watches the Reset button. There is no disc, tray or Reset button
// on a host, so:
//
//   TRCCheck, CheckDVDAndResetState  "nothing happened" (false). Retail returns
//                                    true when it showed the error screen and
//                                    the game is resuming.
//   the Set*Function hooks           kept, so a later backend can call them
//                                    (a window losing focus is the nearest
//                                    thing to an open tray), but nothing calls
//                                    them today.
//   ResetButton                      the flag is kept: the game clears it
//                                    around saves, and a window close request
//                                    deserves the same protection one day.
//   ResetSystem                      exits the process, as OSResetSystem
//                                    ends the program on the console.

S32 gWasDiskError;

namespace
{
    void (*sSndSuspend)();
    void (*sSndResume)();
    void (*sPadStopRumbling)();
    void (*sSndKill)();
    void (*sMovieSuspend)();
    void (*sMovieResume)();
    void (*sMovieResetCallBack)();
    void (*sMovieKill)();
    void (*sAsyncLoadResume)();
    void (*sAsyncLoadSuspend)();
    bool sMovieResetCallBackEnabled;
    bool sInFModLockingCode;
} // namespace

void iTRCDisk::SetPadStopRumblingFunction(void (*f)(void))
{
    sPadStopRumbling = f;
}

void iTRCDisk::SetSndSuspendFunction(void (*f)(void))
{
    sSndSuspend = f;
}

void iTRCDisk::SetSndResumeFunction(void (*f)(void))
{
    sSndResume = f;
}

void iTRCDisk::SetSndKillFunction(void (*f)(void))
{
    sSndKill = f;
}

void iTRCDisk::SetMovieSuspendFunction(void (*f)(void))
{
    sMovieSuspend = f;
}

void iTRCDisk::SetMovieResumeFunction(void (*f)(void))
{
    sMovieResume = f;
}

void iTRCDisk::SetMovieResetCallBack(void (*f)(void))
{
    sMovieResetCallBack = f;
}

void iTRCDisk::EnableMovieResetCallBack(bool enable)
{
    sMovieResetCallBackEnabled = enable;
}

void iTRCDisk::SetAsyncLoadResumeFunction(void (*f)(void))
{
    sAsyncLoadResume = f;
}

void iTRCDisk::SetAsyncLoadSuspendFunction(void (*f)(void))
{
    sAsyncLoadSuspend = f;
}

bool iTRCDisk::TRCCheck()
{
    return false;
}

void iTRCDisk::Init()
{
    gWasDiskError = 0;
    ResetButton::mResetEnabled = true;
}

void iTRCDisk::ResetMessage()
{
}

void iTRCDisk::DisplayErrorMessage()
{
}

// The DVD state machine's answer for "the drive is fine".
S32 iTRCDisk::SetDVDState()
{
    return 0;
}

void iTRCDisk::SetErrorMessage(const char*)
{
}

bool iTRCDisk::IsDiskIDed()
{
    return true;
}

bool iTRCDisk::SetInFModLockingCode(bool locking)
{
    bool was = sInFModLockingCode;
    sInFModLockingCode = locking;
    return was;
}

U8 iTRCDisk::CheckDiskErrorInHigherPriorityThread()
{
    return 0;
}

bool iTRCDisk::CheckDVDAndResetState()
{
    return false;
}

void iTRCDisk::FMODCallBack(int)
{
}

void iTRCDisk::FMODUpdateDiskError(int)
{
}

bool ResetButton::mResetEnabled = true;
BOOL ResetButton::mWasResetButtonPressed;

void ResetButton::EnableReset()
{
    mResetEnabled = true;
}

void ResetButton::DisableReset()
{
    mResetEnabled = false;
}

void ResetButton::SetSndKillFunction(void (*func)())
{
    sSndKill = func;
}

void ResetButton::SetMovieKillFunction(void (*func)(void))
{
    sMovieKill = func;
}

void ResetButton::CheckResetButton()
{
}

void ResetButton::ResetSystem()
{
    if (sSndKill != NULL)
    {
        sSndKill();
    }
    if (sMovieKill != NULL)
    {
        sMovieKill();
    }
    exit(0);
}
