#ifndef ICLIP2D_H
#define ICLIP2D_H

#include <types.h>

// PC-only: cutting a 2D triangle at two lines across the screen, for
// rw/im.cpp's cut at the 4:3 area's top and bottom on a screen taller than 4:3
// (iScreen2DClipY in iScreen.h). A template so that tssm_hudtest can check it
// with a vertex of its own: the vertex needs RwIm2DVertex's fields (x, y, z, w,
// u, v and emissiveColor's red, green, blue and alpha).

// The point t of the way from a to b, across the screen. The position and the
// screen z are straight lines on the screen. u, v and the colour go as the
// rasteriser takes them, over 1/w (w is the camera z, rwplcore.h), so a cut
// edge shows what the whole triangle showed there. With one w, as nearly all
// 2D has, that is a straight line too.
template <class V> V iClip2DLerp(const V& a, const V& b, F32 t)
{
    V r = a;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;

    F32 sa = 1.0f - t;
    F32 sb = t;
    if (a.w > 0.0f && b.w > 0.0f)
    {
        const F32 qa = (1.0f - t) / a.w;
        const F32 qb = t / b.w;
        const F32 q = qa + qb;
        r.w = 1.0f / q;
        sa = qa / q;
        sb = qb / q;
    }
    else
    {
        r.w = a.w + (b.w - a.w) * t;
    }

    r.u = a.u * sa + b.u * sb;
    r.v = a.v * sa + b.v * sb;

    const F32 red = (F32)a.emissiveColor.red * sa + (F32)b.emissiveColor.red * sb + 0.5f;
    const F32 green = (F32)a.emissiveColor.green * sa + (F32)b.emissiveColor.green * sb + 0.5f;
    const F32 blue = (F32)a.emissiveColor.blue * sa + (F32)b.emissiveColor.blue * sb + 0.5f;
    const F32 alpha = (F32)a.emissiveColor.alpha * sa + (F32)b.emissiveColor.alpha * sb + 0.5f;
    r.emissiveColor.red = (U8)(red > 255.0f ? 255.0f : red);
    r.emissiveColor.green = (U8)(green > 255.0f ? 255.0f : green);
    r.emissiveColor.blue = (U8)(blue > 255.0f ? 255.0f : blue);
    r.emissiveColor.alpha = (U8)(alpha > 255.0f ? 255.0f : alpha);
    return r;
}

// One side of a line across the screen: the part of the polygon `in` (n
// corners) with y >= edge (keep = 1: under the line, y runs down the screen)
// or y <= edge (keep = -1), in the same order, into `out` (room for n + 1).
// Returns its corners.
template <class V> S32 iClip2DCutY(const V* in, S32 n, F32 edge, F32 keep, V* out)
{
    S32 m = 0;
    for (S32 i = 0; i < n; i++)
    {
        const V& a = in[i];
        const V& b = in[i + 1 < n ? i + 1 : 0];
        const bool inA = keep * (a.y - edge) >= 0.0f;
        const bool inB = keep * (b.y - edge) >= 0.0f;
        if (inA)
        {
            out[m++] = a;
        }
        if (inA != inB)
        {
            out[m++] = iClip2DLerp(a, b, (edge - a.y) / (b.y - a.y));
        }
    }
    return m;
}

// The triangle (a, b, c) cut to top <= y <= bottom, as a list of triangles
// with its winding, into `out` (room for 9). Returns the vertices written: 0
// when nothing is left, 3, 6 or 9.
template <class V>
S32 iClip2DTriangle(const V& a, const V& b, const V& c, F32 top, F32 bottom, V* out)
{
    const V tri[3] = { a, b, c };
    V below[4];
    V band[5];
    S32 n = iClip2DCutY(tri, 3, top, 1.0f, below);
    n = iClip2DCutY(below, n, bottom, -1.0f, band);

    // What is left is convex: a fan from its first corner.
    S32 m = 0;
    for (S32 i = 1; i + 1 < n; i++)
    {
        out[m++] = band[0];
        out[m++] = band[i];
        out[m++] = band[i + 1];
    }
    return m;
}

#endif
