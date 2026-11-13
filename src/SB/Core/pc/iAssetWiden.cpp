#include "iAssetWiden.h"

// The copy-at-load transforms: disk layout in, the host's layout out.
// iAssetWiden.h says why and how they are hooked; iAssetHooks.cpp hooks them.
//
// Every transform reads the disk bytes by offset (the layout the Xbox data was
// written with, which is the 32-bit layout of the game's own structs) and
// builds the host's structs. A struct is copied in runs: the bytes between two
// pointer slots keep their order and spacing at both widths, so each run is
// copied whole and each 4-byte slot becomes an 8-byte one at the host's
// offsetof(). The disk offsets below are checked against the structs when this
// file is compiled for a 32-bit host (DISK_AT, DISK_SIZE), where the struct is
// the disk layout.
//
// Each transform runs twice: once to measure (no buffer) and once to write.

#include "zAssetTypes.h"
#include "xSnd.h"
#include "zLOD.h"
#include "zCollGeom.h"
#include "xCM.h"
#include "zDestructible.h"
#include "zShrapnel.h"
#include "xLightKit.h"
#include "zScene.h"
#include "xSplineAsset.h"
#include "xJSP.h"
#include "xClumpColl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unordered_set>
#include <vector>

#define WIDE_64 (sizeof(void*) == 8)

// The disk offset of a field, checked where the struct is the disk layout.
#define DISK_AT(T, field, off)                                                                     \
    static_assert(WIDE_64 || offsetof(T, field) == (off), #T "::" #field " is not at " #off)
#define DISK_SIZE(T, size) static_assert(WIDE_64 || sizeof(T) == (size), #T " is not " #size)

// ---------------------------------------------------------------------------
// The copies

static void* DefaultAlloc(size_t n)
{
    return malloc(n);
}

static void DefaultFree(void* p)
{
    free(p);
}

// iAssetHooks.cpp points these at RenderWare's low heap (below 4 GB).
void* (*gAssetWidenAlloc)(size_t) = DefaultAlloc;
void (*gAssetWidenFree)(void*) = DefaultFree;

// In front of each copy, so it can be freed from the pointer the game has.
struct CopyHeader
{
    void* raw;
    U32 size;
    U32 magic;
};

#define COPY_MAGIC 0x57494445 // 'WIDE'

static std::unordered_set<const void*> sLive;
static U32 sLiveBytes;
static U32 sPeakBytes;

static CopyHeader* HeaderOf(void* p)
{
    return (CopyHeader*)(((UPtr)p - sizeof(CopyHeader)) & ~(UPtr)7);
}

// A buffer of `size` bytes whose address is `phase` past a multiple of 16, so
// that a struct the game finds at `phase + n * 8` bytes in is 8-aligned (LODT
// and COLL rows start 4 bytes in, ATBL's raw pointers 20).
static U8* CopyAlloc(U32 size, U32 phase)
{
    // The header, then up to 15 bytes to the next multiple of 16, then the phase.
    U8* raw = (U8*)gAssetWidenAlloc(size + 32 + 15 + 8);
    if (raw == NULL)
    {
        return NULL;
    }

    U8* p = (U8*)(((UPtr)raw + 32 + 15) & ~(UPtr)15) + phase;
    CopyHeader* h = HeaderOf(p);
    h->raw = raw;
    h->size = size;
    h->magic = COPY_MAGIC;

    memset(p, 0, size);
    sLive.insert(p);
    sLiveBytes += size;
    if (sLiveBytes > sPeakBytes)
    {
        sPeakBytes = sLiveBytes;
    }
    return p;
}

void iAssetWidenFree(void* copy)
{
    if (copy == NULL || sLive.erase(copy) == 0)
    {
        return;
    }

    CopyHeader* h = HeaderOf(copy);
    sLiveBytes -= h->size;
    h->magic = 0;
    gAssetWidenFree(h->raw);
}

S32 iAssetWidenIsCopy(const void* p)
{
    return p != NULL && sLive.count(p) != 0;
}

void iAssetWidenStats(U32* live, U32* bytes, U32* peakBytes)
{
    *live = (U32)sLive.size();
    *bytes = sLiveBytes;
    *peakBytes = sPeakBytes;
}

#ifdef TSSM_PTR64
// iPtr32's table of pointers above 4 GB (compat/types.h). Entries are never
// removed: what goes in is the executable's statics.
static const void* sFar[IPTR32_FAR_MAX];
static U32 sFarCount;

U32 iPtr32Far(const void* p)
{
    for (U32 i = 0; i < sFarCount; i++)
    {
        if (sFar[i] == p)
        {
            return IPTR32_FAR_BASE + IPTR32_FAR_STEP * i;
        }
    }

    if (sFarCount == IPTR32_FAR_MAX)
    {
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            printf("tssm: iPtr32: no room for %p (above 4 GB) in a 4-byte slot; truncated\n", p);
        }
        return (U32)(UPtr)p;
    }

    sFar[sFarCount] = p;
    printf("tssm: iPtr32: %p (above 4 GB) kept as handle %u\n", p, sFarCount);
    return IPTR32_FAR_BASE + IPTR32_FAR_STEP * sFarCount++;
}

void* iPtr32FarGet(U32 handle)
{
    U32 i = (handle - IPTR32_FAR_BASE) / IPTR32_FAR_STEP;
    if ((handle - IPTR32_FAR_BASE) % IPTR32_FAR_STEP != 0 || i >= sFarCount)
    {
        return (void*)(UPtr)handle;
    }
    return (void*)sFar[i];
}
#endif

// ---------------------------------------------------------------------------
// Reading the disk layout and writing the host's

namespace
{
    struct In
    {
        const U8* p;
        U32 size;

        bool has(U32 off, U32 n) const
        {
            return off <= size && n <= size - off;
        }

        U32 u32(U32 off) const
        {
            U32 v;
            memcpy(&v, p + off, 4);
            return v;
        }

        U16 u16(U32 off) const
        {
            U16 v;
            memcpy(&v, p + off, 2);
            return v;
        }
    };

    // Writes at `pos` bytes from the copy's start; measures only, when `base`
    // is NULL.
    struct Out
    {
        U8* base;
        U32 pos;
        U32 phase;

        U32 put(const void* src, U32 n)
        {
            U32 at = pos;
            if (base != NULL && n != 0)
            {
                memcpy(base + pos, src, n);
            }
            pos += n;
            return at;
        }

        U32 skip(U32 n)
        {
            U32 at = pos;
            pos += n; // the copy is zeroed
            return at;
        }

        // Aligns the next write to `a` bytes of address.
        void align(U32 a)
        {
            while ((phase + pos) % a)
            {
                pos++;
            }
        }

        U8* at(U32 off) const
        {
            return base != NULL ? base + off : NULL;
        }

        void ptr(U32 off, UPtr value)
        {
            if (base != NULL)
            {
                memcpy(base + off, &value, sizeof(value));
            }
        }

        void word(U32 off, U32 value)
        {
            if (base != NULL)
            {
                memcpy(base + off, &value, 4);
            }
        }
    };

    // A 4-byte pointer slot of a struct: where it is on disk, and where the
    // host's struct has it.
    struct Slot
    {
        U32 disk;
        U32 host;
    };

#define SLOT(T, field, off) { (off), (U32)offsetof(T, field) }

    // One record of `diskSize` bytes into `hostSize`, every slot widened (the
    // disk value zero-extended).
    U32 Record(Out& o, const In& in, U32 at, U32 diskSize, U32 hostSize, const Slot* slots, S32 n)
    {
        U32 start = o.skip(hostSize);
        U8* dst = o.at(start);
        if (dst == NULL)
        {
            return start;
        }

        const U8* src = in.p + at;
        U32 d = 0;
        U32 h = 0;
        for (S32 i = 0; i < n; i++)
        {
            memcpy(dst + h, src + d, slots[i].disk - d);
            UPtr v;
            U32 v32;
            memcpy(&v32, src + slots[i].disk, 4);
            v = v32;
            memcpy(dst + slots[i].host, &v, sizeof(v));
            d = slots[i].disk + 4;
            h = slots[i].host + sizeof(void*);
        }
        memcpy(dst + h, src + d, diskSize - d);
        return start;
    }
} // namespace

#define FAIL(why)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        *reason = why;                                                                             \
        return false;                                                                              \
    } while (0)

// ---------------------------------------------------------------------------
// ATBL: an animation table. ATBL_Read resolves in place: the raw ids into
// pointers (zaRaw), each file's RawData byte offset into a pointer, and the
// U32 indices that offset points at into pointers. Effects are found by byte
// offsets from the asset's start.
//
//     disk                               host
//     xAnimAssetTable (20)               the same
//     U32 raw id x NumRaw                void* x NumRaw (the id zero-extended)
//     xAnimAssetFile x NumFiles (32)     xAnimAssetFile (40); RawData = the
//                                        offset of its index array below
//     xAnimAssetState x NumStates (28)   the same; EffectOffset moved
//     the rest (index arrays, effects)   the same bytes
//                                        void* index arrays, one per file

DISK_SIZE(xAnimAssetTable, 20);
DISK_SIZE(xAnimAssetFile, 32);
DISK_AT(xAnimAssetFile, RawData, 16);
DISK_SIZE(xAnimAssetState, 28);
DISK_AT(xAnimAssetState, EffectOffset, 12);

static const Slot kATBLFileSlots[] = { SLOT(xAnimAssetFile, RawData, 16) };

static bool WidenATBL(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 20))
    {
        FAIL("shorter than its table header");
    }

    U32 numRaw = in.u32(4);
    U32 numFiles = in.u32(8);
    U32 numStates = in.u32(12);
    U32 diskRaw = 20;
    U32 diskFiles = diskRaw + 4 * numRaw;
    U32 diskStates = diskFiles + 32 * numFiles;
    U32 diskTail = diskStates + 28 * numStates;
    if (numRaw > 0x10000 || numFiles > 0x10000 || numStates > 0x10000 || !in.has(0, diskTail))
    {
        FAIL("counts larger than the asset");
    }

    o.put(in.p, 20);

    for (U32 i = 0; i < numRaw; i++)
    {
        UPtr id = in.u32(diskRaw + 4 * i);
        o.put(&id, sizeof(id));
    }

    U32 hostFiles = o.pos;
    for (U32 i = 0; i < numFiles; i++)
    {
        Record(o, in, diskFiles + 32 * i, 32, sizeof(xAnimAssetFile), kATBLFileSlots, 1);
    }

    U32 hostStates = o.put(in.p + diskStates, 28 * numStates);
    U32 hostTail = o.put(in.p + diskTail, in.size - diskTail);

    for (U32 i = 0; i < numStates; i++)
    {
        U32 rec = diskStates + 28 * i;
        U32 effectCount = in.u32(rec + 8);
        U32 effectOffset = in.u32(rec + 12);
        if (effectCount != 0)
        {
            if (effectOffset < diskTail || effectOffset >= in.size)
            {
                FAIL("an effect outside the asset's tail");
            }
            o.word(hostStates + 28 * i + 12, effectOffset - diskTail + hostTail);
        }
    }

    for (U32 i = 0; i < numFiles; i++)
    {
        U32 rec = diskFiles + 32 * i;
        U32 count = (U32)in.u16(rec + 12) * in.u16(rec + 14);
        U32 indexOffset = in.u32(rec + 16);
        if (!in.has(indexOffset, 4 * count))
        {
            FAIL("a file's raw index array outside the asset");
        }

        o.align(8);
        U32 hostIndex = o.pos;
        for (U32 k = 0; k < count; k++)
        {
            UPtr index = in.u32(indexOffset + 4 * k);
            o.put(&index, sizeof(index));
        }
        o.ptr(hostFiles + sizeof(xAnimAssetFile) * i + offsetof(xAnimAssetFile, RawData), hostIndex);
    }

    return true;
}

// ---------------------------------------------------------------------------
// SGRP: a sound group, xSndGroupHeader (pszGroupName at the end) and its
// xSndGroupInfo entries.

DISK_SIZE(xSndGroupHeader, 32);
DISK_AT(xSndGroupHeader, pszGroupName, 28);
DISK_SIZE(xSndGroupInfo, 16);

static const Slot kSGRPSlots[] = { SLOT(xSndGroupHeader, pszGroupName, 28) };

static bool WidenSGRP(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 32))
    {
        FAIL("shorter than its header");
    }

    Record(o, in, 0, 32, sizeof(xSndGroupHeader), kSGRPSlots, 1);
    o.put(in.p + 32, in.size - 32);
    return true;
}

// ---------------------------------------------------------------------------
// LODT and COLL: a row count, then rows of model asset ids that zLOD and
// zCollGeom resolve in place into bucket and atomic pointers. The rows start 4
// bytes in, which is 8-aligned in the copy (phase 4).

DISK_SIZE(zLODTable, 36);
DISK_AT(zLODTable, baseBucket, 0);
DISK_AT(zLODTable, noRenderDist, 4);
DISK_AT(zLODTable, lodBucket, 12);
DISK_AT(zLODTable, lodDist, 24);

static const Slot kLODTSlots[] = { SLOT(zLODTable, baseBucket, 0), SLOT(zLODTable, lodBucket[0], 12),
                                   SLOT(zLODTable, lodBucket[1], 16),
                                   SLOT(zLODTable, lodBucket[2], 20) };

DISK_SIZE(zCollGeomTable, 12);
DISK_AT(zCollGeomTable, colModel, 4);
DISK_AT(zCollGeomTable, camcolModel, 8);

static const Slot kCOLLSlots[] = { SLOT(zCollGeomTable, baseModel, 0),
                                   SLOT(zCollGeomTable, colModel[0], 4),
                                   SLOT(zCollGeomTable, camcolModel, 8) };

static bool WidenRows(Out& o, const In& in, U32 diskRow, U32 hostRow, const Slot* slots, S32 n,
                      const char** reason)
{
    if (!in.has(0, 4))
    {
        FAIL("shorter than its row count");
    }

    U32 count = in.u32(0);
    if (count > 0x100000 || !in.has(4, count * diskRow))
    {
        FAIL("more rows than the asset holds");
    }

    o.put(in.p, 4);
    for (U32 i = 0; i < count; i++)
    {
        Record(o, in, 4 + diskRow * i, diskRow, hostRow, slots, n);
    }
    U32 tail = 4 + count * diskRow;
    o.put(in.p + tail, in.size - tail);
    return true;
}

// ---------------------------------------------------------------------------
// CRDT: credits. An xCMheader, then credits blocks: xCMcredits, its presets,
// and hunks (xCMhunk and its text) up to credits_size. The body is encrypted
// on disk (state bit 1; xCMstart decrypts it on first use) and the hunks' text
// pointers are byte offsets from the asset's start (state bit 0; xCMprep turns
// them into pointers and back). The copy is decrypted (bit 1 cleared), every
// hunk is 8 bytes longer, and the offsets point at the same text in the copy.
//
// A picture preset (align 4) is read as an xCMtexture over box[0], whose
// texture slot xCMrender fills on first use. The host's 8-byte pointer there
// takes in the 4 bytes of pad after the slot, which the data sets (0.1f), so
// the copy clears both: the texture is looked up as on the console.

DISK_SIZE(xCMheader, 24);
DISK_SIZE(xCMcredits, 56);
DISK_SIZE(xCMpreset, 76);
DISK_AT(xCMpreset, box, 12);
DISK_SIZE(xCMhunk, 24);
DISK_AT(xCMhunk, text1, 16);
DISK_AT(xCMhunk, text2, 20);
DISK_SIZE(xCMtexture, 32);
DISK_AT(xCMtexture, texture, 24);

// The presets are copied as they are, and the widened slot stays inside box[0].
static_assert(sizeof(xCMpreset) == 76, "xCMpreset is not the disk layout");
static_assert(offsetof(xCMtexture, texture) + sizeof(void*) <= sizeof(xCMtextbox),
              "xCMtexture::texture is not inside box[0]");

static const Slot kHunkSlots[] = { SLOT(xCMhunk, text1, 16), SLOT(xCMhunk, text2, 20) };

// xCM.cpp's key, as xCMstart uses it (12 bytes of it).
static const char kCreditsKey[] = "xCMChunkHandler";

namespace
{
    struct Run
    {
        U32 disk;
        U32 end;
        U32 host;
    };
} // namespace

static bool WidenCRDT(Out& o, const In& diskIn, const char** reason)
{
    if (!diskIn.has(0, 24) || diskIn.u32(0) != 0xBEEEEEEF)
    {
        *reason = ""; // not credits data: xCMstart ignores it too
        return false;
    }

    // Decrypted, as xCMstart would (Decrypt in xCM.cpp).
    std::vector<U8> plain(diskIn.p, diskIn.p + diskIn.size);
    U32 state = diskIn.u32(12);
    U32 total = diskIn.u32(20);
    if (total > diskIn.size || total < 24)
    {
        FAIL("total_size outside the asset");
    }
    if (state & 0x2)
    {
        U8 last = 0;
        U32 key = 0;
        for (U32 i = 24; i < total; i++)
        {
            plain[i] = plain[i] ^ last ^ (U8)kCreditsKey[key];
            last = plain[i];
            key = (key + 1) % 12;
        }
    }
    In in = { plain.data(), diskIn.size };

    std::vector<Run> runs;
    std::vector<U32> hunks; // host offsets of the hunks with text offsets to move

    U32 hostHeader = o.put(in.p, 24);
    runs.push_back({ 0, 24, hostHeader });

    U32 dp = 24;
    U32 hunkTotal = 0;
    while (dp < total)
    {
        if (!in.has(dp, 56))
        {
            FAIL("a credits block past the end");
        }
        U32 creditsSize = in.u32(dp);
        U32 numPresets = in.u32(dp + 52);
        U32 pp = dp + 56;
        U32 hp = pp + 76 * numPresets;
        if (creditsSize < 56 || numPresets > 0x10000 || !in.has(dp, creditsSize) || hp > dp + creditsSize)
        {
            FAIL("a credits block larger than the asset");
        }

        U32 hostCredits = o.put(in.p + dp, hp - dp);
        runs.push_back({ dp, hp, hostCredits });

        for (U32 i = 0; i < numPresets; i++)
        {
            if (in.u16(pp + 76 * i + 2) == 4)
            {
                o.ptr(hostCredits + 56 + 76 * i + offsetof(xCMpreset, box) +
                          offsetof(xCMtexture, texture),
                      0);
            }
        }

        U32 hunkCount = 0;
        while (hp - dp < creditsSize)
        {
            if (!in.has(hp, 24))
            {
                FAIL("a hunk past the end");
            }
            U32 hunkSize = in.u32(hp);
            U32 preset = in.u32(hp + 4);
            if (hunkSize < 24 || !in.has(hp, hunkSize) || preset >= numPresets)
            {
                FAIL("a hunk with a bad size or preset");
            }

            U32 hostHunk = Record(o, in, hp, 24, sizeof(xCMhunk), kHunkSlots, 2);
            o.word(hostHunk, hunkSize - 24 + sizeof(xCMhunk));
            U32 hostText = o.put(in.p + hp + 24, hunkSize - 24);
            runs.push_back({ hp + 24, hp + hunkSize, hostText });

            U16 align = in.u16(pp + 76 * preset + 2);
            if (align <= 3 && (state & 0x1))
            {
                hunks.push_back(hostHunk);
            }

            hp += hunkSize;
            hunkCount++;
        }

        o.word(hostCredits, creditsSize + hunkCount * (sizeof(xCMhunk) - 24));
        hunkTotal += hunkCount;
        dp = hp;
    }

    U32 hostEnd = o.put(in.p + total, in.size - total);
    runs.push_back({ total, in.size, hostEnd });

    o.word(hostHeader + 12, state & ~0x2);
    o.word(hostHeader + 20, total + hunkTotal * (sizeof(xCMhunk) - 24));

    // The texts, by offset from the asset's start, to the same bytes in the copy.
    if (o.base != NULL)
    {
        for (size_t h = 0; h < hunks.size(); h++)
        {
            xCMhunk* hunk = (xCMhunk*)(o.base + hunks[h]);
            char** texts[2] = { &hunk->text1, &hunk->text2 };
            for (S32 t = 0; t < 2; t++)
            {
                UPtr off = (UPtr)*texts[t];
                if (off == 0)
                {
                    continue;
                }
                bool moved = false;
                for (size_t r = 0; r < runs.size(); r++)
                {
                    if (off >= runs[r].disk && off < runs[r].end)
                    {
                        *texts[t] = (char*)(UPtr)(off - runs[r].disk + runs[r].host);
                        moved = true;
                        break;
                    }
                }
                if (!moved)
                {
                    FAIL("a text offset outside the copied text");
                }
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// DEST: a destructible. The header's last field is `states`, and the states
// are stored from there: each an xDestructibleAssetState whose last field
// (`animlist`, a pointer in the struct) holds the animation count, followed by
// that many animation ids. zDestructible reads the ids at
// &animlist->animationIDs and steps to the next state from `state + 1`, which
// on a 64-bit host are both 8 bytes further than on disk: the count gets 4
// bytes of padding after it.

DISK_SIZE(xDestructibleAsset, 44);
DISK_AT(xDestructibleAsset, states, 40);
DISK_SIZE(xDestructibleAssetState, 52);
DISK_AT(xDestructibleAssetState, animlist, 48);

static bool WidenDEST(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 40))
    {
        FAIL("shorter than its header");
    }

    U32 nstates = in.u32(4);
    o.put(in.p, 40);

    U32 d = 40;
    for (U32 i = 0; i < nstates; i++)
    {
        if (!in.has(d, 52))
        {
            FAIL("a state past the end");
        }
        U32 nanim = in.u32(d + 48);
        if (nanim > 0x10000 || !in.has(d + 52, 4 * nanim))
        {
            FAIL("an animation list past the end");
        }

        // The 48 bytes of fields and the count at offsetof(animlist), the ids
        // at offsetof(animationIDs) past it, which is also sizeof(state).
        U32 host = o.skip(sizeof(xDestructibleAssetState));
        if (o.base != NULL)
        {
            memcpy(o.base + host, in.p + d, 52);
        }
        o.put(in.p + d + 52, 4 * nanim);
        d += 52 + 4 * nanim;
    }

    static_assert(offsetof(xDestructibleAssetState, animlist) +
                          offsetof(xDestructibleAssetAttachedAnimList, animationIDs) ==
                      sizeof(xDestructibleAssetState),
                  "the ids follow the state");

    o.put(in.p + d, in.size - d);
    return true;
}

// ---------------------------------------------------------------------------
// SHRP: a shrapnel asset, zShrapnelAsset and fassetCount fragment records of
// the type each one names. Projectile, particle and shrapnel records hold
// pointers zShrapnel_SceneInit fills in; the others hold none and keep their
// size. A record of a type not listed stops the copy: the rest is copied as it
// is (zShrapnel_SceneInit could not walk past it either).

DISK_SIZE(zShrapnelAsset, 12);
DISK_AT(zShrapnelAsset, initCB, 8);
DISK_SIZE(zFragAsset, 24);
DISK_SIZE(zFragProjectileAsset, 272);
DISK_AT(zFragProjectileAsset, modelFile, 28);
DISK_AT(zFragProjectileAsset, child, 248);
DISK_AT(zFragProjectileAsset, scaleCurve, 264);
DISK_SIZE(zFragParticleAsset, 504);
DISK_AT(zFragParticleAsset, emit.emit_volume, 488);
DISK_AT(zFragParticleAsset, parEmitter, 500);
DISK_SIZE(zFragShrapnelAsset, 32);
DISK_AT(zFragShrapnelAsset, shrapAsset, 28);
DISK_SIZE(zFragLightningAsset, 112);
DISK_SIZE(zFragSoundAsset, 68);
DISK_SIZE(zFragShockwaveAsset, 84);
DISK_SIZE(zFragExplosionAsset, 72);
DISK_SIZE(zFragDistortionAsset, 92);
DISK_SIZE(zFragFireAsset, 92);

static const Slot kShrapnelSlots[] = { SLOT(zShrapnelAsset, initCB, 8) };
static const Slot kProjectileSlots[] = { SLOT(zFragProjectileAsset, modelFile, 28),
                                         SLOT(zFragProjectileAsset, child, 248),
                                         SLOT(zFragProjectileAsset, scaleCurve, 264) };
static const Slot kParticleSlots[] = { SLOT(zFragParticleAsset, emit.emit_volume, 488),
                                       SLOT(zFragParticleAsset, parEmitter, 500) };
static const Slot kShrapnelFragSlots[] = { SLOT(zFragShrapnelAsset, shrapAsset, 28) };

namespace
{
    struct FragKind
    {
        U32 type;
        U32 diskSize;
        U32 hostSize;
        const Slot* slots;
        S32 numSlots;
    };
} // namespace

static const FragKind kFrags[] = {
    { eFragShrapnel, 32, sizeof(zFragShrapnelAsset), kShrapnelFragSlots, 1 },
    { eFragParticle, 504, sizeof(zFragParticleAsset), kParticleSlots, 2 },
    { eFragProjectile, 272, sizeof(zFragProjectileAsset), kProjectileSlots, 3 },
    { eFragLightning, 112, sizeof(zFragLightningAsset), NULL, 0 },
    { eFragSound, 68, sizeof(zFragSoundAsset), NULL, 0 },
    { eFragShockwave, 84, sizeof(zFragShockwaveAsset), NULL, 0 },
    { eFragExplosion, 72, sizeof(zFragExplosionAsset), NULL, 0 },
    { eFragDistortion, 92, sizeof(zFragDistortionAsset), NULL, 0 },
    { eFragFire, 92, sizeof(zFragFireAsset), NULL, 0 },
};

static bool WidenSHRP(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 12))
    {
        FAIL("shorter than its header");
    }

    S32 count = (S32)in.u32(0);
    Record(o, in, 0, 12, sizeof(zShrapnelAsset), kShrapnelSlots, 1);

    U32 d = 12;
    for (S32 i = 0; i < count; i++)
    {
        if (!in.has(d, 4))
        {
            FAIL("a fragment past the end");
        }
        U32 type = in.u32(d);
        const FragKind* kind = NULL;
        for (size_t k = 0; k < sizeof(kFrags) / sizeof(kFrags[0]); k++)
        {
            if (kFrags[k].type == type)
            {
                kind = &kFrags[k];
                break;
            }
        }
        if (kind == NULL)
        {
            break;
        }
        if (!in.has(d, kind->diskSize))
        {
            FAIL("a fragment past the end");
        }

        // Back to back, as zShrapnel_SceneInit steps by sizeof: a record with
        // pointers that follows one whose size is not a multiple of 8 (sound,
        // distortion, fire) is only 4-aligned, which x86-64 and arm64 read
        // without complaint.
        Record(o, in, d, kind->diskSize, kind->hostSize, kind->slots, kind->numSlots);
        d += kind->diskSize;
    }

    o.put(in.p + d, in.size - d);
    return true;
}

// ---------------------------------------------------------------------------
// LKIT: a light kit, xLightKit (lightList) and lightCount xLightKitLight
// records (platLight), which xLightKit_Prepare finds right after the header.

DISK_SIZE(xLightKit, 16);
DISK_AT(xLightKit, lightList, 12);
DISK_SIZE(xLightKitLight, 96);
DISK_AT(xLightKitLight, platLight, 92);

static const Slot kLightKitSlots[] = { SLOT(xLightKit, lightList, 12) };
static const Slot kLightSlots[] = { SLOT(xLightKitLight, platLight, 92) };

static bool WidenLKIT(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 16))
    {
        FAIL("shorter than its header");
    }

    U32 count = in.u32(8);
    if (count > 0x10000 || !in.has(16, 96 * count))
    {
        FAIL("more lights than the asset holds");
    }

    Record(o, in, 0, 16, sizeof(xLightKit), kLightKitSlots, 1);
    for (U32 i = 0; i < count; i++)
    {
        Record(o, in, 16 + 96 * i, 96, sizeof(xLightKitLight), kLightSlots, 1);
    }
    U32 tail = 16 + 96 * count;
    o.put(in.p + tail, in.size - tail);
    return true;
}

// ---------------------------------------------------------------------------
// DYNA 0xFABDB3B3: the scene parameters (zScene.cpp). Its idle extras are byte
// offsets, from idle03ExtraCount, to arrays of animation ids that zScene turns
// into xAnimFile pointers in place. Every other DYNA is read as it is.

#define DYNA_SCENE_PARAMETERS 0xFABDB3B3

DISK_SIZE(zSceneParameters, 68);
DISK_AT(zSceneParameters, idle03ExtraCount, 16);
DISK_AT(zSceneParameters, idle03Extras, 20);
DISK_AT(zSceneParameters, idle04ExtraCount, 24);
DISK_AT(zSceneParameters, idle04Extras, 28);

static const Slot kSceneParamSlots[] = { SLOT(zSceneParameters, idle03Extras, 20),
                                         SLOT(zSceneParameters, idle04Extras, 28) };

static bool WidenDYNA(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 16) || in.u32(8) != DYNA_SCENE_PARAMETERS)
    {
        *reason = ""; // another subtype: read as it is
        return false;
    }
    if (!in.has(0, 68))
    {
        FAIL("scene parameters shorter than zSceneParameters");
    }

    U32 host = Record(o, in, 0, 68, sizeof(zSceneParameters), kSceneParamSlots, 2);
    o.put(in.p + 68, in.size - 68);

    // The arrays, moved behind everything else.
    const U32 counts[2] = { in.u32(16), in.u32(24) };
    const U32 offsets[2] = { in.u32(20), in.u32(28) };
    const U32 slots[2] = { (U32)offsetof(zSceneParameters, idle03Extras),
                           (U32)offsetof(zSceneParameters, idle04Extras) };
    const U32 base = (U32)offsetof(zSceneParameters, idle03ExtraCount);
    for (S32 a = 0; a < 2; a++)
    {
        if (counts[a] == 0)
        {
            continue;
        }
        U32 at = 16 + offsets[a];
        if (counts[a] > 0x1000 || !in.has(at, 4 * counts[a]))
        {
            FAIL("an idle-extras array outside the asset");
        }
        o.align(8);
        U32 array = o.pos;
        for (U32 k = 0; k < counts[a]; k++)
        {
            UPtr id = in.u32(at + 4 * k);
            o.put(&id, sizeof(id));
        }
        o.ptr(host + slots[a], array - (host + base));
    }
    return true;
}

// ---------------------------------------------------------------------------
// SPLN: a NURBS curve, x::NURBS (knot, control) and its control points and
// knots, which x::Spline_Read finds right after the header.

DISK_SIZE(x::NURBS, 28);
DISK_AT(x::NURBS, knot, 20);
DISK_AT(x::NURBS, control, 24);

static const Slot kNURBSSlots[] = { SLOT(x::NURBS, knot, 20), SLOT(x::NURBS, control, 24) };

static bool WidenSPLN(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, 28))
    {
        FAIL("shorter than x::NURBS");
    }

    Record(o, in, 0, 28, sizeof(x::NURBS), kNURBSSlots, 2);
    o.put(in.p + 28, in.size - 28);
    return true;
}

// ---------------------------------------------------------------------------
// JSP: the level geometry's info asset (the other JSP assets are RenderWare
// clumps, read as they are). Three marked blocks, read in place by
// xJSP_MultiStreamRead:
//
//   0xBEEF01  the collision tree: 3 U32 (tag, branch count, triangle count),
//             the branches, the triangles (xClumpCollBSPTriangle, whose first
//             field is a pointer in a union with the vertex index)
//   0xBEEF02  xJSPHeader (version 5 here), the node infos, xJSPNodeTree, its
//             branches and leaves, the strip vector count and vectors
//   0xBEEF04  xJSPExtraInfo
//
// The blocks' lengths are rewritten for the copy, and the collision block is
// padded so that the header after it is 8-aligned.

DISK_SIZE(xClumpCollBSPTriangle, 8);
DISK_AT(xClumpCollBSPTriangle, flags, 4);
DISK_SIZE(xClumpCollBSPBranchNode, 16);
DISK_SIZE(xJSPHeader, 44);
DISK_AT(xJSPHeader, clump, 12);
DISK_AT(xJSPHeader, colltree, 16);
DISK_AT(xJSPHeader, jspNodeList, 20);
DISK_AT(xJSPHeader, stripVecList, 28);
DISK_AT(xJSPHeader, vertDataFlags, 32);
DISK_AT(xJSPHeader, nodetree, 36);
DISK_AT(xJSPHeader, nodelight, 40);
DISK_SIZE(xJSPNodeInfo, 8);
DISK_SIZE(xJSPNodeTree, 16);
DISK_AT(xJSPNodeTree, branchNodes, 4);
DISK_AT(xJSPNodeTree, leafNodes, 12);
DISK_SIZE(xJSPNodeTreeBranch, 16);
DISK_SIZE(xJSPNodeTreeLeaf, 32);

static const Slot kTriangleSlots[] = { SLOT(xClumpCollBSPTriangle, v, 0) };
static const Slot kJSPHeaderSlots[] = {
    SLOT(xJSPHeader, clump, 12),        SLOT(xJSPHeader, colltree, 16),
    SLOT(xJSPHeader, jspNodeList, 20),  SLOT(xJSPHeader, stripVecList, 28),
    SLOT(xJSPHeader, nodetree, 36),     SLOT(xJSPHeader, nodelight, 40),
};
static const Slot kNodeTreeSlots[] = { SLOT(xJSPNodeTree, branchNodes, 4),
                                       SLOT(xJSPNodeTree, leafNodes, 12) };

#define JSP_MARK 12

static bool WidenJSP(Out& o, const In& in, const char** reason)
{
    if (!in.has(0, JSP_MARK) || in.u32(0) != 0xBEEF01)
    {
        *reason = ""; // a clump: read as it is
        return false;
    }

    // 0xBEEF01, the collision tree.
    U32 len1 = in.u32(4);
    U32 p1 = JSP_MARK;
    if (!in.has(p1, len1) || len1 < 12)
    {
        FAIL("the collision block is past the end");
    }
    U32 numBranches = in.u32(p1 + 4);
    U32 numTriangles = in.u32(p1 + 8);
    if (numBranches > 0x1000000 || numTriangles > 0x1000000 ||
        (U64)12 + 16 * (U64)numBranches + 8 * (U64)numTriangles > len1)
    {
        FAIL("the collision tree is larger than its block");
    }

    U32 mark1 = o.put(in.p, JSP_MARK);
    U32 body1 = o.put(in.p + p1, 12 + 16 * numBranches);
    for (U32 i = 0; i < numTriangles; i++)
    {
        Record(o, in, p1 + 12 + 16 * numBranches + 8 * i, 8, sizeof(xClumpCollBSPTriangle),
               kTriangleSlots, 1);
    }
    U32 used1 = 12 + 16 * numBranches + 8 * numTriangles;
    o.put(in.p + p1 + used1, len1 - used1);
    // The next block's header, 8-aligned.
    while ((o.phase + o.pos + JSP_MARK) % 8)
    {
        o.skip(1);
    }
    o.word(mark1 + 4, o.pos - body1);

    // 0xBEEF02, the header and the trees.
    U32 m2 = p1 + len1;
    if (!in.has(m2, JSP_MARK) || in.u32(m2) != 0xBEEF02)
    {
        FAIL("no header block after the collision block");
    }
    U32 len2 = in.u32(m2 + 4);
    U32 p2 = m2 + JSP_MARK;
    if (!in.has(p2, len2) || len2 < 44)
    {
        FAIL("the header block is past the end");
    }
    U32 version = in.u32(p2 + 4);
    if (version != 5)
    {
        FAIL("a JSP header that is not version 5");
    }
    U32 nodeCount = in.u32(p2 + 8);
    U32 pTree = p2 + 44 + 8 * nodeCount;
    if (nodeCount > 0x1000000 || pTree + 16 > p2 + len2)
    {
        FAIL("the node infos are larger than the header block");
    }
    U32 treeBranches = in.u32(pTree);
    U32 treeLeaves = in.u32(pTree + 8);
    U32 pRest = pTree + 16 + 16 * treeBranches + 32 * treeLeaves;
    if (treeBranches > 0x1000000 || treeLeaves > 0x1000000 || pRest > p2 + len2)
    {
        FAIL("the node tree is larger than the header block");
    }

    U32 mark2 = o.put(in.p + m2, JSP_MARK);
    U32 body2 = Record(o, in, p2, 44, sizeof(xJSPHeader), kJSPHeaderSlots, 6);
    o.put(in.p + p2 + 44, 8 * nodeCount);
    Record(o, in, pTree, 16, sizeof(xJSPNodeTree), kNodeTreeSlots, 2);
    o.put(in.p + pTree + 16, (p2 + len2) - (pTree + 16));
    o.word(mark2 + 4, o.pos - body2);

    // 0xBEEF04 and anything after it, as they are.
    U32 m4 = p2 + len2;
    o.put(in.p + m4, in.size - m4);
    return true;
}

// ---------------------------------------------------------------------------

namespace
{
    struct TypeEntry
    {
        U32 type;
        U32 phase;
        bool (*widen)(Out& o, const In& in, const char** reason);
    };
} // namespace

static bool WidenLODT(Out& o, const In& in, const char** reason)
{
    return WidenRows(o, in, 36, sizeof(zLODTable), kLODTSlots, 4, reason);
}

static bool WidenCOLL(Out& o, const In& in, const char** reason)
{
    return WidenRows(o, in, 12, sizeof(zCollGeomTable), kCOLLSlots, 3, reason);
}

static const TypeEntry kTypes[] = {
    { 'ATBL', 4, WidenATBL }, { 'SGRP', 0, WidenSGRP },
    { 'LODT', 4, WidenLODT }, { 'COLL', 4, WidenCOLL }, { 'CRDT', 0, WidenCRDT },
    { 'DEST', 0, WidenDEST }, { 'SHRP', 0, WidenSHRP }, { 'LKIT', 0, WidenLKIT },
    { 'DYNA', 0, WidenDYNA }, { 'SPLN', 0, WidenSPLN }, { 'JSP ', 0, WidenJSP },
};

static const TypeEntry* FindType(U32 type)
{
    for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); i++)
    {
        if (kTypes[i].type == type)
        {
            return &kTypes[i];
        }
    }
    return NULL;
}

S32 iAssetWidenType(U32 type)
{
    return WIDE_64 && FindType(type) != NULL;
}

void* iAssetWiden(U32 type, U32 aid, const void* disk, U32 size, U32* outsize, const char** reason)
{
    *outsize = 0;
    *reason = "";

    const TypeEntry* entry = FindType(type);
    if (!WIDE_64 || entry == NULL || disk == NULL || size == 0)
    {
        return NULL;
    }

    In in = { (const U8*)disk, size };
    Out measure = { NULL, 0, entry->phase };
    if (!entry->widen(measure, in, reason))
    {
        return NULL;
    }

    U8* copy = CopyAlloc(measure.pos, entry->phase);
    if (copy == NULL)
    {
        *reason = "out of memory";
        return NULL;
    }

    Out write = { copy, 0, entry->phase };
    if (!entry->widen(write, in, reason) || write.pos != measure.pos)
    {
        if (**reason == '\0')
        {
            *reason = "the two passes disagree";
        }
        iAssetWidenFree(copy);
        return NULL;
    }

    *outsize = write.pos;
    return copy;
}
