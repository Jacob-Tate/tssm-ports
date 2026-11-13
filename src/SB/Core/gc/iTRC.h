#ifndef ITRC_H
#define ITRC_H

#include <dolphin/gx/GXStruct.h>
#include <dolphin/os/OSAlloc.h>
#include <dolphin/os/OSFont.h>
#include <dolphin/types.h>
#include <dolphin/dvd.h>
#include <dolphin/os/OSThread.h>
#include <types.h>

// Copied from bfbb repo

//#include "xPad.h"

// struct _tagiTRCPadInfo {
//   _tagPadInit pad_init;
// };

namespace ROMFont
{
    extern OSFontHeader* mFontData;
    extern void* mXFBs[2];
    extern void* mCurrentFrameBuffer;
    extern GXRenderModeObj* mRenderMode;
    extern bool mFirstFrame;
    extern F32 mRomFontScale;

    void Init();
    void InitDisplay(GXRenderModeObj* mode);
    void InitGX();
    void InitVI();
    void RenderBegin();
    void RenderEnd();
    void SwapBuffers();
    void DrawCell(S32 param_1, S32 param_2, S32 param_3, S32 param_4);
    void LoadSheet(void* image_ptr);
    S32 DrawString(S32 x, S32 y, char* string);
    S32 GetWidth(char* string);
    void DrawTextBox(S32 x, S32 y, S32 width, S32 height, char* string);
    void DrawBlackScreen();
} // namespace ROMFont

// Yes, this is a namespace, not a class.
namespace iTRCDisk
{
    extern char mMessage[256];
    extern void (*mSndSuspend)();
    extern void (*mSndResume)();
    extern void (*mPadStopRumbling)();
    extern void (*mSndKill)();
    extern void (*mMovieSuspendFunction)();
    extern void (*mMovieResumeFunction)();
    extern void (*mAsyncLoadResume)();
    extern void (*mAsyncLoadSuspend)();
    extern void (*mMovieResetCallBack)();
    extern bool mInFModLockingCode;
    extern bool mInCriticalSection;
    extern bool mFModError;
    extern OSThread* mMainThread;
    extern OSPriority mMainPriority;
    extern OSThread* mFModThread;
    extern OSPriority mFModPriority;
    extern bool mDiskErrorInHigherPriorityThread;

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
    extern void (*mSndKill)();

    void EnableReset();
    void DisableReset();
    void SetSndKillFunction(void (*func)());
    void SetMovieKillFunction(void (*)(void));
    void CheckResetButton();
    void ResetSystem();

} // namespace ResetButton

#endif
