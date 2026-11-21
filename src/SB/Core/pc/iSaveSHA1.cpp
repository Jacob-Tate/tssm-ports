// PC-only: SHA-1 and HMAC-SHA1. See iSaveSHA1.h.
#include "iSaveSHA1.h"

#include <string.h>

static U32 iSHA1_rol(U32 v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void iSHA1_block(iSHA1* s, const U8* p)
{
    U32 w[80];
    for (int i = 0; i < 16; i++)
    {
        w[i] = ((U32)p[i * 4] << 24) | ((U32)p[i * 4 + 1] << 16) | ((U32)p[i * 4 + 2] << 8) |
               (U32)p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++)
    {
        w[i] = iSHA1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    U32 a = s->h[0];
    U32 b = s->h[1];
    U32 c = s->h[2];
    U32 d = s->h[3];
    U32 e = s->h[4];

    for (int i = 0; i < 80; i++)
    {
        U32 f;
        U32 k;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        U32 t = iSHA1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = iSHA1_rol(b, 30);
        b = a;
        a = t;
    }

    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

void iSHA1Init(iSHA1* s)
{
    s->h[0] = 0x67452301u;
    s->h[1] = 0xEFCDAB89u;
    s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xC3D2E1F0u;
    s->length = 0;
    s->fill = 0;
}

void iSHA1Update(iSHA1* s, const void* data, size_t n)
{
    const U8* p = (const U8*)data;
    s->length += n;

    while (n > 0)
    {
        size_t take = 64 - s->fill;
        if (take > n)
        {
            take = n;
        }
        memcpy(s->block + s->fill, p, take);
        s->fill += (U32)take;
        p += take;
        n -= take;

        if (s->fill == 64)
        {
            iSHA1_block(s, s->block);
            s->fill = 0;
        }
    }
}

void iSHA1Final(iSHA1* s, U8 out[ISHA1_SIZE])
{
    U64 bits = s->length * 8;

    U8 pad[72];
    size_t n = (s->fill < 56) ? 56 - s->fill : 120 - s->fill;
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    iSHA1Update(s, pad, n);

    U8 len[8];
    for (int i = 0; i < 8; i++)
    {
        len[i] = (U8)(bits >> (56 - 8 * i));
    }
    iSHA1Update(s, len, 8);

    for (int i = 0; i < 5; i++)
    {
        out[i * 4] = (U8)(s->h[i] >> 24);
        out[i * 4 + 1] = (U8)(s->h[i] >> 16);
        out[i * 4 + 2] = (U8)(s->h[i] >> 8);
        out[i * 4 + 3] = (U8)s->h[i];
    }

    memset(s, 0, sizeof(*s));
}

void iSHA1Of(const void* data, size_t n, U8 out[ISHA1_SIZE])
{
    iSHA1 s;
    iSHA1Init(&s);
    iSHA1Update(&s, data, n);
    iSHA1Final(&s, out);
}

void iHMACSHA1(const void* key, size_t keysize, const void* data1, size_t n1, const void* data2,
               size_t n2, U8 out[ISHA1_SIZE])
{
    U8 k[64];
    memset(k, 0, sizeof(k));
    if (keysize > 64)
    {
        iSHA1Of(key, keysize, k);
    }
    else if (keysize > 0)
    {
        memcpy(k, key, keysize);
    }

    U8 pad[64];
    U8 inner[ISHA1_SIZE];
    iSHA1 s;

    for (int i = 0; i < 64; i++)
    {
        pad[i] = (U8)(k[i] ^ 0x36);
    }
    iSHA1Init(&s);
    iSHA1Update(&s, pad, 64);
    if (n1 > 0)
    {
        iSHA1Update(&s, data1, n1);
    }
    if (n2 > 0)
    {
        iSHA1Update(&s, data2, n2);
    }
    iSHA1Final(&s, inner);

    for (int i = 0; i < 64; i++)
    {
        pad[i] = (U8)(k[i] ^ 0x5C);
    }
    iSHA1Init(&s);
    iSHA1Update(&s, pad, 64);
    iSHA1Update(&s, inner, ISHA1_SIZE);
    iSHA1Final(&s, out);

    // Nothing of the key is left on the stack.
    memset(k, 0, sizeof(k));
    memset(pad, 0, sizeof(pad));
    memset(inner, 0, sizeof(inner));
}
