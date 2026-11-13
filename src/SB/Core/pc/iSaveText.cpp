// The load screen's "no saves" message; see iSaveText.h. Port-only.

#include "iSaveText.h"

#include "xString.h"
#include "xstransvc.h"

#include <stdio.h>
#include <string.h>

void iSaveTextNoGames(S32 target, S32 otherWithSaves)
{
    U32 size = 0;
    void* asset = xSTFindAsset(xStrHash("LD NOGAMES TXT"), &size);
    if (asset == NULL || size <= sizeof(U32))
    {
        return;
    }

    char* text = (char*)asset + sizeof(U32);
    const U32 capacity = size - (U32)sizeof(U32);

    // The string, if it ends inside the asset; nothing is read past it.
    if (memchr(text, '\0', capacity) == NULL)
    {
        return;
    }

    // The US disc's words, or ours from an earlier call.
    if (strstr(text, "Xbox Hard Disk") == NULL && strstr(text, "Memory Card Slot") == NULL)
    {
        return;
    }

    char want[128];
    if (otherWithSaves >= 0 && otherWithSaves != target)
    {
        // No ';' or ',' and nothing after the button: the menu font has no
        // glyph for either mark, and a '.' after the button sits apart from it.
        snprintf(want, sizeof(want),
                 "No saves on Memory Card Slot %d. Your saves are on Slot %d. Press {i:ui_accept}",
                 (int)target + 1, (int)otherWithSaves + 1);
    }
    else
    {
        snprintf(want, sizeof(want),
                 "No game saves on Memory Card Slot %d. Press {i:ui_accept} to continue.",
                 (int)target + 1);
    }

    const U32 n = (U32)strlen(want);
    if (n + 1 > capacity || strcmp(text, want) == 0)
    {
        return;
    }

    memcpy(text, want, n + 1);
    memcpy(asset, &n, sizeof(n));
}
