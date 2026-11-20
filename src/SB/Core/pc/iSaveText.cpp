// The load screen's "no saves" message, and the save screens' warning as a
// setting; see iSaveText.h. Port-only.

#include "iSaveText.h"

#include "iConfig.h"
#include "iLowHeap.h"
#include "xpkrsvc.h"
#include "xString.h"
#include "xstransvc.h"

#include <stdio.h>
#include <stdlib.h>
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

// ---------------------------------------------------------------------------
// The save screens' warning

namespace
{
    // An xTextAsset: a U32 length, then the string and its NUL.
    const U32 kHeader = (U32)sizeof(U32);

    // Room for the longest line with the longest choice's words (about 190
    // characters), with margin. The packer takes the copy's size as the
    // asset's.
    const U32 kCopyText = 320;

    struct Copy
    {
        U32 id;
        U8* mem; // kHeader + kCopyText bytes
        char original[kCopyText];
        bool logged;
    };

    // One per asset that held a line, for the life of the process: the
    // packer's node points at it while the asset is loaded, and the next load
    // of the same asset is handed the same copy.
    const S32 kMaxCopies = 32;
    Copy sCopies[kMaxCopies];
    S32 sCopyCount;

    iSaveTextChoice sChoice = ISAVETEXT_AUTO;
    void* (*sGameRead)(void*, U32, void*, U32, U32*);

    // The copy's string, rewritten for the choice in force.
    void Render(Copy& c)
    {
        char text[kCopyText];
        if (!iSaveTextRewrite(c.original, sChoice, text, sizeof(text)))
        {
            snprintf(text, sizeof(text), "%s", c.original);
        }
        const U32 n = (U32)strlen(text);
        memcpy(c.mem, &n, sizeof(n));
        memcpy(c.mem + kHeader, text, n + 1);
    }

    Copy* CopyFor(U32 id)
    {
        for (S32 i = 0; i < sCopyCount; i++)
        {
            if (sCopies[i].id == id)
            {
                return &sCopies[i];
            }
        }
        if (sCopyCount >= kMaxCopies)
        {
            return NULL;
        }

        // Below 4 GB on a 64-bit host, like every other asset (iLowHeap.h).
        const size_t bytes = kHeader + kCopyText;
        U8* mem = iLowHeapWanted() ? (U8*)iLowHeapMalloc(bytes) : (U8*)malloc(bytes);
        if (mem == NULL)
        {
            return NULL;
        }
        memset(mem, 0, bytes);

        Copy& c = sCopies[sCopyCount++];
        c.id = id;
        c.mem = mem;
        c.original[0] = '\0';
        c.logged = false;
        return &c;
    }

    // The TEXT type's readXForm: an asset that is one of the lines comes back
    // as this file's copy of it; every other one is handed on untouched.
    void* ReadText(void* userdata, U32 id, void* indata, U32 insize, U32* outsize)
    {
        void* result = indata;
        *outsize = insize;
        if (sGameRead != NULL)
        {
            result = sGameRead(userdata, id, indata, insize, outsize);
        }
        if (result != indata || indata == NULL || insize <= kHeader)
        {
            return result;
        }

        // The string, if it ends inside the asset; nothing is read past it.
        const char* text = (const char*)indata + kHeader;
        const U32 room = insize - kHeader;
        if (memchr(text, '\0', room) == NULL || !iSaveTextIsLine(text))
        {
            return result;
        }

        Copy* c = CopyFor(id);
        if (c == NULL || strlen(text) >= sizeof(c->original))
        {
            return result;
        }
        snprintf(c->original, sizeof(c->original), "%s", text);
        Render(*c);

        if (!c->logged)
        {
            c->logged = true;
            printf("tssm: save text: %08x is a save screen line; it says %s's words (%s)\n",
                   (unsigned)id, iSaveTextChoiceName(iSaveTextResolve(sChoice)),
                   iSaveTextChoiceName(sChoice));
            fflush(stdout);
        }

        *outsize = kHeader + kCopyText;
        return c->mem;
    }
} // namespace

void iSaveTextInstall(PKRAssetType* handlers)
{
    if (handlers == NULL)
    {
        return;
    }

    PKRAssetType* t = handlers;
    while (t->typetag != 0 && t->typetag != 'TEXT')
    {
        t++;
    }
    if (t->typetag == 0)
    {
        printf("tssm: save text: no TEXT handler in the game's table\n");
        return;
    }
    if (t->readXForm == ReadText)
    {
        return;
    }

    sGameRead = t->readXForm;
    t->readXForm = ReadText;
}

void iSaveTextApplyConfig()
{
    static char sBadLogged[64];

    const char* value = iConfigGetString("game.save_text", "auto");
    bool ok = true;
    sChoice = iSaveTextParse(value, &ok);
    if (!ok)
    {
        // Once per bad value, not on every apply.
        if (strncmp(sBadLogged, value, sizeof(sBadLogged) - 1) != 0)
        {
            snprintf(sBadLogged, sizeof(sBadLogged), "%s", value);
            printf("tssm: config: game.save_text is not auto, xbox, ps2, gamecube, generic, pc, "
                   "android, switch or 3ds, using auto: %s\n",
                   value);
            fflush(stdout);
        }
    }
    else
    {
        sBadLogged[0] = '\0';
    }

    for (S32 i = 0; i < sCopyCount; i++)
    {
        if (sCopies[i].original[0] != '\0')
        {
            Render(sCopies[i]);
        }
    }
}

iSaveTextChoice iSaveTextCurrent()
{
    return sChoice;
}
