#ifndef ISAVESHA1_H
#define ISAVESHA1_H

// PC-only: SHA-1 and HMAC-SHA1 (FIPS 180-1, RFC 2104), for the signature at the
// end of an Xbox save (iSaveXbox.h). Small and portable: <types.h> and the C
// library. Known answers: tests/xsave_selftest.cpp (FIPS 180-1's and RFC 2202's).

#include <types.h>

#include <stddef.h>

#define ISHA1_SIZE 20

struct iSHA1
{
    U32 h[5];
    U64 length; // bytes taken so far
    U8 block[64];
    U32 fill;
};

void iSHA1Init(iSHA1* s);
void iSHA1Update(iSHA1* s, const void* data, size_t n);
void iSHA1Final(iSHA1* s, U8 out[ISHA1_SIZE]);

void iSHA1Of(const void* data, size_t n, U8 out[ISHA1_SIZE]);

// HMAC-SHA1(key, data1 then data2). Either part may be NULL with length 0.
void iHMACSHA1(const void* key, size_t keysize, const void* data1, size_t n1, const void* data2,
               size_t n2, U8 out[ISHA1_SIZE]);

#endif
