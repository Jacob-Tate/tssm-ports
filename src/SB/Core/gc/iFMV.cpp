#include "iFMV.h"
#include "iARAMTmp.h"
#include "iFile.h"
#include "iSystem.h"
#include "iTRC.h"
#include "xColor.h"
#include "xpkrsvc.h"
#include "xstransvc.h"
#include "zGameState.h"
#include "iCamera.h"
#include "iPad.h"
#include "iTime.h"
#include "xCamera.h"
#include "xPad.h"
#include "xSubTitles.h"
#include "xTRC.h"
#include "zGame.h"
#include "zGlobals.h"
#include "zMain.h"

#include <string.h>

// FIXME: This should be in a RW header somewhere
extern "C" {
void RwGameCubeGetXFBs(void*, void*);
}

//.sbss
HBINK hBinkCurrMovie;
static S32 sBlackCounter;
static bool sARAMtmp;
static U32 amt;

static void* _MovieAlloc(U32 uNumBytes)
{
    void* addr;

    if (sARAMtmp)
    {
        if (uNumBytes <= amt)
        {
            addr = iARAMTmpMalloc(uNumBytes);
            if (addr == IARAMTMP_NO_MEMORY)
            {
                addr = RwMalloc(uNumBytes, 0);
            }
            else
            {
                amt -= uNumBytes;
            }
        }
        else
        {
            addr = RwMalloc(uNumBytes, 0);
        }
    }
    else
    {
        addr = RwMalloc(uNumBytes, 0);
    }

    return addr;
}

static void _MovieFree(void* mem)
{
    if (iARAMTmpInARAMTemp(mem))
    {
        iARAMTmpFree(mem);
    }
    else
    {
        RwFree(mem);
    }
}

S32 iFMVSystemInit()
{
    return 1;
}

static HBINK iFMVOpen(const char* fullFile, U32 uBinkFlags)
{
    RADARAMCALLBACKS OurGCCallbacks;
    HBINK hBinkMovie;
    S32 numRetries;
    tag_xFile tfile;

    OurGCCallbacks.aram_malloc = iSystemARAMAlloc;
    OurGCCallbacks.aram_free = iSystemARAMFree;
    BinkSetSoundSystem(BinkOpenAX, (U32)&OurGCCallbacks);
    iTRCDisk::SetMovieResetCallBack(iFMVResetCallback);
    iTRCDisk::EnableMovieResetCallBack(true);
    BinkSetMemory(_MovieAlloc, _MovieFree);

    hBinkMovie = NULL;
    if (!iFileExists(fullFile, 0))
    {
        return NULL;
    }

    numRetries = 0;
    do
    {
        if (iTRCDisk::IsDiskIDed())
        {
            if ((gGameMode == eGameMode_Game || gGameMode == eGameMode_Pause) && !sARAMtmp)
            {
                sARAMtmp = true;
                amt = iARAMTmpBegin('ANIM', 0x280000);
            }

            hBinkMovie = BinkOpen(fullFile, uBinkFlags);
            BinkGetError();
            if (hBinkMovie == NULL)
            {
                BinkGetError();
            }
        }

        if (iTRCDisk::TRCCheck())
        {
            numRetries++;
            if (hBinkMovie != NULL)
            {
                BinkClose(hBinkMovie);
                hBinkMovie = NULL;
            }
        }
        else
        {
            if (sBlackCounter > 0)
            {
                if (--sBlackCounter > 0)
                {
                    VISetBlack(TRUE);
                }
                else
                {
                    VISetBlack(FALSE);
                }
                VIFlush();
                VIWaitForRetrace();
            }

            if (hBinkMovie == NULL)
            {
                numRetries++;
                iFileOpen(fullFile, 0, &tfile);
                DVDSeekAsyncPrio(&tfile.ps.fileInfo, 0, NULL, 2);
                iTRCDisk::TRCCheck();
                DVDCancel(&tfile.ps.fileInfo.cb);
                iFileClose(&tfile);
            }
        }
    } while (hBinkMovie == NULL && numRetries < 100);

    return hBinkMovie;
}

static inline void iFMVClose(HBINK hMovie)
{
    BinkClose(hMovie);
    iTRCDisk::SetMovieResetCallBack(NULL);
    iTRCDisk::EnableMovieResetCallBack(false);

    if (sARAMtmp)
    {
        sARAMtmp = false;
        iARAMTmpEnd(false);
    }
}

static inline S32 NextPow2(S32 in)
{
    S32 ret = 1;
    while (ret < in)
    {
        ret <<= 1;
    }
    return ret;
}

MOVIE* iFMVOpen(PKRAssetTOCInfo& rAssetInfo)
{
    RADARAMCALLBACKS OurGCCallbacks;
    MOVIE* pMovie;
    char* pFilename;
    char fullname[256];

    OurGCCallbacks.aram_malloc = iSystemARAMAlloc;
    OurGCCallbacks.aram_free = iSystemARAMFree;
    BinkSetSoundSystem(BinkOpenAX, (U32)&OurGCCallbacks);
    iTRCDisk::SetMovieResetCallBack(iFMVResetCallback);
    iTRCDisk::EnableMovieResetCallBack(true);
    BinkSetMemory(_MovieAlloc, _MovieFree);

    pMovie = (MOVIE*)_MovieAlloc(sizeof(MOVIE));

    pFilename = xST_xAssetID_HIPFullPath(rAssetInfo.aid);
    iFileFullPath(pFilename, fullname);
    if (!DVDFastOpen(DVDConvertPathToEntrynum(fullname), &pMovie->file))
    {
        // retail does not return here: pMovie is NULL from now on and still written through below
        iFMVClose(pMovie);
    }

    pMovie->file.cb.userData =
        (void*)(rAssetInfo.plus_offset + rAssetInfo.sector * iFileGetSectorSize(XFILE_DRIVE_CDVD));

    pMovie->hBink = BinkOpen((const char*)&pMovie->file, BINKFILEHANDLE | BINKNOFILLIOBUF);
    if (pMovie->hBink == NULL)
    {
        iFMVClose(pMovie);
    }
    else
    {
        pMovie->pBinkRaster =
            RwRasterCreate(NextPow2(pMovie->hBink->Width), NextPow2(pMovie->hBink->Height), 32,
                           rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
        if (pMovie->pBinkRaster == NULL)
        {
            iFMVClose(pMovie);
        }
        else
        {
            RwRasterPushContext(pMovie->pBinkRaster);
            RwRasterClear(*(const RwInt32*)&g_BLACK);
            RwRasterPopContext();

            pMovie->pBinkTexture = RwTextureCreate(pMovie->pBinkRaster);
            if (pMovie->pBinkTexture == NULL)
            {
                iFMVClose(pMovie);
            }
        }
    }

    return pMovie;
}

void iFMVClose(MOVIE*& pMovie)
{
    iTRCDisk::SetMovieResetCallBack(NULL);
    iTRCDisk::EnableMovieResetCallBack(false);

    if (pMovie->pBinkTexture != NULL)
    {
        RwTextureDestroy(pMovie->pBinkTexture);
    }

    BinkClose(pMovie->hBink);
    DVDClose(&pMovie->file);
    _MovieFree(pMovie);
    pMovie = NULL;
}

static U32 iFMVPlay(const char* filename, U32 buttons, F32 time, U32 uSubtitlesAID, bool skippable,
                    bool lockController, S32 uAudioTrack);

U32 iFMVPlay(const char* filename, U32 buttons, F32 time, U32 uSubtitlesAID, bool skippable,
             bool lockController)
{
    char fullname[64];
    char BIK[5] = ".BIK";
    _GameOstrich old;
    U32 ret;

    strcpy(fullname, filename);
    strcat(fullname, BIK);

    if (!iFileExists(fullname, 0))
    {
        return 1;
    }

    xFMVPauseSound(true);
    old = zGameGetOstrich();
    zGameSetOstrich(eGameOstrich_PlayingMovie);
    ret = iFMVPlay(fullname, buttons, time, uSubtitlesAID, skippable, lockController,
                   xSTGetLocalizationEnum());
    zGameSetOstrich(old);
    xFMVPauseSound(false);

    return ret;
}

void iFMVSuspend()
{
    if (hBinkCurrMovie != NULL)
    {
        BinkPause(hBinkCurrMovie, TRUE);
    }
}

void iFMVResume()
{
    if (hBinkCurrMovie != NULL)
    {
        BinkPause(hBinkCurrMovie, FALSE);
        sBlackCounter = 3;
    }
}

// MovieRect (an RwRect) and pad (a _tagxPad*) as in Ratatouille's iFMVPlay; plain locals in their
// place change the register allocation. 'pad->pressed & buttons' keeps retail's operand order.
static U32 iFMVPlay(const char* filename, U32 buttons, F32 time, U32 uSubtitlesAID, bool skippable,
                    bool lockController, S32 uAudioTrack)
{
    iPadStopRumble(xglobals->pad0);

    if (skippable)
    {
        buttons |= XPAD_BUTTON_O | XPAD_BUTTON_SELECT;
    }

    if (filename == NULL)
    {
        return 1;
    }

    char fullFile[256];
    iFileFullPath(filename, fullFile);

    BinkSetSoundTrack(1, (U32*)&uAudioTrack);
    HBINK hBinkMovie = iFMVOpen(fullFile, BINKSNDTRACK);
    if (hBinkMovie == NULL)
    {
        iTime delay = iTimeGet();
        iTime now = iTimeGet();
        while (now < delay + 200)
        {
            now = iTimeGet();
        }

        if (sARAMtmp)
        {
            sARAMtmp = false;
            iARAMTmpEnd(true);
        }
        return 1;
    }

    bool bTrackFound = false;
    for (S32 track = 0; track < hBinkMovie->NumTracks; track++)
    {
        if (uAudioTrack == hBinkMovie->trackIDs[track])
        {
            bTrackFound = true;
            break;
        }
    }

    if (!bTrackFound)
    {
        BinkClose(hBinkMovie);
        iTRCDisk::SetMovieResetCallBack(NULL);
        iTRCDisk::EnableMovieResetCallBack(false);
        if (sARAMtmp)
        {
            sARAMtmp = false;
            iARAMTmpEnd(true);
        }

        hBinkMovie = iFMVOpen(fullFile, 0);
        uAudioTrack = BinkGetTrackID(hBinkMovie, 0);
    }

    BinkSetVolume(hBinkMovie, uAudioTrack, xFMVGetBinkCompliantVolume());

    RwRaster* pCamRaster = RwCameraGetRaster(xglobals->screen->icam);
    S32 iBackBufWidth = RwRasterGetWidth(pCamRaster);
    S32 iBackBufHeight = RwRasterGetHeight(pCamRaster);
    S32 iSafeWidth = (iBackBufWidth + 3) & ~3;
    S32 iSafeHeight = (iBackBufHeight + 3) & ~3;
    RwRect MovieRect;
    if ((S32)hBinkMovie->Width > iSafeWidth || (S32)hBinkMovie->Height > iSafeHeight)
    {
        return 1;
    }

    S32 iHorizPad = iBackBufWidth - (S32)hBinkMovie->Width;
    S32 iVertPad = iBackBufHeight - (S32)hBinkMovie->Height;
    MovieRect.x = iHorizPad / 2;
    MovieRect.y = iVertPad / 2;

    // First pad polled for the skip buttons (this local has no name in the PS2 DWARF)
    S32 controllerStart = lockController ? xglobals->currentActivePad : 0;
    S32 controllerEnd = xglobals->currentActivePad + 1;

    RwCamera* cam = iCameraCreate(FB_XRES, FB_YRES, FALSE);
    RwRGBA bg = { 0, 0, 0, 0 };
    RwCameraClear(cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
    RwCameraShowRaster(cam, NULL, 1);
    RwCameraShowRaster(cam, NULL, 1);
    iCameraDestroy(cam);

    VIWaitForRetrace();
    VISetBlack(FALSE);
    VIFlush();

    iTime fmvAbortDelay = iTimeGet();
    bool aborted = false;
    iTime now = iTimeGet();
    iTime dwTimeNow = iTimeGet();

    void* xfbDisp;
    void* xfbCopy;
    RwGameCubeGetXFBs(&xfbDisp, &xfbCopy);
    void* xfbDest = xfbCopy;
    bool bUseYUVPath = (uSubtitlesAID == 0);
    RwRaster* pBinkRaster = NULL;
    RwGameCubeRasterExtension* pRasterExt = NULL;
    S32 iBinkMovieHalfHeight = (S32)hBinkMovie->Height / 2;
    if (!bUseYUVPath)
    {
        pBinkRaster = RwRasterCreate(hBinkMovie->Width, iBinkMovieHalfHeight, 32,
                                     rwRASTERTYPENORMAL | rwRASTERFORMAT888 | rwRASTERDONTALLOCATE);
        pRasterExt =
            RWPLUGINOFFSET(RwGameCubeRasterExtension, pBinkRaster, _RwGameCubeRasterExtOffset);
    }
    else
    {
        VISetBlack(FALSE);
    }

    hBinkCurrMovie = hBinkMovie;
    bool waiting = false;
    while (hBinkMovie->FrameNum != hBinkMovie->Frames && !aborted)
    {
        if (iTRCDisk::TRCCheck())
        {
            break;
        }

        if (sBlackCounter > 0)
        {
            if (--sBlackCounter > 0)
            {
                VISetBlack(TRUE);
            }
            else
            {
                VISetBlack(FALSE);
            }
            VIFlush();
            VIWaitForRetrace();
        }

        if (!waiting)
        {
            if ((F32)fmvAbortDelay + 2000.0f <= (F32)iTimeGet() && skippable)
            {
                for (S32 i = controllerStart; i < controllerEnd; i++)
                {
                    _tagxPad* pad = &mPad[i];
                    xPadUpdate(i, 0.0f);
                    if ((pad->pressed & buttons) &&
                        (gTrcPad[0].state == TRC_PadInserted || !lockController))
                    {
                        aborted = true;
                        break;
                    }
                }
            }

            if (gSoak && (F32)hBinkMovie->FrameNum /
                                 ((F32)hBinkMovie->FrameRate / (F32)hBinkMovie->FrameRateDiv) >
                             2.0f)
            {
                aborted = true;
            }

            BinkDoFrame(hBinkMovie);

            if (bUseYUVPath)
            {
                S32 oneIfSkip =
                    BinkCopyToBuffer(hBinkMovie, xfbDest, iBackBufWidth * 2, hBinkMovie->Height,
                                     MovieRect.x, MovieRect.y, BINKCOPYALL | BINKSURFACEYUY2);
                DCStoreRange((U8*)xfbDest + (MovieRect.x + iBackBufWidth * MovieRect.y) * 2,
                             hBinkMovie->Height * (iBackBufWidth * 2));
                if (oneIfSkip == 0)
                {
                    VISetNextFrameBuffer(xfbDest);
                    VIFlush();
                    if (xfbDest == xfbCopy)
                    {
                        xfbDest = xfbDisp;
                    }
                    else
                    {
                        xfbDest = xfbCopy;
                    }
                }
            }
            else
            {
                RwRasterPushContext(pCamRaster);
                RwGameCubeGetXFBs(&xfbDisp, &xfbCopy);
                pRasterExt->pixels = (RwUInt8*)xfbCopy;

                U8* pBits = RwRasterLock(pBinkRaster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKRAW);
                S32 oneIfSkip =
                    BinkCopyToBufferRect(hBinkMovie, pBits, RwRasterGetStride(pBinkRaster),
                                         iBinkMovieHalfHeight, 0, 0, 0, 0,
                                         RwRasterGetWidth(pBinkRaster), iBinkMovieHalfHeight,
                                         BINKCOPYALL | BINKSURFACE32);
                RwRasterUnlock(pBinkRaster);

                if (oneIfSkip == 0)
                {
                    RwRasterClear(0xFF000000);
                    RwRasterRenderFast(pBinkRaster, MovieRect.x, MovieRect.y);

                    pBits = RwRasterLock(pBinkRaster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKRAW);
                    BinkCopyToBufferRect(hBinkMovie, pBits, RwRasterGetStride(pBinkRaster),
                                         iBinkMovieHalfHeight, 0, 0, 0, iBinkMovieHalfHeight,
                                         RwRasterGetWidth(pBinkRaster), iBinkMovieHalfHeight,
                                         BINKCOPYALL | BINKNOSKIP | BINKSURFACE32);
                    RwRasterUnlock(pBinkRaster);
                    RwRasterRenderFast(pBinkRaster, MovieRect.x,
                                       MovieRect.y + iBinkMovieHalfHeight);
                }
                else
                {
                    BinkGetError();
                }

                RwRasterPopContext();
                pRasterExt->pixels = NULL;

                if (uSubtitlesAID)
                {
                    xglobals->screen->begin_render(false);
                    xSubTitlesRender(uSubtitlesAID,
                                     (F32)(hBinkMovie->FrameNum * hBinkMovie->FrameRateDiv) /
                                         (F32)hBinkMovie->FrameRate);
                    xglobals->screen->end_render();
                }

                iCameraShowRaster(xglobals->screen->icam);
            }

            if (hBinkMovie->FrameNum != hBinkMovie->Frames)
            {
                BinkNextFrame(hBinkMovie);
            }
        }

        waiting = BinkWait(hBinkMovie);
    }

    if (bUseYUVPath)
    {
        RwGameCubeGetXFBs(&xfbDisp, &xfbCopy);
        VISetBlack(TRUE);
        VISetNextFrameBuffer(xfbDisp);
        VIFlush();
        VIWaitForRetrace();
        VISetBlack(FALSE);
    }
    else
    {
        RwRasterDestroy(pBinkRaster);
    }

    BinkClose(hBinkMovie);
    iTRCDisk::SetMovieResetCallBack(NULL);
    iTRCDisk::EnableMovieResetCallBack(false);
    if (sARAMtmp)
    {
        sARAMtmp = false;
        iARAMTmpEnd(true);
    }

    hBinkCurrMovie = NULL;
    return 0;
}

void iFMVResetCallback()
{
    iTRCDisk::TRCCheck();
    OSSetResetCallback(iFMVResetCallback);
}

void iFMVKill()
{
    if (hBinkCurrMovie != NULL)
    {
        iFMVClose(hBinkCurrMovie);
        hBinkCurrMovie = NULL;
    }
}
