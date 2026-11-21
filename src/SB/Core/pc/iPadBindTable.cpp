// The binding tables, the presets and the parser: everything about a binding
// that needs no config file and no device. iPadBind.cpp is the half that reads
// config.ini and asks the pad in hand. The grammar is in iPadBind.h, and
// docs/CONTROLS.md has the table below as a player reads it.
//
// TSSM-ADAPTED from treedome: TSSM's rows, named after the Xbox controller.

#include "iPadBind.h"

#include "iHost.h"
#include "iPadLayout.h"
#include "xPad.h"

#include <stdio.h>
#include <string.h>

// The order here is the order a generated config.ini lists them, so it reads
// like a controller: face buttons, triggers, the two small buttons, Back and
// Start, the d-pad, then the keyboard's stand-ins for the sticks.
//
// Which bit each Xbox button sets is the Xbox release's own pad layer for
// A, Y, the triggers, white, black, Back and Start (DEFAULT.XBE, the function
// that fills _tagxPad::on from XInput; CONTROLS.md quotes it). A, B, X and Y
// set the bits gc/iPad.cpp sets for the GameCube buttons printed with the same
// letters -- which is also what the Xbox menus answer to, because the Xbox's UI
// code swaps its B and X events back. The one thing the Xbox moved for real,
// which face button each MOVE reads, is the move layout in iPadLayout.h.
//
// Black sets Z as well as R2 and Back sets Z as well as Select:
//   - black: the Xbox sets both (0x102000), and the GameCube's Z+R does too.
//   - back:  the Xbox data opens the upgrade window on PadPressSelect, and the
//            Xbox's zUIMgrUpdate wakes the menus on Start or Select. The
//            GameCube code this port runs wakes them on Start or Z, so Back
//            carries Z to wake them; zUI::DoUpdate tests Select before Z, so
//            under the Xbox's menus the press arrives as Select. The
//            GameCube's menus open the window on PadPressE (Z) and hear
//            nothing on Select, so under them iPad.cpp's SelectForTheMenus
//            sends Back as Z alone, as a GameCube pad does.
const iPadBindButton kPadBindButtons[] = {
    { "a", XPAD_BUTTON_X, "#A", "space", "jump, double jump; accept; car: drive", IPAD_AXIS_NONE },
    { "b", XPAD_BUTTON_TRIANGLE, "#B", "mouse2, backspace",
      "SpongeBowl, Cartwheel, car horn; cancel, let go of a bungee", IPAD_AXIS_NONE },
    { "x", XPAD_BUTTON_O, "#X", "mouse1, f",
      "Karate Spin, Star Spin, Smash, car reverse; options", IPAD_AXIS_NONE },
    { "y", XPAD_BUTTON_SQUARE, "#Y", "q", "Bash, swing", IPAD_AXIS_NONE },
    { "lt", XPAD_BUTTON_L1, "lt", "mouse3, r",
      "camera: tap to center, hold for close-up; shows the HUD", IPAD_AXIS_NONE },
    { "rt", XPAD_BUTTON_R1, "rt", "e", "talk, pick up and throw, Sonic Wave, nitro",
      IPAD_AXIS_NONE },
    { "white", XPAD_BUTTON_L2, "lb", "z", "nothing in normal play", IPAD_AXIS_NONE },
    { "black", XPAD_BUTTON_R2 | XPAD_BUTTON_Z, "rb", "c", "nothing in normal play",
      IPAD_AXIS_NONE },
    { "back", XPAD_BUTTON_SELECT | XPAD_BUTTON_Z, "back", "tab", "upgrade window",
      IPAD_AXIS_NONE },
    { "start", XPAD_BUTTON_START, "start", "escape, enter", "pause", IPAD_AXIS_NONE },
    { "up", XPAD_BUTTON_UP, "dpup", "up", "d-pad: walks, and moves through menus",
      IPAD_AXIS_NONE },
    { "down", XPAD_BUTTON_DOWN, "dpdown", "down", NULL, IPAD_AXIS_NONE },
    { "left", XPAD_BUTTON_LEFT, "dpleft", "left", NULL, IPAD_AXIS_NONE },
    { "right", XPAD_BUTTON_RIGHT, "dpright", "right", NULL, IPAD_AXIS_NONE },
    { "move_up", 0, "", "w", "left stick, for a keyboard", IPAD_AXIS_MOVE_UP },
    { "move_down", 0, "", "s", NULL, IPAD_AXIS_MOVE_DOWN },
    { "move_left", 0, "", "a", NULL, IPAD_AXIS_MOVE_LEFT },
    { "move_right", 0, "", "d", NULL, IPAD_AXIS_MOVE_RIGHT },
    { "walk", 0, "", "lctrl", "hold: move keys walk instead of run", IPAD_AXIS_WALK },
    { "camera_up", 0, "", "i", "right stick, for a keyboard (the mouse turns it too)",
      IPAD_AXIS_CAMERA_UP },
    { "camera_down", 0, "", "k", NULL, IPAD_AXIS_CAMERA_DOWN },
    { "camera_left", 0, "", "j", NULL, IPAD_AXIS_CAMERA_LEFT },
    { "camera_right", 0, "", "l", NULL, IPAD_AXIS_CAMERA_RIGHT },
};

const S32 kPadBindButtonCount = (S32)(sizeof(kPadBindButtons) / sizeof(kPadBindButtons[0]));

const char* iPadBindDescribe(const iPadBindButton* button)
{
    if (button == NULL)
    {
        return NULL;
    }

    // The two rows the move layout trades between. Everything else reads the
    // same under both layouts.
    if (iPadLayoutActive() == IPAD_LAYOUT_GAMECUBE)
    {
        if (button->mask == XPAD_BUTTON_TRIANGLE)
        {
            return "Karate Spin, Star Spin, Smash, car reverse; cancel, let go of a bungee";
        }
        if (button->mask == XPAD_BUTTON_O)
        {
            return "SpongeBowl, Cartwheel, car horn; options";
        }
    }

    return button->does;
}

// The presets. Entries line up with kPadBindButtons above by position; a NULL
// takes that row's own default.
#define PAD_FACES "#A", "#B", "#X", "#Y"

const iPadBindPreset kPadBindPresets[] = {
    // FIRST, and so what "auto" settles on when it cannot place the pad.
    // Every row is the base: the Xbox's four shoulders map one to one onto a
    // modern pad's -- triggers for LT/RT, and the bumpers where Xbox 360 put
    // the old white and black buttons.
    { "xbox", "an Xbox pad or anything that maps like one", { NULL } },

    // Same rows; the letters are found on the shapes (cross A, circle B,
    // square X, triangle Y), which is where SDL puts them anyway.
    { "playstation", "a DualShock or DualSense", { NULL } },

    // Same rows, found by the letter printed on the button: Nintendo prints A
    // on the east button, so "A" in a prompt is the button that says A.
    { "switch", "a Switch Pro controller or Joy-Cons", { NULL } },

    // Three shoulders where the game wants four, so Z modifies exactly as on
    // the console (gc/iPad.cpp): L alone is LT, Z+L is white, Z+R is black.
    // Z alone is Back, which the GameCube release's prompts draw as Z for the
    // upgrade window. SDL gives a GameCube pad no left bumper and no Back.
    { "gamecube",
      "a GameCube controller on an adapter: Z is Back, Z+L white, Z+R black",
      { PAD_FACES, "lt+!rb", "rt+!rb", "rb+lt", "rb+rt", "rb" } },
};

const S32 kPadBindPresetCount = (S32)(sizeof(kPadBindPresets) / sizeof(kPadBindPresets[0]));

const char* iPadBindPresetEntry(const char* preset, S32 row)
{
    if (row < 0 || row >= kPadBindButtonCount || row >= IPAD_BIND_MAX_BUTTONS)
    {
        return NULL;
    }

    for (S32 i = 0; i < kPadBindPresetCount; i++)
    {
        if (preset != NULL && iHostStrCaseCmp(kPadBindPresets[i].name, preset) == 0)
        {
            const char* bound = kPadBindPresets[i].pad[row];
            return bound != NULL ? bound : kPadBindButtons[row].pad;
        }
    }

    return kPadBindButtons[row].pad;
}

S16 iPadBindSoleInput(const iPadBind& bind)
{
    if (bind.alts < 1)
    {
        return -1;
    }

    S16 found = -1;
    for (S32 i = 0; i < bind.length[0]; i++)
    {
        if (bind.negate[0][i])
        {
            continue;
        }
        if (found >= 0)
        {
            return -1;
        }
        found = bind.id[0][i];
    }

    return found;
}

const char* iPadBindTokenName(S16 id, const iPadBindToken* tokens, S32 tokenCount)
{
    for (S32 i = 0; i < tokenCount; i++)
    {
        if (tokens[i].id == id)
        {
            return tokens[i].name;
        }
    }
    return NULL;
}

const iPadBindButton* iPadBindFind(const char* name)
{
    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        if (iHostStrCaseCmp(kPadBindButtons[i].name, name) == 0)
        {
            return &kPadBindButtons[i];
        }
    }
    return NULL;
}

namespace
{
    const S32 kMaxToken = 24;

    const char* skipSpace(const char* s)
    {
        while (*s == ' ' || *s == '\t')
        {
            s++;
        }
        return s;
    }

    // Copies up to the next `stop` character or the end, trimmed. Returns the
    // position to carry on from.
    const char* takeToken(const char* s, char stop, char* out, S32 outSize)
    {
        s = skipSpace(s);

        const char* end = s;
        while (*end != '\0' && *end != stop)
        {
            end++;
        }

        const char* trimmed = end;
        while (trimmed > s && (trimmed[-1] == ' ' || trimmed[-1] == '\t'))
        {
            trimmed--;
        }

        S32 n = (S32)(trimmed - s);
        if (n >= outSize)
        {
            n = outSize - 1;
        }
        memcpy(out, s, (size_t)n);
        out[n] = '\0';

        return end;
    }

    S16 lookup(const char* name, const iPadBindToken* tokens, S32 tokenCount)
    {
        for (S32 i = 0; i < tokenCount; i++)
        {
            if (iHostStrCaseCmp(tokens[i].name, name) == 0)
            {
                return tokens[i].id;
            }
        }
        return -1;
    }

    // Where a diagnostic goes: printed with the setting's name, or kept for a
    // caller that shows it itself. Only the first is kept.
    struct Report
    {
        const char* what;
        char* err;
        S32 errSize;
        bool any;

        void say(const char* fmt, const char* a, S32 n)
        {
            char line[192];
            if (a != NULL)
            {
                snprintf(line, sizeof(line), fmt, a);
            }
            else
            {
                snprintf(line, sizeof(line), fmt, (int)n);
            }

            if (err != NULL)
            {
                if (!any && errSize > 0)
                {
                    snprintf(err, (size_t)errSize, "%s", line);
                }
            }
            else
            {
                printf("tssm: %s: %s\n", what, line);
            }
            any = true;
        }
    };

    bool parse(const char* text, const iPadBindToken* tokens, S32 tokenCount, Report& r,
               iPadBind* out)
    {
        memset(out, 0, sizeof(*out));

        if (text == NULL)
        {
            return true;
        }

        // Built to the side and only committed at the end, so a line that fails
        // half way through leaves the button unbound rather than partly bound.
        iPadBind built;
        memset(&built, 0, sizeof(built));

        const char* s = skipSpace(text);
        if (*s == '\0')
        {
            // Deliberately empty. Not an error: it is how a button is turned off.
            *out = built;
            return true;
        }

        bool ok = true;

        while (*s != '\0')
        {
            char alternative[kMaxToken * IPAD_BIND_CHORD + 8];
            s = takeToken(s, ',', alternative, (S32)sizeof(alternative));

            if (*s == ',')
            {
                s++;

                // A separator with nothing after it is someone half way through
                // an edit, not someone who meant this.
                if (*skipSpace(s) == '\0')
                {
                    r.say("',' with nothing after it, ignored", NULL, 0);
                    ok = false;
                    break;
                }
            }

            if (alternative[0] == '\0')
            {
                r.say("empty alternative, ignored", NULL, 0);
                ok = false;
                continue;
            }

            if (built.alts == IPAD_BIND_ALTS)
            {
                r.say("more than %d alternatives, the rest are ignored", NULL, IPAD_BIND_ALTS);
                ok = false;
                break;
            }

            S8 length = 0;
            S32 positives = 0;
            bool altOk = true;

            for (const char* c = alternative; *c != '\0';)
            {
                char token[kMaxToken];
                c = takeToken(c, '+', token, (S32)sizeof(token));

                if (*c == '+')
                {
                    c++;

                    if (*skipSpace(c) == '\0')
                    {
                        r.say("'+' with nothing after it, ignored", NULL, 0);
                        altOk = false;
                        break;
                    }
                }

                const char* name = token;
                bool negate = false;
                if (name[0] == '!')
                {
                    negate = true;
                    name = skipSpace(name + 1);
                }

                if (name[0] == '\0')
                {
                    r.say("'+' with nothing after it, ignored", NULL, 0);
                    altOk = false;
                    break;
                }

                if (length == IPAD_BIND_CHORD)
                {
                    r.say("more than %d inputs joined by '+', ignored", NULL, IPAD_BIND_CHORD);
                    altOk = false;
                    break;
                }

                S16 id = lookup(name, tokens, tokenCount);
                if (id < 0)
                {
                    r.say("'%s' is not an input this device has, ignored", name, 0);
                    altOk = false;
                    break;
                }

                built.id[built.alts][length] = id;
                built.negate[built.alts][length] = negate ? 1 : 0;
                length++;

                if (!negate)
                {
                    positives++;
                }
            }

            if (!altOk)
            {
                ok = false;
                continue;
            }

            // All negations would be satisfied by an idle controller, which is
            // a button that is on until you press something. Always a mistake.
            if (positives == 0)
            {
                r.say("'%s' is only '!' terms, so nothing would press it; ignored", alternative,
                      0);
                ok = false;
                continue;
            }

            built.length[built.alts] = length;
            built.alts++;
        }

        if (!ok)
        {
            return false;
        }

        *out = built;
        return true;
    }
} // namespace

bool iPadBindParse(const char* text, const iPadBindToken* tokens, S32 tokenCount, const char* what,
                   iPadBind* out)
{
    Report r = { what, NULL, 0, false };
    return parse(text, tokens, tokenCount, r, out);
}

bool iPadBindParseQuiet(const char* text, const iPadBindToken* tokens, S32 tokenCount,
                        iPadBind* out, char* err, S32 errSize)
{
    char none[1];
    Report r = { "", err != NULL ? err : none, err != NULL ? errSize : 1, false };
    if (err != NULL && errSize > 0)
    {
        err[0] = '\0';
    }
    return parse(text, tokens, tokenCount, r, out);
}

void iPadBindFormat(const iPadBind& bind, const iPadBindToken* tokens, S32 tokenCount, char* out,
                    S32 size)
{
    if (size <= 0)
    {
        return;
    }
    out[0] = '\0';

    S32 used = 0;
    for (S32 a = 0; a < bind.alts; a++)
    {
        for (S32 i = 0; i < bind.length[a]; i++)
        {
            const char* name = iPadBindTokenName(bind.id[a][i], tokens, tokenCount);
            const char* sep = (i > 0) ? "+" : ((a > 0) ? ", " : "");
            S32 n = snprintf(out + used, (size_t)(size - used), "%s%s%s", sep,
                             bind.negate[a][i] ? "!" : "", name != NULL ? name : "?");
            if (n < 0 || used + n >= size)
            {
                out[size - 1] = '\0';
                return;
            }
            used += n;
        }
    }
}

bool iPadBindHeld(const iPadBind& bind, bool (*held)(S16 id))
{
    for (S32 a = 0; a < bind.alts; a++)
    {
        bool all = true;
        for (S32 i = 0; i < bind.length[a]; i++)
        {
            if (held(bind.id[a][i]) == (bind.negate[a][i] != 0))
            {
                all = false;
                break;
            }
        }

        if (all)
        {
            return true;
        }
    }

    return false;
}

U32 iPadBindEvaluate(const iPadBind* binds, bool (*held)(S16 id), U32* axes)
{
    U32 on = 0;
    U32 axisBits = 0;

    for (S32 i = 0; i < kPadBindButtonCount && i < IPAD_BIND_MAX_BUTTONS; i++)
    {
        if (!iPadBindHeld(binds[i], held))
        {
            continue;
        }

        if (kPadBindButtons[i].axis != IPAD_AXIS_NONE)
        {
            axisBits |= 1u << kPadBindButtons[i].axis;
        }
        else
        {
            on |= kPadBindButtons[i].mask;
        }
    }

    if (axes != NULL)
    {
        *axes = axisBits;
    }
    return on;
}
