// PC-only, co-op (docs/MULTIPLAYER.md): the hashing the handshake and the
// packet tags need. SHA-256 (FIPS 180-4), HMAC (RFC 2104), PBKDF2 (RFC 8018),
// HKDF (RFC 5869), written from the specifications and checked against their
// published test vectors by tssm_nettest. No encryption: packets are
// authenticated, not hidden (docs/MULTIPLAYER.md, section 11, "Encryption").
#ifndef INETCRYPTO_H
#define INETCRYPTO_H

#include <stddef.h>
#include <stdint.h>

namespace iNet {

struct Sha256
{
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[64];
    size_t used;
};

void sha256_init(Sha256* s);
void sha256_update(Sha256* s, const void* data, size_t len);
void sha256_final(Sha256* s, uint8_t out[32]);
void sha256(const void* data, size_t len, uint8_t out[32]);

struct Hmac
{
    Sha256 inner;
    Sha256 outer;
};

void hmac_init(Hmac* h, const uint8_t* key, size_t keylen);
void hmac_update(Hmac* h, const void* data, size_t len);
void hmac_final(Hmac* h, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, size_t keylen, const void* msg, size_t len, uint8_t out[32]);

void pbkdf2_sha256(const uint8_t* pw, size_t pwlen, const uint8_t* salt, size_t saltlen,
                   uint32_t iters, uint8_t* out, size_t outlen);

// outlen <= 255*32.
bool hkdf_sha256(const uint8_t* salt, size_t saltlen, const uint8_t* ikm, size_t ikmlen,
                 const void* info, size_t infolen, uint8_t* out, size_t outlen);

// Constant time in n: the comparison of a MAC must not stop at the first
// differing byte.
bool ct_equal(const uint8_t* a, const uint8_t* b, size_t n);
void secure_zero(void* p, size_t n);

// The OS CSPRNG: BCryptGenRandom on Windows, getrandom or /dev/urandom on
// POSIX (Android included). The Switch build would use libnx's csrng.
bool os_random(void* out, size_t n);

} // namespace iNet

#endif
