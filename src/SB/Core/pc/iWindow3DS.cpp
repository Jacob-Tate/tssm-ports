#include "iWindow.h"

// The window seam on the Nintendo 3DS, while the build draws nothing (librw's
// NULL backend; docs/3DS.md).
//
// iWindowNull.cpp with one difference: SDL's video subsystem is opened and
// pumped, though nothing is drawn through it. On the 3DS, SDL's video driver
// is what scans the buttons and the touch screen (hidScanInput, which its
// joystick driver then reads) and what runs the APT main loop (aptMainLoop:
// HOME, sleep, and the system asking the program to close, which it turns
// into SDL_EVENT_QUIT). Without it there is no input and no way out.
//
// The screens are SDL's (gfxInit in its VideoInit) and stay blank, but for
// the spike's text (3ds/i3DSSpike.cpp). When a citro3d device exists, it owns
// them instead and this file goes with it.

#include "iHost.h"
#include "iSpike.h"
#include "iSteadyFrame.h"
#include "3ds/i3DS.h"
#ifdef RW_C3D
#include "3ds/c3d/rwc3d.h"
#endif

#include <SDL3/SDL.h>

#include <stdio.h>

namespace
{
    // The top screen. iScreen asks for its render size elsewhere; this is only
    // what iWindowGetDisplaySize answers.
    const S32 kTopWidth = 400;
    const S32 kTopHeight = 240;

    bool sOpened;
    bool sVideo;
    S32 sWidth;
    S32 sHeight;
    S32 sFrameRate = 60;
    S32 sVSync = TRUE;
    S32 sShouldClose;

    bool sPacerStarted;
    U64 sNextFrame;

    // iWindowSetFrameRateCeiling's ceiling, 0 for none.
    S32 sFrameRateCeiling;

    S32 PacedRate()
    {
        if (sFrameRateCeiling > 0 && (sFrameRate <= 0 || sFrameRate > sFrameRateCeiling))
        {
            return sFrameRateCeiling;
        }
        return sFrameRate;
    }

    void ApplyRate()
    {
        sPacerStarted = false;
#ifdef RW_C3D
        rw::c3d::setFrameRate((int)PacedRate());
#endif
    }

#ifdef TSSM_3DS_OVERLAY
    // The bottom screen's, for the settings overlay (gui/iGui.cpp), which
    // draws there through citro3d. SDL's 3DS driver only notes which screen
    // a window is on: it makes no framebuffer and draws nothing.
    SDL_Window* sOverlay;

    void MakeOverlayWindow()
    {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        SDL_DisplayID bottom = 0;
        for (int i = 0; displays != NULL && i < count; i++)
        {
            const char* name = SDL_GetDisplayName(displays[i]);
            if (name != NULL && SDL_strstr(name, "bottom") != NULL)
            {
                bottom = displays[i];
            }
        }
        SDL_free(displays);
        if (bottom == 0)
        {
            printf("tssm: 3ds: SDL has no bottom screen; no settings overlay\n");
            return;
        }
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "TSSM overlay");
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                              SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottom));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                              SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottom));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 320);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 240);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
        sOverlay = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (sOverlay == NULL)
        {
            printf("tssm: 3ds: no window for the settings overlay (%s)\n", SDL_GetError());
        }
    }
#endif
} // namespace

S32 iWindowOpen(const iWindowParams* params)
{
    if (params == NULL || sOpened)
    {
        return FALSE;
    }

    // No window: SDL's 3DS driver needs none to pump events.
#ifdef TSSM_3DS_OVERLAY
    // The overlay takes the bottom screen's touches as they are.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
    sVideo = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (!sVideo)
    {
        // Not fatal for the game, but there will be no input: say so.
        printf("tssm: 3ds: SDL video did not start (%s); no buttons will be read\n",
               SDL_GetError());
    }

    // SDL's video has just set both screens up afresh (gfxInit); the spike's
    // text on them, when it has shown any, is put back.
    i3DSSpikeScreensReset();
    i3DSProbeBanner();

#ifdef TSSM_3DS_OVERLAY
    if (sVideo && !iSpikeOn())
    {
        MakeOverlayWindow();
    }
#endif

    sOpened = true;
    sWidth = params->width;
    sHeight = params->height;
    return TRUE;
}

void iWindowClose()
{
#ifdef TSSM_3DS_OVERLAY
    if (sOverlay != NULL)
    {
        SDL_DestroyWindow(sOverlay);
        sOverlay = NULL;
    }
#endif
    if (sVideo)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        sVideo = false;
    }
    sOpened = false;
    sWidth = 0;
    sHeight = 0;
}

// As iWindowSDL.cpp: SDL has one event queue, and iPadHostSDL.cpp's
// iPadHostPoll takes the joystick and gamepad range of it. This side takes the
// rest -- the quit -- and drops what nothing reads (touch, sensors).
void iWindowPump()
{
    SDL_PumpEvents();

    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                          SDL_EVENT_JOYSTICK_AXIS_MOTION - 1) > 0)
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            sShouldClose = 1;
        }
    }

    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FINGER_DOWN, SDL_EVENT_LAST) > 0)
    {
    }
}

// The period the pacer below slept to for the frame just shown, or 0 when it
// did not pace the frame (citro3d paced it, there is no cap, or the frame was
// late), for iWindowGameFrameTime.
static U64 sPacedPeriodNs;

// The same deadline pacing as iWindowSDL.cpp, without SDL_DelayPrecise: its
// spin for the last fraction would hold core 0, which is cooperative on the
// 3DS, and the kernel's sleep is fine-grained enough on its own.
void iWindowPaceFrame()
{
    // The spike runs its frames back to back and keeps the game's time step
    // itself (iSpike.h).
    sPacedPeriodNs = 0;
    const S32 rate = PacedRate();
    if (rate <= 0 || iSpikeOn())
    {
        sPacerStarted = false;
        return;
    }

#ifdef RW_C3D
    // citro3d waits for the display itself (C3D_FrameBegin, at the rate
    // iWindowSetFrameRate gave it).
    if (rw::c3d::pacesFrames())
    {
        sPacerStarted = false;
        return;
    }
#endif

    const U64 framePeriodNs = 1000000000ULL / (U64)rate;
    const U64 now = iHostMonotonicNs();

    if (!sPacerStarted)
    {
        sNextFrame = now;
        sPacerStarted = true;
    }

    sNextFrame += framePeriodNs;

    // A frame that overran is not paid back by skipping the next sleeps.
    if (sNextFrame <= now)
    {
        sNextFrame = now;
        return;
    }

    iHostSleepUntilNs(sNextFrame);
    sPacedPeriodNs = framePeriodNs;
}

// The game's frame time (iSteadyFrame.h, zGameLoop). citro3d paces the frame
// (C3D_FrameBegin) and the pacer above returns before its sleep, so the game
// keeps the measured time there: the 3DS runs under 30 fps, and two of its
// screen's refreshes are not 1/30 s (src/SB/Core/pc/PORTING.md).
static iSteadyFrame sSteady;

F32 iWindowGameFrameTime(F32 measured)
{
    return iSteadyFrameTime(&sSteady, sPacedPeriodNs, measured);
}

void iWindowSetFrameRate(S32 fps)
{
    sFrameRate = fps > 0 ? fps : 0;
    ApplyRate();
}

void iWindowSetFrameRateCeiling(S32 fps)
{
    const S32 before = PacedRate();
    sFrameRateCeiling = fps > 0 ? fps : 0;
    if (PacedRate() != before)
    {
        ApplyRate();
    }
}

// The GameCube's hold ([extras] frame_lock, iExtras.h) is not kept here: the
// console holds only its own cap, 30 frames a second unless the player raises
// it, as in 0.9.2: at 60 the GameCube's 30 fps levels run at 60 too. Holding
// them would also change how the Patty Wagon steps (iExtrasHeldCarSteps),
// which no console has run yet.
void iWindowSetFrameLock(S32 count)
{
    (void)count;
}

S32 iWindowGetFrameLock()
{
    return 0;
}

S32 iWindowGetFrameRate()
{
    return sFrameRate;
}

S32 iWindowGetPacedRate()
{
    return PacedRate();
}

// No system here takes the frame's work time.
void iWindowSetFrameWorkHook(iWindowFrameWorkFn hook)
{
    (void)hook;
}

void iWindowFrameWorkDone()
{
}

// The 3DS's LCDs refresh at about 59.8 Hz.
S32 iWindowGetDisplayRefreshRate()
{
    return 60;
}

S32 iWindowGetDisplaySize(S32* width, S32* height)
{
    *width = kTopWidth;
    *height = kTopHeight;
    return TRUE;
}

void iWindowSetVSync(S32 on)
{
    sVSync = on;
}

S32 iWindowGetVSync()
{
    return sVSync;
}

S32 iWindowShouldClose()
{
    return sShouldClose;
}

void iWindowRequestClose()
{
    sShouldClose = TRUE;
}

void iWindowGetSize(S32* width, S32* height)
{
    if (width != NULL)
    {
        *width = sWidth;
    }

    if (height != NULL)
    {
        *height = sHeight;
    }
}

iWindowMode iWindowGetMode()
{
    return iWINDOW_WINDOWED;
}

// One screen, one mode.
S32 iWindowSetMode(iWindowMode)
{
    return FALSE;
}

void iWindowSetExclusive(S32)
{
}

void* iWindowNativeHandle()
{
    return NULL;
}

// librw's NULL backend creates no window of its own.
const iWindowDeferred* iWindowDeferredParams()
{
    return NULL;
}

void iWindowDeferredCreated()
{
}

const char* iWindowBackendName()
{
#ifdef RW_C3D
    if (rw::c3d::deviceOpen())
    {
        return "3ds (citro3d)";
    }
#endif
    return "3ds (nothing drawn)";
}
