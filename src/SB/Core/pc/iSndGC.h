#ifndef ISNDGC_H
#define ISNDGC_H

#include <types.h>

#include "iSndDsp.h"

// PC-only: the GameCube release's sound table, as bytes. Like iSndXbox.h it
// knows nothing of the game, so the sound test (tests/gcsndtest.cpp) runs
// exactly the code the game does.
//
// A GameCube SNDI asset (gc/iSnd.h) is, all big-endian unless said:
//
//   0x00  gcSoundInfoAsset: id, uSumFSBSize, four runtime pointers (zero),
//         nWavFiles, nSounds, nStreams (u16), nFSBFiles, nCutscene (u8)
//   0x20  uSumFSBSize bytes of FSB3 sound banks: bank 0 holds the sound
//         effects, and each stream is a bank of its own
//         then nFSBFiles s32 bank offsets, from 0x20
//         nWavFiles gcWavInfo (8 bytes: id, flags, sample, bank, -), sorted
//         by id: flags 0x2 streamed, 0x1 looping
//         nCutscene sDSPADPCM headers (0x64 bytes: the DSP-ADPCM header and
//         the track's id), whose samples are in the cutscenes' own chunks
//
// An FSB3 bank is little-endian: "FSB3", the sample count, the header and
// data sizes, the version and the mode, then the sample headers, then the
// samples back to back. A full sample header is 80 bytes (the name, the
// length in samples and in bytes, the loop points, the mode, the rate, ...,
// the channel count), then 0x2E bytes of DSP state per channel: the eight
// coefficient pairs, the gain, the predictor, the history and the loop's,
// big-endian. With mode bit 2 every header after the first is "basic": the
// two lengths and the DSP state, the rest taken from the first.
//
// Measured on the disc (USA): 64 tables, 552 banks, 3,318 sounds (2,829
// effects, 489 streams), all at 32 kHz, mono but for 22 stereo streams; 27
// cutscene tracks at 22,050 Hz, mono. Every loop is the whole sample and
// every history and loop history is zero. The table is read in place, a
// few hundred bytes at a time: one of them (mnuc.HIP's) is 131 MB.

// One sound, as iSnd plays it.
struct iSndGCSound
{
    U32 assetID;
    U8 flags; // gcWavInfo's: 0x2 streamed, 0x1 looping
    U8 channels; // 1 or 2
    U16 pad;
    U32 rate; // Hz
    U32 samples; // sample frames
    U32 offset; // of its DSP-ADPCM bytes, from the start of the SNDI asset
    U32 size; // those bytes, whole blocks (iSndDspBytesFor)
    iSndDspChannel ch[ISNDDSP_MAX_CHANNELS];
};

// One cutscene track: its samples are in the cutscene's SOUND records.
struct iSndGCCutscene
{
    U32 assetID;
    U32 rate;
    U32 samples;
    iSndDspChannel ch;
};

struct iSndGCTable
{
    U32 id; // the header's own id
    U32 numSounds;
    U32 numStreams; // of numSounds, the streamed ones
    iSndGCSound* sounds; // in the table's order, sorted by id
    U32 numCutscene;
    iSndGCCutscene* cutscenes;
};

// Reads `size` bytes at `offset` of the SNDI asset; returns the bytes read.
typedef U32 (*iSndGCReader)(void* ctx, U32 offset, void* buf, U32 size);

// Reads the GameCube SNDI asset of `size` bytes. False, with the reason in
// `why`, when it is not one or anything in it does not fit: counts against
// the bytes, every bank and sample inside the asset, channel counts, rates
// and lengths in range. Nothing is read outside [0, size).
bool iSndGCReadTable(iSndGCReader read, void* ctx, U32 size, iSndGCTable* out, char* why,
                     U32 whySize);
void iSndGCFreeTable(iSndGCTable* t);

// Whether the first bytes of an SNDI asset of `size` bytes could be a GameCube
// table's header (`head` holds min(size, 0x20) bytes).
bool iSndGCLooksLikeTable(const void* head, U32 headBytes, U32 size);

// Decodes a sound's bytes (s->size of them, from s->offset) into 16-bit PCM,
// interleaved, s->samples frames, in a block from malloc. NULL when the bytes
// are short. *outBytes gets the PCM's length.
void* iSndGCDecode(const iSndGCSound* s, const void* data, U32 bytes, U32* outBytes);

#endif
