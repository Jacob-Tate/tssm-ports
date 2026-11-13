// The Bink calls shared code makes directly, on PC: STUBS.
//
// Caller: xFMVGetTexture (xFMV.cpp), which plays a movie onto a texture inside a
// level by driving a Bink handle frame by frame. On PC iFMVOpen (pc/iFMV.cpp)
// returns NULL, so xFMVGetTexture returns before its first Bink call and none
// of these is reached today; each reports itself once if one is.
//
// The port of the movie-texture path is iFMVOpen on treedome's FFmpeg decoder
// (pc/iFMVDecoderFFmpeg.cpp reads Bink 1), with xFMV.cpp's direct calls moved
// behind i* functions through the GameCube gate. Full-screen movies already
// play through iFMVPlay and do not use these.
//
// Listed in stubs/STUBS.md (tools/pcstubs.py).

#include <types.h>
#include <bink.h>

#include "iStub.h"

extern "C" {

int BinkDoFrame(HBINK bnk)
{
    IPORT_STUB();
    return 0;
}

int BinkCopyToBuffer(HBINK bnk, void* dest, int destpitch, unsigned int destheight,
                     unsigned int destx, unsigned int desty, unsigned int flags)
{
    IPORT_STUB();
    return 0;
}

S32 BinkPause(HBINK bink, S32 pause)
{
    IPORT_STUB();
    return 0;
}

void BinkGoto(HBINK bink, int frame, unsigned int flags)
{
    IPORT_STUB();
}

int BinkWait(HBINK bink)
{
    IPORT_STUB();
    return 0;
}

void BinkNextFrame(HBINK bink)
{
    IPORT_STUB();
}

} // extern "C"
