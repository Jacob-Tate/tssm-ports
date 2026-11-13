// The binding API for a settings screen. See iPadControls.h.

#include "iPadControls.h"

#include "iConfig.h"
#include "iConfigTable.h"
#include "iHost.h"
#include "iPadHost.h"
#include "iPadKeyboard.h"
#include "iPadLayout.h"
#include "iPadPrompts.h"
#include "iPadSettings.h"
#include "iPadTest.h"
#include "iPadTokens.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const iPadBindToken* Tokens(iPadBindDevice device, S32* count)
    {
        return (device == IPAD_BIND_PAD) ? iPadPadTokens(count) : iPadKeyTokens(count);
    }

    const iPadBindButton* RowAt(S32 row)
    {
        if (row < 0 || row >= kPadBindButtonCount)
        {
            return NULL;
        }
        return &kPadBindButtons[row];
    }

    void Key(iPadBindDevice device, const iPadBindButton* b, char* out, S32 size)
    {
        snprintf(out, (size_t)size, "%s.%s", device == IPAD_BIND_PAD ? "pad" : "keyboard",
                 b->name);
    }

    const char* Default(iPadBindDevice device, const iPadBindButton* b)
    {
        return device == IPAD_BIND_PAD ? iPadBindPadDefault(b) : b->key;
    }

    void Apply()
    {
        iPadHostReloadBindings();
        iPadPromptsInvalidate();
    }
} // namespace

S32 iPadControlsRowCount()
{
    return kPadBindButtonCount;
}

bool iPadControlsRow(S32 row, iPadControlsRowInfo* out)
{
    const iPadBindButton* b = RowAt(row);
    if (b == NULL || out == NULL)
    {
        return false;
    }
    out->name = b->name;
    out->does = iPadBindDescribe(b);
    out->mask = b->mask;
    out->stick = b->axis != IPAD_AXIS_NONE;
    return true;
}

S32 iPadControlsFindRow(const char* name)
{
    const iPadBindButton* b = iPadBindFind(name);
    return b != NULL ? (S32)(b - kPadBindButtons) : -1;
}

void iPadControlsGet(iPadBindDevice device, S32 row, char* out, S32 size, bool* isDefault)
{
    const iPadBindButton* b = RowAt(row);
    if (size > 0)
    {
        out[0] = '\0';
    }
    if (b == NULL)
    {
        return;
    }

    char key[64];
    Key(device, b, key, sizeof(key));

    const char* def = Default(device, b);
    const char* value = iConfigGetString(key, def);
    snprintf(out, (size_t)size, "%s", value != NULL ? value : "");

    if (isDefault != NULL)
    {
        *isDefault = (value == NULL && def == NULL) ||
                     (value != NULL && def != NULL && iHostStrCaseCmp(value, def) == 0);
    }
}

bool iPadControlsSet(iPadBindDevice device, S32 row, const char* text, char* err, S32 errSize)
{
    const iPadBindButton* b = RowAt(row);
    if (b == NULL)
    {
        if (err != NULL && errSize > 0)
        {
            snprintf(err, (size_t)errSize, "no such row");
        }
        return false;
    }

    S32 count;
    const iPadBindToken* tokens = Tokens(device, &count);

    iPadBind parsed;
    if (!iPadBindParseQuiet(text != NULL ? text : "", tokens, count, &parsed, err, errSize))
    {
        return false;
    }

    // Written back from the parse, so the file holds one spelling.
    char normal[160];
    iPadBindFormat(parsed, tokens, count, normal, sizeof(normal));

    char key[64];
    Key(device, b, key, sizeof(key));
    iConfigSet(key, normal);
    Apply();
    return true;
}

bool iPadControlsAdd(iPadBindDevice device, S32 row, const char* input, char* err, S32 errSize)
{
    char current[160];
    iPadControlsGet(device, row, current, sizeof(current), NULL);

    char joined[200];
    if (current[0] == '\0')
    {
        snprintf(joined, sizeof(joined), "%s", input);
    }
    else
    {
        snprintf(joined, sizeof(joined), "%s, %s", current, input);
    }
    return iPadControlsSet(device, row, joined, err, errSize);
}

bool iPadControlsRemove(iPadBindDevice device, S32 row, S32 index)
{
    S32 count;
    const iPadBindToken* tokens = Tokens(device, &count);

    char current[160];
    iPadControlsGet(device, row, current, sizeof(current), NULL);

    iPadBind parsed;
    if (!iPadBindParseQuiet(current, tokens, count, &parsed, NULL, 0) || index < 0 ||
        index >= parsed.alts)
    {
        return false;
    }

    for (S32 a = index; a + 1 < parsed.alts; a++)
    {
        memcpy(parsed.id[a], parsed.id[a + 1], sizeof(parsed.id[a]));
        memcpy(parsed.negate[a], parsed.negate[a + 1], sizeof(parsed.negate[a]));
        parsed.length[a] = parsed.length[a + 1];
    }
    parsed.alts--;

    char text[160];
    iPadBindFormat(parsed, tokens, count, text, sizeof(text));
    return iPadControlsSet(device, row, text, NULL, 0);
}

void iPadControlsReset(iPadBindDevice device, S32 row)
{
    const iPadBindButton* b = RowAt(row);
    if (b == NULL)
    {
        return;
    }

    char key[64];
    Key(device, b, key, sizeof(key));
    iConfigUnset(key);
    Apply();
}

void iPadControlsResetAll(iPadBindDevice device)
{
    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        char key[64];
        Key(device, &kPadBindButtons[i], key, sizeof(key));
        iConfigUnset(key);
    }
    Apply();
}

S32 iPadControlsCapture(iPadBindDevice device, S32 prime, char* out, S32 size)
{
    return device == IPAD_BIND_PAD ? iPadHostCaptureButton(prime, out, size) :
                                     iPadHostCaptureKey(prime, out, size);
}

const char* iPadControlsInputLabel(iPadBindDevice device, const char* token)
{
    static char buf[32];

    if (token == NULL)
    {
        return "";
    }

    S32 count;
    const iPadBindToken* tokens = Tokens(device, &count);
    S16 id = -1;
    for (S32 i = 0; i < count; i++)
    {
        if (iHostStrCaseCmp(tokens[i].name, token) == 0)
        {
            id = tokens[i].id;
            break;
        }
    }

    if (device == IPAD_BIND_PAD)
    {
        if (id < 0)
        {
            return token;
        }
        const char* kind = iPadHostPadKind();
        if (kind == NULL)
        {
            kind = iPadBindActivePreset();
        }
        return iPadInputLabel(id, kind);
    }

    struct Name
    {
        const char* token;
        const char* label;
    };
    static const Name kNames[] = {
        { "mouse1", "Left mouse" },     { "mouse2", "Right mouse" }, { "mouse3", "Middle mouse" },
        { "mouse4", "Mouse 4" },        { "mouse5", "Mouse 5" },     { "wheelup", "Wheel up" },
        { "wheeldown", "Wheel down" },  { "lctrl", "Left Ctrl" },    { "rctrl", "Right Ctrl" },
        { "lshift", "Left Shift" },     { "rshift", "Right Shift" }, { "lalt", "Left Alt" },
        { "ralt", "Right Alt" },        { "escape", "Esc" },         { "pageup", "Page Up" },
        { "pagedown", "Page Down" },    { "capslock", "Caps Lock" },
    };
    for (U32 i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
    {
        if (iHostStrCaseCmp(kNames[i].token, token) == 0)
        {
            return kNames[i].label;
        }
    }

    // Everything else is its token, capitalised: "space" -> "Space", "f" -> "F".
    snprintf(buf, sizeof(buf), "%s", token);
    if (buf[0] != '\0')
    {
        buf[0] = (char)toupper((unsigned char)buf[0]);
    }
    if (buf[1] == '\0' || (buf[0] == 'F' && isdigit((unsigned char)buf[1])))
    {
        for (char* p = buf; *p != '\0'; p++)
        {
            *p = (char)toupper((unsigned char)*p);
        }
    }
    return buf;
}

S32 iPadControlsWhoUses(iPadBindDevice device, const char* token, S32* rows, S32 max)
{
    S32 count;
    const iPadBindToken* tokens = Tokens(device, &count);

    S16 id = -1;
    for (S32 i = 0; i < count; i++)
    {
        if (iHostStrCaseCmp(tokens[i].name, token) == 0)
        {
            id = tokens[i].id;
            break;
        }
    }
    if (id < 0)
    {
        return 0;
    }

    const iPadBind* binds = (device == IPAD_BIND_PAD) ? iPadHostPadBinds() : iPadKeyboardBinds();
    if (binds == NULL)
    {
        return 0;
    }

    S32 found = 0;
    for (S32 r = 0; r < kPadBindButtonCount && r < IPAD_BIND_MAX_BUTTONS; r++)
    {
        bool uses = false;
        for (S32 a = 0; a < binds[r].alts && !uses; a++)
        {
            for (S32 i = 0; i < binds[r].length[a]; i++)
            {
                if (binds[r].id[a][i] == id && !binds[r].negate[a][i])
                {
                    uses = true;
                    break;
                }
            }
        }
        if (uses)
        {
            if (found < max && rows != NULL)
            {
                rows[found] = r;
            }
            found++;
        }
    }
    return found;
}

const char* iPadControlsGetSetting(const char* name)
{
    char key[64];
    snprintf(key, sizeof(key), "input.%s", name);
    const iConfigSetting* s = iConfigTableFind(key);
    return iConfigGetString(key, s != NULL ? s->value : "");
}

bool iPadControlsSetSetting(const char* name, const char* value, char* err, S32 errSize)
{
    char key[64];
    snprintf(key, sizeof(key), "input.%s", name);

    const iConfigSetting* s = iConfigTableFind(key);
    if (s == NULL)
    {
        if (err != NULL && errSize > 0)
        {
            snprintf(err, (size_t)errSize, "no setting input.%s", name);
        }
        return false;
    }

    char why[256];
    if (!iConfigTableValidate(s, value, why, sizeof(why)))
    {
        if (err != NULL && errSize > 0)
        {
            snprintf(err, (size_t)errSize, "%s", why);
        }
        return false;
    }

    iConfigSet(key, value);

    if (iHostStrCaseCmp(name, "button_icons") == 0)
    {
        iPadPromptsSetIcons(value);
    }
    Apply();
    return true;
}

bool iPadControlsSave()
{
    return iConfigSave();
}

void iPadControlsGetLive(iPadControlsLive* out)
{
    const iPadTestSnapshot& s = iPadTestLast();
    out->on = s.on;
    out->stick_x = s.stick_x;
    out->stick_y = s.stick_y;
    out->cstick_x = s.cstick_x;
    out->cstick_y = s.cstick_y;
    out->trigger_l = s.trigger_l;
    out->trigger_r = s.trigger_r;
    out->connected = s.connected;
    out->device = iPadHostDeviceName(0);
    out->preset = iPadBindActivePreset();
    out->layout = iPadLayoutName(iPadLayoutActive());
    out->icons = iPadPromptsActiveSet();
}

void iPadControlsSetMouseCapture(bool allowed)
{
    iPadKeyboardSetMouseCapture(allowed);
}
