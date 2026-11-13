#include "iTRC.h"

#include "iSystem.h"
#include "xDebug.h"
#include "xPad.h"

#include <dolphin/gx/GXCull.h>
#include <dolphin/gx/GXFrameBuffer.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXManage.h>
#include <dolphin/gx/GXPixel.h>
#include <dolphin/gx/GXTexture.h>
#include <dolphin/gx/GXTransform.h>
#include <dolphin/gx/GXVert.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <dolphin/vi/vifuncs.h>
#include <fmod/fmod.h>
#include <string.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/strtoul.h>
#include <types.h>

// FIXME: These should be in a RW header somewhere
extern GXRenderModeObj* _RwDlRenderMode;
extern "C" {
void RwGameCubeGetXFBs(void*, void*);
}

#define FONT_MATRIX_ID 0x1E
#define TEXTBOX_MAX_TEXT_LENGTH 256

namespace ROMFont
{
    OSFontHeader* mFontData;
    void* mXFBs[2];
    void* mCurrentFrameBuffer;
    GXRenderModeObj* mRenderMode;
    bool mFirstFrame = true;
    F32 mRomFontScale = 1.0f;
} // namespace ROMFont

namespace ResetButton
{
    bool mResetEnabled = 1;
    BOOL mWasResetButtonPressed;
    bool mDiscIded; // Return type may be wrong
    void (*mSndKill)();
    void (*mMovieKillFunction)(); // Return type may be wrong
} // namespace ResetButton

namespace iTRCDisk
{
    char mMessage[256];

    void (*mSndSuspend)();
    void (*mSndResume)();
    void (*mAsyncLoadSuspend)();
    void (*mAsyncLoadResume)();
    void (*mPadStopRumbling)();
    void (*mMovieSuspendFunction)();
    void (*mMovieResumeFunction)();
    void (*mMovieResetCallBack)();
} // namespace iTRCDisk

S32 gWasDiskError;

namespace iTRCDisk
{
    bool mInCriticalSection;
    bool mFModError;
    bool mInFModLockingCode;
    OSThread* mMainThread;
    OSPriority mMainPriority;
    OSThread* mFModThread;
    OSPriority mFModPriority;
    bool mDiskErrorInHigherPriorityThread;
} // namespace iTRCDisk

inline void ROMFont::Init()
{
    if (OSGetFontEncode() == 1)
    {
        mFontData = (OSFontHeader*)OSAllocFromHeap(__OSCurrHeap, 0x120F00);
    }
    else
    {
        mFontData = (OSFontHeader*)OSAllocFromHeap(__OSCurrHeap, 0x20120);
    }

    OSInitFont(mFontData);
}

inline void ROMFont::InitDisplay(GXRenderModeObj* mode)
{
    GXColor Bkcolor = { 0 };
    Mtx44 projection;
    Mtx modelview;

    mRenderMode = mode;
    RwGameCubeGetXFBs(&mXFBs[0], &mXFBs[1]);
    mCurrentFrameBuffer = mXFBs[1];

    InitGX();
    InitVI();

    GXSetCopyClear(Bkcolor, GX_MAX_Z24);
    C_MTXOrtho(projection, 0.0f, mRenderMode->efbHeight, 0.0f, mRenderMode->fbWidth, 0.0f, -100.0f);
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    PSMTXIdentity(modelview);
    GXLoadPosMtxImm(modelview, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);
}

inline void ROMFont::InitGX()
{
    U16 xfbHeight;
    F32 yScale;

    GXSetViewport(0.0f, 0.0f, mRenderMode->fbWidth, mRenderMode->efbHeight, 0.0f, 1.0f);
    GXSetScissor(0, 0, mRenderMode->fbWidth, mRenderMode->efbHeight);
    yScale = GXGetYScaleFactor(mRenderMode->efbHeight, mRenderMode->xfbHeight);
    xfbHeight = GXSetDispCopyYScale(yScale);
    GXSetDispCopySrc(0, 0, mRenderMode->fbWidth, mRenderMode->efbHeight);
    GXSetDispCopyDst(mRenderMode->fbWidth, xfbHeight);
    GXSetCopyFilter(mRenderMode->aa, mRenderMode->sample_pattern, GX_TRUE, mRenderMode->vfilter);

    if (mRenderMode->aa != FALSE)
    {
        GXSetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    }
    else
    {
        GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
    }

    GXCopyDisp(mCurrentFrameBuffer, GX_TRUE);
    GXSetDispCopyGamma(GX_GM_1_0);
    GXSetCullMode(GX_CULL_NONE);
}

inline void ROMFont::InitVI()
{
    U32 nin;

    VISetNextFrameBuffer(mXFBs[0]);
    mCurrentFrameBuffer = mXFBs[1];
    VIFlush();
    VIWaitForRetrace();

    nin = mRenderMode->viTVmode & 1;
    if (nin)
    {
        VIWaitForRetrace();
    }
}

inline void ROMFont::RenderBegin()
{
    if (mRenderMode->field_rendering != FALSE)
    {
        GXSetViewportJitter(0.0f, 0.0f, mRenderMode->fbWidth, mRenderMode->efbHeight, 0.0f, 1.0f,
                            VIGetNextField());
    }
    else
    {
        GXSetViewport(0.0f, 0.0f, mRenderMode->fbWidth, mRenderMode->efbHeight, 0.0f, 1.0f);
    }

    GXInvalidateVtxCache();
    GXInvalidateTexAll();
}

inline void ROMFont::SwapBuffers()
{
    VISetNextFrameBuffer(mCurrentFrameBuffer);

    if (mFirstFrame != false)
    {
        VISetBlack(TRUE);
        mFirstFrame = false;
    }
    else
    {
        VISetBlack(FALSE);
    }

    VIFlush();
    VIWaitForRetrace();

    mCurrentFrameBuffer = mCurrentFrameBuffer == mXFBs[0] ? mXFBs[1] : mXFBs[0];
}

inline void ROMFont::RenderEnd()
{
    GXSetZMode(TRUE, GX_LEQUAL, TRUE);
    GXSetColorUpdate(TRUE);
    GXCopyDisp(mCurrentFrameBuffer, TRUE);
    GXDrawDone();
    SwapBuffers();
}

// The corners read the cell size from the font header, as Ratatouille's debug build does; the
// texture coordinates use the locals. Locals for the corners too swap registers in DrawTextBox.
inline void ROMFont::DrawCell(S32 x, S32 y, S32 u, S32 v)
{
    u16 cellWidth;
    u16 cellHeight;

    s16 tex_bottom;
    s16 tex_right;
    s16 tex_left;
    s16 tex_top;

    s16 left;
    s16 right;
    s16 bottom;
    s16 top;

    left = x;
    bottom = y;
    cellWidth = mFontData->cellWidth;
    cellHeight = mFontData->cellHeight;
    right = left + mFontData->cellWidth;
    top = bottom + mFontData->cellHeight;

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);

    GXPosition3s16(left, bottom, 0);
    tex_left = u;
    tex_bottom = v;
    GXTexCoord2s16(tex_left, tex_bottom);

    GXPosition3s16(right, bottom, 0);
    tex_right = u + cellWidth;
    GXTexCoord2s16(tex_right, tex_bottom);

    GXPosition3s16(right, top, 0);
    tex_top = v + cellHeight;
    GXTexCoord2s16(tex_right, tex_top);

    GXPosition3s16(left, top, 0);
    GXTexCoord2s16(tex_left, tex_top);

    GXEnd();
}

inline void ROMFont::LoadSheet(void* image_ptr)
{
    GXAnisotropy max_aniso;
    GXTexObj texObj;
    Mtx mtx;
    U32 sheetWidth;
    U32 sheetHeight;

    max_aniso = GX_ANISO_1;
    GXInitTexObj(&texObj, image_ptr, mFontData->sheetWidth, mFontData->sheetHeight,
                 (GXTexFmt)mFontData->sheetFormat, GX_CLAMP, GX_CLAMP, FALSE);
    GXInitTexObjLOD(&texObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, FALSE, FALSE, max_aniso);
    GXLoadTexObj(&texObj, GX_TEXMAP0);
    sheetWidth = mFontData->sheetWidth;
    sheetHeight = mFontData->sheetHeight;
    PSMTXScale(mtx, 1.0f / sheetWidth, 1.0f / sheetHeight, 1.0f);
    GXLoadTexMtxImm(mtx, FONT_MATRIX_ID, GX_MTX2x4);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, FONT_MATRIX_ID);
}

inline S32 ROMFont::DrawString(S32 x, S32 y, char* string)
{
    S32 cursor;
    void* image[1];
    S32 u;
    S32 v;
    S32 width;

    cursor = 0;
    while (*string != '\0')
    {
        string = OSGetFontTexture(string, image, (s32*)&u, (s32*)&v, (s32*)&width);
        width = width * mRomFontScale;
        LoadSheet(image[0]);
        DrawCell(x + cursor, y, u, v);
        cursor += width;
    }
    return cursor;
}

inline S32 ROMFont::GetWidth(char* string)
{
    S32 total;
    void* image[1];
    S32 u;
    S32 v;
    S32 width;

    total = 0;
    while (*string != '\0')
    {
        string = OSGetFontTexture(string, image, (s32*)&u, (s32*)&v, (s32*)&width);
        width = width * mRomFontScale;
        total += width;
    }
    return total;
}

inline void ROMFont::DrawBlackScreen()
{
    RenderBegin();
    GXSetZMode(TRUE, GX_LEQUAL, TRUE);
    GXSetColorUpdate(TRUE);
    GXCopyDisp(mCurrentFrameBuffer, TRUE);
    GXDrawDone();
    VISetNextFrameBuffer(mCurrentFrameBuffer);
    VISetBlack(TRUE);
    VIFlush();
    VIWaitForRetrace();

    mCurrentFrameBuffer = mCurrentFrameBuffer == mXFBs[0] ? mXFBs[1] : mXFBs[0];
}

// The long cursors (as in the Ratatouille GC DWARF) put x and y in retail's registers.
void ROMFont::DrawTextBox(S32 x, S32 y, S32 width, S32 height, char* string)
{
    char* word;
    char tempChars[TEXTBOX_MAX_TEXT_LENGTH];
    long currentX;
    long currentY;
    long widthOfSpace;
    S32 wordWidth;

    if (string != NULL)
    {
        RenderBegin();
        currentX = x;
        currentY = y;
        strcpy(tempChars, string);
        word = strtok(tempChars, " ");
        widthOfSpace = GetWidth(" ");
        while (word != NULL && y + height > currentY)
        {
            wordWidth = GetWidth(word);
            if (currentX + wordWidth > x + width)
            {
                currentX = x;
                currentY += mFontData->cellHeight;
            }
            DrawString(currentX, currentY, word);
            word = strtok(NULL, " ");
            currentX += wordWidth + widthOfSpace;
        }
        RenderEnd();
    }
}

void ResetButton::EnableReset()
{
    ResetButton::mResetEnabled = 1;
}

void ResetButton::DisableReset()
{
    ResetButton::mResetEnabled = 0;
}

void ResetButton::SetSndKillFunction(void (*Func)())
{
    ResetButton::mSndKill = Func;
}

void ResetButton::SetMovieKillFunction(void (*Func)())
{
    ResetButton::mMovieKillFunction = Func;
}

void ResetButton::ResetSystem()
{
    S32 dvdStatus;
    bool discIDed;

    if (mResetEnabled)
    {
        if (mMovieKillFunction != NULL)
        {
            mMovieKillFunction();
        }

        OSDisableInterrupts();
        iTRCDisk::mInCriticalSection = false;
        iTRCDisk::mFModError = false;
        iTRCDisk::mDiskErrorInHigherPriorityThread = false;
        PADRecalibrate(PAD_CHAN0_BIT | PAD_CHAN1_BIT | PAD_CHAN2_BIT | PAD_CHAN3_BIT);
        VISetBlack(TRUE);
        VIFlush();
        VIWaitForRetrace();
        OSEnableInterrupts();

        dvdStatus = DVDGetDriveStatus();
        discIDed = DVDCheckDisk();

        if (dvdStatus == DVD_STATE_COVER_OPEN || dvdStatus == DVD_STATE_WRONG_DISK || !mDiscIded ||
            !discIDed)
        {
            OSResetSystem(TRUE, 0, FALSE);
        }
        else
        {
            if (mSndKill != NULL)
            {
                mSndKill();
            }
            OSResetSystem(FALSE, 0, FALSE);
        }
    }
}

void iTRCDisk::Init()
{
    ROMFont::Init();

    gWasDiskError = 0;
    mInCriticalSection = false;
    mFModError = false;
    mInFModLockingCode = false;

    FSOUND_GC_SetDvdStatusCallback(FMODCallBack);
    FSOUND_GC_SetUpdateDvdStatusCallback(FMODUpdateDiskError);

    mMainThread = OSGetCurrentThread();
    mMainPriority = OSGetThreadPriority(mMainThread);
}

void iTRCDisk::SetPadStopRumblingFunction(void (*Func)())
{
    mPadStopRumbling = Func;
}

void iTRCDisk::SetSndSuspendFunction(void (*Func)())
{
    mSndSuspend = Func;
}

void iTRCDisk::SetSndResumeFunction(void (*Func)())
{
    mSndResume = Func;
}

void iTRCDisk::SetAsyncLoadSuspendFunction(void (*Func)())
{
    mAsyncLoadSuspend = Func;
}

void iTRCDisk::SetAsyncLoadResumeFunction(void (*Func)())
{
    mAsyncLoadResume = Func;
}

void iTRCDisk::SetMovieSuspendFunction(void (*Func)())
{
    mMovieSuspendFunction = Func;
}

void iTRCDisk::SetMovieResumeFunction(void (*Func)())
{
    mMovieResumeFunction = Func;
}

void iTRCDisk::SetMovieResetCallBack(void (*Func)())
{
    mMovieResetCallBack = Func;
}

bool iTRCDisk::IsDiskIDed()
{
    return DVDCheckDisk();
}

bool iTRCDisk::SetInFModLockingCode(bool Func)
{
    return mInFModLockingCode = Func;
}

void iTRCDisk::DisplayErrorMessage()
{
    GXCullMode oldCullMode;
    bool exitRW;
    S32 dvdStatus;

    if (strcmp(mMessage, "") != 0)
    {
        GXGetCullMode(&oldCullMode);

        exitRW = false;
        if (_RwDlRenderMode == NULL)
        {
            iSystemInitRWFailSafe();
            exitRW = true;
        }

        ROMFont::InitDisplay(_RwDlRenderMode);

        dvdStatus = SetDVDState();
        while (dvdStatus != DVD_STATE_END && dvdStatus != DVD_STATE_BUSY)
        {
            ROMFont::DrawTextBox(50, 170, 452, 100, mMessage);
            if (dvdStatus != DVD_STATE_FATAL_ERROR)
            {
                ResetButton::CheckResetButton();
            }
            xPadUpdate(0, 0.0f);
            dvdStatus = SetDVDState();
        }

        ResetMessage();

        if (exitRW)
        {
            iSystemExitRWFailSafe();
            ROMFont::DrawBlackScreen();
            ROMFont::DrawBlackScreen();
        }

        GXSetCullMode(oldCullMode);
        VISetBlack(TRUE);
        VIFlush();
        VIWaitForRetrace();
        gWasDiskError = 1;
    }
}

void iTRCDisk::SetErrorMessage(const char* message)
{
    strcpy(mMessage, message);
}

void iTRCDisk::ResetMessage()
{
    memset(mMessage, 0, sizeof(mMessage));
}

// The asserts are compiled-out placeholders (conditions unknown); 4 of them keep GC/2.6 from
// auto-inlining this (>= 30 statements), as retail calls it.
S32 iTRCDisk::SetDVDState()
{
    S32 dvd_state = DVDGetDriveStatus();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    switch (dvd_state)
    {
    case DVD_STATE_END:
        break;
    case DVD_STATE_NO_DISK:
        SetErrorMessage("Please insert the 'The SpongeBob SquarePants Movie' Game Disc.");
        ResetButton::mDiscIded = false;
        break;
    case DVD_STATE_WRONG_DISK:
        SetErrorMessage(
            "This is not the 'The SpongeBob SquarePants Movie' Game Disc. Please insert "
            "the 'The SpongeBob SquarePants Movie' Game Disc.");
        ResetButton::mDiscIded = false;
        break;
    case DVD_STATE_COVER_OPEN:
        SetErrorMessage(
            "The Disc Cover is open. If you want to continue the game, please close the "
            "Disc Cover.");
        ResetButton::mDiscIded = false;
        break;
    case DVD_STATE_RETRY:
        SetErrorMessage("The Game Disc could not be read. Please read the Nintendo GameCube\x99 "
                        "Instruction Booklet for more information.");
        ResetButton::mDiscIded = false;
        break;
    case DVD_STATE_FATAL_ERROR:
        SetErrorMessage("An error has occurred. Turn the power off and refer to the Nintendo "
                        "GameCube\x99 Instruction Booklet for further instructions.");
        break;
    default:
        if (DVDCheckDisk())
        {
            ResetButton::mDiscIded = true;
            ResetMessage();
            memset(mMessage, ' ', 10);
        }
        else
        {
            SetErrorMessage(
                "Please wait... Checking for the 'The SpongeBob SquarePants Movie' Game "
                "Disc.");
            ResetButton::mDiscIded = false;
            dvd_state = DVD_STATE_COVER_OPEN;
        }
        break;
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    return dvd_state;
}

inline bool iTRCDisk::CheckDVDAndResetState()
{
    S32 dvdDriveStatus = DVDGetDriveStatus();
    S32 checkDVD = DVDCheckDisk();

    if ((dvdDriveStatus != DVD_STATE_END && dvdDriveStatus != DVD_STATE_BUSY) || mFModError ||
        mDiskErrorInHigherPriorityThread)
    {
        return true;
    }

    return false;
}

inline void ResetButton::CheckResetButton()
{
    BOOL currentResetState = OSGetResetButtonState();

    if (!currentResetState && mWasResetButtonPressed)
    {
        ResetSystem();
    }

    mWasResetButtonPressed |= currentResetState;
}

bool iTRCDisk::TRCCheck()
{
    OSThread* curThread;
    S32 dvdDriveStatus;
    S32 checkDVD;
    bool wasThereAnError;

    curThread = OSGetCurrentThread();

    if (!mInCriticalSection)
    {
        mInCriticalSection = true;
        OSSetResetCallback(NULL);
    }
    else
    {
        while (mInCriticalSection)
        {
        }
    }

    dvdDriveStatus = SetDVDState();
    checkDVD = DVDCheckDisk();
    wasThereAnError = false;

    if (dvdDriveStatus != DVD_STATE_FATAL_ERROR)
    {
        ResetButton::CheckResetButton();
    }

    if ((dvdDriveStatus != DVD_STATE_END && dvdDriveStatus != DVD_STATE_BUSY) || mFModError ||
        mDiskErrorInHigherPriorityThread)
    {
        if (mSndSuspend != NULL)
        {
            mSndSuspend();
        }
        if (mPadStopRumbling != NULL)
        {
            mPadStopRumbling();
        }
        if (mMovieSuspendFunction != NULL)
        {
            mMovieSuspendFunction();
        }
        if (mAsyncLoadSuspend != NULL)
        {
            mAsyncLoadSuspend();
        }

        DisplayErrorMessage();

        if (mMovieResumeFunction != NULL)
        {
            mMovieResumeFunction();
        }
        if (mAsyncLoadResume != NULL)
        {
            mAsyncLoadResume();
        }
        if (mSndResume != NULL)
        {
            mSndResume();
        }

        wasThereAnError = true;

        ROMFont::DrawBlackScreen();
        ROMFont::DrawBlackScreen();
        VISetBlack(FALSE);
        VIFlush();
        VIWaitForRetrace();
    }

    mInCriticalSection = false;
    mDiskErrorInHigherPriorityThread = false;

    if (mFModThread != NULL && mFModError)
    {
        OSSetThreadPriority(mFModThread, mFModPriority);
        mFModThread = NULL;
    }

    mFModError = false;
    OSSetResetCallback(mMovieResetCallBack);

    return wasThereAnError;
}

void iTRCDisk::FMODCallBack(int status)
{
    if (!mInCriticalSection && status != 0 && !mFModError)
    {
        mFModError = true;
        mFModThread = OSGetCurrentThread();
        mFModPriority = OSGetThreadPriority(mFModThread);
        OSSetThreadPriority(mFModThread, OS_PRIORITY_MAX);
    }
}

U8 iTRCDisk::CheckDiskErrorInHigherPriorityThread()
{
    if (!mDiskErrorInHigherPriorityThread)
    {
        mDiskErrorInHigherPriorityThread = CheckDVDAndResetState();
    }

    return mDiskErrorInHigherPriorityThread;
}

void iTRCDisk::FMODUpdateDiskError(int)
{
    mFModError = true;

    if (mInFModLockingCode)
    {
        TRCCheck();
    }
}

void iTRCDisk::EnableMovieResetCallBack(bool enable)
{
    if (enable)
    {
        OSSetResetCallback(mMovieResetCallBack);
    }
    else
    {
        OSSetResetCallback(NULL);
    }
}
