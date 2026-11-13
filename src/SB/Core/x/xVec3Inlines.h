#ifndef XVEC3INLINES_H
#define XVEC3INLINES_H

#include "xVec3.h"
#include "xMathInlines.h"

inline void xVec3Sub(xVec3* o, const xVec3* a, const xVec3* b)
{
    o->x = a->x - b->x;
    o->y = a->y - b->y;
    o->z = a->z - b->z;
}
inline void xVec3Cross(xVec3* o, const xVec3* a, const xVec3* b)
{
    o->x = a->y * b->z - b->y * a->z;
    o->y = a->z * b->x - b->z * a->x;
    o->z = a->x * b->y - b->x * a->y;
}

// No GC symbol (always inlined); signature from Ratatouille. The epsilon really is 0.
inline U32 xVec3Equals(const xVec3* a, const xVec3* b)
{
    return xeq(a->x, b->x, 0.0f) && xeq(a->y, b->y, 0.0f) && xeq(a->z, b->z, 0.0f);
}

inline void xVec3Inv(xVec3* o, const xVec3* v)
{
    o->x = -v->x;
    o->y = -v->y;
    o->z = -v->z;
}

void xVec3Copy(xVec3* o, const xVec3* v);
// No GC symbol (always inlined); signature from Ratatouille.
inline F32 xVec3Length(F32 _x, F32 _y, F32 _z)
{
    return xsqrt(_x * _x + _y * _y + _z * _z);
}

inline F32 xVec3Length(const xVec3* v)
{
    return xsqrt(v->x * v->x + v->y * v->y + v->z * v->z);
}
inline void xVec3SMul(xVec3* o, const xVec3* v, F32 s)
{
    o->x = v->x * s;
    o->y = v->y * s;
    o->z = v->z * s;
}
inline void xVec3Add(xVec3* o, const xVec3* a, const xVec3* b)
{
    o->x = a->x + b->x;
    o->y = a->y + b->y;
    o->z = a->z + b->z;
}
inline void xVec3Init(xVec3* v, F32 _x, F32 _y, F32 _z)
{
    v->x = _x;
    v->y = _y;
    v->z = _z;
}

inline void xVec3AddTo(xVec3* o, const xVec3* v)
{
    o->x += v->x;
    o->y += v->y;
    o->z += v->z;
}

inline void xVec3Lerp(xVec3* o, const xVec3* a, const xVec3* b, F32 t)
{
    o->x = a->x * (1.0f - t) + b->x * t;
    o->y = a->y * (1.0f - t) + b->y * t;
    o->z = a->z * (1.0f - t) + b->z * t;
}

// No GC symbol (always inlined).
inline void xVec3ScaleC(xVec3* o, const xVec3* v, F32 x, F32 y, F32 z)
{
    o->x = v->x * x;
    o->y = v->y * y;
    o->z = v->z * z;
}

// No GC symbol (always inlined); body from Ratatouille.
inline F32 xVec3NormalizeSafe(xVec3& v)
{
    F32 len_squ = v.length2();
    if (len_squ < 9.9999994e-11f)
    {
        return 0.0f;
    }
    len_squ = xsqrt(len_squ);
    v *= 1.0f / len_squ;
    return len_squ;
}

// No GC symbol (always inlined); body from Ratatouille.
inline F32 xVec3NormalizeSafe(xVec3& o, const xVec3& v)
{
    F32 len_squ = v.length2();
    if (len_squ < 9.9999994e-11f)
    {
        return 0.0f;
    }
    len_squ = xsqrt(len_squ);
    xVec3SMul(&o, &v, 1.0f / len_squ);
    return len_squ;
}

inline F32 xVec3Dist(const xVec3* a, const xVec3* b)
{
    F32 dx = a->x - b->x;
    F32 dy = a->y - b->y;
    F32 dz = a->z - b->z;

    return xsqrt(dx * dx + dy * dy + dz * dz);
}

// Locals as in Ratatouille; the named d changes register allocation in callers that inline this.
inline F32 xVec3Dist2(const xVec3* a, const xVec3* b)
{
    F32 d;
    F32 dx__ = a->x - b->x;
    F32 dy__ = a->y - b->y;
    F32 dz__ = a->z - b->z;
    d = dx__ * dx__ + dy__ * dy__ + dz__ * dz__;
    return d;
}

// Component overload from Ratatouille's xVec3Inlines.h; used by zThrown_Update.
static inline F32 xVec3Dist2(F32 ax, F32 ay, F32 az, F32 bx, F32 by, F32 bz)
{
    F32 d;
    F32 dx__ = ax - bx;
    F32 dy__ = ay - by;
    F32 dz__ = az - bz;
    d = dx__ * dx__ + dy__ * dy__ + dz__ * dz__;
    return d;
}

inline F32 xVec3Length2(const xVec3* vec)
{
    return vec->x * vec->x + vec->y * vec->y + vec->z * vec->z;
}

inline F32 xVec3LengthFast(F32 x, F32 y, F32 z)
{
    F32 len;
    xsqrtfast(len, x * x + y * y + z * z);
    return len;
}

// No GC symbol (always inlined); from BfBB.
inline F32 xVec3LengthFast(const xVec3* v)
{
    F32 len;
    xsqrtfast(len, v->x * v->x + v->y * v->y + v->z * v->z);
    return len;
}

inline void xVec3AddScaled(xVec3* o, const xVec3* v, F32 s)
{
    o->x += v->x * s;
    o->y += v->y * s;
    o->z += v->z * s;
}

inline void xVec3AddScaled(xVec3& o, const xVec3& a, const xVec3& b, F32 scale)
{
    o.x = a.x + b.x * scale;
    o.y = a.y + b.y * scale;
    o.z = a.z + b.z * scale;
}

inline void xVec3SMulBy(xVec3* v, F32 s)
{
    v->x *= s;
    v->y *= s;
    v->z *= s;
}

inline void xVec3SubFrom(xVec3* o, const xVec3* v)
{
    o->x -= v->x;
    o->y -= v->y;
    o->z -= v->z;
}

#define xVec3NormalizeMacro(o, v, len)                                                             \
    MACRO_START                                                                                    \
    {                                                                                              \
        F32 len2 = SQR((v)->x) + SQR((v)->y) + SQR((v)->z);                                        \
        if (xeq(len2, 1.0f, 1e-5f))                                                                \
        {                                                                                          \
            (o)->x = (v)->x;                                                                       \
            (o)->y = (v)->y;                                                                       \
            (o)->z = (v)->z;                                                                       \
            *(len) = 1.0f;                                                                         \
        }                                                                                          \
        else if (xeq(len2, 0.0f, 1e-5f))                                                           \
        {                                                                                          \
            (o)->x = 0.0f;                                                                         \
            (o)->y = 1.0f;                                                                         \
            (o)->z = 0.0f;                                                                         \
            *(len) = 0.0f;                                                                         \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            *(len) = xsqrt(len2);                                                                  \
            F32 len_inv = 1.0f / *(len);                                                           \
            (o)->x = (v)->x * len_inv;                                                             \
            (o)->y = (v)->y * len_inv;                                                             \
            (o)->z = (v)->z * len_inv;                                                             \
        }                                                                                          \
    }                                                                                              \
    MACRO_STOP

// Same as xVec3NormalizeMacro with the estimated square root (a macro: the PS2 line table puts
// xVec3NormalizeFast on one source line).
#define xVec3NormalizeFastMacro(o, v, len)                                                         \
    MACRO_START                                                                                    \
    {                                                                                              \
        F32 len2 = SQR((v)->x) + SQR((v)->y) + SQR((v)->z);                                        \
        if (xeq(len2, 1.0f, 1e-5f))                                                                \
        {                                                                                          \
            (o)->x = (v)->x;                                                                       \
            (o)->y = (v)->y;                                                                       \
            (o)->z = (v)->z;                                                                       \
            *(len) = 1.0f;                                                                         \
        }                                                                                          \
        else if (xeq(len2, 0.0f, 1e-5f))                                                           \
        {                                                                                          \
            (o)->x = 0.0f;                                                                         \
            (o)->y = 1.0f;                                                                         \
            (o)->z = 0.0f;                                                                         \
            *(len) = 0.0f;                                                                         \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            xsqrtfast(*(len), len2);                                                               \
            F32 len_inv = 1.0f / *(len);                                                           \
            (o)->x = (v)->x * len_inv;                                                             \
            (o)->y = (v)->y * len_inv;                                                             \
            (o)->z = (v)->z * len_inv;                                                             \
        }                                                                                          \
    }                                                                                              \
    MACRO_STOP

#endif
