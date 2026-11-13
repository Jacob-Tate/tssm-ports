// The control bindings: the half that reads config.ini and asks the pad in
// hand. The tables and the parser are in iPadBindTable.cpp; the grammar is in
// iPadBind.h.

#include "iPadBind.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadHost.h"

#include <stdio.h>
#include <string.h>

const char* iPadBindActivePreset()
{
    const char* want = iConfigGetString("input.preset", "auto");

    if (want == NULL || iHostStrCaseCmp(want, "auto") == 0)
    {
        const char* kind = iPadHostPadKind();
        want = (kind != NULL) ? kind : "xbox";
    }

    return want;
}

const char* iPadBindPadDefault(const iPadBindButton* button)
{
    if (button == NULL)
    {
        return NULL;
    }

    // Asking the backend on every call rather than caching is what lets a
    // controller swapped mid-game rebind: iPadHostSDL.cpp reloads the bindings
    // when port 0 changes hands, and this is what then answers differently.
    const char* want = iPadBindActivePreset();

    const char* bound = iPadBindPresetEntry(want, (S32)(button - kPadBindButtons));
    if (bound == NULL)
    {
        return button->pad;
    }

    // A '#' row names a LETTER, and which input carries it is the pad's
    // business. No pad yet (the config writer asks before one is open), or a
    // pad with no button printed with that letter: the Xbox position.
    if (bound[0] == '#')
    {
        const char* input = iPadHostInputForLabel(bound[1]);
        if (input != NULL)
        {
            return input;
        }

        switch (bound[1])
        {
        case 'A':
            return "a";
        case 'B':
            return "b";
        case 'X':
            return "x";
        case 'Y':
            return "y";
        default:
            return "";
        }
    }

    return bound;
}

void iPadBindLoad(iPadBindDevice device, const iPadBindToken* tokens, S32 tokenCount, iPadBind* out)
{
    const char* section = (device == IPAD_BIND_PAD) ? "pad" : "keyboard";

    for (S32 i = 0; i < kPadBindButtonCount && i < IPAD_BIND_MAX_BUTTONS; i++)
    {
        const iPadBindButton* b = &kPadBindButtons[i];
        const char* fallback = (device == IPAD_BIND_PAD) ? iPadBindPadDefault(b) : b->key;

        char key[96];
        snprintf(key, sizeof(key), "%s.%s", section, b->name);

        iPadBindParse(iConfigGetString(key, fallback), tokens, tokenCount, key, &out[i]);
    }

    for (S32 i = kPadBindButtonCount; i < IPAD_BIND_MAX_BUTTONS; i++)
    {
        memset(&out[i], 0, sizeof(out[i]));
    }

    // The array is sized by a macro and filled from a table whose length the
    // compiler will not hand over, so this is where the two are compared. A
    // row past the end would silently never be pressable.
    if (kPadBindButtonCount > IPAD_BIND_MAX_BUTTONS)
    {
        printf("tssm: %d rows to bind but room for %d; the last %d are unbound\n",
               (int)kPadBindButtonCount, (int)IPAD_BIND_MAX_BUTTONS,
               (int)(kPadBindButtonCount - IPAD_BIND_MAX_BUTTONS));
        fflush(stdout);
    }
}
