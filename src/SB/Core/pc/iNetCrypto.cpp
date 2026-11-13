// PC-only, co-op: SHA-256, HMAC, PBKDF2, HKDF and the OS random source
// (iNetCrypto.h).
#include "iNetCrypto.h"

#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#elif defined(__SWITCH__)
#include <switch.h>
#include <atomic>
#include <stdio.h>
#elif defined(__ANDROID__)
#include <stdlib.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace iNet {

static const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void compress(uint32_t h[8], const uint8_t* p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++)
    {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++)
    {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_init(Sha256* s)
{
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, iv, sizeof iv);
    s->total = 0;
    s->used = 0;
}

void sha256_update(Sha256* s, const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    s->total += len;
    if (s->used)
    {
        size_t take = 64 - s->used;
        if (take > len) take = len;
        memcpy(s->buf + s->used, p, take);
        s->used += take; p += take; len -= take;
        if (s->used < 64) return;
        compress(s->h, s->buf);
        s->used = 0;
    }
    while (len >= 64) { compress(s->h, p); p += 64; len -= 64; }
    if (len) { memcpy(s->buf, p, len); s->used = len; }
}

void sha256_final(Sha256* s, uint8_t out[32])
{
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->used != 56) sha256_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++)
    {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void sha256(const void* data, size_t len, uint8_t out[32])
{
    Sha256 s;
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, out);
}

void hmac_init(Hmac* h, const uint8_t* key, size_t keylen)
{
    uint8_t k[64] = { 0 };
    if (keylen > 64) sha256(key, keylen, k);
    else if (keylen) memcpy(k, key, keylen);
    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&h->inner);
    sha256_update(&h->inner, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&h->outer);
    sha256_update(&h->outer, pad, 64);
    secure_zero(k, sizeof k);
    secure_zero(pad, sizeof pad);
}

void hmac_update(Hmac* h, const void* data, size_t len) { sha256_update(&h->inner, data, len); }

void hmac_final(Hmac* h, uint8_t out[32])
{
    uint8_t ih[32];
    sha256_final(&h->inner, ih);
    sha256_update(&h->outer, ih, 32);
    sha256_final(&h->outer, out);
}

void hmac_sha256(const uint8_t* key, size_t keylen, const void* msg, size_t len, uint8_t out[32])
{
    Hmac h;
    hmac_init(&h, key, keylen);
    hmac_update(&h, msg, len);
    hmac_final(&h, out);
    secure_zero(&h, sizeof h);
}

void pbkdf2_sha256(const uint8_t* pw, size_t pwlen, const uint8_t* salt, size_t saltlen,
                   uint32_t iters, uint8_t* out, size_t outlen)
{
    Hmac base;
    hmac_init(&base, pw, pwlen); // keyed once, copied per block and per iteration
    for (uint32_t block = 1; outlen; block++)
    {
        uint8_t be[4] = { (uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block };
        Hmac h = base;
        hmac_update(&h, salt, saltlen);
        hmac_update(&h, be, 4);
        uint8_t u[32], t[32];
        hmac_final(&h, u);
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iters; i++)
        {
            h = base;
            hmac_update(&h, u, 32);
            hmac_final(&h, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        size_t n = outlen < 32 ? outlen : 32;
        memcpy(out, t, n);
        out += n;
        outlen -= n;
        secure_zero(u, 32);
        secure_zero(t, 32);
    }
    secure_zero(&base, sizeof base);
}

bool hkdf_sha256(const uint8_t* salt, size_t saltlen, const uint8_t* ikm, size_t ikmlen,
                 const void* info, size_t infolen, uint8_t* out, size_t outlen)
{
    if (outlen > 255 * 32) return false;
    uint8_t zeros[32] = { 0 };
    uint8_t prk[32];
    if (!salt || !saltlen) { salt = zeros; saltlen = 32; }
    hmac_sha256(salt, saltlen, ikm, ikmlen, prk);
    uint8_t t[32];
    size_t tlen = 0;
    for (uint8_t i = 1; outlen; i++)
    {
        Hmac h;
        hmac_init(&h, prk, 32);
        hmac_update(&h, t, tlen);
        hmac_update(&h, info, infolen);
        hmac_update(&h, &i, 1);
        hmac_final(&h, t);
        tlen = 32;
        size_t n = outlen < 32 ? outlen : 32;
        memcpy(out, t, n);
        out += n;
        outlen -= n;
    }
    secure_zero(prk, sizeof prk);
    secure_zero(t, sizeof t);
    return true;
}

bool ct_equal(const uint8_t* a, const uint8_t* b, size_t n)
{
    volatile uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d = d | (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

void secure_zero(void* p, size_t n)
{
    volatile uint8_t* v = (volatile uint8_t*)p;
    while (n--) *v++ = 0;
}

bool os_random(void* out, size_t n)
{
#if defined(_WIN32)
    while (n)
    {
        ULONG chunk = n > 0x10000000 ? 0x10000000 : (ULONG)n;
        if (BCryptGenRandom(NULL, (PUCHAR)out, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
        out = (uint8_t*)out + chunk;
        n -= chunk;
    }
    return true;
#elif defined(__SWITCH__)
    // Horizon has no /dev/urandom. csrng is the system's secure source; if it
    // cannot start, the kernel-seeded generator.
    static const bool csrng = R_SUCCEEDED(csrngInitialize());
    if (csrng && R_SUCCEEDED(csrngGetRandomBytes(out, n)))
        return true;
    static std::atomic<bool> said(false);
    if (!said.exchange(true))
    {
        printf("tssm: coop: the system's secure random source (csrng) is not available; using the "
               "kernel-seeded generator\n");
        fflush(stdout);
    }
    randomGet(out, n);
    return true;
#elif defined(__ANDROID__)
    // getrandom is API 28; the APK's minimum is 24. Never fails.
    arc4random_buf(out, n);
    return true;
#else
    uint8_t* p = (uint8_t*)out;
#if defined(__linux__)
    while (n)
    {
        ssize_t r = getrandom(p, n, 0);
        if (r < 0) { if (errno == EINTR) continue; break; }
        p += r; n -= (size_t)r;
    }
    if (!n) return true;
#endif
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    while (n)
    {
        ssize_t r = read(fd, p, n);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; close(fd); return false; }
        p += r; n -= (size_t)r;
    }
    close(fd);
    return true;
#endif
}

} // namespace iNet
