// config.ini's [input] settings as plain values. See iPadSettings.h; the
// settings themselves (defaults, ranges, comments) are in iConfigTable.cpp.

#include "iPadSettings.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadLayout.h"
#include "iPadStick.h"

#include <stdio.h>
#include <stdlib.h>

namespace
{
    iPadSettings sSettings = { IPAD_KEYBOARD_ALWAYS,
                               IPAD_CURVE_XBOX,
                               0.30f,
                               -1.0f,
                               1.0f,
                               0,
                               0,
                               1,
                               1.0f,
                               1.0f,
                               IPAD_TEST_OFF,
                               0,
                               1.0f,
                               1.0f };

    F32 clampf(F32 v, F32 lo, F32 hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // A percentage setting, or `fallback` for "auto" or a value out of range.
    F32 percent(const char* key, F32 fallback, F32 lo, F32 hi)
    {
        const char* v = iConfigGetString(key, "auto");
        if (v == NULL || v[0] == '\0' || iHostStrCaseCmp(v, "auto") == 0)
        {
            return fallback;
        }

        char* end = NULL;
        F32 p = (F32)strtod(v, &end);
        if (end == v || p < lo || p > hi)
        {
            printf("tssm: config: %s is '%s', not auto or %g to %g; using the default\n", key, v,
                   (double)lo, (double)hi);
            fflush(stdout);
            return fallback;
        }
        return p * 0.01f;
    }
} // namespace

const iPadSettings& iPadSettingsGet()
{
    return sSettings;
}

iPadSettings& iPadSettingsEdit()
{
    return sSettings;
}

void iPadSettingsLoad()
{
    iPadSettings& s = sSettings;

    const char* kb = iConfigGetString("input.keyboard", "always");
    s.keyboard = (iHostStrCaseCmp(kb, "fallback") == 0) ? IPAD_KEYBOARD_FALLBACK :
                 (iHostStrCaseCmp(kb, "off") == 0)      ? IPAD_KEYBOARD_OFF :
                                                          IPAD_KEYBOARD_ALWAYS;

    const char* curve = iConfigGetString("input.stick_curve", "xbox");
    s.curve = (iHostStrCaseCmp(curve, "gamecube") == 0) ? IPAD_CURVE_GAMECUBE :
              (iHostStrCaseCmp(curve, "linear") == 0)   ? IPAD_CURVE_LINEAR :
                                                          IPAD_CURVE_XBOX;

    s.trigger_threshold = percent("input.trigger_threshold", 0.30f, 5.0f, 95.0f);
    s.deadzone = percent("input.deadzone", -1.0f, 0.0f, 90.0f);
    iPadStickSetDeadzone(s.deadzone < 0.0f ? -1.0f : s.deadzone * 100.0f);

    s.camera_sensitivity = clampf(iConfigGetFloat("input.camera_sensitivity", 1.0f), 0.1f, 5.0f);
    s.camera_invert_x = iConfigGetBool("input.camera_invert_x", FALSE);
    s.camera_invert_y = iConfigGetBool("input.camera_invert_y", FALSE);

    s.mouse_camera = iConfigGetBool("input.mouse_camera", TRUE);
    s.mouse_sensitivity = clampf(iConfigGetFloat("input.mouse_sensitivity", 1.0f), 0.05f, 20.0f);

    s.rumble = clampf(iConfigGetFloat("input.rumble", 100.0f), 0.0f, 100.0f) * 0.01f;

    s.touch_drag_look = iConfigGetBool("input.touch_drag_look", FALSE);
    s.touch_scale = clampf(iConfigGetFloat("input.touch_scale", 1.0f), 0.5f, 1.2f);
    s.touch_opacity = clampf(iConfigGetFloat("input.touch_opacity", 100.0f), 10.0f, 100.0f) * 0.01f;

    // TSSM_INPUT_TEST beats the file, so a test run needs no config edit.
    const char* test = getenv("TSSM_INPUT_TEST");
    if (test == NULL || test[0] == '\0')
    {
        test = iConfigGetString("input.test", "off");
    }
    if (iHostStrCaseCmp(test, "overlay") == 0)
    {
        s.test = IPAD_TEST_OVERLAY;
    }
    else if (iHostStrCaseCmp(test, "off") == 0 || iHostStrCaseCmp(test, "0") == 0)
    {
        s.test = IPAD_TEST_OFF;
    }
    else
    {
        // "log", "on", "1" and anything else that is not off.
        s.test = IPAD_TEST_LOG;
    }

    iPadLayoutSetChoice(iConfigGetString("input.layout", "auto"));
}
