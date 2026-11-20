#include "iAssetWiden.h"

// The packer side of the copy-at-load widening (iAssetWiden.h): the handler
// table's hooks, the copies' memory, and the log. New code.

#include "iLowHeap.h"
#include "iSaveText.h"
#include "xpkrsvc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unordered_map>

extern void* (*gAssetWidenAlloc)(size_t);
extern void (*gAssetWidenFree)(void*);

#ifndef TSSM_PTR64

void iAssetTypesInstall(PKRAssetType* handlers)
{
    // 32-bit: every slot is pointer-sized already; the assets are read as they are.
    // The save screens' warning is a setting on every host (iSaveText.h).
    iSaveTextInstall(handlers);
}

#else

namespace
{
    enum HookKind
    {
        // No readXForm of the game's: the copy becomes the asset's memory.
        kReplace,
        // The game's readXForm builds something of its own from the copy,
        // which is freed when it returns.
        kWrapTemp,
        // The game's readXForm keeps pointers into the copy, which is freed
        // after the asset's unload.
        kWrapKeep,
        // Not widened (no such asset in the Xbox data): a load is reported.
        kReport,
    };

    struct Hook
    {
        U32 type;
        HookKind kind;
        void* (*read)(void*, U32, void*, U32, U32*);
        void (*unload)(void*, U32);
        U32 widened;
        U32 asIs;
        U64 diskBytes;
        U64 hostBytes;
        bool reported;
    };
} // namespace

static Hook sHooks[] = {
    { 'ATBL', kWrapTemp }, { 'SGRP', kReplace }, { 'LODT', kReplace },  { 'COLL', kReplace },
    { 'CRDT', kReplace },  { 'DEST', kReplace }, { 'SHRP', kReplace },  { 'LKIT', kReplace },
    { 'DYNA', kReplace },  { 'SPLN', kWrapKeep }, { 'JSP ', kWrapKeep }, { 'MPHT', kReport },
    { 'CRV ', kReport },   { 'VOLU', kReport },   { 'SPLP', kReport },
};

#define NUM_HOOKS ((S32)(sizeof(sHooks) / sizeof(sHooks[0])))

// What a kWrapKeep readXForm returned (the JSP header), to the copy it keeps
// pointers into.
static std::unordered_map<void*, void*> sKept;

static bool sLog;

static const char* TypeName(U32 type, char* buf)
{
    buf[0] = (char)(type >> 24);
    buf[1] = (char)(type >> 16);
    buf[2] = (char)(type >> 8);
    buf[3] = (char)type;
    buf[4] = '\0';
    return buf;
}

static void* LowAlloc(size_t n)
{
    return iLowHeapMalloc(n);
}

static void LowFree(void* p)
{
    iLowHeapFree(p);
}

static void* HookRead(S32 k, void* userdata, U32 aid, void* indata, U32 insize, U32* outsize)
{
    Hook& h = sHooks[k];
    char name[5];

    if (h.kind == kReport)
    {
        if (!h.reported)
        {
            h.reported = true;
            printf("tssm: widen: a %s asset (%08x) was loaded; this type is not widened for a "
                   "64-bit host and may be misread\n",
                   TypeName(h.type, name), aid);
        }
    }

    if (indata == NULL || insize == 0 || h.kind == kReport)
    {
        if (h.read != NULL)
        {
            return h.read(userdata, aid, indata, insize, outsize);
        }
        *outsize = insize;
        return indata;
    }

    U32 hostsize = 0;
    const char* why = "";
    void* copy = iAssetWiden(h.type, aid, indata, insize, &hostsize, &why);

    if (copy == NULL)
    {
        h.asIs++;
        if (*why != '\0')
        {
            printf("tssm: widen: %s %08x (%u bytes): %s; read as it is\n", TypeName(h.type, name),
                   aid, insize, why);
        }
    }
    else
    {
        h.widened++;
        h.diskBytes += insize;
        h.hostBytes += hostsize;
        if (sLog)
        {
            printf("tssm: widen: %s %08x %u -> %u bytes\n", TypeName(h.type, name), aid, insize,
                   hostsize);
        }
    }

    void* data = copy != NULL ? copy : indata;
    U32 size = copy != NULL ? hostsize : insize;
    void* result;

    switch (h.kind)
    {
    case kReplace:
        *outsize = size;
        return data;

    case kWrapTemp:
        result = h.read(userdata, aid, data, size, outsize);
        iAssetWidenFree(copy);
        return result;

    case kWrapKeep:
        result = h.read(userdata, aid, data, size, outsize);
        if (copy != NULL && result != copy)
        {
            if (result == NULL)
            {
                iAssetWidenFree(copy);
            }
            else
            {
                void*& kept = sKept[result];
                if (kept != NULL)
                {
                    printf("tssm: widen: %s %08x: its result already keeps a copy\n",
                           TypeName(h.type, name), aid);
                }
                kept = copy;
            }
        }
        return result;

    default:
        return data;
    }
}

static void HookUnload(S32 k, void* userdata, U32 aid)
{
    Hook& h = sHooks[k];

    if (h.unload != NULL)
    {
        h.unload(userdata, aid);
    }

    if (iAssetWidenIsCopy(userdata))
    {
        iAssetWidenFree(userdata);
        return;
    }

    std::unordered_map<void*, void*>::iterator it = sKept.find(userdata);
    if (it != sKept.end())
    {
        iAssetWidenFree(it->second);
        sKept.erase(it);
    }
}

template <S32 K>
static void* ReadHook(void* userdata, U32 aid, void* indata, U32 insize, U32* outsize)
{
    return HookRead(K, userdata, aid, indata, insize, outsize);
}

template <S32 K>
static void UnloadHook(void* userdata, U32 aid)
{
    HookUnload(K, userdata, aid);
}

typedef void* (*ReadFn)(void*, U32, void*, U32, U32*);
typedef void (*UnloadFn)(void*, U32);

static const ReadFn sReadHooks[] = {
    ReadHook<0>, ReadHook<1>, ReadHook<2>,  ReadHook<3>,  ReadHook<4>,  ReadHook<5>,  ReadHook<6>,
    ReadHook<7>, ReadHook<8>, ReadHook<9>,  ReadHook<10>, ReadHook<11>, ReadHook<12>, ReadHook<13>,
    ReadHook<14>,
};
static const UnloadFn sUnloadHooks[] = {
    UnloadHook<0>,  UnloadHook<1>,  UnloadHook<2>,  UnloadHook<3>,  UnloadHook<4>,
    UnloadHook<5>,  UnloadHook<6>,  UnloadHook<7>,  UnloadHook<8>,  UnloadHook<9>,
    UnloadHook<10>, UnloadHook<11>, UnloadHook<12>, UnloadHook<13>, UnloadHook<14>,
};

static_assert(sizeof(sReadHooks) / sizeof(sReadHooks[0]) == sizeof(sHooks) / sizeof(sHooks[0]),
              "one read hook per type");
static_assert(sizeof(sUnloadHooks) / sizeof(sUnloadHooks[0]) ==
                  sizeof(sHooks) / sizeof(sHooks[0]),
              "one unload hook per type");

// One line at exit: what was widened, what was read as it is, and the copies
// still alive (the packages still loaded).
static void ReportAtExit()
{
    char line[1024];
    size_t n = 0;
    char name[5];

    n += snprintf(line + n, sizeof(line) - n, "tssm: widen: at exit:");
    for (S32 k = 0; k < NUM_HOOKS && n < sizeof(line); k++)
    {
        const Hook& h = sHooks[k];
        if (h.widened == 0 && h.asIs == 0)
        {
            continue;
        }
        n += snprintf(line + n, sizeof(line) - n, " %s %u", TypeName(h.type, name), h.widened);
        if (h.asIs != 0 && n < sizeof(line))
        {
            n += snprintf(line + n, sizeof(line) - n, "(+%u as is)", h.asIs);
        }
    }

    U64 disk = 0, host = 0;
    for (S32 k = 0; k < NUM_HOOKS; k++)
    {
        disk += sHooks[k].diskBytes;
        host += sHooks[k].hostBytes;
    }

    U32 live, bytes, peak;
    iAssetWidenStats(&live, &bytes, &peak);
    if (n < sizeof(line))
    {
        snprintf(line + n, sizeof(line) - n,
                 "; %llu KB widened to %llu KB; %u copies alive, %u KB (at most %u KB)",
                 (unsigned long long)(disk / 1024), (unsigned long long)(host / 1024), live,
                 bytes / 1024, peak / 1024);
    }
    printf("%s\n", line);
    fflush(stdout);
}

void iAssetTypesInstall(PKRAssetType* handlers)
{
    // The save screens' warning is a setting on every host (iSaveText.h).
    iSaveTextInstall(handlers);

    const char* env = getenv("TSSM_WIDEN");
    if (env != NULL && strcmp(env, "0") == 0)
    {
        printf("tssm: widen: off (TSSM_WIDEN=0); 64-bit asset reads will be wrong\n");
        return;
    }
    sLog = env != NULL && strcmp(env, "log") == 0;

    if (iLowHeapWanted())
    {
        gAssetWidenAlloc = LowAlloc;
        gAssetWidenFree = LowFree;
    }

    S32 installed = 0;
    S32 reported = 0;
    for (S32 k = 0; k < NUM_HOOKS; k++)
    {
        PKRAssetType* t = handlers;
        while (t->typetag != 0 && t->typetag != sHooks[k].type)
        {
            t++;
        }
        if (t->typetag == 0)
        {
            char name[5];
            printf("tssm: widen: no %s handler in the game's table\n",
                   TypeName(sHooks[k].type, name));
            continue;
        }

        sHooks[k].read = t->readXForm;
        sHooks[k].unload = t->assetUnloaded;
        if ((sHooks[k].kind == kWrapTemp || sHooks[k].kind == kWrapKeep) && t->readXForm == NULL)
        {
            char name[5];
            printf("tssm: widen: %s has no readXForm to wrap\n", TypeName(sHooks[k].type, name));
            continue;
        }

        t->readXForm = sReadHooks[k];
        t->assetUnloaded = sUnloadHooks[k];
        if (sHooks[k].kind == kReport)
        {
            reported++;
        }
        else
        {
            installed++;
        }
    }

    printf("tssm: widen: copy-at-load for %d asset types (64-bit host); %d more are reported if "
           "loaded\n",
           installed, reported);
    atexit(ReportAtExit);
}

#endif
