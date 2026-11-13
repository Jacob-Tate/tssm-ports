// PC-only: the save formats. See iSaveFormat.h and docs/SAVES.md.
#include "iSaveFormat.h"
#include "iHost.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// --- byte order -------------------------------------------------------------------

U16 iSaveGetU16(const U8* p)
{
    return (U16)((p[0] << 8) | p[1]);
}

U32 iSaveGetU32(const U8* p)
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
}

void iSavePutU16(U8* p, U16 v)
{
    p[0] = (U8)(v >> 8);
    p[1] = (U8)v;
}

void iSavePutU32(U8* p, U32 v)
{
    p[0] = (U8)(v >> 24);
    p[1] = (U8)(v >> 16);
    p[2] = (U8)(v >> 8);
    p[3] = (U8)v;
}

static U64 iSF_get64(const U8* p)
{
    return ((U64)iSaveGetU32(p) << 32) | iSaveGetU32(p + 4);
}

static void iSF_put64(U8* p, U64 v)
{
    iSavePutU32(p, (U32)(v >> 32));
    iSavePutU32(p + 4, (U32)v);
}

static U32 iSF_native32(const U8* p)
{
    U32 v;
    memcpy(&v, p, 4);
    return v;
}

static void iSF_putNative32(U8* p, U32 v)
{
    memcpy(p, &v, 4);
}

static U64 iSF_native64(const U8* p)
{
    U64 v;
    memcpy(&v, p, 8);
    return v;
}

static void iSF_putNative64(U8* p, U64 v)
{
    memcpy(p, &v, 8);
}

const char* iSaveTagName(U32 tag, char* out5)
{
    for (S32 i = 0; i < 4; i++)
    {
        U8 c = (U8)(tag >> (24 - 8 * i));
        out5[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    out5[4] = '\0';
    return out5;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iSF_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

// --- CRC ------------------------------------------------------------------------------

U32 iSaveCRC(U32 accum, const U8* p, size_t n)
{
    static U32 table[256];
    static bool ready = false;

    if (!ready)
    {
        for (U32 i = 0; i < 256; i++)
        {
            U32 c = i << 24;
            for (S32 j = 0; j < 8; j++)
            {
                c = (c & 0x80000000u) ? ((c << 1) ^ 0x04C11DB7u) : (c << 1);
            }
            table[i] = c;
        }
        ready = true;
    }

    for (size_t i = 0; i < n; i++)
    {
        accum = (accum << 8) ^ table[((accum >> 24) ^ p[i]) & 0xFF];
    }

    return accum;
}

// --- the payload ---------------------------------------------------------------------

static const U32 TAG_GDAT = ISF_TAG('G', 'D', 'A', 'T');
static const U32 TAG_LEDR = ISF_TAG('L', 'E', 'D', 'R');
static const U32 TAG_ROOM = ISF_TAG('R', 'O', 'O', 'M');
static const U32 TAG_PREF = ISF_TAG('P', 'R', 'E', 'F');
static const U32 TAG_SVID = ISF_TAG('S', 'V', 'I', 'D');
static const U32 TAG_SFIL = ISF_TAG('S', 'F', 'I', 'L');

bool iSavePayloadInspect(const U8* p, size_t n, iSavePayloadInfo* info, char* err, size_t errsize)
{
    iSavePayloadInfo local;
    if (info == NULL)
    {
        info = &local;
    }

    info->size = 0;
    info->crcStored = 0;
    info->crcComputed = 0;
    info->crcOK = false;
    info->version = 0;
    info->serialVersion = -1;
    info->label[0] = '\0';
    info->progress = -1;
    info->gametime = 0;
    info->levelIndex = -1;
    info->scene[0] = '\0';
    info->serialClients = 0;
    info->blocks.clear();

    if (p == NULL || n < ISF_PAYLOAD_HEADER)
    {
        iSF_err(err, errsize, "too short to be a save (%ld bytes)", (long)n);
        return false;
    }

    U32 magic = iSaveGetU32(p);
    if (magic != TAG_GDAT)
    {
        if (magic == ISF_TAG('T', 'A', 'D', 'G'))
        {
            iSF_err(err, errsize, "a save in a little-endian host's byte order, not the console's");
        }
        else
        {
            iSF_err(err, errsize, "not a save: it does not start with GDAT");
        }
        return false;
    }

    info->version = (S32)iSaveGetU32(p + 4);
    info->size = iSaveGetU32(p + 8);
    info->crcStored = iSaveGetU32(p + 12);

    if (info->version != 1)
    {
        iSF_err(err, errsize, "save format version %ld, the game reads only 1",
                (long)info->version);
        return false;
    }

    if (info->size < ISF_PAYLOAD_HEADER || info->size > n)
    {
        iSF_err(err, errsize, "the header says %lu bytes, there are %lu", (unsigned long)info->size,
                (unsigned long)n);
        return false;
    }

    info->crcComputed =
        iSaveCRC(0xFFFFFFFFu, p + ISF_PAYLOAD_HEADER, info->size - ISF_PAYLOAD_HEADER);
    info->crcOK = (info->crcComputed == info->crcStored);

    bool haveLeader = false;
    bool haveRoom = false;
    U32 pos = ISF_PAYLOAD_HEADER;
    char tagname[5];

    while (pos < info->size)
    {
        if (pos + ISF_CLIENT_HEADER > info->size)
        {
            iSF_err(err, errsize, "a block header runs past the end at %lu", (unsigned long)pos);
            return false;
        }

        iSaveBlock b;
        b.tag = iSaveGetU32(p + pos);
        b.room = (S32)iSaveGetU32(p + pos + 4);
        b.used = (S32)iSaveGetU32(p + pos + 8);
        b.offset = pos + ISF_CLIENT_HEADER;

        if (b.room < 0 || b.used < 0 || b.used > b.room ||
            (U64)b.offset + (U64)b.room > (U64)info->size)
        {
            iSF_err(err, errsize, "block %s at %lu has a bad size (%ld of %ld used)",
                    iSaveTagName(b.tag, tagname), (unsigned long)pos, (long)b.used, (long)b.room);
            return false;
        }

        info->blocks.push_back(b);
        const U8* d = p + b.offset;

        if (b.tag == TAG_LEDR)
        {
            if (info->blocks.size() != 1)
            {
                iSF_err(err, errsize, "the leader block is not the first");
                return false;
            }

            if (b.room < ISF_LEADER_SIZE)
            {
                iSF_err(err, errsize, "the leader block is too small (%ld bytes)", (long)b.room);
                return false;
            }

            memcpy(info->label, d, 63);
            info->label[63] = '\0';
            info->progress = (S32)iSaveGetU32(d + 0x40);
            info->gametime = (S64)iSF_get64(d + 0x48);
            info->levelIndex = (S32)(signed char)d[0x50];
            haveLeader = true;
        }
        else if (b.tag == TAG_ROOM)
        {
            S32 k = b.used < 7 ? b.used : 7;
            memcpy(info->scene, d, (size_t)k);
            info->scene[k] = '\0';
            haveRoom = true;
        }
        else if (b.tag == TAG_SVID)
        {
            if (b.used >= 4)
            {
                info->serialVersion = (S32)iSaveGetU32(d);
            }
        }
        else if (b.tag != TAG_PREF && b.tag != TAG_SFIL)
        {
            info->serialClients++;
        }

        pos = b.offset + (U32)b.room;
    }

    if (!haveLeader)
    {
        iSF_err(err, errsize, "no leader block (LEDR)");
        return false;
    }

    if (!haveRoom)
    {
        iSF_err(err, errsize, "no scene block (ROOM)");
        return false;
    }

    if (info->serialVersion != 1)
    {
        iSF_err(err, errsize, "game state version %ld, the game reads only 1",
                (long)info->serialVersion);
        return false;
    }

    if (!info->crcOK)
    {
        iSF_err(err, errsize, "checksum mismatch: stored %08lx, computed %08lx",
                (unsigned long)info->crcStored, (unsigned long)info->crcComputed);
        return false;
    }

    return true;
}

// --- canonical <-> host ------------------------------------------------------------

namespace
{
    // Reads the source's byte order, writes the destination's.
    struct Conv
    {
        iSaveConvertDir dir;

        U32 rd32(const U8* p) const
        {
            return dir == ISF_TO_HOST ? iSaveGetU32(p) : iSF_native32(p);
        }

        U64 rd64(const U8* p) const
        {
            return dir == ISF_TO_HOST ? iSF_get64(p) : iSF_native64(p);
        }

        void wr32(U8* p, U32 v) const
        {
            if (dir == ISF_TO_HOST)
            {
                iSF_putNative32(p, v);
            }
            else
            {
                iSavePutU32(p, v);
            }
        }

        void wr64(U8* p, U64 v) const
        {
            if (dir == ISF_TO_HOST)
            {
                iSF_putNative64(p, v);
            }
            else
            {
                iSF_put64(p, v);
            }
        }

        void words(const U8* src, U8* dst, size_t bytes) const
        {
            for (size_t i = 0; i + 4 <= bytes; i += 4)
            {
                wr32(dst + i, rd32(src + i));
            }
        }
    };
} // namespace

// The leader, from one side's layout to the other's. `avail` bytes of the block
// are present in both buffers (a short read has the head of it only).
static S32 iSF_convertLeader(const Conv& cv, const U8* src, U8* dst, S32 room, S32 used, S32 avail,
                             const iSaveHostLayout* host)
{
    iSaveHostLayout canon = { ISF_LEADER_SIZE, 0x40, 0x48, 0x50 };
    const iSaveHostLayout* from = (cv.dir == ISF_TO_HOST) ? &canon : host;
    const iSaveHostLayout* to = (cv.dir == ISF_TO_HOST) ? host : &canon;

    if ((S32)from->leaderSize > avail || (S32)to->leaderSize > room)
    {
        // Too little of it to convert field by field: swap what is word-shaped
        // past the label and leave the rest.
        memcpy(dst, src, (size_t)avail);
        if (avail >= 0x44)
        {
            cv.wr32(dst + 0x40, cv.rd32(src + 0x40));
        }
        return used;
    }

    std::vector<U8> out((size_t)room, 0xBF);

    memset(&out[0], 0, to->leaderSize);
    memcpy(&out[0], src, 64); // gameLabel, the first member on every host
    cv.wr32(&out[to->offProgress], cv.rd32(src + from->offProgress));
    cv.wr64(&out[to->offGametime], cv.rd64(src + from->offGametime));
    out[to->offThumb] = src[from->offThumb];

    // What the leader client writes after the struct ("--TakeMeToYourLeader--"),
    // moved with it; the block's fill follows.
    S32 tail = used - (S32)from->leaderSize;
    if (tail < 0)
    {
        tail = 0;
    }
    if ((S32)to->leaderSize + tail > room)
    {
        tail = room - (S32)to->leaderSize;
    }

    S32 copy = tail;
    if ((S32)from->leaderSize + copy > avail)
    {
        copy = avail - (S32)from->leaderSize;
    }
    memcpy(&out[to->leaderSize], src + from->leaderSize, (size_t)copy);

    memcpy(dst, &out[0], (size_t)avail);
    return (S32)to->leaderSize + tail;
}

bool iSavePayloadConvert(iSaveConvertDir dir, const U8* src, U8* dst, size_t n,
                         const iSaveHostLayout* host)
{
    memcpy(dst, src, n);

    if (n < ISF_PAYLOAD_HEADER)
    {
        return false;
    }

    iSaveHostLayout same = { ISF_LEADER_SIZE, 0x40, 0x48, 0x50 };
    if (host == NULL)
    {
        host = &same;
    }

    Conv cv = { dir };

    if (cv.rd32(src) != TAG_GDAT)
    {
        // Not a payload. Left as it was, for the game's own check to refuse.
        return false;
    }

    U32 size = cv.rd32(src + 8);
    cv.words(src, dst, ISF_PAYLOAD_HEADER);

    // The block walk.
    U32 end = size < n ? size : (U32)n;
    U32 pos = ISF_PAYLOAD_HEADER;
    bool whole = true;

    while (pos < size)
    {
        if (pos + ISF_CLIENT_HEADER > end)
        {
            whole = false;
            break;
        }

        U32 tag = cv.rd32(src + pos);
        S32 room = (S32)cv.rd32(src + pos + 4);
        S32 used = (S32)cv.rd32(src + pos + 8);
        U32 data = pos + ISF_CLIENT_HEADER;

        if (room < 0 || used < 0 || (U64)data + (U64)room > (U64)size)
        {
            whole = false;
            break;
        }

        S32 avail = (S32)(end > data ? end - data : 0);
        if (avail > room)
        {
            avail = room;
        }

        if (tag == TAG_LEDR)
        {
            used = iSF_convertLeader(cv, src + data, dst + data, room, used, avail, host);
        }
        else if (tag == TAG_ROOM || tag == TAG_SFIL)
        {
            // Characters.
        }
        else if (tag == TAG_PREF || tag == TAG_SVID)
        {
            // S32s and F32s, then fill.
            cv.words(src + data, dst + data, (size_t)(used < avail ? used : avail));
        }
        else
        {
            // A serializer client: its bit buffer, stored as S32s.
            cv.words(src + data, dst + data, (size_t)avail);
        }

        cv.wr32(dst + pos, tag);
        cv.wr32(dst + pos + 4, (U32)room);
        cv.wr32(dst + pos + 8, (U32)used);

        pos = data + (U32)room;
    }

    if (pos != size && n >= size)
    {
        whole = false;
    }

    // The CRC, when all of it is here to compute.
    if (n >= size && size >= ISF_PAYLOAD_HEADER)
    {
        U32 stored = cv.rd32(src + 12);
        U32 srcCRC = iSaveCRC(0xFFFFFFFFu, src + ISF_PAYLOAD_HEADER, size - ISF_PAYLOAD_HEADER);
        U32 dstCRC = iSaveCRC(0xFFFFFFFFu, dst + ISF_PAYLOAD_HEADER, size - ISF_PAYLOAD_HEADER);
        cv.wr32(dst + 12, (whole && stored == srcCRC) ? dstCRC : ~dstCRC);
    }

    return whole;
}

// --- .gci --------------------------------------------------------------------------

bool iSaveGCIParse(const U8* file, size_t n, iSaveGCI* out, char* err, size_t errsize)
{
    memset(out, 0, sizeof(*out));

    size_t off = 0;
    if (n >= 0x110 + ISF_DENTRY_SIZE && memcmp(file, "GCSAVE", 6) == 0)
    {
        off = 0x110; // GameShark
    }

    if (n < off + ISF_DENTRY_SIZE)
    {
        iSF_err(err, errsize, "too short to be a GameCube save file");
        return false;
    }

    const U8* d = file + off;
    memcpy(out->gamecode, d, 4);
    out->gamecode[4] = '\0';
    memcpy(out->maker, d + 4, 2);
    out->maker[2] = '\0';
    memcpy(out->filename, d + 8, 32);
    out->filename[32] = '\0';
    out->bannerFormat = d[7];
    out->time = iSaveGetU32(d + 0x28);
    out->iconAddr = iSaveGetU32(d + 0x2C);
    out->iconFormat = iSaveGetU16(d + 0x30);
    out->iconSpeed = iSaveGetU16(d + 0x32);
    out->permission = d[0x34];
    out->copyTimes = d[0x35];
    out->startBlock = iSaveGetU16(d + 0x36);
    out->length = iSaveGetU16(d + 0x38);
    out->commentAddr = iSaveGetU32(d + 0x3C);
    out->dentry = d;
    out->data = d + ISF_DENTRY_SIZE;
    out->dataSize = (size_t)out->length * ISF_BLOCK_SIZE;

    if (out->length == 0 || n - off - ISF_DENTRY_SIZE < out->dataSize)
    {
        iSF_err(err, errsize, "the file entry says %u blocks and the file has %lu bytes of them",
                (unsigned)out->length, (unsigned long)(n - off - ISF_DENTRY_SIZE));
        return false;
    }

    return true;
}

S32 iSaveGCIGameIndex(const iSaveGCI* gci)
{
    if (strcmp(gci->maker, ISF_MAKER) != 0 || strncmp(gci->gamecode, "GGV", 3) != 0)
    {
        return -1;
    }

    size_t base = strlen(ISF_FILE_BASE);
    if (strncmp(gci->filename, ISF_FILE_BASE, base) != 0 || strlen(gci->filename) != base + 2 ||
        gci->filename[base] != '0' || gci->filename[base + 1] < '1' ||
        gci->filename[base + 1] > '0' + ISF_NUM_GAMES)
    {
        return -1;
    }

    return gci->filename[base + 1] - '1';
}

bool iSaveGCIPayload(const iSaveGCI* gci, std::vector<U8>& payload, iSavePayloadInfo* info,
                     char* err, size_t errsize)
{
    if (iSaveGCIGameIndex(gci) < 0)
    {
        iSF_err(err, errsize, "not a SpongeBob Movie save (game %s, maker %s, file %s)",
                gci->gamecode, gci->maker, gci->filename);
        return false;
    }

    if (gci->dataSize < ISF_HEAD_SIZE + ISF_PAYLOAD_HEADER)
    {
        iSF_err(err, errsize, "the file is too short for the game's head and a payload");
        return false;
    }

    // As iSG_curKosher compares them: one character short of each string.
    const U8* head = gci->data;
    if (memcmp(head, ISF_GAME_NAME, strlen(ISF_GAME_NAME) - 1) != 0 ||
        memcmp(head + ISF_FOOTER_OFFSET, ISF_FOOTER, strlen(ISF_FOOTER) - 1) != 0)
    {
        iSF_err(err, errsize, "the file's head is not the game's (title or footer differ)");
        return false;
    }

    const U8* p = gci->data + ISF_HEAD_SIZE;
    size_t n = gci->dataSize - ISF_HEAD_SIZE;

    iSavePayloadInfo local;
    if (info == NULL)
    {
        info = &local;
    }

    if (!iSavePayloadInspect(p, n, info, err, errsize))
    {
        return false;
    }

    payload.assign(p, p + info->size);
    return true;
}

void iSaveGCIBuildDefaults(iSaveGCIBuild* b)
{
    memset(b, 0, sizeof(*b));
    memcpy(b->gamecode, ISF_GAMECODE_NTSC, 5);
    b->startBlock = 5;
}

void iSaveComposeHead(U8* head, const char* comment, const U8* banner, const U8* icon)
{
    memset(head, 0, ISF_HEAD_SIZE);

    // strncpy and the forced terminator, as iSG_bfr_icondata does them.
    strncpy((char*)head, ISF_GAME_NAME, ISF_COMMENT_SIZE);
    head[ISF_COMMENT_SIZE - 1] = 0;

    if (comment != NULL)
    {
        strncpy((char*)head + ISF_COMMENT_SIZE, comment, ISF_COMMENT_SIZE);
    }
    head[2 * ISF_COMMENT_SIZE - 1] = 0;

    if (banner != NULL)
    {
        memcpy(head + ISF_BANNER_OFFSET, banner, ISF_BANNER_SIZE);
    }
    else
    {
        iSaveDefaultBanner(head + ISF_BANNER_OFFSET);
    }

    if (icon != NULL)
    {
        memcpy(head + ISF_ICON_OFFSET, icon, ISF_ICON_SIZE);
    }
    else
    {
        iSaveDefaultIcon(head + ISF_ICON_OFFSET);
    }

    memcpy(head + ISF_FOOTER_OFFSET, ISF_FOOTER, strlen(ISF_FOOTER));
}

bool iSaveGCIMake(const U8* payload, size_t n, const iSaveGCIBuild* b, std::vector<U8>& out)
{
    if (n < ISF_PAYLOAD_HEADER || b->game < 0 || b->game >= ISF_NUM_GAMES)
    {
        return false;
    }

    size_t padded = (n + ISF_BLOCK_SIZE - 1) / ISF_BLOCK_SIZE * ISF_BLOCK_SIZE;
    size_t blocks = (ISF_HEAD_SIZE + padded) / ISF_BLOCK_SIZE;

    out.assign(ISF_DENTRY_SIZE + ISF_HEAD_SIZE + padded, 0);
    U8* d = &out[0];

    // CARDCreate's strncpy of the name: the rest of the field is zeros.
    char name[32];
    snprintf(name, sizeof(name), "%s%02d", ISF_FILE_BASE, (int)b->game + 1);

    if (b->verbatimDentry != NULL && b->verbatimHead != NULL)
    {
        memcpy(d, b->verbatimDentry, ISF_DENTRY_SIZE);
        iSavePutU16(d + 0x38, (U16)blocks);
        memcpy(d + ISF_DENTRY_SIZE, b->verbatimHead, ISF_HEAD_SIZE);

        // The slot it is in now, which need not be the one it came from.
        memset(d + 8, 0, 32);
        memcpy(d + 8, name, strlen(name));
    }
    else
    {
        // CARDCreate on a formatted card: the entry starts as 0xFF.
        memset(d, 0xFF, ISF_DENTRY_SIZE);
        memcpy(d, b->gamecode, 4);
        memcpy(d + 4, ISF_MAKER, 2);
        d[7] = 0x02; // banner RGB5A3, icon animation looping

        memset(d + 8, 0, 32);
        memcpy(d + 8, name, strlen(name));

        iSavePutU32(d + 0x28, b->time);
        iSavePutU32(d + 0x2C, ISF_BANNER_OFFSET); // iconAddr: the banner, then the icon
        iSavePutU16(d + 0x30, 0x0002); // frame 0 RGB5A3, frame 1 none
        iSavePutU16(d + 0x32, 0x0002); // frame 0 middle speed, frame 1 the end
        d[0x34] = 0x04; // CARD_ATTR_PUBLIC
        d[0x35] = b->copyTimes;
        iSavePutU16(d + 0x36, b->startBlock);
        iSavePutU16(d + 0x38, (U16)blocks);
        iSavePutU32(d + 0x3C, 0); // commentAddr: the title and comment open the file

        iSaveComposeHead(d + ISF_DENTRY_SIZE, b->comment, b->banner, b->icon);
    }

    memcpy(d + ISF_DENTRY_SIZE + ISF_HEAD_SIZE, payload, n);
    return true;
}

void iSaveGCIDolphinName(const char* gamecode, S32 game, char* out, size_t outsize)
{
    snprintf(out, outsize, "%s-%.4s-%s%02d.gci", ISF_MAKER, gamecode, ISF_FILE_BASE, (int)game + 1);
}

// --- the port's banner and icon ----------------------------------------------------

// RGB5A3, opaque: 1RRRRRGGGGGBBBBB, big-endian, in 4x4 tiles.
static U16 iSF_rgb(S32 r, S32 g, S32 b)
{
    return (U16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static void iSF_pixel(U8* img, S32 w, S32 x, S32 y, U16 v)
{
    S32 tile = (y / 4) * (w / 4) + (x / 4);
    S32 idx = tile * 16 + (y % 4) * 4 + (x % 4);
    iSavePutU16(img + idx * 2, v);
}

// 5x7 capitals, the ones the banner needs. Each row is 5 bits, MSB on the left.
static const U8* iSF_glyph(char c)
{
    static const U8 S[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    static const U8 P[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
    static const U8 O[7] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static const U8 N[7] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
    static const U8 G[7] = { 0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F };
    static const U8 E[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
    static const U8 B[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
    static const U8 M[7] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
    static const U8 V[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
    static const U8 I[7] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
    static const U8 C[7] = { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F };

    switch (c)
    {
    case 'S':
        return S;
    case 'P':
        return P;
    case 'O':
        return O;
    case 'N':
        return N;
    case 'G':
        return G;
    case 'E':
        return E;
    case 'B':
        return B;
    case 'M':
        return M;
    case 'V':
        return V;
    case 'I':
        return I;
    case 'C':
        return C;
    default:
        return NULL;
    }
}

static void iSF_text(U8* img, S32 w, S32 h, S32 x0, S32 y0, const char* s, U16 fg, U16 shadow)
{
    for (S32 pass = 0; pass < 2; pass++)
    {
        S32 x = x0 + (pass == 0 ? 1 : 0);
        S32 y = y0 + (pass == 0 ? 1 : 0);
        for (const char* c = s; *c != '\0'; c++, x += 6)
        {
            const U8* g = iSF_glyph(*c);
            if (g == NULL)
            {
                continue;
            }
            for (S32 row = 0; row < 7; row++)
            {
                for (S32 col = 0; col < 5; col++)
                {
                    if ((g[row] >> (4 - col)) & 1)
                    {
                        S32 px = x + col;
                        S32 py = y + row;
                        if (px >= 0 && px < w && py >= 0 && py < h)
                        {
                            iSF_pixel(img, w, px, py, pass == 0 ? shadow : fg);
                        }
                    }
                }
            }
        }
    }
}

void iSaveDefaultBanner(U8* out)
{
    const S32 w = 96;
    const S32 h = 32;

    // Sea, darkening downwards.
    for (S32 y = 0; y < h; y++)
    {
        for (S32 x = 0; x < w; x++)
        {
            S32 t = y * 255 / (h - 1);
            iSF_pixel(out, w, x, y, iSF_rgb(40 - t / 10, 150 - t / 3, 230 - t / 4));
        }
    }

    const char* title = "SPONGEBOB MOVIE";
    S32 width = (S32)strlen(title) * 6 - 1;
    iSF_text(out, w, h, (w - width) / 2, 6, title, iSF_rgb(255, 236, 64), iSF_rgb(20, 40, 90));
    iSF_text(out, w, h, (w - 11) / 2, 18, "PC", iSF_rgb(255, 255, 255), iSF_rgb(20, 40, 90));
}

void iSaveDefaultIcon(U8* out)
{
    const S32 w = 32;
    const S32 h = 32;

    // A sponge: yellow, a darker edge, a few holes.
    static const S32 holes[][3] = { { 8, 9, 3 },   { 21, 7, 2 },  { 16, 17, 4 }, { 7, 23, 2 },
                                    { 25, 22, 3 }, { 13, 27, 1 }, { 26, 13, 1 }, { 5, 15, 1 } };

    for (S32 y = 0; y < h; y++)
    {
        for (S32 x = 0; x < w; x++)
        {
            U16 v = iSF_rgb(255, 230, 50);
            if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
            {
                v = iSF_rgb(150, 120, 20);
            }
            else
            {
                for (size_t i = 0; i < sizeof(holes) / sizeof(holes[0]); i++)
                {
                    S32 dx = x - holes[i][0];
                    S32 dy = y - holes[i][1];
                    if (dx * dx + dy * dy <= holes[i][2] * holes[i][2])
                    {
                        v = iSF_rgb(190, 160, 30);
                    }
                }
            }
            iSF_pixel(out, w, x, y, v);
        }
    }
}

bool iSaveTPLImage(const U8* tpl, size_t n, const U8** data, size_t* size, U16* width, U16* height,
                   U32* format)
{
    if (n < 12 || iSaveGetU32(tpl) != 2142000)
    {
        return false;
    }

    U32 count = iSaveGetU32(tpl + 4);
    U32 table = iSaveGetU32(tpl + 8);
    if (count < 1 || (U64)table + 8 > n)
    {
        return false;
    }

    U32 header = iSaveGetU32(tpl + table);
    if (header == 0 || (U64)header + 12 > n)
    {
        return false;
    }

    U16 h = iSaveGetU16(tpl + header);
    U16 w = iSaveGetU16(tpl + header + 2);
    U32 fmt = iSaveGetU32(tpl + header + 4);
    U32 off = iSaveGetU32(tpl + header + 8);

    // 16 bits a texel for the formats the card takes (RGB5A3 is 5).
    size_t bytes = (size_t)((w + 3) & ~3) * ((h + 3) & ~3) * 2;
    if ((U64)off + bytes > n)
    {
        return false;
    }

    *data = tpl + off;
    *size = bytes;
    *width = w;
    *height = h;
    *format = fmt;
    return true;
}

// --- raw card images ---------------------------------------------------------------

static void iSF_cardSum(const U8* p, size_t len, U16* sum, U16* inv)
{
    U16 s = 0;
    U16 v = 0;
    for (size_t i = 0; i + 1 < len; i += 2)
    {
        U16 w = iSaveGetU16(p + i);
        s = (U16)(s + w);
        v = (U16)(v + (U16)~w);
    }
    *sum = (s == 0xFFFF) ? 0 : s;
    *inv = (v == 0xFFFF) ? 0 : v;
}

// Which of the two copies of a system block the SDK would use: the one whose
// check code is newer, of those whose checksum holds. -1 for neither.
static S32 iSF_pickCopy(const U8* a, const U8* b, bool fat)
{
    const U8* blk[2] = { a, b };
    bool ok[2];
    S16 code[2];

    for (S32 i = 0; i < 2; i++)
    {
        U16 sum;
        U16 inv;
        if (fat)
        {
            iSF_cardSum(blk[i] + 4, ISF_BLOCK_SIZE - 4, &sum, &inv);
            ok[i] = sum == iSaveGetU16(blk[i]) && inv == iSaveGetU16(blk[i] + 2);
            code[i] = (S16)iSaveGetU16(blk[i] + 4);
        }
        else
        {
            iSF_cardSum(blk[i], ISF_BLOCK_SIZE - 4, &sum, &inv);
            ok[i] = sum == iSaveGetU16(blk[i] + 0x1FFC) && inv == iSaveGetU16(blk[i] + 0x1FFE);
            code[i] = (S16)iSaveGetU16(blk[i] + 0x1FFA);
        }
    }

    if (ok[0] && ok[1])
    {
        return (S16)(code[0] - code[1]) < 0 ? 1 : 0;
    }
    return ok[0] ? 0 : ok[1] ? 1 : -1;
}

bool iSaveCardFiles(const U8* img, size_t n, std::vector<std::vector<U8> >& gcis, char* err,
                    size_t errsize)
{
    gcis.clear();

    if (n < 5 * ISF_BLOCK_SIZE || n % ISF_BLOCK_SIZE != 0)
    {
        iSF_err(err, errsize,
                "not a memory card image: %lu bytes is not a whole number of 8 KB blocks",
                (unsigned long)n);
        return false;
    }

    size_t blocks = n / ISF_BLOCK_SIZE;
    U16 mbits = iSaveGetU16(img + 0x22);
    if (mbits != 0 && (size_t)mbits * 16 <= blocks)
    {
        blocks = (size_t)mbits * 16;
    }

    S32 d = iSF_pickCopy(img + 1 * ISF_BLOCK_SIZE, img + 2 * ISF_BLOCK_SIZE, false);
    S32 f = iSF_pickCopy(img + 3 * ISF_BLOCK_SIZE, img + 4 * ISF_BLOCK_SIZE, true);
    if (d < 0 || f < 0)
    {
        iSF_err(err, errsize, "the card's %s is damaged in both copies",
                d < 0 ? "directory" : "block map");
        return false;
    }

    const U8* dir = img + (1 + d) * ISF_BLOCK_SIZE;
    const U8* fat = img + (3 + f) * ISF_BLOCK_SIZE;

    for (S32 i = 0; i < 127; i++)
    {
        const U8* e = dir + i * ISF_DENTRY_SIZE;
        if (e[0] == 0xFF) // a free entry, as the SDK tests it
        {
            continue;
        }

        U16 start = iSaveGetU16(e + 0x36);
        U16 length = iSaveGetU16(e + 0x38);

        std::vector<U8> gci(ISF_DENTRY_SIZE + (size_t)length * ISF_BLOCK_SIZE);
        memcpy(&gci[0], e, ISF_DENTRY_SIZE);

        U16 b = start;
        bool ok = length > 0;
        for (U16 k = 0; ok && k < length; k++)
        {
            if (b < 5 || b >= blocks)
            {
                ok = false;
                break;
            }
            memcpy(&gci[ISF_DENTRY_SIZE + (size_t)k * ISF_BLOCK_SIZE],
                   img + (size_t)b * ISF_BLOCK_SIZE, ISF_BLOCK_SIZE);
            b = iSaveGetU16(fat + (size_t)b * 2);
        }

        if (ok)
        {
            gcis.push_back(gci);
        }
    }

    return true;
}

bool iSaveCardCreate(S32 sizeMbits, const std::vector<std::vector<U8> >& gcis, std::vector<U8>& img,
                     char* err, size_t errsize)
{
    size_t blocks = (size_t)sizeMbits * 16;
    if (sizeMbits < 4 || blocks > 0x1000)
    {
        iSF_err(err, errsize, "card size %ld Mbit", (long)sizeMbits);
        return false;
    }

    img.assign(blocks * ISF_BLOCK_SIZE, 0xFF);

    // The ID block (CARDID): an arbitrary serial, ANSI.
    U8* id = &img[0];
    for (S32 i = 0; i < 32; i++)
    {
        id[i] = (U8)(0x5A ^ i);
    }
    iSavePutU16(id + 0x20, 0);
    iSavePutU16(id + 0x22, (U16)sizeMbits);
    iSavePutU16(id + 0x24, 0);
    U16 sum;
    U16 inv;
    iSF_cardSum(id, 0x1FC, &sum, &inv);
    iSavePutU16(id + 0x1FC, sum);
    iSavePutU16(id + 0x1FE, inv);

    std::vector<U8> dir(ISF_BLOCK_SIZE, 0xFF);
    std::vector<U8> fat(ISF_BLOCK_SIZE, 0x00);

    U16 next = 5;
    for (size_t i = 0; i < gcis.size(); i++)
    {
        iSaveGCI g;
        if (i >= 127 || !iSaveGCIParse(&gcis[i][0], gcis[i].size(), &g, err, errsize))
        {
            return false;
        }

        if ((size_t)next + g.length > blocks)
        {
            iSF_err(err, errsize, "the files do not fit on a %ld Mbit card", (long)sizeMbits);
            return false;
        }

        U8* e = &dir[i * ISF_DENTRY_SIZE];
        memcpy(e, g.dentry, ISF_DENTRY_SIZE);
        iSavePutU16(e + 0x36, next);

        for (U16 k = 0; k < g.length; k++)
        {
            U16 b = (U16)(next + k);
            memcpy(&img[(size_t)b * ISF_BLOCK_SIZE], g.data + (size_t)k * ISF_BLOCK_SIZE,
                   ISF_BLOCK_SIZE);
            iSavePutU16(&fat[(size_t)b * 2], (k + 1 < g.length) ? (U16)(b + 1) : (U16)0xFFFF);
        }
        next = (U16)(next + g.length);
    }

    iSavePutU16(&fat[6], (U16)(blocks - next)); // free blocks
    iSavePutU16(&fat[8], (U16)(next - 1)); // last allocated

    // Both copies of each, the second the newer.
    for (S32 copy = 0; copy < 2; copy++)
    {
        U8* dd = &img[(1 + copy) * ISF_BLOCK_SIZE];
        memcpy(dd, &dir[0], ISF_BLOCK_SIZE);
        iSavePutU16(dd + 0x1FFA, (U16)copy);
        iSF_cardSum(dd, ISF_BLOCK_SIZE - 4, &sum, &inv);
        iSavePutU16(dd + 0x1FFC, sum);
        iSavePutU16(dd + 0x1FFE, inv);

        U8* ff = &img[(3 + copy) * ISF_BLOCK_SIZE];
        memcpy(ff, &fat[0], ISF_BLOCK_SIZE);
        iSavePutU16(ff + 4, (U16)copy);
        iSF_cardSum(ff + 4, ISF_BLOCK_SIZE - 4, &sum, &inv);
        iSavePutU16(ff, sum);
        iSavePutU16(ff + 2, inv);
    }

    return true;
}

// --- clocks ------------------------------------------------------------------------

// Days from 1970-01-01 to a civil date (proleptic Gregorian).
static S64 iSF_daysFromCivil(S64 y, S32 m, S32 d)
{
    y -= m <= 2;
    S64 era = (y >= 0 ? y : y - 399) / 400;
    S64 yoe = y - era * 400;
    S64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    S64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static const S64 GC_EPOCH_DAYS = 10957; // 2000-01-01, in days since 1970

U32 iSaveGCTimeFromUnix(S64 unixSeconds)
{
    iHostCalendar t;
    iHostLocalTimeOf(unixSeconds, &t);

    S64 days = iSF_daysFromCivil(t.year + 1900, t.mon + 1, t.mday) - GC_EPOCH_DAYS;
    S64 secs = days * 86400 + t.hour * 3600 + t.min * 60 + t.sec;
    return secs < 0 ? 0 : (U32)secs;
}

static void iSF_civil(U32 gcSeconds, S32* y, S32* mo, S32* d, S32* h, S32* mi, S32* s)
{
    S64 z = (S64)(gcSeconds / 86400) + GC_EPOCH_DAYS + 719468;
    U32 rem = gcSeconds % 86400;
    S64 era = (z >= 0 ? z : z - 146096) / 146097;
    S64 doe = z - era * 146097;
    S64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    S64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    S64 mp = (5 * doy + 2) / 153;
    *d = (S32)(doy - (153 * mp + 2) / 5 + 1);
    *mo = (S32)(mp < 10 ? mp + 3 : mp - 9);
    *y = (S32)(yoe + era * 400 + (*mo <= 2));
    *h = (S32)(rem / 3600);
    *mi = (S32)(rem / 60 % 60);
    *s = (S32)(rem % 60);
}

S64 iSaveUnixFromGCTime(U32 gcSeconds)
{
    S32 y, mo, d, h, mi, s;
    iSF_civil(gcSeconds, &y, &mo, &d, &h, &mi, &s);

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = -1;
    time_t r = mktime(&t);
    return r == (time_t)-1 ? 946684800 + (S64)gcSeconds : (S64)r;
}

void iSaveFormatStamp(U32 gcSeconds, bool pal, char* out, size_t outsize)
{
    S32 y, mo, d, h, mi, s;
    iSF_civil(gcSeconds, &y, &mo, &d, &h, &mi, &s);

    if (pal)
    {
        snprintf(out, outsize, "%02d.%02d.%04d %02d:%02d:%02d", d, mo, y, h, mi, s);
    }
    else
    {
        snprintf(out, outsize, "%02d/%02d/%04d %02d:%02d:%02d", mo, d, y, h, mi, s);
    }
}
