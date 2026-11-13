// TSSM test hook, the part inside SDL: TSSM_TEST_AUDIO_RESTART's lost, swap,
// swapfail, default and other (iAudioSDLTest.h).
//
// These are the calls SDL's own WASAPI backend makes when Windows reports a
// playback device gone or a new default device (SDL_immdevice.c's
// IMMNotificationClient, SDL_wasapi.c's AudioDeviceDisconnected and
// DefaultAudioDeviceChanged), made from inside the process, so that a test can
// put SDL through a device loss without anything on the machine changing. They
// need SDL's private audio structures, so CMakeLists.txt compiles this file into
// SDL3-static itself: Windows, SDL built from third_party, audio backend sdl.
// Nothing here runs unless a test asks for it.

#include "SDL_internal.h"

#include "audio/SDL_sysaudio.h"

#ifdef SDL_AUDIO_DRIVER_WASAPI
#include "core/windows/SDL_windows.h"
#define COBJMACROS
#include <audioclient.h>
#include "audio/wasapi/SDL_wasapi.h"
#endif

// Mirrored in iAudioSDLTest.cpp.
typedef struct TSSM_SDLTestDevice
{
    SDL_AudioDeviceID id;
    char name[96];
    int logical; // logical devices open on it (the game's are opened as the default)
    int zombie; // SDL has taken it as lost
    int opened; // open at the OS level
} TSSM_SDLTestDevice;

#define TSSM_MAX_DEVICES 32

typedef struct
{
    SDL_AudioDevice* devices[TSSM_MAX_DEVICES];
    int count;
    SDL_AudioDeviceID only;
} TSSM_Collect;

// Under SDL's subsystem lock (read), which must not be held while a device's
// own lock is taken: only a reference here, the device lock afterwards.
static bool TSSM_CollectOne(SDL_AudioDevice* device, void* userdata)
{
    TSSM_Collect* c = (TSSM_Collect*)userdata;
    if (device->recording || c->count >= TSSM_MAX_DEVICES)
    {
        return false;
    }
    if (c->only != 0 && device->instance_id != c->only)
    {
        return false;
    }
    RefPhysicalAudioDevice(device);
    c->devices[c->count++] = device;
    return false; // keep going: every device
}

static void TSSM_Collect_(TSSM_Collect* c, SDL_AudioDeviceID only)
{
    c->count = 0;
    c->only = only;
    if (SDL_GetCurrentAudioDriver() != NULL)
    {
        SDL_FindPhysicalAudioDeviceByCallback(TSSM_CollectOne, c);
        SDL_ClearError(); // "Device not found": the callback never says it found one
    }
}

static bool TSSM_IsWasapi(void)
{
    const char* driver = SDL_GetCurrentAudioDriver();
    return driver != NULL && SDL_strcmp(driver, "wasapi") == 0;
}

int TSSM_SDLTestPlaybackDevices(TSSM_SDLTestDevice* out, int max)
{
    TSSM_Collect c;
    TSSM_Collect_(&c, 0);

    int n = 0;
    for (int i = 0; i < c.count; i++)
    {
        SDL_AudioDevice* device = c.devices[i];
        if (n < max)
        {
            TSSM_SDLTestDevice* o = &out[n++];
            SDL_LockMutex(device->lock);
            o->id = device->instance_id;
            SDL_strlcpy(o->name, device->name != NULL ? device->name : "?", sizeof(o->name));
            o->zombie = SDL_GetAtomicInt(&device->zombie);
            o->opened = device->currently_opened ? 1 : 0;
            o->logical = 0;
            for (SDL_LogicalAudioDevice* l = device->logical_devices; l != NULL; l = l->next)
            {
                o->logical++;
            }
            SDL_UnlockMutex(device->lock);
        }
        UnrefPhysicalAudioDevice(device);
    }
    return n;
}

// Whether the logical device `logical` sits on a physical device SDL has taken
// as lost (its stand-in, which swallows the sound and keeps time): 1 if so, 0
// if it is on a working one, -1 if no device has it open.
int TSSM_SDLTestOnLostDevice(SDL_AudioDeviceID logical)
{
    TSSM_Collect c;
    TSSM_Collect_(&c, 0);

    int result = -1;
    for (int i = 0; i < c.count; i++)
    {
        SDL_AudioDevice* device = c.devices[i];
        if (result < 0 && logical != 0)
        {
            SDL_LockMutex(device->lock);
            for (SDL_LogicalAudioDevice* l = device->logical_devices; l != NULL; l = l->next)
            {
                if (l->instance_id == logical)
                {
                    result = SDL_GetAtomicInt(&device->zombie) != 0 ? 1 : 0;
                    break;
                }
            }
            SDL_UnlockMutex(device->lock);
        }
        UnrefPhysicalAudioDevice(device);
    }
    return result;
}

// Windows says the device is no longer there (OnDeviceStateChanged, not
// active), from its notification thread: SDL_immdevice.c calls the WASAPI
// backend's AudioDeviceDisconnected, which is WASAPI_DisconnectDevice. SDL
// marks the device lost (a zombie that swallows audio), and on the main
// thread's next SDL_PumpEvents sends SDL_EVENT_AUDIO_DEVICE_REMOVED.
bool TSSM_SDLTestLose(SDL_AudioDeviceID physical)
{
    TSSM_Collect c;
    TSSM_Collect_(&c, physical);
    if (c.count == 0)
    {
        return false;
    }
    SDL_AudioDevice* device = c.devices[0];

#ifdef SDL_AUDIO_DRIVER_WASAPI
    if (TSSM_IsWasapi())
    {
        WASAPI_DisconnectDevice(device);
    }
    else
#endif
    {
        SDL_AudioDeviceDisconnected(device);
    }

    UnrefPhysicalAudioDevice(device);
    return true;
}

#ifdef SDL_AUDIO_DRIVER_WASAPI
// SDL_wasapi.c's mgmtthrtask_DefaultAudioDeviceChanged, which this mirrors.
static bool TSSM_NewDefaultTask(void* userdata)
{
    SDL_AudioDevice* device = (SDL_AudioDevice*)userdata;
    SDL_DefaultAudioDeviceChanged(device);
    UnrefPhysicalAudioDevice(device); // the reference taken for the task
    return true;
}
#endif

// Windows names a new default device (OnDefaultDeviceChanged): SDL_wasapi.c's
// DefaultAudioDeviceChanged queues SDL_DefaultAudioDeviceChanged on its
// management thread without waiting, and that moves every logical device
// opened as the default onto the new one.
bool TSSM_SDLTestNewDefault(SDL_AudioDeviceID physical)
{
    TSSM_Collect c;
    TSSM_Collect_(&c, physical);
    if (c.count == 0)
    {
        return false;
    }
    SDL_AudioDevice* device = c.devices[0]; // this reference goes to the task

#ifdef SDL_AUDIO_DRIVER_WASAPI
    if (TSSM_IsWasapi())
    {
        if (WASAPI_ProxyToManagementThread(TSSM_NewDefaultTask, device, NULL))
        {
            return true;
        }
        UnrefPhysicalAudioDevice(device);
        return false;
    }
#endif

    SDL_DefaultAudioDeviceChanged(device);
    UnrefPhysicalAudioDevice(device);
    return true;
}

#ifdef SDL_AUDIO_DRIVER_WASAPI
// SDL_immdevice.c's SDL_IMMDevice_HandleData, which is private to that file:
// the WASAPI backend's handle on a device, freed with the device
// (SDL_IMMDevice_FreeDeviceHandle, with SDL_free).
typedef struct TSSM_IMMDeviceHandle
{
    LPWSTR immdevice_id;
    GUID directsound_guid;
} TSSM_IMMDeviceHandle;
#endif

// Windows names a new default device that SDL cannot open (one another program
// holds in exclusive mode, say): a playback device is added to SDL's list as
// SDL_immdevice.c adds one, and made the default as DefaultAudioDeviceChanged
// does. SDL_DefaultAudioDeviceChanged makes it the default, fails to open it,
// and leaves every logical device where it was -- the game's on the device they
// were on, lost or not. Its WASAPI id is not an endpoint id at all, which
// Windows refuses at once (E_INVALIDARG); an id of the right form that names no
// endpoint would get E_NOTFOUND, which SDL waits up to eight seconds on. The
// device goes with SDL's audio at the next restart. WASAPI only.
bool TSSM_SDLTestNewDefaultWontOpen(void)
{
#ifdef SDL_AUDIO_DRIVER_WASAPI
    if (!TSSM_IsWasapi())
    {
        return false;
    }

    TSSM_IMMDeviceHandle* handle = (TSSM_IMMDeviceHandle*)SDL_calloc(1, sizeof(*handle));
    if (handle == NULL)
    {
        return false;
    }
    handle->immdevice_id = SDL_wcsdup(L"tssm-test-will-not-open");
    if (handle->immdevice_id == NULL)
    {
        SDL_free(handle);
        return false;
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = 48000;
    SDL_AudioDevice* device =
        SDL_AddAudioDevice(false, "TSSM test: a default that will not open", &spec, handle);
    if (device == NULL)
    {
        SDL_free(handle->immdevice_id);
        SDL_free(handle);
        return false;
    }

    RefPhysicalAudioDevice(device); // the task's, as DefaultAudioDeviceChanged takes it
    if (!WASAPI_ProxyToManagementThread(TSSM_NewDefaultTask, device, NULL))
    {
        UnrefPhysicalAudioDevice(device);
        return false;
    }
    return true;
#else
    return false;
#endif
}

// Gain 0 on every logical device open on `physical`, so that a test that moves
// them onto another device plays silence there. Returns how many.
int TSSM_SDLTestSilence(SDL_AudioDeviceID physical)
{
    TSSM_Collect c;
    TSSM_Collect_(&c, physical);
    if (c.count == 0)
    {
        return 0;
    }
    SDL_AudioDevice* device = c.devices[0];

    SDL_AudioDeviceID ids[16];
    int n = 0;
    SDL_LockMutex(device->lock);
    for (SDL_LogicalAudioDevice* l = device->logical_devices; l != NULL && n < 16; l = l->next)
    {
        ids[n++] = l->instance_id;
    }
    SDL_UnlockMutex(device->lock);
    UnrefPhysicalAudioDevice(device);

    for (int i = 0; i < n; i++)
    {
        SDL_SetAudioDeviceGain(ids[i], 0.0f);
    }
    return n;
}
