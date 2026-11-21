// tssm_input_test: the input layer, end to end, with no game data, no screen
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
#include "iCameraLook.h"
#include "iMouseLook.h"
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

#include <rwcore.h>

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

// The menu pointer is a game unit (it reads the game's UI widgets), and there
// is no game here: a menu is up when the test says so, and takes no clicks.
static bool sMenuUp;

U32 iMenuMouseUpdate(S32, U32 on)
{
    iMouseLookSetPointer(sMenuUp);
    return iMouseLookSpentOut(on);
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

// Nor does any package: the prompts ask which release the font came from, and
// the pad layer which release the menus came from (TestBackForTheMenus), by
// the package that holds the menu that opens the upgrade window. That one is
// the test's: a few bytes whose PLAT chunk says which release.
static const char* sMenusPackage;

char* xST_xAssetID_HIPFullPath(U32 id)
{
    if (sMenusPackage != NULL && id == xStrHash("__PAUSE_STATE_PAUSE MGR"))
    {
        return (char*)sMenusPackage;
    }
    return NULL;
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

    // The disc's raster of a texture (zSpotlight's flares ask for TT01's pad_button1): one that
    // nothing has swapped keeps its own. No package is loaded, so nothing is swapped here.
    {
        RwTexture tex;
        memset(&tex, 0, sizeof(tex));
        tex.raster = (RwRaster*)&tex;
        Check(iPadPromptsDiscRaster(&tex) == tex.raster, "an unswapped texture keeps its raster");
        Check(iPadPromptsDiscRaster(NULL) == NULL, "no texture, no raster");
    }

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

    // The mouse. With no on-foot camera asking for the turn (there is no game
    // here; a car's or a boss's camera has the view), a distance becomes
    // deflection past the game's 32.
    sKeyboard.mouse_dx = 10.0f;
    {
        S32 x = Frame().analog2.x;
        Check(x < -32 && x >= -110, "mouse right turns the view right (C-stick left)");
    }
    Check(iMouseLookSteering(), "the mouse has the camera once it has moved");
    CheckInt(Frame().analog2.x, 0, "and stops when the mouse does");
    sKeyboard.mouse_dy = 10.0f;
    Check(Frame().analog2.y < -32, "mouse down looks down (the camera rises)");
    Frame();

    // The on-foot camera takes the turn itself (iCameraOrbit.cpp calls
    // iCameraLookTake every frame it runs): the mouse then turns it directly,
    // in radians, and the right stick's value is left alone.
    {
        F32 yaw = 0.0f;
        F32 pitch = 0.0f;
        iCameraLookTake(&yaw, &pitch);
        sKeyboard.mouse_dx = 100.0f;
        sKeyboard.mouse_dy = -40.0f;
        _tagxPad& p = Frame();
        Check(p.analog2.x == 0 && p.analog2.y == 0, "mouse look does not go through the stick");
        Check(iCameraLookTake(&yaw, &pitch) != 0, "the mouse is steering");
        Check(fabsf(yaw - 0.35f) < 0.0001f, "100 counts right turn the view 0.35 rad right");
        Check(fabsf(pitch + 0.14f) < 0.0001f, "40 counts forward look 0.14 rad up");
        Frame();
        iCameraLookTake(&yaw, &pitch);
        Check(yaw == 0.0f && pitch == 0.0f, "and the camera stays where it was pointed");

        // Each axis has its speed and its direction.
        iPadSettings& cfg = iPadSettingsEdit();
        cfg.mouse_sensitivity_x = 2.0f;
        cfg.mouse_sensitivity_y = 0.5f;
        cfg.mouse_invert_x = 1;
        cfg.mouse_invert_y = 1;
        sKeyboard.mouse_dx = 100.0f;
        sKeyboard.mouse_dy = -40.0f;
        Frame();
        iCameraLookTake(&yaw, &pitch);
        Check(fabsf(yaw + 0.70f) < 0.0001f, "sensitivity x 2, inverted: 0.70 rad left");
        Check(fabsf(pitch - 0.07f) < 0.0001f, "sensitivity y 0.5, inverted: 0.07 rad down");
        cfg.mouse_sensitivity_x = 1.0f;
        cfg.mouse_sensitivity_y = 1.0f;
        cfg.mouse_invert_x = 0;
        cfg.mouse_invert_y = 0;

        // The camera keys (the right stick) take the camera back, with the
        // game's own camera, until the mouse moves again.
        sKeys[SDL_SCANCODE_L] = true;
        sKeyboard.mouse_dx = 50.0f;
        CheckInt(Frame().analog2.x, 127, "the camera keys win over the mouse");
        Check(!iMouseLookSteering() && iCameraLookTake(&yaw, &pitch) == 0 && yaw == 0.0f,
              "and the mouse stops steering");
        KeysUp();
        Frame();
        iCameraLookTake(&yaw, &pitch);
        Check(!iMouseLookSteering(), "it stays the stick's with the keys let go");
        sKeyboard.mouse_dx = 20.0f;
        Frame();
        Check(iCameraLookTake(&yaw, &pitch) != 0 && fabsf(yaw - 0.07f) < 0.0001f,
              "and the mouse takes it back when it moves");

        // Mouse look off: nothing from the mouse, on either path.
        cfg.mouse_look = 0;
        sKeyboard.mouse_dx = 50.0f;
        CheckInt(Frame().analog2.x, 0, "mouse look off: no stick deflection");
        Check(iCameraLookTake(&yaw, &pitch) == 0 && yaw == 0.0f, "mouse look off: no turn");
        cfg.mouse_look = 1;
        Frame();
        Frame();
    }

    // A click a menu took comes out of the button the mouse button is bound
    // to (X), but not while a key is holding that button as well.
    sKeys[KEY_MOUSE_LEFT] = true;
    Frame();
    Check(iMouseLookBound(1) == XPAD_BUTTON_O, "a taken left click takes X with it");
    sKeys[SDL_SCANCODE_F] = true;
    Frame();
    Check(iMouseLookBound(1) == 0, "but not the X the F key is holding");
    KeysUp();
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

        // The GameCube release's font.HIP has GameCube pictures: the gamecube
        // set keeps them, and the xbox set draws its own.
        iPadPromptsSetGameCubeFont(true);
        CheckStr(iPadPromptsLabelFor("gamecube", "b"), "gamecube:B",
                 "GC font: GC icons keep the disc's B (west)");
        CheckStr(iPadPromptsLabelFor("gamecube", "x"), "gamecube:X",
                 "GC font: GC icons keep the disc's X (east)");
        CheckStr(iPadPromptsLabelFor("gamecube", "white"), "Z+L", "GC font: white is still Z+L");
        CheckStr(iPadPromptsLabelFor("xbox", "b"), "X", "GC font: xbox icons are drawn");
        iPadPromptsSetGameCubeFont(false);
        CheckStr(iPadPromptsLabelFor("xbox", "b"), "xbox:X", "Xbox font: xbox icons keep the disc's");
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
    Check(!iCameraLookActive(), "drag look: a finger only resting does not have the camera");
    FingerMove(8, emptyX + PX(150.0f), emptyY);
    {
        _tagxPad& p = TouchFrame();
        Check(p.analog2.x < -32, "drag look: a drag right on the right half turns the view right");
        CheckInt(p.analog1.x, 0, "and does not walk");
        Check(iCameraLookActive() != 0, "drag look: the finger has the camera from its first drag");
        F32 yaw = 0.0f;
        F32 pitch = 0.0f;
        iCameraLookTake(&yaw, &pitch);
        Check(yaw > 0.0f && pitch == 0.0f, "drag look: a drag right is a turn to the right");
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
    Check(iCameraLookActive() != 0, "drag look: the camera stays the finger's after the drag");
    FingerDown(9, rs.x, rs.y);
    TouchFrame();
    FingerMove(9, rs.x + PX(200.0f), rs.y);
    TouchFrame();
    Check(!iCameraLookActive(), "drag look: the right stick gives the camera back to the game");
    FingerUp(9, rs.x + PX(200.0f), rs.y);
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

    // ---- a tap, for the game's menus (iMenuMouse.h)
    {
        F32 tx = -1.0f;
        F32 ty = -1.0f;
        FingerDown(20, emptyX, emptyY);
        FingerUp(20, emptyX, emptyY);
        TouchFrame();
        Check(iPadTouchTakeTap(&tx, &ty), "a tap on empty screen is a tap");
        Check(fabsf(tx - emptyX) < 0.001f && fabsf(ty - emptyY) < 0.001f,
              "at the spot it landed, as fractions of the window");
        Check(!iPadTouchTakeTap(&tx, &ty), "taken once");

        FingerDown(20, ls.x, ls.y);
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "not while the finger is still down");
        FingerUp(20, ls.x, ls.y);
        TouchFrame();
        Check(iPadTouchTakeTap(&tx, &ty), "a tap where the left stick takes the thumb is a tap");

        FingerDown(20, emptyX, emptyY);
        FingerUp(20, emptyX, emptyY);
        TouchFrame();
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "a tap nobody took is gone by the next poll");

        FingerDown(20, a.x, a.y);
        FingerUp(20, a.x, a.y);
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "a tap on A is A, not a tap");
        TouchFrame();

        FingerDown(20, emptyX, emptyY);
        TouchFrame();
        FingerMove(20, emptyX + PX(120.0f), emptyY);
        TouchFrame();
        FingerUp(20, emptyX, emptyY);
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "a finger that wandered and came back is not a tap");

        FingerDown(20, emptyX, emptyY);
        FingerDown(21, emptyX + PX(200.0f), emptyY);
        FingerUp(20, emptyX, emptyY);
        FingerUp(21, emptyX + PX(200.0f), emptyY);
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "two fingers at once are not a tap");

        FingerDown(20, emptyX, emptyY);
        TouchFrame();
        SDL_Delay(500);
        FingerUp(20, emptyX, emptyY);
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "a finger held half a second is not a tap");

        Check(iPadControlsSetSetting("touch_menus", "off", err, sizeof(err)), "touch_menus off");
        FingerDown(20, emptyX, emptyY);
        FingerUp(20, emptyX, emptyY);
        TouchFrame();
        Check(!iPadTouchTakeTap(&tx, &ty), "touch_menus off: no taps");
        Check(iPadControlsSetSetting("touch_menus", "on", err, sizeof(err)), "touch_menus on");
        TouchFrame();
    }

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

// What a real mouse does, as SDL posts it: 40 counts right, a left click and a
// notch of the wheel.
static void PostMouse()
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.which = 1;
    e.motion.xrel = 40.0f;
    SDL_PushEvent(&e);

    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.which = 1;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.down = false;
    SDL_PushEvent(&e);

    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.which = 1;
    e.wheel.y = 1.0f;
    SDL_PushEvent(&e);
}

static void PostButton(bool down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.which = 1;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = down;
    SDL_PushEvent(&e);
}

// The Escape key going down, as SDL posts it. (The keyboard's state is not
// changed by a posted event, so nothing it is bound to is pressed: Escape by
// itself, as with pause moved to another key.)
static void PostEscape()
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_ESCAPE;
    e.key.key = SDLK_ESCAPE;
    e.key.down = true;
    SDL_PushEvent(&e);
}

// Escape frees the pointer, and what takes it back. With the real mouse path
// on `window`, the pointer held for mouse look.
static void TestEscape(SDL_Window* window)
{
    iPadSettings& cfg = iPadSettingsEdit();

    // Whatever is on screen, whatever it is bound to.
    PostEscape();
    {
        _tagxPad& p = Frame();
        Check(!SDL_GetWindowRelativeMouseMode(window) && !iMouseLookHeld(),
              "Escape frees the pointer");
        CheckBits(p.on & XPAD_BUTTON_START, 0, "also where it is not the pause key");
    }
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.which = 1;
    e.motion.xrel = 25.0f;
    SDL_PushEvent(&e);
    Frame();
    Frame();
    Check(!SDL_GetWindowRelativeMouseMode(window), "and it stays free while the mouse moves");

    // A click in the window takes it back, and is not the game's.
    PostButton(true);
    {
        _tagxPad& p = Frame();
        Check(SDL_GetWindowRelativeMouseMode(window) && iMouseLookHeld(),
              "a click in the window takes the pointer back");
        CheckBits(p.on & XPAD_BUTTON_O, 0, "that click is not X (attack)");
        CheckBits(Frame().on & XPAD_BUTTON_O, 0, "nor while it is held");
    }
    PostButton(false);
    Frame();
    PostButton(true);
    CheckBits(Frame().on & XPAD_BUTTON_O, XPAD_BUTTON_O, "the next click is X again");
    PostButton(false);
    Frame();

    // Between polls (a loading screen): the window's pump lets go at once.
    PostEscape();
    iMouseLookPump();
    Check(!SDL_GetWindowRelativeMouseMode(window), "Escape frees it between polls too");
    Frame();
    Check(!iMouseLookHeld(), "and the next poll leaves it free");

    // The pause menu Escape opens by default: the pointer comes back when
    // play goes on after it.
    sMenuUp = true;
    Frame();
    Frame();
    Check(!SDL_GetWindowRelativeMouseMode(window), "free in the menu Escape opened");
    PostButton(true);
    Frame();
    Check(!SDL_GetWindowRelativeMouseMode(window), "a click in a menu does not take it back");
    PostButton(false);
    Frame();
    sMenuUp = false;
    Frame();
    Frame();
    Check(SDL_GetWindowRelativeMouseMode(window), "held again when play goes on after the menu");

    // Escape in a menu (it closes the pause menu) frees nothing afterwards.
    sMenuUp = true;
    Frame();
    Frame();
    PostEscape();
    Frame();
    sMenuUp = false;
    Frame();
    Frame();
    Check(SDL_GetWindowRelativeMouseMode(window), "Escape that closes a menu leaves it held after");

    // mouse_menus off: no pointer to click with, but the mouse is let go.
    cfg.mouse_menus = 0;
    sMenuUp = true;
    Frame();
    Frame();
    Check(!SDL_GetWindowRelativeMouseMode(window) && !iMouseLookHeld(),
          "mouse_menus off: a menu still frees the pointer");
    PostButton(true);
    CheckBits(Frame().on & XPAD_BUTTON_O, XPAD_BUTTON_O,
              "mouse_menus off: a click in a menu is the button it is bound to");
    PostButton(false);
    Frame();
    sMenuUp = false;
    Frame();
    Frame();
    Check(SDL_GetWindowRelativeMouseMode(window), "mouse_menus off: held again after the menu");
    cfg.mouse_menus = 1;

    // A click a menu took that closes the menu ("Return to Game") is not a
    // new press in play while the button is still down.
    sMenuUp = true;
    Frame();
    Frame();
    PostButton(true);
    Frame();
    iMouseLookSpend(1);
    sMenuUp = false;
    {
        _tagxPad& p = Frame();
        CheckBits((p.on | p.pressed) & XPAD_BUTTON_O, 0,
                  "the click that closed a menu is not X in play");
        _tagxPad& q = Frame();
        CheckBits((q.on | q.pressed) & XPAD_BUTTON_O, 0, "for as long as it is held");
    }
    PostButton(false);
    Frame();
    PostButton(true);
    CheckBits(Frame().pressed & XPAD_BUTTON_O, XPAD_BUTTON_O, "and the next click is X");
    PostButton(false);
    Frame();
    Check(SDL_GetWindowRelativeMouseMode(window), "the pointer is held at the end");
}

// Every default key of every button row, by itself, reaches the game as that
// row's buttons: the [keyboard] column of iPadBindTable.cpp, as a player with
// no [keyboard] section has it.
static void TestEveryDefaultKey()
{
    S32 count;
    const iPadBindToken* tokens = iPadKeyTokens(&count);
    KeysUp();
    Frame();

    for (S32 row = 0; row < kPadBindButtonCount; row++)
    {
        const iPadBindButton& b = kPadBindButtons[row];
        if (b.mask == 0)
        {
            continue;
        }

        char what[160];
        iPadBind bind;
        snprintf(what, sizeof(what), "the default keys of %s parse", b.name);
        Check(iPadBindParse(b.key, tokens, count, b.name, &bind) && bind.alts > 0, what);

        for (S32 a = 0; a < bind.alts; a++)
        {
            if (bind.length[a] != 1)
            {
                continue;
            }
            const S16 id = bind.id[a][0];
            sKeys[id] = true;
            snprintf(what, sizeof(what), "%s alone = %s", iPadBindTokenName(id, tokens, count),
                     b.name);
            CheckBits(Frame().on, b.mask, what);
            KeysUp();
            Frame();
        }
    }
}

// Back opens the upgrade window, which the Xbox's menus hear as PadPressSelect
// and the GameCube's as PadPressE, zUI::DoUpdate's event for Z. Back carries
// both bits, and DoUpdate raises only Select when both are there: under the
// GameCube's menus Back has to be the GameCube's Z alone (iPad.cpp). The menus'
// release is the PLAT chunk of the package xST_xAssetID_HIPFullPath names.
static bool WritePackage(const char* path, const char* plat, const char* name)
{
    // HIPA, then PACK holding PLAT: as far as iGCPackagePlatform reads.
    U8 b[64];
    S32 n = 0;
    const S32 platLen = (S32)(strlen(plat) + 1 + strlen(name) + 1);
    const U32 sizes[3] = { 0, (U32)(8 + platLen), (U32)platLen };
    const char* ids[3] = { "HIPA", "PACK", "PLAT" };
    for (S32 i = 0; i < 3; i++)
    {
        memcpy(b + n, ids[i], 4);
        b[n + 4] = (U8)(sizes[i] >> 24);
        b[n + 5] = (U8)(sizes[i] >> 16);
        b[n + 6] = (U8)(sizes[i] >> 8);
        b[n + 7] = (U8)sizes[i];
        n += 8;
    }
    memcpy(b + n, plat, strlen(plat) + 1);
    n += (S32)strlen(plat) + 1;
    memcpy(b + n, name, strlen(name) + 1);
    n += (S32)strlen(name) + 1;

    SDL_IOStream* f = SDL_IOFromFile(path, "wb");
    if (f == NULL)
    {
        return false;
    }
    const bool ok = SDL_WriteIO(f, b, (size_t)n) == (size_t)n;
    return SDL_CloseIO(f) && ok;
}

static void TestBackForTheMenus(const char* dir)
{
    char gamecube[600];
    char xbox[600];
    snprintf(gamecube, sizeof(gamecube), "%s/menus_gamecube.hip", dir);
    snprintf(xbox, sizeof(xbox), "%s/menus_xbox.hip", dir);
    Check(WritePackage(gamecube, "GC", "GameCube") && WritePackage(xbox, "BX", "Xbox"),
          "the stand-in menu packages are written");

    const U32 back = XPAD_BUTTON_SELECT | XPAD_BUTTON_Z;
    KeysUp();
    Frame();

    // No menus yet (the boot screens): what Back always was.
    sMenusPackage = NULL;
    sKeys[SDL_SCANCODE_TAB] = true;
    CheckBits(Frame().on, back, "no menus loaded: Tab = Select + Z");
    KeysUp();
    Frame();

    sMenusPackage = xbox;
    sKeys[SDL_SCANCODE_TAB] = true;
    CheckBits(Frame().pressed, back, "Xbox menus: Tab presses Select + Z (PadPressSelect)");
    KeysUp();
    Frame();

    sMenusPackage = gamecube;
    sKeys[SDL_SCANCODE_TAB] = true;
    {
        _tagxPad& p = Frame();
        CheckBits(p.on, XPAD_BUTTON_Z, "GameCube menus: Tab is the GameCube's Z (PadPressE)");
        CheckBits(p.pressed, XPAD_BUTTON_Z, "pressed as Z");
    }
    CheckBits(Frame().pressed, 0, "not pressed again while Tab is held");
    // The menus are asked once per press: menus that change while Back is held
    // (a level loading) leave the press as it began.
    sMenusPackage = xbox;
    CheckBits(Frame().on, XPAD_BUTTON_Z, "menus changed while Tab is held: still Z");
    sMenusPackage = gamecube;
    KeysUp();
    CheckBits(Frame().released, XPAD_BUTTON_Z, "and let go as Z");
    sKeys[SDL_SCANCODE_C] = true;
    CheckBits(Frame().on, XPAD_BUTTON_R2 | XPAD_BUTTON_Z, "GameCube menus: black (C) as before");
    KeysUp();
    sKeys[SDL_SCANCODE_ESCAPE] = true;
    CheckBits(Frame().on, XPAD_BUTTON_START, "GameCube menus: Escape is still Start");
    KeysUp();
    Frame();

    {
        Pad pad = Attach(0x045e, 0x0b12, "Xbox Series X Controller (virtual)");
        Frame();
        Frame();
        sKeys[SDL_SCANCODE_TAB] = true;
        CheckBits(Frame().on, XPAD_BUTTON_Z,
                  "GameCube menus: Tab with a controller connected but unused is Z");
        KeysUp();
        Frame();
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_BACK), XPAD_BUTTON_Z,
                  "GameCube menus: a controller's Back is Z");
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), XPAD_BUTTON_R2 | XPAD_BUTTON_Z,
                  "GameCube menus: a controller's black as before");
        sMenusPackage = xbox;
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_BACK), back,
                  "Xbox menus: a controller's Back is Select + Z");
        sKeys[SDL_SCANCODE_TAB] = true;
        CheckBits(Frame().on, back,
                  "Xbox menus: Tab with a controller connected but unused is Select + Z");
        KeysUp();
        Frame();
        sMenusPackage = gamecube;
        Detach(pad);
        Frame();
        Frame();
    }

    {
        Pad pad = Attach(0x057e, 0x0337, "GameCube adapter (virtual)");
        Frame();
        Frame();
        CheckBits(Tap(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), XPAD_BUTTON_Z,
                  "GameCube menus: the GameCube pad's Z is Z, as on the console");
        Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
        Button(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
        CheckBits(Frame().on, XPAD_BUTTON_R2 | XPAD_BUTTON_Z,
                  "GameCube menus: its Z+R is R2 + Z, as gc/iPad.cpp sets them");
        Release(pad);
        Frame();
        Detach(pad);
        Frame();
        Frame();
    }

    sMenusPackage = NULL;
}

// The launcher brings the input layer up and down before the game does
// (gui/iGuiLauncher.cpp). The real mouse path, so a window that has the
// keyboard: SDL's dummy video driver gives one without a screen.
static void TestLauncherMouse()
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_Window* window = NULL;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        window = SDL_CreateWindow("tssm_input_test", 320, 240, 0);
    }
    Check(window != NULL && SDL_GetKeyboardFocus() == window, "a window with the keyboard");
    if (window == NULL)
    {
        return;
    }

    iPadKeyboardSetTestSource(NULL);
    xPadKill();

    // The launcher's turn.
    iPadHostInit();
    PostMouse();
    iPadHostPollDevices();
    Check(!SDL_GetWindowRelativeMouseMode(window), "the launcher does not hold the pointer");
    iPadHostExit();

    // The game's. Its first read of the mouse is xPadEnable's.
    xPadInit();
    xPadEnable(0);

    F32 dx;
    F32 dy;
    U32 held;
    U32 pressed;
    S32 wheel;
    CheckBits(mPad[0].on, 0, "the click on the launcher presses nothing in the game");
    iPadKeyboardMouse(&dx, &dy, &held, &pressed, &wheel);
    Check(dx == 0.0f && dy == 0.0f, "the mouse's movement over the launcher is not the game's");
    Check(held == 0 && pressed == 0 && wheel == 0, "nor its click or its wheel");
    Check(!iMouseLookSteering(), "and mouse look has not started");

    PostMouse();
    CheckBits(Frame().on & XPAD_BUTTON_O, XPAD_BUTTON_O, "a click in the game is X (attack)");
    iPadKeyboardMouse(&dx, &dy, &held, &pressed, &wheel);
    Check(dx == 40.0f && (pressed & SDL_BUTTON_LMASK) != 0 && wheel == 1,
          "and the mouse's movement and wheel are read");
    Check(SDL_GetWindowRelativeMouseMode(window), "the game holds the pointer for mouse look");
    Frame();
    Frame();

    TestEscape(window);

    // The stand-in keyboard again before the window goes, so nothing asks
    // about the window afterwards.
    iPadKeyboardSetTestSource(&sKeyboard);
    Frame();
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
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
    TestEveryDefaultKey();
    TestOtherPads();
    TestMenus();
    TestTouchMouse();
    TestTouch();
    TestBackForTheMenus(dir);
    TestLauncherMouse();

    xPadKill();

    printf("tssm_input_test: %d checks, %d failed\n", (int)sChecks, (int)sFailures);
    return sFailures == 0 ? 0 : 1;
}
