#ifndef XSUBTITLES_H
#define XSUBTITLES_H

#include <types.h>

#include "xBase.h"

struct xSubtitleLine
{
    F32 fStartTime;
    F32 fStopTime;
    U32 uStringOffset;
};

struct xSubtitlesAsset : xBaseAsset
{
    U16 m_uAssetNumLines;
    U16 m_uAssetByteCount;
    xSubtitleLine m_paSubtitleLines[];
};

U8 xSubTitlesRender(U32 aid, F32 fTime);
void xSubTitlesUnloaded(void* userdata, U32 assetid);
void* xSubTitlesRead(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);

#endif
