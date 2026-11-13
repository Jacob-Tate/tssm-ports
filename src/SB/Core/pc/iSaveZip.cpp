// PC-only: stored-entry zips for the save store. See iSaveZip.h.
#include "iSaveZip.h"
#include "iHost.h"

#include <stdio.h>
#include <string.h>

// The zip CRC: CRC-32, reflected, polynomial 0xEDB88320. (Not the game's.)
static U32 iSZ_crc(const U8* p, size_t n)
{
    static U32 table[256];
    static bool ready = false;
    if (!ready)
    {
        for (U32 i = 0; i < 256; i++)
        {
            U32 c = i;
            for (S32 k = 0; k < 8; k++)
            {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        ready = true;
    }

    U32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
    {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static void iSZ_put16(std::vector<U8>& v, U32 x)
{
    v.push_back((U8)x);
    v.push_back((U8)(x >> 8));
}

static void iSZ_put32(std::vector<U8>& v, U32 x)
{
    iSZ_put16(v, x & 0xFFFF);
    iSZ_put16(v, x >> 16);
}

static U32 iSZ_get16(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8);
}

static U32 iSZ_get32(const U8* p)
{
    return iSZ_get16(p) | (iSZ_get16(p + 2) << 16);
}

// MS-DOS date and time, local, as zip stores them.
static void iSZ_dosTime(S64 mtime, U32* dosTime, U32* dosDate)
{
    iHostCalendar t;
    if (mtime > 0)
    {
        iHostLocalTimeOf(mtime, &t);
    }
    else
    {
        iHostLocalTime(&t);
    }

    S32 year = t.year + 1900;
    if (year < 1980)
    {
        year = 1980;
    }
    *dosTime = ((U32)t.hour << 11) | ((U32)t.min << 5) | ((U32)t.sec / 2);
    *dosDate = ((U32)(year - 1980) << 9) | ((U32)(t.mon + 1) << 5) | (U32)t.mday;
}

void iSaveZipBuild(const std::vector<iSaveZipEntry>& entries, std::vector<U8>& out)
{
    out.clear();
    std::vector<U8> central;

    for (size_t i = 0; i < entries.size(); i++)
    {
        const iSaveZipEntry& e = entries[i];
        U32 crc = iSZ_crc(e.data.empty() ? NULL : &e.data[0], e.data.size());
        U32 dt;
        U32 dd;
        iSZ_dosTime(e.mtime, &dt, &dd);
        U32 offset = (U32)out.size();

        iSZ_put32(out, 0x04034B50);
        iSZ_put16(out, 10); // version needed: 1.0
        iSZ_put16(out, 0); // flags
        iSZ_put16(out, 0); // stored
        iSZ_put16(out, dt);
        iSZ_put16(out, dd);
        iSZ_put32(out, crc);
        iSZ_put32(out, (U32)e.data.size());
        iSZ_put32(out, (U32)e.data.size());
        iSZ_put16(out, (U32)e.name.size());
        iSZ_put16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.data.begin(), e.data.end());

        iSZ_put32(central, 0x02014B50);
        iSZ_put16(central, 20); // made by: MS-DOS, 2.0
        iSZ_put16(central, 10);
        iSZ_put16(central, 0);
        iSZ_put16(central, 0);
        iSZ_put16(central, dt);
        iSZ_put16(central, dd);
        iSZ_put32(central, crc);
        iSZ_put32(central, (U32)e.data.size());
        iSZ_put32(central, (U32)e.data.size());
        iSZ_put16(central, (U32)e.name.size());
        iSZ_put16(central, 0); // extra
        iSZ_put16(central, 0); // comment
        iSZ_put16(central, 0); // disk
        iSZ_put16(central, 0); // internal attributes
        iSZ_put32(central, 0); // external attributes
        iSZ_put32(central, offset);
        central.insert(central.end(), e.name.begin(), e.name.end());
    }

    U32 cdOffset = (U32)out.size();
    out.insert(out.end(), central.begin(), central.end());

    iSZ_put32(out, 0x06054B50);
    iSZ_put16(out, 0);
    iSZ_put16(out, 0);
    iSZ_put16(out, (U32)entries.size());
    iSZ_put16(out, (U32)entries.size());
    iSZ_put32(out, (U32)central.size());
    iSZ_put32(out, cdOffset);
    iSZ_put16(out, 0);
}

bool iSaveZipIs(const U8* p, size_t n)
{
    return n >= 4 && iSZ_get32(p) == 0x04034B50;
}

bool iSaveZipParse(const U8* p, size_t n, std::vector<iSaveZipEntry>& out, char* err,
                   size_t errsize)
{
    out.clear();

    // The end record: the last 22 bytes, or earlier by the length of a comment.
    size_t eocd = (size_t)-1;
    if (n >= 22)
    {
        size_t lowest = (n - 22 > 0xFFFF) ? n - 22 - 0xFFFF : 0;
        for (size_t i = n - 22 + 1; i-- > lowest;)
        {
            if (iSZ_get32(p + i) == 0x06054B50)
            {
                eocd = i;
                break;
            }
        }
    }

    if (eocd == (size_t)-1)
    {
        snprintf(err, errsize, "not a zip, or a damaged one (no end record)");
        return false;
    }

    U32 count = iSZ_get16(p + eocd + 10);
    U32 cdSize = iSZ_get32(p + eocd + 12);
    U32 cdOff = iSZ_get32(p + eocd + 16);
    if ((U64)cdOff + cdSize > eocd)
    {
        snprintf(err, errsize, "a damaged zip (its directory is out of range)");
        return false;
    }

    size_t pos = cdOff;
    for (U32 i = 0; i < count; i++)
    {
        if (pos + 46 > eocd || iSZ_get32(p + pos) != 0x02014B50)
        {
            snprintf(err, errsize, "a damaged zip (directory entry %u)", (unsigned)i);
            return false;
        }

        U32 method = iSZ_get16(p + pos + 10);
        U32 crc = iSZ_get32(p + pos + 16);
        U32 csize = iSZ_get32(p + pos + 20);
        U32 usize = iSZ_get32(p + pos + 24);
        U32 nlen = iSZ_get16(p + pos + 28);
        U32 xlen = iSZ_get16(p + pos + 30);
        U32 clen = iSZ_get16(p + pos + 32);
        U32 local = iSZ_get32(p + pos + 42);

        if (pos + 46 + nlen > eocd)
        {
            snprintf(err, errsize, "a damaged zip (directory entry %u)", (unsigned)i);
            return false;
        }

        iSaveZipEntry e;
        e.name.assign((const char*)p + pos + 46, nlen);
        e.mtime = 0;
        pos += 46 + nlen + xlen + clen;

        // A folder.
        if (!e.name.empty() && e.name[e.name.size() - 1] == '/')
        {
            continue;
        }

        if (method != 0 || csize != usize)
        {
            snprintf(err, errsize,
                     "%s in this zip is compressed; extract the zip and import the .gci files "
                     "in it instead",
                     e.name.c_str());
            return false;
        }

        if ((U64)local + 30 > n || iSZ_get32(p + local) != 0x04034B50)
        {
            snprintf(err, errsize, "a damaged zip (%s)", e.name.c_str());
            return false;
        }

        U64 data = (U64)local + 30 + iSZ_get16(p + local + 26) + iSZ_get16(p + local + 28);
        if (data + csize > n)
        {
            snprintf(err, errsize, "a damaged zip (%s is cut short)", e.name.c_str());
            return false;
        }

        e.data.assign(p + data, p + data + csize);
        if (iSZ_crc(e.data.empty() ? NULL : &e.data[0], e.data.size()) != crc)
        {
            snprintf(err, errsize, "a damaged zip (%s fails its CRC)", e.name.c_str());
            return false;
        }

        out.push_back(e);
    }

    return true;
}
