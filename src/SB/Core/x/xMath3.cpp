#include "xMath3.h"
#include "xMathInlines.h"
#include "iMath3.h"
#include "xIsect.h"
#include "xRand.h"

const xVec3 g_O3 = { 0, 0, 0 };
const xVec3 g_X3 = { 1, 0, 0 };
const xVec3 g_Y3 = { 0, 1, 0 };
const xVec3 g_Z3 = { 0, 0, 1 };
const xVec3 g_NX3 = { -1, 0, 0 };
const xVec3 g_NY3 = { 0, -1, 0 };
const xVec3 g_NZ3 = { 0, 0, -1 };
const xVec3 g_Onez = { 1, 1, 1 };
// Braced explicitly: xQuat's first member is an xVec3 (anonymous union) and MWCC misplaces a flat
// list ({0,0,0,1} became v=(0,1,0), s=0, a 180-degree yaw); retail holds v=(0,0,0), s=1.
const xQuat g_IQ = { { 0.0f, 0.0f, 0.0f }, 1.0f };

void xMath3Init()
{
    iMath3Init();

    g_I3.right.x = g_X3.x;
    g_I3.right.y = g_X3.y;
    g_I3.right.z = g_X3.z;
    g_I3.up.x = g_Y3.x;
    g_I3.up.y = g_Y3.y;
    g_I3.up.z = g_Y3.z;
    g_I3.at.x = g_Z3.x;
    g_I3.at.y = g_Z3.y;
    g_I3.at.z = g_Z3.z;
    g_I3.pos.x = g_O3.x;
    g_I3.pos.y = g_O3.y;
    g_I3.pos.z = g_O3.z;
}

void xLine3VecDist2(const xVec3* p1, const xVec3* p2, const xVec3* v, xIsect* isx)
{
    xVec3 ldir;
    xVec3Sub(&ldir, p2, p1);
    xVec3Sub(&isx->norm, v, p1);

    F32 ldirdotlv = xVec3Dot(&ldir, &isx->norm);
    if (ldirdotlv <= 0.0f)
    {
        isx->dist = xVec3Length2(&isx->norm);
        return;
    }

    F32 ldirlen2 = xVec3Length2(&ldir);
    if (ldirdotlv >= ldirlen2)
    {
        xVec3Sub(&isx->norm, v, p2);
        isx->dist = xVec3Length2(&isx->norm);
        return;
    }

    F32 lvlen2 = xVec3Length2(&isx->norm);
    isx->dist = lvlen2 - SQ(ldirdotlv) / ldirlen2;
}

// Closest point on triangle (V0, V1, V2) to p, as barycentric parameters along the edges
// V1 - V0 (s) and V2 - V0 (t). Based on Eberly's point-triangle distance (Magic Software).
void xTri3VecMinDist(const xVec3* V0, const xVec3* V1, const xVec3* V2, const xVec3* p, F32& s,
                     F32& t)
{
    F32 kDiffx, kDiffy, kDiffz;
    F32 edge0x, edge0y, edge0z;
    F32 edge1x, edge1y, edge1z;
    F32 fA00, fA01, fA11, fB0, fB1;
    F32 fDet;
    F32 fS, fT;
    F32 fInvDet;
    F32 fTmp0, fTmp1, fNumer, fDenom;

    kDiffx = V0->x - p->x;
    kDiffy = V0->y - p->y;
    kDiffz = V0->z - p->z;
    edge0x = V1->x - V0->x;
    edge0y = V1->y - V0->y;
    edge0z = V1->z - V0->z;
    edge1x = V2->x - V0->x;
    edge1y = V2->y - V0->y;
    edge1z = V2->z - V0->z;

    fA00 = edge0x * edge0x + edge0y * edge0y + edge0z * edge0z;
    fA01 = edge0x * edge1x + edge0y * edge1y + edge0z * edge1z;
    fA11 = edge1x * edge1x + edge1y * edge1y + edge1z * edge1z;
    fB0 = kDiffx * edge0x + kDiffy * edge0y + kDiffz * edge0z;
    fB1 = kDiffx * edge1x + kDiffy * edge1y + kDiffz * edge1z;

    fDet = xabs(fA00 * fA11 - fA01 * fA01);
    fS = fA01 * fB1 - fA11 * fB0;
    fT = fA01 * fB0 - fA00 * fB1;

    if (fS + fT <= fDet)
    {
        if (fS < 0.0f)
        {
            if (fT < 0.0f)
            {
                // region 4
                if (fB0 < 0.0f)
                {
                    fT = 0.0f;
                    if (-fB0 >= fA00)
                    {
                        fS = 1.0f;
                    }
                    else
                    {
                        fS = -fB0 / fA00;
                    }
                }
                else
                {
                    fS = 0.0f;
                    if (fB1 >= 0.0f)
                    {
                        fT = 0.0f;
                    }
                    else if (-fB1 >= fA11)
                    {
                        fT = 1.0f;
                    }
                    else
                    {
                        fT = -fB1 / fA11;
                    }
                }
            }
            else
            {
                // region 3
                fS = 0.0f;
                if (fB1 >= 0.0f)
                {
                    fT = 0.0f;
                }
                else if (-fB1 >= fA11)
                {
                    fT = 1.0f;
                }
                else
                {
                    fT = -fB1 / fA11;
                }
            }
        }
        else if (fT < 0.0f)
        {
            // region 5
            fT = 0.0f;
            if (fB0 >= 0.0f)
            {
                fS = 0.0f;
            }
            else if (-fB0 >= fA00)
            {
                fS = 1.0f;
            }
            else
            {
                fS = -fB0 / fA00;
            }
        }
        else
        {
            // region 0
            fInvDet = 1.0f / fDet;
            fS *= fInvDet;
            fT *= fInvDet;
        }
    }
    else
    {
        if (fS < 0.0f)
        {
            // region 2
            fTmp0 = fA01 + fB0;
            fTmp1 = fA11 + fB1;
            if (fTmp1 > fTmp0)
            {
                fNumer = fTmp1 - fTmp0;
                fDenom = fA00 - 2.0f * fA01 + fA11;
                if (fNumer >= fDenom)
                {
                    fS = 1.0f;
                    fT = 0.0f;
                }
                else
                {
                    fS = fNumer / fDenom;
                    fT = 1.0f - fS;
                }
            }
            else
            {
                fS = 0.0f;
                if (fTmp1 <= 0.0f)
                {
                    fT = 1.0f;
                }
                else if (fB1 >= 0.0f)
                {
                    fT = 0.0f;
                }
                else
                {
                    fT = -fB1 / fA11;
                }
            }
        }
        else if (fT < 0.0f)
        {
            // region 6
            fTmp0 = fA01 + fB1;
            fTmp1 = fA00 + fB0;
            if (fTmp1 > fTmp0)
            {
                fNumer = fTmp1 - fTmp0;
                fDenom = fA00 - 2.0f * fA01 + fA11;
                if (fNumer >= fDenom)
                {
                    fT = 1.0f;
                    fS = 0.0f;
                }
                else
                {
                    fT = fNumer / fDenom;
                    fS = 1.0f - fT;
                }
            }
            else
            {
                fT = 0.0f;
                if (fTmp1 <= 0.0f)
                {
                    fS = 1.0f;
                }
                else if (fB0 >= 0.0f)
                {
                    fS = 0.0f;
                }
                else
                {
                    fS = -fB0 / fA00;
                }
            }
        }
        else
        {
            // region 1
            fNumer = fA11 + fB1 - fA01 - fB0;
            if (fNumer <= 0.0f)
            {
                fS = 0.0f;
                fT = 1.0f;
            }
            else
            {
                fDenom = fA00 - 2.0f * fA01 + fA11;
                if (fNumer >= fDenom)
                {
                    fS = 1.0f;
                    fT = 0.0f;
                }
                else
                {
                    fS = fNumer / fDenom;
                    fT = 1.0f - fS;
                }
            }
        }
    }

    s = fS;
    t = fT;
}

S32 xPointInBox(const xBox* b, const xVec3* p)
{
    return (char)((p->x >= b->lower.x) && (p->x <= b->upper.x) && (p->y >= b->lower.y) &&
                  (p->y <= b->upper.y) && (p->z >= b->lower.z) && (p->z <= b->upper.z));
}

// The PS2 line table has only three statement lines for the extents, but the GC code is exactly
// this per-axis accumulation (same as BfBB).
void xBoxInitBoundOBB(xBox* o, const xBox* b, const xMat4x3* m)
{
    xVec3 boxcent;
    F32 xmax;
    F32 ymax;
    F32 zmax;

    boxcent.x = 0.5f * (b->lower.x + b->upper.x);
    boxcent.y = 0.5f * (b->lower.y + b->upper.y);
    boxcent.z = 0.5f * (b->lower.z + b->upper.z);

    xmax = xabs(m->right.x * (b->upper.x - boxcent.x));
    ymax = xabs(m->right.y * (b->upper.x - boxcent.x));
    zmax = xabs(m->right.z * (b->upper.x - boxcent.x));

    xmax += xabs(m->up.x * (b->upper.y - boxcent.y));
    ymax += xabs(m->up.y * (b->upper.y - boxcent.y));
    zmax += xabs(m->up.z * (b->upper.y - boxcent.y));

    xmax += xabs(m->at.x * (b->upper.z - boxcent.z));
    ymax += xabs(m->at.y * (b->upper.z - boxcent.z));
    zmax += xabs(m->at.z * (b->upper.z - boxcent.z));

    xMat4x3Toworld(&boxcent, m, &boxcent);

    o->lower.x = boxcent.x - xmax;
    o->lower.y = boxcent.y - ymax;
    o->lower.z = boxcent.z - zmax;
    o->upper.x = boxcent.x + xmax;
    o->upper.y = boxcent.y + ymax;
    o->upper.z = boxcent.z + zmax;
}

void xBoxInitBoundCapsule(xBox* b, const xCapsule* c)
{
    if (c->start.x < c->end.x)
    {
        b->lower.x = c->start.x - c->r;
        b->upper.x = c->end.x + c->r;
    }
    else
    {
        b->lower.x = c->end.x - c->r;
        b->upper.x = c->start.x + c->r;
    }

    if (c->start.y < c->end.y)
    {
        b->lower.y = c->start.y - c->r;
        b->upper.y = c->end.y + c->r;
    }
    else
    {
        b->lower.y = c->end.y - c->r;
        b->upper.y = c->start.y + c->r;
    }

    if (c->start.z < c->end.z)
    {
        b->lower.z = c->start.z - c->r;
        b->upper.z = c->end.z + c->r;
    }
    else
    {
        b->lower.z = c->end.z - c->r;
        b->upper.z = c->start.z + c->r;
    }
}

void xBoxFromCone(xBox& box, const xVec3& center, const xVec3& dir, F32 dist, F32 r1, F32 r2)
{
    xBox temp;
    xBoxFromCircle(box, center, dir, r1);
    xBoxFromCircle(temp, center + dir * dist, dir, r2);
    xBoxUnion(box, box, temp);
}

void xMat3x3Normalize(xMat3x3* o, const xMat3x3* m)
{
    xVec3Normalize((xVec3*)o, (xVec3*)m);
    xVec3Normalize(&o->up, &m->up);
    xVec3Normalize(&o->at, &m->at);
}

/* xMat3x3GetEuler (xMat3x3 const *, xVec3 *) */
void xMat3x3GetEuler(const xMat3x3* m, xVec3* a)
{
    F32 pitch;
    F32 yaw = -xasin(m->at.y);
    F32 roll;

    if (yaw < (PI / 2))
    {
        if (yaw > -(PI / 2))
        {
            pitch = xatan2(m->at.x, m->at.z);
            roll = xatan2(m->right.y, m->up.y);
        }
        else
        {
            pitch = -xatan2(-m->up.x, m->right.x);
            roll = 0.0f;
        }
    }
    else
    {
        pitch = xatan2(-m->up.x, m->right.x);
        roll = 0.0f;
    }

    a->x = pitch;
    a->y = yaw;
    a->z = roll;
}

void xMat4x3MoveLocalRight(xMat4x3* m, F32 mag)
{
    m->pos.x += m->right.x * mag;
    m->pos.y += m->right.y * mag;
    m->pos.z += m->right.z * mag;
}

void xMat4x3MoveLocalUp(xMat4x3* m, F32 mag)
{
    m->pos.x += m->up.x * mag;
    m->pos.y += m->up.y * mag;
    m->pos.z += m->up.z * mag;
}

/* xMat4x3MoveLocalAt (xMat4x3 *, float) */
void xMat4x3MoveLocalAt(xMat4x3* m, F32 mag)
{
    m->pos.x += m->at.x * mag;
    m->pos.y += m->at.y * mag;
    m->pos.z += m->at.z * mag;
}

F32 xMat3x3LookVec(xMat3x3* m, const xVec3* at)
{
    F32 vec_len = xVec3Normalize(&m->at, at);
    xVec3Inv(&m->at, &m->at);

    if ((F32)__fabs(1.0f - m->at.y) < 0.00001f)
    {
        m->right.x = 1.0f;
        m->right.y = 0.0f;
        m->right.z = 0.0f;
        m->up.x = 0.0f;
        m->up.y = 0.0f;
        m->up.z = 1.0f;
        m->at.x = 0.0f;
        m->at.y = -1.0f;
        m->at.z = 0.0f;
        return vec_len;
    }
    if ((F32)__fabs(1.0f + m->at.y) < 0.00001f)
    {
        m->right.x = -1.0f;
        m->right.y = 0.0f;
        m->right.z = 0.0f;
        m->up.x = 0.0f;
        m->up.y = 0.0f;
        m->up.z = -1.0f;
        m->at.x = 0.0f;
        m->at.y = 1.0f;
        m->at.z = 0.0f;
        return vec_len;
    }
    if (((F32)__fabs(at->z) < 0.00001f) && ((F32)__fabs(at->x) < 0.00001f))
    {
        m->right.x = 1.0f;
        m->right.y = 0.0f;
        m->right.z = 0.0f;
        m->up.x = 0.0f;
        m->up.y = 1.0f;
        m->up.z = 0.0f;
        m->at.x = 0.0f;
        m->at.y = 0.0f;
        m->at.z = 1.0f;
        return 0.0f;
    }
    m->right.x = m->at.z;
    m->right.y = 0.0f;
    m->right.z = -m->at.x;
    xVec3Normalize((xVec3*)m, (xVec3*)m);
    xVec3Cross(&m->up, &m->at, (xVec3*)m);
    xVec3Cross((xVec3*)m, &m->up, &m->at);
    m->flags = 0;
    return vec_len;
}

F32 xMat3x3LookVec4(xMat3x3& mat, F32 at_x, F32 at_y, F32 at_z, const xVec3& hint_right)
{
    F32 mag2 = at_x * at_x + at_y * at_y + at_z * at_z;
    F32 imag;

    // Retail's constant is 0x322BCC76 = 0.0001f squared in float (as in xHud/zShrapnel).
    if (mag2 < SQR(0.0001f))
    {
        mat = g_I3;
        return 0.0f;
    }

    mag2 = xsqrt(mag2);
    imag = 1.0f / mag2;

    mat.at.assign(at_x * imag, at_y * imag, at_z * imag);

    if ((F32)__fabs(mat.at.dot(hint_right)) > 0.99999f)
    {
        if ((F32)__fabs(mat.at.y) > 0.99999f)
        {
            mat.right.assign(0.0f, mat.at.z, -mat.at.y);
        }
        else
        {
            mat.right.assign(-mat.at.z, 0.0f, mat.at.x);
        }

        mat.right.normalize();
        mat.up = mat.right.cross(mat.at);
    }
    else
    {
        xVec3Cross(&mat.up, &mat.at, &hint_right);
        mat.up.normalize();
        xVec3Cross(&mat.right, &mat.up, &mat.at);
    }

    return mag2;
}

/* xMat3x3Euler (xMat3x3 *, xVec3 const *) */
void xMat3x3Euler(xMat3x3* m, const xVec3* ypr)
{
    xMat3x3Euler(m, ypr->x, ypr->y, ypr->z);
}

/* xMat3x3Euler (xMat3x3 *, float, float, float) */
void xMat3x3Euler(xMat3x3* m, F32 yaw, F32 pitch, F32 roll)
{
    F32 sy = isin(yaw);
    F32 cy = icos(yaw);
    F32 sp = isin(pitch);
    F32 cp = icos(pitch);
    F32 sr = isin(roll);
    F32 cr = icos(roll);

    m->right.x = (cy * cr) + (sr * (sy * sp));
    m->right.y = cp * sr;
    m->right.z = (-sy * cr) + (sr * (cy * sp));
    m->up.x = (-cy * sr) + (cr * (sy * sp));
    m->up.y = cp * cr;
    m->up.z = (sy * sr) + (cr * (cy * sp));
    m->at.x = sy * cp;
    m->at.y = -sp;
    m->at.z = cy * cp;
    m->flags = 0;
}

void xMat3x3RotC(xMat3x3* m, F32 _x, F32 _y, F32 _z, F32 t)
{
    F32 b;
    F32 a;
    F32 c;

    if (t == 0.0f)
    {
        xMat3x3Identity(m);
        return;
    }

    a = icos(t);
    b = isin(t);

    c = 1.0f - a;

    m->right.x = (c * _x * _x) + a;
    m->right.y = (b * _z) + (c * _x * _y);
    m->right.z = (-b * _y) + (c * _z * _x);

    m->up.x = (-b * _z) + (c * _x * _y);
    m->up.y = (c * _y * _y) + a;
    m->up.z = (b * _x) + (c * _y * _z);

    m->at.x = (b * _y) + (c * _z * _x);
    m->at.y = (-b * _x) + (c * _y * _z);
    m->at.z = (c * _z * _z) + a;

    m->flags = 0;
}

void xMat3x3RotX(xMat3x3* m, F32 t)
{
    F32 a = icos(t);
    F32 b = isin(t);

    xVec3Copy(&m->right, &g_X3);
    xVec3Init(&m->up, 0.0f, a, b);
    xVec3Init(&m->at, 0.0f, -b, a);
    m->flags = 0;
}

void xMat3x3RotY(xMat3x3* m, F32 t)
{
    F32 a = icos(t);
    F32 b = isin(t);

    xVec3Init((xVec3*)m, a, 0.0f, -b);
    xVec3Copy(&m->up, &g_Y3);
    xVec3Init(&m->at, b, 0.0f, a);
    m->flags = 0;
}

void xMat3x3RotZ(xMat3x3* m, F32 t)
{
    F32 a = icos(t);
    F32 b = isin(t);

    xVec3Init(&m->right, a, b, 0.0f);
    xVec3Init(&m->up, -b, a, 0.0f);
    xVec3Copy(&m->at, &g_Z3);
    m->flags = 0;
}

/* xMat3x3ScaleC (xMat3x3 *, float, float, float) */
void xMat3x3ScaleC(xMat3x3* m, F32 x, F32 y, F32 z)
{
    xVec3Init((xVec3*)m, x, 0.0f, 0.0f);
    xVec3Init(&m->up, 0.0f, y, 0.0f);
    xVec3Init(&m->at, 0.0f, 0.0f, z);
    m->flags = 0;
}

void xMat3x3RMulRotY(xMat3x3* o, const xMat3x3* m, F32 t)
{
    F32 a = icos(t);
    F32 b = isin(t);
    if (o == m)
    {
        F32 temp = (a * o->right.x) + (b * o->right.z);
        o->right.z = (a * o->right.z) - (b * o->right.x);
        o->right.x = temp;

        temp = (a * o->up.x) + (b * o->up.z);
        o->up.z = (a * o->up.z) - (b * o->up.x);
        o->up.x = temp;

        temp = (a * o->at.x) + (b * o->at.z);
        o->at.z = (a * o->at.z) - (b * o->at.x);
        o->at.x = temp;
    }
    else
    {
        o->right.x = (a * m->right.x + (b * m->right.z));
        o->right.y = m->right.y;
        o->right.z = (a * m->right.z - (b * m->right.x));

        o->up.x = (a * m->up.x + (b * m->up.z));
        o->up.y = m->up.y;
        o->up.z = (a * m->up.z - (b * m->up.x));

        o->at.x = (a * m->at.x + (b * m->at.z));
        o->at.y = m->at.y;
        o->at.z = (a * m->at.z - (b * m->at.x));

        o->flags = 0;
    }
}

void xMat3x3Transpose(xMat3x3* o, const xMat3x3* m)
{
    if (o == m)
    {
        {
            F32 temp = o->right.y;
            o->right.y = o->up.x;
            o->up.x = temp;
        }
        {
            F32 temp = o->right.z;
            o->right.z = o->at.x;
            o->at.x = temp;
        }
        {
            F32 temp = o->up.z;
            o->up.z = o->at.y;
            o->at.y = temp;
        }

        return;
    }

    o->right.x = m->right.x;
    o->right.y = m->up.x;
    o->right.z = m->at.x;

    o->up.x = m->right.y;
    o->up.y = m->up.y;
    o->up.z = m->at.y;

    o->at.x = m->right.z;
    o->at.y = m->up.z;
    o->at.z = m->at.z;

    o->flags = 0;
}

void xMat3x3Mul(xMat3x3* o, const xMat3x3* a, const xMat3x3* b)
{
    xMat3x3 temp;
    xMat3x3* tp;
    U32 usetemp;

    usetemp = (o == a || o == b);

    if (usetemp)
    {
        tp = &temp;
    }
    else
    {
        tp = o;
    }

    tp->right.x = a->right.x * b->right.x + a->right.y * b->up.x + a->right.z * b->at.x;
    tp->right.y = a->right.x * b->right.y + a->right.y * b->up.y + a->right.z * b->at.y;
    tp->right.z = a->right.x * b->right.z + a->right.y * b->up.z + a->right.z * b->at.z;

    tp->up.x = a->up.x * b->right.x + a->up.y * b->up.x + a->up.z * b->at.x;
    tp->up.y = a->up.x * b->right.y + a->up.y * b->up.y + a->up.z * b->at.y;
    tp->up.z = a->up.x * b->right.z + a->up.y * b->up.z + a->up.z * b->at.z;

    tp->at.x = a->at.x * b->right.x + a->at.y * b->up.x + a->at.z * b->at.x;
    tp->at.y = a->at.x * b->right.y + a->at.y * b->up.y + a->at.z * b->at.y;
    tp->at.z = a->at.x * b->right.z + a->at.y * b->up.z + a->at.z * b->at.z;

    tp->flags = 0;

    if (usetemp)
    {
        xMat3x3Copy(o, tp);
    }
}

void xMat3x3LMulVec(xVec3* o, const xMat3x3* m, const xVec3* v)
{
    F32 y = (m->up.x * v->x) + (m->up.y * v->y) + (m->up.z * v->z);
    F32 z = (m->at.x * v->x) + (m->at.y * v->y) + (m->at.z * v->z);

    o->x = (m->right.x * v->x) + (m->right.y * v->y) + (m->right.z * v->z);
    o->y = y;
    o->z = z;
}

void xMat3x3Tolocal(xVec3* o, const xMat3x3* m, const xVec3* v)
{
    F32 sumRt = xVec3Length2(&m->right);
    F32 sumUp = xVec3Length2(&m->up);
    F32 sumAt = xVec3Length2(&m->at);

    xMat3x3LMulVec(o, m, v);
    o->x /= sumRt;
    o->y /= sumUp;
    o->z /= sumAt;
}

void xMat4x3Rot(xMat4x3* m, const xVec3* a, F32 t, const xVec3* p)
{
    xMat4x3 temp;

    xMat3x3RotC(m, a->x, a->y, a->z, t);
    xVec3Copy(&m->pos, p);
    xMat3x3Identity(&temp);
    xVec3Inv(&temp.pos, p);
    xMat4x3Mul(m, &temp, m);
}

void xMat4x3RotY(xMat4x3* m, F32 t, const xVec3* p)
{
    xMat4x3 temp;

    xMat3x3RotY(m, t);
    xVec3Copy(&m->pos, p);
    xMat3x3Identity(&temp);
    xVec3Inv(&temp.pos, p);
    xMat4x3Mul(m, &temp, m);
}

/* xMat4x3Mul (xMat4x3 *, xMat4x3 const *, xMat4x3 const *) */
void xMat4x3Mul(xMat4x3* o, const xMat4x3* a, const xMat4x3* b)
{
    xVec3 pos;

    xMat4x3Toworld(&pos, b, &a->pos);
    xMat3x3Mul((xMat3x3*)o, (xMat3x3*)a, (xMat3x3*)b);
    xVec3Copy(&o->pos, &pos);
}

void xMat4x3RMul3x3(xMat4x3* o, const xMat3x3* m3x3, const xMat4x3* m)
{
    xMat3x3Mul(o, m3x3, m);
    if (o != m)
    {
        xVec3Copy(&o->pos, &m->pos);
    }
}

void xQuatFromMat(xQuat* q, const xMat3x3* m)
{
    F32* mp = (F32*)m;
    F32* qvp = (F32*)q;
    F32 tr = m->right.x + m->up.y + m->at.z;
    F32 root;

    if (tr > 0.0f)
    {
        root = xsqrt(1.0f + tr);
        q->s = 0.5f * root;
        root = 0.5f / root;
        q->v.x = root * (m->at.y - m->up.z);
        q->v.y = root * (m->right.z - m->at.x);
        q->v.z = root * (m->up.x - m->right.y);
    }
    else
    {
        static S32 nxt[3] = { 1, 2, 0 };

        S32 i = 0;
        if (mp[5] > mp[0])
            i = 1;
        if (mp[10] > mp[i * 5])
            i = 2;

        S32 j = nxt[i];
        S32 k = nxt[j];

        root = xsqrt(mp[i * 5] - mp[j * 5] - mp[k * 5] + 1.0f);
        if (xabs(root) < 1e-5f)
        {
            xQuatCopy(q, &g_IQ);
            return;
        }

        qvp[i] = 0.5f * root;
        root = 0.5f / root;
        q->s = root * (mp[j + k * 4] - mp[k + j * 4]);
        qvp[j] = root * (mp[i + j * 4] + mp[j + i * 4]);
        qvp[k] = root * (mp[i + k * 4] + mp[k + i * 4]);

        if (q->s < 0.0f)
        {
            xQuatFlip(q, q);
        }
    }
}

void xQuatFromAxisAngle(xQuat* q, const xVec3* a, F32 t)
{
    F32 t_2;

    if (t == 0.0f)
    {
        xQuatCopy(q, &g_IQ);
    }
    else
    {
        t_2 = isin(t * 0.5f);
        q->s = icos((t * 0.5f));
        xVec3SMul(&q->v, a, t_2);
    }
}

/* xQuatToMat (xQuat const *, xMat3x3 *) */
void xQuatToMat(const xQuat* q, xMat3x3* m)
{
    F32 tx = (2.0f * q->v.x);
    F32 ty = (2.0f * q->v.y);
    F32 tz = (2.0f * q->v.z);
    F32 tsx = tx * q->s;
    F32 tsy = ty * q->s;
    F32 tsz = tz * q->s;
    F32 txx = tx * q->v.x;
    F32 txy = ty * q->v.x;
    F32 txz = tz * q->v.x;
    F32 tyy = ty * q->v.y;
    F32 tyz = tz * q->v.y;
    F32 tzz = tz * q->v.z;

    m->right.x = (1.0f - tyy) - tzz;
    m->right.y = txy - tsz;
    m->right.z = txz + tsy;

    m->up.x = txy + tsz;
    m->up.y = (1.0f - tzz) - txx;
    m->up.z = tyz - tsx;

    m->at.x = txz - tsy;
    m->at.y = tyz + tsx;
    m->at.z = (1.0f - txx) - tyy;

    m->flags = 0;
}

void xQuatToAxisAngle(const xQuat* q, xVec3* a, F32* t)
{
    *t = 2.0f * xacos(q->s);
    xVec3Normalize(a, &q->v);
}

F32 xQuatNormalize(xQuat* o, const xQuat* q)
{
    F32 one_len, len, len2;
    len2 = xQuatLength2(q);
    if (len2 == 1.0f)
    {
        if (o != q)
        {
            xQuatCopy(o, q);
        }
        return 1.0f;
    }

    if (len2 == 0.0f)
    {
        if (o != q)
        {
            xQuatCopy(o, &g_IQ);
        }
        return 0.0f;
    }

    len = xsqrt(len2);
    one_len = 1.0f / len;
    xQuatSMul(o, q, one_len);
    return len;
}

// b2 is negated field by field (v, then s) as in BfBB; xQuatFlip (s first) would merge the dot
// product with abdot. theta/aerp/berp are not in the PS2 DWARF; the blend's unfused fmuls/fadds
// need the aerp/berp temporaries.
void xQuatSlerp(xQuat* o, const xQuat* a, const xQuat* b, F32 t)
{
    F32 asph;
    F32 bsph;
    F32 one_sintheta;
    F32 theta;
    F32 abdot;
    xQuat aerp;
    xQuat berp;
    xQuat b2;

    abdot = xQuatDot(a, b);
    if (abdot < 0.0f)
    {
        abdot = -abdot;
        b2.v.x = -b->v.x;
        b2.v.y = -b->v.y;
        b2.v.z = -b->v.z;
        b2.s = -b->s;
        b = &b2;
    }

    if (abdot >= 0.999f)
    {
        asph = 1.0f - t;
        bsph = t;
    }
    else
    {
        theta = xacos(abdot);
        one_sintheta = 1.0f / isin(theta);
        asph = one_sintheta * isin((1.0f - t) * theta);
        bsph = one_sintheta * isin(t * theta);
    }

    xQuatSMul(&aerp, a, asph);
    xQuatSMul(&berp, b, bsph);
    xQuatAdd(o, &aerp, &berp);
    xQuatNormalize(o, o);
}

// Computing all four components before storing keeps o == a / o == b (xQuatDiff) correct.
void xQuatMul(xQuat* o, const xQuat* a, const xQuat* b)
{
    F32 _s = a->s * b->s - a->v.x * b->v.x - a->v.y * b->v.y - a->v.z * b->v.z;
    F32 _x = a->s * b->v.x + a->v.x * b->s + a->v.y * b->v.z - a->v.z * b->v.y;
    F32 _y = a->s * b->v.y + a->v.y * b->s + a->v.z * b->v.x - a->v.x * b->v.z;
    F32 _z = a->s * b->v.z + a->v.z * b->s + a->v.x * b->v.y - a->v.y * b->v.x;

    o->v.x = _x;
    o->v.y = _y;
    o->v.z = _z;
    o->s = _s;
    xQuatNormalize(o, o);
}

/* xQuatDiff (xQuat *, xQuat const *, xQuat const *) */
void xQuatDiff(xQuat* o, const xQuat* a, const xQuat* b)
{
    xQuatConj(o, a);
    xQuatMul(o, o, b);
    if (o->s < 0.0f)
    {
        xQuatFlip(o, o);
    }
}

// The PS2 DWARF lists only axis and angle, but the GC code keeps the dot product in its own
// register (separate from the acos result), so it is a separate local here.
U8 xVec3Rotate(xMat3x3* o, const xVec3* a, const xVec3* b, F32 l)
{
    F32 dot = xVec3Dot(a, b);
    xVec3 axis;
    F32 angle;

    if (dot >= 0.9999f)
    {
        return 1;
    }

    xVec3Cross(&axis, a, b);
    xVec3Normalize(&axis, &axis);

    angle = xacos(dot);
    if (angle > l)
    {
        angle = l;
    }

    xMat3x3RotC(o, axis.x, axis.y, axis.z, angle);
    return 0;
}

// Dead-stripped placeholder, not recovered code: retail's .sdata2 has 4.0f and 1e-6f between
// xSphereRandomPointOnSphere's and xVec3Rotate's literals, from stripped code between the two
// (Ratatouille lists unused xConicSolver and others there). Their bodies are unknown.
static void __deadstripped_xMath3(F32* out)
{
    out[0] = 4.0f;
    out[1] = 1e-6f;
}

// Marsaglia's method: a uniform point on the unit sphere from four values in [-1, 1]. An inline
// helper (the PS2 line table has it on the caller's line); name and locals from Ratatouille's
// DWARF. The squares are named locals, as retail's register use needs.
static inline void randomPointOnSphere(xVec3& pt)
{
    F32 x0, x1, x2, x3, x0sq, x1sq, x2sq, x3sq, sumsq;

    do
    {
        x0 = 2.0f * xurand() - 1.0f;
        x1 = 2.0f * xurand() - 1.0f;
        x2 = 2.0f * xurand() - 1.0f;
        x3 = 2.0f * xurand() - 1.0f;
        x0sq = x0 * x0;
        x1sq = x1 * x1;
        x2sq = x2 * x2;
        x3sq = x3 * x3;
        sumsq = x0sq + x1sq + x2sq + x3sq;
    } while (sumsq >= 1.0f);

    pt.x = 2.0f * (x1 * x3 + x0 * x2) / sumsq;
    pt.y = 2.0f * (x2 * x3 - x0 * x1) / sumsq;
    pt.z = (x0sq + x3sq - x1sq - x2sq) / sumsq;
}

void xSphereRandomPointOnSphere(const xSphere* sphere, xVec3& pt)
{
    randomPointOnSphere(pt);
    pt *= sphere->r;
    pt += sphere->center;
}
