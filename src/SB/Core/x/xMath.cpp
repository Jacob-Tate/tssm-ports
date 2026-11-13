#include "xMath.h"
#include "xMathInlines.h"
#include <stdlib.h>

static S32 xmath_inited;
static S32 xmath_exited;
static U32 rndseed;
S32 binom_triangle[528];

static void xBinomInit();

void xMathInit()
{
    if (!xmath_inited)
    {
        xmath_inited = 1;
        rndseed = 0;
        xBinomInit();
    }
}

void xMathExit()
{
    if (!xmath_exited)
    {
        xmath_exited = 1;
    }
}

F32 xatof(const char* x)
{
    return atof(x);
}

U32 xMathSolveQuadratic(F32 a, F32 b, F32 c, F32* x1, F32* x2)
{
    F32 d;
    F32 dx;
    F32 p;

    if (a == 0.0f)
    {
        if (b == 0.0f)
        {
            return 0;
        }

        *x1 = -c / b;
        return 1;
    }

    d = b * b - (4.0f * a * c);

    if (d < 0.0f)
    {
        return 0;
    }

    dx = 1.0f / (2.0f * a);

    *x1 = -b * dx;

    if (d == 0.0f)
    {
        return 1;
    }

    *x2 = *x1;

    p = dx * xsqrt(d);

    if (a > 0.0f)
    {
        *x1 -= p;
        *x2 += p;
    }
    else
    {
        *x1 += p;
        *x2 -= p;
    }

    return 2;
}

U32 xMathSolveCubic(F32 a, F32 b, F32 c, F32 d, F32* x1, F32* x2, F32* x3)
{
    F32 arecip;
    F32 fA;
    F32 fB;
    F32 fOffset;
    F32 fDiscr;
    F32 fHalfB;
    F32 fTemp;
    F32 fDist;
    F32 fAngle;
    F32 fCos;
    F32 fSin;

    if (a == 0.0f)
    {
        return xMathSolveQuadratic(b, c, d, x1, x2);
    }

    if (a != 1.0f)
    {
        arecip = 1.0f / a;
        b *= arecip;
        c *= arecip;
        d *= arecip;
    }

    fA = 0.33333334f * (3.0f * c - b * b);
    fB = 0.037037037f * (27.0f * d + (2.0f * (b * b * b) - 9.0f * b * c));
    fOffset = 0.33333334f * b;
    fDiscr = 0.25f * (fB * fB) + 0.037037037f * (fA * (fA * fA));
    fHalfB = 0.5f * fB;

    if (xabs(fDiscr) < 0.000001f)
    {
        fDiscr = 0.0f;
    }

    if (fDiscr > 0.0f)
    {
        fTemp = xsqrt(fDiscr);

        fDist = -fHalfB + fTemp;
        if (fDist >= 0.0f)
        {
            *x1 = xpow(fDist, 0.33333334f);
        }
        else
        {
            *x1 = -xpow(-fDist, 0.33333334f);
        }

        fDist = -fHalfB - fTemp;
        if (fDist >= 0.0f)
        {
            *x1 += xpow(fDist, 0.33333334f);
        }
        else
        {
            *x1 -= xpow(-fDist, 0.33333334f);
        }

        *x1 -= fOffset;
        return 1;
    }

    if (fDiscr < 0.0f)
    {
        fTemp = xsqrt(-0.33333334f * fA);
        fAngle = 0.33333334f * xatan2(xsqrt(-fDiscr), -fHalfB);

        fCos = icos(fAngle);
        fSin = isin(fAngle);

        *x1 = 2.0f * fTemp * fCos - fOffset;
        *x2 = -fTemp * (fCos + 1.7320508f * fSin) - fOffset;
        *x3 = -fTemp * (fCos - 1.7320508f * fSin) - fOffset;
        return 3;
    }

    if (fHalfB >= 0.0f)
    {
        fDist = -xpow(fHalfB, 0.33333334f);
    }
    else
    {
        fDist = xpow(-fHalfB, 0.33333334f);
    }

    *x1 = 2.0f * fDist - fOffset;
    *x2 = -fDist - fOffset;
    return 2;
}

F32 xAngleClamp(F32 a)
{
    F32 b = xfmod(a, 2 * PI);

    if (b < 0.0f)
    {
        b += 2 * PI;
    }

    return b;
}

F32 xAngleClampFast(F32 a)
{
    if (a < 0.0f)
    {
        a += 2 * PI;
    }
    else if (a >= 2 * PI)
    {
        a -= 2 * PI;
    }

    return a;
}

F32 xDangleClamp(F32 a)
{
    F32 b = xfmod(a, 2 * PI);

    if (b >= PI)
    {
        b -= 2 * PI;
    }
    else if (b < -PI)
    {
        b += 2 * PI;
    }

    return b;
}

// Dead-stripped placeholder, not recovered code: retail's .sdata2 has PI and -PI just before
// xDangleClamp's literals, from a stripped function between xDangleClamp and xBinomInit
// (xDangleClampFast in The Incredibles). Its body is unknown.
static void __deadstripped_xMath(F32* out)
{
    out[0] = PI;
    out[1] = -PI;
}

static void xBinomInit()
{
    S32* row0;
    S32* row1;
    S32 i;
    S32* col00;
    S32* col01;
    S32* col1;

    row0 = binom_triangle;
    row1 = row0 + 1;
    row0[0] = 1;

    for (i = 1; i < 32; i++)
    {
        row1[i] = 1;
        row1[0] = 1;

        col00 = row0;
        col01 = row0 + 1;
        for (col1 = row1 + 1; col1 < row1 + i; col1++)
        {
            *col1 = *col00++ + *col01++;
        }

        row0 = row1;
        row1 += i + 1;
    }
}

// oldv is const: GC/2.6 then reloads v for the else-if range test, as retail does.
void xAccelMove(F32& x, F32& v, F32 a, F32 dt, F32 endx, F32 maxv)
{
    F32 offset;
    F32 t1;
    F32 t2;

    offset = endx - x;
    t1 = (xabs(v) < 0.001f || xnegative(offset) != xnegative(v)) ? FLOAT_MAX : offset / v;
    t2 = xabs(v / a);

    if (t1 < t2)
    {
        a *= -1.0f;
    }

    if (offset < 0.0f)
    {
        a *= -1.0f;
    }

    const F32 oldv = v;
    F32 dv = a * dt;
    F32 newv = v + dv;
    F32 adx;
    F32 diff;
    F32 dx;

    if (xabs(newv) <= maxv)
    {
        v = newv;
        adx = 0.5f * dv * dt;
    }
    else if (xabs(v) <= maxv)
    {
        v = range_limit(newv, -maxv, maxv);
        if (oldv != v)
        {
            diff = v - oldv;
            adx = 0.5f * diff * diff / a;
        }
        else
        {
            adx = 0.0f;
        }
    }
    else if (xnegative(newv) != xnegative(dv))
    {
        v = newv;
        adx = 0.5f * dv * dt;
    }
    else
    {
        adx = 0.0f;
    }

    dx = oldv * dt + adx;

    if (t1 > t2)
    {
        if (xnegative(dx) == xnegative(offset) && xabs(dx) > xabs(offset))
        {
            dx = offset;
            v = 0.0f;
        }
    }
    else if (xnegative(dx) != xnegative(offset))
    {
        dx = offset;
        v = 0.0f;
    }

    x += dx;
}

void xAccelMove(F32& x, F32& v, F32 a, F32 dt, F32 maxv)
{
    F32 diff;
    F32 dv;

    if (xabs(v) > xabs(maxv))
    {
        if (v < 0.0f)
        {
            if (a > 0.0f)
            {
                a = -a;
            }
        }
        else if (a < 0.0f)
        {
            a = -a;
        }

        a = -a;
    }

    if (a < 0.0f)
    {
        if (maxv > 0.0f)
        {
            maxv = -maxv;
        }
    }
    else if (maxv < 0.0f)
    {
        maxv = -maxv;
    }

    diff = maxv - v;
    dv = a * dt;

    if (xabs(diff) < xabs(dv))
    {
        x += v * dt + 0.5f * diff * diff / a;
        v = maxv;
        return;
    }

    x += dt * (0.5f * a * dt) + v * dt;
    v += dv;
}

void xAccelStop(F32& x, F32& v, F32 a, F32 dt)
{
    F32 oldv = v;

    if (oldv >= -0.00001f && oldv <= 0.00001f)
    {
        return;
    }

    if (oldv < 0.0f)
    {
        if (a > 0.0f)
        {
            a = -a;
        }
    }
    else if (a < 0.0f)
    {
        a = -a;
    }

    a = -a;
    v += a * dt;

    if (xnegative(oldv) == xnegative(v))
    {
        x += a * (0.5f * dt * dt) + oldv * dt;
        return;
    }

    if (!(a >= -0.00001f && a <= 0.00001f))
    {
        x -= 0.5f * oldv * oldv / a;
    }

    v = 0.0f;
}

void xFuncPiece_EndPoints(xFuncPiece* func, F32 pi, F32 pf, F32 fi, F32 ff)
{
    F32 fdiff;
    F32 xfinv;

    func->end = pf - pi;
    xfinv = 1.0f / func->end;
    fdiff = ff - fi;

    func->order = 1;
    func->coef[0] = fi;
    func->coef[1] = fdiff * xfinv;

    xFuncPiece_ShiftPiece(func, func, -pi);
}

void xFuncPiece_ShiftPiece(xFuncPiece* shift, xFuncPiece* func, F32 newZero)
{
    S32 i;
    S32 j;
    xFuncPiece temp;

    for (i = 0; i < func->order; i++)
    {
        temp.coef[i] = 0.0f;
    }

    temp.coef[func->order] = func->coef[func->order];

    for (i = func->order - 1; i >= 0; i--)
    {
        for (j = i; j < func->order; j++)
        {
            temp.coef[j] += newZero * temp.coef[j + 1];
        }
        temp.coef[i] += func->coef[i];
    }

    shift->order = func->order;

    for (i = 0; i <= shift->order; i++)
    {
        shift->coef[i] = temp.coef[i];
    }

    shift->end = func->end - newZero;
}

void xResponseCurve::reset(U32 values, const void* curve, U32 nodes)
{
    _values = values;
    this->curve = (inode*)curve;
    _nodes = nodes;
    active_node = 0;
}

void xResponseCurve::eval_linear(F32 t, F32* value) const
{
    F32* end = value + _values;
    U32 stride = sizeof(node) + _values * sizeof(F32);

    find_active_node(t, stride);

    inode& n1 = *(inode*)((U8*)curve + active_node * stride);
    inode& n2 = *(inode*)((U8*)&n1 + stride);
    F32 dt = n2.t - n1.t;

    if (dt >= -0.00001f && dt <= 0.00001f)
    {
        F32* v1 = n1.value;
        while (value != end)
        {
            *value++ = *v1++;
        }
        return;
    }

    F32 u = (t - n1.t) / dt;
    F32* v1 = n1.value;
    F32* v2 = n2.value;
    while (value != end)
    {
        *value++ = *v1 + u * (*v2 - *v1);
        v1++;
        v2++;
    }
}

// Dead-stripped placeholder, not recovered code: retail's @stringBase0 keeps the unreferenced
// messages of a stripped "xmath module autotest" (Ratatouille's xMathTest). Its body is unknown.
static const char* __deadstripped_xMath()
{
    return "-------------------------------------------\n\0"
           "      --< xmath module autotest >--\n\0"
           "  note: testing accuracy, not performance, at this time.\n\0"
           "--> testing exp/log...\n\0"
           "(x=%e,z=%e) are equal to %d digits (x/z=%f)\n\0"
           "--> number hits per (binary) digit:\n\0"
           "equal to %d digits: %d hits\n\0"
           "--> testing sqrt...\n";
}
