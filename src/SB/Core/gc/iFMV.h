#ifndef IFMV_H
#define IFMV_H

#include <types.h>
#include <bink.h>
#include <PowerPC_EABI_Support\MSL_C\MSL_Common\size_t.h>
#include <dolphin/dvd.h>
#include <rwcore.h>
#include "xFMV.h"

struct PKRAssetTOCInfo;

// GameCube layout (size 0x48). The movie's byte offset inside its HIP file is passed to the Bink
// file IO in file.cb.userData (BinkOpen with BINKFILEHANDLE copies the DVDFileInfo).
struct MOVIE
{
    HBINK hBink;
    DVDFileInfo file;
    RwRaster* pBinkRaster;
    RwTexture* pBinkTexture;
};

MOVIE* iFMVOpen(PKRAssetTOCInfo& rAssetInfo);
void iFMVSuspend();
void iFMVResume();
void iFMVResetCallback();
void iFMVKill();
void iFMVClose(MOVIE*& pMovie);
U32 iFMVPlay(const char* filename, U32 buttons, F32 time, U32 uSubtitlesAID, bool skippable,
             bool lockController);
#endif
