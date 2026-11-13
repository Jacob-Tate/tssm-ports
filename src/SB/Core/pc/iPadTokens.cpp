// TSSM-ADAPTED from treedome: more pad buttons, and the mouse. See iPadTokens.h.

#include "iPadTokens.h"

#include "iHost.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

static const iPadBindToken kPadTokens[] = {
    { "a", PADIN_A },           { "b", PADIN_B },
    { "x", PADIN_X },           { "y", PADIN_Y },
    { "lb", PADIN_LB },         { "rb", PADIN_RB },
    { "lt", PADIN_LT },         { "rt", PADIN_RT },
    { "ls", PADIN_LS },         { "rs", PADIN_RS },
    { "back", PADIN_BACK },     { "start", PADIN_START },
    { "dpup", PADIN_DPUP },     { "dpdown", PADIN_DPDOWN },
    { "dpleft", PADIN_DPLEFT }, { "dpright", PADIN_DPRIGHT },
    { "misc1", PADIN_MISC1 },   { "paddle1", PADIN_PADDLE1 },
    { "paddle2", PADIN_PADDLE2 }, { "paddle3", PADIN_PADDLE3 },
    { "paddle4", PADIN_PADDLE4 }, { "touchpad", PADIN_TOUCHPAD },
    { "ltclick", PADIN_LTCLICK }, { "rtclick", PADIN_RTCLICK },
};

// Key names, for the right-hand side of a [keyboard] binding. The names are the
// ones config.ini has always used, so a file written by an older build still
// parses; only what they resolve to changed.
//
// Letters and digits are not listed: they are appended below, because writing
// out thirty-six rows that each say "this key is called what is on it" is
// noise. What is listed is everything whose name is not its character.
static const iPadBindToken kKeyTokens[] = {
    { "space", SDL_SCANCODE_SPACE },
    { "enter", SDL_SCANCODE_RETURN },
    { "tab", SDL_SCANCODE_TAB },
    { "escape", SDL_SCANCODE_ESCAPE },
    { "backspace", SDL_SCANCODE_BACKSPACE },
    { "shift", KEY_EITHER_SHIFT },
    { "lshift", SDL_SCANCODE_LSHIFT },
    { "rshift", SDL_SCANCODE_RSHIFT },
    { "ctrl", KEY_EITHER_CTRL },
    { "lctrl", SDL_SCANCODE_LCTRL },
    { "rctrl", SDL_SCANCODE_RCTRL },
    { "alt", KEY_EITHER_ALT },
    { "lalt", SDL_SCANCODE_LALT },
    { "ralt", SDL_SCANCODE_RALT },
    { "up", SDL_SCANCODE_UP },
    { "down", SDL_SCANCODE_DOWN },
    { "left", SDL_SCANCODE_LEFT },
    { "right", SDL_SCANCODE_RIGHT },
    { "insert", SDL_SCANCODE_INSERT },
    { "delete", SDL_SCANCODE_DELETE },
    { "home", SDL_SCANCODE_HOME },
    { "end", SDL_SCANCODE_END },
    { "pageup", SDL_SCANCODE_PAGEUP },
    { "pagedown", SDL_SCANCODE_PAGEDOWN },
    { "capslock", SDL_SCANCODE_CAPSLOCK },
    { "comma", SDL_SCANCODE_COMMA },
    { "period", SDL_SCANCODE_PERIOD },
    { "minus", SDL_SCANCODE_MINUS },
    { "equals", SDL_SCANCODE_EQUALS },
    { "semicolon", SDL_SCANCODE_SEMICOLON },
    { "slash", SDL_SCANCODE_SLASH },
    { "tilde", SDL_SCANCODE_GRAVE },
    { "lbracket", SDL_SCANCODE_LEFTBRACKET },
    { "backslash", SDL_SCANCODE_BACKSLASH },
    { "rbracket", SDL_SCANCODE_RIGHTBRACKET },
    { "quote", SDL_SCANCODE_APOSTROPHE },
    { "f1", SDL_SCANCODE_F1 },
    { "f2", SDL_SCANCODE_F2 },
    { "f3", SDL_SCANCODE_F3 },
    { "f4", SDL_SCANCODE_F4 },
    { "f5", SDL_SCANCODE_F5 },
    { "f6", SDL_SCANCODE_F6 },
    { "f7", SDL_SCANCODE_F7 },
    { "f8", SDL_SCANCODE_F8 },
    { "f9", SDL_SCANCODE_F9 },
    { "f10", SDL_SCANCODE_F10 },
    { "f11", SDL_SCANCODE_F11 },
    { "f12", SDL_SCANCODE_F12 },
    { "numpad0", SDL_SCANCODE_KP_0 },
    { "numpad1", SDL_SCANCODE_KP_1 },
    { "numpad2", SDL_SCANCODE_KP_2 },
    { "numpad3", SDL_SCANCODE_KP_3 },
    { "numpad4", SDL_SCANCODE_KP_4 },
    { "numpad5", SDL_SCANCODE_KP_5 },
    { "numpad6", SDL_SCANCODE_KP_6 },
    { "numpad7", SDL_SCANCODE_KP_7 },
    { "numpad8", SDL_SCANCODE_KP_8 },
    { "numpad9", SDL_SCANCODE_KP_9 },
    { "numpadplus", SDL_SCANCODE_KP_PLUS },
    { "numpadminus", SDL_SCANCODE_KP_MINUS },
    { "numpadstar", SDL_SCANCODE_KP_MULTIPLY },
    { "numpadslash", SDL_SCANCODE_KP_DIVIDE },
    { "numpaddot", SDL_SCANCODE_KP_PERIOD },
    { "numpadenter", SDL_SCANCODE_KP_ENTER },
    { "mouse1", KEY_MOUSE_LEFT },
    { "mouse2", KEY_MOUSE_RIGHT },
    { "mouse3", KEY_MOUSE_MIDDLE },
    { "mouse4", KEY_MOUSE_X1 },
    { "mouse5", KEY_MOUSE_X2 },
    { "wheelup", KEY_WHEEL_UP },
    { "wheeldown", KEY_WHEEL_DOWN },
};

static const S32 kKeyTokenCount = (S32)(sizeof(kKeyTokens) / sizeof(kKeyTokens[0]));

static iPadBindToken sKeyTokens[kKeyTokenCount + 36];
static S32 sKeyTokenCount;
static char sLetterNames[36][2];

// The number row is not contiguous with itself: SDL runs 1 through 9 and then
// puts 0 after them, in the order the keys sit on the board.
static S32 DigitScancode(S32 digit)
{
    return digit == 0 ? SDL_SCANCODE_0 : (SDL_SCANCODE_1 + digit - 1);
}

const iPadBindToken* iPadKeyTokens(S32* count)
{
    if (sKeyTokenCount == 0)
    {
        for (S32 i = 0; i < kKeyTokenCount; i++)
        {
            sKeyTokens[sKeyTokenCount++] = kKeyTokens[i];
        }

        for (S32 i = 0; i < 36; i++)
        {
            sLetterNames[i][0] = (i < 26) ? (char)('a' + i) : (char)('0' + (i - 26));
            sLetterNames[i][1] = '\0';

            sKeyTokens[sKeyTokenCount].name = sLetterNames[i];
            sKeyTokens[sKeyTokenCount].id =
                (S16)((i < 26) ? (SDL_SCANCODE_A + i) : DigitScancode(i - 26));
            sKeyTokenCount++;
        }
    }

    *count = sKeyTokenCount;
    return sKeyTokens;
}

const iPadBindToken* iPadPadTokens(S32* count)
{
    *count = (S32)(sizeof(kPadTokens) / sizeof(kPadTokens[0]));
    return kPadTokens;
}

S32 iPadInputFromSDLButton(S32 button)
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return PADIN_A;
    case SDL_GAMEPAD_BUTTON_EAST:
        return PADIN_B;
    case SDL_GAMEPAD_BUTTON_WEST:
        return PADIN_X;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return PADIN_Y;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return PADIN_LB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return PADIN_RB;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return PADIN_LS;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return PADIN_RS;
    case SDL_GAMEPAD_BUTTON_BACK:
        return PADIN_BACK;
    case SDL_GAMEPAD_BUTTON_START:
        return PADIN_START;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return PADIN_DPUP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return PADIN_DPDOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return PADIN_DPLEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return PADIN_DPRIGHT;
    case SDL_GAMEPAD_BUTTON_MISC1:
        return PADIN_MISC1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
        return PADIN_PADDLE1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
        return PADIN_PADDLE2;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
        return PADIN_PADDLE3;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
        return PADIN_PADDLE4;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        return PADIN_TOUCHPAD;
    case SDL_GAMEPAD_BUTTON_MISC3:
        return PADIN_LTCLICK;
    case SDL_GAMEPAD_BUTTON_MISC4:
        return PADIN_RTCLICK;
    default:
        return -1;
    }
}

const char* iPadInputLabel(S32 input, const char* kind)
{
    // Positions first, then what each family prints there.
    static const char* const kXbox[PADIN_COUNT] = {
        "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back", "Start",
        "Up", "Down", "Left", "Right", "Share", "P1", "P2", "P3", "P4", "Touchpad", "LT click",
        "RT click",
    };
    static const char* const kPlayStation[PADIN_COUNT] = {
        "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Share",
        "Options", "Up", "Down", "Left", "Right", "Mic", "P1", "P2", "P3", "P4", "Touchpad",
        "L2 click", "R2 click",
    };
    // SDL reports positions, and Nintendo prints B south and A east.
    static const char* const kSwitch[PADIN_COUNT] = {
        "B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "-", "+",
        "Up", "Down", "Left", "Right", "Capture", "SR", "SL", "SL", "SR", "Touchpad",
        "ZL click", "ZR click",
    };
    // An adapter's usual mapping: A south, B west, X east, Y north, Z on the
    // right bumper, L and R the analog triggers.
    static const char* const kGameCube[PADIN_COUNT] = {
        "A", "X", "B", "Y", "-", "Z", "L", "R", "-", "-", "-", "Start",
        "Up", "Down", "Left", "Right", "-", "-", "-", "-", "-", "-", "L click", "R click",
    };

    if (input < 0 || input >= PADIN_COUNT)
    {
        return "?";
    }

    if (kind != NULL)
    {
        if (iHostStrCaseCmp(kind, "playstation") == 0)
        {
            return kPlayStation[input];
        }
        if (iHostStrCaseCmp(kind, "switch") == 0)
        {
            return kSwitch[input];
        }
        if (iHostStrCaseCmp(kind, "gamecube") == 0)
        {
            return kGameCube[input];
        }
    }
    return kXbox[input];
}

bool iPadSwitchLabels()
{
#ifdef __SWITCH__
    return true;
#else
    static int sOn = -1;
    if (sOn < 0)
    {
        const char* env = getenv("TSSM_PAD_LABELS");
        sOn = (env != NULL && iHostStrCaseCmp(env, "switch") == 0) ? 1 : 0;
    }
    return sOn != 0;
#endif
}

const char* iPadSwitchRowLabel(const char* row)
{
    // The row's Xbox button, whose position the Switch's label is read at.
    struct Row
    {
        const char* name;
        S32 input;
    };
    static const Row kRows[] = {
        { "a", PADIN_A },         { "b", PADIN_B },      { "x", PADIN_X },
        { "y", PADIN_Y },         { "lt", PADIN_LT },    { "rt", PADIN_RT },
        { "white", PADIN_LB },    { "black", PADIN_RB }, { "back", PADIN_BACK },
        { "start", PADIN_START },
    };

    if (row == NULL)
    {
        return NULL;
    }
    for (U32 i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++)
    {
        if (iHostStrCaseCmp(kRows[i].name, row) == 0)
        {
            return iPadInputLabel(kRows[i].input, "switch");
        }
    }
    return NULL;
}
