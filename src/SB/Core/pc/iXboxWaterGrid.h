#ifndef IXBOXWATERGRID_H
#define IXBOXWATERGRID_H

#include <types.h>

// PC-only: the arithmetic of the Xbox release's water (iXboxWater.h), with no
// game headers, so tests/watertest.cpp can check it alone. Written from the
// behaviour of the Xbox executable; the addresses are DEFAULT.XBE's.
//
// A water body's surface is a grid of flat, square tiles laid over the world XZ
// bounds of the body's mesh (xWaterAddBody, 0x1AD7A0):
//
//   - The tile edge is qx * cell units: 16 by default and in SC02, 64 in B301
//     (32 cells of 2). A tile has (qx + 1) x (qz + 1) vertices.
//   - The grid's corner is the bounds' floor snapped to whole tiles with C's
//     truncating %, then one more tile down when it is negative, even when it
//     was on a tile edge already: x0 = fx - fx % unit, and x0 -= unit if x0 < 0.
//   - Along z there are ceil(depth / cell) cells; along x, ceil(that count /
//     (depth / width)); each is rounded up to whole tiles.
//   - The height is the world y of the mesh's FIRST vertex.
//   - Tiles go row by row: tile (r, c) has its corner at (x0 + c * w, z0 + r * d).
//
// In SC02 only a tile with a mesh vertex strictly inside it is drawn; every
// other level draws them all.
struct iXboxWaterPreset
{
    // A tile farther than this from the camera (squared distance of its
    // centre) is not drawn.
    F32 cullDist2;

    // Cell size, and cells per tile edge along x and z.
    F32 cell;
    U32 qx;
    U32 qz;
};

// The tile flags. The Xbox keeps the same three bits on each tile's patch.
enum
{
    iXBOXWATER_TILE_HASVERTS = 0x1, // SC02: a mesh vertex lies inside
    iXBOXWATER_TILE_MASKED = 0x2, // SC02: none does, never drawn
    iXBOXWATER_TILE_CULLED = 0x4 // too far, or outside the view, this frame
};

struct iXboxWaterTile
{
    F32 y;
    F32 x0;
    F32 z0;
    F32 w;
    F32 d;

    // The bounding sphere the cull tests: the centre at the water's height,
    // and the half diagonal.
    F32 cx;
    F32 cy;
    F32 cz;
    F32 radius;

    U32 flags;
};

struct iXboxWaterLayout
{
    U32 rows; // along z
    U32 cols; // along x
    F32 x0;
    F32 z0;
    F32 y;
    F32 tileW;
    F32 tileD;
};

// The most tiles one body may have. The shipped bodies have 56, 90 and 144; a
// modded mesh spanning thousands of units would otherwise allocate without
// bound.
const U32 iXBOXWATER_MAX_TILES = 4096;

// The grid for a mesh given as world-space points (x, y, z per point). False,
// leaving *out untouched, when there are no points, a coordinate is not finite,
// the preset is unusable, or the grid would exceed iXBOXWATER_MAX_TILES.
bool iXboxWaterLayoutFor(const F32* xyz, U32 count, const iXboxWaterPreset& preset,
                         iXboxWaterLayout* out);

// Tile (r, c) of a layout, flags clear.
void iXboxWaterTileAt(const iXboxWaterLayout& layout, U32 r, U32 c, iXboxWaterTile* tile);

// Whether a point lies strictly inside the tile in x and z (SC02's mask).
bool iXboxWaterTileHasPoint(const iXboxWaterTile& tile, const F32* xyz, U32 count);

// The triangle-strip indices of one tile's (nv x nv) vertices, vertex (i, j) at
// i * nv + j: a strip per row of quads, joined by two repeated indices
// (0x1C3DA0). Returns the count, (nv - 1) * nv * 2 + (nv - 2) * 2: 574 for 17
// vertices a side, 2174 for 33. `out` may be NULL to ask for the count only;
// otherwise it must hold that many. 0 when nv is below 2 or above 255.
U32 iXboxWaterStripIndices(U32 nv, U16* out);

// The bump-map clock (iWaterUpdate, 0x1C39B0): t advances by speed * dt; at 2
// or more its whole part is taken off, as the Xbox computes it; the frame is
// t / (2 / 17), so the 17 frames take 2 / speed seconds (0.83 s at the
// default 2.4, 0.25 s at B301's 8). Returns the frame, 0 to 16; a t that is
// not finite or below zero starts again at 0.
S32 iXboxWaterClockStep(F32* t, F32 speed, F32 dt);

// The bump frames (iWaterLoadTextures 0x1B1B30, one frame 0x1B1A90): RAW assets
// bumps0..bumps16 of plat.HIP, 43008 bytes each. The Xbox copies the first
// 0x8000 bytes of each into a 128 x 128 V8U8 texture -- row-major, two signed
// bytes a texel, du then dv -- and never reads the other 10240. The data has a
// quirk of its own, dv = -1 wherever du is negative, and is drawn as it is.
const U32 iXBOXWATER_BUMP_FRAMES = 17;
const U32 iXBOXWATER_BUMP_SIZE = 128;
const U32 iXBOXWATER_BUMP_BYTES = 0x8000;

// One frame as 128 x 128 RGBA8, the order a texture of any renderer takes: R =
// du + 128, G = dv + 128, B = 0, A = 255. Filtering the biased bytes and taking
// 128 off afterwards gives what filtering the signed ones gives. Only the first
// iXBOXWATER_BUMP_BYTES of `src` are read. False, writing nothing, when `size`
// is below that. `out` holds 128 * 128 * 4 bytes.
bool iXboxWaterBumpToRGBA(const U8* src, U32 size, U8* out);

// The bump matrices' scales for a tile whose centre is `d` units from the camera
// (iWaterPatchLOD, 0x1C3B60): the refraction's is nearDist / d at or beyond
// nearDist and 1 nearer (10 units in every level), the reflection's num / d,
// unclamped (20, or 30 in B301). So the ripples bend the picture less with
// distance, the reflection's more than tenfold close up. The Xbox divides with
// no guard; here d is taken as at least 0.001 (and a d that is not a number as
// 0.001).
void iXboxWaterLodScales(F32 d, F32 nearDist, F32 num, F32* refract, F32* reflect);

// The two bump matrices before those scales (iWaterInitConstants, 0x1C2C40), as
// (m00, m01, m10, m11) in texbem's order: du' = m00 du + m10 dv, dv' = m01 du +
// m11 dv.
extern const F32 iXBOXWATER_BUMP_REFRACT[4];
extern const F32 iXBOXWATER_BUMP_REFLECT[4];

// The size of the refraction and reflection targets for a render size. The
// Xbox draws them at 512 x 512 for its 640 x 480 screen; here they keep its
// texels per screen pixel on each axis (0.8 across, 16/15 down), to the
// nearest multiple of 16, at least 16 and at most `cap` (taken down to a
// multiple of 16) on each axis: 640 x 480 gives 512 x 512, 1920 x 1080 gives
// 1536 x 1152. The distortion is in texture units, so it is the same size on
// screen at any of them.
void iXboxWaterTargetSize(S32 renderW, S32 renderH, S32 cap, S32* w, S32* h);

#endif
