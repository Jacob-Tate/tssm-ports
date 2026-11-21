// The input test. What it prints and why is in iPadTest.h.

#include "iPadTest.h"

#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadLayout.h"
#include "iPadSettings.h"
#include "iPadTokens.h"
#include "iPadTouch.h"
#include "iTouchOverlay.h"
#include "xPad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    struct Seen
    {
        U32 on;
        S8 a1x;
        S8 a1y;
        S8 a2x;
        S8 a2y;
        U8 tl;
        U8 tr;
        bool connected;
        U32 sources;
    };

    Seen sLast;
    bool sHaveLast;
    U32 sFrame;
    iPadTestSnapshot sSnapshot;

    const char* SourceName(U32 sources)
    {
        switch (sources & (IPAD_HOST_SOURCE_PAD | IPAD_HOST_SOURCE_KEYBOARD))
        {
        case IPAD_HOST_SOURCE_PAD:
            return "pad";
        case IPAD_HOST_SOURCE_KEYBOARD:
            return (sources & IPAD_HOST_SOURCE_MOUSE) ? "keyboard+mouse" : "keyboard";
        case IPAD_HOST_SOURCE_PAD | IPAD_HOST_SOURCE_KEYBOARD:
            return "pad+keyboard";
        default:
            return (sources & IPAD_HOST_SOURCE_MOUSE) ? "mouse" :
                   (sources & IPAD_HOST_SOURCE_TOUCH) ? "touch" :
                                                        "-";
        }
    }

    // ---- the overlay ------------------------------------------------------

    void Shape(iTouchOverlayShape* shapes, S32& n, F32 x, F32 y, F32 hw, F32 hh, bool round,
               U8 r, U8 g, U8 b, bool lit, char label, F32 fill, char label2 = 0)
    {
        if (n >= ITOUCHOVERLAY_MAX_SHAPES)
        {
            return;
        }
        iTouchOverlayShape& s = shapes[n++];
        memset(&s, 0, sizeof(s));
        s.x = x;
        s.y = y;
        s.halfWidth = hw;
        s.halfHeight = hh;
        s.corner = round ? (hw < hh ? hw : hh) : (hh * 0.3f);
        s.r = r;
        s.g = g;
        s.b = b;
        s.a = lit ? 230 : (U8)(60 + 140 * fill);
        s.label = label;
        s.label2 = label2;
    }

    void Overlay(const _tagxPad* pad, U32 on, const iPadHostState* host)
    {
        // The touch controls own the overlay when they are on.
        if (iPadTouchEnabled())
        {
            return;
        }

        S32 winW = 0;
        S32 winH = 0;
        if (!iTouchOverlayWindowSize(&winW, &winH))
        {
            return;
        }

        const F32 h = (F32)winH;
        const F32 u = 0.03f * h; // one unit: a face button's radius
        const F32 ox = 4.0f * u;
        const F32 oy = h - 6.5f * u;

        iTouchOverlayShape shapes[ITOUCHOVERLAY_MAX_SHAPES];
        S32 n = 0;

        // Body.
        Shape(shapes, n, ox + 7.0f * u, oy, 10.5f * u, 4.8f * u, false, 20, 20, 20, false, 0, 0.3f);

        // Sticks: the ring is the reach, the knob where the GAME sees it.
        F32 lx = ox + 1.5f * u;
        F32 ly = oy - 1.2f * u;
        F32 rx = ox + 9.5f * u;
        F32 ry = oy + 2.0f * u;
        Shape(shapes, n, lx, ly, 2.0f * u, 2.0f * u, true, 200, 200, 200, false, 0, 0.1f);
        Shape(shapes, n, lx + 2.0f * u * pad->analog1.x / 127.0f,
              ly + 2.0f * u * pad->analog1.y / 127.0f, 0.7f * u, 0.7f * u, true, 255, 255, 255,
              pad->analog1.x != 0 || pad->analog1.y != 0, 0, 0.0f);
        Shape(shapes, n, rx, ry, 1.6f * u, 1.6f * u, true, 230, 200, 60, false, 0, 0.1f);
        Shape(shapes, n, rx + 1.6f * u * pad->analog2.x / 127.0f,
              ry + 1.6f * u * pad->analog2.y / 127.0f, 0.6f * u, 0.6f * u, true, 255, 230, 90,
              pad->analog2.x != 0 || pad->analog2.y != 0, 0, 0.0f);

        // Face buttons, as the Xbox prints them; on the Switch, the letters
        // printed in the same places, on its uncoloured buttons.
        F32 fx = ox + 12.5f * u;
        F32 fy = oy - 1.2f * u;
        const bool sw = iPadSwitchLabels();
        Shape(shapes, n, fx, fy + 1.4f * u, u, u, true, sw ? 170 : 60, sw ? 170 : 200,
              sw ? 170 : 60, (on & XPAD_BUTTON_X) != 0, sw ? 'B' : 'A', 0.0f);
        Shape(shapes, n, fx + 1.4f * u, fy, u, u, true, sw ? 170 : 220, sw ? 170 : 50,
              sw ? 170 : 50, (on & XPAD_BUTTON_TRIANGLE) != 0, sw ? 'A' : 'B', 0.0f);
        Shape(shapes, n, fx - 1.4f * u, fy, u, u, true, sw ? 170 : 50, sw ? 170 : 110,
              sw ? 170 : 230, (on & XPAD_BUTTON_O) != 0, sw ? 'Y' : 'X', 0.0f);
        Shape(shapes, n, fx, fy - 1.4f * u, u, u, true, sw ? 170 : 230, sw ? 170 : 190,
              sw ? 170 : 40, (on & XPAD_BUTTON_SQUARE) != 0, sw ? 'X' : 'Y', 0.0f);

        // White and black, Back and Start (the Switch's - and +).
        Shape(shapes, n, fx + 2.6f * u, fy + 2.4f * u, 0.6f * u, 0.6f * u, true, 240, 240, 240,
              (on & XPAD_BUTTON_L2) != 0, 0, 0.0f);
        Shape(shapes, n, fx + 1.2f * u, fy + 3.0f * u, 0.6f * u, 0.6f * u, true, 90, 90, 90,
              (on & XPAD_BUTTON_R2) != 0, 0, 0.0f);
        Shape(shapes, n, ox + 5.5f * u, oy - 1.0f * u, 0.6f * u, 0.45f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_SELECT) != 0, sw ? '-' : '=', 0.0f);
        Shape(shapes, n, ox + 7.5f * u, oy - 1.0f * u, 0.6f * u, 0.45f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_START) != 0, sw ? '+' : '>', 0.0f);

        // Triggers, filled as far as they are pulled (the Switch's ZL, ZR).
        F32 tl = host != NULL ? host->trigger_l : 0.0f;
        F32 tr = host != NULL ? host->trigger_r : 0.0f;
        Shape(shapes, n, ox + 1.0f * u, oy - 5.2f * u, 1.6f * u, 0.7f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_L1) != 0, sw ? 'Z' : 'L', tl, sw ? 'L' : 0);
        Shape(shapes, n, ox + 13.0f * u, oy - 5.2f * u, 1.6f * u, 0.7f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_R1) != 0, sw ? 'Z' : 'R', tr, sw ? 'R' : 0);

        // Z on its own, which Back and black both carry.
        Shape(shapes, n, ox + 6.5f * u, oy - 3.4f * u, 0.8f * u, 0.45f * u, false, 120, 90, 220,
              (on & XPAD_BUTTON_Z) != 0, 'Z', 0.0f);

        // D-pad.
        F32 dx = ox + 4.5f * u;
        F32 dy = oy + 2.2f * u;
        Shape(shapes, n, dx, dy - 0.9f * u, 0.45f * u, 0.55f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_UP) != 0, 0, 0.0f);
        Shape(shapes, n, dx, dy + 0.9f * u, 0.45f * u, 0.55f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_DOWN) != 0, 0, 0.0f);
        Shape(shapes, n, dx - 0.9f * u, dy, 0.55f * u, 0.45f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_LEFT) != 0, 0, 0.0f);
        Shape(shapes, n, dx + 0.9f * u, dy, 0.55f * u, 0.45f * u, false, 200, 200, 200,
              (on & XPAD_BUTTON_RIGHT) != 0, 0, 0.0f);

        iTouchOverlaySubmit(shapes, n);
    }
} // namespace

void iPadTestDescribeBits(U32 bits, char* out, S32 size)
{
    struct Name
    {
        U32 bit;
        const char* name;
    };

    // The Xbox controller's names for the bits; Z and R2/Select are listed
    // apart because black and Back each set two.
    static const Name kNames[] = {
        { XPAD_BUTTON_X, "A" },         { XPAD_BUTTON_TRIANGLE, "B" },
        { XPAD_BUTTON_O, "X" },         { XPAD_BUTTON_SQUARE, "Y" },
        { XPAD_BUTTON_L1, "LT" },       { XPAD_BUTTON_R1, "RT" },
        { XPAD_BUTTON_L2, "White" },    { XPAD_BUTTON_R2, "Black" },
        { XPAD_BUTTON_SELECT, "Back" }, { XPAD_BUTTON_Z, "Z" },
        { XPAD_BUTTON_START, "Start" }, { XPAD_BUTTON_UP, "Up" },
        { XPAD_BUTTON_DOWN, "Down" },   { XPAD_BUTTON_LEFT, "Left" },
        { XPAD_BUTTON_RIGHT, "Right" },
    };

    if (size <= 0)
    {
        return;
    }
    out[0] = '\0';

    S32 used = 0;
    U32 rest = bits;
    for (U32 i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
    {
        if (!(bits & kNames[i].bit))
        {
            continue;
        }
        rest &= ~kNames[i].bit;
        S32 w = snprintf(out + used, (size_t)(size - used), "%s%s", used ? " " : "",
                         kNames[i].name);
        if (w < 0 || used + w >= size)
        {
            return;
        }
        used += w;
    }

    if (rest != 0)
    {
        snprintf(out + used, (size_t)(size - used), "%s0x%x", used ? " " : "", (unsigned)rest);
    }
    else if (used == 0)
    {
        snprintf(out, (size_t)size, "-");
    }
}

void iPadTestStart()
{
    const iPadSettings& s = iPadSettingsGet();
    if (s.test == IPAD_TEST_OFF)
    {
        return;
    }

    static const char* const kCurve[] = { "xbox", "gamecube", "linear" };
    static const char* const kKeyboard[] = { "always", "fallback", "off" };

    printf("tssm: input test on (%s). preset %s, layout %s, stick curve %s, deadzone %s, "
           "trigger click at %d%%, keyboard %s, mouse look %s\n",
           s.test == IPAD_TEST_OVERLAY ? "log + overlay" : "log", iPadBindActivePreset(),
           iPadLayoutName(iPadLayoutActive()), kCurve[s.curve], s.deadzone < 0.0f ? "auto" : "set",
           (int)(s.trigger_threshold * 100.0f + 0.5f), kKeyboard[s.keyboard],
           s.mouse_look ? "on" : "off");
    fflush(stdout);
}

const iPadTestSnapshot& iPadTestLast()
{
    return sSnapshot;
}

void iPadTestFrame(const _tagxPad* pad, U32 on, const iPadHostState* host)
{
    sSnapshot.on = on;
    sSnapshot.stick_x = pad->analog1.x;
    sSnapshot.stick_y = pad->analog1.y;
    sSnapshot.cstick_x = pad->analog2.x;
    sSnapshot.cstick_y = pad->analog2.y;
    sSnapshot.trigger_l = host != NULL ? host->trigger_l : 0.0f;
    sSnapshot.trigger_r = host != NULL ? host->trigger_r : 0.0f;
    sSnapshot.sources = host != NULL ? host->sources : 0;
    sSnapshot.connected = host != NULL && host->connected;

    const iPadSettings& s = iPadSettingsGet();
    if (s.test == IPAD_TEST_OFF)
    {
        return;
    }

    sFrame++;

    Seen now;
    now.on = on;
    now.a1x = pad->analog1.x;
    now.a1y = pad->analog1.y;
    now.a2x = pad->analog2.x;
    now.a2y = pad->analog2.y;
    now.tl = (U8)(host != NULL ? host->trigger_l * 100.0f + 0.5f : 0);
    now.tr = (U8)(host != NULL ? host->trigger_r * 100.0f + 0.5f : 0);
    now.connected = host != NULL && host->connected;
    now.sources = host != NULL ? host->sources : 0;

    if (!sHaveLast || memcmp(&now, &sLast, sizeof(now)) != 0)
    {
        char bits[160];
        iPadTestDescribeBits(on, bits, sizeof(bits));

        U32 pressed = sHaveLast ? (on & ~sLast.on) : on;
        char down[160];
        iPadTestDescribeBits(pressed, down, sizeof(down));

        const char* device = iPadHostDeviceName(0);

        printf("tssm: input %6u %-9s on=%06x [%s] new=[%s] stick=(%4d,%4d) cstick=(%4d,%4d) "
               "lt=%3u%% rt=%3u%% from=%s (%s)\n",
               (unsigned)sFrame, now.connected ? "connected" : "absent", (unsigned)on, bits,
               pressed ? down : "", (int)now.a1x, (int)now.a1y, (int)now.a2x, (int)now.a2y,
               (unsigned)now.tl, (unsigned)now.tr, SourceName(now.sources),
               device != NULL ? device : "nothing");
        fflush(stdout);

        sLast = now;
        sHaveLast = true;
    }

    if (s.test == IPAD_TEST_OVERLAY)
    {
        Overlay(pad, on, host);
    }
}
