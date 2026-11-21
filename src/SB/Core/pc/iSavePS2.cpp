// PC-only: the PlayStation 2 release's save files. See iSavePS2.h and
// docs/SAVES.md section 8.
//
// Everything a file says about itself (counts, sizes, cluster numbers) is
// checked against what is really there before it is used: these files come
// from anywhere.
#include "iSavePS2.h"
#include "iSaveAES.h"
#include "iSaveFormat.h"
#include "iSaveSHA1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iP2_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

static U32 iP2_get16(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8);
}

static U32 iP2_get32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

static void iP2_put16(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
}

static void iP2_put32(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
    p[2] = (U8)(v >> 16);
    p[3] = (U8)(v >> 24);
}

static void iP2_putF32(U8* p, float f)
{
    U32 v;
    memcpy(&v, &f, 4);
    iP2_put32(p, v);
}

// A fixed-size name field as a string.
static std::string iP2_name(const U8* p, size_t max)
{
    size_t n = 0;
    while (n < max && p[n] != 0)
    {
        n++;
    }
    return std::string((const char*)p, n);
}

void iSavePS2FolderClear(iSavePS2Folder* f)
{
    f->name.clear();
    f->mode = IPS2_MODE_DIR;
    f->attr = 0;
    f->created = 0;
    f->modified = 0;
    f->files.clear();
}

iSavePS2File* iSavePS2Find(iSavePS2Folder* f, const char* name)
{
    for (size_t i = 0; i < f->files.size(); i++)
    {
        if (f->files[i].name == name)
        {
            return &f->files[i];
        }
    }
    return NULL;
}

const iSavePS2File* iSavePS2Find(const iSavePS2Folder* f, const char* name)
{
    return iSavePS2Find((iSavePS2Folder*)f, name);
}

// --- names -----------------------------------------------------------------------------

bool iSavePS2IsGameFolder(const char* name, bool* pal)
{
    static const char* const kNames[2] = { IPS2_FOLDER_NTSC, IPS2_FOLDER_PAL };
    size_t n = strlen(name);

    for (int r = 0; r < 2; r++)
    {
        size_t full = strlen(kNames[r]);
        bool same = (n == full || n == full - 1) && strncmp(name, kNames[r], full - 1) == 0 &&
                    (n == full - 1 || name[full - 1] == ' ' || name[full - 1] == '_');
        if (same)
        {
            if (pal != NULL)
            {
                *pal = (r == 1);
            }
            return true;
        }
    }
    return false;
}

S32 iSavePS2GameOfName(const char* name)
{
    size_t bl = strlen(ISF_FILE_BASE);
    if (strncmp(name, ISF_FILE_BASE, bl) == 0 && name[bl] == '0' && name[bl + 1] >= '1' &&
        name[bl + 1] <= '3' && name[bl + 2] == '\0')
    {
        return name[bl + 1] - '1';
    }
    return -1;
}

void iSavePS2SaveName(S32 game, char* out, size_t outsize)
{
    snprintf(out, outsize, "%s%02d", ISF_FILE_BASE, (int)game + 1);
}

std::string iSavePS2HostName(const char* name)
{
    std::string s = name;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (strchr("\\%:|\"<>/?*", s[i]) != NULL || (unsigned char)s[i] < 0x20)
        {
            s[i] = '_';
        }
    }
    for (size_t i = s.size(); i > 0 && (s[i - 1] == ' ' || s[i - 1] == '.'); i--)
    {
        s[i - 1] = '_';
    }
    return s;
}

// --- time ------------------------------------------------------------------------------

#define IP2_JAPAN (9 * 3600)

// Days since 1970-01-01 of a date, and back (the usual civil-calendar sums).
static S64 iP2_daysFromCivil(S64 y, S64 m, S64 d)
{
    y -= (m <= 2) ? 1 : 0;
    S64 era = (y >= 0 ? y : y - 399) / 400;
    S64 yoe = y - era * 400;
    S64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    S64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void iP2_civilFromDays(S64 z, S64* y, S64* m, S64* d)
{
    z += 719468;
    S64 era = (z >= 0 ? z : z - 146096) / 146097;
    S64 doe = z - era * 146097;
    S64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    S64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    S64 mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yoe + era * 400 + (*m <= 2 ? 1 : 0);
}

void iSavePS2TimePack(S64 unixSeconds, U8 out[8])
{
    S64 t = unixSeconds + IP2_JAPAN;
    if (unixSeconds <= 0 || t < 0)
    {
        t = 946684800 + IP2_JAPAN; // 2000-01-01, the console's own epoch
    }

    S64 days = t / 86400;
    S64 secs = t % 86400;
    S64 y;
    S64 m;
    S64 d;
    iP2_civilFromDays(days, &y, &m, &d);

    out[0] = 0;
    out[1] = (U8)(secs % 60);
    out[2] = (U8)((secs / 60) % 60);
    out[3] = (U8)(secs / 3600);
    out[4] = (U8)d;
    out[5] = (U8)m;
    iP2_put16(out + 6, (U32)y);
}

S64 iSavePS2TimeUnpack(const U8 in[8])
{
    U32 year = iP2_get16(in + 6);
    if (year < 1970 || year > 2200 || in[5] < 1 || in[5] > 12 || in[4] < 1 || in[4] > 31 ||
        in[3] > 23 || in[2] > 59 || in[1] > 59)
    {
        return 0;
    }
    S64 t = iP2_daysFromCivil(year, in[5], in[4]) * 86400 + in[3] * 3600 + in[2] * 60 + in[1] -
            IP2_JAPAN;
    return t > 0 ? t : 0;
}

// --- directory entries -------------------------------------------------------------------
//
// The card's own, 512 bytes, which .psu files carry as they are:
//   0x00 u32 mode   0x04 u32 length (bytes; for a folder, entries)
//   0x08 created    0x10 u32 first cluster   0x14 u32 entry in the parent
//   0x18 modified   0x20 u32 attributes      0x40 name[32]

static void iP2_entryMake(U8* e, U32 mode, U32 length, S64 created, U32 cluster, U32 dirEntry,
                          S64 modified, U32 attr, const char* name)
{
    memset(e, 0, IPS2_ENTRY_SIZE);
    iP2_put32(e, mode);
    iP2_put32(e + 4, length);
    iSavePS2TimePack(created, e + 8);
    iP2_put32(e + 0x10, cluster);
    iP2_put32(e + 0x14, dirEntry);
    iSavePS2TimePack(modified, e + 0x18);
    iP2_put32(e + 0x20, attr);
    size_t n = strlen(name);
    memcpy(e + 0x40, name, n < 32 ? n : 32);
}

// --- the files beside the saves ----------------------------------------------------------

// ASCII as the full-width Shift-JIS the console's browser shows.
static size_t iP2_sjis(const char* text, U8* out, size_t outsize)
{
    size_t n = 0;
    for (const char* c = text; *c != '\0' && n + 2 <= outsize; c++)
    {
        U32 code = 0x8140; // a full-width space
        if (*c >= 'A' && *c <= 'Z')
        {
            code = 0x8260 + (U32)(*c - 'A');
        }
        else if (*c >= 'a' && *c <= 'z')
        {
            code = 0x8281 + (U32)(*c - 'a');
        }
        else if (*c >= '0' && *c <= '9')
        {
            code = 0x824F + (U32)(*c - '0');
        }
        else if (*c == '-')
        {
            code = 0x817C;
        }
        out[n++] = (U8)(code >> 8);
        out[n++] = (U8)code;
    }
    return n;
}

void iSavePS2IconSys(std::vector<U8>& out)
{
    out.assign(IPS2_ICON_SYS_SIZE, 0);
    U8* p = &out[0];

    memcpy(p, "PS2D", 4);
    iP2_put16(p + 6, 22); // the title breaks after "SpongeBob -": 11 characters
    iP2_put32(p + 0x0C, 0x70); // how see-through the browser's background is

    // The background's four corners.
    static const U32 kBg[4][3] = {
        { 65, 128, 115 }, { 65, 128, 115 }, { 10, 79, 110 }, { 10, 79, 110 }
    };
    for (int i = 0; i < 4; i++)
    {
        for (int k = 0; k < 3; k++)
        {
            iP2_put32(p + 0x10 + i * 16 + k * 4, kBg[i][k]);
        }
    }

    // Three lights: where each points and its colour; then the ambient light.
    static const float kDir[3][3] = { { 1, 0, 1 }, { -1, 0, 1 }, { 0, 2, 0 } };
    static const float kLight[3] = { 0.3f, 0.3f, 0.4f };
    for (int i = 0; i < 3; i++)
    {
        for (int k = 0; k < 3; k++)
        {
            iP2_putF32(p + 0x50 + i * 16 + k * 4, kDir[i][k]);
            iP2_putF32(p + 0x80 + i * 16 + k * 4, kLight[i]);
        }
    }
    for (int k = 0; k < 3; k++)
    {
        iP2_putF32(p + 0xB0 + k * 4, 0.35f);
    }

    iP2_sjis("SpongeBob - The Movie", p + 0xC0, 66);

    // The icon for the list, for copying and for deleting: all the same one.
    for (int i = 0; i < 3; i++)
    {
        memcpy(p + 0x104 + i * 64, IPS2_ICON_FILE, strlen(IPS2_ICON_FILE));
    }
}

void iSavePS2Filler(std::vector<U8>& out)
{
    out.assign(IPS2_FILLER_SIZE, 0xBF);
    memset(&out[0], 0, 20 * 32); // twenty lines of 32 bytes, empty here
}

namespace
{
    struct IconVertex
    {
        S32 x, y, z;
        S32 nx, ny, nz;
        S32 u, v;
    };
} // namespace

static void iP2_putVertex(std::vector<U8>& out, const IconVertex& v)
{
    U8 b[24];
    iP2_put16(b, (U32)v.x);
    iP2_put16(b + 2, (U32)v.y);
    iP2_put16(b + 4, (U32)v.z);
    iP2_put16(b + 6, 0);
    iP2_put16(b + 8, (U32)v.nx);
    iP2_put16(b + 10, (U32)v.ny);
    iP2_put16(b + 12, (U32)v.nz);
    iP2_put16(b + 14, 0);
    iP2_put16(b + 16, (U32)v.u);
    iP2_put16(b + 18, (U32)v.v);
    b[20] = 127;
    b[21] = 127;
    b[22] = 127;
    b[23] = 255;
    out.insert(out.end(), b, b + 24);
}

void iSavePS2OwnIcon(std::vector<U8>& out)
{
    out.clear();

    // The model: a slab standing on the floor (up is -y, 4096 to the unit),
    // the picture on all six sides.
    const S32 x0 = -3200, x1 = 3200;
    const S32 y0 = -7000, y1 = -600;
    const S32 z0 = -1300, z1 = 1300;

    // Each side: its normal and four corners, round the side.
    static const int kFace[6][4][3] = {
        { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } }, // front
        { { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } }, // back
        { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } }, // left
        { { 1, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 } }, // right
        { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } }, // top
        { { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 0 } }, // bottom
    };
    static const int kNormal[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 },
                                       { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 } };
    static const int kUV[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const int kTri[6] = { 0, 1, 2, 0, 2, 3 };

    U8 head[20];
    iP2_put32(head, 0x010000);
    iP2_put32(head + 4, 1); // one shape
    iP2_put32(head + 8, 0x07); // a texture, not packed
    iP2_putF32(head + 12, 1.0f);
    iP2_put32(head + 16, 36);
    out.insert(out.end(), head, head + 20);

    for (int f = 0; f < 6; f++)
    {
        for (int t = 0; t < 6; t++)
        {
            int c = kTri[t];
            IconVertex v;
            v.x = kFace[f][c][0] ? x1 : x0;
            v.y = kFace[f][c][1] ? y1 : y0;
            v.z = kFace[f][c][2] ? z1 : z0;
            v.nx = kNormal[f][0] * 4096;
            v.ny = kNormal[f][1] * 4096;
            v.nz = kNormal[f][2] * 4096;
            v.u = kUV[c][0] * 4096;
            v.v = kUV[c][1] * 4096;
            iP2_putVertex(out, v);
        }
    }

    // One frame that never changes: the shape, at full weight.
    U8 anim[36];
    iP2_put32(anim, 1);
    iP2_put32(anim + 4, 1); // frame length
    iP2_putF32(anim + 8, 1.0f); // speed
    iP2_put32(anim + 12, 0);
    iP2_put32(anim + 16, 1); // frames
    iP2_put32(anim + 20, 0); // the shape
    iP2_put32(anim + 24, 1); // keys
    iP2_putF32(anim + 28, 1.0f);
    iP2_putF32(anim + 32, 1.0f);
    out.insert(out.end(), anim, anim + 36);

    // The picture, 128 x 128, five bits a colour: the port's own sponge block,
    // a yellow square with a darker rim and a few holes.
    static const int kHoles[9][3] = { { 30, 28, 11 }, { 78, 22, 7 },   { 104, 50, 12 },
                                      { 52, 62, 9 },  { 22, 84, 8 },   { 84, 92, 13 },
                                      { 50, 106, 6 }, { 112, 104, 5 }, { 16, 52, 4 } };
    size_t at = out.size();
    out.resize(at + 128 * 128 * 2);
    for (int y = 0; y < 128; y++)
    {
        for (int x = 0; x < 128; x++)
        {
            int r = 250, g = 212, b = 60;

            int edge = x < y ? x : y;
            edge = (127 - x) < edge ? 127 - x : edge;
            edge = (127 - y) < edge ? 127 - y : edge;
            if (edge < 5)
            {
                r = 214, g = 168, b = 34;
            }

            for (int h = 0; h < 9; h++)
            {
                int dx = x - kHoles[h][0];
                int dy = y - kHoles[h][1];
                int rr = kHoles[h][2] * kHoles[h][2];
                int dd = dx * dx + dy * dy;
                if (dd <= rr)
                {
                    // Darker toward the top left, as a dent lit from there.
                    bool shade = (dx + dy) < 0 && dd > rr / 3;
                    r = shade ? 176 : 206;
                    g = shade ? 132 : 160;
                    b = shade ? 22 : 30;
                }
            }

            U32 px = 0x8000u | (U32)(r >> 3) | ((U32)(g >> 3) << 5) | ((U32)(b >> 3) << 10);
            iP2_put16(&out[at + (size_t)(y * 128 + x) * 2], px);
        }
    }
}

bool iSavePS2IconCheck(const U8* p, size_t n, char* err, size_t errsize)
{
    if (p == NULL || n < 20 || iP2_get32(p) != 0x010000)
    {
        iP2_err(err, errsize, "not a PlayStation 2 icon (it does not start with the icon header)");
        return false;
    }

    U32 shapes = iP2_get32(p + 4);
    U32 texture = iP2_get32(p + 8);
    U32 vertices = iP2_get32(p + 16);
    if (shapes < 1 || shapes > 64 || vertices < 3 || vertices > 65536 || vertices % 3 != 0)
    {
        iP2_err(err, errsize, "the icon's shape or vertex count is not possible");
        return false;
    }

    size_t at = 20 + (size_t)vertices * (shapes * 8 + 16);
    if (at + 20 > n || iP2_get32(p + at) != 1)
    {
        iP2_err(err, errsize, "the icon is cut short (its animation is not where it should be)");
        return false;
    }

    U32 frames = iP2_get32(p + at + 16);
    at += 20;
    if (frames > 1024)
    {
        iP2_err(err, errsize, "the icon's frame count is not possible");
        return false;
    }
    for (U32 i = 0; i < frames; i++)
    {
        if (at + 8 > n)
        {
            iP2_err(err, errsize, "the icon is cut short");
            return false;
        }
        U32 keys = iP2_get32(p + at + 4);
        if (keys > 4096)
        {
            iP2_err(err, errsize, "the icon's key count is not possible");
            return false;
        }
        at += 8 + (size_t)keys * 8;
    }

    if ((texture & 0x08) != 0)
    {
        if (at + 4 > n)
        {
            iP2_err(err, errsize, "the icon is cut short");
            return false;
        }
        U32 packed = iP2_get32(p + at);
        at += 4;
        if (packed > n - at)
        {
            iP2_err(err, errsize, "the icon is cut short");
            return false;
        }
        at += packed;
    }
    else
    {
        at += 128 * 128 * 2;
    }

    if (at != n)
    {
        iP2_err(err, errsize, "the icon's parts add up to %lu bytes, the file is %lu",
                (unsigned long)at, (unsigned long)n);
        return false;
    }
    return true;
}

namespace
{
    struct HipChunk
    {
        U32 tag;
        size_t data;
        size_t size;
    };
} // namespace

// The chunk at `at`, which must end by `end`. A package's own numbers are
// big-endian on every console.
static bool iP2_chunk(const U8* p, size_t at, size_t end, HipChunk* c)
{
    if (at > end || end - at < 8)
    {
        return false;
    }
    c->tag = iSaveGetU32(p + at);
    c->size = iSaveGetU32(p + at + 4);
    c->data = at + 8;
    return c->size <= end - c->data;
}

bool iSavePS2IconFromPackage(const U8* hip, size_t n, std::vector<U8>& icon)
{
    icon.clear();

    HipChunk top;
    if (hip == NULL || !iP2_chunk(hip, 0, n, &top) || top.tag != ISF_TAG('H', 'I', 'P', 'A'))
    {
        return false;
    }

    for (size_t at = top.data + top.size; iP2_chunk(hip, at, n, &top); at = top.data + top.size)
    {
        if (top.tag != ISF_TAG('D', 'I', 'C', 'T'))
        {
            continue;
        }

        HipChunk toc;
        size_t dictEnd = top.data + top.size;
        for (size_t t = top.data; iP2_chunk(hip, t, dictEnd, &toc); t = toc.data + toc.size)
        {
            if (toc.tag != ISF_TAG('A', 'T', 'O', 'C'))
            {
                continue;
            }

            HipChunk a;
            size_t tocEnd = toc.data + toc.size;
            for (size_t q = toc.data; iP2_chunk(hip, q, tocEnd, &a); q = a.data + a.size)
            {
                // AHDR: id, type, offset, size, ...
                if (a.tag != ISF_TAG('A', 'H', 'D', 'R') || a.size < 16 ||
                    iSaveGetU32(hip + a.data) != IPS2_ICON_ASSET)
                {
                    continue;
                }

                size_t offset = iSaveGetU32(hip + a.data + 8);
                size_t size = iSaveGetU32(hip + a.data + 12);
                if (offset > n || size > n - offset ||
                    !iSavePS2IconCheck(hip + offset, size, NULL, 0))
                {
                    return false;
                }
                icon.assign(hip + offset, hip + offset + size);
                return true;
            }
        }
    }
    return false;
}

// --- .psu ------------------------------------------------------------------------------

bool iSavePS2PsuIs(const U8* p, size_t n)
{
    if (p == NULL || n < 3 * IPS2_ENTRY_SIZE)
    {
        return false;
    }
    U32 mode = iP2_get32(p);
    U32 count = iP2_get32(p + 4);
    return (mode & IPS2_MODE_ISDIR) != 0 && (mode & 0xFFFF0000u) == 0 && count >= 2 &&
           count <= 4096 && p[0x40] != 0 && iP2_name(p + IPS2_ENTRY_SIZE + 0x40, 32) == "." &&
           iP2_name(p + 2 * IPS2_ENTRY_SIZE + 0x40, 32) == "..";
}

bool iSavePS2PsuParse(const U8* p, size_t n, iSavePS2Folder* out, char* err, size_t errsize)
{
    iSavePS2FolderClear(out);
    if (!iSavePS2PsuIs(p, n))
    {
        iP2_err(err, errsize, "not a .psu file");
        return false;
    }

    out->name = iP2_name(p + 0x40, 32);
    out->mode = iP2_get32(p);
    out->attr = iP2_get32(p + 0x20);
    out->created = iSavePS2TimeUnpack(p + 8);
    out->modified = iSavePS2TimeUnpack(p + 0x18);

    U32 count = iP2_get32(p + 4);
    size_t at = IPS2_ENTRY_SIZE;
    for (U32 i = 0; i < count; i++)
    {
        if (n - at < IPS2_ENTRY_SIZE)
        {
            iP2_err(err, errsize, "the .psu file is cut short (%u of its %u entries are there)",
                    (unsigned)i, (unsigned)count);
            return false;
        }
        const U8* e = p + at;
        at += IPS2_ENTRY_SIZE;

        U32 mode = iP2_get32(e);
        if ((mode & IPS2_MODE_ISFILE) == 0)
        {
            continue; // "." and "..": nothing follows them
        }

        U32 length = iP2_get32(e + 4);
        size_t padded = ((size_t)length + 1023) & ~(size_t)1023;
        if (length > n - at)
        {
            iP2_err(err, errsize, "the .psu file is cut short (in %s)",
                    iP2_name(e + 0x40, 32).c_str());
            return false;
        }

        iSavePS2File f;
        f.name = iP2_name(e + 0x40, 32);
        f.data.assign(p + at, p + at + length);
        f.mode = mode;
        f.attr = iP2_get32(e + 0x20);
        f.created = iSavePS2TimeUnpack(e + 8);
        f.modified = iSavePS2TimeUnpack(e + 0x18);
        out->files.push_back(f);

        at += (padded <= n - at) ? padded : (n - at);
    }
    return true;
}

void iSavePS2PsuBuild(const iSavePS2Folder* folder, std::vector<U8>& out)
{
    out.clear();
    U8 e[IPS2_ENTRY_SIZE];

    U32 mode = (folder->mode & IPS2_MODE_ISDIR) ? folder->mode : IPS2_MODE_DIR;
    iP2_entryMake(e, mode, (U32)folder->files.size() + 2, folder->created, 0, 0, folder->modified,
                  folder->attr, folder->name.c_str());
    out.insert(out.end(), e, e + sizeof(e));
    iP2_entryMake(e, IPS2_MODE_DIR, 0, folder->created, 0, 0, folder->modified, 0, ".");
    out.insert(out.end(), e, e + sizeof(e));
    iP2_entryMake(e, IPS2_MODE_DIR, 0, folder->created, 0, 0, folder->modified, 0, "..");
    out.insert(out.end(), e, e + sizeof(e));

    for (size_t i = 0; i < folder->files.size(); i++)
    {
        const iSavePS2File& f = folder->files[i];
        iP2_entryMake(e, (f.mode & IPS2_MODE_ISFILE) ? f.mode : IPS2_MODE_FILE, (U32)f.data.size(),
                      f.created, 0, 0, f.modified, f.attr, f.name.c_str());
        out.insert(out.end(), e, e + sizeof(e));
        out.insert(out.end(), f.data.begin(), f.data.end());
        out.resize(out.size() + ((1024 - f.data.size() % 1024) % 1024), 0);
    }
}

// --- .max ------------------------------------------------------------------------------
//
// "Ps2PowerSave", a checksum, the folder's name and its icon.sys title name
// (32 bytes each), the packed size, the file count and the unpacked size; then
// the files packed with LZARI (Okumura's LZSS with arithmetic coding, a
// 4096-byte window that starts as spaces). Unpacked, each file is its length,
// its name[32] and its bytes, the next one starting 8 short of a 16-byte line.

namespace
{
    const int kN = 4096;
    const int kF = 60;
    const int kThreshold = 2;
    const int kM = 15;
    const U32 kQ1 = 1u << kM;
    const U32 kQ2 = 2u << kM;
    const U32 kQ3 = 3u << kM;
    const U32 kQ4 = 4u << kM;
    const U32 kMaxCum = kQ1 - 1;
    const int kNChar = 256 - kThreshold + kF;

    struct Lzari
    {
        const U8* src;
        size_t n;
        size_t pos;
        U32 buffer;
        U32 mask;

        int charToSym[kNChar];
        int symToChar[kNChar + 1];
        U32 symFreq[kNChar + 1];
        U32 symCum[kNChar + 1];
        U32 positionCum[kN + 1];

        U32 low;
        U32 high;
        U32 value;

        U32 bit()
        {
            if (mask == 0)
            {
                buffer = (pos < n) ? src[pos] : 0;
                pos++;
                mask = 128;
            }
            U32 r = (buffer & mask) ? 1 : 0;
            mask >>= 1;
            return r;
        }

        void start()
        {
            pos = 0;
            buffer = 0;
            mask = 0;

            symCum[kNChar] = 0;
            for (int sym = kNChar; sym >= 1; sym--)
            {
                int ch = sym - 1;
                charToSym[ch] = sym;
                symToChar[sym] = ch;
                symFreq[sym] = 1;
                symCum[sym - 1] = symCum[sym] + symFreq[sym];
            }
            symFreq[0] = 0;
            symToChar[0] = 0;

            positionCum[kN] = 0;
            for (int i = kN; i >= 1; i--)
            {
                positionCum[i - 1] = positionCum[i] + 10000 / (U32)(i + 200);
            }

            low = 0;
            high = kQ4;
            value = 0;
            for (int i = 0; i < kM + 2; i++)
            {
                value = 2 * value + bit();
            }
        }

        void update(int sym)
        {
            if (symCum[0] >= kMaxCum)
            {
                U32 c = 0;
                for (int i = kNChar; i > 0; i--)
                {
                    symCum[i] = c;
                    symFreq[i] = (symFreq[i] + 1) >> 1;
                    c += symFreq[i];
                }
                symCum[0] = c;
            }

            int i = sym;
            while (symFreq[i] == symFreq[i - 1])
            {
                i--;
            }
            if (i < sym)
            {
                int chI = symToChar[i];
                int chSym = symToChar[sym];
                symToChar[i] = chSym;
                symToChar[sym] = chI;
                charToSym[chI] = sym;
                charToSym[chSym] = i;
            }
            symFreq[i]++;
            while (--i >= 0)
            {
                symCum[i]++;
            }
        }

        // FALSE when the numbers have stopped making sense (a damaged file).
        bool narrow(U32 cumLow, U32 cumHigh, U32 total)
        {
            U32 range = high - low;
            high = low + (U32)(((U64)range * cumHigh) / total);
            low = low + (U32)(((U64)range * cumLow) / total);

            for (;;)
            {
                if (low >= kQ2)
                {
                    value -= kQ2;
                    low -= kQ2;
                    high -= kQ2;
                }
                else if (low >= kQ1 && high <= kQ3)
                {
                    value -= kQ1;
                    low -= kQ1;
                    high -= kQ1;
                }
                else if (high > kQ2)
                {
                    break;
                }
                low += low;
                high += high;
                value = 2 * value + bit();
                if (pos > n + 16)
                {
                    return false;
                }
            }
            return high > low && value >= low && value <= high;
        }

        bool decodeChar(int* ch)
        {
            U32 range = high - low;
            if (range == 0 || value < low)
            {
                return false;
            }
            U32 x = (U32)((((U64)(value - low) + 1) * symCum[0] - 1) / range);

            int i = 1;
            int j = kNChar;
            while (i < j)
            {
                int k = (i + j) / 2;
                if (symCum[k] > x)
                {
                    i = k + 1;
                }
                else
                {
                    j = k;
                }
            }
            int sym = i;
            if (!narrow(symCum[sym], symCum[sym - 1], symCum[0]))
            {
                return false;
            }
            *ch = symToChar[sym];
            update(sym);
            return true;
        }

        bool decodePosition(int* position)
        {
            U32 range = high - low;
            if (range == 0 || value < low)
            {
                return false;
            }
            U32 x = (U32)((((U64)(value - low) + 1) * positionCum[0] - 1) / range);

            int i = 1;
            int j = kN;
            while (i < j)
            {
                int k = (i + j) / 2;
                if (positionCum[k] > x)
                {
                    i = k + 1;
                }
                else
                {
                    j = k;
                }
            }
            int p = i - 1;
            if (!narrow(positionCum[p + 1], positionCum[p], positionCum[0]))
            {
                return false;
            }
            *position = p;
            return true;
        }
    };
} // namespace

static bool iP2_lzari(const U8* src, size_t n, size_t textsize, std::vector<U8>& out)
{
    out.clear();
    out.reserve(textsize);

    std::vector<Lzari> state(1);
    Lzari& z = state[0];
    z.src = src;
    z.n = n;
    z.start();

    std::vector<U8> text((size_t)kN, 0x20);
    int r = kN - kF;

    while (out.size() < textsize)
    {
        int c;
        if (!z.decodeChar(&c))
        {
            return false;
        }
        if (c < 256)
        {
            out.push_back((U8)c);
            text[(size_t)r] = (U8)c;
            r = (r + 1) & (kN - 1);
        }
        else
        {
            int position;
            if (!z.decodePosition(&position))
            {
                return false;
            }
            int i = (r - position - 1) & (kN - 1);
            int j = c - 255 + kThreshold;
            for (int k = 0; k < j; k++)
            {
                U8 b = text[(size_t)((i + k) & (kN - 1))];
                out.push_back(b);
                text[(size_t)r] = b;
                r = (r + 1) & (kN - 1);
            }
        }
    }

    out.resize(textsize);
    return true;
}

bool iSavePS2MaxIs(const U8* p, size_t n)
{
    return p != NULL && n >= 0x5C && memcmp(p, "Ps2PowerSave", 12) == 0;
}

bool iSavePS2MaxParse(const U8* p, size_t n, iSavePS2Folder* out, char* err, size_t errsize)
{
    iSavePS2FolderClear(out);
    if (!iSavePS2MaxIs(p, n))
    {
        iP2_err(err, errsize, "not an Action Replay Max (.max) file");
        return false;
    }

    out->name = iP2_name(p + 0x10, 32);
    U32 packed = iP2_get32(p + 0x50);
    U32 files = iP2_get32(p + 0x54);
    U32 length = iP2_get32(p + 0x58);

    if (packed < 4 || (size_t)packed > n - 0x58 || files > 256 || length > 16u * 1024u * 1024u)
    {
        iP2_err(err, errsize, "the .max file's header does not fit the file");
        return false;
    }

    std::vector<U8> body;
    if (!iP2_lzari(p + 0x5C, (size_t)packed - 4, length, body))
    {
        iP2_err(err, errsize, "the .max file's packed data is damaged");
        return false;
    }

    size_t at = 0;
    for (U32 i = 0; i < files; i++)
    {
        if (body.size() - at < 36)
        {
            iP2_err(err, errsize, "the .max file is cut short (%u of its %u files are there)",
                    (unsigned)i, (unsigned)files);
            return false;
        }
        U32 size = iP2_get32(&body[at]);
        iSavePS2File f;
        f.name = iP2_name(&body[at + 4], 32);
        at += 36;
        if ((size_t)size > body.size() - at)
        {
            iP2_err(err, errsize, "the .max file is cut short (in %s)", f.name.c_str());
            return false;
        }
        f.data.assign(body.begin() + (long)at, body.begin() + (long)(at + size));
        f.mode = IPS2_MODE_FILE;
        f.attr = 0;
        f.created = 0;
        f.modified = 0;
        out->files.push_back(f);

        at += size;
        at = ((at + 8 + 15) / 16) * 16 - 8;
        if (at > body.size())
        {
            at = body.size();
        }
    }
    return true;
}

// --- .psv ------------------------------------------------------------------------------
//
// "\0VSP", a seed and the PlayStation 3's signature, then at 0x38 a small
// header (0x3C: 2 for a PS2 save; 0x64: the file count), the folder's entry
// at 0x68 (created, modified, entries, mode, name[32]) and the files' at 0xA0,
// 0x3C bytes each: created, modified, size, mode, name[32], where its bytes
// are.

bool iSavePS2PsvIs(const U8* p, size_t n)
{
    return p != NULL && n >= 0xA0 && memcmp(p, "\0VSP", 4) == 0 && iP2_get32(p + 0x3C) == 2;
}

bool iSavePS2PsvParse(const U8* p, size_t n, iSavePS2Folder* out, char* err, size_t errsize)
{
    iSavePS2FolderClear(out);
    if (!iSavePS2PsvIs(p, n))
    {
        iP2_err(err, errsize, "not a PlayStation 2 save exported by a PlayStation 3 (.psv)");
        return false;
    }

    U32 files = iP2_get32(p + 0x64);
    if (files > 256 || (size_t)files * 0x3C > n - 0xA0)
    {
        iP2_err(err, errsize, "the .psv file's header does not fit the file");
        return false;
    }

    out->created = iSavePS2TimeUnpack(p + 0x68);
    out->modified = iSavePS2TimeUnpack(p + 0x70);
    out->mode = iP2_get32(p + 0x7C);
    out->name = iP2_name(p + 0x80, 32);

    for (U32 i = 0; i < files; i++)
    {
        const U8* e = p + 0xA0 + (size_t)i * 0x3C;
        U32 size = iP2_get32(e + 0x10);
        U32 where = iP2_get32(e + 0x38);

        iSavePS2File f;
        f.name = iP2_name(e + 0x18, 32);
        if ((size_t)where > n || (size_t)size > n - where)
        {
            iP2_err(err, errsize, "the .psv file is cut short (in %s)", f.name.c_str());
            return false;
        }
        f.data.assign(p + where, p + where + size);
        f.mode = iP2_get32(e + 0x14);
        f.attr = 0;
        f.created = iSavePS2TimeUnpack(e);
        f.modified = iSavePS2TimeUnpack(e + 8);
        out->files.push_back(f);
    }
    return true;
}

// --- .psv, written -------------------------------------------------------------------------
//
// As a PlayStation 3 lays one out (one exported by a PS3 is the reference):
//
//   0x00 "\0VSP", 0          0x08 the seed (20)        0x1C the signature (20)
//   0x30 8 zeros             0x38 0x2C, the size of what follows to 0x64
//   0x3C 2: a PS2 save       0x40 the folder's size as a .psu: 512 bytes for
//                                 each entry (the folder, ".", "..", each file)
//                                 and each file in whole kilobytes
//   0x44 icon.sys: where, how long; 0x4C, 0x54, 0x5C the same for the three
//        icons icon.sys names (list, copy, delete); 0x64 the file count
//   0x68 the folder's entry, 0xA0 the files', as iSavePS2PsvParse reads them
//   then each file's bytes, one after the other, in the folder's order.
//
// The signature (as tools that re-sign .psv files describe the PS3's): a
// 20-byte key is made from the seed -- the seed and 44 zero bytes, decrypted
// with AES-128 in CBC mode under the key file's key and IV, of which the first
// 20 bytes are kept -- and the signature is HMAC-SHA1 under that key of the
// whole file, with the signature's own 20 bytes zero. Only the first two
// blocks reach the 20 bytes kept, so only those two are decrypted here.

std::string iSavePS2PsvName(const char* folder)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string name;
    size_t n = strlen(folder);
    for (size_t i = 0; i < n; i++)
    {
        if (i < 12)
        {
            name += folder[i];
        }
        else
        {
            name += kHex[((U8)folder[i]) >> 4];
            name += kHex[((U8)folder[i]) & 15];
        }
    }
    return name + ".PSV";
}

bool iSavePS2PsvKeysParse(const U8* file, size_t n, iSavePS2PsvKeys* out, char* err, size_t errsize)
{
    iSavePS2PsvKeysClear(out);

    if (file == NULL || n != IPS2_PSV_KEYS_FILE_SIZE)
    {
        iP2_err(err, errsize,
                "a PS3 key file is 32 bytes (the key the PS3 signs PlayStation 2 saves with, "
                "then its IV); this file is %lu",
                (unsigned long)n);
        return false;
    }

    bool any = false;
    for (size_t i = 0; i < n; i++)
    {
        any = any || file[i] != 0;
    }
    if (!any)
    {
        iP2_err(err, errsize,
                "this PS3 key file holds only zeros, which would only repeat the stand-in "
                "signature");
        return false;
    }

    memcpy(out->key, file, IPS2_PSV_KEY_SIZE);
    memcpy(out->iv, file + IPS2_PSV_KEY_SIZE, IPS2_PSV_KEY_SIZE);
    return true;
}

void iSavePS2PsvKeysClear(iSavePS2PsvKeys* k)
{
    iSaveWipe(k, sizeof(*k));
}

// The HMAC key the seed makes.
static void iP2_psvHmacKey(const U8* seed, const iSavePS2PsvKeys* keys, U8 out[ISHA1_SIZE])
{
    iSavePS2PsvKeys standIn;
    iSavePS2PsvKeysClear(&standIn);
    if (keys == NULL)
    {
        keys = &standIn;
    }

    U8 buf[32];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, seed, IPS2_PSV_SEED_SIZE);

    iAES128 aes;
    iAES128Init(&aes, keys->key);
    U8 chain[IAES_BLOCK];
    memcpy(chain, keys->iv, IAES_BLOCK);
    for (int b = 0; b < 2; b++)
    {
        U8 plain[IAES_BLOCK];
        U8 cipher[IAES_BLOCK];
        memcpy(cipher, buf + b * IAES_BLOCK, IAES_BLOCK);
        iAES128Decrypt(&aes, cipher, plain);
        for (int i = 0; i < IAES_BLOCK; i++)
        {
            buf[b * IAES_BLOCK + i] = (U8)(plain[i] ^ chain[i]);
        }
        memcpy(chain, cipher, IAES_BLOCK);
        iSaveWipe(plain, sizeof(plain));
    }
    memcpy(out, buf, ISHA1_SIZE);

    iAES128Clear(&aes);
    iSaveWipe(chain, sizeof(chain));
    iSaveWipe(buf, sizeof(buf));
}

static void iP2_psvSignature(const U8* psv, size_t n, const iSavePS2PsvKeys* keys,
                             U8 out[IPS2_PSV_SIGNATURE_SIZE])
{
    U8 key[ISHA1_SIZE];
    iP2_psvHmacKey(psv + IPS2_PSV_SEED, keys, key);

    U8 zero[IPS2_PSV_SIGNATURE_SIZE];
    memset(zero, 0, sizeof(zero));

    // HMAC over the file in three parts, the signature's place as zeros.
    iSHA1 inner;
    U8 pad[64];
    memset(pad, 0x36, sizeof(pad));
    for (int i = 0; i < ISHA1_SIZE; i++)
    {
        pad[i] ^= key[i];
    }
    iSHA1Init(&inner);
    iSHA1Update(&inner, pad, sizeof(pad));
    iSHA1Update(&inner, psv, IPS2_PSV_SIGNATURE);
    iSHA1Update(&inner, zero, sizeof(zero));
    iSHA1Update(&inner, psv + IPS2_PSV_SIGNATURE + IPS2_PSV_SIGNATURE_SIZE,
                n - IPS2_PSV_SIGNATURE - IPS2_PSV_SIGNATURE_SIZE);
    U8 digest[ISHA1_SIZE];
    iSHA1Final(&inner, digest);

    memset(pad, 0x5C, sizeof(pad));
    for (int i = 0; i < ISHA1_SIZE; i++)
    {
        pad[i] ^= key[i];
    }
    iSHA1 outer;
    iSHA1Init(&outer);
    iSHA1Update(&outer, pad, sizeof(pad));
    iSHA1Update(&outer, digest, sizeof(digest));
    iSHA1Final(&outer, out);

    iSaveWipe(key, sizeof(key));
    iSaveWipe(pad, sizeof(pad));
    iSaveWipe(&inner, sizeof(inner));
    iSaveWipe(&outer, sizeof(outer));
}

void iSavePS2PsvSign(U8* psv, size_t n, const iSavePS2PsvKeys* keys)
{
    if (psv == NULL || n < 0xA0)
    {
        return;
    }
    U8 sig[IPS2_PSV_SIGNATURE_SIZE];
    iP2_psvSignature(psv, n, keys, sig);
    memcpy(psv + IPS2_PSV_SIGNATURE, sig, sizeof(sig));
}

bool iSavePS2PsvVerify(const U8* psv, size_t n, const iSavePS2PsvKeys* keys)
{
    if (psv == NULL || n < 0xA0)
    {
        return false;
    }
    U8 sig[IPS2_PSV_SIGNATURE_SIZE];
    iP2_psvSignature(psv, n, keys, sig);
    return memcmp(psv + IPS2_PSV_SIGNATURE, sig, sizeof(sig)) == 0;
}

// The file of `name` among the folder's, as its place in the .psv.
static bool iP2_psvPlace(const iSavePS2Folder* folder, const std::vector<U32>& at, const U8* name,
                         U32* where, U32* size)
{
    std::string want = iP2_name(name, 64);
    for (size_t i = 0; !want.empty() && i < folder->files.size(); i++)
    {
        if (folder->files[i].name == want)
        {
            *where = at[i];
            *size = (U32)folder->files[i].data.size();
            return true;
        }
    }
    return false;
}

void iSavePS2PsvBuild(const iSavePS2Folder* folder, const U8* seed, const iSavePS2PsvKeys* keys,
                      std::vector<U8>& out)
{
    const size_t files = folder->files.size();
    out.assign(0xA0 + files * 0x3C, 0);
    U8* p = &out[0];

    memcpy(p, "\0VSP", 4);
    for (int i = 0; i < IPS2_PSV_SEED_SIZE; i++)
    {
        p[IPS2_PSV_SEED + i] = seed != NULL ? seed[i] : (U8)i;
    }
    iP2_put32(p + 0x38, 0x2C);
    iP2_put32(p + 0x3C, 2);

    U32 psuSize = 3 * IPS2_ENTRY_SIZE;
    std::vector<U32> at(files);
    U32 where = (U32)out.size();
    for (size_t i = 0; i < files; i++)
    {
        U32 size = (U32)folder->files[i].data.size();
        at[i] = where;
        where += size;
        psuSize += IPS2_ENTRY_SIZE + ((size + 1023) & ~1023u);
    }
    iP2_put32(p + 0x40, psuSize);

    // icon.sys and the three icons it names.
    const iSavePS2File* sys = iSavePS2Find(folder, IPS2_ICON_SYS);
    U32 place;
    U32 size;
    if (iP2_psvPlace(folder, at, (const U8*)IPS2_ICON_SYS, &place, &size))
    {
        iP2_put32(p + 0x44, place);
        iP2_put32(p + 0x48, size);
    }
    for (int k = 0; sys != NULL && sys->data.size() >= IPS2_ICON_SYS_SIZE && k < 3; k++)
    {
        if (iP2_psvPlace(folder, at, &sys->data[0x104 + k * 64], &place, &size))
        {
            iP2_put32(p + 0x4C + k * 8, place);
            iP2_put32(p + 0x50 + k * 8, size);
        }
    }
    iP2_put32(p + 0x64, (U32)files);

    U8* d = p + 0x68;
    iSavePS2TimePack(folder->created, d);
    iSavePS2TimePack(folder->modified, d + 8);
    iP2_put32(d + 0x10, (U32)files + 2);
    iP2_put32(d + 0x14, (folder->mode & IPS2_MODE_ISDIR) ? folder->mode : IPS2_MODE_DIR);
    memcpy(d + 0x18, folder->name.c_str(), folder->name.size() < 32 ? folder->name.size() : 32);

    for (size_t i = 0; i < files; i++)
    {
        const iSavePS2File& f = folder->files[i];
        U8* e = p + 0xA0 + i * 0x3C;
        iSavePS2TimePack(f.created, e);
        iSavePS2TimePack(f.modified, e + 8);
        iP2_put32(e + 0x10, (U32)f.data.size());
        iP2_put32(e + 0x14, (f.mode & IPS2_MODE_ISFILE) ? f.mode : IPS2_MODE_FILE);
        memcpy(e + 0x18, f.name.c_str(), f.name.size() < 32 ? f.name.size() : 32);
        iP2_put32(e + 0x38, at[i]);
    }

    for (size_t i = 0; i < files; i++)
    {
        out.insert(out.end(), folder->files[i].data.begin(), folder->files[i].data.end());
    }

    iSavePS2PsvSign(&out[0], out.size(), keys);
}

// --- a card image ------------------------------------------------------------------------
//
// Pages of 512 bytes, two to a cluster, sixteen to an erase block. A card
// image "with ECC" has 16 spare bytes after each page: three code bytes for
// each 128-byte quarter, then four zeros.
//
//   page 0            the superblock: "Sony PS2 Memory Card Format ", the
//                     geometry, where the allocatable clusters start
//                     (alloc_offset) and how many there are (alloc_end), the
//                     root directory's cluster, and ifc_list
//   ifc_list[]        clusters of 256 numbers, each a cluster of the table
//   the table (FAT)   256 entries a cluster, one for each allocatable cluster:
//                     the top bit set for "in use", the rest the next cluster
//                     of the chain (0x7FFFFFFF: the last)
//   directories       chains of clusters, two 512-byte entries in each; a
//                     directory's first two are "." and ".."
//
// Cluster numbers in the table and in entries count from alloc_offset.

static const char kCardMagic[] = "Sony PS2 Memory Card Format ";

#define IP2_FAT_USED 0x80000000u
#define IP2_FAT_LAST 0x7FFFFFFFu
#define IP2_FAT_END 0xFFFFFFFFu
#define IP2_MAX_FILE (16u * 1024u * 1024u)

void iSavePS2Ecc(const U8* chunk, U8 ecc[3])
{
    U32 column = 0x77;
    U32 line0 = 0x7F;
    U32 line1 = 0x7F;

    for (U32 i = 0; i < 128; i++)
    {
        U32 b = chunk[i];

        // The byte's parity over each of six column groups, and over all of
        // it.
        static const U32 kGroups[6] = { 0x55, 0x33, 0x0F, 0xAA, 0xCC, 0xF0 };
        static const U32 kBit[6] = { 0x01, 0x02, 0x04, 0x10, 0x20, 0x40 };
        for (int g = 0; g < 6; g++)
        {
            U32 v = b & kGroups[g];
            v ^= v >> 4;
            v ^= v >> 2;
            v ^= v >> 1;
            if (v & 1)
            {
                column ^= kBit[g];
            }
        }

        U32 v = b;
        v ^= v >> 4;
        v ^= v >> 2;
        v ^= v >> 1;
        if (v & 1)
        {
            line0 ^= ~i;
            line1 ^= i;
        }
    }

    ecc[0] = (U8)(column & 0x77);
    ecc[1] = (U8)(line0 & 0x7F);
    ecc[2] = (U8)(line1 & 0x7F);
}

bool iSavePS2CardIs(const U8* p, size_t n)
{
    return p != NULL && n >= IPS2_PAGE_SIZE && memcmp(p, kCardMagic, 28) == 0;
}

namespace
{
    struct Card
    {
        const U8* p;
        U8* w; // NULL for a card that is only read
        size_t n;
        bool ecc;
        size_t raw; // a page's bytes in the image
        U32 clusters;
        U32 allocOffset;
        U32 allocEnd;
        U32 rootDir;
        U32 ifc[32];
        U32 hint; // where the search for a free cluster goes on from
    };

    bool cardOpen(Card* c, const U8* p, U8* w, size_t n, char* err, size_t errsize)
    {
        if (!iSavePS2CardIs(p, n))
        {
            iP2_err(err, errsize, "not a formatted PlayStation 2 memory card");
            return false;
        }

        c->p = p;
        c->w = w;
        c->n = n;
        c->hint = 0;

        U32 pageLen = iP2_get16(p + 0x28);
        U32 perCluster = iP2_get16(p + 0x2A);
        c->clusters = iP2_get32(p + 0x30);
        c->allocOffset = iP2_get32(p + 0x34);
        c->allocEnd = iP2_get32(p + 0x38);
        c->rootDir = iP2_get32(p + 0x3C);
        for (int i = 0; i < 32; i++)
        {
            c->ifc[i] = iP2_get32(p + 0x50 + i * 4);
        }

        if (pageLen != IPS2_PAGE_SIZE || perCluster != 2 || c->clusters < 64 ||
            c->clusters > 0x40000)
        {
            iP2_err(err, errsize,
                    "the card's layout is not one this port knows (%u-byte pages, %u to a "
                    "cluster)",
                    (unsigned)pageLen, (unsigned)perCluster);
            return false;
        }

        size_t pages = (size_t)c->clusters * 2;
        if (n == pages * (IPS2_PAGE_SIZE + IPS2_SPARE_SIZE))
        {
            c->ecc = true;
        }
        else if (n == pages * IPS2_PAGE_SIZE)
        {
            c->ecc = false;
        }
        else
        {
            iP2_err(err, errsize,
                    "the card file's size (%lu bytes) is not that of a card of %u clusters, "
                    "with or without the spare bytes",
                    (unsigned long)n, (unsigned)c->clusters);
            return false;
        }
        c->raw = IPS2_PAGE_SIZE + (c->ecc ? IPS2_SPARE_SIZE : 0);

        if (c->allocOffset >= c->clusters)
        {
            iP2_err(err, errsize, "the card's header is damaged");
            return false;
        }
        if (c->allocEnd > c->clusters - c->allocOffset)
        {
            c->allocEnd = c->clusters - c->allocOffset;
        }
        if (c->rootDir >= c->allocEnd)
        {
            iP2_err(err, errsize, "the card's header is damaged");
            return false;
        }
        return true;
    }

    bool readCluster(const Card* c, U32 abs, U8* buf)
    {
        if (abs >= c->clusters)
        {
            return false;
        }
        size_t page = (size_t)abs * 2;
        memcpy(buf, c->p + page * c->raw, IPS2_PAGE_SIZE);
        memcpy(buf + IPS2_PAGE_SIZE, c->p + (page + 1) * c->raw, IPS2_PAGE_SIZE);
        return true;
    }

    void writePage(Card* c, size_t page, const U8* data)
    {
        U8* at = c->w + page * c->raw;
        memcpy(at, data, IPS2_PAGE_SIZE);
        if (c->ecc)
        {
            for (int q = 0; q < 4; q++)
            {
                iSavePS2Ecc(data + q * 128, at + IPS2_PAGE_SIZE + q * 3);
            }
            memset(at + IPS2_PAGE_SIZE + 12, 0, 4);
        }
    }

    bool writeCluster(Card* c, U32 abs, const U8* buf)
    {
        if (c->w == NULL || abs >= c->clusters)
        {
            return false;
        }
        writePage(c, (size_t)abs * 2, buf);
        writePage(c, (size_t)abs * 2 + 1, buf + IPS2_PAGE_SIZE);
        return true;
    }

    // Where allocatable cluster `rel`'s table entry is.
    bool fatWhere(const Card* c, U32 rel, U32* cluster, U32* index)
    {
        if (rel >= c->allocEnd)
        {
            return false;
        }
        U32 indirect = rel / 256;
        if (indirect / 256 >= 32)
        {
            return false;
        }
        U8 buf[IPS2_CLUSTER_SIZE];
        if (!readCluster(c, c->ifc[indirect / 256], buf))
        {
            return false;
        }
        *cluster = iP2_get32(buf + (indirect % 256) * 4);
        *index = rel % 256;
        return *cluster < c->clusters;
    }

    bool fatGet(const Card* c, U32 rel, U32* value)
    {
        U32 cluster;
        U32 index;
        U8 buf[IPS2_CLUSTER_SIZE];
        if (!fatWhere(c, rel, &cluster, &index) || !readCluster(c, cluster, buf))
        {
            return false;
        }
        *value = iP2_get32(buf + index * 4);
        return true;
    }

    bool fatSet(Card* c, U32 rel, U32 value)
    {
        U32 cluster;
        U32 index;
        U8 buf[IPS2_CLUSTER_SIZE];
        if (!fatWhere(c, rel, &cluster, &index) || !readCluster(c, cluster, buf))
        {
            return false;
        }
        iP2_put32(buf + index * 4, value);
        return writeCluster(c, cluster, buf);
    }

    // The cluster after `rel` in its chain. `last` when there is none. FALSE
    // for a chain that runs into a cluster that is not in use.
    bool chainNext(const Card* c, U32 rel, U32* next, bool* last)
    {
        U32 v;
        if (!fatGet(c, rel, &v) || (v & IP2_FAT_USED) == 0)
        {
            return false;
        }
        *next = v & IP2_FAT_LAST;
        *last = (*next == IP2_FAT_LAST);
        return *last || *next < c->allocEnd;
    }

    bool allocCluster(Card* c, U32* rel)
    {
        for (U32 tries = 0; tries < c->allocEnd; tries++)
        {
            U32 at = (c->hint + tries) % c->allocEnd;
            U32 v;
            if (!fatGet(c, at, &v))
            {
                return false;
            }
            if ((v & IP2_FAT_USED) == 0)
            {
                if (!fatSet(c, at, IP2_FAT_END))
                {
                    return false;
                }
                c->hint = at + 1;
                *rel = at;
                return true;
            }
        }
        return false;
    }

    void freeChain(Card* c, U32 first)
    {
        U32 at = first;
        for (U32 guard = 0; guard < c->allocEnd && at < c->allocEnd; guard++)
        {
            U32 v;
            if (!fatGet(c, at, &v) || (v & IP2_FAT_USED) == 0)
            {
                return;
            }
            fatSet(c, at, IP2_FAT_LAST);
            if (at < c->hint)
            {
                c->hint = at;
            }
            at = v & IP2_FAT_LAST;
        }
    }

    // The cluster holding directory entry `index` of the directory starting at
    // `first`. With `extend`, the chain grows to reach it.
    bool entryCluster(Card* c, U32 first, U32 index, bool extend, U32* rel)
    {
        U32 at = first;
        for (U32 step = 0; step < index / 2; step++)
        {
            U32 next;
            bool last;
            if (step >= c->allocEnd || !chainNext(c, at, &next, &last))
            {
                return false;
            }
            if (last)
            {
                if (!extend || c->w == NULL || !allocCluster(c, &next) ||
                    !fatSet(c, at, next | IP2_FAT_USED))
                {
                    return false;
                }
                U8 blank[IPS2_CLUSTER_SIZE];
                memset(blank, 0xFF, sizeof(blank));
                if (!writeCluster(c, c->allocOffset + next, blank))
                {
                    return false;
                }
            }
            at = next;
        }
        *rel = at;
        return true;
    }

    bool readEntry(const Card* c, U32 first, U32 index, U8* e)
    {
        U32 rel;
        U8 buf[IPS2_CLUSTER_SIZE];
        if (!entryCluster((Card*)c, first, index, false, &rel) ||
            !readCluster(c, c->allocOffset + rel, buf))
        {
            return false;
        }
        memcpy(e, buf + (index % 2) * IPS2_ENTRY_SIZE, IPS2_ENTRY_SIZE);
        return true;
    }

    bool writeEntry(Card* c, U32 first, U32 index, const U8* e)
    {
        U32 rel;
        U8 buf[IPS2_CLUSTER_SIZE];
        if (!entryCluster(c, first, index, true, &rel) ||
            !readCluster(c, c->allocOffset + rel, buf))
        {
            return false;
        }
        memcpy(buf + (index % 2) * IPS2_ENTRY_SIZE, e, IPS2_ENTRY_SIZE);
        return writeCluster(c, c->allocOffset + rel, buf);
    }

    bool readData(const Card* c, U32 first, U32 length, std::vector<U8>& out)
    {
        out.clear();
        if (length > IP2_MAX_FILE)
        {
            return false;
        }
        out.resize(length);

        U32 at = first;
        for (U32 done = 0; done < length; done += IPS2_CLUSTER_SIZE)
        {
            U8 buf[IPS2_CLUSTER_SIZE];
            if (at >= c->allocEnd || !readCluster(c, c->allocOffset + at, buf))
            {
                return false;
            }
            U32 take = length - done < IPS2_CLUSTER_SIZE ? length - done : IPS2_CLUSTER_SIZE;
            memcpy(&out[done], buf, take);

            if (done + IPS2_CLUSTER_SIZE < length)
            {
                U32 next;
                bool last;
                if (!chainNext(c, at, &next, &last) || last)
                {
                    return false;
                }
                at = next;
            }
        }
        return true;
    }

    bool writeData(Card* c, const std::vector<U8>& data, U32* first)
    {
        *first = IP2_FAT_END; // what an empty file's entry holds
        U32 prev = 0;
        for (size_t done = 0; done < data.size(); done += IPS2_CLUSTER_SIZE)
        {
            U32 rel;
            if (!allocCluster(c, &rel))
            {
                return false;
            }
            U8 buf[IPS2_CLUSTER_SIZE];
            memset(buf, 0xFF, sizeof(buf));
            size_t take =
                data.size() - done < IPS2_CLUSTER_SIZE ? data.size() - done : IPS2_CLUSTER_SIZE;
            memcpy(buf, &data[done], take);
            if (!writeCluster(c, c->allocOffset + rel, buf))
            {
                return false;
            }

            if (done == 0)
            {
                *first = rel;
            }
            else if (!fatSet(c, prev, rel | IP2_FAT_USED))
            {
                return false;
            }
            prev = rel;
        }
        return true;
    }

    // The root directory's entry count, from its "." entry.
    bool rootLength(const Card* c, U32* length)
    {
        U8 e[IPS2_ENTRY_SIZE];
        if (!readEntry(c, c->rootDir, 0, e))
        {
            return false;
        }
        U32 mode = iP2_get32(e);
        *length = iP2_get32(e + 4);
        return (mode & IPS2_MODE_ISDIR) != 0 && *length >= 2 && *length <= c->allocEnd * 2;
    }

    // The root's entry for folder `name`: its index and bytes.
    bool findFolder(const Card* c, const char* name, U32 rootLen, U32* index, U8* e)
    {
        for (U32 i = 2; i < rootLen; i++)
        {
            if (!readEntry(c, c->rootDir, i, e))
            {
                return false;
            }
            U32 mode = iP2_get32(e);
            if ((mode & IPS2_MODE_EXISTS) != 0 && (mode & IPS2_MODE_ISDIR) != 0 &&
                iP2_name(e + 0x40, 32) == name)
            {
                *index = i;
                return true;
            }
        }
        return false;
    }
} // namespace

void iSavePS2CardFormat(std::vector<U8>& card, bool withEcc, S64 now)
{
    const U32 clusters = IPS2_CARD_CLUSTERS;
    const U32 allocOffset = 41; // 8: the list of table clusters; 9..40: the table
    const U32 allocEnd = 8135; // up to the two spare erase blocks at the end
    size_t raw = IPS2_PAGE_SIZE + (withEcc ? IPS2_SPARE_SIZE : 0);

    // Erased: every byte 0xFF, the spare bytes too.
    card.assign((size_t)clusters * 2 * raw, 0xFF);

    Card c;
    c.p = &card[0];
    c.w = &card[0];
    c.n = card.size();
    c.ecc = withEcc;
    c.raw = raw;
    c.clusters = clusters;
    c.allocOffset = allocOffset;
    c.allocEnd = allocEnd;
    c.rootDir = 0;
    c.hint = 0;

    U8 buf[IPS2_CLUSTER_SIZE];

    memset(buf, 0, IPS2_PAGE_SIZE);
    memcpy(buf, kCardMagic, 28);
    memcpy(buf + 0x1C, "1.2.0.0", 7);
    iP2_put16(buf + 0x28, IPS2_PAGE_SIZE);
    iP2_put16(buf + 0x2A, 2);
    iP2_put16(buf + 0x2C, 16);
    iP2_put16(buf + 0x2E, 0xFF00);
    iP2_put32(buf + 0x30, clusters);
    iP2_put32(buf + 0x34, allocOffset);
    iP2_put32(buf + 0x38, allocEnd);
    iP2_put32(buf + 0x3C, 0);
    iP2_put32(buf + 0x40, 1023); // the two erase blocks kept for interrupted writes
    iP2_put32(buf + 0x44, 1022);
    iP2_put32(buf + 0x50, 8);
    memset(buf + 0xD0, 0xFF, 32 * 4); // no bad blocks
    buf[0x150] = 2; // a PlayStation 2 card
    buf[0x151] = 0x52;
    writePage(&c, 0, buf);

    for (int i = 0; i < 32; i++)
    {
        c.ifc[i] = (i == 0) ? 8 : 0;
    }

    memset(buf, 0xFF, sizeof(buf));
    for (U32 i = 0; i < 32; i++)
    {
        iP2_put32(buf + i * 4, 9 + i);
    }
    writeCluster(&c, 8, buf);

    for (U32 t = 0; t < 32; t++)
    {
        for (U32 i = 0; i < 256; i++)
        {
            U32 rel = t * 256 + i;
            U32 v = (rel == 0 || rel >= allocEnd) ? IP2_FAT_END : IP2_FAT_LAST;
            iP2_put32(buf + i * 4, v);
        }
        writeCluster(&c, 9 + t, buf);
    }

    iP2_entryMake(buf, IPS2_MODE_DIR, 2, now, 0, 0, now, 0, ".");
    iP2_entryMake(buf + IPS2_ENTRY_SIZE, 0xA426, 0, now, 0, 0, now, 0, "..");
    writeCluster(&c, allocOffset, buf);
}

bool iSavePS2CardList(const U8* card, size_t n, std::vector<std::string>& names, char* err,
                      size_t errsize)
{
    names.clear();

    Card c;
    U32 rootLen;
    if (!cardOpen(&c, card, NULL, n, err, errsize))
    {
        return false;
    }
    if (!rootLength(&c, &rootLen))
    {
        iP2_err(err, errsize, "the card's root directory is damaged");
        return false;
    }

    for (U32 i = 2; i < rootLen; i++)
    {
        U8 e[IPS2_ENTRY_SIZE];
        if (!readEntry(&c, c.rootDir, i, e))
        {
            iP2_err(err, errsize, "the card's root directory is damaged");
            return false;
        }
        U32 mode = iP2_get32(e);
        if ((mode & IPS2_MODE_EXISTS) != 0 && (mode & IPS2_MODE_ISDIR) != 0)
        {
            names.push_back(iP2_name(e + 0x40, 32));
        }
    }
    return true;
}

bool iSavePS2CardReadFolder(const U8* card, size_t n, const char* name, iSavePS2Folder* out,
                            char* err, size_t errsize)
{
    iSavePS2FolderClear(out);

    Card c;
    U32 rootLen;
    if (!cardOpen(&c, card, NULL, n, err, errsize))
    {
        return false;
    }
    if (!rootLength(&c, &rootLen))
    {
        iP2_err(err, errsize, "the card's root directory is damaged");
        return false;
    }

    U8 d[IPS2_ENTRY_SIZE];
    U32 index;
    if (!findFolder(&c, name, rootLen, &index, d))
    {
        iP2_err(err, errsize, "the card has no folder \"%s\"", name);
        return false;
    }

    out->name = iP2_name(d + 0x40, 32);
    out->mode = iP2_get32(d);
    out->attr = iP2_get32(d + 0x20);
    out->created = iSavePS2TimeUnpack(d + 8);
    out->modified = iSavePS2TimeUnpack(d + 0x18);

    U32 length = iP2_get32(d + 4);
    U32 first = iP2_get32(d + 0x10);
    if (length > 4096 || first >= c.allocEnd)
    {
        iP2_err(err, errsize, "the card's folder \"%s\" is damaged", name);
        return false;
    }

    size_t total = 0;
    for (U32 i = 2; i < length; i++)
    {
        U8 e[IPS2_ENTRY_SIZE];
        if (!readEntry(&c, first, i, e))
        {
            iP2_err(err, errsize, "the card's folder \"%s\" is damaged", name);
            return false;
        }
        U32 mode = iP2_get32(e);
        if ((mode & IPS2_MODE_EXISTS) == 0 || (mode & IPS2_MODE_ISFILE) == 0)
        {
            continue;
        }

        // No folder holds more than the card does.
        total += iP2_get32(e + 4);
        if (iP2_get32(e + 4) > IP2_MAX_FILE || total > (size_t)c.clusters * IPS2_CLUSTER_SIZE)
        {
            iP2_err(err, errsize, "the card's folder \"%s\" is damaged", name);
            return false;
        }

        iSavePS2File f;
        f.name = iP2_name(e + 0x40, 32);
        f.mode = mode;
        f.attr = iP2_get32(e + 0x20);
        f.created = iSavePS2TimeUnpack(e + 8);
        f.modified = iSavePS2TimeUnpack(e + 0x18);
        if (!readData(&c, iP2_get32(e + 0x10), iP2_get32(e + 4), f.data))
        {
            iP2_err(err, errsize,
                    "the card's file \"%s\" is damaged (its chain of clusters "
                    "breaks off)",
                    f.name.c_str());
            return false;
        }
        out->files.push_back(f);
    }
    return true;
}

bool iSavePS2CardWriteFolder(std::vector<U8>& card, const iSavePS2Folder* folder, S64 now,
                             char* err, size_t errsize)
{
    if (folder->name.empty() || folder->name.size() > 31)
    {
        iP2_err(err, errsize, "the folder's name does not fit a card");
        return false;
    }

    // Everything is done to a copy, which takes the card's place only when all
    // of it worked.
    std::vector<U8> work = card;
    if (work.empty())
    {
        iP2_err(err, errsize, "not a formatted PlayStation 2 memory card");
        return false;
    }

    Card c;
    U32 rootLen;
    if (!cardOpen(&c, &work[0], &work[0], work.size(), err, errsize))
    {
        return false;
    }
    if (!rootLength(&c, &rootLen))
    {
        iP2_err(err, errsize, "the card's root directory is damaged");
        return false;
    }

    static const char kFull[] = "the memory card is full";
    static const char kDamaged[] = "the card's tables are damaged";

    U8 d[IPS2_ENTRY_SIZE];
    U32 dirIndex;
    U32 dirCluster;
    U32 dirLen;

    if (findFolder(&c, folder->name.c_str(), rootLen, &dirIndex, d))
    {
        dirCluster = iP2_get32(d + 0x10);
        dirLen = iP2_get32(d + 4);
        if (dirCluster >= c.allocEnd || dirLen < 2 || dirLen > 4096)
        {
            iP2_err(err, errsize, "the card's folder \"%s\" is damaged", folder->name.c_str());
            return false;
        }
    }
    else
    {
        // A new folder: a cluster with its "." and "..", and an entry in the
        // root, in a slot a deleted entry left if there is one.
        dirIndex = rootLen;
        for (U32 i = 2; i < rootLen; i++)
        {
            U8 e[IPS2_ENTRY_SIZE];
            if (!readEntry(&c, c.rootDir, i, e))
            {
                iP2_err(err, errsize, "%s", kDamaged);
                return false;
            }
            if ((iP2_get32(e) & IPS2_MODE_EXISTS) == 0)
            {
                dirIndex = i;
                break;
            }
        }

        if (!allocCluster(&c, &dirCluster))
        {
            iP2_err(err, errsize, "%s", kFull);
            return false;
        }

        S64 created = folder->created > 0 ? folder->created : now;
        U8 buf[IPS2_CLUSTER_SIZE];
        iP2_entryMake(buf, IPS2_MODE_DIR, 0, created, c.rootDir, dirIndex, created, 0, ".");
        iP2_entryMake(buf + IPS2_ENTRY_SIZE, IPS2_MODE_DIR, 0, created, 0, 0, created, 0, "..");
        writeCluster(&c, c.allocOffset + dirCluster, buf);

        dirLen = 2;
        iP2_entryMake(d, (folder->mode & IPS2_MODE_ISDIR) ? folder->mode : IPS2_MODE_DIR, dirLen,
                      created, dirCluster, 0, now, folder->attr, folder->name.c_str());
        if (!writeEntry(&c, c.rootDir, dirIndex, d))
        {
            iP2_err(err, errsize, "%s", kFull);
            return false;
        }

        if (dirIndex == rootLen)
        {
            rootLen++;
            U8 root[IPS2_ENTRY_SIZE];
            if (!readEntry(&c, c.rootDir, 0, root))
            {
                iP2_err(err, errsize, "%s", kDamaged);
                return false;
            }
            iP2_put32(root + 4, rootLen);
            iSavePS2TimePack(now, root + 0x18);
            writeEntry(&c, c.rootDir, 0, root);
        }
    }

    for (size_t k = 0; k < folder->files.size(); k++)
    {
        const iSavePS2File& f = folder->files[k];
        if (f.name.empty() || f.name.size() > 31 || f.data.size() > IP2_MAX_FILE)
        {
            iP2_err(err, errsize, "the file \"%s\" does not fit a card", f.name.c_str());
            return false;
        }

        // The file's entry if it is there, else a free slot, else a new one.
        U32 index = dirLen;
        S64 created = f.created > 0 ? f.created : now;
        for (U32 i = 2; i < dirLen; i++)
        {
            U8 e[IPS2_ENTRY_SIZE];
            if (!readEntry(&c, dirCluster, i, e))
            {
                iP2_err(err, errsize, "%s", kDamaged);
                return false;
            }
            U32 mode = iP2_get32(e);
            if ((mode & IPS2_MODE_EXISTS) == 0)
            {
                if (index == dirLen)
                {
                    index = i;
                }
                continue;
            }
            if ((mode & IPS2_MODE_ISFILE) != 0 && iP2_name(e + 0x40, 32) == f.name)
            {
                index = i;
                if (f.created <= 0 && iSavePS2TimeUnpack(e + 8) > 0)
                {
                    created = iSavePS2TimeUnpack(e + 8);
                }
                if (iP2_get32(e + 4) > 0)
                {
                    freeChain(&c, iP2_get32(e + 0x10));
                }
                break;
            }
        }
        U32 first;
        if (!writeData(&c, f.data, &first))
        {
            iP2_err(err, errsize, "%s", kFull);
            return false;
        }

        U8 e[IPS2_ENTRY_SIZE];
        iP2_entryMake(e, (f.mode & IPS2_MODE_ISFILE) ? f.mode : IPS2_MODE_FILE, (U32)f.data.size(),
                      created, first, 0, f.modified > 0 ? f.modified : now, f.attr, f.name.c_str());
        if (!writeEntry(&c, dirCluster, index, e))
        {
            iP2_err(err, errsize, "%s", kFull);
            return false;
        }
        if (index == dirLen)
        {
            dirLen++;
        }
    }

    // The folder's own entry: how many it holds now, and when it changed.
    if (!readEntry(&c, c.rootDir, dirIndex, d))
    {
        iP2_err(err, errsize, "%s", kDamaged);
        return false;
    }
    iP2_put32(d + 4, dirLen);
    iSavePS2TimePack(now, d + 0x18);
    if (!writeEntry(&c, c.rootDir, dirIndex, d))
    {
        iP2_err(err, errsize, "%s", kDamaged);
        return false;
    }

    card.swap(work);
    return true;
}

namespace
{
    // Marks the first `clustersWanted` clusters of a chain in `seen`. FALSE
    // for a chain that breaks off or loops before that, or runs through a
    // cluster another chain has.
    bool walkChain(const Card* c, U32 first, U32 clustersWanted, std::vector<U8>& seen)
    {
        U32 at = first;
        for (U32 i = 0; i < clustersWanted; i++)
        {
            if (at >= c->allocEnd || seen[at])
            {
                return false;
            }
            seen[at] = 1;
            if (i + 1 == clustersWanted)
            {
                break;
            }

            U32 next;
            bool last;
            if (!chainNext(c, at, &next, &last) || last)
            {
                return false;
            }
            at = next;
        }
        return true;
    }
} // namespace

bool iSavePS2CardCheck(const U8* card, size_t n, U32* freeClusters, char* err, size_t errsize)
{
    Card c;
    if (!cardOpen(&c, card, NULL, n, err, errsize))
    {
        return false;
    }

    if (c.ecc)
    {
        size_t pages = (size_t)c.clusters * 2;
        for (size_t page = 0; page < pages; page++)
        {
            const U8* at = card + page * c.raw;
            const U8* spare = at + IPS2_PAGE_SIZE;

            bool erased = true;
            for (size_t i = 0; i < c.raw && erased; i++)
            {
                erased = (at[i] == 0xFF);
            }
            if (erased)
            {
                continue;
            }

            for (int q = 0; q < 4; q++)
            {
                U8 ecc[3];
                iSavePS2Ecc(at + q * 128, ecc);
                // (Some tools leave the top bit of the line bytes set.)
                if (ecc[0] != (spare[q * 3] & 0x77) || ecc[1] != (spare[q * 3 + 1] & 0x7F) ||
                    ecc[2] != (spare[q * 3 + 2] & 0x7F))
                {
                    iP2_err(err, errsize, "page %lu's error-correcting code does not match it",
                            (unsigned long)page);
                    return false;
                }
            }
        }
    }

    U32 rootLen;
    if (!rootLength(&c, &rootLen))
    {
        iP2_err(err, errsize, "the card's root directory is damaged");
        return false;
    }

    std::vector<U8> seen(c.allocEnd, 0);
    if (!walkChain(&c, c.rootDir, (rootLen + 1) / 2, seen))
    {
        iP2_err(err, errsize, "the root directory's chain of clusters is damaged");
        return false;
    }

    for (U32 i = 2; i < rootLen; i++)
    {
        U8 d[IPS2_ENTRY_SIZE];
        if (!readEntry(&c, c.rootDir, i, d))
        {
            iP2_err(err, errsize, "the card's root directory is damaged");
            return false;
        }
        U32 mode = iP2_get32(d);
        if ((mode & IPS2_MODE_EXISTS) == 0)
        {
            continue;
        }

        std::string name = iP2_name(d + 0x40, 32);
        U32 length = iP2_get32(d + 4);
        U32 first = iP2_get32(d + 0x10);

        if ((mode & IPS2_MODE_ISFILE) != 0)
        {
            if (length > 0 && !walkChain(&c, first, (length + 1023) / 1024, seen))
            {
                iP2_err(err, errsize, "the file \"%s\" has a damaged chain", name.c_str());
                return false;
            }
            continue;
        }

        if (length < 2 || length > 4096 || !walkChain(&c, first, (length + 1) / 2, seen))
        {
            iP2_err(err, errsize, "the folder \"%s\" has a damaged chain", name.c_str());
            return false;
        }

        for (U32 k = 2; k < length; k++)
        {
            U8 e[IPS2_ENTRY_SIZE];
            if (!readEntry(&c, first, k, e))
            {
                iP2_err(err, errsize, "the folder \"%s\" is damaged", name.c_str());
                return false;
            }
            U32 fmode = iP2_get32(e);
            U32 flen = iP2_get32(e + 4);
            if ((fmode & IPS2_MODE_EXISTS) == 0 || (fmode & IPS2_MODE_ISFILE) == 0 || flen == 0)
            {
                continue;
            }
            if (!walkChain(&c, iP2_get32(e + 0x10), (flen + 1023) / 1024, seen))
            {
                iP2_err(err, errsize, "the file \"%s/%s\" has a damaged chain", name.c_str(),
                        iP2_name(e + 0x40, 32).c_str());
                return false;
            }
        }
    }

    U32 freeCount = 0;
    for (U32 rel = 0; rel < c.allocEnd; rel++)
    {
        U32 v;
        if (!fatGet(&c, rel, &v))
        {
            iP2_err(err, errsize, "the card's allocation table is damaged");
            return false;
        }
        if ((v & IP2_FAT_USED) == 0)
        {
            freeCount++;
        }
    }
    if (freeClusters != NULL)
    {
        *freeClusters = freeCount;
    }
    return true;
}
