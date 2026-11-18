#include "iSndDsp.h"

// See iSndDsp.h.

void iSndDspReset(iSndDspState* st, const iSndDspChannel* ch, U32 channels, bool loop)
{
    for (U32 c = 0; c < ISNDDSP_MAX_CHANNELS; c++)
    {
        const bool have = ch != NULL && c < channels;
        st->hist1[c] = have ? (loop ? ch[c].loopHist1 : ch[c].hist1) : 0;
        st->hist2[c] = have ? (loop ? ch[c].loopHist2 : ch[c].hist2) : 0;
    }
}

static inline S16 iClamp16(S64 v)
{
    return (S16)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

U32 iSndDspDecode(const U8* src, U32 bytes, U32 channels, const iSndDspChannel* ch,
                  iSndDspState* st, S16* out, U32 maxBlocks)
{
    if (src == NULL || ch == NULL || st == NULL || out == NULL || channels == 0 ||
        channels > ISNDDSP_MAX_CHANNELS)
    {
        return 0;
    }

    const U32 block = ISNDDSP_FRAME_BYTES * channels;
    U32 blocks = bytes / block;
    if (blocks > maxBlocks)
    {
        blocks = maxBlocks;
    }

    for (U32 b = 0; b < blocks; b++)
    {
        const U8* blk = src + b * block;

        for (U32 c = 0; c < channels; c++)
        {
            // Byte k of this channel's frame: two bytes per channel in turn.
            U8 frame[ISNDDSP_FRAME_BYTES];
            for (U32 k = 0; k < ISNDDSP_FRAME_BYTES; k++)
            {
                frame[k] = channels == 1 ? blk[k] : blk[(k / 2) * 2 * channels + c * 2 + (k & 1)];
            }

            const S32 scale = 1 << (frame[0] & 0xF);
            const U32 pair = (frame[0] >> 4) & 7;
            const S32 c1 = ch[c].coef[pair * 2];
            const S32 c2 = ch[c].coef[pair * 2 + 1];
            S32 h1 = st->hist1[c];
            S32 h2 = st->hist2[c];
            S16* dst = out + (size_t)b * ISNDDSP_FRAME_SAMPLES * channels + c;

            for (U32 i = 0; i < ISNDDSP_FRAME_SAMPLES; i++)
            {
                const U8 byte = frame[1 + i / 2];
                S32 nibble = (i & 1) ? (byte & 0xF) : (byte >> 4);
                if (nibble >= 8)
                {
                    nibble -= 16;
                }

                // The DSP's own arithmetic: 11 fractional bits, rounded. In
                // 64 bits: extreme coefficients in a damaged table would
                // overflow 32, which retail's never come near.
                const S64 s = (S64)(nibble * scale) * 2048 + 1024 + (S64)c1 * h1 + (S64)c2 * h2;
                const S16 sample = iClamp16(s >> 11);

                dst[(size_t)i * channels] = sample;
                h2 = h1;
                h1 = sample;
            }

            st->hist1[c] = (S16)h1;
            st->hist2[c] = (S16)h2;
        }
    }

    return blocks * ISNDDSP_FRAME_SAMPLES;
}

U32 iSndDspBytesFor(U32 samples, U32 channels)
{
    const U32 frames = samples / ISNDDSP_FRAME_SAMPLES + (samples % ISNDDSP_FRAME_SAMPLES != 0);
    return frames * ISNDDSP_FRAME_BYTES * channels;
}
