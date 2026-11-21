#ifndef ISNDDATA_H
#define ISNDDATA_H

#include <types.h>

// PC-only: where a sound's samples come from, decoded.
//
// TSSM-ADAPTED from treedome's iSndData (CC0). The cache and its rules are
// treedome's; what changed is where the bytes are. On the console the sound
// layer (SRAM) never reaches main memory -- PKR_layerLoadDest maps it to
// PKR_LDDEST_SKIP, and FMOD streams each sound bank from the package into ARAM
// itself (gc/iSnd.cpp) -- so a host has to read the samples from the package
// too. iSnd.cpp knows, from the package's own table of contents, which file
// and which byte range an asset is (see iSnd.cpp for why it does not ask the
// packer), and hands that here.

struct iSndDataSource
{
    const char* path; // the package, as iFileOpen takes it
    U32 offset; // of the asset, in the package
    U32 size; // of the asset, in bytes

    // The table entry's format (iSndXbox.h).
    U32 format_tag;
    U32 channels;
    U32 block_align;
    U32 bits;
    U32 sample_rate; // for TSSM_SND_DUMP's files only

    // A sound of a GameCube table (iSndGC.h): DSP-ADPCM, decoded with its own
    // coefficients, and the format fields above are not used. NULL for Xbox.
    const struct iSndGCSound* gc;
};

// The samples of one asset as 16-bit PCM, interleaved, reading and decoding
// them the first time they are asked for. NULL if the asset cannot be read or
// decoded, which a caller treats as "play this voice silently" (iSndHost.h).
// *bytes gets the PCM's length.
//
// The block stays valid until the matching iSndDataRelease; every successful
// Acquire is paired with exactly one Release, including when a voice is stopped
// early. Entries are keyed by asset id AND package, since two packages may
// hold different data under one id.
const void* iSndDataAcquire(U32 assetID, const iSndDataSource* src, U32* bytes);
void iSndDataRelease(const void* data);

// Drop everything, including entries still held. Only for iSndInit/iSndExit,
// where no voice can be playing.
void iSndDataReset();

// For the log: how much is cached and how much of it is spoken for.
void iSndDataStats(U32* entries, U32* bytes, U32* pinned);

// TSSM_SND_DUMP names a folder: each sound decoded is also written there as
// <asset id>.wav, once. Diagnostics only; NULL when it is not set.
const char* iSndDataDumpDir();

#endif
