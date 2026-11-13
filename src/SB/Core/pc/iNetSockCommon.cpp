// PC-only, co-op: the address helpers shared by the socket backends (iNetSock.h).
// Pure string and byte work, no OS.
#include "iNetSock.h"

#include <stdio.h>
#include <string.h>

namespace iNet {

static const uint8_t kV4Prefix[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

bool addr_equal(const NetAddr& a, const NetAddr& b)
{
    return a.port == b.port && memcmp(a.ip, b.ip, 16) == 0;
}

bool addr_is_v4(const NetAddr& a) { return memcmp(a.ip, kV4Prefix, 12) == 0; }

static bool parse_u16(const char* s, size_t n, uint16_t* out)
{
    if (n == 0 || n > 5) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    if (v == 0 || v > 65535) return false;
    *out = (uint16_t)v;
    return true;
}

static bool parse_v4(const char* s, size_t n, uint8_t out[4])
{
    int part = 0;
    uint32_t v = 0;
    size_t digits = 0;
    for (size_t i = 0; i <= n; i++)
    {
        if (i == n || s[i] == '.')
        {
            if (digits == 0 || digits > 3 || v > 255 || part > 3) return false;
            out[part++] = (uint8_t)v;
            v = 0;
            digits = 0;
        }
        else if (s[i] >= '0' && s[i] <= '9')
        {
            v = v * 10 + (uint32_t)(s[i] - '0');
            digits++;
        }
        else
            return false;
    }
    return part == 4;
}

// RFC 4291 text form, with "::" once. No embedded IPv4, no zone index.
static bool parse_v6(const char* s, size_t n, uint8_t out[16])
{
    uint16_t groups[8];
    int count = 0, gap = -1;
    size_t i = 0;
    if (n >= 2 && s[0] == ':' && s[1] == ':') { gap = 0; i = 2; }
    else if (n && s[0] == ':') return false;
    while (i < n)
    {
        uint32_t v = 0;
        size_t digits = 0;
        while (i < n && s[i] != ':')
        {
            char c = s[i];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0 || ++digits > 4) return false;
            v = v * 16 + (uint32_t)d;
            i++;
        }
        if (digits == 0 || count == 8) return false;
        groups[count++] = (uint16_t)v;
        if (i == n) break;
        i++; // ':'
        if (i < n && s[i] == ':')
        {
            if (gap >= 0) return false;
            gap = count;
            i++;
        }
        else if (i == n)
            return false; // trailing single ':'
    }
    if (gap < 0 && count != 8) return false;
    if (gap >= 0 && count > 7) return false;
    memset(out, 0, 16);
    int tail = gap >= 0 ? count - gap : 0;
    int head = gap >= 0 ? gap : count;
    for (int g = 0; g < head; g++) { out[2 * g] = (uint8_t)(groups[g] >> 8); out[2 * g + 1] = (uint8_t)groups[g]; }
    for (int g = 0; g < tail; g++)
    {
        int dst = 8 - tail + g;
        out[2 * dst] = (uint8_t)(groups[head + g] >> 8);
        out[2 * dst + 1] = (uint8_t)groups[head + g];
    }
    return true;
}

bool addr_parse(const char* text, uint16_t default_port, NetAddr* out)
{
    if (!text) return false;
    size_t n = strnlen(text, 64);
    if (n == 0 || n >= 64) return false;
    NetAddr a;
    memset(&a, 0, sizeof a);
    a.port = default_port;
    if (text[0] == '[')
    {
        const char* close = (const char*)memchr(text, ']', n);
        if (!close) return false;
        if (!parse_v6(text + 1, (size_t)(close - text - 1), a.ip)) return false;
        size_t rest = n - (size_t)(close - text) - 1;
        if (rest)
        {
            if (close[1] != ':' || !parse_u16(close + 2, rest - 1, &a.port)) return false;
        }
    }
    else
    {
        const char* colon = (const char*)memchr(text, ':', n);
        size_t hostlen = colon ? (size_t)(colon - text) : n;
        uint8_t v4[4];
        if (!parse_v4(text, hostlen, v4))
        {
            // A bare IPv6 address without brackets cannot carry a port.
            if (!parse_v6(text, n, a.ip)) return false;
        }
        else
        {
            memcpy(a.ip, kV4Prefix, 12);
            memcpy(a.ip + 12, v4, 4);
            if (colon && !parse_u16(colon + 1, n - hostlen - 1, &a.port)) return false;
        }
    }
    if (a.port == 0) return false;
    *out = a;
    return true;
}

void addr_format(const NetAddr& a, char* out, size_t cap)
{
    if (!cap) return;
    if (addr_is_v4(a))
        snprintf(out, cap, "%u.%u.%u.%u:%u", a.ip[12], a.ip[13], a.ip[14], a.ip[15], a.port);
    else
    {
        char tmp[64];
        int p = 0;
        for (int g = 0; g < 8; g++)
            p += snprintf(tmp + p, sizeof tmp - (size_t)p, g ? ":%x" : "%x", (a.ip[2 * g] << 8) | a.ip[2 * g + 1]);
        snprintf(out, cap, "[%s]:%u", tmp, a.port);
    }
}

} // namespace iNet
