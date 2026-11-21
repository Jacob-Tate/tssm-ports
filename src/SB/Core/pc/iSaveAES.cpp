// PC-only: AES-128, as FIPS 197 defines it. See iSaveAES.h.
//
// The state is the 16 bytes in order, four to a column. The S-box and its
// inverse are made from their definition (the inverse in GF(2^8), then the
// affine map), so no table of the standard's is copied in here.
#include "iSaveAES.h"

#include <string.h>

namespace
{
    // Multiplication in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1.
    U8 gmul(U8 a, U8 b)
    {
        U8 p = 0;
        while (b != 0)
        {
            if (b & 1)
            {
                p ^= a;
            }
            a = (U8)((a << 1) ^ ((a & 0x80) ? 0x1B : 0x00));
            b >>= 1;
        }
        return p;
    }

    U8 rotl8(U8 x, int s)
    {
        return (U8)((x << s) | (x >> (8 - s)));
    }

    struct Tables
    {
        U8 sbox[256];
        U8 inv[256];

        Tables()
        {
            for (int x = 0; x < 256; x++)
            {
                // x^254 is x's inverse (and 0 stays 0).
                U8 y = 1;
                for (int k = 0; k < 254; k++)
                {
                    y = gmul(y, (U8)x);
                }
                if (x == 0)
                {
                    y = 0;
                }
                U8 s = (U8)(y ^ rotl8(y, 1) ^ rotl8(y, 2) ^ rotl8(y, 3) ^ rotl8(y, 4) ^ 0x63);
                sbox[x] = s;
                inv[s] = (U8)x;
            }
        }
    };

    const Tables& tables()
    {
        static const Tables t;
        return t;
    }

    void addRound(U8 s[16], const U8 k[16])
    {
        for (int i = 0; i < 16; i++)
        {
            s[i] ^= k[i];
        }
    }

    // Row r of the state is bytes r, r+4, r+8, r+12; ShiftRows turns it left
    // by r.
    void shiftRows(U8 s[16], bool inverse)
    {
        U8 t[16];
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (inverse)
                {
                    t[r + 4 * ((c + r) % 4)] = s[r + 4 * c];
                }
                else
                {
                    t[r + 4 * c] = s[r + 4 * ((c + r) % 4)];
                }
            }
        }
        memcpy(s, t, 16);
    }

    void mixColumns(U8 s[16], bool inverse)
    {
        // The matrix's first row; each row after it is the one above turned
        // right by one.
        const U8 m0 = inverse ? 14 : 2;
        const U8 m1 = inverse ? 11 : 3;
        const U8 m2 = inverse ? 13 : 1;
        const U8 m3 = inverse ? 9 : 1;
        for (int c = 0; c < 4; c++)
        {
            U8* a = s + 4 * c;
            U8 b[4];
            for (int r = 0; r < 4; r++)
            {
                b[r] = (U8)(gmul(a[r], m0) ^ gmul(a[(r + 1) % 4], m1) ^ gmul(a[(r + 2) % 4], m2) ^
                            gmul(a[(r + 3) % 4], m3));
            }
            memcpy(a, b, 4);
        }
    }

    void subBytes(U8 s[16], const U8 box[256])
    {
        for (int i = 0; i < 16; i++)
        {
            s[i] = box[s[i]];
        }
    }
} // namespace

void iAES128Init(iAES128* a, const U8 key[IAES_BLOCK])
{
    const Tables& t = tables();
    U8* w = &a->round[0][0]; // 44 words of 4 bytes, in order
    memcpy(w, key, 16);

    U8 rcon = 1;
    for (int i = 4; i < 44; i++)
    {
        U8 temp[4];
        memcpy(temp, w + 4 * (i - 1), 4);
        if (i % 4 == 0)
        {
            U8 first = temp[0];
            temp[0] = (U8)(t.sbox[temp[1]] ^ rcon);
            temp[1] = t.sbox[temp[2]];
            temp[2] = t.sbox[temp[3]];
            temp[3] = t.sbox[first];
            rcon = gmul(rcon, 2);
        }
        for (int k = 0; k < 4; k++)
        {
            w[4 * i + k] = (U8)(w[4 * (i - 4) + k] ^ temp[k]);
        }
    }
}

void iAES128Encrypt(const iAES128* a, const U8 in[IAES_BLOCK], U8 out[IAES_BLOCK])
{
    const Tables& t = tables();
    U8 s[16];
    memcpy(s, in, 16);
    addRound(s, a->round[0]);
    for (int r = 1; r < 10; r++)
    {
        subBytes(s, t.sbox);
        shiftRows(s, false);
        mixColumns(s, false);
        addRound(s, a->round[r]);
    }
    subBytes(s, t.sbox);
    shiftRows(s, false);
    addRound(s, a->round[10]);
    memcpy(out, s, 16);
    iSaveWipe(s, sizeof(s));
}

void iAES128Decrypt(const iAES128* a, const U8 in[IAES_BLOCK], U8 out[IAES_BLOCK])
{
    const Tables& t = tables();
    U8 s[16];
    memcpy(s, in, 16);
    addRound(s, a->round[10]);
    for (int r = 9; r >= 1; r--)
    {
        shiftRows(s, true);
        subBytes(s, t.inv);
        addRound(s, a->round[r]);
        mixColumns(s, true);
    }
    shiftRows(s, true);
    subBytes(s, t.inv);
    addRound(s, a->round[0]);
    memcpy(out, s, 16);
    iSaveWipe(s, sizeof(s));
}

void iAES128Clear(iAES128* a)
{
    iSaveWipe(a, sizeof(*a));
}

void iSaveWipe(void* p, size_t n)
{
    volatile U8* b = (volatile U8*)p;
    while (n-- > 0)
    {
        *b++ = 0;
    }
}
