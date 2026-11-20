// PC-only: a package's platform, from its PLAT chunk. See iGCPackage.h.
#include "iGCPackage.h"

#include <string.h>

static void iGCPutString(char* out, U32 outsize, const U8* s, U32 n)
{
    if (out == NULL || outsize == 0)
    {
        return;
    }
    U32 len = 0;
    while (len < n && s[len] != '\0' && len + 1 < outsize)
    {
        len++;
    }
    memcpy(out, s, len);
    out[len] = '\0';
}

iGCPlatform iGCPackagePlatform(const void* headv, U32 got, char* id, U32 idsize, char* name,
                               U32 namesize)
{
    const U8* head = (const U8*)headv;
    iGCPutString(id, idsize, (const U8*)"", 0);
    iGCPutString(name, namesize, (const U8*)"", 0);

    if (head == NULL)
    {
        return IGCPLAT_NONE;
    }
    if (got < 16 || memcmp(head, "HIPA", 4) != 0)
    {
        return IGCPLAT_NOTPACKAGE;
    }

    // HIPA's own payload is the rest of the file, so step into it and walk the
    // top-level chunks; PACK's children are walked the same way.
    U32 o = 8;
    U32 end = got;

    for (int depth = 0; depth < 2; depth++)
    {
        bool descended = false;

        while (o <= end && end - o >= 8)
        {
            const U8* c = head + o;
            U32 size = ((U32)c[4] << 24) | ((U32)c[5] << 16) | ((U32)c[6] << 8) | c[7];
            U32 body = o + 8;
            U32 stop = (size < end - body) ? body + size : end;

            if (memcmp(c, "PACK", 4) == 0)
            {
                o = body;
                end = stop;
                descended = true;
                break;
            }

            if (memcmp(c, "PLAT", 4) == 0)
            {
                U32 n = 0;
                while (body + n < stop && head[body + n] != '\0')
                {
                    n++;
                }
                if (n == 0)
                {
                    return IGCPLAT_NONE;
                }
                iGCPutString(id, idsize, head + body, n);

                // The string after it.
                U32 s = body + n;
                while (s < stop && head[s] == '\0')
                {
                    s++;
                }
                iGCPutString(name, namesize, head + s, stop - s);

                if (n == 2 && (memcmp(head + body, "BX", 2) == 0 || memcmp(head + body, "XB", 2) == 0))
                {
                    return IGCPLAT_XBOX;
                }
                if (n == 2 && memcmp(head + body, "GC", 2) == 0)
                {
                    return IGCPLAT_GAMECUBE;
                }
                return IGCPLAT_OTHER;
            }

            if (size == 0 || size >= end - body)
            {
                break;
            }
            o = body + size;
        }

        if (!descended)
        {
            break;
        }
    }

    return IGCPLAT_NONE;
}

// RenderWare chunk headers are little-endian on both consoles: type, size, version.
static U32 iGCLe32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

enum
{
    kRwStruct = 0x01,
    kRwTextureNative = 0x15,
    kRwTexDictionary = 0x16,
    kGCNameOffset = 0x18,
    kGCNameSize = 32
};

U32 iGCPackageRestoreTextureNames(void* datav, U32 size, const char* assetName)
{
    U8* data = (U8*)datav;
    if (data == NULL || assetName == NULL || assetName[0] == '\0' || size < 12)
    {
        return 0;
    }

    // The name to write: the asset's, less ".RW3" or the part of it a
    // 31-character ADBG name kept.
    char name[kGCNameSize];
    U32 len = 0;
    while (assetName[len] != '\0' && len < kGCNameSize - 1)
    {
        name[len] = assetName[len];
        len++;
    }
    name[len] = '\0';
    static const char kSuffix[] = ".RW3";
    for (U32 n = 4; n >= 1; n--)
    {
        if (len >= n)
        {
            bool match = true;
            for (U32 i = 0; i < n; i++)
            {
                char c = name[len - n + i];
                if (c >= 'a' && c <= 'z')
                {
                    c = (char)(c - 'a' + 'A');
                }
                if (c != kSuffix[i])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                len -= n;
                name[len] = '\0';
                break;
            }
        }
    }
    if (len == 0)
    {
        return 0;
    }

    U32 type = iGCLe32(data);
    U32 dictSize = iGCLe32(data + 4);
    if (type != kRwTexDictionary || dictSize > size - 12)
    {
        return 0;
    }

    U32 written = 0;
    U32 o = 12;
    U32 end = 12 + dictSize;
    while (o <= end && end - o >= 12)
    {
        U32 ctype = iGCLe32(data + o);
        U32 csize = iGCLe32(data + o + 4);
        U32 body = o + 12;
        if (csize > end - body)
        {
            break;
        }
        if (ctype == kRwTextureNative && csize >= 12)
        {
            // Its first child is the native struct.
            U32 stype = iGCLe32(data + body);
            U32 ssize = iGCLe32(data + body + 4);
            U32 sbody = body + 12;
            if (stype == kRwStruct && ssize <= csize - 12 && ssize >= kGCNameOffset + kGCNameSize &&
                data[sbody] == 0 && data[sbody + 1] == 0 && data[sbody + 2] == 0 && data[sbody + 3] == 6)
            {
                char* native = (char*)data + sbody + kGCNameOffset;
                if (native[0] == '\0' || memcmp(native, "temp", 5) == 0)
                {
                    memset(native, 0, kGCNameSize);
                    memcpy(native, name, len);
                    written++;
                }
            }
        }
        o = body + csize;
    }
    return written;
}

enum
{
    kRwClump = 0x10,
    kRwAtomic = 0x14
};

bool iGCPackageModelFits(const void* datav, U32 size)
{
    const U8* data = (const U8*)datav;
    if (data == NULL)
    {
        return false;
    }

    // As RwStreamFindChunk walks: skip whole chunks until the clump.
    U32 o = 0;
    while (size - o >= 12)
    {
        U32 type = iGCLe32(data + o);
        U32 csize = iGCLe32(data + o + 4);
        U32 body = o + 12;
        if (csize > size - body)
        {
            return false;
        }
        if (type != kRwClump)
        {
            o = body + csize;
            continue;
        }

        U32 end = body + csize;
        U32 atomics = 0;
        S32 wanted = -1;
        U32 c = body;
        while (end - c >= 12)
        {
            U32 ctype = iGCLe32(data + c);
            U32 cn = iGCLe32(data + c + 4);
            if (cn > end - c - 12)
            {
                return false;
            }
            if (c == body)
            {
                if (ctype != kRwStruct || cn < 4)
                {
                    return false;
                }
                wanted = (S32)iGCLe32(data + c + 12);
            }
            else if (ctype == kRwAtomic)
            {
                atomics++;
            }
            c += 12 + cn;
        }
        return c == end && wanted >= 1 && (U32)wanted == atomics;
    }
    return false;
}
