#include "iSndHost.h"
#include "iAudioSDL.h"
#include "iAudioSDLTest.h"
#include "iSndMeter.h"
#include "iHost.h"
#include "iSndReverb.h"

#include <SDL3/SDL.h>
#ifdef __3DS__
#include <3ds.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The audio backend: a software mixer feeding one SDL audio stream.
//
// The seam in iSndHost.h describes a device with 64 independent voices, each
// with its own rate, volume per side and pitch. No host audio API offers that
// directly -- SDL renders one stream, and the APIs that do offer voices
// (XAudio2, DirectSound) bring a submix graph, their own threading model and
// their own idea of what a voice is. Mixing here instead is both less code and
// a closer fit: the game's model becomes 64 float accumulations per output
// frame, and the only thing SDL is asked for is somewhere to put the result.
//
// It also makes the pitch and rate handling exact rather than approximate.
// Every voice is resampled to the device rate by stepping a fractional read
// position, so a sound authored at 22050 Hz played at pitch 1.3 on a 48 kHz
// device is one multiply, and the point where it runs out is known to the
// sample rather than to the frame.
//
// The stream is opened at the device's own rate in float stereo, so SDL has no
// conversion left to do on the way out. Which driver, what the hardware format
// is, and how stereo reaches a mono endpoint are all below the stream and all
// SDL's.
//
// **Failing to open a device is not an error.** A machine with no output, or
// with the endpoint in use exclusively, still has to run the game correctly,
// and a great deal of BFBB waits on sounds -- dialogue lines, cutscene beats,
// NPC barks. So the mixer runs either way: with a device the voices advance
// because the mixer consumed them, and without one they advance on the
// monotonic clock in iSndHostUpdate. The game cannot tell the difference, which
// is the whole point of iSndHostNull.cpp and is preserved here rather than
// replaced.
//
// TSSM-ADAPTED: a reverb send per voice (the reverb runs on a send bus, not on
// the whole mix), a grouped pause, and fed voices that play from a ring the
// game thread fills (iSndHost.h).
//
// TSSM: the device is closed and opened again when the app goes to the
// background and comes back, when a playback device is removed, and when it
// stops asking for audio, all through iAudioSDL.h. The voices carry on across
// it: on the silent path's clock while there is no device, and from where
// they were once there is one again.

// ---------------------------------------------------------------------------
// Voices

struct hvoice
{
    bool acquired;
    bool playing;
    bool paused;
    bool looping;

    // Borrowed from iSndData's cache, which pins it for as long as the voice
    // holds it. NULL is a legal voice: it plays silently and still ends at the
    // right moment, which is what a missing or unreadable asset sounds like.
    const void* data;
    U32 frames;
    U32 channels;
    U32 bits;
    U32 rate;
    U32 loop_start;
    U32 loop_end;

    // Read position in source frames. Fractional because the source rate and
    // the device rate are unrelated.
    double pos;

    // Where the mix is heading, and where it is now. Ramped across a block
    // rather than applied at its start: the game moves volumes every frame as
    // an emitter approaches, and stepping the gain discontinuously at a block
    // boundary is audible as a click on exactly the sounds that move most.
    float volL;
    float volR;
    float curL;
    float curR;

    float pitch;

    // TSSM: mixed into the reverb's send bus rather than the dry mix.
    bool send;

    // TSSM: whose level it takes (iSndHostSetGroup).
    iSndHostGroup group;

    // TSSM: a fed voice. `ring` holds `ring_cap` frames (a power of two) of
    // 16-bit PCM; `written` counts every frame ever fed, and `pos` is the read
    // position on the same count, so the frames queued are written - pos.
    bool feed;
    bool feed_ended;
    S16* ring;
    U32 ring_cap;
    U64 written;

    // TSSM: with no device, the moment this voice was last advanced to. Per
    // voice, so that a voice started after a long gap between updates is not
    // advanced by time from before it started.
    U64 silent_ns;
};

static hvoice sVoices[ISNDHOST_MAX_VOICES];
static SDL_Mutex* sLock;

// ---------------------------------------------------------------------------
// Device

static SDL_AudioStream* sStream;
static bool sSubsystem;

// The rate the stream runs at, which is the device's own. Also the rate the
// silent path steps voices at when there is no device at all.
static U32 sOutRate;

// What SDL says the device wants per callback. Reported in the backend name and
// used to size the mix buffer. The callback is free to ask for more than this,
// which the mix loop handles by running more than once.
static U32 sDeviceFrames;

static bool sDeviceUp;
static char sName[128];

// The silent path's clock. Only read and written by iSndHostUpdate, on the game
// thread, and only when there is no device.
static U64 sLastSilentNs;

// Scratch for one mixed block, in float. Sized on the endpoint buffer, which
// shared-mode WASAPI reports once at initialisation.
static float* sMixBuffer;
static U32 sMixBufferFrames;

// TSSM: the reverb's send bus, the same size. The voices with a send are mixed
// here, the reverb adds its wet signal to it in place, and the result joins
// the dry mix.
static float* sSendBuffer;

// The environmental reverb the game last asked for. Kept here as well as in
// iSndReverb because the reverb's delay lengths depend on the output rate,
// which is not known until a device opens and changes if one is swapped: the
// game sets this once when a scene loads and must not have to set it again.
static iSndHostReverb sReverb;
static bool sReverbWanted;

// TSSM: the groups' levels (iSndHostSetGroupGain), in 1/65536ths: set by the
// game thread and read by the mixer once a block, so neither waits for the
// other. sGroupNow, where each has got to on its way there, is the mixer's.
static const int kGroupGainOne = 65536;
static SDL_AtomicInt sGroupGain[ISNDHOST_GROUP_COUNT] = { { kGroupGainOne },
                                                          { kGroupGainOne },
                                                          { kGroupGainOne } };
static float sGroupNow[ISNDHOST_GROUP_COUNT] = { 1.0f, 1.0f, 1.0f };

// TSSM: whether the device restarts through iAudioSDL.h (not under
// TSSM_AUDIO=0).
static bool sAttached;

// TSSM_TEST_AUDIO_RESTART's level lines (iAudioSDLTest.h): the loudest sample
// the callback has sent SDL since the game thread last asked, in millionths.
// Measured only once a test has turned it on.
static SDL_AtomicInt sTestMeterOn;
static SDL_AtomicInt sTestPeak;

// TSSM: the stall check. The callback counts itself; the game thread notes
// when the count last moved, and a device that has not asked for audio for a
// second while the game was running is restarted. That is what a stream SDL
// could not start again looks like: no event, no error, just no callbacks.
static SDL_AtomicInt sCallbacks;
static int sSeenCallbacks;
static U64 sAliveNs;
static U64 sLastUpdateNs;
static const U64 kStallNs = 1000000000ULL;

// TSSM: the 3DS spike's count of the callback's own time
// (iSndHostMixStatsTake). Written by the callback under sLock.
static iSndHostMixStats sMixStats;

// TSSM_AUDIO_PROF=1 (a measurement): the mixer's parts, a line every 2 s from
// the audio thread. Nanoseconds and counts since the last line.
static int sProfOn = -1;
static U64 sProfVoiceNs, sProfReverbNs, sProfRestNs, sProfLockWaitNs, sProfLast;
static U64 sProfVoiceFrames, sProfFeedFrames, sProfBlocks, sProfVoicesSum, sProfSendBlocks;
static U32 sProfVoicesMax;
static U64 sProfGameWaitNs, sProfGameLocks, sProfGameWaitMaxNs;

// The game thread's lock, its wait timed under TSSM_AUDIO_PROF.
static void iLockGame()
{
    if (sProfOn <= 0)
    {
        SDL_LockMutex(sLock);
        return;
    }
    const U64 t0 = iHostMonotonicNs();
    SDL_LockMutex(sLock);
    const U64 w = iHostMonotonicNs() - t0;
    sProfGameWaitNs += w;
    sProfGameLocks++;
    if (w > sProfGameWaitMaxNs)
    {
        sProfGameWaitMaxNs = w;
    }
}

// ---------------------------------------------------------------------------
// Mixing

static bool iVoiceValid(S32 voice)
{
    return voice >= 0 && voice < ISNDHOST_MAX_VOICES && sVoices[voice].acquired;
}

// The seam does not promise that iSndHostInit runs first -- the null backend
// tolerates being called cold, so this one has to as well. Only the game thread
// reaches these entry points, so no lock is needed to make the lock. If SDL
// will not give us a mutex, every lock below becomes a no-op, which is the
// right answer while there is no audio thread to race against.
static void iEnsureLock()
{
    if (sLock == NULL)
    {
        sLock = SDL_CreateMutex();
    }
}

// One source frame, as a float in -1..1, with linear interpolation between the
// two frames it falls between. `ch` is 0 for left and 1 for right; a mono
// source answers the same value to both.
static inline float iSampleAt(const hvoice* v, double pos, U32 ch)
{
    if (v->feed)
    {
        // The ring: frames [written - ring_cap, written) are in it. The read
        // position never passes `written`, and the writer never overwrites a
        // frame the reader has not passed (iSndHostFeed).
        U64 i0 = (U64)pos;
        if (v->ring == NULL || i0 >= v->written)
        {
            return 0.0f;
        }

        U64 i1 = i0 + 1 < v->written ? i0 + 1 : i0;
        float frac = (float)(pos - (double)i0);
        U32 c = (v->channels > 1 && ch < v->channels) ? ch : 0;
        U32 mask = v->ring_cap - 1;
        float a = (float)v->ring[(size_t)(i0 & mask) * v->channels + c] * (1.0f / 32768.0f);
        float b = (float)v->ring[(size_t)(i1 & mask) * v->channels + c] * (1.0f / 32768.0f);
        return a + (b - a) * frac;
    }

    if (v->data == NULL)
    {
        return 0.0f;
    }

    U32 i0 = (U32)pos;
    if (i0 >= v->frames)
    {
        return 0.0f;
    }

    U32 i1 = i0 + 1;
    if (i1 >= v->frames)
    {
        // Past the end, hold the last frame rather than wrapping into whatever
        // follows in the cache.
        i1 = i0;
    }

    float frac = (float)(pos - (double)i0);
    U32 c = (v->channels > 1 && ch < v->channels) ? ch : 0;
    U32 stride = v->channels;

    if (v->bits == 16)
    {
        const S16* p = (const S16*)v->data;
        float a = (float)p[i0 * stride + c] * (1.0f / 32768.0f);
        float b = (float)p[i1 * stride + c] * (1.0f / 32768.0f);
        return a + (b - a) * frac;
    }

    const U8* p = (const U8*)v->data;
    float a = ((float)p[i0 * stride + c] - 128.0f) * (1.0f / 128.0f);
    float b = ((float)p[i1 * stride + c] - 128.0f) * (1.0f / 128.0f);
    return a + (b - a) * frac;
}

// Advance one voice past the end of its data, looping if it should. Returns
// false when the voice has finished.
static bool iWrap(hvoice* v)
{
    // A looping voice may turn round before the data runs out, which is what a
    // soundtrack override does. Everything else ends where the samples do.
    U32 end = v->frames;
    if (v->looping && v->loop_end != 0 && v->loop_end <= v->frames)
    {
        end = v->loop_end;
    }

    if (v->pos < (double)end)
    {
        return true;
    }

    if (!v->looping || end == 0)
    {
        return false;
    }

    U32 start = v->loop_start < end ? v->loop_start : 0;
    double span = (double)(end - start);

    if (span <= 0.0)
    {
        v->pos = (double)start;
        return true;
    }

    // A loop shorter than one output step would spin here, so this subtracts
    // repeatedly rather than once.
    while (v->pos >= (double)end)
    {
        v->pos -= span;
    }

    return true;
}

// TSSM: where each group's level is at the start of a block and at its end. It
// moves full scale in 20 ms at most, whatever the size of the block, so that no
// slider can step the signal: a device with a small period would otherwise make
// a jump to zero a click. A level that has arrived stays exactly where it is.
static void iStepGroupGains(U32 frames, float* from, float* to)
{
    const float most = (float)frames / (0.02f * (float)sOutRate);

    for (S32 g = 0; g < ISNDHOST_GROUP_COUNT; g++)
    {
        const float want = (float)SDL_GetAtomicInt(&sGroupGain[g]) / (float)kGroupGainOne;
        const float now = sGroupNow[g];

        from[g] = now;
        to[g] = want > now + most ? now + most : (want < now - most ? now - most : want);
        sGroupNow[g] = to[g];
    }
}

// TSSM: one fed voice's block. It reads up to the last frame fed and then
// holds, rather than wrapping or ending, unless the feed has been closed.
// (The mixer as it was: TSSM_AUDIO_CHECK's reference, below.)
static void iMixFeedRef(hvoice* v, float* out, U32 frames, double step, float from, float to)
{
    float gL = v->curL * from;
    float gR = v->curR * from;
    float dL = (v->volL * to - gL) / (float)frames;
    float dR = (v->volR * to - gR) / (float)frames;

    U32 n = 0;
    for (; n < frames; n++)
    {
        if (v->pos + 1.0 > (double)v->written)
        {
            // Dry. The position stays where it is, so a late frame is played
            // late rather than skipped. A closed feed has finished.
            if (v->feed_ended)
            {
                v->pos = (double)v->written;
                v->playing = false;
            }
            break;
        }

        out[n * 2 + 0] += iSampleAt(v, v->pos, 0) * gL;
        out[n * 2 + 1] += iSampleAt(v, v->pos, 1) * gR;

        v->pos += step;
        gL += dL;
        gR += dR;
    }

    v->curL = v->volL;
    v->curR = v->volR;
}

// The voices' block into `mix` and `send`, as the mixer did it before the
// loops below: kept as TSSM_AUDIO_CHECK's reference, which runs it on a copy
// of the voices and compares.
static void iMixVoicesRef(hvoice* voices, float* mix, float* send, U32 frames,
                          const float* groupFrom, const float* groupTo)
{
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &voices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }

        float* out = mix;
        float from = groupFrom[v->group];
        float to = groupTo[v->group];
        if (v->send && send != NULL)
        {
            out = send;
            from = 1.0f;
            to = 1.0f;
        }

        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        if (!(step > 0.0) || !(step < 1e6))
        {
            step = 0.0;
        }

        if (v->feed)
        {
            iMixFeedRef(v, out, frames, step, from, to);
            continue;
        }

        if (v->data == NULL)
        {
            v->pos += step * (double)frames;
            if (!iWrap(v))
            {
                v->playing = false;
            }
            continue;
        }

        float gL = v->curL * from;
        float gR = v->curR * from;
        float dL = (v->volL * to - gL) / (float)frames;
        float dR = (v->volR * to - gR) / (float)frames;

        U32 n = 0;
        for (; n < frames; n++)
        {
            if (!iWrap(v))
            {
                v->playing = false;
                break;
            }

            out[n * 2 + 0] += iSampleAt(v, v->pos, 0) * gL;
            out[n * 2 + 1] += iSampleAt(v, v->pos, 1) * gR;

            v->pos += step;
            gL += dL;
            gR += dR;
        }

        if (n == frames)
        {
            v->curL = v->volL;
            v->curR = v->volR;
            if (!iWrap(v))
            {
                v->playing = false;
            }
        }
        else
        {
            v->curL = gL;
            v->curR = gR;
        }
    }
}

// TSSM: the loops below give the same samples as iSampleAt and iWrap, to the
// bit: the same double position stepped the same way, the same float sums in
// the same order. What they leave out is the work around them that was done
// per frame per channel: the end and loop checks folded into one compare
// against the block's end, the position turned into a frame index once per
// frame instead of twice, the format's branches taken once per block, and a
// fed voice's 64-bit read position (a library call each way on the 3DS)
// counted from a whole frame taken once per block.

// 16-bit frames with `CH` channels (1 or 2), from `n` while the position stays
// below `endD`. Returns where it stopped.
template <U32 CH>
static inline U32 iRun16(const S16* p, U32 last, double& pos, double endD, double step, float* out,
                         U32 n, U32 frames, float& gL, float& gR, float dL, float dR)
{
    const float k = 1.0f / 32768.0f;
    for (; n < frames && pos < endD; n++)
    {
        const U32 i0 = (U32)pos;
        const U32 i1 = i0 < last ? i0 + 1 : i0;
        const float frac = (float)(pos - (double)i0);
        const float a0 = (float)p[i0 * CH] * k;
        const float b0 = (float)p[i1 * CH] * k;
        const float s0 = a0 + (b0 - a0) * frac;
        float s1 = s0;
        if (CH == 2)
        {
            const float a1 = (float)p[i0 * CH + 1] * k;
            const float b1 = (float)p[i1 * CH + 1] * k;
            s1 = a1 + (b1 - a1) * frac;
        }
        out[n * 2 + 0] += s0 * gL;
        out[n * 2 + 1] += s1 * gR;
        pos += step;
        gL += dL;
        gR += dR;
    }
    return n;
}

// Any other format, through iSampleAt.
static inline U32 iRunAny(hvoice* v, double& pos, double endD, double step, float* out, U32 n,
                          U32 frames, float& gL, float& gR, float dL, float dR)
{
    for (; n < frames && pos < endD; n++)
    {
        out[n * 2 + 0] += iSampleAt(v, pos, 0) * gL;
        out[n * 2 + 1] += iSampleAt(v, pos, 1) * gR;
        pos += step;
        gL += dL;
        gR += dR;
    }
    return n;
}

// A sound voice's block: TRUE while it is still playing.
static bool iMixSound(hvoice* v, float* out, U32 frames, double step, float from, float to)
{
    float gL = v->curL * from;
    float gR = v->curR * from;
    float dL = (v->volL * to - gL) / (float)frames;
    float dR = (v->volR * to - gR) / (float)frames;

    // iWrap's end, which nothing changes during the block (the lock is held).
    U32 end = v->frames;
    if (v->looping && v->loop_end != 0 && v->loop_end <= v->frames)
    {
        end = v->loop_end;
    }
    const double endD = (double)end;
    const U32 kind = v->bits == 16 && v->channels == 1 ? 1 : v->bits == 16 && v->channels == 2 ? 2 : 0;
    const S16* p16 = (const S16*)v->data;
    const U32 last = v->frames - 1; // i1 = i0 + 1 below the last frame

    double pos = v->pos;
    U32 n = 0;
    bool ended = false;
    while (n < frames)
    {
        if (!(pos < endD))
        {
            v->pos = pos;
            // A position iWrap could not bring below the end (only NaN can do
            // that) ends the voice rather than spinning here under the lock.
            if (!iWrap(v) || !(v->pos < endD))
            {
                ended = true;
                break;
            }
            pos = v->pos;
        }
        if (kind == 1)
        {
            n = iRun16<1>(p16, last, pos, endD, step, out, n, frames, gL, gR, dL, dR);
        }
        else if (kind == 2)
        {
            n = iRun16<2>(p16, last, pos, endD, step, out, n, frames, gL, gR, dL, dR);
        }
        else
        {
            n = iRunAny(v, pos, endD, step, out, n, frames, gL, gR, dL, dR);
        }
    }
    v->pos = pos;

    iSndMeterFrames(iSNDMETER_MIX_SOUNDS, n);

    // Only claim to have reached the target gain if the whole block ran;
    // a voice that ended early keeps where its ramp got to, which matters
    // if the same slot is restarted before the next block.
    if (!ended)
    {
        v->curL = v->volL;
        v->curR = v->volR;

        // The loop tests for the end BEFORE reading, which it has to. A
        // voice whose last frame is the block's last frame would therefore
        // stay "playing" until the next block noticed -- a whole endpoint
        // period of slack in every timing the game derives from a sound.
        // Settle it here instead.
        return iWrap(v);
    }
    v->curL = gL;
    v->curR = gR;
    return false;
}

// TSSM: one fed voice's block. It reads up to the last frame fed and then
// holds, rather than wrapping or ending, unless the feed has been closed.
static void iMixFeedLocked(hvoice* v, float* out, U32 frames, double step, float from, float to)
{
    float gL = v->curL * from;
    float gR = v->curR * from;
    float dL = (v->volL * to - gL) / (float)frames;
    float dR = (v->volR * to - gR) / (float)frames;

    const double writtenD = (double)v->written;
    double pos = v->pos;
    U32 n = 0;
    if (v->ring == NULL)
    {
        // iSampleAt's silence, with the clock still running.
        for (; n < frames && !(pos + 1.0 > writtenD); n++)
        {
            out[n * 2 + 0] += 0.0f * gL;
            out[n * 2 + 1] += 0.0f * gR;
            pos += step;
            gL += dL;
            gR += dR;
        }
    }
    else
    {
        // Frame i0 = base + (U32)(pos - baseD): pos - baseD is exact (both
        // are whole multiples of pos's unit), and so is the fraction.
        const U64 base = (U64)pos;
        const double baseD = (double)base;
        const U64 lastFed = v->written - 1;
        const U32 mask = v->ring_cap - 1;
        const U32 ch = v->channels;
        const U32 c1 = ch > 1 ? 1 : 0;
        const S16* ring = v->ring;
        const float k = 1.0f / 32768.0f;
        for (; n < frames && !(pos + 1.0 > writtenD); n++)
        {
            const double l = pos - baseD;
            const U32 li = (U32)l;
            const U64 i0 = base + li;
            const U64 i1 = i0 < lastFed ? i0 + 1 : i0;
            const float frac = (float)(l - (double)li);
            const S16* f0 = ring + (size_t)(i0 & mask) * ch;
            const S16* f1 = ring + (size_t)(i1 & mask) * ch;
            const float a0 = (float)f0[0] * k;
            const float b0 = (float)f1[0] * k;
            const float a1 = (float)f0[c1] * k;
            const float b1 = (float)f1[c1] * k;
            out[n * 2 + 0] += (a0 + (b0 - a0) * frac) * gL;
            out[n * 2 + 1] += (a1 + (b1 - a1) * frac) * gR;
            pos += step;
            gL += dL;
            gR += dR;
        }
    }
    if (n < frames && v->feed_ended)
    {
        // Dry and closed: finished. (Dry and open: the position stays where
        // it is, so a late frame is played late rather than skipped.)
        pos = writtenD;
        v->playing = false;
    }
    v->pos = pos;

    v->curL = v->volL;
    v->curR = v->volR;

    iSndMeterFrames(iSNDMETER_MIX_FEEDS, n);
}

// TSSM_AUDIO_CHECK=1 (a check): every block mixed again by iMixVoicesRef from
// a copy of the voices taken before it, and the two compared to the bit, the
// buses and where each voice got to. A line every 2 s from the audio thread.
static int sCheckOn = -1;
static hvoice sCheckVoices[ISNDHOST_MAX_VOICES];
static float* sCheckMix;
static float* sCheckSend;
static U32 sCheckFrames;
static U64 sCheckBlocks, sCheckBad, sCheckVoiceBad, sCheckSamples, sCheckLast;

static bool iCheckBegin(U32 frames)
{
    if (sCheckOn < 0)
    {
        const char* e = getenv("TSSM_AUDIO_CHECK");
        sCheckOn = e != NULL && e[0] == '1' ? 1 : 0;
    }
    if (sCheckOn <= 0)
    {
        return false;
    }
    if (frames > sCheckFrames)
    {
        free(sCheckMix);
        free(sCheckSend);
        sCheckMix = (float*)malloc(sizeof(float) * 2 * frames);
        sCheckSend = (float*)malloc(sizeof(float) * 2 * frames);
        sCheckFrames = sCheckMix != NULL && sCheckSend != NULL ? frames : 0;
        if (sCheckFrames == 0)
        {
            return false;
        }
    }
    memcpy(sCheckVoices, sVoices, sizeof(sVoices));
    return true;
}

static void iCheckEnd(U32 frames, const float* groupFrom, const float* groupTo)
{
    memset(sCheckMix, 0, sizeof(float) * 2 * frames);
    memset(sCheckSend, 0, sizeof(float) * 2 * frames);
    iMixVoicesRef(sCheckVoices, sCheckMix, sSendBuffer != NULL ? sCheckSend : NULL, frames,
                  groupFrom, groupTo);
    U64 bad = 0;
    for (U32 i = 0; i < frames * 2; i++)
    {
        bad += memcmp(&sCheckMix[i], &sMixBuffer[i], sizeof(float)) != 0;
        if (sSendBuffer != NULL)
        {
            bad += memcmp(&sCheckSend[i], &sSendBuffer[i], sizeof(float)) != 0;
        }
    }
    U64 vbad = 0;
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        const hvoice& a = sCheckVoices[i];
        const hvoice& b = sVoices[i];
        vbad += memcmp(&a.pos, &b.pos, sizeof(double)) != 0 || a.playing != b.playing ||
                memcmp(&a.curL, &b.curL, sizeof(float)) != 0 ||
                memcmp(&a.curR, &b.curR, sizeof(float)) != 0;
    }
    sCheckBlocks++;
    sCheckSamples += frames * 2;
    sCheckBad += bad;
    sCheckVoiceBad += vbad;
    const U64 now = iHostMonotonicNs();
    if (sCheckLast == 0)
    {
        sCheckLast = now;
    }
    else if (now - sCheckLast >= 2000000000ULL)
    {
        printf("tssm: audio check: %llu blocks, %llu samples: %llu samples differ from the "
               "reference mixer, %llu voice ends differ\n",
               (unsigned long long)sCheckBlocks, (unsigned long long)sCheckSamples,
               (unsigned long long)sCheckBad, (unsigned long long)sCheckVoiceBad);
        fflush(stdout);
        sCheckLast = now;
    }
}

// Mix `frames` output frames into sMixBuffer as interleaved stereo. Caller
// holds the lock.
static void iMixLocked(U32 frames)
{
    memset(sMixBuffer, 0, sizeof(float) * 2 * frames);

    float groupFrom[ISNDHOST_GROUP_COUNT];
    float groupTo[ISNDHOST_GROUP_COUNT];
    iStepGroupGains(frames, groupFrom, groupTo);

    bool sendUsed = false;
    if (sSendBuffer != NULL)
    {
        memset(sSendBuffer, 0, sizeof(float) * 2 * frames);
    }

    const bool check = iCheckBegin(frames);
    const U64 profT0 = sProfOn > 0 ? iHostMonotonicNs() : 0;
    U32 profVoices = 0;
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &sVoices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }
        profVoices++;
        if (sProfOn > 0)
        {
            if (v->feed)
                sProfFeedFrames += frames;
            else if (v->data != NULL)
                sProfVoiceFrames += frames;
        }

        // TSSM: the bus this voice is mixed into, and its group's level over
        // this block. The send bus takes its level as a whole, further down.
        float* out = sMixBuffer;
        float from = groupFrom[v->group];
        float to = groupTo[v->group];
        if (v->send && sSendBuffer != NULL)
        {
            out = sSendBuffer;
            sendUsed = true;
            from = 1.0f;
            to = 1.0f;
        }

        // A voice with no data still has to advance, so that it finishes when
        // its sound would have. Skipping the mix but not the clock is the
        // difference between a silent sound and a stuck one.
        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        if (!(step > 0.0) || !(step < 1e6))
        {
            step = 0.0;
        }

        if (v->feed)
        {
            iMixFeedLocked(v, out, frames, step, from, to);
            continue;
        }

        if (v->data == NULL)
        {
            v->pos += step * (double)frames;
            if (!iWrap(v))
            {
                v->playing = false;
            }
            continue;
        }

        if (!iMixSound(v, out, frames, step, from, to))
        {
            v->playing = false;
        }
    }

    // The environmental reverb. Here rather than in the render thread so that
    // it is inside the same lock the parameters are set under, and so that the
    // test hook exercises it too.
    //
    // TSSM-ADAPTED: over the send bus only, which holds the voices the game
    // marked for it, and then the bus (dry and wet) joins the mix. It runs
    // even when no voice sent anything this block, so a tail can ring out.
    const U64 profT1 = sProfOn > 0 ? iHostMonotonicNs() : 0;
    if (sProfOn > 0)
    {
        sProfVoiceNs += profT1 - profT0;
        sProfBlocks++;
        sProfVoicesSum += profVoices;
        sProfVoicesMax = profVoices > sProfVoicesMax ? profVoices : sProfVoicesMax;
        sProfSendBlocks += sendUsed ? 1 : 0;
    }
    if (check)
    {
        iCheckEnd(frames, groupFrom, groupTo); // before the reverb changes the send bus
    }
    if (sSendBuffer != NULL)
    {
        iSndReverbProcess(sSendBuffer, frames);
        if (sendUsed || !iSndReverbIdle())
        {
            float g = groupFrom[ISNDHOST_GROUP_EFFECTS];
            const float d = (groupTo[ISNDHOST_GROUP_EFFECTS] - g) / (float)frames;
            for (U32 n = 0; n < frames; n++)
            {
                sMixBuffer[n * 2 + 0] += sSendBuffer[n * 2 + 0] * g;
                sMixBuffer[n * 2 + 1] += sSendBuffer[n * 2 + 1] * g;
                g += d;
            }
        }
    }
    if (sProfOn > 0)
    {
        sProfReverbNs += iHostMonotonicNs() - profT1;
    }
}

// Keep the finished block inside the representable range. Sixty-four voices
// summing at once can leave it, and float output means nothing below here will
// notice: the wrap happens in the driver or the hardware, as a click.
static void iClamp(U32 frames)
{
    for (U32 n = 0; n < frames * 2; n++)
    {
        if (sMixBuffer[n] > 1.0f)
        {
            sMixBuffer[n] = 1.0f;
        }
        else if (sMixBuffer[n] < -1.0f)
        {
            sMixBuffer[n] = -1.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// The stream callback
//
// SDL calls this on its own audio thread whenever the stream is running short,
// asking for `additional` bytes. It may ask for more than one mix buffer's
// worth -- after a stall, or on a driver with a period longer than the one SDL
// reported -- so this fills as many blocks as it takes rather than assuming the
// request fits.

static void SDLCALL iStreamCallback(void* userdata, SDL_AudioStream* stream, int additional,
                                    int total)
{
    (void)userdata;
    (void)total;

    const int calls = SDL_AddAtomicInt(&sCallbacks, 1);
#ifdef __3DS__
    if (calls == 0)
    {
        // Which core SDL's audio thread ended up on (i3DS.cpp's threadCreate).
        printf("tssm: audio: the mixer runs on core %d\n", (int)svcGetProcessorID());
        fflush(stdout);
    }
#else
    (void)calls;
#endif

    const U64 startNs = iHostMonotonicNs();
    U64 produced = 0;
    const int frame_bytes = (int)(sizeof(float) * 2);

    while (additional >= frame_bytes)
    {
        U32 frames = (U32)(additional / frame_bytes);
        if (frames > sMixBufferFrames)
        {
            frames = sMixBufferFrames;
        }

        if (sProfOn < 0)
        {
            const char* e = getenv("TSSM_AUDIO_PROF");
            sProfOn = e != NULL && e[0] == '1' ? 1 : 0;
        }
        const U64 lw0 = sProfOn > 0 ? iHostMonotonicNs() : 0;
        SDL_LockMutex(sLock);
        if (sProfOn > 0)
        {
            sProfLockWaitNs += iHostMonotonicNs() - lw0;
        }
        iMixLocked(frames);
        SDL_UnlockMutex(sLock);

        iClamp(frames);
        iSndMeterF32(iSNDMETER_OUT, sMixBuffer, frames, 2);

        // TSSM_TEST_AUDIO_RESTART's meter (iAudioSDLTest.h), off unless a test
        // turned it on.
        if (SDL_GetAtomicInt(&sTestMeterOn) != 0)
        {
            float loudest = 0.0f;
            for (U32 i = 0; i < frames * 2; i++)
            {
                const float a = sMixBuffer[i] < 0.0f ? -sMixBuffer[i] : sMixBuffer[i];
                if (a > loudest)
                {
                    loudest = a;
                }
            }
            const int v = (int)(loudest * 1000000.0f);
            int was = SDL_GetAtomicInt(&sTestPeak);
            while (v > was && !SDL_CompareAndSwapAtomicInt(&sTestPeak, was, v))
            {
                was = SDL_GetAtomicInt(&sTestPeak);
            }
        }

        int bytes = (int)frames * frame_bytes;
        if (!SDL_PutAudioStreamData(stream, sMixBuffer, bytes))
        {
            // Nothing useful to do about it here. The next callback will try
            // again, and the voices have already been advanced, so the game
            // stays in step with what it thinks it is hearing.
            break;
        }

        produced += frames;
        additional -= bytes;
    }

    const U64 tookNs = iHostMonotonicNs() - startNs;
    if (sProfOn > 0)
    {
        const U64 now = iHostMonotonicNs();
        sProfRestNs += tookNs;
        if (sProfLast == 0)
        {
            sProfLast = now;
        }
        else if (now - sProfLast >= 2000000000ULL)
        {
            const double k = 1000.0 / (double)(now - sProfLast); // ns -> ms a second
            // The game thread adds to these under the lock: copy and clear them there.
            SDL_LockMutex(sLock);
            const U64 gameWaitNs = sProfGameWaitNs;
            const U64 gameLocks = sProfGameLocks;
            const U64 gameWaitMaxNs = sProfGameWaitMaxNs;
            sProfGameWaitNs = sProfGameLocks = sProfGameWaitMaxNs = 0;
            SDL_UnlockMutex(sLock);
            printf("tssm: audio prof: %.2f s: callbacks' wall %.1f ms a second (lock wait %.1f, "
                   "voices %.1f, reverb %.1f); %llu blocks, voices a block %.1f (peak %u), "
                   "send used in %llu; voice frames %llu a second (fed %llu); the game "
                   "thread's lock: %llu a second, waited %.2f ms a second, longest %.2f ms\n",
                   (double)(now - sProfLast) / 1e9, sProfRestNs * k,
                   sProfLockWaitNs * k, sProfVoiceNs * k, sProfReverbNs * k,
                   (unsigned long long)sProfBlocks,
                   sProfBlocks ? (double)sProfVoicesSum / (double)sProfBlocks : 0.0,
                   (unsigned)sProfVoicesMax, (unsigned long long)sProfSendBlocks,
                   (unsigned long long)(sProfVoiceFrames * 1e9 / (double)(now - sProfLast)),
                   (unsigned long long)(sProfFeedFrames * 1e9 / (double)(now - sProfLast)),
                   (unsigned long long)(gameLocks * 1e9 / (double)(now - sProfLast)),
                   gameWaitNs * k, gameWaitMaxNs / 1e6);
            fflush(stdout);
            sProfVoiceNs = sProfReverbNs = sProfRestNs = sProfLockWaitNs = 0;
            sProfVoiceFrames = sProfFeedFrames = sProfBlocks = sProfVoicesSum = sProfSendBlocks = 0;
            sProfVoicesMax = 0;
            sProfLast = now;
        }
    }
    SDL_LockMutex(sLock);
    sMixStats.calls++;
    sMixStats.totalNs += tookNs;
    sMixStats.frames += produced;
    if (tookNs > sMixStats.maxNs)
    {
        sMixStats.maxNs = tookNs;
    }
    SDL_UnlockMutex(sLock);
}

// ---------------------------------------------------------------------------
// Device setup

static void iTeardownDevice()
{
    // Destroying the stream unbinds it from the logical device SDL opened for
    // it and closes that device, which also stops the callback. Nothing below
    // may run while it could still be in flight.
    if (sStream != NULL)
    {
        SDL_DestroyAudioStream(sStream);
        sStream = NULL;
    }

    free(sMixBuffer);
    sMixBuffer = NULL;
    free(sSendBuffer);
    sSendBuffer = NULL;
    sMixBufferFrames = 0;

    // After the callback has gone, which is the only other user of it.
    // sReverbWanted deliberately survives: a device coming back should come
    // back with the effect the current scene asked for.
    iSndReverbExit();

    if (sSubsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sSubsystem = false;
    }

    sDeviceUp = false;
}

// Everything here reports and returns false rather than failing hard: see the
// note at the top about a missing device being a configuration, not an error.
static bool iBringUpDevice()
{
    // Refcounted, and the window and pad backends init their own subsystems the
    // same way, so this neither depends on nor disturbs them.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        printf("iSndHost: SDL audio would not start (%s); no audio\n", SDL_GetError());
        return false;
    }

    sSubsystem = true;

    // Ask the device what it is already running at and open the stream there,
    // so SDL has no resampling to do. A failure here means no output device at
    // all, which is the silent path rather than an error.
    SDL_AudioSpec have;
    int device_frames = 0;

    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &have, &device_frames))
    {
        printf("iSndHost: no default output device (%s); no audio\n", SDL_GetError());
        return false;
    }

    sOutRate = have.freq > 0 ? (U32)have.freq : 48000;
    sDeviceFrames = device_frames > 0 ? (U32)device_frames : 1024;

    // Four periods of headroom. The callback asks for what the stream is short
    // of rather than for one period, and a block bigger than this only costs an
    // extra pass through the mix loop, so this is a size and not a limit.
    sMixBufferFrames = sDeviceFrames * 4;
    sMixBuffer = (float*)malloc(sizeof(float) * 2 * sMixBufferFrames);
    sSendBuffer = (float*)malloc(sizeof(float) * 2 * sMixBufferFrames);

    if (sMixBuffer == NULL || sSendBuffer == NULL)
    {
        printf("iSndHost: could not allocate the mix buffer; no audio\n");
        sMixBufferFrames = 0;
        return false;
    }

    // Stereo float at the device's rate. SDL converts from this to whatever the
    // hardware wants, including down to a mono endpoint, which is the one piece
    // of format handling this backend does not have to write.
    SDL_AudioSpec want;
    want.format = SDL_AUDIO_F32;
    want.channels = 2;
    want.freq = (int)sOutRate;

    // Opened paused, so the mix buffer above is guaranteed to exist before the
    // first callback and the reverb below is built before it is heard.
    sStream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, iStreamCallback, NULL);

    if (sStream == NULL)
    {
        printf("iSndHost: could not open an audio stream (%s); no audio\n", SDL_GetError());
        return false;
    }

    // The reverb's delay lengths are in samples, so it can only be built once
    // the device has said what rate it runs at. If the game already asked for
    // an effect -- it sets one when a scene loads, which can be before a device
    // is available -- it is applied now rather than lost.
    iSndReverbInit(sOutRate);
    if (sReverbWanted)
    {
        iSndReverbSet(&sReverb);
    }

    if (!SDL_ResumeAudioStreamDevice(sStream))
    {
        printf("iSndHost: the stream would not start (%s); no audio\n", SDL_GetError());
        return false;
    }

    const char* driver = SDL_GetCurrentAudioDriver();

    snprintf(sName, sizeof(sName), "SDL3 %s (%u Hz, float32 stereo, %u-frame buffer)",
             driver != NULL ? driver : "?", sOutRate, sDeviceFrames);

    return true;
}

// TSSM: the silent path's clock starts now, for every voice. After the device
// goes, so the voices are not advanced by the time it was gone (the app in
// the background, where the game's own clock stops too), and after it comes
// back, so a later loss starts from where the device left them.
static void iResetSilentClock()
{
    U64 now = iHostMonotonicNs();

    iLockGame();
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        sVoices[i].silent_ns = now;
    }
    SDL_UnlockMutex(sLock);

    sLastSilentNs = now;
}

// TSSM: this backend as an iAudioSDL.h user.
static bool iDeviceUser(bool up)
{
    if (!up)
    {
        if (sDeviceUp)
        {
            iTeardownDevice();
        }
        iResetSilentClock();
        return true;
    }

    if (!sDeviceUp)
    {
        sDeviceUp = iBringUpDevice();

        if (!sDeviceUp)
        {
            iTeardownDevice();
            snprintf(sName, sizeof(sName), "SDL3 (device lost; silent, but keeps time)");
            if (sOutRate == 0)
            {
                sOutRate = 48000;
            }
        }
    }

    iResetSilentClock();
    sSeenCallbacks = SDL_GetAtomicInt(&sCallbacks);
    sAliveNs = iHostMonotonicNs();

    return sDeviceUp;
}

// TSSM_TEST_AUDIO_RESTART's level lines (iAudioSDLTest.h). Game thread.
void iSndHostSDLTestMeter(bool on)
{
    SDL_SetAtomicInt(&sTestMeterOn, on ? 1 : 0);
}

void iSndHostSDLTestStats(U32* callbacks, float* peak, U32* device)
{
    *callbacks = (U32)SDL_GetAtomicInt(&sCallbacks);
    if (peak != NULL)
    {
        *peak = (float)SDL_SetAtomicInt(&sTestPeak, 0) / 1000000.0f;
    }
    *device = sStream != NULL ? (U32)SDL_GetAudioStreamDevice(sStream) : 0;
}

// ---------------------------------------------------------------------------
// The seam

void iSndHostInit()
{
    iEnsureLock();

    iLockGame();
    memset(sVoices, 0, sizeof(sVoices));
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        sVoices[i].pitch = 1.0f;
    }
    SDL_UnlockMutex(sLock);

    if (sDeviceUp)
    {
        return;
    }

    // TSSM_AUDIO=0 forces the silent path, which is the quickest way to tell a
    // bug in the mixer apart from a bug in the game logic that waits on it.
    const char* off = getenv("TSSM_AUDIO");
    if (off != NULL && off[0] == '0')
    {
        snprintf(sName, sizeof(sName), "SDL3 (silenced by TSSM_AUDIO=0, but keeps time)");
        sOutRate = 48000;
        sLastSilentNs = iHostMonotonicNs();
        return;
    }

    sDeviceUp = iBringUpDevice();

    if (!sDeviceUp)
    {
        iTeardownDevice();
        snprintf(sName, sizeof(sName), "SDL3 (no device; silent, but keeps time)");

        // The fallback clock needs a rate to convert its elapsed time into
        // frames. Any value works as long as it matches what the voices are
        // stepped against; 48 kHz is what a device would most likely have been.
        sOutRate = 48000;
        sLastSilentNs = iHostMonotonicNs();
    }

    sSeenCallbacks = SDL_GetAtomicInt(&sCallbacks);
    sAliveNs = iHostMonotonicNs();
    sLastUpdateNs = sAliveNs;

    iAudioSDLAttach(iDeviceUser, sDeviceUp);
    sAttached = true;
}

void iSndHostExit()
{
    if (sAttached)
    {
        iAudioSDLDetach(iDeviceUser);
        sAttached = false;
    }

    iTeardownDevice();

    iLockGame();
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        free(sVoices[i].ring);
    }
    memset(sVoices, 0, sizeof(sVoices));
    SDL_UnlockMutex(sLock);
}

S32 iSndHostAcquire(U32 priority)
{
    iEnsureLock();

    iLockGame();

    S32 got = -1;
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        if (!sVoices[i].acquired)
        {
            memset(&sVoices[i], 0, sizeof(sVoices[i]));
            sVoices[i].acquired = true;
            sVoices[i].pitch = 1.0f;
            got = i;
            break;
        }
    }

    SDL_UnlockMutex(sLock);

    // Every voice busy. Retail's AXAcquireVoice evicts a lower-priority one
    // here; the mixer has exactly as many voices as the game's own table, so
    // running out means the game already decided all 64 were worth keeping, and
    // there is nothing better to evict than what it chose. Failing is a state
    // the game handles.
    return got;
}

void iSndHostRelease(S32 voice)
{
    iEnsureLock();

    iLockGame();

    S16* ring = NULL;
    if (iVoiceValid(voice))
    {
        ring = sVoices[voice].ring;
        memset(&sVoices[voice], 0, sizeof(sVoices[voice]));
    }

    SDL_UnlockMutex(sLock);

    // Outside the lock: the mixer can no longer reach it.
    free(ring);
}

void iSndHostStart(S32 voice, const iSndHostSample* sample)
{
    if (sample == NULL)
    {
        return;
    }

    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice))
    {
        hvoice* v = &sVoices[voice];

        U32 channels = sample->channels != 0 ? sample->channels : 1;
        U32 bits = sample->bits != 0 ? sample->bits : 16;

        // What the caller says the sample is, against what it actually handed
        // over. A short block would be read past the end at the last frame, so
        // the byte count wins.
        U32 frame_bytes = channels * (bits / 8);
        U32 frames = sample->num_samples;

        if (sample->data != NULL && frame_bytes != 0)
        {
            U32 have = sample->bytes / frame_bytes;
            if (frames == 0 || frames > have)
            {
                frames = have;
            }
        }

        // TSSM: a sample voice is not a fed one, whatever the slot was before.
        v->feed = false;
        v->feed_ended = false;

        v->data = sample->data;
        v->frames = frames;
        v->channels = channels;
        v->bits = bits;
        v->rate = sample->sample_rate != 0 ? sample->sample_rate : 22050;
        v->looping = sample->looping;
        v->loop_start = sample->loop_start;
        v->loop_end = sample->loop_end;
        v->pos = 0.0;
        v->playing = frames > 0;
        v->paused = false;
        v->silent_ns = iHostMonotonicNs();

        // Start at the target gain rather than ramping up from silence: the
        // first block of a sound is its attack, and fading it in blunts every
        // percussive effect in the game.
        v->curL = v->volL;
        v->curR = v->volR;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostStop(S32 voice)
{
    iEnsureLock();

    iLockGame();

    S16* ring = NULL;
    if (iVoiceValid(voice))
    {
        sVoices[voice].playing = false;
        sVoices[voice].paused = false;

        // Drop the borrowed pointer here, not just the playing flag. The caller
        // is free to release the sample as soon as this returns, and the mixer
        // must have no way to reach it afterwards.
        sVoices[voice].data = NULL;
        sVoices[voice].frames = 0;

        // TSSM: a fed voice's ring is its own, and goes with it.
        ring = sVoices[voice].ring;
        sVoices[voice].ring = NULL;
        sVoices[voice].ring_cap = 0;
        sVoices[voice].written = 0;
        sVoices[voice].feed = false;
        sVoices[voice].feed_ended = false;
    }

    SDL_UnlockMutex(sLock);

    free(ring);
}

void iSndHostPause(S32 voice, bool paused)
{
    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice) && sVoices[voice].playing)
    {
        // TSSM: time paused is not time played, on the silent clock either.
        if (sVoices[voice].paused && !paused)
        {
            sVoices[voice].silent_ns = iHostMonotonicNs();
        }
        sVoices[voice].paused = paused;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetVol(S32 voice, F32 left, F32 right)
{
    if (left < 0.0f)
        left = 0.0f;
    else if (left > 1.0f)
        left = 1.0f;
    if (right < 0.0f)
        right = 0.0f;
    else if (right > 1.0f)
        right = 1.0f;

    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice))
    {
        sVoices[voice].volL = left;
        sVoices[voice].volR = right;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetPitch(S32 voice, F32 pitch)
{
    // NaN and infinity are refused too: either would make every position NaN
    // and stall a looping voice's mix for good.
    if (!(pitch > 0.0f) || !(pitch < 1e6f))
    {
        return;
    }

    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice))
    {
        sVoices[voice].pitch = pitch;
    }

    SDL_UnlockMutex(sLock);
}

bool iSndHostIsPlaying(S32 voice)
{
    iEnsureLock();

    iLockGame();

    bool playing = iVoiceValid(voice) && sVoices[voice].playing;

    SDL_UnlockMutex(sLock);

    return playing;
}

void iSndHostUpdate()
{
    // TSSM: a restart that is due (iAudioSDL.h) happens here, on the game
    // thread, before the voices are looked at.
    if (sAttached)
    {
        iAudioSDLPoll();
    }

    if (sDeviceUp)
    {
        // The render thread advanced every voice as it consumed it, and retires
        // them itself. Nothing to do but check that it still does.
        U64 now = iHostMonotonicNs();
        int seen = SDL_GetAtomicInt(&sCallbacks);

        // A gap between updates (a load, a breakpoint, the machine asleep)
        // says nothing about the device, so the wait starts again after one.
        if (seen != sSeenCallbacks || now - sLastUpdateNs > 250000000ULL)
        {
            sSeenCallbacks = seen;
            sAliveNs = now;
        }
        else if (sAttached && now - sAliveNs > kStallNs)
        {
            sAliveNs = now;
            iAudioSDLRestart(iAUDIOSDL_STALLED);
        }

        sLastUpdateNs = now;
        return;
    }

    // No device: advance the voices on the wall clock instead, so that
    // everything the game derives from a sound's length still happens on time.
    U64 now = iHostMonotonicNs();
    sLastSilentNs = now;

    iEnsureLock();

    iLockGame();

    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &sVoices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }

        // TSSM-ADAPTED: from when this voice was last advanced (or started),
        // not from the last update, which may predate it.
        U64 elapsed = now - v->silent_ns;
        v->silent_ns = now;

        // A long stall -- a breakpoint, a scene load -- would otherwise skip a
        // sound entirely. Cap the step at a quarter second, which is what a
        // device would effectively have done by dropping the frames it could
        // not fill.
        if (elapsed > 250000000ULL)
        {
            elapsed = 250000000ULL;
        }

        double frames = ((double)elapsed / 1000000000.0) * (double)sOutRate;

        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        v->pos += step * frames;

        if (v->feed)
        {
            // TSSM: a fed voice cannot run ahead of what it was given.
            if (v->pos >= (double)v->written)
            {
                v->pos = (double)v->written;
                if (v->feed_ended)
                {
                    v->playing = false;
                }
            }
            continue;
        }

        if (!iWrap(v))
        {
            v->playing = false;
        }
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetReverb(const iSndHostReverb* params)
{
    iEnsureLock();

    iLockGame();

    if (params != NULL)
    {
        sReverb = *params;
        sReverbWanted = true;
        iSndReverbSet(&sReverb);
    }
    else
    {
        sReverbWanted = false;
        iSndReverbSet(NULL);
    }

    SDL_UnlockMutex(sLock);
}

// ---------------------------------------------------------------------------
// TSSM additions (iSndHost.h)

void iSndHostSetSend(S32 voice, bool send)
{
    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice))
    {
        sVoices[voice].send = send;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetGroup(S32 voice, iSndHostGroup group)
{
    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice))
    {
        sVoices[voice].group = group;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetGroupGain(iSndHostGroup group, F32 gain)
{
    gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    SDL_SetAtomicInt(&sGroupGain[group], (int)(gain * (float)kGroupGainOne + 0.5f));
}

void iSndHostPauseGroup(const S32* voices, S32 count, bool paused)
{
    if (voices == NULL)
    {
        return;
    }

    iEnsureLock();

    iLockGame();

    for (S32 i = 0; i < count; i++)
    {
        S32 voice = voices[i];
        if (iVoiceValid(voice) && sVoices[voice].playing)
        {
            if (sVoices[voice].paused && !paused)
            {
                sVoices[voice].silent_ns = iHostMonotonicNs();
            }
            sVoices[voice].paused = paused;
        }
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostStartFeed(S32 voice, U32 channels, U32 sample_rate, U32 capacity, bool paused)
{
    if (channels == 0)
    {
        channels = 1;
    }

    // A power of two, so a frame's place in the ring is a mask.
    U32 cap = 1024;
    while (cap < capacity && cap < (1U << 24))
    {
        cap <<= 1;
    }

    // Allocated before the lock is taken: the mixer is never kept waiting on
    // the heap.
    S16* ring = (S16*)calloc((size_t)cap * channels, sizeof(S16));

    iEnsureLock();

    iLockGame();

    S16* old = NULL;
    if (iVoiceValid(voice))
    {
        hvoice* v = &sVoices[voice];

        old = v->ring;
        v->ring = ring;
        v->ring_cap = ring != NULL ? cap : 0;
        v->written = 0;
        v->feed = true;
        v->feed_ended = false;

        v->data = NULL;
        v->frames = 0;
        v->channels = channels;
        v->bits = 16;
        v->rate = sample_rate != 0 ? sample_rate : 44100;
        v->looping = false;
        v->loop_start = 0;
        v->loop_end = 0;
        v->pos = 0.0;
        v->playing = ring != NULL;
        v->paused = paused;
        v->silent_ns = iHostMonotonicNs();
        v->curL = v->volL;
        v->curR = v->volR;
        ring = NULL;
    }

    SDL_UnlockMutex(sLock);

    free(old);
    free(ring);
}

U32 iSndHostFeed(S32 voice, const S16* frames, U32 count)
{
    if (frames == NULL || count == 0)
    {
        return 0;
    }

    iEnsureLock();

    iLockGame();

    U32 taken = 0;
    if (iVoiceValid(voice) && sVoices[voice].feed && sVoices[voice].ring != NULL &&
        !sVoices[voice].feed_ended)
    {
        hvoice* v = &sVoices[voice];

        // Room is what the reader has passed, less two frames: the frame it is
        // on and the one it interpolates towards must survive the write.
        U64 readFrame = (U64)v->pos;
        U64 queued = v->written - readFrame;
        U64 room = (U64)v->ring_cap > queued + 2 ? (U64)v->ring_cap - queued - 2 : 0;

        taken = count < room ? count : (U32)room;

        U32 mask = v->ring_cap - 1;
        U32 ch = v->channels;
        for (U32 n = 0; n < taken;)
        {
            // Up to the end of the ring per copy.
            U32 at = (U32)((v->written + n) & mask);
            U32 run = v->ring_cap - at;
            if (run > taken - n)
            {
                run = taken - n;
            }

            memcpy(v->ring + (size_t)at * ch, frames + (size_t)n * ch,
                   (size_t)run * ch * sizeof(S16));
            n += run;
        }

        v->written += taken;
    }

    SDL_UnlockMutex(sLock);

    return taken;
}

U32 iSndHostFeedQueued(S32 voice)
{
    iEnsureLock();

    iLockGame();

    U32 queued = 0;
    if (iVoiceValid(voice) && sVoices[voice].feed)
    {
        U64 readFrame = (U64)sVoices[voice].pos;
        U64 q = sVoices[voice].written > readFrame ? sVoices[voice].written - readFrame : 0;
        queued = q > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (U32)q;
    }

    SDL_UnlockMutex(sLock);

    return queued;
}

void iSndHostFeedEnd(S32 voice)
{
    iEnsureLock();

    iLockGame();

    if (iVoiceValid(voice) && sVoices[voice].feed)
    {
        sVoices[voice].feed_ended = true;
    }

    SDL_UnlockMutex(sLock);
}

// ---------------------------------------------------------------------------
// Test hook
//
// Deliberately not in iSndHost.h: nothing in the game may call this, and its
// only caller is src/SB/Core/pc/tests/selftest.cpp. It exists because the
// interesting half of this file -- the resampling, the gain ramp, the loop
// wrap -- is otherwise only reachable through a real audio endpoint, which a
// self-test must not require and could not make deterministic if it did.
//
// Mixes one block at `rate` into `out` as interleaved stereo, exactly as the
// render thread would, and advances the voices by it.
void iSndHostTestMix(U32 rate, float* out, U32 frames)
{
    if (out == NULL || frames == 0 || rate == 0)
    {
        return;
    }

    iEnsureLock();

    if (sMixBuffer == NULL || sSendBuffer == NULL || frames > sMixBufferFrames)
    {
        free(sMixBuffer);
        free(sSendBuffer);
        sMixBuffer = (float*)malloc(sizeof(float) * 2 * frames);
        sSendBuffer = (float*)malloc(sizeof(float) * 2 * frames);
        sMixBufferFrames = (sMixBuffer != NULL && sSendBuffer != NULL) ? frames : 0;
    }

    if (sMixBuffer == NULL || sSendBuffer == NULL)
    {
        return;
    }

    U32 saved = sOutRate;
    sOutRate = rate;

    iLockGame();
    iMixLocked(frames);
    SDL_UnlockMutex(sLock);

    sOutRate = saved;

    memcpy(out, sMixBuffer, sizeof(float) * 2 * frames);
}

bool iSndHostMixStatsTake(iSndHostMixStats* out, bool reset)
{
    iEnsureLock();
    iLockGame();
    *out = sMixStats;
    out->rate = sDeviceUp ? sOutRate : 0;
    if (reset)
    {
        memset(&sMixStats, 0, sizeof(sMixStats));
    }
    SDL_UnlockMutex(sLock);
    return sDeviceUp;
}

const char* iSndHostName()
{
    return sName[0] != '\0' ? sName : "SDL3 (not initialised)";
}
