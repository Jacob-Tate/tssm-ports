#include "xFMV.h"

#include "xSnd.h"
#include "iFMV.h"

static void DefaultFMVPauseSoundCB(bool doPause);

static F32 sFMVVolumeScale = 1.0f;
static void (*sxFMVPauseSoundCB)(bool) = DefaultFMVPauseSoundCB;

void xFMVPauseSound(bool doPause)
{
    sxFMVPauseSoundCB(doPause);

    if (doPause)
    {
        xSndMgrUpdate();
    }
}

static void DefaultFMVPauseSoundCB(bool doPause)
{
    xSndMgrPauseSounds(-3, doPause, true);
}

void (*xFMVSetPauseSoundCB(void (*pauseSoundCB)(bool)))(bool)
{
    void (*curCB)(bool) = sxFMVPauseSoundCB;

    if (pauseSoundCB == NULL)
    {
        pauseSoundCB = DefaultFMVPauseSoundCB;
    }
    sxFMVPauseSoundCB = pauseSoundCB;

    return curCB;
}

S32 xFMVGetBinkCompliantVolume()
{
    S32 volume = 32768.0f * xSndMgrGetDampedVolume(4) * sFMVVolumeScale;
    return volume;
}

RwTexture* xFMVGetTexture(MOVIE* pMovie, U16 looping, U16 freezeframed, U32 startframe,
                          U32 endframe, U32)
{
    if (pMovie == NULL)
    {
        return NULL;
    }

    if (endframe == 0)
    {
        endframe = pMovie->hBink->Frames;
    }

    if (pMovie->hBink->LastFrameNum == 0xFFFFFFFF)
    {
        BinkGoto(pMovie->hBink, startframe + 1, 0);
    }

    if (!BinkWait(pMovie->hBink))
    {
        BinkDoFrame(pMovie->hBink);

        RwUInt8* pBits = RwRasterLock(pMovie->pBinkRaster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKRAW);
        BinkCopyToBuffer(pMovie->hBink, pBits, RwRasterGetStride(pMovie->pBinkRaster),
                         RwRasterGetHeight(pMovie->pBinkRaster), 0, 0, BINKCOPYALL | BINKSURFACE32);
        RwRasterUnlock(pMovie->pBinkRaster);

        if (pMovie->hBink->Paused != freezeframed)
        {
            BinkPause(pMovie->hBink, freezeframed);
        }

        if (looping)
        {
            if (endframe == pMovie->hBink->LastFrameNum && startframe != 0)
            {
                BinkGoto(pMovie->hBink, startframe, 0);
            }
            BinkNextFrame(pMovie->hBink);
            BinkWait(pMovie->hBink);
        }
        else if (endframe != pMovie->hBink->LastFrameNum)
        {
            BinkNextFrame(pMovie->hBink);
            BinkWait(pMovie->hBink);
        }
    }

    return pMovie->pBinkTexture;
}
