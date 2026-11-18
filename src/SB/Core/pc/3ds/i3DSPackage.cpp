// The 3DS package formats the updater reads (i3DSPackage.h). The layouts are
// 3dbrew's "CIA", "Title metadata", "Ticket" and "3DSX Format", and what
// makerom (Project_CTR) and 3dsxtool write.

#include "3ds/i3DSPackage.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
    void Say(char* out, size_t outSize, const char* fmt, ...)
    {
        if (out == NULL || outSize == 0)
        {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(out, outSize, fmt, ap);
        va_end(ap);
    }

    uint32_t Le32(const uint8_t* p)
    {
        return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }

    uint64_t Le64(const uint8_t* p)
    {
        return (uint64_t)Le32(p) | (uint64_t)Le32(p + 4) << 32;
    }

    uint16_t Be16(const uint8_t* p)
    {
        return (uint16_t)(p[0] << 8 | p[1]);
    }

    uint32_t Be32(const uint8_t* p)
    {
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
    }

    uint64_t Be64(const uint8_t* p)
    {
        return (uint64_t)Be32(p) << 32 | Be32(p + 4);
    }

    // A CIA's sections each start on a 64-byte boundary.
    uint64_t Align64(uint64_t n)
    {
        return (n + 63) & ~(uint64_t)63;
    }

    // A signed block's signature, with its type word and the padding after
    // it: where the block's own data begins. 0 for a type that is not one.
    uint32_t SignatureBytes(uint32_t type)
    {
        switch (type)
        {
        case 0x10000: // RSA-4096, SHA-1
        case 0x10003: // RSA-4096, SHA-256
            return 4 + 0x200 + 0x3C;
        case 0x10001: // RSA-2048, SHA-1
        case 0x10004: // RSA-2048, SHA-256
            return 4 + 0x100 + 0x3C;
        case 0x10002: // ECDSA, SHA-1
        case 0x10005: // ECDSA, SHA-256
            return 4 + 0x3C + 0x40;
        default:
            return 0;
        }
    }
} // namespace

int i3DSParseCia(const uint8_t* head, size_t len, uint64_t fileSize, i3DSCiaInfo* out, char* why,
                 size_t whySize)
{
    memset(out, 0, sizeof(*out));
    if (head == NULL || len < 0x20)
    {
        Say(why, whySize, "too short for a CIA header");
        return 0;
    }
    const uint32_t headerSize = Le32(head + 0x00);
    const uint16_t type = (uint16_t)(head[4] | head[5] << 8);
    const uint16_t format = (uint16_t)(head[6] | head[7] << 8);
    const uint32_t certSize = Le32(head + 0x08);
    const uint32_t ticketSize = Le32(head + 0x0C);
    const uint32_t tmdSize = Le32(head + 0x10);
    const uint32_t metaSize = Le32(head + 0x14);
    const uint64_t contentSize = Le64(head + 0x18);
    // Every CIA so far has a 0x2020-byte header, type and version 0.
    if (headerSize != 0x2020 || type != 0 || format != 0)
    {
        Say(why, whySize, "not a CIA header (size %#x, type %u, version %u)", headerSize, type,
            format);
        return 0;
    }
    if (certSize == 0 || certSize > 0x10000 || ticketSize == 0 || ticketSize > 0x10000 ||
        tmdSize < 0x200 || tmdSize > 0x100000 || metaSize > 0x100000)
    {
        Say(why, whySize, "implausible section sizes (certificates %u, ticket %u, TMD %u, meta %u)",
            certSize, ticketSize, tmdSize, metaSize);
        return 0;
    }
    const uint64_t tmdAt = Align64(headerSize) + Align64(certSize) + Align64(ticketSize);
    const uint64_t contentAt = tmdAt + Align64(tmdSize);
    // The meta section, when there is one, is the file's last; nothing may
    // follow it but its own alignment.
    const uint64_t end = contentAt + Align64(contentSize) + (metaSize != 0 ? Align64(metaSize) : 0);
    const uint64_t endTight = contentAt + contentSize + metaSize;
    if (contentSize == 0 ||
        (fileSize != end && fileSize != endTight &&
         !(metaSize != 0 && fileSize == contentAt + Align64(contentSize) + metaSize)))
    {
        Say(why, whySize, "the sections add up to %llu bytes, the file has %llu",
            (unsigned long long)end, (unsigned long long)fileSize);
        return 0;
    }

    if (tmdAt + 4 > len)
    {
        Say(why, whySize, "the TMD starts past what was read");
        return 0;
    }
    const uint32_t sigType = Be32(head + tmdAt);
    const uint32_t sigBytes = SignatureBytes(sigType);
    if (sigBytes == 0)
    {
        Say(why, whySize, "the TMD's signature type %#x is not one", sigType);
        return 0;
    }
    const uint64_t tmdHeader = tmdAt + sigBytes;
    if (tmdHeader + 0xC4 > len || sigBytes + 0xC4 > tmdSize)
    {
        Say(why, whySize, "the TMD's header lies past what was read");
        return 0;
    }
    out->titleId = Be64(head + tmdHeader + 0x4C);
    out->version = Be16(head + tmdHeader + 0x9C);
    out->contentCount = Be16(head + tmdHeader + 0x9E);
    out->contentSize = contentSize;
    if (out->contentCount == 0 ||
        sigBytes + 0xC4 + 0x24 * 64 + 0x30 * (uint64_t)out->contentCount > tmdSize)
    {
        Say(why, whySize, "the TMD lists %u contents in %u bytes", out->contentCount, tmdSize);
        return 0;
    }
    return 1;
}

int i3DSParse3dsx(const uint8_t* head, size_t len, uint64_t fileSize, i3DS3dsxInfo* out, char* why,
                  size_t whySize)
{
    memset(out, 0, sizeof(*out));
    if (head == NULL || len < 44 || memcmp(head, "3DSX", 4) != 0)
    {
        Say(why, whySize, "not a .3dsx (no \"3DSX\" at its start)");
        return 0;
    }
    const uint16_t headerSize = (uint16_t)(head[4] | head[5] << 8);
    // 32 bytes, plus 12 for the SMDH and the RomFS when there are those.
    if (headerSize < 44)
    {
        Say(why, whySize, "a .3dsx without an SMDH or a RomFS (header %u bytes)", headerSize);
        return 0;
    }
    out->smdhOffset = Le32(head + 32);
    out->smdhSize = Le32(head + 36);
    out->romfsOffset = Le32(head + 40);
    // An SMDH is 0x36C0 bytes; it and the RomFS lie after the header, in the
    // file, the RomFS after the SMDH (3dsxtool's order).
    if (out->smdhSize != 0x36C0 || out->smdhOffset < headerSize ||
        (uint64_t)out->smdhOffset + out->smdhSize > fileSize ||
        out->romfsOffset < out->smdhOffset + out->smdhSize || out->romfsOffset >= fileSize)
    {
        Say(why, whySize,
            "a .3dsx whose SMDH (%u bytes at %u) or RomFS (at %u) does not fit its "
            "%llu bytes",
            out->smdhSize, out->smdhOffset, out->romfsOffset, (unsigned long long)fileSize);
        return 0;
    }
    return 1;
}

int i3DSTitleVersion(const char* text, uint16_t* out)
{
    unsigned parts[3] = { 0, 0, 0 };
    int count = 0;
    const char* p = text;
    if (p == NULL || *p == '\0')
    {
        return 0;
    }
    while (*p != '\0')
    {
        if (count == 3 || *p < '0' || *p > '9')
        {
            return 0;
        }
        unsigned v = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9')
        {
            v = v * 10 + (unsigned)(*p - '0');
            p++;
            if (++digits > 3)
            {
                return 0;
            }
        }
        parts[count++] = v;
        if (*p == '.')
        {
            p++;
            if (*p == '\0')
            {
                return 0;
            }
        }
        else if (*p != '\0')
        {
            return 0;
        }
    }
    if (parts[0] > 63 || parts[1] > 63 || parts[2] > 15)
    {
        return 0;
    }
    *out = (uint16_t)(parts[0] << 10 | parts[1] << 4 | parts[2]);
    return 1;
}

void i3DSTitleVersionText(uint16_t version, char* out, size_t outSize)
{
    snprintf(out, outSize, "%u.%u.%u", (unsigned)(version >> 10), (unsigned)((version >> 4) & 63),
             (unsigned)(version & 15));
}

int i3DSCiaVersionIs(uint16_t version, const char* text)
{
    uint16_t named = 0;
    return i3DSTitleVersion(text, &named) && named == version ? 1 : 0;
}
