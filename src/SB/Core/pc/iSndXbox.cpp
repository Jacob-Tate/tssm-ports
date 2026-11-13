#include "iSndXbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See iSndXbox.h. The IMA tables and the decoder's shape are treedome's
// (pc/iSndData.cpp, CC0); the block layout is corrected for TSSM's data, below.

const iSndXboxEntry* iSndXboxTableEntries(const void* sndi, U32 bytes, U32* count)
{
    if (count != NULL)
    {
        *count = 0;
    }

    if (sndi == NULL || bytes < sizeof(iSndXboxTable))
    {
        return NULL;
    }

    const iSndXboxTable* t = (const iSndXboxTable*)sndi;
    U32 n = t->num_sfx + t->num_streams + t->num_cutscene;

    // Exact, not "at least": every retail table is 12 + 44 * n bytes, so a
    // table that is not is not a table this code understands.
    if (n > 0x10000 || bytes != sizeof(iSndXboxTable) + n * sizeof(iSndXboxEntry))
    {
        return NULL;
    }

    if (count != NULL)
    {
        *count = n;
    }

    return (const iSndXboxEntry*)((const U8*)sndi + sizeof(iSndXboxTable));
}

// ---------------------------------------------------------------------------
// Xbox ADPCM
//
// IMA ADPCM in fixed blocks of 36 bytes per channel: a four-byte header per
// channel (a 16-bit sample, a step index, a zero byte), then 32 bytes per
// channel of four-bit codes, interleaved by channel in four-byte words (eight
// codes of the left, eight of the right, ...), low nibble first.
//
// **A block is 64 samples: the header's sample, then 63 decoded codes; the
// 64th code is not played.** Measured on the disc: across 24,000 block
// boundaries of 61 SND assets, the step from a block's last sample to the next
// block's header sample averages 1443 against 1453 inside a block when the
// header is the first sample, and 2289 against 1435 when the header is decoder
// state only and all 64 codes are played (treedome's BfBB reading, which
// clicks at every block edge: 689 times a second at 44.1 kHz). The table's own
// avgBytesPerSec says 64 samples per 36 bytes, so the last code is the one
// left out. Stereo blocks (72 bytes) measure the same with four-byte
// interleave and come apart with one, two or eight.

static const S32 kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const S32 kImaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

#define ISNDXBOX_MAX_CHANNELS 8

static inline S32 iClamp16(S32 v)
{
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

// Frames one block decodes to, per channel: the header sample plus one fewer
// than the codes it holds.
static U32 iFramesPerBlock(U32 channels, U32 block_align)
{
    if (channels == 0 || channels > ISNDXBOX_MAX_CHANNELS || block_align % channels != 0 ||
        block_align / channels < 5)
    {
        return 0;
    }

    return (block_align / channels - 4) * 2;
}

U32 iSndXboxDecodeAdpcmBlocks(const U8* src, U32 bytes, U32 channels, U32 block_align, S16* out,
                              U32 maxBlocks)
{
    U32 fpb = iFramesPerBlock(channels, block_align);
    if (fpb == 0 || src == NULL || out == NULL)
    {
        return 0;
    }

    U32 header = 4 * channels;
    U32 frames = 0;

    for (U32 b = 0; b < maxBlocks; b++)
    {
        U32 off = b * block_align;
        if (off + block_align > bytes)
        {
            // A trailing partial block is not played: none exist on the disc
            // (every data_size is a whole number of blocks).
            break;
        }

        const U8* blk = src + off;
        S16* dst = out + (size_t)frames * channels;

        for (U32 c = 0; c < channels; c++)
        {
            const U8* h = blk + 4 * c;
            S32 pred = (S16)((U16)h[0] | ((U16)h[1] << 8));
            S32 index = h[2] > 88 ? 88 : h[2];

            dst[c] = (S16)pred;

            // Codes 0..fpb-2 of this channel make frames 1..fpb-1.
            for (U32 k = 0; k + 1 < fpb; k++)
            {
                // Eight codes per four-byte word, words taking turns by channel.
                U32 word = (k / 8) * channels + c;
                U32 byte = header + word * 4 + (k % 8) / 2;
                S32 nibble = (k & 1) ? (blk[byte] >> 4) : (blk[byte] & 0xF);

                S32 step = kImaStep[index];
                S32 diff = step >> 3;
                if (nibble & 1)
                {
                    diff += step >> 2;
                }
                if (nibble & 2)
                {
                    diff += step >> 1;
                }
                if (nibble & 4)
                {
                    diff += step;
                }

                pred = iClamp16((nibble & 8) ? pred - diff : pred + diff);

                index += kImaIndex[nibble];
                index = index < 0 ? 0 : (index > 88 ? 88 : index);

                dst[(size_t)(k + 1) * channels + c] = (S16)pred;
            }
        }

        frames += fpb;
    }

    return frames;
}

U32 iSndXboxFrames(const iSndXboxEntry* e)
{
    if (e == NULL || e->block_align == 0)
    {
        return 0;
    }

    U32 channels = e->channels != 0 ? e->channels : 1;

    if (e->format_tag == ISNDXBOX_FORMAT_ADPCM)
    {
        U32 fpb = iFramesPerBlock(channels, e->block_align);
        return fpb != 0 ? (e->data_size / e->block_align) * fpb : 0;
    }

    return e->data_size / e->block_align;
}

void* iSndXboxDecode(const void* data, U32 bytes, U32 format_tag, U32 channels, U32 block_align,
                     U32 bits_per_sample, U32* outBytes)
{
    if (outBytes != NULL)
    {
        *outBytes = 0;
    }

    if (data == NULL || bytes == 0)
    {
        return NULL;
    }

    if (channels == 0)
    {
        channels = 1;
    }

    if (format_tag == ISNDXBOX_FORMAT_PCM && bits_per_sample == 16)
    {
        U32 n = bytes - bytes % (2 * channels);
        if (n == 0)
        {
            return NULL;
        }

        void* out = malloc(n);
        if (out != NULL)
        {
            memcpy(out, data, n);
            if (outBytes != NULL)
            {
                *outBytes = n;
            }
        }
        return out;
    }

    if (format_tag == ISNDXBOX_FORMAT_ADPCM)
    {
        U32 fpb = iFramesPerBlock(channels, block_align);
        U32 blocks = fpb != 0 ? bytes / block_align : 0;
        if (blocks == 0)
        {
            return NULL;
        }

        S16* out = (S16*)malloc((size_t)blocks * fpb * channels * sizeof(S16));
        if (out == NULL)
        {
            return NULL;
        }

        U32 frames =
            iSndXboxDecodeAdpcmBlocks((const U8*)data, bytes, channels, block_align, out, blocks);
        if (outBytes != NULL)
        {
            *outBytes = frames * channels * sizeof(S16);
        }
        return out;
    }

    // 8-bit PCM and anything else: not on the disc.
    return NULL;
}

// ---------------------------------------------------------------------------
// Package table of contents

static inline U32 iBE32(const U8* p)
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
}

static int iCompareAid(const void* a, const void* b)
{
    U32 x = ((const iSndHipAsset*)a)->aid;
    U32 y = ((const iSndHipAsset*)b)->aid;
    return x < y ? -1 : (x > y ? 1 : 0);
}

S32 iSndHipFindAssets(iSndHipReader read, void* ctx, const U32* types, iSndHipAsset* out, S32 max)
{
    U8 hdr[8];

    // HIPA (an empty chunk), then PACK, then DICT { ATOC { AINF, AHDR... }, LTOC }.
    if (read(ctx, 0, hdr, 8) != 8 || iBE32(hdr) != 'HIPA')
    {
        return -1;
    }

    U32 pos = 8 + iBE32(hdr + 4);
    U32 dictBody = 0;
    U32 dictSize = 0;

    for (S32 guard = 0; guard < 16; guard++)
    {
        if (read(ctx, pos, hdr, 8) != 8)
        {
            return -1;
        }

        U32 id = iBE32(hdr);
        U32 size = iBE32(hdr + 4);

        if (id == 'DICT')
        {
            dictBody = pos + 8;
            dictSize = size;
            break;
        }

        pos += 8 + size;
        pos += pos & 1;
    }

    if (dictSize == 0 || dictSize > 64 * 1024 * 1024)
    {
        return -1;
    }

    U8* dict = (U8*)malloc(dictSize);
    if (dict == NULL || read(ctx, dictBody, dict, dictSize) != dictSize)
    {
        free(dict);
        return -1;
    }

    S32 found = 0;
    U32 p = 0;

    while (p + 8 <= dictSize)
    {
        U32 id = iBE32(dict + p);
        U32 size = iBE32(dict + p + 4);
        U32 body = p + 8;
        U32 end = body + size;
        if (end > dictSize || end < body)
        {
            break;
        }

        if (id == 'ATOC')
        {
            U32 q = body;
            while (q + 8 <= end)
            {
                U32 aid = iBE32(dict + q);
                U32 asize = iBE32(dict + q + 4);
                U32 abody = q + 8;
                U32 aend = abody + asize;
                if (aend > end || aend < abody)
                {
                    break;
                }

                // AHDR: id, type, offset, size, pad, flags, then its ADBG.
                if (aid == 'AHDR' && asize >= 24)
                {
                    const U8* a = dict + abody;
                    U32 type = iBE32(a + 4);

                    for (const U32* t = types; *t != 0; t++)
                    {
                        if (*t == type)
                        {
                            if (found < max)
                            {
                                out[found].aid = iBE32(a);
                                out[found].type = type;
                                out[found].offset = iBE32(a + 8);
                                out[found].size = iBE32(a + 12);
                            }
                            found++;
                            break;
                        }
                    }
                }

                q = aend + (aend & 1);
            }
        }

        p = end + (end & 1);
    }

    free(dict);

    S32 n = found < max ? found : max;
    qsort(out, (size_t)n, sizeof(iSndHipAsset), iCompareAid);
    return n;
}

// ---------------------------------------------------------------------------
// Cutscene audio

#define ISNDXBOX_CSN_SOUND 5

const void* iSndXboxCsnAudio(const void* chunk, U32 bytes, U32 assetID, U32* size)
{
    if (size != NULL)
    {
        *size = 0;
    }

    if (chunk == NULL || bytes < 16)
    {
        return NULL;
    }

    const U8* base = (const U8*)chunk;

    // xCutsceneTime: StartTime, EndTime, NumData, ChunkIndex.
    U32 numData;
    memcpy(&numData, base + 8, 4);

    U32 p = 16;
    for (U32 i = 0; i < numData; i++)
    {
        if (p + 16 > bytes)
        {
            return NULL;
        }

        // xCutsceneData: DataType, AssetID, ChunkSize, FileOffset.
        U32 rec[3];
        memcpy(rec, base + p, sizeof(rec));

        U32 payload = p + 16;
        U32 chunkSize = rec[2];
        if (chunkSize > bytes - payload)
        {
            return NULL;
        }

        if (rec[0] == ISNDXBOX_CSN_SOUND && rec[1] == assetID)
        {
            // The PCM starts at the payload's first 32-byte boundary, the
            // record's ChunkSize counting the padding (gc/iCutscene.cpp's
            // iCSSoundGetData does the same against the buffer address, which
            // is the chunk start's alignment).
            U32 start = payload;
            U32 n = chunkSize;
            while ((start & 0x1F) != 0 && n >= 16)
            {
                start += 16;
                n -= 16;
            }

            if (size != NULL)
            {
                *size = n;
            }
            return base + start;
        }

        p = payload + ((chunkSize + 15) & ~15U);
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Diagnostics

static void iPut32(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
    p[2] = (U8)(v >> 16);
    p[3] = (U8)(v >> 24);
}

static void iPut16(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
}

bool iSndXboxWriteWav(const char* path, const S16* pcm, U32 frames, U32 channels, U32 rate)
{
    if (path == NULL || channels == 0)
    {
        return false;
    }

    FILE* fp = fopen(path, "wb");
    if (fp == NULL)
    {
        return false;
    }

    U32 data = frames * channels * 2;
    U8 h[44];
    memcpy(h, "RIFF", 4);
    iPut32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    iPut32(h + 16, 16);
    iPut16(h + 20, 1);
    iPut16(h + 22, channels);
    iPut32(h + 24, rate);
    iPut32(h + 28, rate * channels * 2);
    iPut16(h + 32, channels * 2);
    iPut16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    iPut32(h + 40, data);

    // The host is little-endian (every target the port builds for), so the
    // samples go out as they are.
    bool ok = fwrite(h, 1, sizeof(h), fp) == sizeof(h);
    if (ok && data != 0)
    {
        ok = fwrite(pcm, 1, data, fp) == data;
    }

    ok = (fclose(fp) == 0) && ok;
    return ok;
}
