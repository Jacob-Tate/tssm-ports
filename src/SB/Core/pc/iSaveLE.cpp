// PC-only: the little-endian consoles' save payload. See iSaveLE.h and
// docs/SAVES.md section 6.
#include "iSaveLE.h"
#include "iSaveFormat.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iLE_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

static U32 iLE_get32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

static void iLE_put32(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
    p[2] = (U8)(v >> 16);
    p[3] = (U8)(v >> 24);
}

// --- field maps -----------------------------------------------------------------------------

namespace
{
    // Builds a map: bits that stay as they are, and values whose bytes turn.
    struct Builder
    {
        iSaveFieldMap* map;

        void start()
        {
            map->fields.clear();
            map->bits = 0;
        }

        void bits(U32 n)
        {
            map->bits += n;
        }

        void value(U32 bytes)
        {
            iSaveStreamField f = { map->bits, bytes };
            map->fields.push_back(f);
            map->bits += bytes * 8;
        }
    };

    // What zSceneSave writes for one saved object of each kind: `bits` that
    // need nothing (Write_b1, Write_bn, a U8), then one value of `bytes`.
    struct KindLayout
    {
        U32 bits;
        U32 bytes;
    };

    const KindLayout kKinds[ISLE_KINDS] = {
        { 2, 0 }, // trigger: zEntSave = xBaseSave's bit and xEntSave's
        { 1, 0 }, // move point: xBaseSave
        { 11, 0 }, // pickup: zEntSave, 8 bits of state, 1 bit
        { 2, 0 }, // simple object
        { 2, 0 }, // platform
        { 2, 0 }, // pendulum
        { 2, 0 }, // hangable
        { 4, 0 }, // button: zEntSave, 2 bits
        { 2, 0 }, // EGenerator
        { 9, 4 }, // timer: xBaseSave, U8 state, F32 secondsLeft
        { 9, 2 }, // counter: xBaseSave, U8 state, S16 count
        { 1, 0 }, // group
        { 1, 0 }, // portal
        { 1, 0 }, // camera marker
        { 1, 0 }, // surface
        { 1, 0 }, // volume
        { 1, 0 }, // conditional
        { 1, 0 }, // env
        { 1, 0 }, // dispatcher
        { 1, 0 }, // fog
        { 1, 0 }, // light
        { 1, 0 }, // script
        { 8, 0 }, // task box: U8 state
        { 1, 0 }, // flythrough
        { 1, 0 }, // camera tweak
        { 3, 4 }, // teleport box: zEntSave, 1 bit, U32 currPlrState
        { 0, 4 }, // race timer: F32 bestTime
    };

    // The asset type of each sInitTable kind; 0 for the dynamic ones.
    const U32 kAssetType[ISLE_KINDS] = {
        ISF_TAG('T', 'R', 'I', 'G'), ISF_TAG('M', 'V', 'P', 'T'), ISF_TAG('P', 'K', 'U', 'P'),
        ISF_TAG('S', 'I', 'M', 'P'), ISF_TAG('P', 'L', 'A', 'T'), ISF_TAG('P', 'E', 'N', 'D'),
        ISF_TAG('H', 'A', 'N', 'G'), ISF_TAG('B', 'U', 'T', 'N'), ISF_TAG('E', 'G', 'E', 'N'),
        ISF_TAG('T', 'I', 'M', 'R'), ISF_TAG('C', 'N', 'T', 'R'), ISF_TAG('G', 'R', 'U', 'P'),
        ISF_TAG('P', 'O', 'R', 'T'), ISF_TAG('C', 'A', 'M', ' '), ISF_TAG('S', 'U', 'R', 'F'),
        ISF_TAG('V', 'O', 'L', 'U'), ISF_TAG('C', 'O', 'N', 'D'), ISF_TAG('E', 'N', 'V', ' '),
        ISF_TAG('D', 'P', 'A', 'T'), ISF_TAG('F', 'O', 'G', ' '), ISF_TAG('L', 'I', 'T', 'E'),
        ISF_TAG('S', 'C', 'R', 'P'), 0, 0, 0, 0, 0,
    };

    // The dynamic kinds, by the name zScene.cpp's dynamic_types hashes.
    const char* const kDynName[ISLE_KINDS - ISLE_TASKBOX] = {
        "game_object:task_box", "game_object:Flythrough", "game_object:Camera_Tweak",
        "game_object:Teleport", "game_object:RaceTimer",
    };
} // namespace

U32 iSaveStrHash(const char* s)
{
    U32 h = 0;
    for (; *s != '\0'; s++)
    {
        char c = *s;
        c = (char)(c - (c & (c >> 1) & 0x20));
        h = h * 131 + (U32)(U8)c;
    }
    return h;
}

void iSaveFieldsPlayer(iSaveFieldMap* map)
{
    Builder b = { map };
    b.start();
    b.bits(1);
    for (int i = 0; i < 4; i++)
    {
        b.value(4); // manliness points, tokens, chests in all, chests opened
    }
    b.value(2); // skill points spent
    for (int i = 0; i < 40; i++)
    {
        b.value(4); // 20 x (a level's chests, a level's pickups)
    }
    b.bits(10 * 8); // the power-ups, a byte each
}

void iSaveFieldsCounters(S32 taskCounters, iSaveFieldMap* map)
{
    Builder b = { map };
    b.start();
    b.bits(1);
    for (S32 i = 0; i < taskCounters; i++)
    {
        b.bits(kKinds[ISLE_COUNTER].bits);
        b.value(kKinds[ISLE_COUNTER].bytes);
    }
    b.value(4); // zGameExtras_Save: the cheats that are on
}

void iSaveObjectCountsClear(iSaveObjectCounts* c)
{
    memset(c->n, 0, sizeof(c->n));
    c->taskCounters = -1;
}

void iSaveFieldsScene(const iSaveObjectCounts* counts, iSaveFieldMap* map)
{
    Builder b = { map };
    b.start();
    b.bits(1);
    b.value(4); // zScene.cpp's offsetx, offsety
    b.value(4);
    for (int k = 0; k < ISLE_KINDS; k++)
    {
        for (U32 i = 0; i < counts->n[k]; i++)
        {
            b.bits(kKinds[k].bits);
            if (kKinds[k].bytes != 0)
            {
                b.value(kKinds[k].bytes);
            }
        }
    }
}

// --- packages -------------------------------------------------------------------------------

namespace
{
    struct Chunk
    {
        U32 tag;
        U64 data; // where its contents start
        U32 size;
    };

    bool readChunk(iSaveReadFn read, void* ctx, U64 at, U64 end, Chunk* c)
    {
        U8 h[8];
        if (at + 8 > end || !read(ctx, at, h, 8))
        {
            return false;
        }
        c->tag = iSaveGetU32(h);
        c->size = iSaveGetU32(h + 4);
        c->data = at + 8;
        return c->data + c->size <= end;
    }

    struct MemCtx
    {
        const U8* p;
        size_t n;
    };

    bool memRead(void* ctx, U64 offset, void* buf, size_t n)
    {
        MemCtx* m = (MemCtx*)ctx;
        if (offset > m->n || n > m->n - (size_t)offset)
        {
            return false;
        }
        memcpy(buf, m->p + (size_t)offset, n);
        return true;
    }
} // namespace

bool iSaveHipScan(iSaveReadFn read, void* ctx, U64 size, iSaveObjectCounts* counts, char* err,
                  size_t errsize)
{
    Chunk c;
    if (!readChunk(read, ctx, 0, size, &c) || c.tag != ISF_TAG('H', 'I', 'P', 'A'))
    {
        iLE_err(err, errsize, "not a package (no HIPA)");
        return false;
    }

    U32 dynType[ISLE_KINDS - ISLE_TASKBOX];
    for (int i = 0; i < ISLE_KINDS - ISLE_TASKBOX; i++)
    {
        dynType[i] = iSaveStrHash(kDynName[i]);
    }
    const U32 taskGroup = iSaveStrHash("TASK COUNTER GROUP");

    bool bigEndian = false; // the assets' own order: only the GameCube's is big
    bool haveToc = false;

    U64 at = c.data + c.size;
    while (at < size)
    {
        if (!readChunk(read, ctx, at, size, &c))
        {
            iLE_err(err, errsize, "the package is cut short");
            return false;
        }

        if (c.tag == ISF_TAG('P', 'A', 'C', 'K'))
        {
            Chunk s;
            for (U64 p = c.data; p < c.data + c.size; p = s.data + s.size)
            {
                if (!readChunk(read, ctx, p, c.data + c.size, &s))
                {
                    break;
                }
                U8 id[2];
                if (s.tag == ISF_TAG('P', 'L', 'A', 'T') && s.size >= 2 && read(ctx, s.data, id, 2))
                {
                    bigEndian = (id[0] == 'G' && id[1] == 'C');
                }
            }
        }
        else if (c.tag == ISF_TAG('D', 'I', 'C', 'T'))
        {
            Chunk toc;
            for (U64 p = c.data; p < c.data + c.size; p = toc.data + toc.size)
            {
                if (!readChunk(read, ctx, p, c.data + c.size, &toc))
                {
                    iLE_err(err, errsize, "the package's asset table is damaged");
                    return false;
                }
                if (toc.tag != ISF_TAG('A', 'T', 'O', 'C'))
                {
                    continue;
                }
                haveToc = true;

                Chunk a;
                for (U64 q = toc.data; q < toc.data + toc.size; q = a.data + a.size)
                {
                    if (!readChunk(read, ctx, q, toc.data + toc.size, &a))
                    {
                        iLE_err(err, errsize, "the package's asset table is damaged");
                        return false;
                    }
                    if (a.tag != ISF_TAG('A', 'H', 'D', 'R') || a.size < 24)
                    {
                        continue;
                    }

                    // AHDR: id, type, offset, size, padding, flags.
                    U8 h[24];
                    if (!read(ctx, a.data, h, 24))
                    {
                        iLE_err(err, errsize, "the package's asset table is damaged");
                        return false;
                    }
                    U32 type = iSaveGetU32(h + 4);
                    U32 offset = iSaveGetU32(h + 8);
                    U32 asize = iSaveGetU32(h + 12);

                    int kind = -1;
                    bool dyna = (type == ISF_TAG('D', 'Y', 'N', 'A'));
                    if (!dyna)
                    {
                        for (int k = 0; k < ISLE_TASKBOX; k++)
                        {
                            if (kAssetType[k] == type)
                            {
                                kind = k;
                                break;
                            }
                        }
                        if (kind < 0)
                        {
                            continue;
                        }
                    }

                    // The asset's head: xBaseAsset (id, type, links, flags),
                    // then a group's item count, or a dynamic asset's type,
                    // version and handle and a task box's `persistent` byte.
                    U8 head[20];
                    size_t want = dyna ? 12 : (kind == ISLE_GROUP ? 10 : 8);
                    if (asize < want)
                    {
                        continue;
                    }
                    size_t have = asize < sizeof(head) ? asize : sizeof(head);
                    if ((U64)offset + have > size || !read(ctx, offset, head, have))
                    {
                        iLE_err(err, errsize, "the package is cut short");
                        return false;
                    }

                    U32 id = bigEndian ? iSaveGetU32(head) : iLE_get32(head);
                    U32 flags = bigEndian ? (U32)iSaveGetU16(head + 6) :
                                            (U32)(head[6] | (head[7] << 8));
                    bool saved = (flags & 0x2) != 0;

                    if (dyna)
                    {
                        U32 dtype = bigEndian ? iSaveGetU32(head + 8) : iLE_get32(head + 8);
                        for (int i = 0; i < ISLE_KINDS - ISLE_TASKBOX; i++)
                        {
                            if (dynType[i] == dtype)
                            {
                                kind = ISLE_TASKBOX + i;
                                break;
                            }
                        }
                        if (kind < 0)
                        {
                            continue;
                        }

                        if (kind == ISLE_TASKBOX)
                        {
                            // ztaskbox::load: saved when the asset's flag
                            // or its `persistent` byte says so.
                            saved = saved || (have > 16 && head[16] != 0);
                        }
                        else if (kind != ISLE_CAMERATWEAK)
                        {
                            // zCameraFly_Setup, zEntTeleportBox_Init and
                            // zRaceTimer::ResetTimer set the flag themselves.
                            saved = true;
                        }
                    }
                    else if (kind == ISLE_GROUP && id == taskGroup)
                    {
                        counts->taskCounters = bigEndian ? (S32)iSaveGetU16(head + 8) :
                                                           (S32)(head[8] | (head[9] << 8));
                    }

                    if (saved)
                    {
                        counts->n[kind]++;
                    }
                }
            }
        }
        else if (c.tag == ISF_TAG('S', 'T', 'R', 'M'))
        {
            break; // the assets themselves: nothing more to walk
        }

        at = c.data + c.size;
    }

    if (!haveToc)
    {
        iLE_err(err, errsize, "the package has no asset table");
        return false;
    }
    return true;
}

bool iSaveHipScanData(const U8* p, size_t n, iSaveObjectCounts* counts, char* err, size_t errsize)
{
    MemCtx m = { p, n };
    return iSaveHipScan(memRead, &m, n, counts, err, errsize);
}

// --- the bit stream ----------------------------------------------------------------------------

static U32 iLE_streamByte(const U8* block, size_t n, U32 bit)
{
    size_t i = bit >> 3;
    U32 v = block[i];
    if (i + 1 < n)
    {
        v |= (U32)block[i + 1] << 8;
    }
    return (v >> (bit & 7)) & 0xFF;
}

static void iLE_setStreamByte(U8* block, size_t n, U32 bit, U32 value)
{
    size_t i = bit >> 3;
    U32 shift = bit & 7;
    U32 mask = 0xFFu << shift;
    U32 v = block[i];
    if (i + 1 < n)
    {
        v |= (U32)block[i + 1] << 8;
    }
    v = (v & ~mask) | ((value & 0xFF) << shift);
    block[i] = (U8)v;
    if (i + 1 < n)
    {
        block[i + 1] = (U8)(v >> 8);
    }
}

bool iSaveStreamReverse(U8* block, size_t n, const iSaveFieldMap* map)
{
    for (size_t f = 0; f < map->fields.size(); f++)
    {
        const iSaveStreamField& fld = map->fields[f];
        if ((U64)fld.bit + (U64)fld.bytes * 8 > (U64)n * 8)
        {
            return false;
        }
        for (U32 i = 0; i < fld.bytes / 2; i++)
        {
            U32 lo = fld.bit + i * 8;
            U32 hi = fld.bit + (fld.bytes - 1 - i) * 8;
            U32 a = iLE_streamByte(block, n, lo);
            U32 b = iLE_streamByte(block, n, hi);
            iLE_setStreamByte(block, n, lo, b);
            iLE_setStreamByte(block, n, hi, a);
        }
    }
    return true;
}

S32 iSaveStreamLastBit(const U8* block, size_t n)
{
    for (size_t i = n; i > 0; i--)
    {
        U8 v = block[i - 1];
        if (v != 0)
        {
            S32 bit = 7;
            while (!(v & 0x80))
            {
                v = (U8)(v << 1);
                bit--;
            }
            return (S32)(i - 1) * 8 + bit;
        }
    }
    return -1;
}

// --- the conversion ---------------------------------------------------------------------------

bool iSavePayloadIsLE(const U8* p, size_t n)
{
    return n >= ISF_PAYLOAD_HEADER && iLE_get32(p) == ISF_TAG('G', 'D', 'A', 'T') &&
           iLE_get32(p + 4) == 1;
}

U32 iSavePayloadLESize(const U8* p)
{
    return iLE_get32(p + 8);
}

static void iLE_swapWords(const U8* src, U8* dst, size_t bytes)
{
    size_t i = 0;
    for (; i + 4 <= bytes; i += 4)
    {
        U8 a = src[i];
        U8 b = src[i + 1];
        U8 c = src[i + 2];
        U8 d = src[i + 3];
        dst[i] = d;
        dst[i + 1] = c;
        dst[i + 2] = b;
        dst[i + 3] = a;
    }
    for (; i < bytes; i++)
    {
        dst[i] = src[i];
    }
}

bool iSavePayloadLE(iSaveLEDir dir, const U8* src, size_t n, std::vector<U8>& dst,
                    iSaveFieldMapFn maps, void* ctx, std::string* notes, char* err,
                    size_t errsize)
{
    dst.clear();
    if (notes != NULL)
    {
        notes->clear();
    }

    const bool toLE = (dir == ISLE_TO_LE);
#define ILE_RD32(p) (toLE ? iSaveGetU32(p) : iLE_get32(p))

    if (n < ISF_PAYLOAD_HEADER || ILE_RD32(src) != ISF_TAG('G', 'D', 'A', 'T'))
    {
        iLE_err(err, errsize, "not a save: no GDAT header");
        return false;
    }
    if (ILE_RD32(src + 4) != 1)
    {
        iLE_err(err, errsize, "save version %lu, the game reads only 1",
                (unsigned long)ILE_RD32(src + 4));
        return false;
    }

    U32 size = ILE_RD32(src + 8);
    if (size < ISF_PAYLOAD_HEADER || size > n)
    {
        iLE_err(err, errsize, "the save is cut short (%lu bytes of %lu)", (unsigned long)n,
                (unsigned long)size);
        return false;
    }

    dst.assign(src, src + size);
    U8* out = &dst[0];
    iLE_swapWords(src, out, ISF_PAYLOAD_HEADER);

    std::vector<U8> tmp;
    U32 pos = ISF_PAYLOAD_HEADER;
    while (pos < size)
    {
        if (size - pos < ISF_CLIENT_HEADER)
        {
            iLE_err(err, errsize, "a block header runs past the end of the save");
            dst.clear();
            return false;
        }

        U32 tag = ILE_RD32(src + pos);
        U32 room = ILE_RD32(src + pos + 4);
        U32 used = ILE_RD32(src + pos + 8);
        U32 data = pos + ISF_CLIENT_HEADER;
        char name[5];
        iSaveTagName(tag, name);

        if (room > size - data)
        {
            iLE_err(err, errsize, "block %s runs past the end of the save", name);
            dst.clear();
            return false;
        }

        iLE_swapWords(src + pos, out + pos, ISF_CLIENT_HEADER);

        if (tag == ISF_TAG('L', 'E', 'D', 'R'))
        {
            // The leader: the same layout on every console. Progress (S32 at
            // 0x40) and play time (S64 at 0x48) turn; the rest is bytes.
            if (room >= 0x44)
            {
                iLE_swapWords(src + data + 0x40, out + data + 0x40, 4);
            }
            if (room >= 0x50)
            {
                for (int i = 0; i < 8; i++)
                {
                    out[data + 0x48 + i] = src[data + 0x48 + 7 - i];
                }
            }
        }
        else if (tag == ISF_TAG('R', 'O', 'O', 'M') || tag == ISF_TAG('S', 'F', 'I', 'L'))
        {
            // Characters.
        }
        else if (tag == ISF_TAG('P', 'R', 'E', 'F') || tag == ISF_TAG('S', 'V', 'I', 'D'))
        {
            iLE_swapWords(src + data, out + data, used < room ? used : room);
        }
        else if (room > 0)
        {
            // A serializer client. In little-endian form the stream's bit k
            // is bit k%8 of byte k/8: the map's values are turned there.
            tmp.resize(room);
            if (toLE)
            {
                iLE_swapWords(src + data, &tmp[0], room);
            }
            else
            {
                memcpy(&tmp[0], src + data, room);
            }

            S32 last = iSaveStreamLastBit(&tmp[0], room);
            if (last >= 0)
            {
                iSaveFieldMap map;
                char why[256] = "";
                if (maps == NULL || !maps(ctx, tag, &map, why, sizeof(why)))
                {
                    iLE_err(err, errsize, "block %s cannot be converted: %s", name,
                            why[0] != '\0' ? why : "nothing says what it holds");
                    dst.clear();
                    return false;
                }
                if (!iSaveStreamReverse(&tmp[0], room, &map))
                {
                    iLE_err(err, errsize,
                            "block %s is too small for what the level's files say it saves "
                            "(%lu bits)",
                            name, (unsigned long)map.bits);
                    dst.clear();
                    return false;
                }
                if ((U32)last >= map.bits && notes != NULL)
                {
                    char line[160];
                    snprintf(line, sizeof(line),
                             "%s%s holds %ld more bits than this copy of the level saves "
                             "(kept as they are)",
                             notes->empty() ? "" : "; ", name, (long)last + 1 - (long)map.bits);
                    *notes += line;
                }
            }

            if (toLE)
            {
                memcpy(out + data, &tmp[0], room);
            }
            else
            {
                iLE_swapWords(&tmp[0], out + data, room);
            }
        }

        pos = data + room;
    }

    U32 stored = ILE_RD32(src + 12);
    U32 srcCRC = iSaveCRC(0xFFFFFFFFu, src + ISF_PAYLOAD_HEADER, size - ISF_PAYLOAD_HEADER);
    U32 dstCRC = iSaveCRC(0xFFFFFFFFu, out + ISF_PAYLOAD_HEADER, size - ISF_PAYLOAD_HEADER);
    U32 crc = (stored == srcCRC) ? dstCRC : ~dstCRC;
    if (toLE)
    {
        iLE_put32(out + 12, crc);
    }
    else
    {
        iSavePutU32(out + 12, crc);
    }

#undef ILE_RD32
    return true;
}
