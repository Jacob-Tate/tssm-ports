#include "xSpline.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cmath>

static F32 sBasisUniformBspline[4][4] = {
    { -1.0f / 6.0f, 0.5f, -0.5f, 1.0f / 6.0f },
    { 0.5f, -1.0f, 0.0f, 2.0f / 3.0f },
    { -0.5f, 0.5f, 0.5f, 1.0f / 6.0f },
    { 1.0f / 6.0f, 0.0f, 0.0f, 0.0f },
};

static F32 sBasisBezier[4][4] = {
    { -1.0f, 3.0f, -3.0f, 1.0f },
    { 3.0f, -6.0f, 3.0f, 0.0f },
    { -3.0f, 3.0f, 0.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f },
};

static F32 sBasisHermite[4][4] = {
    { 2.0f, -3.0f, 0.0f, 1.0f },
    { 1.0f, -2.0f, 1.0f, 0.0f },
    { 1.0f, -1.0f, 0.0f, 0.0f },
    { -2.0f, 3.0f, 0.0f, 0.0f },
};

static F32 ArcEvalIterate(xSpline3* spl, F32 s, U32 deriv, xVec3* o, U32 iterations);
static xCoef3* CoefSeg3(xSpline3* spl, U32 seg, xCoef3* tempCoef);
static void Tridiag_Solve(F32* a, F32* b, F32* c, xVec3* d, xVec3* x, S32 n);

static inline void Interpolate_Bspline(xVec3* data, xVec3* control, F32* knots, U32 nodata)
{
    U32 i;
    F32* alpha;
    F32* beta;
    F32* gamma;
    F32 t1;
    F32 t2;
    F32 t3;
    F32 t4;
    F32 t5;

    alpha = (F32*)RwMalloc(nodata * sizeof(F32), 0);
    beta = (F32*)RwMalloc(nodata * sizeof(F32), 0);
    gamma = (F32*)RwMalloc(nodata * sizeof(F32), 0);

    alpha[0] = alpha[nodata - 1] = 0.0f;
    beta[0] = beta[nodata - 1] = 1.0f;
    gamma[0] = gamma[nodata - 1] = 0.0f;

    for (i = 1; i < nodata - 1; i++)
    {
        t1 = knots[i + 1];
        t2 = knots[i + 2];
        t3 = knots[i + 3];
        t4 = knots[i + 4];
        t5 = knots[i + 5];

        alpha[i] = (t4 - t3) * (t4 - t3) / (t4 - t1);
        beta[i] = (t3 - t1) * (t4 - t3) / (t4 - t1) + (t5 - t3) * (t3 - t2) / (t5 - t2);
        gamma[i] = (t3 - t2) * (t3 - t2) / (t5 - t2);

        alpha[i] /= t4 - t2;
        beta[i] /= t4 - t2;
        gamma[i] /= t4 - t2;
    }

    Tridiag_Solve(alpha, beta, gamma, data, control + 1, nodata);

    control[0] = control[1];
    control[nodata + 1] = control[nodata];

    RwFree(alpha);
    RwFree(beta);
    RwFree(gamma);
}

// Composite Simpson's rule over the speed polynomial |P'(u)|; retail forms every
// coefficient product in double precision.
static inline F32 ArcLength3(xCoef3* coef, F64 ustart, F64 uend)
{
    U32 i;
    F64 E;
    F64 D;
    F64 C;
    F64 B;
    F64 A;
    F64 h;
    F64 sum;
    F64 u;

    E = 9.0 * ((F64)coef->x.a[0] * coef->x.a[0] + (F64)coef->y.a[0] * coef->y.a[0] +
               (F64)coef->z.a[0] * coef->z.a[0]);
    D = 12.0 * ((F64)coef->x.a[0] * coef->x.a[1] + (F64)coef->y.a[0] * coef->y.a[1] +
                (F64)coef->z.a[0] * coef->z.a[1]);
    C = 6.0 * ((F64)coef->x.a[0] * coef->x.a[2] + (F64)coef->y.a[0] * coef->y.a[2] +
               (F64)coef->z.a[0] * coef->z.a[2]) +
        4.0 * ((F64)coef->x.a[1] * coef->x.a[1] + (F64)coef->y.a[1] * coef->y.a[1] +
               (F64)coef->z.a[1] * coef->z.a[1]);
    B = 4.0 * ((F64)coef->x.a[1] * coef->x.a[2] + (F64)coef->y.a[1] * coef->y.a[2] +
               (F64)coef->z.a[1] * coef->z.a[2]);
    A = (F64)coef->x.a[2] * coef->x.a[2] + (F64)coef->y.a[2] * coef->y.a[2] +
        (F64)coef->z.a[2] * coef->z.a[2];

    h = (uend - ustart) / 50.0;
    sum = 0.0;
    u = ustart + h;

    for (i = 2; i <= 50; i++)
    {
        if (i & 1)
        {
            sum += 2.0 * sqrt(A + u * (B + u * (C + u * (D + E * u))));
        }
        else
        {
            sum += 4.0 * sqrt(A + u * (B + u * (C + u * (D + E * u))));
        }
        u += h;
    }

    return h *
           (sum + sqrt(A + ustart * (B + ustart * (C + ustart * (D + E * ustart)))) +
            sqrt(A + uend * (B + uend * (C + uend * (D + E * uend))))) /
           3.0;
}

static inline xSpline3* AllocSpline3(xVec3* points, F32* time, U32 numpoints, U32 numalloc,
                                     U32 flags, U32 type)
{
    xSpline3* spl = (xSpline3*)xMemAlloc(gActiveHeap, sizeof(xSpline3), 0);

    if (numalloc < numpoints)
    {
        numalloc = numpoints;
    }

    spl->type = type;
    spl->flags = flags;
    spl->N = numpoints - 1;
    spl->allocN = numalloc - 1;
    spl->p12 = NULL;
    spl->bctrl = NULL;
    spl->knot = NULL;
    spl->coef = NULL;
    spl->arcSample = 0;
    spl->arcLength = NULL;

    spl->points = (xVec3*)xMemAlloc(gActiveHeap, (spl->allocN + 1) * sizeof(xVec3), 0);
    memcpy(spl->points, points, (spl->N + 1) * sizeof(xVec3));

    if (time != NULL)
    {
        spl->time = (F32*)xMemAlloc(gActiveHeap, (spl->allocN + 1) * sizeof(F32), 0);
        memcpy(spl->time, time, (spl->N + 1) * sizeof(F32));
    }
    else
    {
        spl->time = NULL;
    }

    return spl;
}

static inline void EvalCoef3(xCoef3* coef, F32 u, U32 deriv, xVec3* o)
{
    switch (deriv)
    {
    case 0:
        o->x = ((coef->x.a[0] * u + coef->x.a[1]) * u + coef->x.a[2]) * u + coef->x.a[3];
        o->y = ((coef->y.a[0] * u + coef->y.a[1]) * u + coef->y.a[2]) * u + coef->y.a[3];
        o->z = ((coef->z.a[0] * u + coef->z.a[1]) * u + coef->z.a[2]) * u + coef->z.a[3];
        break;
    case 1:
        o->x = (3.0f * coef->x.a[0] * u + 2.0f * coef->x.a[1]) * u + coef->x.a[2];
        o->y = (3.0f * coef->y.a[0] * u + 2.0f * coef->y.a[1]) * u + coef->y.a[2];
        o->z = (3.0f * coef->z.a[0] * u + 2.0f * coef->z.a[1]) * u + coef->z.a[2];
        break;
    case 2:
        o->x = 6.0f * coef->x.a[0] * u + 2.0f * coef->x.a[1];
        o->y = 6.0f * coef->y.a[0] * u + 2.0f * coef->y.a[1];
        o->z = 6.0f * coef->z.a[0] * u + 2.0f * coef->z.a[1];
        break;
    case 3:
        o->x = 6.0f * coef->x.a[0];
        o->y = 6.0f * coef->y.a[0];
        o->z = 6.0f * coef->z.a[0];
        break;
    default:
        o->x = 0.0f;
        o->y = 0.0f;
        o->z = 0.0f;
        break;
    }
}

static inline void BasisToCoef3(xCoef3* coef, F32 N[4][4], xVec3* p0, xVec3* p1, xVec3* p2,
                                xVec3* p3)
{
    U32 c;

    for (c = 0; c < 4; c++)
    {
        coef->x.a[c] = N[0][c] * p0->x + N[1][c] * p1->x + N[2][c] * p2->x + N[3][c] * p3->x;
        coef->y.a[c] = N[0][c] * p0->y + N[1][c] * p1->y + N[2][c] * p2->y + N[3][c] * p3->y;
        coef->z.a[c] = N[0][c] * p0->z + N[1][c] * p1->z + N[2][c] * p2->z + N[3][c] * p3->z;
    }
}

// Rescales a segment polynomial from the knot interval [t1, t2] to [0, 1].
static inline void CoefToUnity3(xCoef3* dest, xCoef3* coef, F32 t1, F32 t2)
{
    U32 i;
    F32 a;
    F32 b;
    F32 c;
    F32 d;
    F32* p;
    F32* o;
    F32 dt;

    p = (F32*)coef;
    o = (F32*)dest;
    dt = t2 - t1;

    for (i = 0; i < 3; i++)
    {
        a = p[0] * dt * dt * dt;
        b = 3.0f * p[0] * dt * dt * t1 + p[1] * dt * dt;
        c = 3.0f * p[0] * dt * t1 * t1 + 2.0f * p[1] * dt * t1 + p[2] * dt;
        d = p[0] * t1 * t1 * t1 + p[1] * t1 * t1 + p[2] * t1 + p[3];
        o[0] = a;
        o[1] = b;
        o[2] = c;
        o[3] = d;
        p += 4;
        o += 4;
    }
}

// Cox-de Boor recursion for the cubic B-spline basis over the knots t[0..7].
static inline void BasisBspline(F32 N[7][4], F32* t)
{
    U32 i;
    U32 k;
    U32 c;
    F32 d1;
    F32 d2;

    N[0][3] = 0.0f;
    N[1][3] = 0.0f;
    N[2][3] = 0.0f;
    N[3][3] = 1.0f;
    N[4][3] = 0.0f;
    N[5][3] = 0.0f;
    N[6][3] = 0.0f;

    for (k = 2; k <= 4; k++)
    {
        for (i = 0; i < 8 - k; i++)
        {
            F32 Ntemp[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            d1 = t[i + k - 1] - t[i];
            if (d1 != 0.0f)
            {
                d1 = 1.0f / d1;
            }

            d2 = t[i + k] - t[i + 1];
            if (d2 != 0.0f)
            {
                d2 = 1.0f / d2;
            }

            for (c = 5 - k; c <= 3; c++)
            {
                Ntemp[c - 1] += d1 * N[i][c] - d2 * N[i + 1][c];
                Ntemp[c] += -t[i] * N[i][c] * d1 + t[i + k] * N[i + 1][c] * d2;
            }

            N[i][0] = Ntemp[0];
            N[i][1] = Ntemp[1];
            N[i][2] = Ntemp[2];
            N[i][3] = Ntemp[3];
        }
    }
}

static inline F32 ClampBspline(xSpline3* spl, F32 u)
{
    if (u < 0.0f)
    {
        u = 0.0f;
    }
    if (u > spl->knot[spl->N + 3])
    {
        u = spl->knot[spl->N + 3];
    }
    return u;
}

static inline U32 SegBspline(xSpline3* spl, F32 u)
{
    U32 min;
    U32 max;
    U32 mid;

    min = 3;
    max = spl->N + 3;
    while (min + 1 != max)
    {
        mid = (max + min) / 2;
        if (spl->knot[mid] >= u)
        {
            max = mid;
        }
        else
        {
            min = mid;
        }
    }

    return min - 3;
}

// idx and the per-component loops are not in the PS2 DWARF (which unrolls them); retail does
// not unroll the j loops.
static void Tridiag_Solve(F32* a, F32* b, F32* c, xVec3* d, xVec3* x, S32 n)
{
    S32 j;
    S32 idx;
    F32 beta;
    F32* gamma;
    xVec3* delta;

    gamma = (F32*)RwMalloc(n * sizeof(F32), 0);
    delta = (xVec3*)RwMalloc(n * sizeof(xVec3), 0);

    gamma[0] = c[0] / b[0];
    for (idx = 0; idx < 3; idx++)
    {
        ((F32*)&delta[0])[idx] = ((F32*)&d[0])[idx] / b[0];
    }

    for (j = 1; j < n; j++)
    {
        beta = b[j] - a[j] * gamma[j - 1];
        gamma[j] = c[j] / beta;
        for (idx = 0; idx < 3; idx++)
        {
            ((F32*)&delta[j])[idx] =
                (((F32*)&d[j])[idx] - a[j] * ((F32*)&delta[j - 1])[idx]) / beta;
        }
    }

    for (idx = 0; idx < 3; idx++)
    {
        ((F32*)&x[n - 1])[idx] = ((F32*)&delta[n - 1])[idx];
    }

    for (j = n - 2; j >= 0; j--)
    {
        for (idx = 0; idx < 3; idx++)
        {
            ((F32*)&x[j])[idx] = ((F32*)&delta[j])[idx] - gamma[j] * ((F32*)&x[j + 1])[idx];
        }
    }

    RwFree(gamma);
    RwFree(delta);
}

static void EvalBspline3(xSpline3* spl, F32 u, U32 deriv, xVec3* o)
{
    U32 seg;
    F32 N[7][4];
    xCoef3 coef;

    u = ClampBspline(spl, u);
    seg = SegBspline(spl, u);

    BasisBspline(N, spl->knot + seg);
    BasisToCoef3(&coef, N, spl->bctrl + seg, spl->bctrl + seg + 1, spl->bctrl + seg + 2,
                 spl->bctrl + seg + 3);

    EvalCoef3(&coef, u, deriv, o);
}

static xCoef3* CoefSeg3(xSpline3* spl, U32 seg, xCoef3* tempCoef)
{
    F32 N[7][4];

    switch (spl->type)
    {
    case 1:
        return spl->coef + seg;
    case 2:
        BasisToCoef3(tempCoef, sBasisHermite, spl->points + seg, spl->p12 + seg * 2,
                     spl->p12 + seg * 2 + 1, spl->points + seg + 1);
        break;
    case 3:
        BasisToCoef3(tempCoef, sBasisBezier, spl->points + seg, spl->p12 + seg * 2,
                     spl->p12 + seg * 2 + 1, spl->points + seg + 1);
        break;
    case 4:
        BasisBspline(N, spl->knot + seg);
        BasisToCoef3(tempCoef, N, spl->bctrl + seg, spl->bctrl + seg + 1, spl->bctrl + seg + 2,
                     spl->bctrl + seg + 3);
        CoefToUnity3(tempCoef, tempCoef, spl->knot[seg + 3], spl->knot[seg + 4]);
        break;
    }

    return tempCoef;
}

void xSpline3_EvalSeg(xSpline3* spl, F32 u, U32 deriv, xVec3* o)
{
    xCoef3 tempCoef;
    F32 flr;
    U32 seg;

    if (spl->type == 4)
    {
        EvalBspline3(spl, u, deriv, o);
        return;
    }

    if (u < 0.0f)
    {
        u = 0.0f;
    }

    flr = std::floorf(u);
    seg = (U32)flr;
    if (seg >= spl->N)
    {
        u = 1.0f;
        seg = spl->N - 1;
    }
    else
    {
        u -= flr;
    }

    switch (spl->type)
    {
    case 1:
        EvalCoef3(spl->coef + seg, u, deriv, o);
        break;
    case 2:
        BasisToCoef3(&tempCoef, sBasisHermite, spl->points + seg, spl->p12 + seg * 2,
                     spl->p12 + seg * 2 + 1, spl->points + seg + 1);
        EvalCoef3(&tempCoef, u, deriv, o);
        break;
    case 3:
        BasisToCoef3(&tempCoef, sBasisBezier, spl->points + seg, spl->p12 + seg * 2,
                     spl->p12 + seg * 2 + 1, spl->points + seg + 1);
        EvalCoef3(&tempCoef, u, deriv, o);
        break;
    }
}

// coef is not in the PS2 DWARF (CoefSeg3 is inlined there); arctest also holds the segment's
// base arc length.
static F32 ArcEvalIterate(xSpline3* spl, F32 s, U32 deriv, xVec3* o, U32 iterations)
{
    xCoef3* coef;
    xCoef3 tempCoef;
    F32 umin;
    F32 umax;
    F32 smin;
    F32 smax;
    F32 utest;
    F32 arctest;
    S32 min;
    S32 max;
    S32 test;
    S32 seg;
    S32 segmul;

    min = -1;
    max = spl->arcSample * spl->N - 1;

    while (min + 1 != max)
    {
        test = (min + max) >> 1;
        if (s > spl->arcLength[test])
        {
            min = test;
        }
        else
        {
            max = test;
        }
    }

    seg = max / (S32)spl->arcSample;
    segmul = seg * (S32)spl->arcSample;
    umin = (F32)(max - segmul) / spl->arcSample;
    umax = (F32)(max + 1 - segmul) / spl->arcSample;
    smin = (max - 1 >= 0) ? spl->arcLength[max - 1] : 0.0f;
    smax = spl->arcLength[max];
    arctest = (segmul - 1 >= 0) ? spl->arcLength[segmul - 1] : 0.0f;
    coef = CoefSeg3(spl, seg, &tempCoef);

    if (s <= smin)
    {
        EvalCoef3(coef, umin, deriv, o);
        return seg + umin;
    }

    if (s >= smax)
    {
        EvalCoef3(coef, umax, deriv, o);
        return seg + umax;
    }

    s -= arctest;
    smin -= arctest;
    smax -= arctest;

    while (iterations != 0)
    {
        utest = 0.5f * (umax - umin) + umin;
        arctest = ArcLength3(coef, 0.0, utest);
        if (s > arctest)
        {
            umin = utest;
            smin = arctest;
        }
        else
        {
            umax = utest;
            smax = arctest;
        }
        iterations--;
    }

    if (smax - smin == 0.0f)
    {
        utest = umin;
    }
    else
    {
        utest = umin + (umax - umin) * (s - smin) / (smax - smin);
        if (utest < 0.0f)
        {
            utest = 0.0f;
        }
        else if (utest > 1.0f)
        {
            utest = 1.0f;
        }
    }

    EvalCoef3(coef, utest, deriv, o);
    return seg + utest;
}

F32 xSpline3_EvalArcApprox(xSpline3* spl, F32 s, U32 deriv, xVec3* o)
{
    if (spl->arcLength != NULL)
    {
        return ArcEvalIterate(spl, s, deriv, o, 0);
    }

    xSpline3_EvalSeg(spl, s, deriv, o);
    return s;
}

void xSpline3_ArcInit(xSpline3* spl, U32 sample)
{
    U32 i;
    U32 seg;
    F32 len;
    F32 arcsum;
    xCoef3 tempCoef;
    xCoef3* coef;

    len = 0.0f;
    if (sample < 1)
    {
        sample = 1;
    }

    spl->arcSample = sample;
    if (spl->arcLength != NULL)
    {
        i = spl->allocN * spl->arcSample;
    }
    else
    {
        i = 0;
    }

    if (i < spl->N * sample)
    {
        spl->arcLength =
            (F32*)xMemAlloc(gActiveHeap, spl->arcSample * spl->allocN * sizeof(F32), 0);
    }

    arcsum = 0.0f;
    for (seg = 0; seg < spl->N; seg++)
    {
        coef = CoefSeg3(spl, seg, &tempCoef);
        for (i = 0; i < sample; i++)
        {
            len = ArcLength3(coef, 0.0, (F32)(i + 1) / (F32)sample);
            spl->arcLength[seg * sample + i] = arcsum + len;
        }
        arcsum += len;
    }
}

xSpline3* xSpline3_Bezier(xVec3* points, F32* time, U32 numpoints, U32 numalloc, xVec3* p1,
                          xVec3* p2)
{
    xSpline3* spl;
    U32 i;

    spl = AllocSpline3(points, time, numpoints, numalloc, 0, 3);
    spl->p12 = (xVec3*)xMemAlloc(gActiveHeap, spl->allocN * 2 * sizeof(xVec3), 0);

    if (p1 == NULL || p2 == NULL)
    {
        xSpline3_Catmullize(spl);
    }
    else
    {
        for (i = 0; i < spl->N; i++)
        {
            spl->p12[2 * i] = p1[i];
            spl->p12[2 * i + 1] = p2[i];
        }
    }

    return spl;
}

void xSpline3_Update(xSpline3* spl)
{
    if (spl->type == 4)
    {
        Interpolate_Bspline(spl->points, spl->bctrl, spl->knot, spl->N + 1);
    }
    if (spl->arcLength != NULL)
    {
        xSpline3_ArcInit(spl, spl->arcSample);
    }
}

void xSpline3_Catmullize(xSpline3* spl)
{
    xSpline3_Update(spl);
}
