// Compile-time checks for the 3DS build's enum size (CMakeLists.txt, the
// TSSM_3DS block): nothing here runs. Compiled as a game unit (the game's
// flags and SBPrefix.h), so it sees the structs as the game does.
//
// devkitARM sizes an enum to its values unless told otherwise, and the
// GameCube build's enums are int-sized (`-enum int`). Built without
// -fno-short-enums, xCurveAsset below is 16 bytes instead of the 20 it is on
// the disc and every curve after the first is read from the wrong place; the
// other asset structs keep their size through padding but read their enum
// field as one byte. The numbers are the GameCube's layouts (int enums; the
// ARM EABI aligns everything else in these structs as the PowerPC EABI does).

#include <stddef.h>

#include "xCamera.h"
#include "xCurveAsset.h"
#include "xsavegame.h"
#include "zNMETypeBuckOTron.h"
#include "zShrapnel.h"
#include "zSurface.h"

#include <SDL3/SDL.h>

namespace
{
    enum iLayout3DSProbe
    {
        iLAYOUT3DS_PROBE
    };
} // namespace

static_assert(sizeof(iLayout3DSProbe) == 4,
              "enums are not int-sized: the 3DS build needs -fno-short-enums (CMakeLists.txt)");

// Asset structs with an enum field, as read in place from the game's packages.
static_assert(sizeof(xCurveAsset) == 20 && offsetof(xCurveAsset, clamp) == 4 &&
                  offsetof(xCurveAsset, delta) == 8,
              "xCurveAsset does not have its disk layout");
static_assert(sizeof(xCamAsset) == 136 && offsetof(xCamAsset, trans_type) == 80,
              "xCamAsset does not have its disk layout");
static_assert(sizeof(zNMEAssetBucketOTron) == 116 &&
                  offsetof(zNMEAssetBucketOTron, spawnMode) == 100,
              "zNMEAssetBucketOTron does not have its disk layout");
static_assert(sizeof(zFragAsset) == 24 && offsetof(zFragAsset, id) == 4,
              "zFragAsset does not have its disk layout");
static_assert(sizeof(zSurfAssetIN) == 440 && offsetof(zSurfAssetIN, damage_type) == 352,
              "zSurfAssetIN does not have its disk layout");

// The save game's working state (xsavegame.cpp), which the port's save store
// fills in.
static_assert(sizeof(st_XSAVEGAME_DATA) == 8432 && offsetof(st_XSAVEGAME_DATA, mode) == 4,
              "st_XSAVEGAME_DATA changed layout");

// SDL is built the libctru way (short enums), the port the game's way. The SDL
// structs the port fills in or reads must lay out the same either way; these
// numbers are asserted on the other side too (3ds/i3DS.cpp). SDL_AudioSpec's
// format is a 2-byte enum there and 4 bytes here: SDL reads the low two bytes
// of what the port writes, and the port reads only channels and freq back.
static_assert(sizeof(SDL_AudioSpec) == 12 && offsetof(SDL_AudioSpec, channels) == 4 &&
                  offsetof(SDL_AudioSpec, freq) == 8,
              "SDL_AudioSpec differs between the port and SDL");
static_assert(sizeof(SDL_KeyboardEvent) == 40 && offsetof(SDL_KeyboardEvent, key) == 28,
              "SDL_KeyboardEvent differs between the port and SDL");
static_assert(sizeof(SDL_Event) == 128, "SDL_Event differs between the port and SDL");
// The events the overlay's ImGui backend reads (gui/iGui.cpp, imgui_impl_sdl3).
static_assert(sizeof(SDL_TouchFingerEvent) == 56 && offsetof(SDL_TouchFingerEvent, x) == 32 &&
                  offsetof(SDL_TouchFingerEvent, windowID) == 52,
              "SDL_TouchFingerEvent differs between the port and SDL");
static_assert(sizeof(SDL_MouseButtonEvent) == 40 && offsetof(SDL_MouseButtonEvent, x) == 28 &&
                  sizeof(SDL_MouseMotionEvent) == 48 && offsetof(SDL_MouseMotionEvent, x) == 28,
              "SDL's mouse events differ between the port and SDL");
static_assert(sizeof(SDL_MouseWheelEvent) == 56 && offsetof(SDL_MouseWheelEvent, direction) == 32 &&
                  offsetof(SDL_MouseWheelEvent, mouse_x) == 36,
              "SDL_MouseWheelEvent differs between the port and SDL");
static_assert(sizeof(SDL_GamepadDeviceEvent) == 24 &&
                  offsetof(SDL_GamepadDeviceEvent, which) == 16 && sizeof(SDL_WindowEvent) == 32 &&
                  offsetof(SDL_WindowEvent, data1) == 20,
              "SDL's gamepad and window events differ between the port and SDL");
