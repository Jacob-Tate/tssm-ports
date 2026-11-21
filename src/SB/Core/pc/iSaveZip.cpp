// PC-only: stored-entry zips for the save store. See iSaveZip.h.
#include "iSaveZip.h"
#include "iHost.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

// An entry's DOS time and date (local time, two-second steps) as Unix seconds;
// 0 when it carries none.
static S64 iSZ_unixTime(U32 dosTime, U32 dosDate)
{
    if (dosDate == 0)
    {
        return 0;
    }

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = (int)((dosDate >> 9) & 0x7F) + 80;
    t.tm_mon = (int)((dosDate >> 5) & 0xF) - 1;
    t.tm_mday = (int)(dosDate & 0x1F);
    t.tm_hour = (int)((dosTime >> 11) & 0x1F);
    t.tm_min = (int)((dosTime >> 5) & 0x3F);
    t.tm_sec = (int)(dosTime & 0x1F) * 2;
    t.tm_isdst = -1;

    time_t when = mktime(&t);
    return when == (time_t)-1 ? 0 : (S64)when;
}

// The most one entry, and a whole zip, may unpack to. A memory card image is
// 8 MB, a save 60 KB.
static const U32 ISZ_ENTRY_LIMIT = 64u * 1024 * 1024;
static const U64 ISZ_TOTAL_LIMIT = 256ull * 1024 * 1024;

// --- inflate (RFC 1951) ---------------------------------------------------------------
//
// What zip tools pack entries with ("deflate", method 8). Reading only: the
// port's own zips stay stored.

namespace
{
    struct Bits
    {
        const U8* p;
        size_t n;
        size_t pos;
        U32 hold;
        S32 count;
        bool bad; // ran off the end
    };

    struct Huffman
    {
        U16 count[16]; // codes of each length
        U16 symbol[288]; // symbols in code order
    };
}

static U32 iSZ_bits(Bits& s, S32 need)
{
    U32 v = s.hold;
    while (s.count < need)
    {
        if (s.pos >= s.n)
        {
            s.bad = true;
            return 0;
        }
        v |= (U32)s.p[s.pos++] << s.count;
        s.count += 8;
    }
    s.hold = v >> need;
    s.count -= need;
    return v & ((1u << need) - 1);
}

// The table for `n` code lengths. FALSE for lengths no code can have (more
// codes of a length than fit).
static bool iSZ_huffman(Huffman& h, const U8* length, S32 n)
{
    memset(h.count, 0, sizeof(h.count));
    memset(h.symbol, 0, sizeof(h.symbol));
    for (S32 i = 0; i < n; i++)
    {
        h.count[length[i]]++;
    }

    S32 left = 1;
    for (S32 len = 1; len < 16; len++)
    {
        left <<= 1;
        left -= h.count[len];
        if (left < 0)
        {
            return false;
        }
    }

    U16 offset[16];
    offset[1] = 0;
    for (S32 len = 1; len < 15; len++)
    {
        offset[len + 1] = (U16)(offset[len] + h.count[len]);
    }
    for (S32 i = 0; i < n; i++)
    {
        if (length[i] != 0)
        {
            h.symbol[offset[length[i]]++] = (U16)i;
        }
    }
    return true;
}

// The next symbol, or -1 for a code that is not in the table.
static S32 iSZ_symbol(Bits& s, const Huffman& h)
{
    S32 code = 0;
    S32 first = 0;
    S32 index = 0;
    for (S32 len = 1; len < 16; len++)
    {
        code |= (S32)iSZ_bits(s, 1);
        if (s.bad)
        {
            return -1;
        }
        S32 count = h.count[len];
        if (code - count < first)
        {
            return h.symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static bool iSZ_codes(Bits& s, std::vector<U8>& out, size_t limit, const Huffman& lit,
                      const Huffman& dist)
{
    static const U16 lengthBase[29] = { 3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                        15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                        67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const U8 lengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const U16 distBase[30] = { 1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const U8 distExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    for (;;)
    {
        S32 sym = iSZ_symbol(s, lit);
        if (sym < 0)
        {
            return false;
        }
        if (sym < 256)
        {
            if (out.size() >= limit)
            {
                return false;
            }
            out.push_back((U8)sym);
            continue;
        }
        if (sym == 256)
        {
            return true;
        }

        sym -= 257;
        if (sym >= 29)
        {
            return false;
        }
        size_t len = lengthBase[sym] + iSZ_bits(s, lengthExtra[sym]);

        S32 d = iSZ_symbol(s, dist);
        if (d < 0 || d >= 30)
        {
            return false;
        }
        size_t back = distBase[d] + iSZ_bits(s, distExtra[d]);
        if (s.bad || back > out.size() || len > limit - out.size())
        {
            return false;
        }

        // Byte by byte: a copy may run into what it is writing.
        size_t from = out.size() - back;
        for (size_t i = 0; i < len; i++)
        {
            out.push_back(out[from + i]);
        }
    }
}

// `n` deflated bytes into `out`, which must come to exactly `size` bytes.
static bool iSZ_inflate(const U8* p, size_t n, size_t size, std::vector<U8>& out)
{
    Bits s;
    s.p = p;
    s.n = n;
    s.pos = 0;
    s.hold = 0;
    s.count = 0;
    s.bad = false;

    out.clear();
    out.reserve(size);

    for (;;)
    {
        U32 last = iSZ_bits(s, 1);
        U32 type = iSZ_bits(s, 2);
        if (s.bad)
        {
            return false;
        }

        if (type == 0)
        {
            // Stored: from the next whole byte, a length and its complement.
            s.hold = 0;
            s.count = 0;
            if (s.pos + 4 > s.n)
            {
                return false;
            }
            U32 len = iSZ_get16(s.p + s.pos);
            U32 inverse = iSZ_get16(s.p + s.pos + 2);
            s.pos += 4;
            if ((len ^ 0xFFFF) != inverse || s.pos + len > s.n || len > size - out.size())
            {
                return false;
            }
            out.insert(out.end(), s.p + s.pos, s.p + s.pos + len);
            s.pos += len;
        }
        else if (type == 1)
        {
            // The fixed codes.
            U8 length[288 + 30];
            S32 i = 0;
            for (; i < 144; i++)
            {
                length[i] = 8;
            }
            for (; i < 256; i++)
            {
                length[i] = 9;
            }
            for (; i < 280; i++)
            {
                length[i] = 7;
            }
            for (; i < 288; i++)
            {
                length[i] = 8;
            }
            for (; i < 288 + 30; i++)
            {
                length[i] = 5;
            }

            Huffman lit;
            Huffman dist;
            iSZ_huffman(lit, length, 288);
            iSZ_huffman(dist, length + 288, 30);
            if (!iSZ_codes(s, out, size, lit, dist))
            {
                return false;
            }
        }
        else if (type == 2)
        {
            // Codes sent with the block.
            static const U8 order[19] = { 16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                          11, 4,  12, 3, 13, 2, 14, 1, 15 };
            S32 nlen = (S32)iSZ_bits(s, 5) + 257;
            S32 ndist = (S32)iSZ_bits(s, 5) + 1;
            S32 ncode = (S32)iSZ_bits(s, 4) + 4;
            if (s.bad || nlen > 286 || ndist > 30)
            {
                return false;
            }

            U8 length[19];
            memset(length, 0, sizeof(length));
            for (S32 i = 0; i < ncode; i++)
            {
                length[order[i]] = (U8)iSZ_bits(s, 3);
            }

            Huffman code;
            if (s.bad || !iSZ_huffman(code, length, 19))
            {
                return false;
            }

            U8 all[286 + 30];
            S32 have = 0;
            while (have < nlen + ndist)
            {
                S32 sym = iSZ_symbol(s, code);
                if (sym < 0)
                {
                    return false;
                }
                if (sym < 16)
                {
                    all[have++] = (U8)sym;
                    continue;
                }

                U8 value = 0;
                S32 repeat;
                if (sym == 16)
                {
                    if (have == 0)
                    {
                        return false;
                    }
                    value = all[have - 1];
                    repeat = 3 + (S32)iSZ_bits(s, 2);
                }
                else if (sym == 17)
                {
                    repeat = 3 + (S32)iSZ_bits(s, 3);
                }
                else
                {
                    repeat = 11 + (S32)iSZ_bits(s, 7);
                }
                if (s.bad || have + repeat > nlen + ndist)
                {
                    return false;
                }
                while (repeat-- > 0)
                {
                    all[have++] = value;
                }
            }

            // The end-of-block code has to exist.
            Huffman lit;
            Huffman dist;
            if (all[256] == 0 || !iSZ_huffman(lit, all, nlen) ||
                !iSZ_huffman(dist, all + nlen, ndist))
            {
                return false;
            }
            if (!iSZ_codes(s, out, size, lit, dist))
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        if (last != 0)
        {
            break;
        }
    }

    return out.size() == size;
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
    U64 total = 0;
    for (U32 i = 0; i < count; i++)
    {
        if (pos + 46 > eocd || iSZ_get32(p + pos) != 0x02014B50)
        {
            snprintf(err, errsize, "a damaged zip (directory entry %u)", (unsigned)i);
            return false;
        }

        U32 flags = iSZ_get16(p + pos + 8);
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
        e.mtime = iSZ_unixTime(iSZ_get16(p + pos + 12), iSZ_get16(p + pos + 14));
        pos += 46 + nlen + xlen + clen;

        // A folder.
        if (!e.name.empty() && e.name[e.name.size() - 1] == '/')
        {
            continue;
        }

        // Stored, or deflated as zip tools pack them. Anything else (a zip
        // with a password, an unusual packer) is named, not guessed at.
        if ((method != 0 && method != 8) || (method == 0 && csize != usize) || (flags & 1) != 0)
        {
            snprintf(err, errsize,
                     "%s in this zip is packed in a way the port does not read; extract the zip "
                     "and import the folder or files in it instead",
                     e.name.c_str());
            return false;
        }

        // Nothing a save travels in is this large; whatever it is, it is
        // not read (a zip of a whole emulator folder may hold anything).
        if (usize > ISZ_ENTRY_LIMIT || total + usize > ISZ_TOTAL_LIMIT)
        {
            continue;
        }
        total += usize;

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

        if (method == 0)
        {
            e.data.assign(p + data, p + data + csize);
        }
        else if (!iSZ_inflate(p + data, csize, usize, e.data))
        {
            snprintf(err, errsize, "a damaged zip (%s does not unpack)", e.name.c_str());
            return false;
        }
        if (iSZ_crc(e.data.empty() ? NULL : &e.data[0], e.data.size()) != crc)
        {
            snprintf(err, errsize, "a damaged zip (%s fails its CRC)", e.name.c_str());
            return false;
        }

        out.push_back(e);
    }

    return true;
}
