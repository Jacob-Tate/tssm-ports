#include "iWindow.h"

#include "iHangDump.h"
#include "iMouseLook.h"
#include "iPerf.h"
#include "iScreen.h"
#include "iSteadyFrame.h"
#include "rw/backend.h"

// The window, on SDL, for both render backends.
//
// The backends still disagree about WHO OWNS IT, which is the whole reason
// iWindow.h has an iWindowDeferred block:
//
//   D3D9  the port creates the window here and hands librw the HWND out of it.
//         librw makes a device against that handle and never touches the window
//         again.
//
//   GL3   librw creates the window. rw::EngineOpenParams carries an
//         `SDL_Window**` out-parameter with the size and the title beside it,
//         and startSDL3 calls SDL_CreateWindow and SDL_GL_CreateContext
//         together during RwEngineStart -- it has to, because the pixel format
//         a window is created with is part of choosing a GL context, and
//         librw's own loop walks four context profiles until one comes up.
//
// So the GL3 arm below is mostly a place to keep the request until librw asks
// for it, and the D3D9 arm creates a window and reads an HWND back out of it.
// The per-frame obligations -- the event pump, the frame pace, the close
// button -- are shared, and were duplicated between two files before this.
//
// SDL rather than CreateWindowEx for D3D9: the port already links SDL for
// controllers and audio, so this costs no dependency, and it brings the DPI
// awareness, the screensaver suppression and the display enumeration that the
// Win32 window backend had to write by hand.

#include <SDL3/SDL.h>

// librw's PNG reader, for the window icon.
#include "src/lodepng/lodepng.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Under GL3, written by librw through the slot handed out below, and null until
// RwEngineStart has returned. Under D3D9, created in iWindowOpen.
static SDL_Window* sWindow;

static S32 sShouldClose;
static S32 sWidth;
static S32 sHeight;
static iWindowMode sMode = iWINDOW_WINDOWED;
static S32 sExclusive;
static bool sTour;

// Where the window was the last time it was windowed, for iWindowSetMode to put
// it back. Zero until then.
static int sWindowedX;
static int sWindowedY;
static int sWindowedW;
static int sWindowedH;

// Copied rather than pointed at. iWindowParams::title belongs to the caller's
// stack frame in iSystem.cpp and librw reads it much later, inside
// RwEngineStart, by which time that frame is long gone.
static char sTitle[128];

static S32 sOpened;

#ifdef RW_GL3
static iWindowDeferred sDeferred;
#endif

#ifdef _WIN32
// The window's own icons, which are NOT the same thing as the executable's.
//
// Explorer reads the executable's first icon resource. A window shows the
// icons it was sent with WM_SETICON, and without them its class's, which SDL
// loads from that same resource at one size. Sending them here, at the two
// system metric sizes, picks the right image of the .ico (16 to 256 pixels)
// for the title bar and for the taskbar instead of scaling one, and does not
// depend on how SDL registered its class.
//
// Resource 1 is the icon CMakeLists.txt compiles in ("the app icon").
static void SetResourceIcons(HWND hwnd)
{
    if (hwnd == NULL)
    {
        return;
    }

    HINSTANCE instance = GetModuleHandleA(NULL);

    HICON big =
        (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                          GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON small_icon =
        (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                          GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    if (big != NULL)
    {
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    }

    if (small_icon != NULL)
    {
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
}
#endif

// The window icon's picture, as CMakeLists.txt compiled it in ("the app
// icon"): kWindowIconPng, kWindowIconPngSize bytes of PNG, empty when the
// build was given none.
#include "tssm_window_icon.inc"

void iWindowSetIcon(SDL_Window* window)
{
    if (window == NULL)
    {
        return;
    }

    // Decoded once; SDL_SetWindowIcon takes a copy for each window.
    static unsigned char* sPixels;
    static unsigned sW;
    static unsigned sH;
    static bool sDecoded;
    if (!sDecoded)
    {
        sDecoded = true;
        if (kWindowIconPngSize > 0 &&
            lodepng_decode32(&sPixels, &sW, &sH, kWindowIconPng, kWindowIconPngSize) != 0)
        {
            printf("tssm: the window icon's picture could not be read; the windows keep the "
                   "system's\n");
            fflush(stdout);
            sPixels = NULL;
        }
    }

    if (sPixels != NULL)
    {
        SDL_Surface* icon =
            SDL_CreateSurfaceFrom((int)sW, (int)sH, SDL_PIXELFORMAT_RGBA32, sPixels, (int)sW * 4);
        if (icon != NULL)
        {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }

#ifdef _WIN32
    SetResourceIcons(
        (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                     NULL));
#endif
}

S32 iWindowOpen(const iWindowParams* params)
{
    if (params == NULL || sOpened)
    {
        return FALSE;
    }

    // **SDL's video subsystem is brought up HERE, not left to librw.**
    //
    // librw's openSDL3 calls SDL_InitSubSystem(SDL_INIT_VIDEO) itself and
    // SDL_QuitSubSystem in closeSDL3, so the GL3 build would work without this.
    // Two things are bought by doing it first anyway, and they are the same two
    // the D3D9 adapter probe in rw/engine_start.cpp buys:
    //
    //   * a machine with no usable video driver says so here, in a sentence,
    //     rather than inside RwEngineStart with nothing on screen to explain it
    //     -- and unlike D3D9's, this failure is reported before the engine has
    //     been initialised at all.
    //
    //   * SDL refcounts subsystem init, so this reference outlives librw's.
    //     Its close then leaves the subsystem up instead of tearing SDL's
    //     process-wide video state down and building it again, which is the
    //     handover D3D9 was seen to fault inside.
    //
    // It is also what makes the process DPI aware, which the Win32 backend used
    // to arrange by hand: SDL sets per-monitor v2 awareness when video comes up
    // and handles WM_DPICHANGED itself, so a window dragged between two
    // monitors at different scalings keeps its physical size instead of being
    // resized behind the game's back and resetting the device on every step.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        printf("tssm: SDL could not open a video device: %s\n", SDL_GetError());
        fflush(stdout);
        return FALSE;
    }

    // Keep the display awake for as long as the game is up. It matters more
    // than a blanked screen would suggest: when the display sleeps, this
    // machine's adapters stop reporting D3DDEVTYPE_HAL, so RenderWare startup
    // fails outright -- a screensaver could take the game down.
    SDL_DisableScreenSaver();

    sMode = params->mode;

    // iTour.h: a hidden window, which nothing may make visible.
    const char* tour = getenv("TSSM_TOUR");
    sTour = tour != NULL && tour[0] != '\0';

    // The size the window will be created at, which is the size asked for and
    // not necessarily what it ends up as -- the two fullscreen modes cover a
    // monitor. What it ends up as is read back below.
    sWidth = params->width;
    sHeight = params->height;

    strncpy(sTitle, params->title != NULL ? params->title : "TSSM", sizeof(sTitle) - 1);
    sTitle[sizeof(sTitle) - 1] = '\0';

    sShouldClose = 0;

    // GL3 creates nothing here. A GL context and the window it draws into have
    // to be made together, so librw makes both inside RwEngineStart and this
    // records what to make; see iWindow.h. D3D is the other way round and is
    // handed a window that already exists, which is the rest of this function.
    //
    // A runtime branch and not an #ifdef, because one executable carries both.
    // iBackendResolve has already run -- RenderWareInit calls it before this,
    // for exactly this reason.
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        sDeferred.handleSlot = (void**)&sWindow;
        sDeferred.title = sTitle;
        sDeferred.width = sWidth;
        sDeferred.height = sHeight;

        sOpened = TRUE;
        return TRUE;
    }
#endif

    // Windowed opens at the render size, wherever the window manager puts it.
    // The other two cover a monitor with no frame -- the WS_POPUP the Win32
    // backend made -- and need that monitor's rectangle. The primary one is the
    // only sensible choice with nothing else to go on, since there is no window
    // yet to ask which display it is nearest.
    //
    // The rectangle is wanted as much as the size: a monitor left of the
    // primary has a negative origin.
    // Vulkan makes its surface on this window, and SDL refuses to make one on a
    // window that was not created for it.
    SDL_WindowFlags flags = iBackendIsVulkan() ? SDL_WINDOW_VULKAN : 0;
    if (sTour)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }
    S32 x = 0;
    S32 y = 0;
    S32 w = params->width;
    S32 h = params->height;
    bool place = false;

    if (sMode == iWINDOW_WINDOWED)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    else
    {
        SDL_Rect bounds;

        if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds) && bounds.w > 0 && bounds.h > 0)
        {
            x = bounds.x;
            y = bounds.y;
            w = bounds.w;
            h = bounds.h;
            place = true;
        }

        flags |= SDL_WINDOW_BORDERLESS;
    }

    // Exclusive fullscreen is deliberately NOT SDL's fullscreen here. Under
    // D3D9 the mode set belongs to the device: SelectFullscreenVideoMode picks
    // a librw video mode between RwEngineOpen and RwEngineStart, and librw
    // creates the device with Windowed = FALSE against this handle. A window
    // SDL had already taken exclusive would be a second mode set the renderer
    // does not know happened. So both non-windowed modes get the same
    // frameless, monitor-sized window and the renderer decides which it is.
    sWindow = SDL_CreateWindow(sTitle, w, h, flags);

    if (sWindow == NULL)
    {
        printf("tssm: SDL could not create a window: %s\n", SDL_GetError());
        fflush(stdout);
        SDL_EnableScreenSaver();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return FALSE;
    }

    if (place)
    {
        SDL_SetWindowPosition(sWindow, x, y);
    }

#ifdef __ANDROID__
    // Android hides the status and navigation bars only for a fullscreen
    // window, and a borderless one leaves them drawn over the game. GL3's window
    // is made fullscreen in iWindowDeferredCreated.
    if (sMode != iWINDOW_WINDOWED)
    {
        SDL_SetWindowFullscreen(sWindow, true);
        SDL_SyncWindow(sWindow);
    }
#endif

    // What the window actually got, in pixels, which is what the back buffer is
    // sized in and what every other part of the port pairs with. Read rather
    // than assumed: for a frameless window covering a monitor it is the
    // monitor, and for a windowed one the window manager may have had its own
    // opinion.
    int got_w = 0;
    int got_h = 0;

    if (SDL_GetWindowSizeInPixels(sWindow, &got_w, &got_h) && got_w > 0 && got_h > 0)
    {
        sWidth = got_w;
        sHeight = got_h;
    }

    iWindowSetIcon(sWindow);

    sOpened = TRUE;
    return TRUE;
}

const iWindowDeferred* iWindowDeferredParams()
{
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return sOpened ? &sDeferred : NULL;
    }
#endif

    // Nothing to defer. D3D is handed a window that already exists, so by the
    // time RwEngineOpen runs there is an HWND and iWindowNativeHandle answers
    // with it.
    return NULL;
}

void iWindowDeferredCreated()
{
#ifdef RW_GL3
    if (!iBackendIsGL3() || sWindow == NULL)
    {
        return;
    }

    // librw made this window, so its icon is set here.
    iWindowSetIcon(sWindow);

    // Borderless, applied now because there was nothing to apply it to before.
    //
    // SDL3 spells it as fullscreen with no display mode set: the window covers
    // the display it is on and the desktop's mode is left alone, which is
    // exactly what iWindow.h describes borderless as. librw's startSDL3 has
    // already called SDL_SetWindowFullscreenMode(win, NULL) on the non-
    // exclusive path, so only the second half is left to do.
    //
    // Exclusive fullscreen is NOT here. It is a video mode, chosen before
    // RwEngineStart by SelectFullscreenVideoMode -- librw creates the window
    // fullscreen in the first place when the selected mode is an exclusive one,
    // and a window put into exclusive fullscreen afterwards would be a mode set
    // the renderer does not know happened.
    //
    // Fullscreen with no exclusive mode chosen is borderless as well: that is
    // SelectFullscreenVideoMode's fallback, and librw then made an ordinary
    // window. A window with a fullscreen mode set is the exclusive one.
    if (sMode == iWINDOW_BORDERLESS ||
        (sMode == iWINDOW_FULLSCREEN && SDL_GetWindowFullscreenMode(sWindow) == NULL))
    {
        SDL_SetWindowFullscreenMode(sWindow, NULL);
        SDL_SetWindowFullscreen(sWindow, true);

        // Blocks until the window manager has actually done it. Without this
        // the size read below is the pre-fullscreen one, because SDL's
        // fullscreen transitions are asynchronous on every platform that has a
        // compositor.
        SDL_SyncWindow(sWindow);
    }

    if (sTour)
    {
        SDL_HideWindow(sWindow);
    }

    // What the window actually got, in the units the back buffer is in.
    int width = 0;
    int height = 0;

    if (SDL_GetWindowSizeInPixels(sWindow, &width, &height) && width > 0 && height > 0)
    {
        sWidth = width;
        sHeight = height;
    }

    sShouldClose = 0;
#endif
}

void iWindowClose()
{
    if (!iBackendIsGL3() && sWindow != NULL)
    {
        SDL_DestroyWindow(sWindow);
    }

    // Under GL3 the window is NOT destroyed here, and that is not an omission:
    // librw created it, and its stopSDL3 destroys it and the GL context
    // together during RwEngineStop. By the time anything calls this, the
    // pointer is already dangling -- librw does not null the slot it wrote --
    // so the only correct thing to do with it is to forget it.
    sWindow = NULL;
    sOpened = FALSE;

    SDL_EnableScreenSaver();

    // Matches the SDL_InitSubSystem in iWindowOpen. Under GL3, librw's own
    // QuitSubSystem has already run by now and dropped the refcount to one;
    // this is the release that actually closes the video subsystem.
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// **SDL's event queue is one queue per process, and this file is not its only
// reader.** iPadHostSDL.cpp's iPadHostPoll drains it too, for hot-plug, and
// both run every frame. A plain SDL_PollEvent on either side is therefore a
// catch-all that takes the other side's events as well, and whichever ran
// first won: iPadHostPoll ran first every frame, swallowed
// SDL_EVENT_WINDOW_CLOSE_REQUESTED, and the window could not be closed at all.
//
// So each side takes only the range it owns. The joystick and gamepad event
// types occupy 0x600 through 0x6FF and are the pad host's; everything else is
// this one's, including the sweep of what nothing in the port reads -- SDL's
// queue is unbounded and fills with mouse, keyboard and display events for as
// long as the game runs.
//
// SDL_PumpEvents is also what refreshes the keyboard state iPadKeyboardSDL.cpp
// reads, so this runs before input is polled rather than after.
void iWindowPump()
{
    // TSSM_PERF's pump columns count this one with its events handled.
    iPerfPumpBegin();

    // TSSM_TEST_HANG_DUMP: every present and every pass of a movie comes
    // through here, so this is the game thread's heartbeat (iHangDump.h).
    iHangDumpBeat();

    SDL_PumpEvents();

    SDL_Event event;

    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                          SDL_EVENT_JOYSTICK_AXIS_MOTION - 1) > 0)
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            sShouldClose = 1;
            break;

        // NOT SDL_DestroyWindow. The game owns its own shutdown -- it saves,
        // tears RenderWare down and frees its heap -- and destroying the window
        // here would pull the device or the GL context out from under all of
        // it. iSystem reads iWindowShouldClose and runs the game's exit path.
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            sShouldClose = 1;
            break;

        // **Tell Windows the window has been painted, because D3D9 will not.**
        //
        // Present writes to the window through the display driver and never
        // touches GDI, so the update region stays dirty and Windows posts
        // WM_PAINT again on the next pump. SDL turns each of those into an
        // exposed event, and at 3840x2160 the round trip cost 15 ms -- a whole
        // frame spent in SDL_PumpEvents, every frame, which made the boot
        // movies crawl and read as a hang. Validating here ends it: measured
        // over the same run, the pump went from 15.7 ms to 2.9 us and the frame
        // rate from about 34 to about 450.
        //
        // Not needed under GL3, where SDL_GL_SwapWindow paints through the GL
        // driver and validates the window as a side effect.
        case SDL_EVENT_WINDOW_EXPOSED:
#ifdef _WIN32
            if (!iBackendIsGL3())
            {
                HWND hwnd = (HWND)iWindowNativeHandle();
                if (hwnd != NULL)
                {
                    ValidateRect(hwnd, NULL);
                }
            }
#endif
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            sWidth = event.window.data1;
            sHeight = event.window.data2;
            break;

        // Alt+Enter and F11 toggle windowed. iPadKeyboardSDL.cpp keeps an Enter
        // pressed with Alt held from also reaching the game as Start.
        case SDL_EVENT_KEY_DOWN:
            if (!event.key.repeat &&
                (event.key.key == SDLK_F11 ||
                 ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                  (event.key.mod & SDL_KMOD_ALT) != 0)))
            {
                iWindowSetMode(sMode == iWINDOW_WINDOWED ? iWINDOW_BORDERLESS : iWINDOW_WINDOWED);
            }
            break;

        default:
            break;
        }
    }

    // Touch, pen, drop, audio device, sensor, camera and render events, none of
    // which anything here reads. Dropped rather than left to pile up.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FINGER_DOWN, SDL_EVENT_LAST) > 0)
    {
    }

    // Escape frees the pointer here as well as at the pad's poll, which a
    // loading screen or a movie may not reach for a while.
    iMouseLookPump();

    iPerfPumpEnd();
}

// 60 Hz, which is what the GameCube's video interface gave the game, and what
// the framerate setting defaults to. See the long comment at the
// iWindowPaceFrame call in rw/camera.cpp for why the port paces at all when it
// is already waiting on the display.
static S32 sFrameRate = 60;
static S32 sVSync = 1;

// A ceiling under the cap, 0 for none: iWindowSetFrameRateCeiling. PacedRate
// is the rate the pacer holds, the cap lowered to the ceiling while one is set.
static S32 sFrameRateCeiling = 0;

// The GameCube's hold, a count of retraces, 0 for none: iWindowSetFrameLock.
// One NTSC retrace is 1001/60000 s.
static S32 sFrameLock = 0;
static const Uint64 kRetraceNs = 16683333ULL;

static S32 CapAndCeiling()
{
    if (sFrameRateCeiling > 0 && (sFrameRate <= 0 || sFrameRate > sFrameRateCeiling))
    {
        return sFrameRateCeiling;
    }
    return sFrameRate;
}

// Whether the hold is what paces the frame: one is set, no movie's ceiling is,
// and the cap is not already lower. A cap equal to the hold's rounded rate (30)
// is paced as the hold, at 29.97.
static bool HoldPaces()
{
    if (sFrameLock <= 1 || sFrameRateCeiling > 0)
    {
        return false;
    }
    const S32 cap = CapAndCeiling();
    const S32 held = (60 + sFrameLock / 2) / sFrameLock;
    return cap <= 0 || cap >= held;
}

static S32 PacedRate()
{
    if (HoldPaces())
    {
        return (60 + sFrameLock / 2) / sFrameLock;
    }
    return CapAndCeiling();
}

// What the pacer holds, as one number that changes whenever it does: the rate,
// or minus the retrace count while the hold paces.
static S32 PacedKey()
{
    return HoldPaces() ? -sFrameLock : CapAndCeiling();
}

// Held at file scope rather than inside iWindowPaceFrame so that a rate change
// can invalidate the deadline. See iWindowSetFrameRate.
static bool sPacerStarted = false;
static Uint64 sNextFrame = 0;

void iWindowSetFrameRate(S32 fps)
{
    sFrameRate = fps > 0 ? fps : 0;

    // The deadline the pacer is holding was measured against the old period.
    // Leaving it be would make the first frame after a change sleep out the
    // remainder of a period that no longer exists.
    sPacerStarted = false;
}

void iWindowSetFrameRateCeiling(S32 fps)
{
    // The deadline is dropped only when the paced rate moves, so a ceiling at
    // or above the cap (60 under the default cap of 60) changes nothing at all.
    const S32 before = PacedKey();
    sFrameRateCeiling = fps > 0 ? fps : 0;
    if (PacedKey() != before)
    {
        sPacerStarted = false;
    }
}

void iWindowSetFrameLock(S32 count)
{
    const S32 before = PacedKey();
    sFrameLock = count > 1 ? count : 0;
    if (PacedKey() != before)
    {
        sPacerStarted = false;
    }
}

S32 iWindowGetFrameLock()
{
    return HoldPaces() ? sFrameLock : 0;
}

S32 iWindowGetFrameRate()
{
    return sFrameRate;
}

S32 iWindowGetPacedRate()
{
    return PacedRate();
}

void iWindowSetVSync(S32 on)
{
    sVSync = on ? 1 : 0;
}

S32 iWindowGetVSync()
{
    return sVSync;
}

// The rate iWindowGetDisplayRefreshRate last found, unrounded (59.94, not 60).
static F32 sRefreshHz = 0.0f;

S32 iWindowGetDisplayRefreshRate()
{
    // The display the window is on, not the primary one -- on a two-monitor
    // machine those differ, and the one that paces the game is the one it is
    // being displayed on. Before the window exists there is no such display and
    // the primary one is the only honest answer.
    SDL_DisplayID display = 0;

    if (sWindow != NULL)
    {
        display = SDL_GetDisplayForWindow(sWindow);
    }

    if (display == 0)
    {
        display = SDL_GetPrimaryDisplay();
    }

    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);

    if (mode == NULL || mode->refresh_rate <= 1.0f)
    {
        // SDL reports 0 for a display whose rate it does not know, and a rate
        // of one is not a number of hertz worth capping to -- that is the game
        // stopped.
        return 0;
    }

    sRefreshHz = mode->refresh_rate;
    return (S32)(mode->refresh_rate + 0.5f);
}

S32 iWindowGetDisplaySize(S32* width, S32* height)
{
    // Refcounted, so this neither needs the window nor disturbs one.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return FALSE;
    }

    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    S32 ok = mode != NULL && mode->w > 0 && mode->h > 0;

    if (ok)
    {
        *width = mode->w;
        *height = mode->h;
#ifdef __ANDROID__
        // The game runs in landscape (sensorLandscape), and the display may
        // still be reported in portrait before the activity has turned.
        if (*height > *width)
        {
            *width = mode->h;
            *height = mode->w;
        }
#endif
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return ok;
}

// **Where vsync already holds the cap, the display paces the frames.**
//
// With vsync on and the cap at the display's rate (video.framerate = display,
// 120 on a 120 Hz phone), a sleep to the cap's deadline is a second clock
// beside the display's, running at nearly the same rate. Their phase drifts,
// and wherever a deadline falls near a refresh, a frame that took no longer
// than any other is shown one refresh late and the one after it is shown in
// the same refresh: a dropped frame in a game whose work fits with room to
// spare. The display's own wait cannot drift against itself, and with a frame
// or two queued ahead it absorbs the frame-to-frame jitter that the sleep
// passes straight on. So there the pacer only keeps a backstop a quarter above
// the cap, for a driver that reports vsync and does not wait; under a working
// vsync its deadline has always passed and it never sleeps. A cap under the
// display's rate (60 on a 120 Hz panel, a movie's ceiling) is paced by the
// sleep as before.
//
// Only on Android, where a swap with vsync on waits for the display: the
// surface's buffer queue hands the game a buffer back only as the display
// takes one. A desktop cannot be trusted to: a variable-refresh monitor runs
// as fast as frames come, up to a maximum above the rate the desktop reports,
// and on the test PC a windowed GL swap under the compositor returned early
// (128 frames a second at a reported 120 Hz) and a 60 fps cap ran at the
// backstop's 75 where the window's display was reported at 60. There the cap
// is always slept to. TSSM_VSYNC_PACE=0 or 1 turns this off or on anywhere.
//
// The display's rate is asked again twice a second, since a window can move
// to another monitor and a phone can change its panel's rate.
static S32 sVSyncPaceSwitch = -1;
static S32 sRefreshRate = 0;
static Uint64 sRefreshAskedAt = 0;

static bool VSyncHoldsRate(S32 rate, Uint64 now)
{
    if (!sVSync)
    {
        return false;
    }
    if (sVSyncPaceSwitch < 0)
    {
        const char* e = getenv("TSSM_VSYNC_PACE");
#ifdef __ANDROID__
        sVSyncPaceSwitch = (e == NULL || *e == '\0' || atoi(e) != 0) ? 1 : 0;
#else
        sVSyncPaceSwitch = (e != NULL && *e != '\0' && atoi(e) != 0) ? 1 : 0;
#endif
    }
    if (!sVSyncPaceSwitch)
    {
        return false;
    }
    if (sRefreshAskedAt == 0 || now - sRefreshAskedAt >= 500000000ULL)
    {
        sRefreshRate = iWindowGetDisplayRefreshRate();
        sRefreshAskedAt = now;
    }
    return sRefreshRate > 0 && rate >= sRefreshRate;
}

// The period that paced the frame just shown, for iWindowGameFrameTime: what
// the sleep held, or one refresh where vsync held the rate. 0 when nothing
// paced it (no cap) or nothing will make up for its lateness (a late frame
// under the hold).
static Uint64 sPacedPeriodNs = 0;

// When the frame's work began: the end of the last pace. See
// iWindowFrameWorkDone.
static Uint64 sFrameStart = 0;
static iWindowFrameWorkFn sFrameWorkHook = NULL;

void iWindowSetFrameWorkHook(iWindowFrameWorkFn hook)
{
    sFrameWorkHook = hook;
}

void iWindowFrameWorkDone()
{
    if (sFrameWorkHook != NULL && sFrameStart != 0)
    {
        sFrameWorkHook(SDL_GetTicksNS() - sFrameStart, PacedRate());
    }
}

// **The pacer's sleep: a timer to just short of the deadline, then a spin to it.**
//
// A sleep wakes some time after the moment it asked for, and whatever it is
// late by goes straight on to the frame. So the timer aims short of the
// deadline by a margin and the rest is spun out on the clock, which ends within
// a microsecond or two of the deadline. The margin follows how late the timer
// has been waking: it rises at once to a late wake, plus a little, and decays
// slowly back down, so where the timer is precise the spin is a fraction of a
// millisecond a frame, and where it is coarse the deadlines are still met.
//
// On Windows the timer is a high-resolution waitable timer (Windows 10 1803 and
// later), which keeps to the time asked for rather than to the scheduler's
// tick; before 1803 an ordinary one, at the 1 ms timer period SDL sets, and the
// margin grows to cover it. SDL_DelayPrecise, used before, slept in 1 ms steps:
// six or seven timer wakes a frame at 120 fps, each one a chance to wake late,
// and only the last step's remainder was spun.
#ifndef __ANDROID__
static const Uint64 kSpinMinNs = 200000ULL;
static const Uint64 kSpinMaxNs = 4000000ULL;
static const Uint64 kSpinPadNs = 100000ULL;
static Uint64 sSpinNs = 1000000ULL;
// The latest the timer has woken past the time it was set for, decaying.
static Uint64 sWakeLateNs = 0;

#if defined(_WIN32)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static HANDLE sPaceTimer;
static bool sPaceTimerTried;

static void TimerSleep(Uint64 ns)
{
    if (!sPaceTimerTried)
    {
        sPaceTimerTried = true;
        sPaceTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                            TIMER_ALL_ACCESS);
        if (sPaceTimer == NULL)
        {
            sPaceTimer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
        }
    }

    if (sPaceTimer != NULL)
    {
        // Relative, in 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ns / 100);
        if (due.QuadPart < 0 && SetWaitableTimer(sPaceTimer, &due, 0, NULL, NULL, FALSE))
        {
            WaitForSingleObject(sPaceTimer, INFINITE);
            return;
        }
    }
    SDL_DelayNS(ns);
}
#else
static void TimerSleep(Uint64 ns)
{
    SDL_DelayNS(ns);
}
#endif

static void SleepUntil(Uint64 deadline, Uint64 now)
{
    // The decay runs on every sleep, the ones too short for the timer too: a
    // margin raised by one late wake must come down even while every frame
    // leaves less than the margin, or each of those frames spins its whole
    // remainder. A wake later than the largest margin counts as that margin.
    sWakeLateNs -= sWakeLateNs / 64;
    if (deadline > now + sSpinNs)
    {
        const Uint64 wakeAt = deadline - sSpinNs;
        TimerSleep(wakeAt - now);
        now = SDL_GetTicksNS();

        Uint64 late = now > wakeAt ? now - wakeAt : 0;
        late = late > kSpinMaxNs ? kSpinMaxNs : late;
        if (late > sWakeLateNs)
        {
            sWakeLateNs = late;
        }
    }
    Uint64 spin = sWakeLateNs + kSpinPadNs;
    spin = spin < kSpinMinNs ? kSpinMinNs : spin;
    sSpinNs = spin > kSpinMaxNs ? kSpinMaxNs : spin;

    while (now < deadline)
    {
        SDL_CPUPauseInstruction();
        now = SDL_GetTicksNS();
    }
}
#endif

static void PaceSleep()
{
    const S32 rate = PacedRate();
    if (rate <= 0)
    {
        sPacerStarted = false;
        sPacedPeriodNs = 0;
        return;
    }

    Uint64 now = SDL_GetTicksNS();

    const bool backstop = VSyncHoldsRate(rate, now);
    const S32 paceRate = backstop ? rate + rate / 4 : rate;
    Uint64 framePeriodNs = 1000000000ULL / (Uint64)paceRate;
    if (HoldPaces())
    {
        // The GameCube's own period, `count` retraces of 1001/60000 s.
        framePeriodNs = (Uint64)sFrameLock * kRetraceNs;
        if (backstop)
        {
            framePeriodNs = framePeriodNs * 4 / 5;
        }
    }

    // Where vsync holds the rate the display paces the frame, unless the
    // backstop below ends up sleeping, which means it does not.
    sPacedPeriodNs = framePeriodNs;
    if (backstop && sRefreshHz > 1.0f)
    {
        sPacedPeriodNs = (Uint64)(1e9 / (double)sRefreshHz);
    }

    if (!sPacerStarted)
    {
        sNextFrame = now;
        sPacerStarted = true;
    }

    // The deadline is carried forward a period at a time rather than re-read
    // from the clock, so the rate does not drift.
    sNextFrame += framePeriodNs;

    // A frame whose own work ran past its deadline goes at once. Less than a
    // period late, the deadlines stay where they were: the next frame has that
    // much less time, and the rate holds. Further behind, the missed deadlines
    // are dropped and pacing starts again from now, because a slow stretch must
    // not be paid for by not sleeping for the next several frames -- that turns
    // it into a burst of fast ones.
    //
    // The GameCube's hold starts again from now after any late frame: the
    // console counts its retraces from the last picture shown, so a slow frame
    // there is never followed by a short one.
    if (sNextFrame <= now)
    {
        const Uint64 behind = now - sNextFrame;
        iPerfPace(0, (S64)behind);
        if (behind >= framePeriodNs || HoldPaces())
        {
            sNextFrame = now;
        }
        // Under the hold no short frame makes up for a late one, so a late
        // frame keeps its measured time, as on the console. (Where vsync
        // paces, the backstop's deadline is always behind.)
        if (HoldPaces() && !backstop)
        {
            sPacedPeriodNs = 0;
        }
        return;
    }
    const Uint64 want = sNextFrame - now;
    sPacedPeriodNs = framePeriodNs;

#ifdef __ANDROID__
    // Android: one sleep. Its nanosleep is a high-resolution timer that wakes
    // within tens of microseconds, so the overshoot SleepUntil spins against
    // does not happen. Spinning, or SDL_DelayPrecise's 1 ms steps, woke a big
    // core once per idle millisecond (700 to 900 times a second in a movie at
    // 120 fps), keeping it out of its deep idle states for nothing.
    SDL_DelayNS(want);
#else
    SleepUntil(sNextFrame, now);
#endif
    iPerfPace(want, (S64)(SDL_GetTicksNS() - sNextFrame));
}

// The game's frame time: steady for a frame that came on time, so the enemies'
// end-of-animation checks see their ends as the console does (iSteadyFrame.h).
static iSteadyFrame sSteady;

F32 iWindowGameFrameTime(F32 measured)
{
    return iSteadyFrameTime(&sSteady, sPacedPeriodNs, measured);
}

void iWindowPaceFrame()
{
    PaceSleep();
    sFrameStart = SDL_GetTicksNS();
}

S32 iWindowShouldClose()
{
    return sShouldClose;
}

void iWindowRequestClose()
{
    sShouldClose = 1;
}

iWindowMode iWindowGetMode()
{
    return sMode;
}

void iWindowSetExclusive(S32 on)
{
    sExclusive = on;
}

S32 iWindowSetMode(iWindowMode mode)
{
#ifdef __ANDROID__
    (void)mode;
    return FALSE;
#else
    if (sWindow == NULL || mode == iWINDOW_FULLSCREEN || sTour)
    {
        return FALSE;
    }

    if (sExclusive)
    {
        printf("tssm: the window is in exclusive fullscreen; set video.mode and restart to "
               "change it\n");
        fflush(stdout);
        return FALSE;
    }

    bool windowed = sMode == iWINDOW_WINDOWED;
    if ((mode == iWINDOW_WINDOWED) == windowed)
    {
        return TRUE;
    }

    if (mode == iWINDOW_WINDOWED)
    {
        // GL3's borderless is SDL fullscreen (iWindowDeferredCreated); the
        // others' is a frameless window the size of the monitor. Undoing the
        // first is harmless for the second.
        SDL_SetWindowFullscreen(sWindow, false);
        SDL_SetWindowBordered(sWindow, true);
        SDL_SetWindowResizable(sWindow, true);

        S32 w = sWindowedW;
        S32 h = sWindowedH;
        if (w <= 0 || h <= 0)
        {
            // Never windowed this session: three quarters of the monitor, in
            // the render size's shape.
            SDL_Rect bounds;
            SDL_DisplayID display = SDL_GetDisplayForWindow(sWindow);
            if (!SDL_GetDisplayBounds(display, &bounds) || bounds.w <= 0 || bounds.h <= 0)
            {
                bounds.w = 1280;
                bounds.h = 720;
            }
            w = bounds.w * 3 / 4;
            h = bounds.h * 3 / 4;
            S32 rw = iScreenWidth();
            S32 rh = iScreenHeight();
            if (rw > 0 && rh > 0)
            {
                if (w * rh > h * rw)
                {
                    w = h * rw / rh;
                }
                else
                {
                    h = w * rh / rw;
                }
            }
        }

        SDL_SetWindowSize(sWindow, w, h);
        if (sWindowedW > 0)
        {
            SDL_SetWindowPosition(sWindow, sWindowedX, sWindowedY);
        }
        else
        {
            SDL_SetWindowPosition(sWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    else
    {
        // Kept so going back lands where the window was.
        SDL_GetWindowPosition(sWindow, &sWindowedX, &sWindowedY);
        SDL_GetWindowSize(sWindow, &sWindowedW, &sWindowedH);

        SDL_Rect bounds;
        if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(sWindow), &bounds) || bounds.w <= 0 ||
            bounds.h <= 0)
        {
            return FALSE;
        }

        SDL_SetWindowResizable(sWindow, false);
        SDL_SetWindowBordered(sWindow, false);
        SDL_SetWindowPosition(sWindow, bounds.x, bounds.y);
        SDL_SetWindowSize(sWindow, bounds.w, bounds.h);
    }

    // The transitions are asynchronous wherever there is a compositor. The size
    // read back is what the back buffer follows.
    SDL_SyncWindow(sWindow);
    int got_w = 0;
    int got_h = 0;
    if (SDL_GetWindowSizeInPixels(sWindow, &got_w, &got_h) && got_w > 0 && got_h > 0)
    {
        sWidth = got_w;
        sHeight = got_h;
    }

    sMode = mode;
    printf("tssm: window is now %s, %dx%d\n", mode == iWINDOW_WINDOWED ? "windowed" : "borderless",
           (int)sWidth, (int)sHeight);
    fflush(stdout);
    return TRUE;
#endif
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

// Whatever the running backend's EngineOpenParams wants, as iWindow.h says: an
// SDL_Window* under GL3, which librw wrote into the slot itself, and the HWND
// behind that window under D3D. Vulkan takes the SDL_Window* as well, one the
// port made.
void* iWindowNativeHandle()
{
#ifdef _WIN32
    if (!iBackendIsGL3() && !iBackendIsVulkan())
    {
        if (sWindow == NULL)
        {
            return NULL;
        }

        return SDL_GetPointerProperty(SDL_GetWindowProperties(sWindow),
                                      SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    }
#endif
    return (void*)sWindow;
}

const char* iWindowBackendName()
{
    return "sdl3";
}
