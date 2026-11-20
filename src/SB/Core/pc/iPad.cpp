#include "iPad.h"
#include "iMenuMouse.h"
#include "iPadHost.h"
#include "iPadPrompts.h"
#include "iPadSettings.h"
#include "iPadTest.h"
#include "gui/iGui.h"

#include <types.h>

#include "xPad.h"
#include "xTRC.h"

#include <math.h>

// PC iPad: the device is a backend behind iPadHost.h, and this file turns what
// it reports into the _tagxPad fields the game reads -- the same job
// gc/iPad.cpp does for PADRead -- plus TSSM's rumble from gc/iPad.cpp.
//
// TSSM-ADAPTED from treedome's pc/iPad.cpp: the stick curve is a setting
// (input.stick_curve: the Xbox release's own curve by default), the camera
// stick takes the sensitivity, inversion and the mouse, and the input test and
// the button prompts hook in here because this runs once per frame for port 0.
//
// TSSM drives the GameCube motor as a pulse train: iPadStartRumble sets a
// rate from the intensity, and every iPadUpdate adds it to a level and runs
// the motor for one frame each time the level passes 3600. iPadHostRumble is
// the same on/off motor, so the arithmetic is kept as retail has it.
//
// Not carried: iPadInHandReset, the console's B + X + Start "reset" held for
// half a second, which on a host would quit the game from a button combo.

F32 g_rumblingThreshold = 0.5f;
static U32 sPadMotor[IPAD_MAX_CONTROLLERS];
static S32 sPadCurrLevel[IPAD_MAX_CONTROLLERS];
static S32 sPadDeltaLevel[IPAD_MAX_CONTROLLERS];

#define IPAD_MOTOR_STOP 0
#define IPAD_MOTOR_RUMBLE 1

S32 iPadInit()
{
    iPadHostInit();
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        sPadCurrLevel[i] = 0;
        sPadDeltaLevel[i] = 0;
        sPadMotor[i] = IPAD_MOTOR_STOP;
    }
    iPadTestStart();
    return 1;
}

_tagxPad* iPadEnable(_tagxPad* pad, S16 port)
{
    U32 new_on = 0;

    pad->port = port;
    pad->slot = 0;
    pad->state = ePad_Enabled;
    gTrcPad[pad->port].state = TRC_PadInserted;
    pad->flags |= 3;
    pad->flags |= 4;
    iPadUpdate(pad, &new_on);
    pad->on = new_on;
    pad->pressed = 0;
    pad->released = 0;
    return pad;
}

// ---------------------------------------------------------------------------
// Stick curves. Each takes a raw host stick (unit square, Y up-positive) and
// gives the game's S8 pair (Y still up-positive; the caller negates it).

static S8 ClampS8(F32 v)
{
    if (v > 127.0f)
    {
        return 127;
    }
    if (v < -127.0f)
    {
        return -127;
    }
    return (S8)v;
}

// The Xbox release's (DEFAULT.XBE, the conversion its iPadUpdate calls for
// both sticks): each axis on its own, |v| <= 0.26 is nothing, |v| >= 0.70 is
// full, straight between, times 128 and clamped.
static S8 CurveXboxAxis(F32 v, F32 deadzone)
{
    const F32 full = 0.70f;
    F32 dz = deadzone >= 0.0f ? deadzone : 0.26f;
    F32 top = (dz < full - 0.05f) ? full : dz + 0.05f;

    F32 a = fabsf(v);
    if (a <= dz)
    {
        return 0;
    }

    F32 t = (a - dz) / (top - dz);
    if (t > 1.0f)
    {
        t = 1.0f;
    }

    F32 out = t * 128.0f;
    return ClampS8(v < 0.0f ? -out : out);
}

// Retail's: GameCube stick units (about 40 at full deflection after PADClamp)
// scaled by 3.2 to the game's 127, with both clamps.
static S8 iPadConvStick(F32 in)
{
    F32 fin;

    if (in > 40.0f)
    {
        in = 40.0f;
    }
    else if (in < -40.0f)
    {
        in = -40.0f;
    }

    fin = 3.2f * in;
    if (fin > 127.0f)
    {
        fin = 127.0f;
    }
    else if (fin < -127.0f)
    {
        fin = -127.0f;
    }

    return (S8)fin;
}

// PADClamp's octagon (src/dolphin/src/pad/Padclamp.c): reach along an axis and
// on a diagonal, for the main stick and the C stick.
#define IPAD_STICK_MAX 72.0f
#define IPAD_STICK_XY 40.0f
#define IPAD_SUBSTICK_MAX 59.0f
#define IPAD_SUBSTICK_XY 31.0f

// A host stick reports a circle; the game's DampenControls measures the
// stick as max(|x|,|y|), which on a circle drops to 0.71 on the diagonals.
// Mapping onto the console's octagon puts full deflection at full speed in
// every direction (treedome).
static void iPadShapeStick(F32 inX, F32 inY, F32 max, F32 xy, F32* outX, F32* outY)
{
    F32 x = max * inX;
    F32 y = max * inY;
    F32 ax = (x < 0.0f) ? -x : x;
    F32 ay = (y < 0.0f) ? -y : y;
    F32 d = (ay <= ax) ? (xy * ax + (max - xy) * ay) : (xy * ay + (max - xy) * ax);

    if (xy * max < d)
    {
        F32 scale = (xy * max) / d;
        x *= scale;
        y *= scale;
    }

    *outX = x;
    *outY = y;
}

// A round deadzone, rescaled from its edge so the first movement past it is
// small rather than a jump (iPadStick.cpp's rule, on floats).
static void RadialDeadzone(F32 x, F32 y, F32 deadzone, F32* outX, F32* outY)
{
    F32 m = sqrtf(x * x + y * y);
    if (m <= deadzone || m <= 0.0f)
    {
        *outX = 0.0f;
        *outY = 0.0f;
        return;
    }

    F32 dx = x / m;
    F32 dy = y / m;
    if (m > 1.0f)
    {
        m = 1.0f;
    }

    F32 t = (m - deadzone) / (1.0f - deadzone);
    *outX = dx * t;
    *outY = dy * t;
}

static void ApplyCurve(F32 x, F32 y, bool sub, S8* outX, S8* outY)
{
    const iPadSettings& cfg = iPadSettingsGet();

    switch (cfg.curve)
    {
    case IPAD_CURVE_GAMECUBE:
    {
        // XInput's documented deadzones by default, as treedome uses them.
        F32 dz = cfg.deadzone >= 0.0f ? cfg.deadzone : (sub ? 8689.0f : 7849.0f) / 32767.0f;
        F32 ux;
        F32 uy;
        RadialDeadzone(x, y, dz, &ux, &uy);

        F32 sx;
        F32 sy;
        if (sub)
        {
            iPadShapeStick(ux, uy, IPAD_SUBSTICK_MAX, IPAD_SUBSTICK_XY, &sx, &sy);
        }
        else
        {
            iPadShapeStick(ux, uy, IPAD_STICK_MAX, IPAD_STICK_XY, &sx, &sy);
        }
        *outX = iPadConvStick(sx);
        *outY = iPadConvStick(sy);
        break;
    }

    case IPAD_CURVE_LINEAR:
    {
        F32 dz = cfg.deadzone >= 0.0f ? cfg.deadzone : 0.15f;
        F32 ux;
        F32 uy;
        RadialDeadzone(x, y, dz, &ux, &uy);
        *outX = ClampS8(ux * 127.0f);
        *outY = ClampS8(uy * 127.0f);
        break;
    }

    default:
        *outX = CurveXboxAxis(x, cfg.deadzone);
        *outY = CurveXboxAxis(y, cfg.deadzone);
        break;
    }
}

// The camera stick, after the curve: sensitivity scales how far past the
// game's 32 it reaches (zCameraUpdate turns at a rate proportional to that),
// so 1.0 is retail and the deadzone does not move.
static S8 CameraAxis(S8 v, F32 sensitivity, S32 invert, F32 mouse)
{
    F32 f = (F32)v;
    if (invert)
    {
        f = -f;
    }

    F32 a = fabsf(f);
    if (a > 32.0f && sensitivity != 1.0f)
    {
        a = 32.0f + (a - 32.0f) * sensitivity;
        f = f < 0.0f ? -a : a;
    }

    if (mouse != 0.0f)
    {
        // The mouse already speaks in these units (iPadKeyboardSDL.cpp); a
        // stick held the other way loses.
        f = (fabsf(mouse) > fabsf(f)) ? mouse : f + mouse;
    }

    return ClampS8(f);
}

static void iPadUpdateRumble(_tagxPad* pad)
{
    if (pad->flags & 8)
    {
        sPadCurrLevel[pad->port] += sPadDeltaLevel[pad->port];
        if (sPadCurrLevel[pad->port] > 3600)
        {
            sPadCurrLevel[pad->port] = 0;
            if (sPadMotor[pad->port] != IPAD_MOTOR_RUMBLE)
            {
                sPadMotor[pad->port] = IPAD_MOTOR_RUMBLE;
                iPadHostRumble(pad->port, 1);
            }
        }
        else if (sPadMotor[pad->port] != IPAD_MOTOR_STOP)
        {
            sPadMotor[pad->port] = IPAD_MOTOR_STOP;
            iPadHostRumble(pad->port, 0);
        }
    }
}

S32 iPadUpdate(_tagxPad* pad, U32* on)
{
    // One poll per frame feeds every port, where the console calls PADRead.
    if (pad->port == 0)
    {
        iPadHostPoll();
    }

    const iPadHostState* host = iPadHostGet(pad->port);

    // The overlay's controller combo, and no game input while it is open
    // (gui/iGui.h).
    host = iGuiFilterPad(pad->port, host);

    if (host == NULL || !host->connected)
    {
        // Retail's PAD_ERR_NO_CONTROLLER arm.
        xTRCPad(pad->port, TRC_PadMissing);
        pad->released = pad->on;
        pad->on = 0;
        pad->analog1.x = 0;
        pad->analog1.y = 0;
        pad->analog2.x = 0;
        pad->analog2.y = 0;

        if (pad->port == 0)
        {
            iPadTestFrame(pad, 0, host);
        }
        return 0;
    }

    // The backend maps device buttons to XPAD_BUTTON_* itself (iPadBind.h);
    // the console does it here with the Z-modifier trick, which the gamecube
    // preset's chords reproduce.
    *on = host->buttons;

    // The mouse in the game's menus (iMenuMouse.h): its click, right click and
    // wheel as the buttons a controller would press, in place of the ones
    // the mouse buttons are bound to.
    if (pad->port == 0)
    {
        *on = iMenuMouseUpdate((pad->flags & 0x10) != 0, *on);
    }

    const iPadSettings& cfg = iPadSettingsGet();

    S8 x;
    S8 y;
    ApplyCurve(host->stick_x, host->stick_y, false, &x, &y);
    pad->analog1.x = x;
    pad->analog1.y = (S8)-y;

    ApplyCurve(host->substick_x, host->substick_y, true, &x, &y);
    pad->analog2.x = CameraAxis(x, cfg.camera_sensitivity, cfg.camera_invert_x, host->camera_x);
    pad->analog2.y =
        CameraAxis((S8)-y, cfg.camera_sensitivity, cfg.camera_invert_y, host->camera_y);

    if (gTrcPad[pad->port].state != TRC_PadInserted)
    {
        xTRCPad(pad->port, TRC_PadInserted);
    }

    iPadUpdateRumble(pad);

    if (pad->port == 0)
    {
        iPadTestFrame(pad, *on, host);
        iPadPromptsUpdate(host);
    }
    return 1;
}

// Empty in retail too.
void iPadRumbleFx(_tagxPad* p, _tagxRumble* r, F32 time_passed)
{
}

void iPadStopRumble(_tagxPad* pad)
{
    sPadCurrLevel[pad->port] = 0;
    sPadDeltaLevel[pad->port] = 0;
    sPadMotor[pad->port] = IPAD_MOTOR_STOP;
    iPadHostRumble(pad->port, 0);
}

// Retail stops globals.currentActivePad's motor, reaching from the platform
// layer into the game's globals. The host stops every port instead (treedome),
// which is the same for a single player and needs no game header here.
void iPadStopRumble()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        sPadCurrLevel[i] = 0;
        sPadDeltaLevel[i] = 0;
        sPadMotor[i] = IPAD_MOTOR_STOP;
        iPadHostRumble(i, 0);
    }
}

void iPadStartRumble(_tagxPad* pad, F32 intensity)
{
    if (pad->flags & 8)
    {
        if (intensity < g_rumblingThreshold)
        {
            sPadCurrLevel[pad->port] = 0;
            sPadDeltaLevel[pad->port] = 0;
            sPadMotor[pad->port] = IPAD_MOTOR_STOP;
        }
        else
        {
            sPadCurrLevel[pad->port] = 0;
            sPadDeltaLevel[pad->port] = (S32)(3600.0 * (0.05 + intensity));
            sPadMotor[pad->port] = IPAD_MOTOR_RUMBLE;
        }
        iPadHostRumble(pad->port, sPadMotor[pad->port] == IPAD_MOTOR_RUMBLE);
    }
}

void iPadKill()
{
    iPadPromptsShutdown();
    iPadHostExit();
}
