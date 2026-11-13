// TSSM_GUI_SELFTEST=<frames>: the overlay over a test picture, on the real
// device and through the real present, before the game has loaded anything.
//
// It exists so the overlay can be checked on every backend without depending on
// how far the game boots. Each frame clears a camera the size of the render
// target to a slowly changing colour and presents it with RwCameraShowRaster --
// the call every one of the game's frames goes through -- so the virtual
// screen, its stretch into the window, the overlay's hook on librw's present
// and the overlay's own drawing are all the production paths. Combine it with
// TSSM_GUI_CAPTURE (and TSSM_GUI_TAB=all, TSSM_GUI_EXIT=1) for pictures.
//
//   TSSM_GUI_SELFTEST_RESIZE=<w>x<h>  resize the window a third of the way in.
//                                     Under D3D9 that is a device reset with the
//                                     overlay open; with a shape other than the
//                                     render size's it shows the letterbox.
//   TSSM_GUI_SELFTEST_PAD=1           plug in a virtual controller and play a
//                                     script of presses on it through the input
//                                     backend and iGuiFilterPad, logging what
//                                     the game would see: the Back+Start combo
//                                     both ways, the buttons held across the
//                                     close, and a press while the overlay is
//                                     open.
//
// The game does not continue afterwards unless TSSM_GUI_SELFTEST_CONTINUE=1:
// the overlay is left open, and a test run wants a clean exit.

#include <SDL3/SDL.h>

#include <rwcore.h>

#include "iCamera.h"
#include "iPadHost.h"
#include "iScreen.h"

#include "gui/iGui.h"

#include <stdio.h>
#include <stdlib.h>

namespace
{
    struct PadStep
    {
        S32 frame;
        SDL_GamepadButton button;
        bool down;
        const char* what;
    };

    // What each step is for, and so what the log should show after it.
    const PadStep kPadScript[] = {
        { 10, SDL_GAMEPAD_BUTTON_BACK, true, "Back down (overlay open: game sees nothing)" },
        { 12, SDL_GAMEPAD_BUTTON_START, true, "Start down: the combo closes the overlay" },
        { 20, SDL_GAMEPAD_BUTTON_START, false, "Start up (still hidden from the game until now)" },
        { 21, SDL_GAMEPAD_BUTTON_BACK, false, "Back up" },
        { 30, SDL_GAMEPAD_BUTTON_SOUTH, true, "A down (overlay closed: game sees it)" },
        { 34, SDL_GAMEPAD_BUTTON_SOUTH, false, "A up" },
        { 40, SDL_GAMEPAD_BUTTON_BACK, true, "Back down (game sees Select)" },
        { 42, SDL_GAMEPAD_BUTTON_START, true, "Start down: the combo opens the overlay" },
        { 46, SDL_GAMEPAD_BUTTON_SOUTH, true, "A down (overlay open: game sees nothing)" },
        { 50, SDL_GAMEPAD_BUTTON_SOUTH, false, "A up" },
        { 51, SDL_GAMEPAD_BUTTON_START, false, "Start up" },
        { 52, SDL_GAMEPAD_BUTTON_BACK, false, "Back up" },
    };

    SDL_Joystick* AttachVirtualPad()
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
        desc.name = "TSSM selftest controller";

        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (id == 0)
        {
            printf("tssm: gui: selftest: no virtual controller: %s\n", SDL_GetError());
            return NULL;
        }
        return SDL_OpenJoystick(id);
    }
} // namespace

void iGuiSelfTestRun(S32 frames)
{
    RwCamera* camera = iCameraCreate(iScreenWidth(), iScreenHeight(), 0);
    if (camera == NULL)
    {
        printf("tssm: gui: selftest: no camera; skipped\n");
        fflush(stdout);
        return;
    }

    printf("tssm: gui: selftest: %d frames of a test picture at %dx%d with the overlay open\n",
           (int)frames, (int)iScreenWidth(), (int)iScreenHeight());
    fflush(stdout);

    int resizeW = 0;
    int resizeH = 0;
    const char* resize = getenv("TSSM_GUI_SELFTEST_RESIZE");
    if (resize != NULL && sscanf(resize, "%dx%d", &resizeW, &resizeH) != 2)
    {
        resizeW = resizeH = 0;
    }

    const char* pad = getenv("TSSM_GUI_SELFTEST_PAD");
    SDL_Joystick* joystick = (pad != NULL && atoi(pad) != 0) ? AttachVirtualPad() : NULL;
    U32 lastSeen = ~0u;
    bool lastOpen = !iGuiIsOpen();

    for (S32 i = 0; i < frames; i++)
    {
        if (resizeW > 0 && resizeH > 0 && i == frames / 3)
        {
            int count = 0;
            SDL_Window** windows = SDL_GetWindows(&count);
            if (windows != NULL && count > 0)
            {
                SDL_SetWindowSize(windows[0], resizeW, resizeH);
                SDL_SyncWindow(windows[0]);
                printf("tssm: gui: selftest: window resized to %dx%d at frame %d\n", resizeW,
                       resizeH, (int)i);
                fflush(stdout);
            }
            SDL_free(windows);
        }

        // The game's side of a frame, as iPadUpdate does it on port 0.
        if (joystick != NULL)
        {
            for (size_t s = 0; s < sizeof(kPadScript) / sizeof(kPadScript[0]); s++)
            {
                if (kPadScript[s].frame == i)
                {
                    SDL_SetJoystickVirtualButton(joystick, kPadScript[s].button, kPadScript[s].down);
                    printf("tssm: gui: selftest: frame %d: %s\n", (int)i, kPadScript[s].what);
                }
            }

            iPadHostPoll();
            const iPadHostState* host = iPadHostGet(0);
            const iPadHostState* seen = iGuiFilterPad(0, host);
            if (seen != NULL && (seen->buttons != lastSeen || iGuiIsOpen() != lastOpen))
            {
                printf("tssm: gui: selftest: frame %d: controller %08x, the game sees %08x, "
                       "overlay %s\n",
                       (int)i, host != NULL ? (unsigned)host->buttons : 0u, (unsigned)seen->buttons,
                       iGuiIsOpen() ? "open" : "closed");
                lastSeen = seen->buttons;
                lastOpen = iGuiIsOpen();
            }
            fflush(stdout);
        }

        // Sea green drifting towards blue: something that is plainly not the
        // black of the bars, so the stretch and the letterbox show in a
        // capture.
        const S32 t = i % 120;
        RwRGBA colour;
        colour.red = (RwUInt8)(20 + t / 4);
        colour.green = (RwUInt8)(110 + t / 2);
        colour.blue = (RwUInt8)(140 + t);
        colour.alpha = 255;

        RwCameraClear(camera, &colour, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
        if (RwCameraBeginUpdate(camera) != NULL)
        {
            RwCameraEndUpdate(camera);
        }
        RwCameraShowRaster(camera, NULL, 1);
    }

    iCameraDestroy(camera);
    if (joystick != NULL)
    {
        const SDL_JoystickID id = SDL_GetJoystickID(joystick);
        SDL_CloseJoystick(joystick);
        SDL_DetachVirtualJoystick(id);
    }

    const char* go = getenv("TSSM_GUI_SELFTEST_CONTINUE");
    if (go != NULL && atoi(go) != 0)
    {
        iGuiSetOpen(false);
        return;
    }

    printf("tssm: gui: selftest: done, exiting\n");
    fflush(stdout);
    exit(0);
}
