#include "iAudioSDL.h"

#include <SDL3/SDL.h>

#include <stdio.h>

// See iAudioSDL.h for why this exists.

namespace
{
    const int kMaxUsers = 4;

    iAudioSDLUser sUsers[kMaxUsers];
    int sNumUsers;
    bool sWatching;

    // The thread the users live on. The event watch runs on whatever thread
    // pushed the event, and only closes devices itself when that is this one.
    SDL_ThreadID sGameThread;

    // Users are closed (for the background, or because opening one failed)
    // and have to be opened again.
    bool sDown;

    // A close or open is running: the watch must not start another inside it.
    bool sBusy;

    // Everything has been closed for this stay in the background.
    bool sClosedForBackground;

    // A device has been open at some point, so failing to open one now is a
    // loss to recover from rather than a machine with no audio.
    bool sEverUp;

    // When a failed restart is tried again, and how many have failed in a row.
    U64 sRetryAtNs;
    U32 sFailures;

    U32 sRestarts;

    // A device that fails again as soon as it is opened would otherwise be
    // restarted every frame. Restarts less than two seconds apart are
    // counted, and from the fourth in a row the next waits 1, 2, 4... up to 30
    // seconds. The foreground is never held.
    Uint64 sLastRestartNs;
    U32 sQuickRestarts;
    Uint64 sHoldUntilNs;

    SDL_AtomicInt sBackground;
    SDL_AtomicInt sSawBackground;
    SDL_AtomicInt sPending;

    // A removed playback device left to SDL to deal with (the watch), seen
    // since the last poll; and when to look at what SDL made of it
    // (CheckRemoved).
    SDL_AtomicInt sRemovedLeft;
    Uint64 sCheckDevicesAtNs;
    const Uint64 kCheckDevicesNs = 2 * SDL_NS_PER_SECOND;

    // The physical devices those removals named, since the game's devices
    // were last opened, for CheckRemoved. More than this many and CheckRemoved
    // restarts to be safe. Under the spinlock: the watch runs on whatever
    // thread pushes the event.
    const int kMaxRemoved = 16;
    SDL_SpinLock sRemovedLock;
    SDL_AudioDeviceID sRemoved[kMaxRemoved];
    int sNumRemoved;
    bool sRemovedOverflow;

    // The SDL audio drivers that move every device opened as the default --
    // both of the game's are -- onto a new default device themselves when the
    // system names one: the ones that call SDL_DefaultAudioDeviceChanged
    // (SDL_wasapi.c; SDL_directsound.c through SDL_immdevice.c, on Windows'
    // own notification thread; SDL_pulseaudio.c, SDL_pipewire.c, and
    // SDL_coreaudio.m on macOS only). Until it names one, SDL keeps the
    // devices on a stand-in that swallows their sound and keeps time. Where a
    // driver here does not move them after all (DirectSound without Windows'
    // device notifications), CheckRemoved restarts two seconds later.
    bool DriverMovesDevices()
    {
        const char* driver = SDL_GetCurrentAudioDriver();
        if (driver == NULL)
        {
            return false;
        }

        static const char* const kMoving[] = {
            "wasapi",    "directsound", "pulseaudio", "pipewire",
#ifdef SDL_PLATFORM_MACOS
            "coreaudio",
#endif
        };
        for (size_t i = 0; i < sizeof(kMoving) / sizeof(kMoving[0]); i++)
        {
            if (SDL_strcasecmp(driver, kMoving[i]) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void NoteRemoved(SDL_AudioDeviceID physical)
    {
        SDL_LockSpinlock(&sRemovedLock);
        if (sNumRemoved < kMaxRemoved)
        {
            sRemoved[sNumRemoved++] = physical;
        }
        else
        {
            sRemovedOverflow = true;
        }
        SDL_UnlockSpinlock(&sRemovedLock);
    }

    // Once every device is closed and SDL's audio is down, the ids are gone
    // with the devices they named.
    void ForgetRemoved()
    {
        SDL_LockSpinlock(&sRemovedLock);
        sNumRemoved = 0;
        sRemovedOverflow = false;
        SDL_UnlockSpinlock(&sRemovedLock);
    }

    // What SDL made of the removals the watch left to it, two seconds after
    // the last one. Game thread. A restart when the game's sound is left going
    // nowhere with nothing to move it:
    //
    // - No playback device left at all: SDL has nowhere to move the game's
    //   devices, and they stay on its stand-in until the system names a new
    //   default. The restart's retries bring the sound back once there is a
    //   device again, whatever the system does about the default.
    // - A removed device SDL still holds. SDL keeps a removed physical device
    //   (as a stand-in) only while something holds it: a logical device on
    //   it, which can only be one of the game's two, or SDL's own hold on the
    //   default device until a new default replaces it. Once SDL has moved
    //   the game's devices to a new default, nothing holds the old one and
    //   SDL forgets it. Still known two seconds on, it has the game's sound
    //   (or is still the default, which has the game's sound unless an
    //   earlier move failed): SDL could not open the new default and "leaves
    //   everything on the old default" (SDL_DefaultAudioDeviceChanged) -- and
    //   it only ever moves devices off the current default, which this one no
    //   longer is -- or the system has named no new default (a device that
    //   failed without going away), or SDL moved the devices onto a device
    //   that was already gone. Asked by id, not by name: a device that comes
    //   back is a new device in SDL, with the same name.
    //
    // The ids are looked up under SDL's subsystem lock only, never a device's,
    // so this cannot wait on SDL's own thread while it moves the devices.
    void CheckRemoved()
    {
        SDL_AudioDeviceID removed[kMaxRemoved];
        SDL_LockSpinlock(&sRemovedLock);
        const int n = sNumRemoved;
        const bool overflow = sRemovedOverflow;
        for (int i = 0; i < n; i++)
        {
            removed[i] = sRemoved[i];
        }
        sNumRemoved = 0;
        sRemovedOverflow = false;
        SDL_UnlockSpinlock(&sRemovedLock);

        // Closed and coming back by itself (the retries), or not up at all.
        if (sDown)
        {
            return;
        }

        int count = 0;
        SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
        SDL_free(devices);
        if (count == 0)
        {
            printf("tssm: audio: no playback device is left\n");
            fflush(stdout);
            iAudioSDLRestart(iAUDIOSDL_REMOVED);
            return;
        }

        for (int i = 0; i < n; i++)
        {
            const char* name = SDL_GetAudioDeviceName(removed[i]);
            if (name != NULL)
            {
                printf("tssm: audio: playback device \"%s\" was removed and SDL still holds it: "
                       "the game's sound is left on it\n",
                       name);
                fflush(stdout);
                iAudioSDLRestart(iAUDIOSDL_REMOVED);
                return;
            }
        }

        if (overflow)
        {
            printf("tssm: audio: more playback devices removed than followed; restarting to be "
                   "safe\n");
            fflush(stdout);
            iAudioSDLRestart(iAUDIOSDL_REMOVED);
        }
    }

    const char* ReasonName(int why)
    {
        switch (why)
        {
        case iAUDIOSDL_FOREGROUND:
            return "back in the foreground";
        case iAUDIOSDL_REMOVED:
            return "a playback device was removed";
        case iAUDIOSDL_STALLED:
            return "the device stopped asking for audio";
        case iAUDIOSDL_RETRY:
            return "trying again";
        default:
            return "asked";
        }
    }

    // Last user first, so the order is the reverse of opening.
    void CloseAll()
    {
        sBusy = true;
        for (int i = sNumUsers - 1; i >= 0; i--)
        {
            sUsers[i](false);
        }
        sBusy = false;
        sDown = true;
        ForgetRemoved();

        // Every reference has to have gone for SDL to drop a dead device. One
        // that has not is somebody else's, and the restart cannot help.
        if (SDL_WasInit(SDL_INIT_AUDIO) != 0)
        {
            printf("tssm: audio: SDL audio is still held after closing every device; a lost "
                   "device may not come back\n");
            fflush(stdout);
        }
    }

    void OpenAll()
    {
        bool ok = true;

        sBusy = true;
        for (int i = 0; i < sNumUsers; i++)
        {
            if (!sUsers[i](true))
            {
                ok = false;
            }
        }
        sBusy = false;

        if (ok)
        {
            sDown = false;
            sFailures = 0;
            sRetryAtNs = 0;
            sEverUp = true;
            return;
        }

        // Left down, so the users that did open are closed again with the rest
        // at the next attempt: every device has to go for SDL's to be rebuilt.
        sDown = true;
        sFailures++;

        if (!sEverUp)
        {
            // Never had a device: the machine has no audio, which is the
            // silent path's business, not a loss. Only the foreground retries.
            sRetryAtNs = 0;
            return;
        }

        U64 seconds = sFailures < 6 ? (1ULL << (sFailures - 1)) : 30;
        sRetryAtNs = SDL_GetTicksNS() + seconds * SDL_NS_PER_SECOND;

        printf("tssm: audio: no device yet; trying again in %u s\n", (unsigned)seconds);
        fflush(stdout);
    }

    bool SDLCALL Watch(void*, SDL_Event* e)
    {
        switch (e->type)
        {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            SDL_SetAtomicInt(&sBackground, 1);
            SDL_SetAtomicInt(&sSawBackground, 1);

            // On Android this is the game thread inside SDL_PumpEvents, and the
            // last thing it does before pausing the audio and blocking until
            // the app comes back. The devices are closed now, so there is
            // nothing for that pause to leave broken. Elsewhere it waits for
            // iAudioSDLPoll.
            if (SDL_GetCurrentThreadID() == sGameThread && !sBusy && !sClosedForBackground)
            {
                CloseAll();
                sClosedForBackground = true;
            }
            break;

        case SDL_EVENT_DID_ENTER_FOREGROUND:
            SDL_SetAtomicInt(&sBackground, 0);

            // SDL sends the foreground events when the app first starts, too,
            // which is no reason to open a device again.
            //
            // Set rather than asked for: a restart still pending from before
            // the background (a loss whose restart was being held back) would
            // otherwise keep its reason, and with it the hold, and the app
            // would come back silent until the hold ran out.
            if (SDL_GetAtomicInt(&sSawBackground) != 0)
            {
                SDL_SetAtomicInt(&sPending, (int)iAUDIOSDL_FOREGROUND);
            }
            break;

        // A playback device removed: a restart only when it is one the game
        // plays on and SDL will not move the game off it.
        //
        // - One of the game's own devices (a logical one, which it opened):
        //   SDL sends that when it gives up on one, and it does not come back.
        //   (Where SDL moves devices, it also sends it for a device opened as
        //   the default when it handles the loss after the system has named a
        //   new default but before it has moved the device there: a restart
        //   that was not needed, and a safe one.)
        // - A physical device, on a driver that does not move the devices
        //   opened as the default. On Android there is only the one, which is
        //   ours, and nothing to move it to (iAudioSDL.h).
        //
        // Where the driver moves them (Windows, macOS, PulseAudio, PipeWire),
        // SDL moves both of the game's devices onto the new default itself,
        // and a device the game does not play on is none of its business. A
        // restart there bought nothing: it cost a gap in the sound and a stall
        // of the game thread at every removal, and it ran at the very moment
        // SDL was moving the devices, which is how the game froze in a movie
        // (iFMVAudioSDL.cpp, Drop). Two seconds after the last such removal,
        // iAudioSDLPoll looks at what SDL made of it (CheckRemoved) and
        // restarts if SDL left the game's sound going nowhere: no device left,
        // or a new default SDL could not open.
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
            if (!e->adevice.recording)
            {
                if (!SDL_IsAudioDevicePhysical(e->adevice.which) || !DriverMovesDevices())
                {
                    iAudioSDLRestart(iAUDIOSDL_REMOVED);
                }
                else
                {
                    const char* name = SDL_GetAudioDeviceName(e->adevice.which);
                    printf("tssm: audio: playback device \"%s\" removed; no restart, SDL moves "
                           "the game's devices to the new default itself\n",
                           name != NULL ? name : "?");
                    fflush(stdout);
                    NoteRemoved(e->adevice.which);
                    SDL_SetAtomicInt(&sRemovedLeft, 1);
                }
            }
            break;

        default:
            break;
        }

        return true;
    }
} // namespace

void iAudioSDLAttach(iAudioSDLUser user, bool opened)
{
    if (user == NULL)
    {
        return;
    }

    if (opened)
    {
        sEverUp = true;
    }

    for (int i = 0; i < sNumUsers; i++)
    {
        if (sUsers[i] == user)
        {
            return;
        }
    }

    if (sNumUsers >= kMaxUsers)
    {
        return;
    }

    sUsers[sNumUsers++] = user;
    sGameThread = SDL_GetCurrentThreadID();

    if (!sWatching)
    {
        sWatching = SDL_AddEventWatch(Watch, NULL);
    }
}

void iAudioSDLDetach(iAudioSDLUser user)
{
    for (int i = 0; i < sNumUsers; i++)
    {
        if (sUsers[i] == user)
        {
            for (int j = i + 1; j < sNumUsers; j++)
            {
                sUsers[j - 1] = sUsers[j];
            }
            sNumUsers--;
            break;
        }
    }

    if (sNumUsers == 0)
    {
        if (sWatching)
        {
            SDL_RemoveEventWatch(Watch, NULL);
            sWatching = false;
        }

        sDown = false;
        sClosedForBackground = false;
        sRetryAtNs = 0;
        sFailures = 0;
        sCheckDevicesAtNs = 0;
        SDL_SetAtomicInt(&sPending, 0);
        SDL_SetAtomicInt(&sRemovedLeft, 0);
        ForgetRemoved();
    }
}

void iAudioSDLRestart(iAudioSDLReason why)
{
    // The foreground is set rather than asked for, as the watch sets it: a
    // restart still pending and held back (a stall) would otherwise keep its
    // reason, and with it the hold, and the game would come back silent for
    // up to 30 seconds. Otherwise the first reason asked for is the one
    // reported.
    if (why == iAUDIOSDL_FOREGROUND)
    {
        SDL_SetAtomicInt(&sPending, (int)why);
        return;
    }

    SDL_CompareAndSwapAtomicInt(&sPending, 0, (int)why);
}

void iAudioSDLPoll()
{
    if (sNumUsers == 0 || sBusy || SDL_GetCurrentThreadID() != sGameThread)
    {
        return;
    }

    // In the background with a device still open: the watch ran on another
    // thread. Close here, and open nothing until the foreground.
    if (SDL_GetAtomicInt(&sBackground) != 0)
    {
        if (!sClosedForBackground)
        {
            CloseAll();
            sClosedForBackground = true;
        }
        return;
    }

    sClosedForBackground = false;

    Uint64 now = SDL_GetTicksNS();

    // A removal the watch left to SDL: what SDL made of it is looked at a
    // moment after the last one, once SDL is done moving the devices
    // (CheckRemoved). A move SDL is still making then (a new default slow to
    // open) gets a restart it did not need, which waits for SDL to finish.
    if (SDL_SetAtomicInt(&sRemovedLeft, 0) != 0)
    {
        sCheckDevicesAtNs = now + kCheckDevicesNs;
    }
    if (sCheckDevicesAtNs != 0 && now >= sCheckDevicesAtNs)
    {
        sCheckDevicesAtNs = 0;
        CheckRemoved();
    }

    int why = SDL_GetAtomicInt(&sPending);

    if (why == iAUDIOSDL_NONE)
    {
        if (!sDown || sRetryAtNs == 0 || now < sRetryAtNs)
        {
            return;
        }
        why = iAUDIOSDL_RETRY;
    }

    if (why != iAUDIOSDL_FOREGROUND && now < sHoldUntilNs)
    {
        return;
    }

    SDL_SetAtomicInt(&sPending, 0);

    sRestarts++;

    // Measured from when the last restart was allowed to happen: from the end
    // of its hold, if it set one. From the restart itself, a hold of two
    // seconds or more would always end the count, and the wait would never
    // grow past two seconds.
    Uint64 since = sHoldUntilNs > sLastRestartNs ? sHoldUntilNs : sLastRestartNs;
    sQuickRestarts = (sLastRestartNs != 0 && (now < since || now - since < 2 * SDL_NS_PER_SECOND)) ?
                         sQuickRestarts + 1 :
                         0;
    sLastRestartNs = now;
    sHoldUntilNs = 0;
    if (sQuickRestarts >= 3)
    {
        U64 seconds = sQuickRestarts < 8 ? (1ULL << (sQuickRestarts - 3)) : 30;
        sHoldUntilNs = now + seconds * SDL_NS_PER_SECOND;
    }

    if (why != iAUDIOSDL_RETRY)
    {
        printf("tssm: audio: restarting the audio devices (%s)\n", ReasonName(why));
        fflush(stdout);
    }

    CloseAll();
    OpenAll();

    if (!sDown)
    {
        const char* driver = SDL_GetCurrentAudioDriver();
        printf("tssm: audio: devices open again (%s)\n", driver != NULL ? driver : "?");
        fflush(stdout);
    }
}

U32 iAudioSDLRestarts()
{
    return sRestarts;
}
