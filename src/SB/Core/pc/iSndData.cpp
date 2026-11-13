#include "iSndData.h"
#include "iSndXbox.h"

#include "iFile.h"
#include "xFile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TSSM-ADAPTED from treedome's pc/iSndData.cpp (CC0): the cache is
// treedome's, the reading and decoding are TSSM's (iSndXbox.cpp).
//
// Reading is LAZY: a sound is read and decoded the first time it is played and
// kept after, rather than a scene's whole sound layer at load. The cache is
// capped and evicts by least-recently-used, and an entry a voice is
// playing from is pinned: that is the promise iSndHost.h makes the backend
// about the pointer it was handed.
//
// Decoding is whole-asset, which suits sound effects: the longest SND on the
// disc is 6.4 s and decodes in a few milliseconds. Streams (SNDS:
// music, speech, ambience) do not come through here -- iSnd.cpp reads and
// decodes them a few seconds ahead of the mixer, since decoding a whole track
// (the menu's longest is 192 s of stereo) would stall the frame that starts it.
// Only a stream whose package will not open for streaming falls back to this.

#define ISNDDATA_MAX_ENTRIES 1024
#define ISNDDATA_DEFAULT_BUDGET_MB 256

struct snd_entry
{
    U32 aid;
    U32 pathHash;
    U32 bytes;
    void* data;
    S32 refs;
    U64 stamp;
};

static snd_entry sEntries[ISNDDATA_MAX_ENTRIES];
static U32 sEntryCount;
static U64 sTotalBytes;
static U64 sStampCounter;
static U64 sBudgetBytes;
static bool sBudgetRead;

static bool sSaidUnreadable;

static bool sDumpRead;
static const char* sDumpDir;

const char* iSndDataDumpDir()
{
    if (!sDumpRead)
    {
        sDumpRead = true;
        const char* env = getenv("TSSM_SND_DUMP");
        sDumpDir = (env != NULL && env[0] != '\0') ? env : NULL;
    }

    return sDumpDir;
}

static U64 iBudget()
{
    if (!sBudgetRead)
    {
        sBudgetRead = true;
        sBudgetBytes = (U64)ISNDDATA_DEFAULT_BUDGET_MB * 1024 * 1024;

        const char* env = getenv("TSSM_SNDCACHE");
        if (env != NULL)
        {
            int mb = atoi(env);
            if (mb > 0)
            {
                sBudgetBytes = (U64)mb * 1024 * 1024;
            }
        }
    }

    return sBudgetBytes;
}

// FNV-1a of the package path, case-folded, with either slash.
static U32 iPathHash(const char* path)
{
    U32 h = 2166136261u;
    for (const char* p = path; p != NULL && *p != '\0'; p++)
    {
        U8 c = (U8)*p;
        if (c >= 'A' && c <= 'Z')
        {
            c = (U8)(c - 'A' + 'a');
        }
        if (c == '\\')
        {
            c = '/';
        }
        h = (h ^ c) * 16777619u;
    }
    return h;
}

static snd_entry* iFindEntry(U32 aid, U32 pathHash)
{
    for (U32 i = 0; i < sEntryCount; i++)
    {
        if (sEntries[i].aid == aid && sEntries[i].pathHash == pathHash)
        {
            return &sEntries[i];
        }
    }

    return NULL;
}

static void iDropEntry(U32 index)
{
    free(sEntries[index].data);
    sTotalBytes -= sEntries[index].bytes;

    // Order does not matter; the array is searched linearly.
    sEntries[index] = sEntries[sEntryCount - 1];
    sEntryCount--;
}

// Make room for `wanted` more bytes by dropping the least recently used entries
// no voice is playing from. False if everything left is pinned, in which case
// the caller adds anyway rather than refuse a sound over a memory target.
static bool iMakeRoom(U64 wanted)
{
    U64 budget = iBudget();

    while (sEntryCount > 0 && sTotalBytes + wanted > budget)
    {
        U32 victim = ISNDDATA_MAX_ENTRIES;
        U64 oldest = 0;

        for (U32 i = 0; i < sEntryCount; i++)
        {
            if (sEntries[i].refs > 0)
            {
                continue;
            }

            if (victim == ISNDDATA_MAX_ENTRIES || sEntries[i].stamp < oldest)
            {
                victim = i;
                oldest = sEntries[i].stamp;
            }
        }

        if (victim == ISNDDATA_MAX_ENTRIES)
        {
            return false;
        }

        iDropEntry(victim);
    }

    return true;
}

// The asset's bytes, from its package. NULL if the package will not open or
// the read is short.
static void* iReadAsset(const iSndDataSource* src)
{
    if (src->path == NULL || src->size == 0)
    {
        return NULL;
    }

    tag_xFile file;
    memset(&file, 0, sizeof(file));
    if (iFileOpen(src->path, IFILE_OPEN_READ, &file) != 0)
    {
        return NULL;
    }

    void* mem = NULL;

    if (iFileSeek(&file, (S32)src->offset, IFILE_SEEK_SET) >= 0)
    {
        mem = malloc(src->size);
        if (mem != NULL && iFileRead(&file, mem, src->size) != src->size)
        {
            free(mem);
            mem = NULL;
        }
    }

    iFileClose(&file);
    return mem;
}

static void iDump(U32 aid, const void* pcm, U32 bytes, const iSndDataSource* src, U32 rate)
{
    const char* dir = iSndDataDumpDir();
    if (dir == NULL || pcm == NULL)
    {
        return;
    }

    U32 ch = src->channels != 0 ? src->channels : 1;
    char path[600];
    snprintf(path, sizeof(path), "%s/%08x.wav", dir, aid);
    iSndXboxWriteWav(path, (const S16*)pcm, bytes / (2 * ch), ch, rate);
}

const void* iSndDataAcquire(U32 assetID, const iSndDataSource* src, U32* bytes)
{
    if (bytes != NULL)
    {
        *bytes = 0;
    }

    if (assetID == 0 || src == NULL)
    {
        return NULL;
    }

    U32 pathHash = iPathHash(src->path);
    snd_entry* e = iFindEntry(assetID, pathHash);

    if (e == NULL)
    {
        if (sEntryCount >= ISNDDATA_MAX_ENTRIES && !iMakeRoom((U64)-1))
        {
            return NULL;
        }

        U32 got = 0;
        void* pcm = NULL;
        void* raw = iReadAsset(src);

        if (raw != NULL)
        {
            pcm = iSndXboxDecode(raw, src->size, src->format_tag, src->channels, src->block_align,
                                 src->bits, &got);
            free(raw);
        }

        if (pcm == NULL)
        {
            // Once: a build that cannot find its sounds says so, and one that
            // can does not chatter.
            if (!sSaidUnreadable)
            {
                sSaidUnreadable = true;
                printf("iSndData: could not read or decode sound %08x (%s at %u, %u bytes, "
                       "format %#x); it plays silently, as do any others like it\n",
                       assetID, src->path != NULL ? src->path : "?", src->offset, src->size,
                       src->format_tag);
                fflush(stdout);
            }
            return NULL;
        }

        iMakeRoom(got);

        e = &sEntries[sEntryCount++];
        e->aid = assetID;
        e->pathHash = pathHash;
        e->bytes = got;
        e->data = pcm;
        e->refs = 0;
        sTotalBytes += got;

        iDump(assetID, pcm, got, src, src->sample_rate);
    }

    e->refs++;
    e->stamp = ++sStampCounter;

    if (bytes != NULL)
    {
        *bytes = e->bytes;
    }

    return e->data;
}

void iSndDataRelease(const void* data)
{
    if (data == NULL)
    {
        return;
    }

    for (U32 i = 0; i < sEntryCount; i++)
    {
        if (sEntries[i].data == data)
        {
            if (sEntries[i].refs > 0)
            {
                sEntries[i].refs--;
            }
            return;
        }
    }
}

void iSndDataReset()
{
    for (U32 i = 0; i < sEntryCount; i++)
    {
        free(sEntries[i].data);
    }

    memset(sEntries, 0, sizeof(sEntries));
    sEntryCount = 0;
    sTotalBytes = 0;
    sStampCounter = 0;
    sSaidUnreadable = false;
}

void iSndDataStats(U32* entries, U32* bytes, U32* pinned)
{
    if (entries != NULL)
    {
        *entries = sEntryCount;
    }

    if (bytes != NULL)
    {
        *bytes = (U32)sTotalBytes;
    }

    if (pinned != NULL)
    {
        U32 n = 0;
        for (U32 i = 0; i < sEntryCount; i++)
        {
            if (sEntries[i].refs > 0)
            {
                n++;
            }
        }
        *pinned = n;
    }
}
