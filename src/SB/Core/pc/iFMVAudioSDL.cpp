// The movie audio device, through SDL.
//
// A separate device from the game's mixer, for the reason iFMVAudio.h gives:
// the voice table behind iSndHost.h is the wrong shape for something that
// produces audio a fragment at a time. Both devices are open during a movie: the
// game's voices are paused around it, so the mixer runs on but plays nothing.
//
// Push mode: a stream bound to the device, filled with SDL_PutAudioStreamData
// and pulled out by SDL on its own thread as the device asks. That is exactly
// the shape of this seam, so there is no ring of blocks here and no device
// thread of ours.
//
// SDL converts the movie's rate and channel count to whatever the hardware
// wants, so a 22 kHz mono cutscene needs no resampling on this side.
//
// TSSM-ADAPTED: the device is closed and opened again with the game's
// (iAudioSDL.h): in the background, after a playback device is removed, and
// when it stops taking audio. While there is none the movie keeps time on a
// stand-in queue that drains at the movie's rate, because the picture is paced
// on what the device has played and would otherwise stop.

#include "iFMVAudio.h"
#include "iAudioSDL.h"
#include "iAudioSDLTest.h"
#include "iSndMeter.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>

namespace
{
    SDL_AudioStream* sStream;
    SDL_AudioDeviceID sDevice;
    bool sSubsystem;
    U32 sChannels;
    U32 sRate;

    // How far ahead of the device the writer is allowed to get. The old waveOut
    // backend held eight 20 ms blocks; the same 160 ms here keeps a 25 fps
    // movie's 40 ms frames from ever finding the queue full for long, without
    // letting a fast decoder run so far ahead that closing has to discard much.
    const U32 kQueueMs = 160;

    // TSSM: a movie is open but its device is not (closed for the background,
    // lost, or not open again yet). The stand-in queue: frames accepted and
    // not yet "played", drained on the wall clock at the movie's rate.
    bool sLost;
    double sVirtualQueued;
    Uint64 sVirtualNs;

    // TSSM: the stall check. Bytes handed to the stream, how many of them the
    // device had taken when last looked at, and when that last changed: audio
    // waiting and none taken for a second is a device that stopped taking it.
    // (The queue's size alone says nothing: the writer tops it up to the same
    // level every pass.)
    Uint64 sPutBytes;
    Uint64 sTakenBytes;
    Uint64 sTakenMovedNs;
    const Uint64 kStallNs = 1000000000ULL;

    // TSSM: the movie's level (iFMVAudio.h), and where the samples have got to
    // on their way to it. Scaled here on the game thread, on the way into the
    // stream, so SDL's audio thread has no part in it: all it costs is that
    // what is queued (kQueueMs) plays at the level it was written at.
    F32 sSettingsVolume = 1.0f;
    F32 sGameVolume = 1.0f;
    F32 sGain = 1.0f;

    U32 QueuedFrames()
    {
        if (sLost)
        {
            Uint64 now = SDL_GetTicksNS();
            sVirtualQueued -= (double)(now - sVirtualNs) * (double)sRate / 1e9;
            sVirtualNs = now;
            if (sVirtualQueued < 0.0)
            {
                sVirtualQueued = 0.0;
            }
            return (U32)sVirtualQueued;
        }

        if (sStream == NULL)
        {
            return 0;
        }

        // **Zero once the device has taken everything it can.**
        //
        // A stream whose rate differs from the device's keeps a few input
        // samples as resampler history, and SDL goes on counting them as
        // queued: a 44.1 kHz movie on a 48 kHz device settles at seven frames
        // and never reaches zero. iFMV.cpp hands its clock over to the wall
        // only when the track has drained AND the queue is empty, so those
        // seven frames stop the clock on the last video frame and the movie
        // hangs there forever. Nothing is left to play when there is no
        // converted output, so that is what "empty" means here.
        if (SDL_GetAudioStreamAvailable(sStream) <= 0)
        {
            return 0;
        }

        // Bytes of INPUT, so this is in the movie's own format rather than the
        // device's. What has already been handed down to the device is no
        // longer counted, so this runs up to one device period short -- ten or
        // twenty milliseconds of picture ahead of sound, constant rather than
        // drifting, and below what anyone sees.
        int queued = SDL_GetAudioStreamQueued(sStream);

        if (queued <= 0)
        {
            return 0;
        }

        return (U32)queued / (sChannels * (U32)sizeof(S16));
    }

    // TSSM_SND_METER: what the movie's device mixed, on SDL's audio thread.
    void SDLCALL MeterPostmix(void* userdata, const SDL_AudioSpec* spec, float* buffer, int buflen)
    {
        (void)userdata;
        if (spec != NULL && spec->channels > 0)
        {
            U32 frames = (U32)buflen / ((U32)sizeof(float) * (U32)spec->channels);
            iSndMeterF32(iSNDMETER_MOVIE_OUT, buffer, frames, (U32)spec->channels);
        }
    }

    // Everything SDL holds for the movie, the subsystem reference included.
    // TSSM: the format stays, for opening again.
    void Drop()
    {
        // TSSM: the device is closed first and the stream destroyed after it,
        // never the other way round. Closing it first also means nothing is
        // left playing the tail of a movie the game has moved on from.
        //
        // The stream is bound to the device (SDL_BindAudioStream). Destroyed
        // while still bound, SDL unbinds it in SDL_UnbindAudioStreams, which
        // is not safe while SDL is moving the device onto a new default
        // playback device -- SDL_DefaultAudioDeviceChanged, on SDL's WASAPI
        // management thread, when the device the game plays on goes away and
        // Windows names another. That unbind locks the physical device the
        // movie's device is on when it starts, and unlocks the one it is on
        // when it ends. If SDL moves it in between, this thread keeps the old
        // device locked for good, SDL asserts "mutex not owned by this thread"
        // (in a debug build), and the close that follows waits forever for the
        // new device, which SDL's management thread holds while it waits for
        // the old device's thread, which waits for the lock this thread kept.
        // The game froze in a movie that way.
        //
        // SDL_CloseAudioDevice has no such window: it finds the physical
        // device with a loop that checks again, once it holds the lock, that
        // the movie's device is still on it (ObtainLogicalAudioDevice), and it
        // unbinds the stream itself under that lock. The stream destroyed
        // afterwards is bound to nothing and touches no device.
        if (sDevice != 0)
        {
            SDL_CloseAudioDevice(sDevice);
            sDevice = 0;
        }

        if (sStream != NULL)
        {
            SDL_DestroyAudioStream(sStream);
            sStream = NULL;
        }

        if (sSubsystem)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sSubsystem = false;
        }
    }

    bool Bring(U32 sample_rate, U32 channels)
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return false;
        }

        sSubsystem = true;

        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_S16;
        spec.channels = (int)channels;
        spec.freq = (int)sample_rate;

        sDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);

        if (sDevice == 0)
        {
            Drop();
            return false;
        }

        if (iSndMeterOn())
        {
            SDL_SetAudioPostmixCallback(sDevice, MeterPostmix, NULL);
        }

        // Both ends of the stream are the movie's format; binding it to the
        // device is what sets the output end to the hardware's.
        sStream = SDL_CreateAudioStream(&spec, &spec);

        if (sStream == NULL || !SDL_BindAudioStream(sDevice, sStream) ||
            !SDL_ResumeAudioDevice(sDevice))
        {
            Drop();
            return false;
        }

        sPutBytes = 0;
        sTakenBytes = 0;
        sTakenMovedNs = SDL_GetTicksNS();

        return true;
    }

    // TSSM: the frames into the stream at the movie's level. Returns how many
    // went in. The level moves full scale in 20 ms at most, so that it cannot
    // step the signal, and at 1 the movie's own samples go in untouched.
    U32 PutFrames(const S16* frames, U32 count)
    {
        const F32 want = sSettingsVolume * sGameVolume;

        if (want == 1.0f && sGain == 1.0f)
        {
            const int bytes = (int)(count * sChannels * (U32)sizeof(S16));
            return SDL_PutAudioStreamData(sStream, frames, bytes) ? count : 0;
        }

        const F32 most = 1.0f / (0.02f * (F32)sRate);

        U32 put = 0;
        while (put < count)
        {
            S16 scaled[512 * 2];
            const U32 n = count - put < 512 ? count - put : 512;
            const S16* in = frames + (size_t)put * sChannels;

            for (U32 i = 0; i < n * sChannels; i += sChannels)
            {
                sGain = want > sGain + most ? sGain + most :
                                              (want < sGain - most ? sGain - most : want);
                for (U32 c = 0; c < sChannels; c++)
                {
                    scaled[i + c] = (S16)lrintf((F32)in[i + c] * sGain);
                }
            }

            const int bytes = (int)(n * sChannels * (U32)sizeof(S16));
            if (!SDL_PutAudioStreamData(sStream, scaled, bytes))
            {
                break;
            }

            put += n;
        }

        return put;
    }

    // TSSM: the movie device as an iAudioSDL.h user. Closing keeps the movie
    // going on the stand-in queue, starting from what the device still held.
    bool DeviceUser(bool up)
    {
        if (!up)
        {
            if (sStream != NULL)
            {
                sVirtualQueued = (double)QueuedFrames();
                sVirtualNs = SDL_GetTicksNS();
                sLost = true;
            }
            Drop();
            return true;
        }

        if (!sLost)
        {
            return true;
        }

        // What the stand-in queue still holds is dropped: at most kQueueMs of
        // the movie's sound, which the picture then catches up on.
        if (Bring(sRate, sChannels))
        {
            sLost = false;
            return true;
        }

        return false;
    }
} // namespace

S32 iFMVAudioOpen(U32 sample_rate, U32 channels)
{
    if (sStream != NULL || sLost || sample_rate == 0 || channels == 0 || channels > 2)
    {
        return FALSE;
    }

    if (!Bring(sample_rate, channels))
    {
        return FALSE;
    }

    sChannels = channels;
    sRate = sample_rate;

    // A movie starts at its level, with nothing to move from.
    sGain = sSettingsVolume * sGameVolume;

    iAudioSDLAttach(DeviceUser, true);

    return TRUE;
}

void iFMVAudioClose()
{
    iAudioSDLDetach(DeviceUser);

    Drop();

    sLost = false;
    sVirtualQueued = 0.0;
    sChannels = 0;
    sRate = 0;
}

void iFMVAudioSetVolume(F32 volume)
{
    sSettingsVolume = volume;
}

void iFMVAudioSetGameVolume(F32 volume)
{
    sGameVolume = volume;
}

U32 iFMVAudioWrite(const S16* frames, U32 num_frames)
{
    iAudioSDLPoll();

    if ((sStream == NULL && !sLost) || frames == NULL || num_frames == 0)
    {
        return 0;
    }

    // SDL would take everything offered and queue it without limit, which would
    // put the whole movie in ahead of the picture and leave iFMVAudioQueued
    // measuring the decoder rather than the device. Cap it here so the caller
    // keeps the rest, which is the contract in iFMVAudio.h.
    U32 cap = (sRate * kQueueMs) / 1000;
    U32 have = QueuedFrames();

    if (have >= cap)
    {
        return 0;
    }

    U32 take = cap - have;
    if (take > num_frames)
    {
        take = num_frames;
    }

    if (sLost)
    {
        sVirtualQueued += (double)take;
        return take;
    }

    const U32 put = PutFrames(frames, take);

    sPutBytes += (Uint64)put * sChannels * sizeof(S16);

    // The frames handed over, as they were before the movie volume.
    iSndMeterS16(iSNDMETER_MOVIE, frames, put, sChannels);

    return put;
}

U32 iFMVAudioQueued()
{
    iAudioSDLPoll();

    if (sStream != NULL)
    {
        Uint64 now = SDL_GetTicksNS();
        int queued = SDL_GetAudioStreamQueued(sStream);
        Uint64 taken = sPutBytes - (queued > 0 ? (Uint64)queued : 0);

        if (taken != sTakenBytes || SDL_GetAudioStreamAvailable(sStream) <= 0)
        {
            sTakenBytes = taken;
            sTakenMovedNs = now;
        }
        else if (now - sTakenMovedNs > kStallNs)
        {
            sTakenMovedNs = now;
            iAudioSDLRestart(iAUDIOSDL_STALLED);
        }
    }

    return QueuedFrames();
}

const char* iFMVAudioName()
{
    // The startup log asks for this before any movie has opened a device, and
    // SDL names no driver until one is up. Say which library it is either way,
    // and which driver once there is one to name.
    const char* driver = SDL_GetCurrentAudioDriver();

    if (driver == NULL)
    {
        return "SDL3";
    }

    static char name[64];
    snprintf(name, sizeof(name), "SDL3 %s", driver);

    return name;
}

// TSSM_TEST_AUDIO_RESTART's level lines (iAudioSDLTest.h). Game thread.
bool iFMVAudioSDLTestStats(U32* taken, U32* queued, bool* standin, U32* device)
{
    *taken = 0;
    *queued = 0;
    *standin = sLost;
    *device = sDevice;

    if (sLost)
    {
        *queued = (U32)sVirtualQueued;
        return true;
    }
    if (sStream == NULL || sChannels == 0)
    {
        return false;
    }

    const int inQueue = SDL_GetAudioStreamQueued(sStream);
    const Uint64 bytes = inQueue > 0 ? (Uint64)inQueue : 0;
    const U32 frame = sChannels * (U32)sizeof(S16);
    *taken = (U32)((sPutBytes > bytes ? sPutBytes - bytes : 0) / frame);
    *queued = (U32)(bytes / frame);
    return true;
}
