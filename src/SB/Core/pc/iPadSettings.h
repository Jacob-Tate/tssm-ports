#ifndef IPADSETTINGS_H
#define IPADSETTINGS_H

#include <types.h>

// PC-only: config.ini's [input] settings, read once and kept as plain values,
// because the input code reads them every frame and config.ini lookups are
// string compares. iPadSettingsLoad re-reads them; iPadHostInit and
// iPadHostReloadBindings call it, and so does anything that has just changed
// one with iConfigSet (iPadControls.h).

enum iPadKeyboardMode
{
    // Keys and mouse play port 0 alongside a controller.
    IPAD_KEYBOARD_ALWAYS,
    // Only while no controller is on port 0 (treedome's behaviour).
    IPAD_KEYBOARD_FALLBACK,
    IPAD_KEYBOARD_OFF
};

enum iPadStickCurve
{
    // The Xbox release's own curve, read out of DEFAULT.XBE: each axis on its
    // own, nothing up to 26% of the travel, full at 70%, straight between.
    IPAD_CURVE_XBOX,
    // The GameCube's: a round deadzone, then the stick shaped onto the
    // console's octagon and clamped as PADClamp and iPadConvStick do -- full
    // speed a little past half way (treedome's).
    IPAD_CURVE_GAMECUBE,
    // Round deadzone, then straight to the edge: full speed only at the rim.
    IPAD_CURVE_LINEAR
};

enum iPadTestMode
{
    IPAD_TEST_OFF,
    // One line per frame in which anything the game sees changed.
    IPAD_TEST_LOG,
    // The same, and the controller drawn on screen (GL3 and Vulkan).
    IPAD_TEST_OVERLAY
};

struct iPadSettings
{
    iPadKeyboardMode keyboard;
    iPadStickCurve curve;

    // Fraction of a trigger's pull at which LT/RT click.
    F32 trigger_threshold;

    // Stick deadzone as a fraction of full travel, or negative for the
    // curve's own.
    F32 deadzone;

    // Scales the right stick (and the camera keys) before the game sees it.
    F32 camera_sensitivity;
    S32 camera_invert_x;
    S32 camera_invert_y;

    // The mouse (iMouseLook.h). Sensitivity 1 is 0.2 degrees a count; smoothing
    // is a time constant in seconds, 0 for none; the release key is a key's
    // name in the [keyboard] grammar, or empty.
    S32 mouse_look;
    F32 mouse_sensitivity_x;
    F32 mouse_sensitivity_y;
    S32 mouse_invert_x;
    S32 mouse_invert_y;
    S32 mouse_raw;
    F32 mouse_smoothing;
    char mouse_release_key[16];
    S32 mouse_menus;

    // 0..1, applied to every rumble.
    F32 rumble;

    iPadTestMode test;
};

const iPadSettings& iPadSettingsGet();
void iPadSettingsLoad();

// For tests and a settings screen that applies a value before saving it.
iPadSettings& iPadSettingsEdit();

#endif
