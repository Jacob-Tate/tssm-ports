#ifndef IFMV_H
#define IFMV_H

// PC counterpart of gc/iFMV.h: TSSM's interface, without the DVD file.
//
// MOVIE keeps its Bink handle because xFMV.cpp (shared) reads hBink's fields
// and calls Bink directly to put a movie on a texture; include/bink/bink.h
// supplies the declarations, and nothing on PC defines them yet -- that path
// is listed as not ported in PORTING.md. The console's DVDFileInfo becomes an
// opaque host handle.

#include <types.h>
#include <bink.h>
#include <stddef.h>
#include <rwcore.h>
#include "xFMV.h"

struct PKRAssetTOCInfo;

struct MOVIE
{
    HBINK hBink;
    void* file;
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
void iFMVSystemInit();

#endif
