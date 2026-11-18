#ifndef ISNDDSP_H
#define ISNDDSP_H

#include <types.h>

// PC-only: GameCube DSP-ADPCM, the format of every sound on the GameCube disc.
// After the decoder of the BfBB PC port (CC0), made to stream: the history is
// kept between calls, so a sound can be decoded a few blocks at a time.
//
// Only <types.h>, so the sound test (tests/gcsndtest.cpp) runs exactly this.
//
// A frame is 8 bytes and 14 samples: a predictor/scale byte (the high nibble
// picks one of eight coefficient pairs, the low nibble is the scale's power of
// two), then fourteen signed 4-bit codes, high nibble first.
//
// A stereo sound of an FSB3 bank interleaves its two channels two bytes at a
// time: L0 L1 R0 R1 L2 L3 R2 R3 ... , one 16-byte block per frame pair.
// Measured on the 22 stereo streams of the GameCube disc: decoded that way the
// two channels correlate at 0.18-0.80, and at 0.01-0.10 with whole 8-byte
// frames taking turns.

#define ISNDDSP_FRAME_BYTES 8
#define ISNDDSP_FRAME_SAMPLES 14
#define ISNDDSP_MAX_CHANNELS 2

struct iSndDspChannel
{
    S16 coef[16]; // eight pairs
    S16 hist1; // the history the sound starts from (yn1, yn2)
    S16 hist2;
    S16 loopHist1; // and the one a loop goes back to
    S16 loopHist2;
};

struct iSndDspState
{
    S16 hist1[ISNDDSP_MAX_CHANNELS];
    S16 hist2[ISNDDSP_MAX_CHANNELS];
};

// The state at the start of a sound (loop false) or of its loop (loop true).
void iSndDspReset(iSndDspState* st, const iSndDspChannel* ch, U32 channels, bool loop);

// Decodes whole blocks (8 * channels bytes each) of `src`, at most
// `maxBlocks` of them, into interleaved 16-bit PCM at `out` (room for
// maxBlocks * 14 * channels samples). Returns the sample frames written. A
// trailing partial block is not decoded. NULL or a channel count other than 1
// or 2 decodes nothing.
U32 iSndDspDecode(const U8* src, U32 bytes, U32 channels, const iSndDspChannel* ch,
                  iSndDspState* st, S16* out, U32 maxBlocks);

// The bytes `samples` sample frames take, whole frames per channel.
U32 iSndDspBytesFor(U32 samples, U32 channels);

#endif
