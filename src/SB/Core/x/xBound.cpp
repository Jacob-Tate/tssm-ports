#include "xBound.h"
#include "xDraw.h"

void xBoundMakeSphere(xBound& b, const xVec3& center, F32 r)
{
    b.type = XBOUND_TYPE_SPHERE;
    b.sph.center = center;
    b.sph.r = r;
    xQuickCullForSphere(&xqc_def_ctrl, &b.qcd, &b.sph);
}

void xBoundUpdate(xBound* b)
{
    if (b->type == XBOUND_TYPE_BOX)
    {
        xVec3Add(&b->box.center, &b->box.box.lower, &b->box.box.upper);
        xVec3SMul(&b->box.center, &b->box.center, 0.5f);
    }
    else if (b->type == XBOUND_TYPE_OBB)
    {
        xVec3Add(&b->box.center, &b->box.box.lower, &b->box.box.upper);
        xVec3SMul(&b->box.center, &b->box.center, 0.5f);
        xMat4x3Toworld(&b->box.center, b->mat, &b->box.center);
    }

    if (b->type != XBOUND_TYPE_NA)
    {
        xQuickCullForBound(&b->qcd, b);
    }
}

void xBoundGetBox(xBox& box, const xBound& bound)
{
    switch (bound.type)
    {
    case XBOUND_TYPE_SPHERE:
    {
        const xSphere& o = bound.sph;

        box.upper.assign(o.center.x + o.r, o.center.y + o.r, o.center.z + o.r);
        box.lower.assign(o.center.x - o.r, o.center.y - o.r, o.center.z - o.r);

        break;
    }
    case XBOUND_TYPE_BOX:
    {
        box = bound.box.box;

        break;
    }
    case XBOUND_TYPE_OBB:
    {
        xBoxInitBoundOBB(&box, &bound.box.box, bound.mat);

        break;
    }
    case XBOUND_TYPE_CYL:
    {
        const xCylinder& c = bound.cyl;

        box.upper.assign(c.center.x + c.r, c.center.y + c.h, c.center.z + c.r);
        box.lower.assign(c.center.x - c.r, c.center.y - c.h, c.center.z - c.r);

        break;
    }
    }
}

void xBoundGetSphere(xSphere& o, const xBound& bound)
{
    switch (bound.type)
    {
    case XBOUND_TYPE_SPHERE:
    {
        o = bound.sph;

        break;
    }
    case XBOUND_TYPE_BOX:
    {
        o.center = bound.box.center;
        o.r = (bound.box.box.upper - bound.box.center).length();

        break;
    }
    case XBOUND_TYPE_OBB:
    {
        const xMat4x3& mat = *bound.mat;
        xVec3 v = (bound.box.box.upper - bound.box.box.lower) * 0.5f;

        F32 r2 = SQR(v.x) * mat.right.length2() + SQR(v.y) * mat.up.length2() +
                 SQR(v.z) * mat.at.length2();

        o.r = xsqrt(r2);
        o.center = bound.box.center;

        break;
    }
    }
}

static inline U32 xSphereHitsSphere(const xSphere* a, const xSphere* b, xCollis* coll)
{
    xIsect isx;

    iSphereIsectSphere(b, a, &isx);

    if (!(isx.penned <= 0.0f))
    {
        coll->flags &= ~0x1;
        return 0;
    }

    if (isx.contained <= 0.0f)
    {
        coll->flags |= 0x10;
    }

    coll->dist = a->r + isx.penned;

    if (coll->flags & 0x1600)
    {
        if (isx.dist == 0.0f)
        {
            xVec3Copy(&coll->tohit, &g_O3);
        }
        else
        {
            F32 scale = -coll->dist / isx.dist;
            xVec3SMul(&coll->tohit, &isx.norm, scale);
        }
    }

    if (coll->flags & 0x800)
    {
        if (isx.dist == 0.0f)
        {
            xVec3Copy(&coll->depen, &g_O3);
        }
        else
        {
            F32 scale = -isx.penned / isx.dist;
            xVec3SMul(&coll->depen, &isx.norm, scale);
        }
    }

    if (coll->flags & 0x1200)
    {
        xVec3Normalize(&coll->hdng, &coll->tohit);
    }

    if (coll->flags & 0x200)
    {
        xVec3Inv(&coll->norm, &coll->hdng);
    }

    coll->flags |= 0x1;
    return 1;
}

inline U32 xBoundSphereHitsOBB(const xSphere* s, const xBox* b, const xMat4x3* m, xCollis* coll)
{
    return xSphereHitsOBB_nu(s, b, m, coll);
}

void xBoundHitsBound(const xBound* a, const xBound* b, xCollis* c)
{
    if (!xQuickCullIsects(&a->qcd, &b->qcd))
    {
        c->flags &= ~0x1;
    }
    else
    {
        switch (a->type | (b->type << k_XBOUNDTYPE_SHIFT))
        {
        case (k_XBOUNDTYPE_SPHERE | (k_XBOUNDTYPE_SPHERE << k_XBOUNDTYPE_SHIFT)):
            xSphereHitsSphere(&a->sph, &b->sph, c);
            break;
        case (k_XBOUNDTYPE_SPHERE | (k_XBOUNDTYPE_OBB << k_XBOUNDTYPE_SHIFT)):
            xBoundSphereHitsOBB(&a->sph, &b->box.box, b->mat, c);
            break;
        case (k_XBOUNDTYPE_SPHERE | (k_XBOUNDTYPE_BOX << k_XBOUNDTYPE_SHIFT)):
            xSphereHitsBox(&a->sph, &b->box.box, c);
            break;
        case (k_XBOUNDTYPE_BOX | (k_XBOUNDTYPE_SPHERE << k_XBOUNDTYPE_SHIFT)):
            xBoxHitsSphere(&a->box.box, &b->sph, c);
            break;
        case (k_XBOUNDTYPE_BOX | (k_XBOUNDTYPE_OBB << k_XBOUNDTYPE_SHIFT)):
            xBoxHitsObb(&a->box.box, &b->box.box, b->mat, c);
            break;
        case (k_XBOUNDTYPE_OBB | (k_XBOUNDTYPE_SPHERE << k_XBOUNDTYPE_SHIFT)):
            xBoundSphereHitsOBB(&b->sph, &a->box.box, a->mat, c);
            break;
        case (k_XBOUNDTYPE_OBB | (k_XBOUNDTYPE_OBB << k_XBOUNDTYPE_SHIFT)):
            xObbHitsObb(&a->box.box, a->mat, &b->box.box, b->mat, c);
            break;
        case (k_XBOUNDTYPE_OBB | (k_XBOUNDTYPE_BOX << k_XBOUNDTYPE_SHIFT)):
            xBoxHitsObb(&b->box.box, &a->box.box, a->mat, c);
            break;
        case (k_XBOUNDTYPE_SPHERE | (k_XBOUNDTYPE_CYLINDER << k_XBOUNDTYPE_SHIFT)):
            xSphereHitsVCylinder(&a->sph, &b->cyl, c);
            break;
        // Retail (GC jump table and PS2 build) routes these pairs to xCylinderHitsSphere too.
        case (k_XBOUNDTYPE_CYLINDER | (k_XBOUNDTYPE_SPHERE << k_XBOUNDTYPE_SHIFT)):
        case (k_XBOUNDTYPE_BOX | (k_XBOUNDTYPE_BOX << k_XBOUNDTYPE_SHIFT)):
        case (k_XBOUNDTYPE_BOX | (k_XBOUNDTYPE_CYLINDER << k_XBOUNDTYPE_SHIFT)):
        case (k_XBOUNDTYPE_OBB | (k_XBOUNDTYPE_CYLINDER << k_XBOUNDTYPE_SHIFT)):
            xCylinderHitsSphere(&a->cyl, &b->sph, c);
            break;
        case (k_XBOUNDTYPE_CYLINDER | (k_XBOUNDTYPE_BOX << k_XBOUNDTYPE_SHIFT)):
        case (k_XBOUNDTYPE_CYLINDER | (k_XBOUNDTYPE_CYLINDER << k_XBOUNDTYPE_SHIFT)):
        case (k_XBOUNDTYPE_CYLINDER | (k_XBOUNDTYPE_OBB << k_XBOUNDTYPE_SHIFT)):
            break;
        }
    }
}

void xBoundHitsModel(const xBound* b, const xModelInstance* m, xCollis* colls, U8 ncolls, F32 sth)
{
    switch (b->type)
    {
    case XBOUND_TYPE_SPHERE:
        xSphereHitsModel3(&b->sph, m, colls, ncolls, sth);
        break;
    case XBOUND_TYPE_BOX:
    case XBOUND_TYPE_CYL:
    case XBOUND_TYPE_OBB:
        // Only sphere bounds are tested against models.
        break;
    }
}

static void xBoundOBBIsectRay(const xBox* b, const xMat4x3* m, const xRay3* r, xIsect* isect)
{
    xRay3 xfr;
    xBox sbox;
    xVec3 scale;
    xMat4x3 mnormal;

    xVec3NormalizeMacro(&mnormal.right, &m->right, &scale.x);
    xVec3NormalizeMacro(&mnormal.up, &m->up, &scale.y);
    xVec3NormalizeMacro(&mnormal.at, &m->at, &scale.z);

    mnormal.pos = m->pos;

    sbox.upper.x = b->upper.x * scale.x;
    sbox.upper.y = b->upper.y * scale.y;
    sbox.upper.z = b->upper.z * scale.z;
    sbox.lower.x = b->lower.x * scale.x;
    sbox.lower.y = b->lower.y * scale.y;
    sbox.lower.z = b->lower.z * scale.z;

    xMat4x3Tolocal(&xfr.origin, &mnormal, &r->origin);
    xMat3x3Tolocal(&xfr.dir, &mnormal, &r->dir);

    xfr.min_t = r->min_t;
    xfr.max_t = r->max_t;
    xfr.flags = r->flags;

    iBoxIsectRay(&sbox, &xfr, isect);
}

void xRayHitsBound(const xRay3* r, const xBound* b, xCollis* c)
{
    xIsect isect = { 0 };

    if (b->type == XBOUND_TYPE_SPHERE)
    {
        iSphereIsectRay(&b->sph, r, &isect);
    }
    else if (b->type == XBOUND_TYPE_OBB)
    {
        xBoundOBBIsectRay(&b->box.box, b->mat, r, &isect);
    }
    else if (b->type == XBOUND_TYPE_BOX)
    {
        iBoxIsectRay(&b->box.box, r, &isect);
    }

    if (isect.penned <= 0.0f)
    {
        c->flags |= 0x1;
        c->dist = isect.dist;
    }
    else
    {
        c->flags &= ~0x1;
    }
}

void xSphereHitsBound(const xSphere* o, const xBound* b, xCollis* c)
{
    switch (b->type)
    {
    case XBOUND_TYPE_SPHERE:
    {
        xSphereHitsSphere(o, &b->sph, c);
        break;
    }
    case XBOUND_TYPE_OBB:
    {
        xBoundSphereHitsOBB(o, &b->box.box, b->mat, c);
        break;
    }
    case XBOUND_TYPE_BOX:
    {
        xSphereHitsBox(o, &b->box.box, c);
        break;
    }
    }
}

void xVecHitsBound(const xVec3* v, const xBound* b, xCollis* c)
{
    xIsect isect = { 0 };

    if (b->type == XBOUND_TYPE_SPHERE)
    {
        iSphereIsectVec(&b->sph, v, &isect);
    }
    else if (b->type == XBOUND_TYPE_OBB)
    {
        xVec3 lv;

        xMat4x3Tolocal(&lv, b->mat, v);
        iBoxIsectVec(&b->box.box, &lv, &isect);
    }
    else if (b->type == XBOUND_TYPE_BOX)
    {
        iBoxIsectVec(&b->box.box, v, &isect);
    }

    if (isect.penned <= 0.0f)
    {
        c->flags |= 0x1;
    }
    else
    {
        c->flags &= ~0x1;
    }
}

void xBoundDraw(const xBound* b)
{
    if (b->type == XBOUND_TYPE_SPHERE)
    {
        xDrawSphere2(&b->sph, 12);
    }
    else if (b->type == XBOUND_TYPE_OBB)
    {
        xDrawOBB(&b->box.box, b->mat);
    }
    else if (b->type == XBOUND_TYPE_BOX)
    {
        xDrawBox(&b->box.box);
    }
}

bool xBoundHitsBound(const xBound& a, const xBound& b)
{
    switch (a.type | (b.type << 3))
    {
    case (XBOUND_TYPE_SPHERE | (XBOUND_TYPE_SPHERE << 3)):
        return xSphereHitsSphere(a.sph, b.sph);
    case (XBOUND_TYPE_SPHERE | (XBOUND_TYPE_OBB << 3)):
        return xSphereHitsOBB(a.sph, b.box.box, *b.mat);
    case (XBOUND_TYPE_SPHERE | (XBOUND_TYPE_BOX << 3)):
        return xSphereHitsBox(a.sph, b.box.box);
    case (XBOUND_TYPE_BOX | (XBOUND_TYPE_SPHERE << 3)):
        return xSphereHitsBox(b.sph, a.box.box);
    case (XBOUND_TYPE_BOX | (XBOUND_TYPE_BOX << 3)):
        return a.box.box.upper.x >= b.box.box.lower.x && a.box.box.upper.y >= b.box.box.lower.y &&
               a.box.box.upper.z >= b.box.box.lower.z && a.box.box.lower.x <= b.box.box.upper.x &&
               a.box.box.lower.y <= b.box.box.upper.y && a.box.box.lower.z <= b.box.box.upper.z;
    case (XBOUND_TYPE_BOX | (XBOUND_TYPE_OBB << 3)):
        return xOBBHitsOBB(a.box.box, g_I3, b.box.box, *b.mat);
    case (XBOUND_TYPE_OBB | (XBOUND_TYPE_SPHERE << 3)):
        return xSphereHitsOBB(b.sph, a.box.box, *a.mat);
    case (XBOUND_TYPE_OBB | (XBOUND_TYPE_OBB << 3)):
        return xOBBHitsOBB(a.box.box, *a.mat, b.box.box, *b.mat);
    case (XBOUND_TYPE_OBB | (XBOUND_TYPE_BOX << 3)):
        return xOBBHitsOBB(a.box.box, *a.mat, b.box.box, g_I3);
    }

    return false;
}

bool xSphereHitsBound(const xVec3& c, F32 r, const xBound& b)
{
    switch (b.type)
    {
    case XBOUND_TYPE_SPHERE:
        return xSphereHitsSphere(c, r, b.sph.center, b.sph.r);
    case XBOUND_TYPE_BOX:
        return xSphereHitsBox(c, r, b.box.box);
    case XBOUND_TYPE_OBB:
        return xSphereHitsOBB(c, r, b.box.box, *b.mat);
    }

    return false;
}

bool xOBBHitsBound(const xBox& box, const xMat4x3& mat, const xBound& b)
{
    switch (b.type)
    {
    case XBOUND_TYPE_SPHERE:
        return xSphereHitsOBB(b.sph, box, mat);
    case XBOUND_TYPE_BOX:
        return xOBBHitsOBB(b.box.box, g_I3, box, mat);
    case XBOUND_TYPE_OBB:
        return xOBBHitsOBB(b.box.box, *b.mat, box, mat);
    }

    return false;
}

void xBoundGetCenter(const xBound* b, xVec3* center)
{
    switch (b->type)
    {
    case XBOUND_TYPE_SPHERE:
        *center = b->sph.center;
        break;
    case XBOUND_TYPE_BOX:
    case XBOUND_TYPE_OBB:
        *center = b->box.center;
        break;
    case XBOUND_TYPE_CYL:
        *center = b->cyl.center;
        break;
    }
}
