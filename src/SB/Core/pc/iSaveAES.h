#ifndef ISAVEAES_H
#define ISAVEAES_H

// PC-only: AES-128 (FIPS 197), one 16-byte block at a time, for the signature
// of a PlayStation 3's .psv export (iSavePS2.h). Small and portable: <types.h>
// and the C library. Its tables are worked out from the standard's
// definitions when first used. Known answers: tests/xsave_selftest.cpp
// (FIPS 197's and SP 800-38A's).

#include <types.h>

#define IAES_BLOCK 16

struct iAES128
{
    U8 round[11][IAES_BLOCK]; // the expanded key: one round key per round
};

void iAES128Init(iAES128* a, const U8 key[IAES_BLOCK]);
void iAES128Encrypt(const iAES128* a, const U8 in[IAES_BLOCK], U8 out[IAES_BLOCK]);
void iAES128Decrypt(const iAES128* a, const U8 in[IAES_BLOCK], U8 out[IAES_BLOCK]);

// Clears the expanded key.
void iAES128Clear(iAES128* a);

// Zeroes n bytes through a volatile pointer, so the compiler cannot drop it as
// a store nothing reads (a plain memset just before a buffer ends can be).
// For key material.
void iSaveWipe(void* p, size_t n);

#endif
