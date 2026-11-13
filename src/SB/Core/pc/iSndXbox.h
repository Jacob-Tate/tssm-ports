#ifndef ISNDXBOX_H
#define ISNDXBOX_H

#include <types.h>

// PC-only: the Xbox release's sound data, as bytes. Nothing in here knows about
// the game -- no voices, no packer, no file layer -- so the standalone sound
// test (tests/sndtest.cpp) runs exactly the code the game does.
//
// Three formats meet here:
//
//  - the SNDI sound table: a 12-byte count header and fixed 44-byte entries
//    built around a WAVEFORMATEX. Every one of the 64 SNDI assets on the Xbox
//    disc is exactly 12 + 44 * n bytes, the same layout as BfBB's Xbox release.
//    The entries name SND (sound effects) and SNDS (streams) assets in the same
//    package, and CSSS assets, which are empty: their PCM is inside a cutscene.
//  - the samples: Xbox ADPCM (format tag 0x69, 36-byte blocks per channel, 64
//    samples per block) for all 3,323 SND and SNDS assets, and 16-bit PCM for
//    the 33 cutscene tracks.
//  - the package table of contents (HIP/HOP ATOC), read here only to find the
//    byte range of a sound asset in the package its table came from.
//
// All of it is little-endian except the package's chunk ids, sizes and AHDR
// fields, which are big-endian as on every Heavy Iron platform.

#define ISNDXBOX_FORMAT_PCM 0x1
#define ISNDXBOX_FORMAT_ADPCM 0x69

// One entry of an Xbox SNDI table, as it sits in the asset.
struct iSndXboxEntry
{
    // WAVEFORMATEX, verbatim.
    U16 format_tag; // 0x00: 1 = PCM, 0x69 = Xbox ADPCM
    U16 channels; // 0x02
    U32 samples_per_sec; // 0x04
    U32 avg_bytes_per_sec; // 0x08
    U16 block_align; // 0x0C
    U16 bits_per_sample; // 0x0E
    U16 cb_size; // 0x10
    U16 pad12; // 0x12: realigns the fields after the 18-byte WAVEFORMATEX
    U32 data_size; // 0x14: byte length of the sample data
    U32 assetID; // 0x18: the SND, SNDS or CSSS asset holding it
    U32 loop_flag; // 0x1C: 0 or 1
    U32 runtime[3]; // 0x20: zero in every retail entry
};

// The header of an Xbox SNDI asset. The entries follow: the sound effects
// first, then the streams, then the cutscene tracks (none on the disc: the
// CSSS entries are counted as streams).
struct iSndXboxTable
{
    U32 num_sfx;
    U32 num_streams;
    U32 num_cutscene;
};

TSSM_STATIC_ASSERT(sizeof(iSndXboxEntry) == 44, "the 44-byte on-disc SNDI entry");
TSSM_STATIC_ASSERT(sizeof(iSndXboxTable) == 12, "the 12-byte SNDI count header");

// The entries of an SNDI asset of `bytes` bytes, or NULL if the counts do not
// describe exactly that many bytes. *count gets the entry total.
const iSndXboxEntry* iSndXboxTableEntries(const void* sndi, U32 bytes, U32* count);

// Sample frames in an entry's data, from the table alone. For ADPCM that is
// 64 per 36-byte block and channel, not data_size / block_align.
U32 iSndXboxFrames(const iSndXboxEntry* e);

// Decode `bytes` of sample data in the format the entry gives into 16-bit PCM,
// interleaved by channel, in a block from malloc that the caller frees. NULL if
// the format is not one the disc uses or the data is too short to be one block.
// *outBytes gets the PCM's length.
void* iSndXboxDecode(const void* data, U32 bytes, U32 format_tag, U32 channels, U32 block_align,
                     U32 bits_per_sample, U32* outBytes);

// Decode at most `maxBlocks` whole ADPCM blocks from `src` into `out` (room for
// maxBlocks * 64 frames * channels). Returns the frames written. The streaming
// form of iSndXboxDecode; a block needs nothing from the one before it.
U32 iSndXboxDecodeAdpcmBlocks(const U8* src, U32 bytes, U32 channels, U32 block_align, S16* out,
                              U32 maxBlocks);

// ---------------------------------------------------------------------------
// Package table of contents

struct iSndHipAsset
{
    U32 aid;
    U32 type; // as a big-endian tag value: 'SND ', 'SNDS', 'SNDI', 'CSN '
    U32 offset; // absolute, in the package file
    U32 size;
};

// Reads `size` bytes at `offset` of the package; returns the bytes read.
typedef U32 (*iSndHipReader)(void* ctx, U32 offset, void* buf, U32 size);

// Every asset of the package whose type is one of `types` (zero-terminated
// list), up to `max` of them, into `out` sorted by asset id. Returns how many
// matched, or -1 if the package is not a HIPA container.
S32 iSndHipFindAssets(iSndHipReader read, void* ctx, const U32* types, iSndHipAsset* out, S32 max);

// ---------------------------------------------------------------------------
// Cutscene audio

// One time chunk of a cutscene is an xCutsceneTime header (16 bytes) and its
// records: a 16-byte xCutsceneData header each, then the payload padded to 16.
// A SOUND record (type 5) holds a slice of one track's 16-bit PCM, starting at
// the first 32-byte boundary of the payload counted from the chunk's start
// (the console's DSP wanted it there; the bytes skipped are zero).
//
// The audio of `assetID` in the chunk at `chunk` (`bytes` long), or NULL if the
// chunk has none. *size gets the PCM's length. The record walk stops at `bytes`,
// so a damaged chunk cannot send it past the buffer.
const void* iSndXboxCsnAudio(const void* chunk, U32 bytes, U32 assetID, U32* size);

// ---------------------------------------------------------------------------
// Diagnostics

// Writes 16-bit PCM as a RIFF WAVE file. For TSSM_SND_DUMP and the sound test;
// false if the file could not be written.
bool iSndXboxWriteWav(const char* path, const S16* pcm, U32 frames, U32 channels, U32 rate);

#endif
