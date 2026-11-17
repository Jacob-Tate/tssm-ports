// TSSM_TEST_AUDIO_RESTART: the audio restart at a chosen moment, for unattended
// runs. See iAudioSDLTest.h. Port-only; nothing here runs unless the variable is
// set.
//
// The moments are noted on the game thread (iCapture.cpp, iFMV.cpp, iFile.cpp)
// and acted on by a thread of this file's own, which stands where Windows'
// audio notification thread stands for a real loss: the restart then reaches
// the game the way a real one does, whatever the game thread is doing at the
// time -- in a movie, a load, a menu or play.

#include "iAudioSDLTest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TSSM_AUDIO_SDL_TEST

#include "iAudioSDL.h"

#ifdef __3DS__
#include "3ds/i3DS.h"
#endif

#include <SDL3/SDL.h>

#ifdef TSSM_AUDIO_SDL_INTERNALS
// iAudioSDLTestSDL.c, compiled into SDL itself.
extern "C" {
struct TSSM_SDLTestDevice
{
    SDL_AudioDeviceID id;
    char name[96];
    int logical;
    int zombie;
    int opened;
};

int TSSM_SDLTestPlaybackDevices(TSSM_SDLTestDevice* out, int max);
bool TSSM_SDLTestLose(SDL_AudioDeviceID physical);
bool TSSM_SDLTestNewDefault(SDL_AudioDeviceID physical);
bool TSSM_SDLTestNewDefaultWontOpen(void);
int TSSM_SDLTestSilence(SDL_AudioDeviceID physical);
int TSSM_SDLTestOnLostDevice(SDL_AudioDeviceID logical);
}
#endif

namespace
{
    enum Moment
    {
        kFrame,
        kMovie,
        kDrained,
        kClosed,
        kLoad
    };

    enum How
    {
        kEvent,
        kLost,
        kSwap,
        kDefault,
        kOther,
        kBackground,
        kDrop,
        kStall,
        kAllLost,
        kSwapFail,
        kHome,
        kSleep,
        kStuck,
        kCancel
    };

    const char* const kHowNames[] = { "event", "lost",     "swap", "default", "other",
                                      "bg",    "drop",     "stall", "alllost", "swapfail",
                                      "home",  "sleep",    "stuck", "cancel" };

    struct Entry
    {
        char text[96];

        Moment moment;
        S32 frame;
        char name[48]; // lower case; empty matches any

        U32 ms;
        U32 count;
        U32 gap;

        How how;
        char target[48]; // lower case; empty: the first other device
        U32 swapDelay;
        bool swapOnRestart; // ~r: the new default as the restart begins

        // When the moment came (0: not yet), and how many have fired. Under
        // sLock once the thread is up.
        Uint64 atNs;
        U32 fired;
    };

    const int kMaxEntries = 16;

    Entry sEntries[kMaxEntries];
    int sCount;
    bool sOn;
    bool sStarted;
    bool sFirstFrame;

    SDL_Mutex* sLock;
    SDL_Thread* sThread;

    // For the log only.
    SDL_AtomicInt sFrameNow;

    // The level lines (iAudioSDLTest.h): when the last one was written, and
    // the mixer's callback count then. Game thread only.
    Uint64 sLevelAtNs;
    U32 sLevelCallbacks;

    void Lower(char* s)
    {
        for (; *s != '\0'; s++)
        {
            *s = (char)tolower((unsigned char)*s);
        }
    }

    // Whether `name` (a lower-case filter) is in `text`, in any case.
    bool Matches(const char* name, const char* text)
    {
        if (name[0] == '\0')
        {
            return true;
        }
        if (text == NULL)
        {
            return false;
        }
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", text);
        Lower(buf);
        return strstr(buf, name) != NULL;
    }

    bool ParseU32(const char*& p, U32* out)
    {
        char* end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p)
        {
            return false;
        }
        *out = (U32)v;
        p = end;
        return true;
    }

    // Up to (not including) the first of `stops`, lower-cased, into `out`.
    void TakeName(const char*& p, const char* stops, char* out, size_t outsize)
    {
        size_t n = 0;
        while (*p != '\0' && strchr(stops, *p) == NULL)
        {
            if (n + 1 < outsize)
            {
                out[n++] = *p;
            }
            p++;
        }
        out[n] = '\0';
        Lower(out);
    }

    bool ParseEntry(const char* text, Entry* e)
    {
        memset(e, 0, sizeof(*e));
        snprintf(e->text, sizeof(e->text), "%s", text);
        e->count = 1;
        e->gap = 100;
        e->how = kEvent;

        const char* p = text;

        if (*p == 'f' && isdigit((unsigned char)p[1]))
        {
            p++;
            U32 frame;
            if (!ParseU32(p, &frame) || frame < 1)
            {
                return false;
            }
            e->moment = kFrame;
            e->frame = (S32)frame;
        }
        else
        {
            static const struct
            {
                const char* word;
                Moment moment;
            } kWords[] = {
                { "movie", kMovie },
                { "drained", kDrained },
                { "closed", kClosed },
                { "load", kLoad },
            };
            bool found = false;
            for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++)
            {
                size_t n = strlen(kWords[i].word);
                if (strncmp(p, kWords[i].word, n) == 0)
                {
                    e->moment = kWords[i].moment;
                    p += n;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
            if (*p == '/')
            {
                p++;
                TakeName(p, "+*@", e->name, sizeof(e->name));
            }
        }

        if (*p == '+')
        {
            p++;
            if (!ParseU32(p, &e->ms))
            {
                return false;
            }
        }

        if (*p == '*')
        {
            p++;
            if (!ParseU32(p, &e->count) || e->count < 1)
            {
                return false;
            }
            if (*p == '/')
            {
                p++;
                if (!ParseU32(p, &e->gap))
                {
                    return false;
                }
            }
        }

        if (*p == '@')
        {
            p++;
            bool found = false;
            for (int i = 0; i < (int)(sizeof(kHowNames) / sizeof(kHowNames[0])); i++)
            {
                size_t n = strlen(kHowNames[i]);
                if (strncmp(p, kHowNames[i], n) == 0 &&
                    (p[n] == '\0' || p[n] == ':' || p[n] == '~'))
                {
                    e->how = (How)i;
                    p += n;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
            if (*p == ':')
            {
                p++;
                TakeName(p, "~", e->target, sizeof(e->target));
            }
            if (*p == '~')
            {
                p++;
                if (*p == 'r')
                {
                    p++;
                    e->swapOnRestart = true;
                }
                else if (!ParseU32(p, &e->swapDelay))
                {
                    return false;
                }
            }
        }

        return *p == '\0';
    }

    // Every audio device event SDL sends, with the device's name: what was
    // really removed and added on this machine during the run, beside what the
    // test did.
    bool SDLCALL LogWatch(void*, SDL_Event* e)
    {
        const char* kind;
        switch (e->type)
        {
        case SDL_EVENT_AUDIO_DEVICE_ADDED:
            kind = "added";
            break;
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
            kind = "removed";
            break;
        case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
            kind = "format changed";
            break;
        default:
            return true;
        }

        const SDL_AudioDeviceID which = e->adevice.which;
        const char* name = SDL_GetAudioDeviceName(which);
        // SDL's instance ids: bit 1 set for a physical device (SDL_audio.c).
        printf("tssm: test: audio: SDL event at frame %d: %s %s %s device %u \"%s\"\n",
               (int)SDL_GetAtomicInt(&sFrameNow), kind, (which & 2) ? "physical" : "logical",
               e->adevice.recording ? "recording" : "playback", (unsigned)which,
               name != NULL ? name : "(no longer known)");
        fflush(stdout);
        return true;
    }

    // Pushed on the main thread, from SDL_PumpEvents' main-thread callbacks,
    // which is where SDL's own device-loss work runs before it pushes the
    // event (SDL_audio.c, SDL_AudioDeviceDisconnected_OnMainThread).
    void SDLCALL PushRemoved(void* userdata)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
        e.adevice.which = (SDL_AudioDeviceID)(uintptr_t)userdata;
        e.adevice.recording = false;
        SDL_PushEvent(&e);
    }

    // @drop: the same, for each device the game itself opened (its logical
    // devices), as SDL sends when it gives up on them
    // (SDL_AudioDeviceDisconnected_OnMainThread). Asked on the game thread,
    // which is where the users' state lives.
    void SDLCALL PushDropped(void*)
    {
        U32 mixer = 0;
        U32 movie = 0;
        U32 unused;
        bool standin;
        iSndHostSDLTestStats(&unused, NULL, &mixer);
        iFMVAudioSDLTestStats(&unused, &unused, &standin, &movie);
        printf("tssm: test: audio: drop: SDL_EVENT_AUDIO_DEVICE_REMOVED for the mixer's device %u "
               "and the movie's %u (0: none open)\n",
               (unsigned)mixer, (unsigned)movie);
        fflush(stdout);
        if (mixer != 0)
        {
            PushRemoved((void*)(uintptr_t)mixer);
        }
        if (movie != 0)
        {
            PushRemoved((void*)(uintptr_t)movie);
        }
    }

    // @bg: what Android does to the game thread when the app is put away and
    // brought back, on the main thread inside SDL_PumpEvents: the
    // will-enter-background event (iAudioSDL.cpp's watch closes every device
    // there), the thread blocked for the time away (SDL_HINT_ANDROID_BLOCK_ON_
    // PAUSE), then the did-enter-foreground event. AAudio's own pause and
    // resume of open streams have no Windows counterpart and are not done.
    void SDLCALL Background(void* userdata)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        SDL_PushEvent(&e);
        SDL_zero(e);
        e.type = SDL_EVENT_DID_ENTER_BACKGROUND;
        SDL_PushEvent(&e);

        SDL_Delay((Uint32)(uintptr_t)userdata);

        SDL_zero(e);
        e.type = SDL_EVENT_WILL_ENTER_FOREGROUND;
        SDL_PushEvent(&e);
        SDL_zero(e);
        e.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        SDL_PushEvent(&e);
    }

#ifdef __3DS__
    // @home (iAudioSDLTest.h): i3DS.cpp's HOME stand-in, `userdata` ms away.
    void SDLCALL Home(void* userdata)
    {
        i3DSTestHome((uint32_t)(uintptr_t)userdata);
    }

    // @cancel: i3DS.cpp's stand-in for HOME ended by a cancel, `userdata` ms
    // away.
    void SDLCALL Cancel(void* userdata)
    {
        i3DSTestCancel((uint32_t)(uintptr_t)userdata);
    }
#endif

#ifdef TSSM_AUDIO_SDL_INTERNALS
    const int kMaxDevices = 32;

    // The device the game plays on (the first with logical devices open), and
    // `target`'s device (the first other playback device not lost, or the first
    // whose name has `target` in it). -1 when there is none.
    void FindDevices(const char* target, TSSM_SDLTestDevice* devs, int n, int* ours, int* other)
    {
        *ours = -1;
        *other = -1;
        for (int i = 0; i < n; i++)
        {
            if (devs[i].logical > 0 && *ours < 0)
            {
                *ours = i;
            }
        }
        for (int i = 0; i < n; i++)
        {
            if (i != *ours && !devs[i].zombie && Matches(target, devs[i].name))
            {
                *other = i;
                break;
            }
        }
    }

    void LogDevices(const TSSM_SDLTestDevice* devs, int n)
    {
        for (int i = 0; i < n; i++)
        {
            printf("tssm: test: audio:   playback device %u \"%s\": %d logical open%s%s\n",
                   (unsigned)devs[i].id, devs[i].name, devs[i].logical,
                   devs[i].opened ? ", open" : "", devs[i].zombie ? ", lost" : "");
        }
    }
#endif

    // ~r: until the game thread starts a restart it had not started when
    // `before` was read (iAudioSDLRestarts), at most three seconds. Spun, not
    // slept: the point is to act within microseconds of it.
    bool WaitForRestart(U32 before)
    {
        const Uint64 t0 = SDL_GetTicksNS();
        while (iAudioSDLRestarts() == before)
        {
            if (SDL_GetTicksNS() - t0 > 3 * SDL_NS_PER_SECOND)
            {
                return false;
            }
            SDL_CPUPauseInstruction();
        }
        return true;
    }

    // `restartsBefore`: the restart count before any entry due at this moment
    // fired, for default~r (iAudioSDLTest.h).
    void Fire(const Entry& e, U32 k, double lateMs, U32 restartsBefore)
    {
        const S32 frame = (S32)SDL_GetAtomicInt(&sFrameNow);
        How how = e.how;

#ifdef TSSM_AUDIO_SDL_INTERNALS
        TSSM_SDLTestDevice devs[kMaxDevices];
        const int n = TSSM_SDLTestPlaybackDevices(devs, kMaxDevices);
        int ours, other;
        FindDevices(e.target, devs, n, &ours, &other);
        const SDL_AudioDeviceID oursId = ours >= 0 ? devs[ours].id : 0;
        const SDL_AudioDeviceID otherId = other >= 0 ? devs[other].id : 0;
#else
        const SDL_AudioDeviceID oursId = 0;
        if (how != kEvent && how != kBackground && how != kDrop && how != kStall && how != kHome &&
            how != kSleep && how != kStuck && how != kCancel)
        {
            printf("tssm: test: audio: @%s needs SDL's internals, which this build does not "
                   "have; done as @event\n",
                   kHowNames[how]);
            how = kEvent;
        }
#endif

        const char* def = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
        printf("tssm: test: audio: '%s' fires (%u of %u) at frame %d, %.1f ms late: @%s; "
               "SDL's default playback device is \"%s\"\n",
               e.text, (unsigned)k, (unsigned)e.count, (int)frame, lateMs, kHowNames[how],
               def != NULL ? def : "(none)");

#ifdef TSSM_AUDIO_SDL_INTERNALS
        LogDevices(devs, n);

        if ((how == kSwap || how == kDefault || how == kOther) && other < 0)
        {
            printf("tssm: test: audio: no other playback device%s%s%s; done as @%s\n",
                   e.target[0] ? " named like \"" : "", e.target, e.target[0] ? "\"" : "",
                   how == kOther ? "event" : (how == kSwap ? "lost" : "event"));
            how = how == kSwap ? kLost : kEvent;
        }
        if (how != kEvent && how != kOther && how != kBackground && how != kDrop && how != kStall &&
            how != kAllLost && ours < 0)
        {
            printf("tssm: test: audio: the game has no device open; done as @event\n");
            how = kEvent;
        }
#endif
        fflush(stdout);

        switch (how)
        {
        case kEvent:
            SDL_RunOnMainThread(PushRemoved, (void*)(uintptr_t)oursId, false);
            break;

        case kBackground:
            SDL_RunOnMainThread(Background,
                                (void*)(uintptr_t)(e.swapDelay > 0 ? e.swapDelay : 1000), false);
            break;

        case kDrop:
            SDL_RunOnMainThread(PushDropped, NULL, false);
            break;

        case kStall:
            iAudioSDLRestart(iAUDIOSDL_STALLED);
            break;

        case kHome:
        case kSleep:
        case kStuck:
        case kCancel:
#ifdef __3DS__
            if (how == kHome || how == kCancel)
            {
                // On the game thread inside SDL_PumpEvents, just before
                // aptMainLoop, where libctru's own HOME suspend runs.
                SDL_RunOnMainThread(how == kHome ? Home : Cancel,
                                    (void*)(uintptr_t)(e.swapDelay > 0 ? e.swapDelay : 1000), false);
            }
            else if (how == kSleep)
            {
                i3DSTestSleep(e.swapDelay > 0 ? e.swapDelay : 5000);
            }
            else
            {
                i3DSTestStuck(e.swapDelay);
            }
#else
            printf("tssm: test: audio: @%s is a 3DS stand-in; nothing done here\n", kHowNames[how]);
#endif
            break;

#ifdef TSSM_AUDIO_SDL_INTERNALS
        case kLost:
            TSSM_SDLTestLose(oursId);
            break;

        case kSwap:
        {
            const int silenced = TSSM_SDLTestSilence(oursId);
            const U32 before = iAudioSDLRestarts();
            TSSM_SDLTestLose(oursId);
            if (e.swapDelay > 0)
            {
                SDL_Delay(e.swapDelay);
            }
            const bool synced = e.swapOnRestart && WaitForRestart(before);
            TSSM_SDLTestNewDefault(otherId);
            printf("tssm: test: audio: \"%s\" lost and \"%s\" made the default%s (%d of the "
                   "game's devices at gain 0)\n",
                   devs[ours].name, devs[other].name,
                   e.swapOnRestart ?
                       (synced ? " as the restart began" : ", no restart seen in 3 s") :
                       "",
                   silenced);
            break;
        }

        case kDefault:
        {
            const int silenced = TSSM_SDLTestSilence(oursId);
            if (e.swapOnRestart && !WaitForRestart(restartsBefore))
            {
                printf("tssm: test: audio: no restart in 3 s; the new default goes now\n");
            }
            TSSM_SDLTestNewDefault(otherId);
            printf("tssm: test: audio: \"%s\" made the default (%d of the game's devices at "
                   "gain 0)\n",
                   devs[other].name, silenced);
            break;
        }

        case kOther:
            TSSM_SDLTestLose(otherId);
            printf("tssm: test: audio: \"%s\" lost\n", devs[other].name);
            break;

        case kSwapFail:
        {
            // No gain 0: nothing moves the game's devices anywhere that plays.
            const U32 before = iAudioSDLRestarts();
            TSSM_SDLTestLose(oursId);
            if (e.swapDelay > 0)
            {
                SDL_Delay(e.swapDelay);
            }
            const bool synced = e.swapOnRestart && WaitForRestart(before);
            const bool named = TSSM_SDLTestNewDefaultWontOpen();
            printf("tssm: test: audio: \"%s\" lost; %s%s\n", devs[ours].name,
                   named ? "a device SDL cannot open named the default, so SDL leaves the game's "
                           "devices on the lost one" :
                           "no device SDL cannot open could be named the default (not WASAPI)",
                   e.swapOnRestart ?
                       (synced ? ", as the restart began" : ", no restart seen in 3 s") :
                       "");
            break;
        }

        case kAllLost:
        {
            const int silenced = TSSM_SDLTestSilence(oursId);
            int lost = 0;
            for (int i = 0; i < n; i++)
            {
                if (!devs[i].zombie && TSSM_SDLTestLose(devs[i].id))
                {
                    lost++;
                }
            }
            printf("tssm: test: audio: every playback device lost (%d; %d of the game's devices "
                   "at gain 0)\n",
                   lost, silenced);
            break;
        }
#else
        default:
            break;
#endif
        }
        fflush(stdout);
    }

    int SDLCALL TriggerThread(void*)
    {
        for (;;)
        {
            const Uint64 now = SDL_GetTicksNS();

            struct
            {
                int index;
                U32 k;
                double late;
            } due[kMaxEntries];
            int n = 0;
            bool waiting = false;

            // Before anything this pass fires: default~r pairs with a restart
            // an entry due at the same moment starts (iAudioSDLTest.h).
            const U32 restartsBefore = iAudioSDLRestarts();

            SDL_LockMutex(sLock);
            for (int i = 0; i < sCount; i++)
            {
                Entry& e = sEntries[i];
                if (e.fired >= e.count)
                {
                    continue;
                }
                waiting = true;
                if (e.atNs == 0)
                {
                    continue;
                }
                const Uint64 at = e.atNs + (Uint64)(e.ms + e.fired * e.gap) * 1000000ULL;
                if (now >= at)
                {
                    e.fired++;
                    due[n].index = i;
                    due[n].k = e.fired;
                    due[n].late = (double)(now - at) / 1e6;
                    n++;
                }
            }
            SDL_UnlockMutex(sLock);

            // Outside the lock: the game thread notes moments under it.
            for (int j = 0; j < n; j++)
            {
                Fire(sEntries[due[j].index], due[j].k, due[j].late, restartsBefore);
            }

            if (!waiting)
            {
                printf("tssm: test: audio: every TSSM_TEST_AUDIO_RESTART entry has fired\n");
                fflush(stdout);
                return 0;
            }

            SDL_Delay(1);
        }
    }

    // The thread, the lock and the log watch, once SDL is up: at the first
    // frame, or at the first movie, whose sound opens before it presents.
    void EnsureStarted()
    {
        if (sStarted || !sOn)
        {
            return;
        }
        sStarted = true;

        sLock = SDL_CreateMutex();
        SDL_AddEventWatch(LogWatch, NULL);

        // The level lines' meter.
        iSndHostSDLTestMeter(true);
        U32 callbacks;
        U32 device;
        iSndHostSDLTestStats(&callbacks, NULL, &device);
        sLevelCallbacks = callbacks;
        sLevelAtNs = SDL_GetTicksNS();

#ifdef TSSM_AUDIO_SDL_INTERNALS
        TSSM_SDLTestDevice devs[kMaxDevices];
        const int n = TSSM_SDLTestPlaybackDevices(devs, kMaxDevices);
        const char* def = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
        printf("tssm: test: audio: driver %s, default playback device \"%s\", %d playback "
               "device(s):\n",
               SDL_GetCurrentAudioDriver() != NULL ? SDL_GetCurrentAudioDriver() : "(none yet)",
               def != NULL ? def : "(none)", n);
        LogDevices(devs, n);
        fflush(stdout);
#endif

        sThread = SDL_CreateThread(TriggerThread, "tssm audio test", NULL);
        if (sThread == NULL)
        {
            printf("tssm: test: audio: could not start the test's thread (%s)\n", SDL_GetError());
            fflush(stdout);
        }
        else
        {
            SDL_DetachThread(sThread);
        }
    }

    // The game thread: a moment has come.
    void Note(Moment moment, const char* name, S32 frame)
    {
        if (!sOn)
        {
            return;
        }
        EnsureStarted();
        if (sLock == NULL)
        {
            return;
        }

        const Uint64 now = SDL_GetTicksNS();

        SDL_LockMutex(sLock);
        for (int i = 0; i < sCount; i++)
        {
            Entry& e = sEntries[i];
            if (e.atNs != 0 || e.moment != moment)
            {
                continue;
            }
            const bool hit = moment == kFrame ? frame >= e.frame : Matches(e.name, name);
            if (hit)
            {
                e.atNs = now;
                printf("tssm: test: audio: '%s': its moment at frame %d%s%s%s\n", e.text,
                       (int)SDL_GetAtomicInt(&sFrameNow), name != NULL ? " (" : "",
                       name != NULL ? name : "", name != NULL ? ")" : "");
                fflush(stdout);
            }
        }
        SDL_UnlockMutex(sLock);
    }

    // A logical device as the level lines name it: the physical device it is
    // on, quoted, and " [lost]" when that is a device SDL has taken as lost
    // (its stand-in, which swallows the sound and keeps time).
    void Describe(U32 device, char* out, size_t outsize)
    {
        if (device == 0)
        {
            snprintf(out, outsize, "(none)");
            return;
        }
        const char* name = SDL_GetAudioDeviceName((SDL_AudioDeviceID)device);
        int lost = -1;
#ifdef TSSM_AUDIO_SDL_INTERNALS
        lost = TSSM_SDLTestOnLostDevice((SDL_AudioDeviceID)device);
#endif
        snprintf(out, outsize, "\"%s\"%s", name != NULL ? name : "(no longer known)",
                 lost > 0 ? " [lost]" : "");
    }

    // The game thread, about once a second: what the sound is doing
    // (iAudioSDLTest.h).
    void Level(S32 frame)
    {
        const Uint64 now = SDL_GetTicksNS();
        if (!sStarted || now - sLevelAtNs < SDL_NS_PER_SECOND)
        {
            return;
        }
        const double secs = (double)(now - sLevelAtNs) / 1e9;
        sLevelAtNs = now;

        U32 callbacks = 0;
        float peak = 0.0f;
        U32 mixer = 0;
        iSndHostSDLTestStats(&callbacks, &peak, &mixer);
        const U32 newCallbacks = callbacks - sLevelCallbacks;
        sLevelCallbacks = callbacks;

        char movie[192];
        U32 taken = 0;
        U32 queued = 0;
        bool standin = false;
        U32 device = 0;
        if (!iFMVAudioSDLTestStats(&taken, &queued, &standin, &device))
        {
            snprintf(movie, sizeof(movie), "no movie");
        }
        else if (standin)
        {
            snprintf(movie, sizeof(movie), "movie on the stand-in queue (%u queued)",
                     (unsigned)queued);
        }
        else
        {
            char where[160];
            Describe(device, where, sizeof(where));
            snprintf(movie, sizeof(movie), "movie taken %u, queued %u, on %s", (unsigned)taken,
                     (unsigned)queued, where);
        }

        char mixerWhere[160];
        Describe(mixer, mixerWhere, sizeof(mixerWhere));
        printf("tssm: test: audio: level at frame %d: mixer +%u callbacks in %.2f s, peak %.4f, "
               "on %s; %s; restarts %u\n",
               (int)frame, (unsigned)newCallbacks, secs, peak, mixerWhere, movie,
               (unsigned)iAudioSDLRestarts());
        fflush(stdout);
    }
} // namespace

void iAudioSDLTestInit()
{
    const char* spec = getenv("TSSM_TEST_AUDIO_RESTART");
    if (spec == NULL || spec[0] == '\0')
    {
        return;
    }

    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", spec);

    for (char* tok = strtok(buf, ", \t"); tok != NULL; tok = strtok(NULL, ", \t"))
    {
        if (sCount == kMaxEntries)
        {
            printf("tssm: test: audio: TSSM_TEST_AUDIO_RESTART has more than %d entries; the "
                   "rest are ignored\n",
                   kMaxEntries);
            break;
        }
        if (!ParseEntry(tok, &sEntries[sCount]))
        {
            printf("tssm: test: audio: TSSM_TEST_AUDIO_RESTART: '%s' is not "
                   "<moment>[+ms][*count[/gap]][@how] (iAudioSDLTest.h); ignored\n",
                   tok);
            continue;
        }
        sCount++;
    }

    sOn = sCount > 0;
    if (sOn)
    {
        printf("tssm: test: audio: %d TSSM_TEST_AUDIO_RESTART entr%s: %s\n", sCount,
               sCount == 1 ? "y" : "ies", spec);
#ifndef TSSM_AUDIO_SDL_INTERNALS
        printf("tssm: test: audio: this build has no SDL internals; lost, swap, swapfail, "
               "default, other and alllost are done as event\n");
#endif
    }
    fflush(stdout);
}

bool iAudioSDLTestOn()
{
    return sOn;
}

void iAudioSDLTestFrame(S32 frame)
{
    if (!sOn)
    {
        return;
    }
    SDL_SetAtomicInt(&sFrameNow, (int)frame);
    sFirstFrame = true;
    Note(kFrame, NULL, frame);
    Level(frame);
#ifdef __3DS__
    i3DSTestFrame();
#endif
}

void iAudioSDLTestMovie(const char* path, S32 what)
{
    if (!sOn)
    {
        return;
    }
    Note(what == 0 ? kMovie : (what == 1 ? kDrained : kClosed), path, 0);
}

void iAudioSDLTestLoad(const char* path)
{
    // Not before the first frame: boot.HIP and font.HIP load before there is
    // a window, let alone a device.
    if (!sOn || !sFirstFrame)
    {
        return;
    }
    Note(kLoad, path, 0);
}

#else // TSSM_AUDIO_SDL_TEST

// No SDL audio in this build: nothing to restart.
void iAudioSDLTestInit()
{
    const char* spec = getenv("TSSM_TEST_AUDIO_RESTART");
    if (spec != NULL && spec[0] != '\0')
    {
        printf("tssm: test: audio: TSSM_TEST_AUDIO_RESTART needs the SDL audio backend; "
               "ignored\n");
        fflush(stdout);
    }
}

bool iAudioSDLTestOn()
{
    return false;
}

void iAudioSDLTestFrame(S32)
{
}

void iAudioSDLTestMovie(const char*, S32)
{
}

void iAudioSDLTestLoad(const char*)
{
}

#endif // TSSM_AUDIO_SDL_TEST
