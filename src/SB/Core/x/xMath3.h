#ifndef XMATH3_H
#define XMATH3_H

#include "xMath.h"

#include "xVec3.h"
#include "xVec3Inlines.h"

// Size: 0x30
struct xMat3x3
{
    xVec3 right;
    S32 flags;
    xVec3 up;
    U32 pad1;
    xVec3 at;
    U32 pad2;
} ALIGNB(16);

// Size: 0x40
struct xMat4x3 : xMat3x3
{
    xVec3 pos;
    U32 pad3;
} ALIGNB(16);

struct xSphere
{
    xVec3 center;
    F32 r;
};

// Size: 0x18
struct xBox
{
    xVec3 upper;
    xVec3 lower;
};

struct xBBox
{
    xVec3 center;
    xBox box;
};

struct xCylinder
{
    xVec3 center;
    F32 r;
    F32 h;
};

struct xQuat
{
    xVec3 v;
    F32 s;
};

struct xVec4
{
    F32 x;
    F32 y;
    F32 z;
    F32 w;
};

struct xRot
{
    xVec3 axis;
    F32 angle;
};

struct xLine3
{
    xVec3 p1;
    xVec3 p2;
};

struct xCapsule
{
    xVec3 start;
    xVec3 end;
    F32 r;
};

struct xRay3;
struct xIsect;

extern const xQuat g_IQ;
extern const xVec3 g_O3;
extern const xVec3 g_X3;
extern const xVec3 g_Y3;
extern const xVec3 g_Z3;
extern const xVec3 g_NX3;
extern const xVec3 g_NY3;
extern const xVec3 g_NZ3;
extern const xVec3 g_Onez;
extern xMat4x3 g_I3;

inline void xMat3x3Copy(xMat3x3* o, const xMat3x3* m)
{
    *o = *m;
}

inline void xMat4x3Copy(xMat4x3* o, const xMat4x3* m)
{
    *o = *m;
}

inline void xMat3x3Identity(xMat3x3* m)
{
    xMat3x3Copy(m, &g_I3);
}

inline void xMat4x3Identity(xMat4x3* m)
{
    xMat4x3Copy(m, &g_I3);
}

inline void xQuatCopy(xQuat* a, const xQuat* b)
{
    a->s = b->s;
    a->v.x = b->v.x;
    a->v.y = b->v.y;
    a->v.z = b->v.z;
}

inline void xQuatConj(xQuat* o, const xQuat* q)
{
    o->s = q->s;
    xVec3Inv(&o->v, &q->v);
}

// Box around the segment s-e grown by r. No symbol in TSSM (always inlined); name and
// parameters from the Ratatouille GC DWARF.
inline void xBoxFromCapsule(xBox& box, const xVec3& s, const xVec3& e, F32 r)
{
    xVec3& l = box.lower;
    xVec3& u = box.upper;

    if (s.x < e.x)
    {
        l.x = s.x;
        u.x = e.x;
    }
    else
    {
        l.x = e.x;
        u.x = s.x;
    }

    if (s.y < e.y)
    {
        l.y = s.y;
        u.y = e.y;
    }
    else
    {
        l.y = e.y;
        u.y = s.y;
    }

    if (s.z < e.z)
    {
        l.z = s.z;
        u.z = e.z;
    }
    else
    {
        l.z = e.z;
        u.z = s.z;
    }

    l.x -= r;
    l.y -= r;
    l.z -= r;
    u.x += r;
    u.y += r;
    u.z += r;
}

void xMath3Init();
void xSphereRandomPointOnSphere(const xSphere* sphere, xVec3& pt);
void xMat4x3RotY(xMat4x3* m, F32 t, const xVec3* p);
void xMat4x3Rot(xMat4x3* m, const xVec3* a, F32 t, const xVec3* p);
void xBoxInitBoundCapsule(xBox* b, const xCapsule* c);
void xLine3VecDist2(const xVec3* p1, const xVec3* p2, const xVec3* v, xIsect* isx);
void xTri3VecMinDist(const xVec3* V0, const xVec3* V1, const xVec3* V2, const xVec3* p, F32& s,
                     F32& t);

void xMat4x3Mul(xMat4x3* o, const xMat4x3* a, const xMat4x3* b);
void xMat4x3RMul3x3(xMat4x3* o, const xMat3x3* m3x3, const xMat4x3* m);
void xMat3x3Euler(xMat3x3* m, F32 yaw, F32 pitch, F32 roll);
void xMat3x3RotC(xMat3x3* m, F32 _x, F32 _y, F32 _z, F32 t);
void xMat3x3RotX(xMat3x3* m, F32 t);
void xMat3x3RotY(xMat3x3* m, F32 t);
void xMat3x3RotZ(xMat3x3* m, F32 t);
void xMat3x3MulRotC(xMat3x3* o, xMat3x3* m, F32 _x, F32 _y, F32 _z, F32 t);
void xMat3x3Normalize(xMat3x3* o, const xMat3x3* m);
void xMat3x3Tolocal(xVec3* o, const xMat3x3* m, const xVec3* v);

inline void xMat4x3Tolocal(xVec3* o, const xMat4x3* m, const xVec3* v)
{
    o->x = v->x - m->pos.x;
    o->y = v->y - m->pos.y;
    o->z = v->z - m->pos.z;

    xMat3x3Tolocal(o, m, o);
}

void xMat4x3MoveLocalRight(xMat4x3* m, F32 mag);
void xMat4x3MoveLocalAt(xMat4x3* m, F32 mag);
void xMat4x3MoveLocalUp(xMat4x3* m, F32 mag);
void xMat3x3GetEuler(const xMat3x3* m, xVec3* a);
void xMat3x3Euler(xMat3x3* m, const xVec3* ypr);
void xQuatToMat(const xQuat* q, xMat3x3* m);
void xQuatDiff(xQuat* o, const xQuat* a, const xQuat* b);
void xQuatToAxisAngle(const xQuat* q, xVec3* a, F32* t);

// No GC symbol (always inlined; weak in BfBB).
inline F32 xQuatGetAngle(const xQuat* q)
{
    if (q->s > 0.99999f)
    {
        return 0.0f;
    }
    else if (q->s < -0.99999f)
    {
        return 6.2831855f;
    }
    else
    {
        return 2.0f * xacos(q->s);
    }
}

void xQuatFromMat(xQuat* q, const xMat3x3* m);
void xQuatFromAxisAngle(xQuat* q, const xVec3* a, F32 t);
void xQuatSlerp(xQuat* q, const xQuat* a, const xQuat* b, F32 t);
F32 xMat3x3LookVec(xMat3x3* m, const xVec3* at);
F32 xMat3x3LookVec4(xMat3x3& mat, F32 at_x, F32 at_y, F32 at_z, const xVec3& hint_right);

// No symbol (inlined); name from the Ratatouille GC DWARF. The "at" row of the rotation matrix
// of the quaternion (see xQuatToMat).
inline void xQuatToLookVec(const xQuat* q, xVec3* v)
{
    F32 tx = 2.0f * q->v.x;
    F32 ty = 2.0f * q->v.y;
    F32 tz = 2.0f * q->v.z;
    F32 tsx = tx * q->s;
    F32 tsy = ty * q->s;
    F32 txx = tx * q->v.x;
    F32 txz = tz * q->v.x;
    F32 tyy = ty * q->v.y;
    F32 tyz = tz * q->v.y;

    v->x = txz - tsy;
    v->y = tyz + tsx;
    v->z = (1.0f - txx) - tyy;
}

inline void xMat3x3LookAt(xMat3x3* m, const xVec3* pos, const xVec3* at)
{
    xVec3 v;

    xVec3Sub(&v, at, pos);
    xMat3x3LookVec(m, &v);
}
void xBoxInitBoundOBB(xBox* o, const xBox* b, const xMat4x3* m);
void xBoxFromCone(xBox& box, const xVec3& center, const xVec3& dir, F32 dist, F32 r1, F32 r2);

// No symbol on GC (always inlined)
inline void xBoxUnion(xBox& a, const xBox& b, const xBox& c)
{
    a.upper.x = MAX(b.upper.x, c.upper.x);
    a.upper.y = MAX(b.upper.y, c.upper.y);
    a.upper.z = MAX(b.upper.z, c.upper.z);

    a.lower.x = MIN(b.lower.x, c.lower.x);
    a.lower.y = MIN(b.lower.y, c.lower.y);
    a.lower.z = MIN(b.lower.z, c.lower.z);
}

// No symbol on GC (always inlined). ext is value-initialized (xVec3()): retail copies it from an
// all-zero template in .bss, where a brace initializer would put the template in .rodata.
inline void xBoxFromCircle(xBox& box, const xVec3& center, const xVec3& dir, F32 r)
{
    xVec3 ext = xVec3();

    ext.x = r * xsqrt(1.0f - dir.x * dir.x);
    ext.y = r * xsqrt(1.0f - dir.y * dir.y);
    ext.z = r * xsqrt(1.0f - dir.z * dir.z);

    box.upper = center + ext;
    box.lower = center - ext;
}

void xMat3x3Scale(xMat3x3* m, const xVec3* s);
void xMat3x3ScaleC(xMat3x3* m, F32 x, F32 y, F32 z);
void xMat3x3RMulRotY(xMat3x3* o, const xMat3x3* m, F32 t);
void xMat3x3Transpose(xMat3x3* o, const xMat3x3* m);
void xMat3x3Mul(xMat3x3* o, const xMat3x3* a, const xMat3x3* b);
void xMat3x3Transpose(xMat3x3* o, const xMat3x3* m);
void xMat3x3SMul(xMat3x3*, const xMat3x3*, F32);
void xBoxFromLine(xBox& box, const xLine3& line);
void xBoxFromRay(xBox& box, const xRay3& ray);
S32 xPointInBox(const xBox* b, const xVec3* p);
void xMat3x3LMulVec(xVec3* o, const xMat3x3* m, const xVec3* v);
U8 xVec3Rotate(xMat3x3* o, const xVec3* a, const xVec3* b, F32 l);

void xQuatMul(xQuat* o, const xQuat* a, const xQuat* b);
F32 xQuatNormalize(xQuat* o, const xQuat* q);

// No GC symbol (always inlined); signature from the Ratatouille GC DWARF (xMath3.h).
inline U32 xQuatEquals(const xQuat* a, const xQuat* b)
{
    return a->s == b->s && xVec3Equals(&a->v, &b->v);
}

inline void xQuatFlip(xQuat* o, const xQuat* q)
{
    o->s = -q->s;
    xVec3Inv(&o->v, &q->v);
}

inline void xQuatSMul(xQuat* q, const xQuat* a, F32 t)
{
    q->s = a->s * t;
    xVec3SMul(&q->v, &a->v, t);
}

inline void xQuatAdd(xQuat* q, const xQuat* a, const xQuat* b)
{
    q->s = a->s + b->s;
    xVec3Add(&q->v, &a->v, &b->v);
}

inline F32 xQuatDot(const xQuat* a, const xQuat* b)
{
    return xVec3Dot(&a->v, &b->v) + a->s * b->s;
}

// No GC symbol (always inlined); name and signature from the Ratatouille GC DWARF.
inline F32 xQuatGetAngleDiff(const xQuat* q0, const xQuat* q1)
{
    F32 s1 = q0->v.x * q1->v.x + q0->v.y * q1->v.y + q0->v.z * q1->v.z + q0->s * q1->s;
    return (s1 > -s1) ? xacos(s1) : xacos(-s1);
}

inline F32 xQuatLength2(const xQuat* q)
{
    return xQuatDot(q, q);
}

inline void xRotCopy(xRot* o, const xRot* r)
{
    o->axis.x = r->axis.x;
    o->axis.y = r->axis.y;
    o->axis.z = r->axis.z;
    o->angle = r->angle;
}

static inline void xMat3x3RMulVec(xVec3* o, const xMat3x3* m, const xVec3* v)
{
    F32 x = m->right.x * v->x + m->up.x * v->y + m->at.x * v->z;
    F32 y = m->right.y * v->x + m->up.y * v->y + m->at.y * v->z;
    F32 z = m->right.z * v->x + m->up.z * v->y + m->at.z * v->z;

    o->x = x;
    o->y = y;
    o->z = z;
}

// No symbol in TSSM (always inlined); the Ratatouille GC DWARF lists both in xMath3.h.
inline void xMat3x3MulScaleC(xMat3x3* o, const xMat3x3* m, F32 x, F32 y, F32 z)
{
    o->right.assign(m->right.x * x, m->right.y * x, m->right.z * x);
    o->up.assign(m->up.x * y, m->up.y * y, m->up.z * y);
    o->at.assign(m->at.x * z, m->at.y * z, m->at.z * z);
}

inline void xMat3x3MulScale(xMat3x3* o, const xMat3x3* m, xVec3* s)
{
    xMat3x3MulScaleC(o, m, s->x, s->y, s->z);
}

// No symbol in TSSM (always inlined); the Ratatouille GC DWARF lists it in xMath3.h.
inline void xMat4x3OrthoInv(xMat4x3* o, const xMat4x3* m)
{
    xVec3 tinv;

    xMat3x3Transpose(o, m);
    xMat3x3RMulVec(&tinv, o, &m->pos);
    xVec3Inv(&o->pos, &tinv);
}

inline void xMat4x3Toworld(xVec3* o, const xMat4x3* m, const xVec3* v)
{
    xMat3x3RMulVec(o, m, v);
    xVec3Add(o, o, &m->pos);
}

inline void xQuatIdent(xQuat* q)
{
    q->v = 0.0f;
    q->s = 1.0f;
}

inline void xMat3x3Rot(xMat3x3* m, const xVec3* a, F32 t)
{
    xMat3x3RotC(m, a->x, a->y, a->z, t);
}

inline void xMat4x3RotC(xMat4x3* m, F32 _x, F32 _y, F32 _z, F32 t)
{
    xMat3x3RotC(m, _x, _y, _z, t);
    xVec3Copy(&m->pos, &g_O3);
}

inline void xMat4x3Rot(xMat4x3* m, const xVec3* a, F32 t)
{
    xMat4x3RotC(m, a->x, a->y, a->z, t);
}

inline void xMat4x3ScaleC(xMat4x3* m, F32 x, F32 y, F32 z)
{
    xMat3x3ScaleC(m, x, y, z);
    xVec3Copy(&m->pos, &g_O3);
}

inline void xMat4x3Scale(xMat4x3* m, const xVec3* s)
{
    xMat4x3ScaleC(m, s->x, s->y, s->z);
}

#endif
