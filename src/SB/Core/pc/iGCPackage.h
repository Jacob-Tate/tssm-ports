#ifndef IGCPACKAGE_H
#define IGCPACKAGE_H

#include <types.h>

// PC-only: which console's packer wrote a HIP or HOP, from the package itself.
//
// HIPA holds PACK, PACK holds PLAT, and PLAT is a run of NUL-terminated
// strings: a two-letter id, then the long name (BfBB's packs; TSSM's go
// straight on), the video standard, the region and the project. TSSM's
// retail packs say "BX" on the Xbox and "GC" on the GameCube; BfBB's Xbox packs
// say "XB". Chunk sizes are big-endian on every platform -- the container is
// the packer's, not the console's -- so the same walk reads them all, and every
// chunk before PLAT is small: the first few hundred bytes of the file are
// enough (iFile.cpp reads 512).
//
// Each package is judged on its own, so a set of Xbox files can take a
// GameCube mod package and the other way round. A package with no PLAT is not
// an answer either way (IGCPLAT_NONE); iFile.cpp treats it as Xbox, as before.
// A file that is not a package at all -- empty, or not starting with HIPA --
// is IGCPLAT_NOTPACKAGE, which iFile.cpp refuses from a mod.
//
// Only <types.h> and the C library, for tests/gcswaptest.cpp.

enum iGCPlatform
{
    IGCPLAT_NONE, // a package with no PLAT in the bytes given, or no bytes to judge
    IGCPLAT_XBOX, // "BX" or "XB"
    IGCPLAT_GAMECUBE, // "GC"
    IGCPLAT_OTHER, // anything else: "PS2", ...
    IGCPLAT_NOTPACKAGE // fewer than 16 bytes, or not starting with HIPA
};

// The platform of the package whose first `n` bytes are `head`. Writes the id
// ("GC") and the string after it (TSSM: "US"; BfBB: "GameCube") when the
// buffers are not NULL; "" when there is no answer.
iGCPlatform iGCPackagePlatform(const void* head, U32 n, char* id, U32 idsize, char* name,
                               U32 namesize);

// RWTX and TEXS: the GameCube disc names none of its textures. The native
// struct's name (+0x18) is "temp" (2,326 of them) or empty (664); the Xbox
// texture of the same asset is named after the asset, its ADBG name less
// ".RW3" (2,978 of 2,990; the other 12 Xbox ones are empty). The game itself
// names a texture when a material asks for it, but the port's texture mods and
// button prompts look textures up by the name they were read with. So, for a
// texture dictionary of GameCube textures (`data`, `size`: the asset), this
// writes the asset name less any trailing piece of ".RW3" (at most 31
// characters) over a native name that is "temp" or empty. Every chunk is
// checked against `size`; nothing outside it is touched. Returns how many
// names it wrote.
U32 iGCPackageRestoreTextureNames(void* data, U32 size, const char* assetName);

// MODL: the game reads a model with RwStreamFindChunk(rwID_CLUMP) and
// RpClumpStreamRead, and retail reads on through whatever that returns. The
// stream holds together when a clump chunk fits the asset (the chunks before
// it fit too), the clump's own chunks fill it exactly, the first is its
// struct, and the struct's atomic count, at least one, is the number of atomic
// chunks. Every model of both releases (3,021 GameCube, 3,040 Xbox) is a clump
// with nothing before it, laid out that way. Damage further inside is for the
// reader to find (librw's GameCube geometry patch checks its own counts).
bool iGCPackageModelFits(const void* data, U32 size);

#endif
