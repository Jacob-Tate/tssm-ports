// tssm_sndtest: the PC sound path on the Xbox sound data, without the game.
//
//     tssm_sndtest <package> <out folder> [sounds]
//
// Reads one package (a .HIP or .HOP from the user's own Xbox asset folder) and
// runs it through the code the game's iSnd uses:
//
//   1. every SNDI table: the 12 + 44 * n layout (iSndXboxTableEntries), and
//      each entry against the asset it names in the package's table of
//      contents (iSndHipFindAssets): the asset exists and its size is the
//      entry's data_size;
//   2. up to [sounds] of its SND and SNDS assets (default 6, the largest
//      stream and any stereo one among them) decoded (iSndXboxDecode) to
//      <out>/<id>.wav, with the frame count checked against the table's
//      (iSndXboxFrames) and the decode rate reported;
//   3. a mix of those sounds through the audio backend's offline mix
//      (iSndHostTestMix): one plain, one an octave up, one panned hard left,
//      one looping, one through the reverb send, to <out>/mix.wav;
//   4. every cutscene (CSN) in the package: its region-0 track pulled chunk by
//      chunk (iSndXboxCsnAudio) the way the game pulls it -- a chunk becomes
//      readable when the picture reaches the chunk before it -- into a fed
//      voice, mixed at 48 kHz to <out>/csn_<id>.wav, with underruns counted
//      (there should be none: the track is laid out a chunk ahead);
//   5. the volume groups (iSndHostSetGroupGain) on a constant signal of the
//      test's own, no game data, before step 4, which then shows they leave
//      the mixer as they found it: full level changes nothing to the bit, each
//      level is exact once arrived, a voice answers only to its group, the way
//      there has no step in it, and the reverb's tail goes with its group.
//
// The audio device is not opened (TSSM_AUDIO=0 is set before the backend
// starts): everything is rendered offline. With --device as the fourth
// argument, step 3 instead plays three of the sounds (one of them fed) through
// the real output device at zero gain -- nothing is heard -- and checks that
// each ends when its length says, on the device's own clock. Exit status 0
// when every check passes.

#include "iSndHost.h"
#include "iSndReverb.h"
#include "iSndXbox.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// iSndHostSDL.cpp's test hook; deliberately in no header.
void iSndHostTestMix(U32 rate, float* out, U32 frames);

static FILE* sPkg;
static S32 sFailures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("FAIL: " __VA_ARGS__);                                                          \
            printf("\n");                                                                          \
            sFailures++;                                                                           \
        }                                                                                          \
    } while (0)

static U32 readPkg(void*, U32 offset, void* buf, U32 size)
{
    if (fseek(sPkg, (long)offset, SEEK_SET) != 0)
    {
        return 0;
    }
    return (U32)fread(buf, 1, size, sPkg);
}

static U8* readAsset(const iSndHipAsset* a)
{
    U8* mem = (U8*)malloc(a->size != 0 ? a->size : 1);
    if (mem != NULL && readPkg(NULL, a->offset, mem, a->size) != a->size)
    {
        free(mem);
        return NULL;
    }
    return mem;
}

static const iSndHipAsset* findAsset(const iSndHipAsset* list, S32 n, U32 aid)
{
    for (S32 i = 0; i < n; i++)
    {
        if (list[i].aid == aid)
        {
            return &list[i];
        }
    }
    return NULL;
}

static double nowSec()
{
    return (double)iHostMonotonicNs() / 1e9;
}

static void writeMix(const char* path, const float* mix, U32 frames, U32 rate)
{
    S16* pcm = (S16*)malloc((size_t)frames * 2 * sizeof(S16));
    if (pcm == NULL)
    {
        return;
    }

    for (U32 n = 0; n < frames * 2; n++)
    {
        float v = mix[n] * 32767.0f;
        pcm[n] = (S16)(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : (S32)v));
    }

    CHECK(iSndXboxWriteWav(path, pcm, frames, 2, rate), "could not write %s", path);
    free(pcm);
}

static float peakOf(const float* mix, U32 n)
{
    float p = 0.0f;
    for (U32 i = 0; i < n; i++)
    {
        float a = mix[i] < 0 ? -mix[i] : mix[i];
        p = a > p ? a : p;
    }
    return p;
}

struct decoded
{
    U32 aid;
    iSndXboxEntry fmt;
    S16* pcm;
    U32 frames;
};

#define MIX_RATE 48000

// Mixes `frames` output frames in blocks of `block`. Answers with the largest
// change from one frame to the next, on either side. `last` is the frame mixed
// last, on the way in and out.
static float groupMix(U32 frames, U32 block, float* last)
{
    static float buf[4096 * 2];
    float biggest = 0.0f;

    for (U32 at = 0; at < frames; at += block)
    {
        U32 n = frames - at < block ? frames - at : block;
        iSndHostTestMix(MIX_RATE, buf, n);

        for (U32 f = 0; f < n; f++)
        {
            for (U32 c = 0; c < 2; c++)
            {
                float step = buf[f * 2 + c] - last[c];
                step = step < 0.0f ? -step : step;
                biggest = step > biggest ? step : biggest;
                last[c] = buf[f * 2 + c];
            }
        }
    }

    return biggest;
}

// Step 5. The source is a constant 0.5 and each voice's own level 0.5, so every
// output sample says the level it was mixed at directly: an effects voice on
// the left, and a voice of `other` (fed, if `otherFed`) on the right.
static void groupGainTest(iSndHostGroup other, bool otherFed)
{
    static const S16 kHalf[8] = { 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384 };
    static S16 sFed[65536];
    float last[2] = { 0.0f, 0.0f };

    iSndHostSample s;
    memset(&s, 0, sizeof(s));
    s.data = kHalf;
    s.bytes = sizeof(kHalf);
    s.channels = 1;
    s.bits = 16;
    s.sample_rate = MIX_RATE;
    s.num_samples = 8;
    s.looping = true;

    S32 fx = iSndHostAcquire(0);
    S32 ot = iSndHostAcquire(0);
    CHECK(fx >= 0 && ot >= 0, "no host voice");
    if (fx < 0 || ot < 0)
    {
        return;
    }

    iSndHostSetVol(fx, 0.5f, 0.0f);
    iSndHostSetVol(ot, 0.0f, 0.5f);
    iSndHostSetGroup(ot, other);
    iSndHostStart(fx, &s);
    if (otherFed)
    {
        for (U32 f = 0; f < 65536; f++)
        {
            sFed[f] = 16384;
        }
        iSndHostStartFeed(ot, 1, MIX_RATE, 65536, false);
        iSndHostFeed(ot, sFed, 65000);
    }
    else
    {
        iSndHostStart(ot, &s);
    }

    // Full level: what the voices' own levels say, to the bit.
    groupMix(4096, 512, last);
    CHECK(last[0] == 0.25f && last[1] == 0.25f, "group %d at full: %f, %f", other, last[0],
          last[1]);

    // A jump to zero and back, in a block a phone's device might ask for and
    // one much longer: no step between two frames beyond a 20 ms ramp's (the
    // whole 0.25 over 960 frames, and a little for rounding), and the other
    // side does not move at all.
    const float rampStep = 0.25f / 960.0f * 1.01f;
    const U32 blocks[2] = { 192, 2048 };
    for (U32 b = 0; b < 2; b++)
    {
        iSndHostSetGroupGain(other, 0.0f);
        float step = groupMix(8192, blocks[b], last);
        CHECK(step <= rampStep, "group %d to zero, block %u: a step of %f", other, blocks[b], step);
        CHECK(last[0] == 0.25f && last[1] == 0.0f, "group %d at zero: %f, %f", other, last[0],
              last[1]);

        iSndHostSetGroupGain(other, 1.0f);
        step = groupMix(8192, blocks[b], last);
        CHECK(step <= rampStep, "group %d back, block %u: a step of %f", other, blocks[b], step);
        CHECK(last[0] == 0.25f && last[1] == 0.25f, "group %d back: %f, %f", other, last[0],
              last[1]);
    }

    iSndHostSetGroupGain(other, 0.5f);
    groupMix(8192, 512, last);
    CHECK(last[0] == 0.25f && last[1] == 0.125f, "group %d at half: %f, %f", other, last[0],
          last[1]);
    iSndHostSetGroupGain(other, 1.0f);
    groupMix(8192, 512, last);

    // The effects group, level and all, with the voice dry...
    iSndHostSetGroupGain(ISNDHOST_GROUP_EFFECTS, 0.5f);
    groupMix(8192, 512, last);
    CHECK(last[0] == 0.125f && last[1] == 0.25f, "effects at half: %f, %f", last[0], last[1]);
    iSndHostSetGroupGain(ISNDHOST_GROUP_EFFECTS, 1.0f);
    groupMix(8192, 512, last);

    // ...and on the reverb's bus. The tail rings on with the voice gone, and is
    // silent at zero while it rings.
    static const iSndHostReverb kCave = { -1000,  0,     0.0f,   1.5f,   1.0f,   -1363,
                                          0.008f, -1153, 0.011f, 100.0f, 100.0f, 5000.0f };
    iSndReverbInit(MIX_RATE);
    iSndHostSetReverb(&kCave);
    iSndHostSetSend(fx, true);
    groupMix(16384, 512, last);
    iSndHostStop(fx);
    groupMix(512, 512, last);
    CHECK(last[0] != 0.0f, "no reverb tail to test with");
    iSndHostSetGroupGain(ISNDHOST_GROUP_EFFECTS, 0.0f);
    groupMix(2048, 512, last);
    CHECK(!iSndReverbIdle() && last[0] == 0.0f, "the tail at zero: %f", last[0]);
    iSndHostSetGroupGain(ISNDHOST_GROUP_EFFECTS, 1.0f);
    groupMix(2048, 512, last);
    CHECK(last[0] != 0.0f, "the tail is gone after zero");
    iSndHostSetReverb(NULL);
    iSndReverbExit();

    iSndHostRelease(fx);
    iSndHostStop(ot);
    iSndHostRelease(ot);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("usage: tssm_sndtest <package> <out folder> [sounds]\n");
        return 2;
    }

    const char* outDir = argv[2];
    S32 wantSounds = argc > 3 ? atoi(argv[3]) : 6;
    bool device = argc > 4 && strcmp(argv[4], "--device") == 0;

    if (!device)
    {
#ifdef _WIN32
        _putenv("TSSM_AUDIO=0");
#else
        setenv("TSSM_AUDIO", "0", 1);
#endif
    }

    sPkg = fopen(argv[1], "rb");
    if (sPkg == NULL)
    {
        printf("cannot open %s\n", argv[1]);
        return 2;
    }

    static const U32 kTypes[] = { 'SNDI', 'SND ', 'SNDS', 'CSN ', 0 };
    const S32 maxAssets = 16384;
    iSndHipAsset* assets = (iSndHipAsset*)malloc(sizeof(iSndHipAsset) * maxAssets);
    S32 n = iSndHipFindAssets(readPkg, NULL, kTypes, assets, maxAssets);
    CHECK(n > 0, "no sound assets found in %s (%d)", argv[1], n);
    if (n <= 0)
    {
        return 1;
    }

    S32 nSndi = 0, nSnd = 0, nSnds = 0, nCsn = 0;
    for (S32 i = 0; i < n; i++)
    {
        nSndi += assets[i].type == 'SNDI';
        nSnd += assets[i].type == 'SND ';
        nSnds += assets[i].type == 'SNDS';
        nCsn += assets[i].type == 'CSN ';
    }
    printf("%s: %d SNDI, %d SND, %d SNDS, %d CSN\n", argv[1], nSndi, nSnd, nSnds, nCsn);

    // ---- 1. the tables ----------------------------------------------------
    decoded* picks = (decoded*)calloc(wantSounds > 0 ? wantSounds : 1, sizeof(decoded));
    S32 nPicks = 0;
    iSndXboxEntry cutsceneFmt[64];
    U32 cutsceneIds[64];
    S32 nCutsceneFmt = 0;

    for (S32 i = 0; i < n; i++)
    {
        if (assets[i].type != 'SNDI')
        {
            continue;
        }

        U8* raw = readAsset(&assets[i]);
        U32 count = 0;
        const iSndXboxEntry* e =
            raw != NULL ? iSndXboxTableEntries(raw, assets[i].size, &count) : NULL;
        CHECK(e != NULL, "SNDI %08x (%u bytes) is not 12 + 44n", assets[i].aid, assets[i].size);
        if (e == NULL)
        {
            free(raw);
            continue;
        }

        const iSndXboxTable* t = (const iSndXboxTable*)raw;
        U32 matched = 0, cutscene = 0, adpcm = 0, pcm = 0, stereo = 0, loops = 0;

        for (U32 k = 0; k < count; k++)
        {
            const iSndHipAsset* a = findAsset(assets, n, e[k].assetID);
            adpcm += e[k].format_tag == ISNDXBOX_FORMAT_ADPCM;
            pcm += e[k].format_tag == ISNDXBOX_FORMAT_PCM;
            stereo += e[k].channels == 2;
            loops += e[k].loop_flag != 0;

            if (a == NULL)
            {
                // A cutscene track (CSSS): no bytes of its own.
                cutscene++;
                if (e[k].format_tag == ISNDXBOX_FORMAT_PCM && nCutsceneFmt < 64)
                {
                    cutsceneIds[nCutsceneFmt] = e[k].assetID;
                    cutsceneFmt[nCutsceneFmt++] = e[k];
                }
                continue;
            }

            CHECK(a->size == e[k].data_size, "%08x: asset is %u bytes, the table says %u",
                  e[k].assetID, a->size, e[k].data_size);
            CHECK((k < t->num_sfx) == (a->type == 'SND '),
                  "%08x: entry %u is in the %s section "
                  "but the asset is %.4s",
                  e[k].assetID, k, k < t->num_sfx ? "sound" : "stream", (const char*)&a->type);
            matched++;
        }

        printf("SNDI %08x: %u sounds, %u streams, %u cutscene; %u entries matched to assets, %u "
               "cutscene tracks; ADPCM %u, PCM %u, stereo %u, looping %u\n",
               assets[i].aid, t->num_sfx, t->num_streams, t->num_cutscene, matched, cutscene, adpcm,
               pcm, stereo, loops);

        // The sounds to decode: the largest stream, a stereo one, then the
        // first sound effects.
        S32 largest = -1, stereoPick = -1;
        for (U32 k = 0; k < count; k++)
        {
            if (findAsset(assets, n, e[k].assetID) == NULL)
            {
                continue;
            }
            if (k >= t->num_sfx && (largest < 0 || e[k].data_size > e[largest].data_size))
            {
                largest = (S32)k;
            }
            if (e[k].channels == 2 && stereoPick < 0)
            {
                stereoPick = (S32)k;
            }
        }

        S32 order[64];
        S32 nOrder = 0;
        if (largest >= 0)
        {
            order[nOrder++] = largest;
        }
        if (stereoPick >= 0 && stereoPick != largest)
        {
            order[nOrder++] = stereoPick;
        }
        for (U32 k = 0; k < count && nOrder < 64; k++)
        {
            if ((S32)k != largest && (S32)k != stereoPick && findAsset(assets, n, e[k].assetID))
            {
                order[nOrder++] = (S32)k;
            }
        }

        // ---- 2. decoding ---------------------------------------------------
        for (S32 j = 0; j < nOrder && nPicks < wantSounds; j++)
        {
            const iSndXboxEntry* x = &e[order[j]];
            const iSndHipAsset* a = findAsset(assets, n, x->assetID);
            U8* data = readAsset(a);
            if (data == NULL)
            {
                CHECK(false, "%08x: could not read", x->assetID);
                continue;
            }

            U32 bytes = 0;
            double t0 = nowSec();
            S16* out = (S16*)iSndXboxDecode(data, a->size, x->format_tag, x->channels,
                                            x->block_align, x->bits_per_sample, &bytes);
            double dt = nowSec() - t0;
            free(data);

            CHECK(out != NULL, "%08x: format %#x did not decode", x->assetID, x->format_tag);
            if (out == NULL)
            {
                continue;
            }

            U32 ch = x->channels != 0 ? x->channels : 1;
            U32 frames = bytes / (2 * ch);
            CHECK(frames == iSndXboxFrames(x), "%08x: decoded %u frames, the table implies %u",
                  x->assetID, frames, iSndXboxFrames(x));

            double secs = (double)frames / (double)x->samples_per_sec;
            printf("  %08x %-6s %s %u ch %5u Hz %8u frames (%6.2f s)%s  decoded in %.1f ms (%.0fx "
                   "real time)\n",
                   x->assetID, order[j] < (S32)t->num_sfx ? "sound" : "stream",
                   x->format_tag == ISNDXBOX_FORMAT_ADPCM ? "ADPCM" : "PCM  ", ch,
                   x->samples_per_sec, frames, secs, x->loop_flag ? " loop" : "", dt * 1000.0,
                   dt > 0 ? secs / dt : 0.0);

            char path[512];
            snprintf(path, sizeof(path), "%s/%08x.wav", outDir, x->assetID);
            CHECK(iSndXboxWriteWav(path, out, frames, ch, x->samples_per_sec), "write %s", path);

            picks[nPicks].aid = x->assetID;
            picks[nPicks].fmt = *x;
            picks[nPicks].pcm = out;
            picks[nPicks].frames = frames;
            nPicks++;
        }

        free(raw);
    }

    // ---- 3. the mixer -------------------------------------------------------
    iSndHostInit();
    printf("backend: %s\n", iSndHostName());

    if (device && nPicks > 0)
    {
        // Real time on the device's clock, at zero gain.
        S32 voices[3];
        F32 expect[3];
        F32 ended[3];
        S32 nv = 0;
        double t0 = nowSec();

        for (S32 k = 0; k < 3 && k < nPicks; k++)
        {
            decoded* d = &picks[k];
            U32 ch = d->fmt.channels != 0 ? d->fmt.channels : 1;
            S32 v = iSndHostAcquire(0);
            if (v < 0)
            {
                break;
            }

            iSndHostSetVol(v, 0.0f, 0.0f);
            expect[nv] = (F32)d->frames / (F32)d->fmt.samples_per_sec;
            ended[nv] = -1.0f;

            if (k == 0)
            {
                // Fed: the whole sound at once, then closed.
                iSndHostStartFeed(v, ch, d->fmt.samples_per_sec, d->frames + 1024, false);
                iSndHostFeed(v, d->pcm, d->frames);
                iSndHostFeedEnd(v);
            }
            else
            {
                iSndHostSample s;
                memset(&s, 0, sizeof(s));
                s.data = d->pcm;
                s.bytes = d->frames * 2 * ch;
                s.channels = ch;
                s.bits = 16;
                s.sample_rate = d->fmt.samples_per_sec;
                s.num_samples = d->frames;
                iSndHostStart(v, &s);
            }
            voices[nv++] = v;
        }

        F32 longest = 0.0f;
        for (S32 k = 0; k < nv; k++)
        {
            longest = expect[k] > longest ? expect[k] : longest;
        }
        if (longest > 20.0f)
        {
            longest = 20.0f;
        }

        while (nowSec() - t0 < longest + 1.0)
        {
            iSndHostUpdate();
            for (S32 k = 0; k < nv; k++)
            {
                if (ended[k] < 0.0f && !iSndHostIsPlaying(voices[k]))
                {
                    ended[k] = (F32)(nowSec() - t0);
                }
            }
            iHostSleepUntilNs(iHostMonotonicNs() + 5000000ULL);
        }

        for (S32 k = 0; k < nv; k++)
        {
            bool cut = expect[k] > 20.0f;
            printf("device: voice %d (%s) length %.3f s, ended at %s%.3f s\n", k,
                   k == 0 ? "fed" : "sample", expect[k], ended[k] < 0 ? "> " : "",
                   ended[k] < 0 ? longest + 1.0f : ended[k]);
            if (!cut)
            {
                CHECK(ended[k] > expect[k] - 0.05f && ended[k] < expect[k] + 0.25f,
                      "device voice %d ended at %.3f s, not %.3f s", k, ended[k], expect[k]);
            }
            iSndHostStop(voices[k]);
            iSndHostRelease(voices[k]);
        }
    }
    else if (nPicks > 0)
    {
        static const iSndHostReverb kCave = { -1000,  0,     0.0f,   1.5f,   1.0f,   -1363,
                                              0.008f, -1153, 0.011f, 100.0f, 100.0f, 5000.0f };
        iSndHostSetReverb(&kCave);

        S32 voices[5];
        S32 nv = 0;
        for (S32 k = 0; k < 5; k++)
        {
            decoded* d = &picks[k % nPicks];
            S32 v = iSndHostAcquire(0);
            CHECK(v >= 0, "no host voice");
            if (v < 0)
            {
                break;
            }

            iSndHostSample s;
            memset(&s, 0, sizeof(s));
            s.data = d->pcm;
            s.bytes = d->frames * 2 * (d->fmt.channels != 0 ? d->fmt.channels : 1);
            s.channels = d->fmt.channels != 0 ? d->fmt.channels : 1;
            s.bits = 16;
            s.sample_rate = d->fmt.samples_per_sec;
            s.num_samples = d->frames;
            s.looping = (k == 3);

            iSndHostSetVol(v, k == 2 ? 0.5f : 0.25f, k == 2 ? 0.0f : 0.25f);
            iSndHostSetPitch(v, k == 1 ? 2.0f : 1.0f);
            iSndHostSetSend(v, k == 4);
            iSndHostStart(v, &s);
            voices[nv++] = v;
        }

        U32 total = MIX_RATE * 4;
        float* mix = (float*)calloc((size_t)total * 2, sizeof(float));
        for (U32 at = 0; at < total; at += 1024)
        {
            U32 block = total - at < 1024 ? total - at : 1024;
            iSndHostTestMix(MIX_RATE, mix + (size_t)at * 2, block);
        }

        float peak = peakOf(mix, total * 2);
        printf("mix: 4 s of %d voices at %u Hz, peak %.3f; looping voice still playing: %s\n", nv,
               MIX_RATE, peak, nv > 3 && iSndHostIsPlaying(voices[3]) ? "yes" : "no");
        CHECK(peak > 0.001f, "the mix is silent");
        CHECK(nv <= 3 || iSndHostIsPlaying(voices[3]), "the looping voice ended");

        char path[512];
        snprintf(path, sizeof(path), "%s/mix.wav", outDir);
        writeMix(path, mix, total, MIX_RATE);
        free(mix);

        for (S32 k = 0; k < nv; k++)
        {
            iSndHostStop(voices[k]);
            iSndHostRelease(voices[k]);
        }
        iSndHostSetReverb(NULL);
    }

    // ---- 5. the volume groups ----------------------------------------------
    if (!device)
    {
        S32 before = sFailures;
        groupGainTest(ISNDHOST_GROUP_MUSIC, false);
        groupGainTest(ISNDHOST_GROUP_MOVIE, true);
        printf("volume groups: %s\n", sFailures == before ? "as they should be" : "FAILED");
    }

    // ---- 4. cutscenes -------------------------------------------------------
    for (S32 i = 0; i < n; i++)
    {
        if (assets[i].type != 'CSN ')
        {
            continue;
        }

        U8* csn = readAsset(&assets[i]);
        if (csn == NULL || assets[i].size < 0x830)
        {
            CHECK(false, "CSN %08x unreadable", assets[i].aid);
            free(csn);
            continue;
        }

        U32 hdr[12];
        memcpy(hdr, csn, sizeof(hdr));
        U32 numData = hdr[2], numTime = hdr[3];
        U32 trackL, trackR;
        memcpy(&trackL, csn + 0x30, 4);
        memcpy(&trackR, csn + 0x34, 4);

        const U32* tco = (const U32*)(csn + 0x830 + 16 * numData);
        const iSndXboxEntry* fmt = NULL;
        for (S32 k = 0; k < nCutsceneFmt; k++)
        {
            if (cutsceneIds[k] == trackL)
            {
                fmt = &cutsceneFmt[k];
            }
        }

        CHECK(fmt != NULL, "CSN %08x: track %08x is not in the package's sound table",
              assets[i].aid, trackL);
        if (fmt == NULL)
        {
            free(csn);
            continue;
        }

        U32 ch = fmt->channels != 0 ? fmt->channels : 1;
        U32 frameBytes = 2 * ch;
        S32 v = iSndHostAcquire(0);
        iSndHostStartFeed(v, ch, fmt->samples_per_sec, 1U << 19, false);
        iSndHostSetVol(v, 1.0f, 1.0f);

        float endTime;
        memcpy(&endTime, csn + tco[numTime - 1] + 4, 4);
        U32 total = (U32)((endTime + 1.0f) * MIX_RATE);
        float* mix = (float*)calloc((size_t)total * 2, sizeof(float));

        // The last chunk with audio: after it the track is silence by design
        // (a few cutscenes end on picture alone), not an underrun.
        U32 lastAudioChunk = 0;
        for (U32 k = 0; k < numTime; k++)
        {
            U32 sz = 0;
            if (iSndXboxCsnAudio(csn + tco[k], tco[k + 1] - tco[k], trackL, &sz) != NULL)
            {
                lastAudioChunk = k;
            }
        }

        // Chunk k is readable from the time the picture enters chunk k - 1
        // (the game reads one chunk ahead into the Stream buffer); chunk 0 and
        // 1 from the start.
        U32 nextChunk = 0;
        U32 fedFrames = 0;
        U32 underruns = 0;
        U32 chunksWithAudio = 0;
        bool ended = false;

        for (U32 at = 0; at < total; at += 512)
        {
            float t = (float)at / MIX_RATE;
            while (nextChunk < numTime)
            {
                float avail = 0.0f;
                if (nextChunk >= 2)
                {
                    memcpy(&avail, csn + tco[nextChunk - 1], 4); // StartTime of chunk k-1
                }
                if (avail > t || iSndHostFeedQueued(v) + (1U << 17) > (1U << 19))
                {
                    break;
                }

                U32 size = 0;
                const U8* p = (const U8*)iSndXboxCsnAudio(
                    csn + tco[nextChunk], tco[nextChunk + 1] - tco[nextChunk], trackL, &size);
                if (p != NULL)
                {
                    U32 frames = size / frameBytes;
                    U32 took = iSndHostFeed(v, (const S16*)p, frames);
                    CHECK(took == frames, "CSN %08x chunk %u: the ring took %u of %u frames",
                          assets[i].aid, nextChunk, took, frames);
                    fedFrames += took;
                    chunksWithAudio++;
                }
                nextChunk++;
            }

            if (nextChunk >= numTime && !ended)
            {
                iSndHostFeedEnd(v);
                ended = true;
            }

            U32 block = total - at < 512 ? total - at : 512;
            U32 need = (U32)((double)block * fmt->samples_per_sec / MIX_RATE) + 2;
            if (nextChunk <= lastAudioChunk && iSndHostIsPlaying(v) && iSndHostFeedQueued(v) < need)
            {
                underruns++;
            }

            iSndHostTestMix(MIX_RATE, mix + (size_t)at * 2, block);
        }

        double trackSecs = (double)fedFrames / fmt->samples_per_sec;
        printf("CSN %08x: %u chunks (%u with audio), %.2f s of picture, track %08x %u ch %u Hz "
               "%.2f s fed, underruns %u, finished: %s\n",
               assets[i].aid, numTime, chunksWithAudio, endTime, trackL, ch, fmt->samples_per_sec,
               trackSecs, underruns, iSndHostIsPlaying(v) ? "no" : "yes");
        CHECK(underruns == 0, "CSN %08x: %u underruns", assets[i].aid, underruns);
        CHECK(chunksWithAudio > 0 && fedFrames > 0, "CSN %08x: no audio", assets[i].aid);
        CHECK(trackSecs > endTime - 2.5 && trackSecs < endTime + 1.5,
              "CSN %08x: %.2f s of audio for %.2f s of picture", assets[i].aid, trackSecs, endTime);
        CHECK(peakOf(mix, total * 2) > 0.001f, "CSN %08x: silent mix", assets[i].aid);

        char path[512];
        snprintf(path, sizeof(path), "%s/csn_%08x.wav", outDir, assets[i].aid);
        writeMix(path, mix, total, MIX_RATE);

        iSndHostStop(v);
        iSndHostRelease(v);
        free(mix);
        free(csn);
    }

    iSndHostExit();

    for (S32 k = 0; k < nPicks; k++)
    {
        free(picks[k].pcm);
    }
    free(picks);
    free(assets);
    fclose(sPkg);

    printf("%s: %d failure(s)\n", sFailures == 0 ? "PASS" : "FAIL", sFailures);
    return sFailures == 0 ? 0 : 1;
}
