#ifndef IGCSWAP_H
#define IGCSWAP_H

#include <types.h>

// PC-only: GameCube assets, byte-swapped into the layout the port reads.
//
// The GameCube and Xbox releases of TSSM ship the same asset types with the
// same layouts; the GameCube's are big-endian. iGCSwapAsset turns one asset
// around, field by field, so that everything after the packer reads it exactly
// as it reads the Xbox copy.
//
// Most of it is generated: tools/gcswap/gen.py writes iGCSwapTable.inc from the
// decomp's own struct layouts (32-bit, as the packers wrote them) and a
// per-type list of those structs in tools/gcswap/spec.py. tssm_gcswaptest
// (tests/gcswaptest.cpp) runs it over every asset of a GameCube disc and
// compares each result with the Xbox asset of the same id. The method is the
// BfBB PC port's (CC0); the tables are TSSM's.
//
// Left as they are (IGCSWAP_ASIS): RenderWare streams (models, textures, the
// world clumps), which librw reads in the GameCube's native formats; the sound
// table and sound data, which the sound layer reads in the GameCube's layout;
// and the flythrough keys, which the GameCube disc ships little-endian.
//
// Only <types.h> and the C library, so the test can compile it on its own.

enum iGCSwapResult
{
    IGCSWAP_DONE, // swapped
    IGCSWAP_ASIS, // a type read as it is on the disc; nothing was changed
    IGCSWAP_UNKNOWN, // a type, or a DYNA subtype, with no rule
    IGCSWAP_MALFORMED // a count, size or offset that does not fit the asset
};

// Swaps one asset of packer type `type` ('SIMP' and so on) in place. Never
// reads or writes outside [data, data + size). On UNKNOWN or MALFORMED the
// bytes may be partly swapped: convert a copy, and drop it unless the result
// is DONE.
iGCSwapResult iGCSwapAsset(U32 type, void* data, U32 size);

// What iGCSwapAsset does with a type, without an asset: DONE for a type with a
// rule, ASIS or UNKNOWN. A DYNA's subtype is only known from the asset.
iGCSwapResult iGCSwapRule(U32 type);

// iGCSwapAsset's answer for an asset, worked out on a copy; `data` is not
// changed. MALFORMED also when the copy cannot be allocated.
iGCSwapResult iGCSwapCheck(U32 type, const void* data, U32 size);

// The four characters of a type code, for messages ("SIMP", "CSN ").
const char* iGCSwapTypeName(U32 type, char buf[5]);

#endif
