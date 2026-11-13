#ifndef ITRC_H
#define ITRC_H

// PC counterpart of gc/iTRC.h: Nintendo's Technical Requirements Checklist --
// what a GameCube game must do when the disc is unreadable, the lid opens, the
// controller is pulled or Reset is pressed. A host has none of those faults.
//
// The interface is TSSM's, minus ROMFont (the disc-error screen drawn with the
// console's ROM font through GX) and the OSThread/OSPriority bookkeeping, which
// nothing outside src/SB/Core/gc reads. Game code reaches TRC through
// iTRCDisk::TRCCheck, iTRCDisk::CheckDiskErrorInHigherPriorityThread,
// ResetButton::EnableReset/DisableReset and gWasDiskError; the setters below
// are what iSystem's TRCInit registers.

#include <types.h>

namespace iTRCDisk
{
    void SetPadStopRumblingFunction(void (*)(void));
    void SetSndSuspendFunction(void (*)(void));
    void SetSndResumeFunction(void (*)(void));
    void SetSndKillFunction(void (*)(void));
    void SetMovieSuspendFunction(void (*)(void));
    void SetMovieResumeFunction(void (*)(void));
    void SetMovieResetCallBack(void (*)(void));
    void EnableMovieResetCallBack(bool);
    void SetAsyncLoadResumeFunction(void (*)(void));
    void SetAsyncLoadSuspendFunction(void (*)(void));
    bool TRCCheck();
    void Init();
    void ResetMessage();
    void DisplayErrorMessage();
    S32 SetDVDState();
    void SetErrorMessage(const char*);
    bool IsDiskIDed();
    bool SetInFModLockingCode(bool);
    U8 CheckDiskErrorInHigherPriorityThread();
    bool CheckDVDAndResetState();
    void FMODCallBack(int status);
    void FMODUpdateDiskError(int);
} // namespace iTRCDisk

extern S32 gWasDiskError;

namespace ResetButton
{
    extern bool mResetEnabled;
    extern BOOL mWasResetButtonPressed;

    void EnableReset();
    void DisableReset();
    void SetSndKillFunction(void (*func)());
    void SetMovieKillFunction(void (*)(void));
    void CheckResetButton();
    void ResetSystem();
} // namespace ResetButton

#endif
