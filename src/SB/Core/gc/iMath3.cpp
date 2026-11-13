#include "iMath3.h"

static U32 ClipBox(const xVec3* E, const xVec3* P, const xVec3* D, F32* t_in, F32* t_out);

void iMath3Init()
{
}

void iSphereIsectVec(const xSphere* s, const xVec3* v, xIsect* isx)
{
    xVec3Sub(&isx->norm, v, &s->center);

    isx->dist = xVec3Length(&isx->norm);
    isx->penned = isx->dist - s->r;
}

void iSphereIsectRay(const xSphere* s, const xRay3* r, xIsect* isx)
{
    F32 t_in;
    F32 t_out;
    U32 num;

    if (!(r->flags & 0x400))
    {
        ((xRay3*)r)->min_t = 0.0f;
    }

    if (!(r->flags & 0x800))
    {
        ((xRay3*)r)->max_t = 1.0f;
    }

    xVec3Sub(&isx->norm, &r->origin, &s->center);

    num = xMathSolveQuadratic(xVec3Dot(&r->dir, &r->dir), 2.0f * xVec3Dot(&isx->norm, &r->dir),
                              xVec3Dot(&isx->norm, &isx->norm) - s->r * s->r, &t_in, &t_out);

    if (num == 0)
    {
        isx->penned = 1.0f;
        isx->contained = 1.0f;
    }
    else if (num == 1)
    {
        if (t_in < r->min_t || t_in > r->max_t)
        {
            isx->dist = t_in;
            isx->penned = 1.0f;
            isx->contained = 1.0f;
        }
        else
        {
            isx->dist = t_in;
            isx->penned = -1.0f;
            isx->contained = 1.0f;
        }
    }
    else if (t_in < r->min_t)
    {
        if (t_out < r->min_t)
        {
            isx->dist = t_out;
            isx->penned = 1.0f;
            isx->contained = 1.0f;
        }
        else
        {
            isx->dist = t_out;
            isx->penned = -1.0f;
            isx->contained = -1.0f;
        }
    }
    else if (t_in <= r->max_t)
    {
        isx->dist = t_in;
        isx->penned = -1.0f;
        isx->contained = 1.0f;
    }
    else
    {
        isx->dist = t_in;
        isx->penned = 1.0f;
        isx->contained = 1.0f;
    }
}

void iSphereIsectSphere(const xSphere* s, const xSphere* p, xIsect* isx)
{
    xVec3Sub(&isx->norm, &p->center, &s->center);

    isx->dist = xVec3Length(&isx->norm);
    isx->penned = isx->dist - p->r - s->r;
    isx->contained = isx->dist - s->r;
}

void iSphereInitBoundVec(xSphere* s, const xVec3* v)
{
    xVec3Copy(&s->center, v);

    s->r = 0.00001f;
}

void iSphereInitBoundOBB(xSphere* s, const xBox* b, const xMat4x3* m)
{
    xVec3 r;

    xVec3Add(&s->center, &b->lower, &b->upper);
    xVec3SMulBy(&s->center, 0.5f);

    xVec3Sub(&r, &b->upper, &s->center);

    xMat4x3Toworld(&s->center, m, &s->center);

    s->r = xsqrt(r.x * r.x * xVec3Length2(&m->right) + r.y * r.y * xVec3Length2(&m->up) +
                 r.z * r.z * xVec3Length2(&m->at));
}

void iSphereBoundVec(xSphere* o, const xSphere* s, const xVec3* v)
{
    F32 scale;
    xSphere temp;
    xSphere* tp;
    U32 usetemp;
    xIsect isx;

    usetemp = (o == s);

    iSphereIsectVec(s, v, &isx);

    if (isx.penned <= 0.0f)
    {
        if (!usetemp)
        {
            memcpy(o, s, sizeof(xSphere));
        }
    }
    else
    {
        if (usetemp)
        {
            tp = &temp;
        }
        else
        {
            tp = o;
        }

        xVec3Copy(&tp->center, &isx.norm);

        scale = (isx.dist - s->r) / (2.0f * isx.dist);

        xVec3SMul(&tp->center, &tp->center, scale);
        xVec3Add(&tp->center, &tp->center, &s->center);

        tp->r = 0.5f * (isx.dist + s->r);

        if (usetemp)
        {
            memcpy(o, tp, sizeof(xSphere));
        }
    }
}

void iSphereBoundSphere(xSphere* o, const xSphere* a, const xSphere* b)
{
    F32 scale;
    xSphere temp;
    xSphere* tp;
    U32 usetemp;
    xIsect isx;

    usetemp = (o == a);

    iSphereIsectSphere(a, b, &isx);

    if (isx.contained <= 0.0f)
    {
        if (!usetemp)
        {
            memcpy(o, a, sizeof(xSphere));
        }
    }
    else if (isx.dist + a->r < b->r)
    {
        memcpy(o, b, sizeof(xSphere));
    }
    else
    {
        if (usetemp)
        {
            tp = &temp;
        }
        else
        {
            tp = o;
        }

        xVec3Copy(&tp->center, &isx.norm);

        scale = (b->r + (isx.dist - a->r)) / (2.0f * isx.dist);

        xVec3SMul(&tp->center, &tp->center, scale);
        xVec3Add(&tp->center, &tp->center, &a->center);

        tp->r = 0.5f * (b->r + (isx.dist + a->r));

        if (usetemp)
        {
            memcpy(o, tp, sizeof(xSphere));
        }
    }
}

void iCylinderIsectVec(const xCylinder* c, const xVec3* v, xIsect* isx)
{
    F32 b = c->center.y + c->h;

    if (v->y >= c->center.y - c->h && v->y <= b &&
        xVec2Dist(c->center.x, c->center.z, v->x, v->z) <= c->r)
    {
        isx->penned = -1.0f;
    }
    else
    {
        isx->penned = 1.0f;
    }
}

void iBoxVecDist(const xBox* box, const xVec3* v, xIsect* isx)
{
    if (v->x < box->lower.x)
    {
        if (v->y < box->lower.y)
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x00000000;
                isx->flags |= 0x80000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x01000000;
                isx->flags |= 0x40000000;
            }
            else
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x02000000;
                isx->flags |= 0x80000000;
            }
        }
        else if (v->y <= box->upper.y)
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = 0.0f;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x03000000;
                isx->flags |= 0x40000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = 0.0f;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x04000000;
                isx->flags |= 0x20000000;
            }
            else
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = 0.0f;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x05000000;
                isx->flags |= 0x40000000;
            }
        }
        else
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x06000000;
                isx->flags |= 0x80000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x07000000;
                isx->flags |= 0x40000000;
            }
            else
            {
                isx->norm.x = box->lower.x - v->x;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x08000000;
                isx->flags |= 0x80000000;
            }
        }
    }
    else if (v->x <= box->upper.x)
    {
        if (v->y < box->lower.y)
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = 0.0f;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x09000000;
                isx->flags |= 0x40000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = 0.0f;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x0A000000;
                isx->flags |= 0x20000000;
            }
            else
            {
                isx->norm.x = 0.0f;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x0B000000;
                isx->flags |= 0x40000000;
            }
        }
        else if (v->y <= box->upper.y)
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = 0.0f;
                isx->norm.y = 0.0f;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x0C000000;
                isx->flags |= 0x20000000;
            }
            else if (v->z <= box->upper.z)
            {
            }
            else
            {
                isx->norm.x = 0.0f;
                isx->norm.y = 0.0f;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x0E000000;
                isx->flags |= 0x20000000;
            }
        }
        else
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = 0.0f;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x0F000000;
                isx->flags |= 0x40000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = 0.0f;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x10000000;
                isx->flags |= 0x20000000;
            }
            else
            {
                isx->norm.x = 0.0f;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x11000000;
                isx->flags |= 0x40000000;
            }
        }
    }
    else
    {
        if (v->y < box->lower.y)
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x12000000;
                isx->flags |= 0x80000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x13000000;
                isx->flags |= 0x40000000;
            }
            else
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = box->lower.y - v->y;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x14000000;
                isx->flags |= 0x80000000;
            }
        }
        else if (v->y <= box->upper.y)
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = 0.0f;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x15000000;
                isx->flags |= 0x40000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = 0.0f;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x16000000;
                isx->flags |= 0x20000000;
            }
            else
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = 0.0f;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x17000000;
                isx->flags |= 0x40000000;
            }
        }
        else
        {
            if (v->z < box->lower.z)
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = box->lower.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x18000000;
                isx->flags |= 0x80000000;
            }
            else if (v->z <= box->upper.z)
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = 0.0f;
                isx->flags = isx->flags & ~0x1F000000 | 0x19000000;
                isx->flags |= 0x40000000;
            }
            else
            {
                isx->norm.x = box->upper.x - v->x;
                isx->norm.y = box->upper.y - v->y;
                isx->norm.z = box->upper.z - v->z;
                isx->flags = isx->flags & ~0x1F000000 | 0x1A000000;
                isx->flags |= 0x80000000;
            }
        }
    }

    isx->dist = xVec3Length(&isx->norm);
}

void iBoxIsectVec(const xBox* b, const xVec3* v, xIsect* isx)
{
    if (v->x >= b->lower.x && v->x <= b->upper.x && v->y >= b->lower.y && v->y <= b->upper.y &&
        v->z >= b->lower.z && v->z <= b->upper.z)
    {
        isx->penned = -1.0f;
    }
    else
    {
        isx->penned = 1.0f;
    }
}

static inline U32 ClipPlane(F32 denom, F32 numer, F32* t_in, F32* t_out)
{
    if (denom > 0.0f)
    {
        if (numer > denom * (*t_out))
        {
            return 0;
        }

        if (numer > denom * (*t_in))
        {
            *t_in = numer / denom;
        }

        return 1;
    }
    else if (denom < 0.0f)
    {
        if (numer > denom * (*t_in))
        {
            return 0;
        }

        if (numer > denom * (*t_out))
        {
            *t_out = numer / denom;
        }

        return 1;
    }

    return (numer <= 0.0f);
}

static U32 ClipBox(const xVec3* E, const xVec3* P, const xVec3* D, F32* t_in, F32* t_out)
{
    return ClipPlane(D->x, -P->x - E->x, t_in, t_out) &&
           ClipPlane(-D->x, P->x - E->x, t_in, t_out) &&
           ClipPlane(D->y, -P->y - E->y, t_in, t_out) &&
           ClipPlane(-D->y, P->y - E->y, t_in, t_out) &&
           ClipPlane(D->z, -P->z - E->z, t_in, t_out) && ClipPlane(-D->z, P->z - E->z, t_in, t_out);
}

void iBoxIsectRay(const xBox* b, const xRay3* r, xIsect* isx)
{
    xVec3 ext;
    xVec3 org;
    F32 t_in;
    F32 t_out;

    if (!(r->flags & 0x400))
    {
        ((xRay3*)r)->min_t = 0.0f;
    }

    if (!(r->flags & 0x800))
    {
        ((xRay3*)r)->max_t = 1.0f;
    }

    t_in = -1e38f;
    t_out = 1e38f;

    xVec3Sub(&ext, &b->upper, &b->lower);
    xVec3SMulBy(&ext, 0.5f);
    xVec3Add(&org, &b->lower, &b->upper);
    xVec3SMulBy(&org, -0.5f);
    xVec3AddTo(&org, &r->origin);

    if (ClipBox(&ext, &org, &r->dir, &t_in, &t_out))
    {
        if (t_in < r->min_t)
        {
            if (t_out < r->min_t)
            {
                isx->dist = t_out;
                isx->penned = 1.0f;
                isx->contained = 1.0f;
            }
            else
            {
                isx->dist = t_out;
                isx->penned = -1.0f;
                isx->contained = -1.0f;
            }
        }
        else if (t_in <= r->max_t)
        {
            isx->dist = t_in;
            isx->penned = -1.0f;
            isx->contained = 1.0f;
        }
        else
        {
            isx->dist = t_in;
            isx->penned = 1.0f;
            isx->contained = 1.0f;
        }
    }
    else
    {
        isx->penned = 1.0f;
        isx->contained = 1.0f;
    }
}

// Classifies the sphere extent [lo, hi] on one axis against the box slab [bmin, bmax]:
// 0 = spans the slab, 1 = crosses bmin, 2 = entirely below, 3 = inside, 4 = crosses bmax,
// 5 = entirely above (code % 3 == 2 means no overlap). Always inlined; lo/hi by value and the
// bounds by reference give retail's code.
static inline U32 SphereCode(F32 lo, F32 hi, const F32& bmin, const F32& bmax)
{
    return (lo < bmin) ? ((hi < bmin) ? 2 :
                          (hi > bmax) ? 0 :
                                        1) :
                         ((lo > bmax) ? 5 :
                          (hi > bmax) ? 4 :
                                        3);
}

void iBoxIsectSphere(const xBox* box, const xSphere* p, xIsect* isx)
{
    U32 xcode;
    U32 ycode;
    U32 zcode;

    xcode = SphereCode(p->center.x - p->r, p->center.x + p->r, box->lower.x, box->upper.x);

    if (xcode % 3 == 2)
    {
        isx->penned = 1.0f;
        return;
    }

    ycode = SphereCode(p->center.y - p->r, p->center.y + p->r, box->lower.y, box->upper.y);

    if (ycode % 3 == 2)
    {
        isx->penned = 1.0f;
        return;
    }

    zcode = SphereCode(p->center.z - p->r, p->center.z + p->r, box->lower.z, box->upper.z);

    if (zcode % 3 == 2)
    {
        isx->penned = 1.0f;
        return;
    }

    iBoxIsectVec(box, &p->center, isx);

    if (isx->penned < 0.0f)
    {
        xVec3 bc;

        xVec3Add(&bc, &box->lower, &box->upper);
        xVec3SMulBy(&bc, 0.5f);
        xVec3Sub(&isx->norm, &p->center, &bc);

        isx->dist = xVec3Length(&isx->norm);
        isx->contained = -1.0f;
    }
    else
    {
        isx->flags = xcode | (ycode << 4) | (zcode << 8);

        iBoxVecDist(box, &p->center, isx);

        isx->penned = isx->dist - p->r;
        isx->contained = 1.0f;
    }
}

void iBoxInitBoundVec(xBox* b, const xVec3* v)
{
    xVec3Copy(&b->lower, v);
    xVec3Copy(&b->upper, v);
}

void iBoxBoundVec(xBox* o, const xBox* b, const xVec3* v)
{
    xVec3Init(&o->lower, MIN(v->x, b->lower.x), MIN(v->y, b->lower.y), MIN(v->z, b->lower.z));
    xVec3Init(&o->upper, MAX(v->x, b->upper.x), MAX(v->y, b->upper.y), MAX(v->z, b->upper.z));
}
