// The Xbox water's tiles, strips, bump clock, bump frames and target size
// (iXboxWaterGrid.h).

#include "iXboxWaterGrid.h"

#include <math.h>
#include <stddef.h>

namespace
{
    // Coordinates beyond this are refused before they reach an int (the
    // shipped meshes stay within 400 units of the origin).
    const F32 kMaxCoord = 1.0e6f;

    bool Usable(F32 v)
    {
        return isfinite(v) && fabsf(v) <= kMaxCoord;
    }

    // The grid's corner on one axis: the floor of the minimum, snapped down to
    // a whole tile by C's truncating %, and one more tile down when negative.
    F32 Corner(F32 lo, S32 unit)
    {
        S32 f = (S32)floorf(lo);
        F32 c = (F32)(f - f % unit);

        if (c < 0.0f)
        {
            c -= (F32)unit;
        }

        return c;
    }
} // namespace

bool iXboxWaterLayoutFor(const F32* xyz, U32 count, const iXboxWaterPreset& preset,
                         iXboxWaterLayout* out)
{
    if (xyz == NULL || count == 0 || out == NULL || !(preset.cell > 0.0f) || !Usable(preset.cell) ||
        preset.qx == 0 || preset.qz == 0 || preset.qx > 254 || preset.qz > 254)
    {
        return false;
    }

    F32 xmin = xyz[0];
    F32 xmax = xyz[0];
    F32 zmin = xyz[2];
    F32 zmax = xyz[2];

    for (U32 i = 0; i < count; i++)
    {
        F32 x = xyz[3 * i];
        F32 y = xyz[3 * i + 1];
        F32 z = xyz[3 * i + 2];

        if (!Usable(x) || !Usable(y) || !Usable(z))
        {
            return false;
        }

        xmin = x < xmin ? x : xmin;
        xmax = x > xmax ? x : xmax;
        zmin = z < zmin ? z : zmin;
        zmax = z > zmax ? z : zmax;
    }

    S32 unit = (S32)((F32)preset.qx * preset.cell);

    if (unit <= 0)
    {
        return false;
    }

    F32 x0 = Corner(xmin, unit);
    F32 z0 = Corner(zmin, unit);
    F32 width = ceilf(xmax) - x0;
    F32 depth = ceilf(zmax) - z0;

    // A mesh with no extent on an axis has no tiles (the Xbox divides by it).
    if (!(width > 0.0f) || !(depth > 0.0f))
    {
        return false;
    }

    // In double; the Xbox's x87 keeps more bits. The three shipped bodies come
    // out the same at float and double precision (127 x 111, 159 x 142 and
    // 383 x 383 cells before rounding up).
    double zCells = ceil((double)depth / (double)preset.cell);
    double xCells = ceil(zCells / ((double)depth / (double)width));

    if (!(zCells >= 1.0) || !(xCells >= 1.0) || zCells > 1.0e6 || xCells > 1.0e6)
    {
        return false;
    }

    U32 nz = (U32)zCells;
    U32 nx = (U32)xCells;

    if (nz % preset.qx)
    {
        nz += preset.qx - nz % preset.qx;
    }

    if (nx % preset.qz)
    {
        nx += preset.qz - nx % preset.qz;
    }

    // Rows count tiles along z and are cut by qx, columns along x by qz, as the
    // Xbox does; every preset has qx == qz.
    U32 rows = nz / preset.qx;
    U32 cols = nx / preset.qz;

    if (rows == 0 || cols == 0 || (U64)rows * cols > iXBOXWATER_MAX_TILES)
    {
        return false;
    }

    out->rows = rows;
    out->cols = cols;
    out->x0 = x0;
    out->z0 = z0;
    out->y = xyz[1];
    out->tileW = (F32)preset.qx * preset.cell;
    out->tileD = (F32)preset.qz * preset.cell;
    return true;
}

void iXboxWaterTileAt(const iXboxWaterLayout& layout, U32 r, U32 c, iXboxWaterTile* tile)
{
    tile->y = layout.y;
    tile->w = layout.tileW;
    tile->d = layout.tileD;
    tile->x0 = (F32)c * tile->w + layout.x0;
    tile->z0 = (F32)r * tile->d + layout.z0;

    F32 hw = tile->w * 0.5f;
    F32 hd = tile->d * 0.5f;

    tile->cx = hw + tile->x0;
    tile->cy = layout.y;
    tile->cz = hd + tile->z0;
    tile->radius = sqrtf(hw * hw + hd * hd);
    tile->flags = 0;
}

bool iXboxWaterTileHasPoint(const iXboxWaterTile& tile, const F32* xyz, U32 count)
{
    F32 x1 = tile.w + tile.x0;
    F32 z1 = tile.d + tile.z0;

    for (U32 i = 0; i < count; i++)
    {
        F32 x = xyz[3 * i];
        F32 z = xyz[3 * i + 2];

        if (x > tile.x0 && x < x1 && z > tile.z0 && z < z1)
        {
            return true;
        }
    }

    return false;
}

U32 iXboxWaterStripIndices(U32 nv, U16* out)
{
    if (nv < 2 || nv > 255)
    {
        return 0;
    }

    U32 total = (nv - 1) * nv * 2 + (nv - 2) * 2;

    if (out == NULL)
    {
        return total;
    }

    U32 n = 0;
    U32 k = 0;

    for (U32 r = 0; r + 1 < nv; r++)
    {
        for (U32 c = nv; c != 0; c--)
        {
            out[n++] = (U16)k;
            out[n++] = (U16)(nv + k);
            k++;
        }

        // The join to the next row: its last vertex, then the next row's first.
        if (r + 2 < nv)
        {
            out[n++] = (U16)(nv + k - 1);
            out[n++] = (U16)k;
        }
    }

    return n;
}

S32 iXboxWaterClockStep(F32* t, F32 speed, F32 dt)
{
    // The period and the frame time, as the Xbox keeps them (2.0 and 2 / 17).
    const F32 period = 2.0f;
    const F32 frameTime = 2.0f / 17.0f;

    F32 v = *t + speed * dt;

    // Not on the Xbox: a clock that cannot be cast to an int restarts.
    if (!isfinite(v) || v < 0.0f || v > kMaxCoord)
    {
        v = 0.0f;
    }

    if (v >= period)
    {
        v -= (F32)(S32)v / period * period;
    }

    *t = v;

    S32 frame = (S32)(v / frameTime);
    return frame > 16 ? 16 : frame;
}

bool iXboxWaterBumpToRGBA(const U8* src, U32 size, U8* out)
{
    if (src == NULL || out == NULL || size < iXBOXWATER_BUMP_BYTES)
    {
        return false;
    }

    const U32 texels = iXBOXWATER_BUMP_SIZE * iXBOXWATER_BUMP_SIZE;

    for (U32 i = 0; i < texels; i++)
    {
        // A signed byte plus 128 is the unsigned byte with its top bit flipped.
        out[4 * i + 0] = (U8)(src[2 * i + 0] ^ 0x80);
        out[4 * i + 1] = (U8)(src[2 * i + 1] ^ 0x80);
        out[4 * i + 2] = 0;
        out[4 * i + 3] = 0xFF;
    }

    return true;
}

const F32 iXBOXWATER_BUMP_REFRACT[4] = { -0.02f, 0.05f, 0.05f, -0.02f };
const F32 iXBOXWATER_BUMP_REFLECT[4] = { -0.02f, 0.25f, 0.25f, -0.02f };

void iXboxWaterLodScales(F32 d, F32 nearDist, F32 num, F32* refract, F32* reflect)
{
    if (!(d >= 0.001f))
    {
        d = 0.001f;
    }

    *refract = d >= nearDist ? nearDist / d : 1.0f;
    *reflect = num / d;
}

namespace
{
    // n * num / den to the nearest multiple of 16, in [16, cap].
    S32 TargetEdge(S32 n, S32 num, S32 den, S32 cap)
    {
        cap -= cap % 16;
        cap = cap < 16 ? 16 : cap;

        if (n <= 0)
        {
            return 16;
        }

        long long units = ((long long)n * num + (long long)den * 8) / ((long long)den * 16);
        long long edge = units * 16;
        return edge < 16 ? 16 : (edge > cap ? cap : (S32)edge);
    }
} // namespace

void iXboxWaterTargetSize(S32 renderW, S32 renderH, S32 cap, S32* w, S32* h)
{
    *w = TargetEdge(renderW, 512, 640, cap);
    *h = TargetEdge(renderH, 512, 480, cap);
}
