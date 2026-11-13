// tssm_input_test: the input layer, end to end, with no game data, no window
// and no real device.
//
// It drives SDL's own virtual controllers -- attached, pressed and unplugged
// through SDL_AttachVirtualJoystick and friends, so the events go through the
// same SDL gamepad code a USB pad's do -- a stand-in keyboard and mouse
// (iPadKeyboardSetTestSource), and the touch controls, with fingers posted as
// SDL's touch events (SDL_PushEvent), which the touch layer watches for as it
// does on Android. Each frame calls the GAME's xPadUpdate (the real
// src/SB/Core/x/xPad.cpp, compiled into this test) and checks what the game
// sees: mPad[0].on / pressed / released and the two sticks.
//
//     tssm_input_test [scratch folder]
//
// The folder gets the test's config.ini (default: the system temp folder).
// Exit status 0 when every check passes.

#include "iConfig.h"
#include "iPad.h"
#include "iPadBind.h"
#include "iPadControls.h"
#include "iPadHost.h"
#include "iPadKeyboard.h"
#include "iPadLayout.h"
#include "iPadPrompts.h"
#include "iPadSettings.h"
#include "iPadTest.h"
#include "iPadTokens.h"
#include "iPadTouch.h"
#include "iTouchOverlay.h"
#include "xPad.h"
#include "xTRC.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The few game symbols the pad path reaches, which the full game would supply.

_tagxPad mPad[4];
_tagTRCPadInfo gTrcPad[4];

void xTRCPad(S32 pad_id, _tagTRCState state)
{
    gTrcPad[pad_id].state = state;
}

// xMath.cpp's, for xatan2 in xPadNormalizeAnalog.
F32 xAngleClampFast(F32 a)
{
    if (a < 0.0f)
    {
        a += 6.2831855f;
    }
    else if (a >= 6.2831855f)
    {
        a -= 6.2831855f;
    }
    return a;
}

U32 zScene_ScreenAdjustMode()
{
    return 0;
}

static S32 sMenuRunning;

S32 zMenuRunning()
{
    return sMenuRunning;
}

S32 zGameIsPaused()
{
    return 0;
}

// No packages are loaded, so no prompt asset exists; the prompt code is tested
// on buffers below.
void* xSTFindAsset(U32, U32* size)
{
    if (size != NULL)
    {
        *size = 0;
    }
    return NULL;
}

U32 xStrHash(const char* str)
{
    U32 i = 0;
    for (; *str != '\0'; ++str)
    {
        char c = *str;
        c = c - (c & (c >> 1) & 0x20);
        i = i * 131 + c;
    }
    return i;
}

// ---------------------------------------------------------------------------

static S32 sChecks;
static S32 sFailures;

static void Check(bool ok, const char* what)
{
    sChecks++;
    if (!ok)
    {
        sFailures++;
        printf("FAIL  %s\n", what);
    }
}

static void CheckBits(U32 got, U32 want, const char* what)
{
    sChecks++;
    if (got != want)
    {
        sFailures++;
        char g[160];
        char w[160];
        iPadTestDescribeBits(got, g, sizeof(g));
        iPadTestDescribeBits(want, w, sizeof(w));
        printf("FAIL  %s: got %06x [%s], want %06x [%s]\n", what, (unsigned)got, g,
               (unsigned)want, w);
    }
}

static void CheckInt(S32 got, S32 want, const char* what)
{
    sChecks++;
    if (got != want)
    {
        sFailures++;
        printf("FAIL  %s: got %d, want %d\n", what, (int)got, (int)want);
    }
}

static void CheckStr(const char* got, const char* want, const char* what)
{
    sChecks++;
    if (got == NULL || want == NULL ? got != want : strcmp(got, want) != 0)
    {
        sFailures++;
        printf("FAIL  %s: got '%s', want '%s'\n", what, got != NULL ? got : "(null)",
               want != NULL ? want : "(null)");
    }
}

// ---- the stand-in keyboard and mouse -------------------------------------------

static bool sKeys[2048];

static bool TestKeyDown(S32 id)
{
    return id >= 0 && id < 2048 && sKeys[id];
}

static iPadKeyboardTestSource sKeyboard = { TestKeyDown, 0.0f, 0.0f };

static void KeysUp()
{
    memset(sKeys, 0, sizeof(sKeys));
}

// ---- virtual controllers ---------------------------------------------------------

struct Pad
{
    SDL_JoystickID id;
    SDL_Joystick* joy;
};

static void Release(struct Pad& p);

// A controller SDL treats as a gamepad, with every standard button and axis.
// The virtual driver numbers them in SDL_GamepadButton order, so button `b`
// below IS SDL_GAMEPAD_BUTTON b, and likewise the axes.
static Pad Attach(Uint16 vendor, Uint16 product, const char* name)
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = vendor;
    desc.product_id = product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.button_mask = (1u << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.name = name;

    Pad p;
    p.id = SDL_AttachVirtualJoystick(&desc);
    p.joy = (p.id != 0) ? SDL_OpenJoystick(p.id) : NULL;
    if (p.joy == NULL)
    {
        printf("FAIL  could not attach a virtual %s: %s\n", name, SDL_GetError());
        sFailures++;
    }
    else
    {
        Release(p);
    }
    return p;
}

static void Detach(Pad& p)
{
    if (p.joy != NULL)
    {
        SDL_CloseJoystick(p.joy);
    }
    SDL_DetachVirtualJoystick(p.id);
    p.joy = NULL;
}

static void Button(Pad& p, SDL_GamepadButton b, bool down)
{
    SDL_SetJoystickVirtualButton(p.joy, (int)b, down);
}

static void Axis(Pad& p, SDL_GamepadAxis a, F32 v)
{
    F32 raw = v * 32767.0f;

    // A virtual trigger is a full-range joystick axis that SDL halves onto the
    // trigger's 0..32767: released is -32768, not 0.
    if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
    {
        raw = v * 65535.0f - 32768.0f;
    }
    if (raw < -32768.0f)
    {
        raw = -32768.0f;
    }
    SDL_SetJoystickVirtualAxis(p.joy, (int)a, (Sint16)raw);
}

static void Release(Pad& p)
{
    for (S32 b = 0; b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; b++)
    {
        Button(p, (SDL_GamepadButton)b, false);
    }
    for (S32 a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
    {
        Axis(p, (SDL_GamepadAxis)a, 0.0f);
    }
}

// One game frame.
static _tagxPad& Frame()
{
    xPadUpdate(0, 1.0f / 60.0f);
    return mPad[0];
}

// Press, look, let go: what `on` held while it was down.
static U32 Tap(Pad& p, SDL_GamepadButton b)
{
    Button(p, b, true);
    U32 on = Frame().on;
    Button(p, b, false);
    Frame();
    return on;
}

// ---------------------------------------------------------------------------

static void TestTables()
{
    // The move layout: TRIANGLE and O trade under xbox, nothing else moves.
    iPadLayoutSetChoice("xbox");
    CheckBits(iPadLayoutButton(XPAD_BUTTON_TRIANGLE), XPAD_BUTTON_O, "xbox layout: spin reads O");
    CheckBits(iPadLayoutButton(XPAD_BUTTON_O), XPAD_BUTTON_TRIANGLE, "xbox layout: bowl reads TRI");
    CheckBits(iPadLayoutButton(XPAD_BUTTON_X), XPAD_BUTTON_X, "xbox layout: jump stays");
    CheckBits(iPadLayoutButton(XPAD_BUTTON_SQUARE), XPAD_BUTTON_SQUARE, "xbox layout: bash stays");
    CheckBits(iPadLayoutButton(0xc0000), 0x60000, "xbox layout: taxi's TRI|SQUARE");
    CheckBits(iPadLayoutButton(XPAD_BUTTON_R1 | XPAD_BUTTON_O), XPAD_BUTTON_R1 | XPAD_BUTTON_TRIANGLE,
              "xbox layout: other bits ride along");
    iPadLayoutSetChoice("gamecube");
    CheckBits(iPadLayoutButton(XPAD_BUTTON_TRIANGLE), XPAD_BUTTON_TRIANGLE, "gc layout: identity");
    CheckBits(iPadLayoutButton(0xc0000), 0xc0000, "gc layout: identity on a mask");
    iPadLayoutSetChoice("auto");

    // The move prompts, rewritten in place.
    char spin[] = "{i:button_picture_04}";
    char bowl[] = "{i:button_picture_03}";
    const iPadPromptMove* spinMove = NULL;
    const iPadPromptMove* bowlMove = NULL;
    for (S32 i = 0; i < kPadPromptMoveCount; i++)
    {
        if (strcmp(kPadPromptMoves[i].name, "button_spinattack_text") == 0)
        {
            spinMove = &kPadPromptMoves[i];
        }
        if (strcmp(kPadPromptMoves[i].name, "button_bowlattack_text") == 0)
        {
            bowlMove = &kPadPromptMoves[i];
        }
    }
    Check(spinMove != NULL && bowlMove != NULL, "prompt table has spin and bowl");
    if (spinMove != NULL && bowlMove != NULL)
    {
        Check(!iPadPromptsPatchMove(*spinMove, spin, sizeof(spin), IPAD_LAYOUT_XBOX),
              "xbox layout leaves the Xbox spin prompt");
        Check(iPadPromptsPatchMove(*spinMove, spin, sizeof(spin), IPAD_LAYOUT_GAMECUBE) &&
                  strcmp(spin, "{i:button_picture_03}") == 0,
              "gamecube layout: spin prompt shows B");
        Check(iPadPromptsPatchMove(*bowlMove, bowl, sizeof(bowl), IPAD_LAYOUT_GAMECUBE) &&
                  strcmp(bowl, "{i:button_picture_04}") == 0,
              "gamecube layout: bowl prompt shows X");
        Check(iPadPromptsPatchMove(*spinMove, spin, sizeof(spin), IPAD_LAYOUT_XBOX) &&
                  strcmp(spin, "{i:button_picture_04}") == 0,
              "back to xbox: spin prompt shows X again");
    }

    // "Pull" / "Press", in place, with the length word kept right.
    {
        char word[8] = "Pull";
        U32 len = 4;
        Check(iPadPromptsPatchVerb(&len, word, sizeof(word), true) && strcmp(word, "Press") == 0 &&
                  len == 5,
              "keyboard: 'Pull' becomes 'Press'");
        Check(iPadPromptsPatchVerb(&len, word, sizeof(word), false) && strcmp(word, "Pull") == 0 &&
                  len == 4,
              "a trigger again: back to 'Pull'");
        char tight[5] = "pull";
        len = 4;
        Check(!iPadPromptsPatchVerb(&len, tight, sizeof(tight), true) && strcmp(tight, "pull") == 0,
              "no room for 'press': left alone");
    }

    CheckBits(iPadPromptsTextureBits("pad_button3"), XPAD_BUTTON_TRIANGLE, "pad_button3 is B");
    CheckBits(iPadPromptsTextureBits("pad_button4"), XPAD_BUTTON_O, "pad_button4 is X");

    // The parser.
    S32 count;
    const iPadBindToken* tokens = iPadPadTokens(&count);
    iPadBind b;
    char err[128];
    Check(iPadBindParseQuiet("rt+!rb, misc1", tokens, count, &b, err, sizeof(err)) && b.alts == 2,
          "parse a chord with a negation and an alternative");
    Check(!iPadBindParseQuiet("rt+", tokens, count, &b, err, sizeof(err)) && err[0] != '\0',
          "a dangling '+' is refused with a reason");
    Check(!iPadBindParseQuiet("!rb", tokens, count, &b, err, sizeof(err)),
          "a binding of only negations is refused");
    char text[64];
    iPadBindParseQuiet("RT + !RB ,misc1", tokens, count, &b, NULL, 0);
    iPadBindFormat(b, tokens, count, text, sizeof(text));
    CheckStr(text, "rt+!rb, misc1", "a binding is written back in one spelling");
}

static void TestXboxPad()
{
    Pad pad = Attach(0x045e, 0x0b12, "Xbox Series X Controller (virtual)");
    Frame();
    Frame();

    CheckStr(iPadHostPadKind(), "xbox", "an Xbox pad is the xbox kind");
    CheckStr(iPadBindActivePreset(), "xbox", "preset auto -> xbox");
    CheckInt(iPadLayoutActive(), IPAD_LAYOUT_XBOX, "layout auto -> xbox with an Xbox pad");

    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), XPAD_BUTTON_X, "A");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_EAST), XPAD_BUTTON_TRIANGLE, "B");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_WEST), XPAD_BUTTON_O, "X");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_NORTH), XPAD_BUTTON_SQUARE, "Y");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), XPAD_BUTTON_L2, "LB = white = L2");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), XPAD_BUTTON_R2 | XPAD_BUTTON_Z,
              "RB = black = R2 + Z");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_BACK), XPAD_BUTTON_SELECT | XPAD_BUTTON_Z,
              "Back = Select + Z");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_START), XPAD_BUTTON_START, "Start");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_DPAD_UP), XPAD_BUTTON_UP, "d-pad up");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN), XPAD_BUTTON_DOWN, "d-pad down");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT), XPAD_BUTTON_LEFT, "d-pad left");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT), XPAD_BUTTON_RIGHT, "d-pad right");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK), 0, "LS click is unbound (no TSSM use)");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK), 0, "RS click is unbound");

    // Edges, as xPadUpdate makes them.
    Button(pad, SDL_GAMEPAD_BUTTON_SOUTH, true);
    _tagxPad& p1 = Frame();
    CheckBits(p1.pressed, XPAD_BUTTON_X, "pressed on the frame A goes down");
    _tagxPad& p2 = Frame();
    CheckBits(p2.pressed, 0, "not pressed again while held");
    CheckBits(p2.on, XPAD_BUTTON_X, "still on while held");
    Button(pad, SDL_GAMEPAD_BUTTON_SOUTH, false);
    _tagxPad& p3 = Frame();
    CheckBits(p3.released, XPAD_BUTTON_X, "released on the frame A comes up");

    // Triggers: analog, clicking at 30% of the pull.
    Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.25f);
    CheckBits(Frame().on, 0, "LT at 25% does not click");
    Check(fabsf(iPadHostGet(0)->trigger_l - 0.25f) < 0.01f, "LT's analog value is kept");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.35f);
    CheckBits(Frame().on, XPAD_BUTTON_L1, "LT at 35% clicks L1");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.0f);
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    CheckBits(Frame().on, XPAD_BUTTON_R1, "RT pulled clicks R1");
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f);
    Frame();

    // Sticks, through the Xbox release's curve: 26% dead, full at 70%.
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 1.0f);
    CheckInt(Frame().analog1.x, 127, "left stick right -> analog1.x 127");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.0f);
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
    CheckInt(Frame().analog1.y, -127, "left stick up -> analog1.y -127 (the game's up)");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTY, 0.0f);
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.2f);
    CheckInt(Frame().analog1.x, 0, "20% is inside the deadzone");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.48f);
    {
        S32 got = Frame().analog1.x;
        Check(got >= 62 && got <= 66, "48% -> about 64, a walk (AnalogMin 32 < x < AnalogMax 110)");
    }
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.7071f);
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTY, 0.7071f);
    {
        _tagxPad& p = Frame();
        Check(p.analog1.x == 127 && p.analog1.y == 127,
              "a full diagonal is full on both axes (DampenControls takes the larger)");
    }
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.0f);
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTY, 0.0f);
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX, 1.0f);
    CheckInt(Frame().analog2.x, 127, "right stick -> analog2 (the C-stick)");
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX, 0.0f);
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHTY, 1.0f);
    CheckInt(Frame().analog2.y, 127, "right stick down -> analog2.y 127");
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHTY, 0.0f);
    Frame();

    // The GameCube curve: full speed a little past half way.
    iPadSettingsEdit().curve = IPAD_CURVE_GAMECUBE;
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.85f);
    CheckInt(Frame().analog1.x, 127, "gamecube curve: 85% is full");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.2f);
    CheckInt(Frame().analog1.x, 0, "gamecube curve: 20% is inside XInput's deadzone");
    iPadSettingsEdit().curve = IPAD_CURVE_XBOX;
    Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.0f);

    // Camera sensitivity stretches past the game's 32 and leaves 32 alone.
    iPadSettingsEdit().camera_sensitivity = 0.5f;
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX, 1.0f);
    CheckInt(Frame().analog2.x, 79, "camera sensitivity 0.5: 127 -> 32 + 95/2");
    iPadSettingsEdit().camera_sensitivity = 1.0f;
    iPadSettingsEdit().camera_invert_x = 1;
    CheckInt(Frame().analog2.x, -127, "camera_invert_x flips the right stick");
    iPadSettingsEdit().camera_invert_x = 0;
    Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX, 0.0f);
    Release(pad);
    Frame();

    // The keyboard plays alongside the pad (input.keyboard = always).
    sKeys[SDL_SCANCODE_SPACE] = true;
    CheckBits(Frame().on, XPAD_BUTTON_X, "Space with a pad plugged in jumps");
    KeysUp();
    Frame();
    iPadSettingsEdit().keyboard = IPAD_KEYBOARD_FALLBACK;
    sKeys[SDL_SCANCODE_SPACE] = true;
    CheckBits(Frame().on, 0, "keyboard = fallback: keys wait while a pad is in");
    KeysUp();
    iPadSettingsEdit().keyboard = IPAD_KEYBOARD_ALWAYS;
    Frame();

    // Rebinding, through the API a settings screen uses.
    char err[128];
    S32 rowA = iPadControlsFindRow("a");
    Check(iPadControlsSet(IPAD_BIND_PAD, rowA, "b", err, sizeof(err)), "rebind a to the B position");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_EAST), XPAD_BUTTON_X | XPAD_BUTTON_TRIANGLE,
              "B now presses both A's row and its own");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), 0, "the A position is now free");
    iPadControlsReset(IPAD_BIND_PAD, rowA);
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), XPAD_BUTTON_X, "reset puts A back");
    Check(!iPadControlsSet(IPAD_BIND_PAD, rowA, "q", err, sizeof(err)) && err[0] != '\0',
          "a keyboard key is not a pad input, and says why");
    CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), XPAD_BUTTON_X, "a refused binding changes nothing");
    S32 whoRows[8];
    CheckInt(iPadControlsWhoUses(IPAD_BIND_PAD, "a", whoRows, 8), 1, "the A position has one row");

    // Trigger threshold as a setting.
    Check(iPadControlsSetSetting("trigger_threshold", "50", err, sizeof(err)), "threshold 50");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.4f);
    CheckBits(Frame().on, 0, "threshold 50: 40% does not click");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.6f);
    CheckBits(Frame().on, XPAD_BUTTON_L1, "threshold 50: 60% clicks");
    Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.0f);
    Check(iPadControlsSetSetting("trigger_threshold", "auto", err, sizeof(err)), "threshold auto");
    Check(!iPadControlsSetSetting("layout", "sideways", err, sizeof(err)),
          "a layout that does not exist is refused");
    Check(iPadControlsSetSetting("layout", "gamecube", err, sizeof(err)) &&
              iPadLayoutButton(XPAD_BUTTON_TRIANGLE) == XPAD_BUTTON_TRIANGLE,
          "layout = gamecube applies at once");
    Check(iPadControlsSetSetting("layout", "auto", err, sizeof(err)) &&
              iPadLayoutButton(XPAD_BUTTON_TRIANGLE) == XPAD_BUTTON_O,
          "layout = auto is xbox again with an Xbox pad");

    // The prompts' glyph choices.
    CheckStr(iPadPromptsLabelFor("xbox", "a"), "xbox:A", "xbox icons keep the disc's A");
    CheckStr(iPadPromptsLabelFor("playstation", "b"), "Circle", "PlayStation icons: B is circle");
    CheckStr(iPadPromptsLabelFor("keyboard", "a"), "SPC", "keyboard icons: A is Space");
    CheckStr(iPadPromptsLabelFor("keyboard", "x"), "LMB", "keyboard icons: X is the left button");

    // Hot-plug: pull it out, and port 0 is the keyboard's.
    Detach(pad);
    Frame();
    Frame();
    Check(iPadHostGet(0)->connected, "port 0 stays connected with the keyboard");
    CheckStr(iPadHostDeviceName(0), "keyboard", "port 0 is the keyboard after unplugging");
    CheckStr(iPadHostPadKind(), NULL, "no pad kind with no pad");
}

static void TestKeyboard()
{
    KeysUp();
    Frame();

    sKeys[SDL_SCANCODE_SPACE] = true;
    CheckBits(Frame().on, XPAD_BUTTON_X, "Space = A");
    KeysUp();
    sKeys[KEY_MOUSE_LEFT] = true;
    CheckBits(Frame().on, XPAD_BUTTON_O, "left mouse = X (attack)");
    KeysUp();
    sKeys[KEY_MOUSE_RIGHT] = true;
    CheckBits(Frame().on, XPAD_BUTTON_TRIANGLE, "right mouse = B");
    KeysUp();
    sKeys[SDL_SCANCODE_Q] = true;
    CheckBits(Frame().on, XPAD_BUTTON_SQUARE, "Q = Y (Bash)");
    KeysUp();
    sKeys[SDL_SCANCODE_E] = true;
    CheckBits(Frame().on, XPAD_BUTTON_R1, "E = RT (talk, throw)");
    KeysUp();
    sKeys[SDL_SCANCODE_R] = true;
    CheckBits(Frame().on, XPAD_BUTTON_L1, "R = LT (camera)");
    KeysUp();
    sKeys[SDL_SCANCODE_TAB] = true;
    CheckBits(Frame().on, XPAD_BUTTON_SELECT | XPAD_BUTTON_Z, "Tab = Back (upgrades)");
    KeysUp();
    sKeys[SDL_SCANCODE_ESCAPE] = true;
    CheckBits(Frame().on, XPAD_BUTTON_START, "Esc = Start");
    KeysUp();
    sKeys[SDL_SCANCODE_UP] = true;
    CheckBits(Frame().on, XPAD_BUTTON_UP, "arrow up = d-pad up");
    KeysUp();

    sKeys[SDL_SCANCODE_W] = true;
    CheckInt(Frame().analog1.y, -127, "W pushes the stick fully up");
    sKeys[SDL_SCANCODE_LCTRL] = true;
    {
        S32 y = Frame().analog1.y;
        Check(y < -32 && y > -110, "W + Ctrl walks: between AnalogMin and AnalogMax");
    }
    KeysUp();
    sKeys[SDL_SCANCODE_D] = true;
    sKeys[SDL_SCANCODE_S] = true;
    {
        _tagxPad& p = Frame();
        Check(p.analog1.x == 127 && p.analog1.y == 127, "D + S is a full diagonal");
    }
    KeysUp();
    sKeys[SDL_SCANCODE_L] = true;
    CheckInt(Frame().analog2.x, 127, "L turns the camera like the C-stick pushed right");
    KeysUp();

    // The mouse: a distance becomes deflection past the game's 32.
    sKeyboard.mouse_dx = 10.0f;
    {
        S32 x = Frame().analog2.x;
        Check(x < -32 && x >= -110, "mouse right turns the view right (C-stick left)");
    }
    CheckInt(Frame().analog2.x, 0, "and stops when the mouse does");
    sKeyboard.mouse_dy = 10.0f;
    Check(Frame().analog2.y < -32, "mouse down looks down (the camera rises)");
    Frame();

    // Rebinding the keyboard, and more than one key on a row.
    char err[128];
    S32 rowA = iPadControlsFindRow("a");
    Check(iPadControlsAdd(IPAD_BIND_KEYBOARD, rowA, "k", err, sizeof(err)), "add K to A");
    sKeys[SDL_SCANCODE_K] = true;
    CheckBits(Frame().on, XPAD_BUTTON_X, "K jumps too");
    KeysUp();
    sKeys[SDL_SCANCODE_SPACE] = true;
    CheckBits(Frame().on, XPAD_BUTTON_X, "and Space still does");
    KeysUp();
    Check(!iPadControlsSet(IPAD_BIND_KEYBOARD, rowA, "nosuchkey", err, sizeof(err)) &&
              err[0] != '\0',
          "an unknown key is refused with a reason");
    iPadControlsReset(IPAD_BIND_KEYBOARD, rowA);
    Frame();
}

static void TestOtherPads()
{
    // Switch Pro: found by the letter printed on it. Nintendo prints A east.
    {
        Pad pad = Attach(0x057e, 0x2009, "Switch Pro Controller (virtual)");
        Frame();
        Frame();
        CheckStr(iPadHostPadKind(), "switch", "a Switch Pro pad is the switch kind");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_EAST), XPAD_BUTTON_X, "Switch A (east) = A");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), XPAD_BUTTON_TRIANGLE, "Switch B (south) = B");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_NORTH), XPAD_BUTTON_O, "Switch X (north) = X");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_WEST), XPAD_BUTTON_SQUARE, "Switch Y (west) = Y");
        CheckStr(iPadPromptsLabelFor("switch", "a"), "A", "Switch icons: A is A");
        Detach(pad);
        Frame();
        Frame();
    }

    // DualSense: the shapes, by position.
    {
        Pad pad = Attach(0x054c, 0x0ce6, "DualSense (virtual)");
        Frame();
        Frame();
        CheckStr(iPadHostPadKind(), "playstation", "a DualSense is the playstation kind");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), XPAD_BUTTON_X, "cross = A");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_EAST), XPAD_BUTTON_TRIANGLE, "circle = B");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_WEST), XPAD_BUTTON_O, "square = X");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_NORTH), XPAD_BUTTON_SQUARE, "triangle = Y");
        CheckInt(iPadLayoutActive(), IPAD_LAYOUT_XBOX, "layout auto is xbox on a DualSense");
        Detach(pad);
        Frame();
        Frame();
    }

    // GameCube adapter: Z modifies, and the move layout follows the console.
    {
        Pad pad = Attach(0x057e, 0x0337, "GameCube adapter (virtual)");
        Frame();
        Frame();
        CheckStr(iPadHostPadKind(), "gamecube", "a GameCube adapter is the gamecube kind");
        CheckInt(iPadLayoutActive(), IPAD_LAYOUT_GAMECUBE, "layout auto -> gamecube");
        CheckBits(iPadLayoutButton(XPAD_BUTTON_TRIANGLE), XPAD_BUTTON_TRIANGLE,
                  "gamecube layout: spin on B, as gc/iPad.cpp");
        // SDL's GameCube face style: A south, X east, B west, Y north.
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_SOUTH), XPAD_BUTTON_X, "GC A = A");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_WEST), XPAD_BUTTON_TRIANGLE, "GC B = B");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_EAST), XPAD_BUTTON_O, "GC X = X");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_NORTH), XPAD_BUTTON_SQUARE, "GC Y = Y");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), XPAD_BUTTON_SELECT | XPAD_BUTTON_Z,
                  "GC Z alone = Back (upgrade window)");

        Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
        CheckBits(Frame().on, XPAD_BUTTON_R1, "GC R alone = RT");
        Button(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
        CheckBits(Frame().on, XPAD_BUTTON_R2 | XPAD_BUTTON_Z | XPAD_BUTTON_SELECT,
                  "GC Z+R = black (R2+Z), not RT");
        Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f);
        Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f);
        CheckBits(Frame().on, XPAD_BUTTON_L2 | XPAD_BUTTON_Z | XPAD_BUTTON_SELECT,
                  "GC Z+L = white (L2), not LT");
        Button(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
        CheckBits(Frame().on, XPAD_BUTTON_L1, "GC L alone = LT");
        Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.0f);
        Frame();

        CheckStr(iPadPromptsLabelFor("gamecube", "white"), "Z+L", "GC icons: white is Z+L");
        Detach(pad);
        Frame();
        Frame();
    }
}

static void TestMenus()
{
    // In a menu xPadUpdate turns the left stick into d-pad presses with a
    // repeat; the keyboard's WASD gets that too.
    sMenuRunning = 1;
    Frame();
    sKeys[SDL_SCANCODE_S] = true;
    CheckBits(Frame().pressed & XPAD_BUTTON_DOWN, XPAD_BUTTON_DOWN,
              "menu: the stick held down presses d-pad down");
    KeysUp();
    Frame();
    sMenuRunning = 0;
    Frame();
}

// ---- the touch screen -------------------------------------------------------------

// The window the touch controls lay out in: a 16:9 phone held sideways.
static const F32 kTouchW = 1920.0f;
static const F32 kTouchH = 1080.0f;

// One touch event, as SDL posts a touch screen's. Positions are fractions of
// the window. The touch layer takes them from its watch, so the queue itself is
// emptied after each frame.
static void PostFinger(Uint32 type, SDL_FingerID id, F32 x, F32 y, SDL_TouchID touch = 1)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.tfinger.touchID = touch;
    e.tfinger.fingerID = id;
    e.tfinger.x = x;
    e.tfinger.y = y;
    e.tfinger.pressure = 1.0f;
    SDL_PushEvent(&e);
}

static void FingerDown(SDL_FingerID id, F32 x, F32 y)
{
    PostFinger(SDL_EVENT_FINGER_DOWN, id, x, y);
}

static void FingerMove(SDL_FingerID id, F32 x, F32 y)
{
    PostFinger(SDL_EVENT_FINGER_MOTION, id, x, y);
}

static void FingerUp(SDL_FingerID id, F32 x, F32 y)
{
    PostFinger(SDL_EVENT_FINGER_UP, id, x, y);
}

// A frame with the touch events it read taken off SDL's queue, which nothing
// else in this test drains.
static _tagxPad& TouchFrame()
{
    _tagxPad& p = Frame();
    SDL_FlushEvents(SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_CANCELED);
    return p;
}

struct Point
{
    F32 x;
    F32 y;
};

static Point Control(const char* name)
{
    Point p = { -1.0f, -1.0f };
    Check(iPadTouchControlAt(name, &p.x, &p.y), name);
    return p;
}

// Pixels to window fractions.
static F32 PX(F32 pixels)
{
    return pixels / kTouchW;
}

static F32 PY(F32 pixels)
{
    return pixels / kTouchH;
}

// SDL's stand-in mouse for a touch screen's first finger is not a mouse.
static void TestTouchMouse()
{
    F32 dx;
    F32 dy;
    U32 buttons;
    iPadKeyboardTakeMouse(&dx, &dy, &buttons);

    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.which = SDL_TOUCH_MOUSEID;
    e.motion.xrel = 50.0f;
    e.motion.yrel = -20.0f;
    SDL_PushEvent(&e);
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.which = SDL_TOUCH_MOUSEID;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = true;
    SDL_PushEvent(&e);
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.which = SDL_PEN_MOUSEID;
    e.motion.xrel = 9.0f;
    SDL_PushEvent(&e);

    iPadKeyboardTakeMouse(&dx, &dy, &buttons);
    Check(dx == 0.0f && dy == 0.0f, "a finger's stand-in mouse does not move the mouse camera");
    CheckInt((S32)buttons, 0, "a finger's stand-in mouse does not hold the left button");

    // A real mouse beside it still counts.
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.which = 1;
    e.motion.xrel = 7.0f;
    e.motion.yrel = 3.0f;
    SDL_PushEvent(&e);
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.which = 1;
    e.button.button = SDL_BUTTON_RIGHT;
    e.button.down = true;
    SDL_PushEvent(&e);

    iPadKeyboardTakeMouse(&dx, &dy, &buttons);
    Check(fabsf(dx - 7.0f) < 0.01f && fabsf(dy - 3.0f) < 0.01f, "a real mouse's motion counts");
    CheckInt((S32)buttons, (S32)SDL_BUTTON_RMASK, "a real mouse's button counts, the finger's not");
    iPadKeyboardTakeMouse(&dx, &dy, &buttons);
    Check(dx == 0.0f && dy == 0.0f, "motion is taken once");

    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.which = 1;
    e.button.button = SDL_BUTTON_RIGHT;
    SDL_PushEvent(&e);
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.which = SDL_TOUCH_MOUSEID;
    e.button.button = SDL_BUTTON_LEFT;
    SDL_PushEvent(&e);
    iPadKeyboardTakeMouse(&dx, &dy, &buttons);
    CheckInt((S32)buttons, 0, "and lets go");

    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_REMOVED);
}

static void TestTouch()
{
    KeysUp();
    iConfigSet("input.touch_controls", "on");
    iPadTouchInit();
    iTouchOverlaySetWindowSize((S32)kTouchW, (S32)kTouchH);
    iPadTouchTestInsets(0.0f, 0.0f, 0.0f, 0.0f);
    TouchFrame();
    Check(iPadTouchEnabled() && iPadTouchShowing(), "touch controls on and showing");

    // ---- the layout
    const Point ls = Control("left_stick");
    const Point rs = Control("right_stick");
    const char* const faces[4] = { "a", "b", "x", "y" };
    Check(ls.x < 0.5f && rs.x > 0.5f, "the left stick on the left half, the right on the right");
    Check(rs.y > 0.6f, "the right stick at the bottom");
    for (S32 i = 0; i < 4; i++)
    {
        const Point f = Control(faces[i]);
        Check(f.x > rs.x, "A B X Y right of the right stick");
        // The ring (0.13 of the height) and a face button (0.07) apart.
        const F32 dx = (f.x - rs.x) * kTouchW;
        const F32 dy = (f.y - rs.y) * kTouchH;
        Check(sqrtf(dx * dx + dy * dy) > (0.13f + 0.07f) * kTouchH,
              "the right stick's ring does not reach a face button");
    }

    // ---- the left stick moves, and only moves
    FingerDown(1, ls.x, ls.y);
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog1.x == 0 && p.analog1.y == 0, "left stick: centred where the thumb lands");
        CheckBits(p.on, 0, "left stick: no button");
    }
    FingerMove(1, ls.x + PX(200.0f), ls.y);
    {
        _tagxPad& p = TouchFrame();
        CheckInt(p.analog1.x, 127, "left stick pushed right walks right");
        Check(p.analog2.x == 0 && p.analog2.y == 0, "the left stick never turns the camera");
        CheckBits(p.on, 0, "the left stick presses nothing (no stand-in mouse button)");
    }
    FingerMove(1, ls.x, ls.y - PY(200.0f));
    {
        _tagxPad& p = TouchFrame();
        CheckInt(p.analog1.y, -127, "left stick pushed up -> the game's up");
        Check(p.analog2.x == 0 && p.analog2.y == 0, "still no camera");
    }
    {
        const iPadHostState* host = iPadHostGet(0);
        Check((host->sources & IPAD_HOST_SOURCE_TOUCH) != 0 &&
                  (host->sources & (IPAD_HOST_SOURCE_KEYBOARD | IPAD_HOST_SOURCE_MOUSE)) == 0,
              "a finger on the stick is touch, not keyboard or mouse");
        CheckStr(iPadPromptsActiveSet(), "touch", "prompts: the on-screen buttons while touching");
    }

    // ---- the right stick, alongside: the controller's right stick
    FingerDown(2, rs.x, rs.y);
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog2.x == 0 && p.analog2.y == 0, "right stick: centred where the thumb lands");
        CheckInt(p.analog1.y, -127, "the left stick keeps its finger");
    }
    FingerMove(2, rs.x + PX(200.0f), rs.y);
    {
        _tagxPad& p = TouchFrame();
        CheckInt(p.analog2.x, 127, "right stick pushed right -> analog2 right, as a pad's");
        CheckInt(p.analog1.y, -127, "and the left stick is still its own");
    }
    FingerMove(2, rs.x, rs.y + PY(200.0f));
    CheckInt(TouchFrame().analog2.y, 127, "right stick pushed down -> analog2.y 127, as a pad's");
    FingerUp(1, ls.x, ls.y - PY(200.0f));
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog1.x == 0 && p.analog1.y == 0, "left finger up: the stick lets go");
        CheckInt(p.analog2.y, 127, "the right stick does not");
    }
    FingerUp(2, rs.x, rs.y + PY(200.0f));
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog2.x == 0 && p.analog2.y == 0, "right finger up: the camera stops");
    }

    // ---- a finger stays with what it landed on
    FingerDown(3, ls.x, ls.y);
    TouchFrame();
    FingerMove(3, rs.x, rs.y);
    {
        _tagxPad& p = TouchFrame();
        CheckInt(p.analog1.x, 127, "a left-stick finger dragged to the right stick still walks");
        Check(p.analog2.x == 0 && p.analog2.y == 0, "and is not handed the camera");
    }
    const Point a = Control("a");
    const Point b = Control("b");
    FingerMove(3, a.x, a.y);
    CheckBits(TouchFrame().on, 0, "nor A, dragged over it");
    FingerUp(3, a.x, a.y);
    TouchFrame();

    FingerDown(4, a.x, a.y);
    {
        _tagxPad& p = TouchFrame();
        CheckBits(p.on, XPAD_BUTTON_X, "A jumps");
        CheckBits(p.pressed, XPAD_BUTTON_X, "pressed the frame it lands");
    }
    FingerMove(4, a.x + PX(10.0f), a.y - PY(10.0f));
    CheckBits(TouchFrame().on, XPAD_BUTTON_X, "a thumb wobbling on A keeps it held");
    FingerMove(4, b.x, b.y);
    CheckBits(TouchFrame().on, 0, "rolled onto B: A lets go, and B is not pressed");
    FingerMove(4, a.x, a.y);
    CheckBits(TouchFrame().on, XPAD_BUTTON_X, "back on A: A again");
    FingerMove(4, 0.5f, 0.5f);
    {
        _tagxPad& p = TouchFrame();
        CheckBits(p.on, 0, "dragged off into the screen: nothing");
        Check(p.analog1.x == 0 && p.analog2.x == 0, "and no stick either");
    }
    FingerUp(4, 0.5f, 0.5f);
    TouchFrame();

    // ---- a second finger on the left half does not take the stick
    FingerDown(5, ls.x, ls.y);
    TouchFrame();
    FingerMove(5, ls.x + PX(200.0f), ls.y);
    TouchFrame();
    FingerDown(6, 0.3f, 0.5f);
    TouchFrame();
    FingerMove(6, 0.05f, 0.5f);
    CheckInt(TouchFrame().analog1.x, 127,
             "a second finger on the left half does not take the stick");
    FingerUp(5, ls.x + PX(200.0f), ls.y);
    {
        _tagxPad& p = TouchFrame();
        CheckInt(p.analog1.x, 0, "the stick's finger lifts: the stick lets go, not passed on");
    }
    FingerMove(6, 0.2f, 0.4f);
    CheckInt(TouchFrame().analog1.x, 0, "the other finger still does nothing");
    FingerUp(6, 0.2f, 0.4f);
    TouchFrame();

    // ---- empty screen does nothing, unless drag look is on
    const F32 emptyX = 0.6f;
    const F32 emptyY = 0.3f;
    FingerDown(7, emptyX, emptyY);
    TouchFrame();
    FingerMove(7, emptyX + PX(150.0f), emptyY + PY(80.0f));
    {
        _tagxPad& p = TouchFrame();
        CheckBits(p.on, 0, "a finger on empty screen presses nothing");
        Check(p.analog1.x == 0 && p.analog1.y == 0 && p.analog2.x == 0 && p.analog2.y == 0,
              "a finger on empty screen moves nothing");
    }
    FingerUp(7, emptyX + PX(150.0f), emptyY + PY(80.0f));
    TouchFrame();

    char err[128];
    Check(iPadControlsSetSetting("touch_drag_look", "on", err, sizeof(err)), "touch_drag_look on");
    FingerDown(8, emptyX, emptyY);
    TouchFrame();
    FingerMove(8, emptyX + PX(150.0f), emptyY);
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog2.x < -32, "drag look: a drag right on the right half turns the view right");
        CheckInt(p.analog1.x, 0, "and does not walk");
    }
    FingerUp(8, emptyX + PX(150.0f), emptyY);
    // The drag is smoothed over a tenth of a second or so, in real time.
    for (S32 i = 0; i < 10; i++)
    {
        SDL_Delay(100);
        TouchFrame();
    }
    CheckInt(TouchFrame().analog2.x, 0, "drag look: the camera stops once the drag is spent");
    FingerDown(9, ls.x, ls.y);
    TouchFrame();
    FingerMove(9, ls.x + PX(200.0f), ls.y);
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog2.x == 0 && p.analog2.y == 0, "drag look: the left stick still never looks");
    }
    FingerUp(9, ls.x + PX(200.0f), ls.y);
    TouchFrame();
    Check(iPadControlsSetSetting("touch_drag_look", "off", err, sizeof(err)),
          "touch_drag_look off");

    // ---- each button its row
    struct Row
    {
        const char* row;
        U32 bits;
    };
    static const Row kRows[] = {
        { "a", XPAD_BUTTON_X },
        { "b", XPAD_BUTTON_TRIANGLE },
        { "x", XPAD_BUTTON_O },
        { "y", XPAD_BUTTON_SQUARE },
        { "lt", XPAD_BUTTON_L1 },
        { "rt", XPAD_BUTTON_R1 },
        { "back", XPAD_BUTTON_SELECT | XPAD_BUTTON_Z },
        { "black", XPAD_BUTTON_R2 | XPAD_BUTTON_Z },
        { "start", XPAD_BUTTON_START },
    };
    for (U32 i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++)
    {
        const Point c = Control(kRows[i].row);
        FingerDown(10, c.x, c.y);
        char what[64];
        snprintf(what, sizeof(what), "touch %s", kRows[i].row);
        CheckBits(TouchFrame().on, kRows[i].bits, what);
        FingerUp(10, c.x, c.y);
        TouchFrame();
    }

    // ---- a tap shorter than a frame still presses for one
    FingerDown(11, a.x, a.y);
    FingerUp(11, a.x, a.y);
    {
        _tagxPad& p = TouchFrame();
        CheckBits(p.pressed, XPAD_BUTTON_X, "a tap inside one frame presses A");
    }
    CheckBits(TouchFrame().released, XPAD_BUTTON_X, "and lets go the next");

    // ---- one finger id on two devices is two fingers; a mouse is not one
    FingerDown(1, ls.x, ls.y);
    PostFinger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y, SDL_PEN_TOUCHID);
    TouchFrame();
    FingerMove(1, ls.x + PX(200.0f), ls.y);
    {
        _tagxPad& p = TouchFrame();
        CheckInt(p.analog1.x, 127, "finger 1 on the screen walks");
        CheckBits(p.on, XPAD_BUTTON_X, "while the pen's 1 holds A");
    }
    PostFinger(SDL_EVENT_FINGER_UP, 1, a.x, a.y, SDL_PEN_TOUCHID);
    {
        _tagxPad& p = TouchFrame();
        CheckBits(p.on, 0, "the pen lifts: A lets go");
        CheckInt(p.analog1.x, 127, "the finger still walks");
    }
    FingerUp(1, ls.x + PX(200.0f), ls.y);
    TouchFrame();
    PostFinger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y, SDL_MOUSE_TOUCHID);
    CheckBits(TouchFrame().on, 0, "a mouse's stand-in finger does not press A");
    PostFinger(SDL_EVENT_FINGER_UP, 1, a.x, a.y, SDL_MOUSE_TOUCHID);
    TouchFrame();

    // ---- the safe area moves the controls in
    iPadTouchTestInsets(100.0f, 150.0f, 0.0f, 40.0f);
    {
        const Point ls2 = Control("left_stick");
        const Point rs2 = Control("right_stick");
        const Point a2 = Control("a");
        Check(fabsf((ls2.x - ls.x) * kTouchW - 100.0f) < 0.5f, "a left inset moves the left stick");
        Check(fabsf((rs.x - rs2.x) * kTouchW - 150.0f) < 0.5f,
              "a right inset moves the right stick");
        Check(fabsf((a.y - a2.y) * kTouchH - 40.0f) < 0.5f, "a bottom inset raises the buttons");
    }
    iPadTouchTestInsets(0.0f, 0.0f, 0.0f, 0.0f);

    // ---- the prompts
    CheckStr(iPadPromptsLabelFor("touch", "a"), "xbox:A", "touch icons: A is the disc's Xbox A");
    CheckStr(iPadPromptsLabelFor("touch", "y"), "xbox:Y", "touch icons: Y is the disc's Xbox Y");
    CheckStr(iPadPromptsLabelFor("touch", "lt"), "L", "touch icons: LT is the on-screen L");
    CheckStr(iPadPromptsLabelFor("touch", "back"), "=", "touch icons: Back is the on-screen =");
    CheckStr(iPadPromptsLabelFor("touch", "start"), ">", "touch icons: Start is the on-screen >");
    CheckStr(iPadPromptsLabelFor("touch", "white"), "-", "touch icons: no white button on screen");
    sKeys[SDL_SCANCODE_SPACE] = true;
    TouchFrame();
    KeysUp();
    TouchFrame();
    CheckStr(iPadPromptsActiveSet(), "keyboard", "prompts: keys after a key");
    FingerDown(12, a.x, a.y);
    TouchFrame();
    FingerUp(12, a.x, a.y);
    TouchFrame();
    CheckStr(iPadPromptsActiveSet(), "touch", "prompts: the on-screen buttons after a touch");

    // ---- a controller in use puts them away; a touch brings them back
    {
        Pad pad = Attach(0x045e, 0x0b12, "Xbox Series X Controller (virtual)");
        TouchFrame();
        TouchFrame();
        Button(pad, SDL_GAMEPAD_BUTTON_SOUTH, true);
        TouchFrame();
        Button(pad, SDL_GAMEPAD_BUTTON_SOUTH, false);
        TouchFrame();
        Check(!iPadTouchShowing(), "a controller in use hides the touch controls");
        CheckStr(iPadPromptsActiveSet(), "xbox", "prompts: the pad's after the pad");
        FingerDown(13, rs.x, rs.y);
        TouchFrame();
        Check(iPadTouchShowing(), "a touch brings them back");
        FingerUp(13, rs.x, rs.y);
        TouchFrame();
        Detach(pad);
        TouchFrame();
        TouchFrame();
    }

    iConfigSet("input.touch_controls", "off");
    iPadTouchInit();
    TouchFrame();
}

int main(int argc, char** argv)
{
    char dir[512];
    if (argc > 1)
    {
        snprintf(dir, sizeof(dir), "%s", argv[1]);
    }
    else
    {
        const char* tmp = getenv("TEMP");
        snprintf(dir, sizeof(dir), "%s/tssm_input_test", tmp != NULL ? tmp : ".");
    }
    SDL_CreateDirectory(dir);

    char config[600];
    snprintf(config, sizeof(config), "%s/config.ini", dir);
    SDL_RemovePath(config);

    // The C library's environment, which is what iConfig and iPadSettings read
    // with getenv (SDL_setenv does not reach it on Windows).
#ifdef _WIN32
    _putenv_s("TSSM_CONFIG", config);
    _putenv_s("TSSM_INPUT_TEST", "1");
#else
    setenv("TSSM_CONFIG", config, 1);
    setenv("TSSM_INPUT_TEST", "1", 1);
#endif
    printf("tssm_input_test: config %s\n", config);

    iPadKeyboardSetTestSource(&sKeyboard);

    xPadInit();
    xPadEnable(0);

    TestTables();
    TestXboxPad();
    TestKeyboard();
    TestOtherPads();
    TestMenus();
    TestTouchMouse();
    TestTouch();

    xPadKill();

    printf("tssm_input_test: %d checks, %d failed\n", (int)sChecks, (int)sFailures);
    return sFailures == 0 ? 0 : 1;
}
