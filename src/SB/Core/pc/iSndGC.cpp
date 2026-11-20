#include "iSndGC.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See iSndGC.h.

#define ISNDGC_HEADER 0x20
#define ISNDGC_WAV 8
#define ISNDGC_CSN 0x64
#define ISNDGC_FSB_HEADER 24
#define ISNDGC_FULL_HEADER 80
#define ISNDGC_DSP_INFO 0x2E
#define ISNDGC_BASIC_HEADER 8

#define ISNDGC_MODE_BASIC 0x2 // FSB3 bank mode: basic headers after the first
#define ISNDGC_SAMPLE_GCADPCM 0x02000000 // sample mode

// Sanity bounds for data a mod could hand in; every retail value is far inside.
#define ISNDGC_MAX_SAMPLES_PER_BANK 4096
#define ISNDGC_MAX_BANK_HEADERS (4U << 20)
#define ISNDGC_MIN_RATE 1000
#define ISNDGC_MAX_RATE 192000

static inline U32 iBE32(const U8* p)
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
}

static inline U16 iBE16(const U8* p)
{
    return (U16)(((U32)p[0] << 8) | (U32)p[1]);
}

static inline U32 iLE32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

static inline U16 iLE16(const U8* p)
{
    return (U16)((U32)p[0] | ((U32)p[1] << 8));
}

static void iWhy(char* why, U32 whySize, const char* fmt, ...)
{
    if (why == NULL || whySize == 0)
    {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, whySize, fmt, ap);
    va_end(ap);
}

// 0x2E bytes of big-endian DSP state: coef[16], gain, predictor, yn1, yn2, and
// the loop's predictor, yn1, yn2.
static void iReadDspInfo(const U8* p, iSndDspChannel* ch)
{
    for (U32 i = 0; i < 16; i++)
    {
        ch->coef[i] = (S16)iBE16(p + 2 * i);
    }
    ch->hist1 = (S16)iBE16(p + 0x24);
    ch->hist2 = (S16)iBE16(p + 0x26);
    ch->loopHist1 = (S16)iBE16(p + 0x2A);
    ch->loopHist2 = (S16)iBE16(p + 0x2C);
}

// One sample of a bank, before it is matched to the table.
struct gcsample
{
    U32 channels;
    U32 rate;
    U32 samples;
    U32 offset; // from the start of the SNDI asset
    U32 size;
    iSndDspChannel ch[ISNDDSP_MAX_CHANNELS];
};

struct gcbank
{
    U32 first; // index of its first sample in the flat array
    U32 count;
};

static bool iRead(iSndGCReader read, void* ctx, U32 size, U32 offset, void* buf, U32 n)
{
    if (offset > size || n > size - offset)
    {
        return false;
    }
    return n == 0 || read(ctx, offset, buf, n) == n;
}

// Bank `index` at `at` (from the start of the asset), inside [0x20, end).
// Appends its samples to `out` (room for `cap`); false with the reason.
static bool iReadBank(iSndGCReader read, void* ctx, U32 size, U32 index, U32 at, U32 end,
                      gcsample** out, U32* used, U32* cap, char* why, U32 whySize)
{
    U8 h[ISNDGC_FSB_HEADER];
    if (at < ISNDGC_HEADER || at > end || end - at < ISNDGC_FSB_HEADER ||
        !iRead(read, ctx, size, at, h, sizeof(h)))
    {
        iWhy(why, whySize, "sound bank %u is outside the table", index);
        return false;
    }
    if (memcmp(h, "FSB3", 4) != 0)
    {
        iWhy(why, whySize, "sound bank %u is not an FSB3 bank", index);
        return false;
    }

    const U32 count = iLE32(h + 4);
    const U32 headers = iLE32(h + 8);
    const U32 data = iLE32(h + 12);
    const U32 mode = iLE32(h + 20);
    const U32 body = at + ISNDGC_FSB_HEADER;

    if (count == 0 || count > ISNDGC_MAX_SAMPLES_PER_BANK || headers > ISNDGC_MAX_BANK_HEADERS ||
        headers > end - body || data > end - body - headers)
    {
        iWhy(why, whySize, "sound bank %u's counts do not fit it (%u samples, %u + %u bytes)",
             index, count, headers, data);
        return false;
    }

    U8* hb = (U8*)malloc(headers != 0 ? headers : 1);
    if (hb == NULL || !iRead(read, ctx, size, body, hb, headers))
    {
        free(hb);
        iWhy(why, whySize, "sound bank %u cannot be read", index);
        return false;
    }

    if (*used + count > *cap)
    {
        U32 grown = *cap != 0 ? *cap : 256;
        while (grown < *used + count)
        {
            grown *= 2;
        }
        gcsample* more = (gcsample*)realloc(*out, sizeof(gcsample) * grown);
        if (more == NULL)
        {
            free(hb);
            iWhy(why, whySize, "out of memory");
            return false;
        }
        *out = more;
        *cap = grown;
    }

    U32 p = 0;
    U32 dataAt = body + headers; // the next sample's bytes
    U32 dataLeft = data;
    U32 channels = 0;
    U32 rate = 0;

    for (U32 k = 0; k < count; k++)
    {
        gcsample* s = &(*out)[*used + k];
        memset(s, 0, sizeof(*s));
        U32 lengthSamples;
        U32 lengthBytes;
        U32 info; // where the DSP state starts

        if (k == 0 || !(mode & ISNDGC_MODE_BASIC))
        {
            if (headers - p < ISNDGC_FULL_HEADER)
            {
                free(hb);
                iWhy(why, whySize, "sound bank %u sample %u's header does not fit", index, k);
                return false;
            }
            const U8* f = hb + p;
            const U32 hsize = iLE16(f);
            lengthSamples = iLE32(f + 32);
            lengthBytes = iLE32(f + 36);
            const U32 smode = iLE32(f + 48);
            rate = iLE32(f + 52);
            channels = iLE16(f + 62);

            if (channels == 0 || channels > ISNDDSP_MAX_CHANNELS || !(smode & ISNDGC_SAMPLE_GCADPCM) ||
                rate < ISNDGC_MIN_RATE || rate > ISNDGC_MAX_RATE)
            {
                free(hb);
                iWhy(why, whySize,
                     "sound bank %u sample %u is not GameCube DSP-ADPCM the port plays "
                     "(%u channels, %u Hz, mode %08x)",
                     index, k, channels, rate, smode);
                return false;
            }
            if (hsize < ISNDGC_FULL_HEADER + ISNDGC_DSP_INFO * channels || hsize > headers - p)
            {
                free(hb);
                iWhy(why, whySize, "sound bank %u sample %u's header does not fit", index, k);
                return false;
            }
            info = p + ISNDGC_FULL_HEADER;
            p += hsize;
        }
        else
        {
            const U32 hsize = ISNDGC_BASIC_HEADER + ISNDGC_DSP_INFO * channels;
            if (headers - p < hsize)
            {
                free(hb);
                iWhy(why, whySize, "sound bank %u sample %u's header does not fit", index, k);
                return false;
            }
            lengthSamples = iLE32(hb + p);
            lengthBytes = iLE32(hb + p + 4);
            info = p + ISNDGC_BASIC_HEADER;
            p += hsize;
        }

        if (lengthBytes > dataLeft)
        {
            free(hb);
            iWhy(why, whySize, "sound bank %u sample %u runs past the bank (%u bytes, %u left)",
                 index, k, lengthBytes, dataLeft);
            return false;
        }

        s->channels = channels;
        s->rate = rate;
        s->offset = dataAt;
        // Only whole blocks are played; a sound says how many samples of
        // them are real.
        const U32 block = ISNDDSP_FRAME_BYTES * channels;
        s->size = lengthBytes - lengthBytes % block;
        const U32 most = s->size / block * ISNDDSP_FRAME_SAMPLES;
        s->samples = lengthSamples < most ? lengthSamples : most;
        for (U32 c = 0; c < channels; c++)
        {
            iReadDspInfo(hb + info + ISNDGC_DSP_INFO * c, &s->ch[c]);
        }

        dataAt += lengthBytes;
        dataLeft -= lengthBytes;
    }

    free(hb);
    *used += count;
    return true;
}

bool iSndGCLooksLikeTable(const void* head, U32 headBytes, U32 size)
{
    if (head == NULL || headBytes < ISNDGC_HEADER || size < ISNDGC_HEADER)
    {
        return false;
    }
    const U8* h = (const U8*)head;
    const U32 sum = iBE32(h + 4);
    // The runtime pointers are zero in the file.
    return sum <= size - ISNDGC_HEADER && iBE32(h + 8) == 0 && iBE32(h + 12) == 0 &&
           iBE32(h + 16) == 0 && iBE32(h + 20) == 0;
}

bool iSndGCReadTable(iSndGCReader read, void* ctx, U32 size, iSndGCTable* out, char* why,
                     U32 whySize)
{
    memset(out, 0, sizeof(*out));
    iWhy(why, whySize, "");

    U8 h[ISNDGC_HEADER];
    if (read == NULL || !iRead(read, ctx, size, 0, h, sizeof(h)) ||
        !iSndGCLooksLikeTable(h, sizeof(h), size))
    {
        iWhy(why, whySize, "not a GameCube sound table");
        return false;
    }

    const U32 sum = iBE32(h + 4);
    const U32 nWav = iBE16(h + 0x18);
    const U32 nBanks = h[0x1E];
    const U32 nCsn = h[0x1F];
    const U32 tail = ISNDGC_HEADER + sum;
    const U64 tailBytes = (U64)4 * nBanks + (U64)ISNDGC_WAV * nWav + (U64)ISNDGC_CSN * nCsn;

    if (tailBytes > size - tail)
    {
        iWhy(why, whySize, "its counts (%u sounds, %u banks, %u cutscene tracks) do not fit "
                           "its %u bytes",
             nWav, nBanks, nCsn, size);
        return false;
    }

    U8* t = (U8*)malloc(tailBytes != 0 ? (size_t)tailBytes : 1);
    if (t == NULL || !iRead(read, ctx, size, tail, t, (U32)tailBytes))
    {
        free(t);
        iWhy(why, whySize, "its tables cannot be read");
        return false;
    }

    gcsample* samples = NULL;
    U32 used = 0;
    U32 cap = 0;
    gcbank* banks = (gcbank*)calloc(nBanks != 0 ? nBanks : 1, sizeof(gcbank));
    bool ok = banks != NULL;
    if (!ok)
    {
        iWhy(why, whySize, "out of memory");
    }

    // Only the banks a sound names are read.
    const U8* wav = t + 4 * nBanks;
    U8 wanted[256];
    memset(wanted, 0, sizeof(wanted));
    for (U32 i = 0; ok && i < nWav; i++)
    {
        const U8* w = wav + ISNDGC_WAV * i;
        const U32 bank = (w[4] & 0x2) ? w[6] : 0;
        if (bank >= nBanks)
        {
            iWhy(why, whySize, "sound %08x names bank %u of %u", iBE32(w), bank, nBanks);
            ok = false;
        }
        else
        {
            wanted[bank] = 1;
        }
    }

    for (U32 b = 0; ok && b < nBanks; b++)
    {
        banks[b].first = used;
        if (!wanted[b])
        {
            continue;
        }
        const S32 rel = (S32)iBE32(t + 4 * b);
        if (rel < 0 || (U32)rel > sum)
        {
            iWhy(why, whySize, "sound bank %u is outside the table", b);
            ok = false;
            break;
        }
        ok = iReadBank(read, ctx, size, b, ISNDGC_HEADER + (U32)rel, tail, &samples, &used, &cap,
                       why, whySize);
        banks[b].count = used - banks[b].first;
    }

    if (ok)
    {
        out->id = iBE32(h);
        out->sounds = (iSndGCSound*)calloc(nWav != 0 ? nWav : 1, sizeof(iSndGCSound));
        out->cutscenes = (iSndGCCutscene*)calloc(nCsn != 0 ? nCsn : 1, sizeof(iSndGCCutscene));
        ok = out->sounds != NULL && out->cutscenes != NULL;
        if (!ok)
        {
            iWhy(why, whySize, "out of memory");
        }
    }

    for (U32 i = 0; ok && i < nWav; i++)
    {
        const U8* w = wav + ISNDGC_WAV * i;
        const bool streamed = (w[4] & 0x2) != 0;
        // As the console: an effect is sample N of bank 0, a stream bank N.
        const U32 bank = streamed ? w[6] : 0;
        const U32 index = w[5];
        if (index >= banks[bank].count)
        {
            iWhy(why, whySize, "sound %08x names sample %u of bank %u, which has %u", iBE32(w),
                 index, bank, banks[bank].count);
            ok = false;
            break;
        }

        const gcsample* s = &samples[banks[bank].first + index];
        iSndGCSound* d = &out->sounds[i];
        d->assetID = iBE32(w);
        d->flags = w[4] & 0x3;
        d->channels = (U8)s->channels;
        d->rate = s->rate;
        d->samples = s->samples;
        d->offset = s->offset;
        d->size = s->size;
        memcpy(d->ch, s->ch, sizeof(d->ch));
        out->numStreams += streamed;
    }

    for (U32 j = 0; ok && j < nCsn; j++)
    {
        // sDSPADPCM: num_samples, num_nibbles, sample_rate, loop flag and
        // format (u16), loop start and end, current address, then the DSP
        // state at 0x1C, the track's id at 0x60.
        const U8* c = wav + ISNDGC_WAV * nWav + ISNDGC_CSN * j;
        iSndGCCutscene* d = &out->cutscenes[j];
        d->assetID = iBE32(c + 0x60);
        d->samples = iBE32(c);
        d->rate = iBE32(c + 8);
        iReadDspInfo(c + 0x1C, &d->ch);
        if (d->rate < ISNDGC_MIN_RATE || d->rate > ISNDGC_MAX_RATE || iBE16(c + 0xE) != 0)
        {
            iWhy(why, whySize, "cutscene track %08x is not DSP-ADPCM the port plays (%u Hz)",
                 d->assetID, d->rate);
            ok = false;
        }
    }

    free(samples);
    free(banks);
    free(t);

    if (!ok)
    {
        iSndGCFreeTable(out);
        return false;
    }

    out->numSounds = nWav;
    out->numCutscene = nCsn;
    return true;
}

void iSndGCFreeTable(iSndGCTable* t)
{
    if (t == NULL)
    {
        return;
    }
    free(t->sounds);
    free(t->cutscenes);
    memset(t, 0, sizeof(*t));
}

void* iSndGCDecode(const iSndGCSound* s, const void* data, U32 bytes, U32* outBytes)
{
    if (outBytes != NULL)
    {
        *outBytes = 0;
    }
    if (s == NULL || data == NULL || s->channels == 0 || s->channels > ISNDDSP_MAX_CHANNELS ||
        s->samples == 0)
    {
        return NULL;
    }

    const U32 need = iSndDspBytesFor(s->samples, s->channels);
    if (bytes < need)
    {
        return NULL;
    }

    const U32 blocks = need / (ISNDDSP_FRAME_BYTES * s->channels);
    S16* pcm = (S16*)malloc((size_t)blocks * ISNDDSP_FRAME_SAMPLES * s->channels * sizeof(S16));
    if (pcm == NULL)
    {
        return NULL;
    }

    iSndDspState st;
    iSndDspReset(&st, s->ch, s->channels, false);
    iSndDspDecode((const U8*)data, need, s->channels, s->ch, &st, pcm, blocks);

    if (outBytes != NULL)
    {
        *outBytes = s->samples * s->channels * (U32)sizeof(S16);
    }
    return pcm;
}
