#ifndef XMATH_H
#define XMATH_H

#include <types.h>

#include "iMath.h"

// Heavy Iron's own value (Ratatouille's xMath.h: "#define HUGE 1e38f"); MSL's fdlibm.h defines
// HUGE as FLT_MAX, which turned xScene's 1e38 "no hit" results into hits.
#ifdef HUGE
#undef HUGE
#endif
#define HUGE 1e38f

struct xResponseCurve
{
    struct node
    {
        F32 t;
    };

    struct inode : node
    {
        F32 value[1];
    };

    void eval_linear(F32 t, F32* value) const;
    void reset(U32 values, const void* curve, U32 nodes);

    inode* get_node(U32 index) const
    {
        return (inode*)((U8*)curve + index * (sizeof(node) + _values * sizeof(F32)));
    }

    F32 start_t() const
    {
        return curve->t;
    }

    F32 end_t() const
    {
        return get_node(_nodes - 1)->t;
    }

    void find_active_node(F32 t, U32 stride) const
    {
        const U8* it = (const U8*)curve + stride * active_node;
        while (true)
        {
            if (t < ((const inode*)it)->t)
            {
                active_node--;
                it -= stride;
            }
            else
            {
                const inode& next = *(const inode*)(it + stride);
                if (t > next.t)
                {
                    active_node++;
                    it += stride;
                }
                else
                {
                    return;
                }
            }
        }
    }

    // Private in the Ratatouille GC DWARF. It makes the class non-POD, which is why
    // 'new runtime_config[n]' (zExplosion) goes through __construct_new_array.
private:
    U32 _values;
    inode* curve;
    U32 _nodes;
    mutable U32 active_node; // updated by the const eval_linear()
};

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define xabs(x) iabs(x)
#define xeq(a, b, e) (xabs((a) - (b)) <= (e))
#define xfeq0(x) (((x) >= -1e-5f) && ((x) <= 1e-5f))

#define CLAMP(x, a, b) (MAX((a), MIN((x), (b))))

#define SQR(x) ((x) * (x))

#define xlerp(a, b, t) ((a) + (t) * ((b) - (a)))

#define ALIGN(x, a) ((x) + ((a) - 1) & ~((a) - 1))

// Override these to point to their corresponding symbols in .sdata2
// For example:
//     #undef PI
//     #undef ONEEIGHTY
//     #define PI _771_1
//     #define ONEEIGHTY _778_0
#define PI 3.1415927f
#define PI_DOUBLE 3.141592653589793
#define ONEEIGHTY 180.0f

#define DEG2RAD(x) ((PI_DOUBLE) * (x) / (ONEEIGHTY))
#define RAD2DEG(x) ((ONEEIGHTY) * (x) / (PI_DOUBLE))

#define FLOAT_MAX 1e38f
#define FLOAT_MIN -1e38f

struct xFuncPiece
{
    F32 coef[5];
    F32 end;
    S32 order;
    xFuncPiece* next;
};

F32 xlog(F32 f);

// 1 if f is negative, else 0. Used by the xAccel* sign comparisons; no symbol
// and no DWARF entry exists, so the name is a placeholder.
inline S32 xnegative(F32 f)
{
    return (f < 0.0f) ? 1 : 0;
}

void xMathInit();
void xMathExit();
F32 xatof(const char* x);
void xsrand(U32 seed);

U32 xMathSolveQuadratic(F32 a, F32 b, F32 c, F32* x1, F32* x2);
U32 xMathSolveCubic(F32 a, F32 b, F32 c, F32 d, F32* x1, F32* x2, F32* x3);
F32 xAngleClamp(F32 a);
F32 xAngleClampFast(F32 a);
F32 xDangleClamp(F32 a);
void xAccelMove(F32& x, F32& v, F32 a, F32 dt, F32 endx, F32 maxv);
F32 xAccelMoveTime(F32 dx, F32 a, F32, F32 maxv);
void xAccelMove(F32& x, F32& v, F32 a, F32 dt, F32 maxv);
void xAccelStop(F32& x, F32& v, F32 a, F32 dt);
F32 xFuncPiece_Eval(xFuncPiece* func, F32 param, xFuncPiece** iterator);
void xFuncPiece_EndPoints(xFuncPiece* func, F32 pi, F32 pf, F32 fi, F32 ff);
void xFuncPiece_ShiftPiece(xFuncPiece* shift, xFuncPiece* func, F32 newZero);
// No symbol in TSSM (always inlined); BfBB's PS2 DWARF lists it in xMath.h.
inline F32 xSCurve(F32 t, F32 softness)
{
    F32 T2 = 1.0f - softness;

    if (t < softness)
    {
        return 0.5f * t * t / (softness * T2);
    }

    if (t > T2)
    {
        t = 1.0f - t;
        return 1.0f - 0.5f * t * t / (softness * T2);
    }

    return (t - 0.5f * softness) / T2;
}

// No symbol in TSSM (always inlined); the Ratatouille GC DWARF lists it in xMath.h.
inline F32 xSCurve(F32 t)
{
    if (t <= 0.5f)
    {
        return 2.0f * t * t;
    }

    F32 a = 1.0f - t;
    return 1.0f - 2.0f * a * a;
}

// No symbol in TSSM (always inlined); BfBB has it as the weak xSCurve__FRfRfRff.
// Position p, velocity v and acceleration a of the s-curve at t.
inline void xSCurve(F32& p, F32& v, F32& a, F32 t)
{
    if (t <= 0.5f)
    {
        a = 4.0f;
        v = t * a;
        p = 0.5f * t * v;
    }
    else
    {
        F32 d = t - 1.0f;
        a = -4.0f;
        v = a * d;
        p = 0.5f * v * d + 1.0f;
    }
}

// Advances a critically damped spring by dt: offset s and velocity v, decay factor
// e = exp(-k * dt). No symbol in TSSM (always inlined into xCam::spring_interp); name and
// signature from the Ratatouille GC DWARF.
inline void xDampSpring(F32& s, F32& v, F32 dt, F32 k, F32 e)
{
    F32 c1 = s;
    F32 c2 = k * c1 + v;
    s = e * (c2 * dt + c1);
    v = c2 * e - k * s;
}

// xrmod has no symbol in TSSM (always inlined): defined in xMathInlines.h

// Defined in xMath.h (as in the PS2 DWARF) but not declared inline, so callers keep a bl and
// each user unit emits a weak instance.
template <class T> T range_limit(T v, T minv, T maxv)
{
    if (v <= minv)
    {
        return minv;
    }

    if (v >= maxv)
    {
        return maxv;
    }

    return v;
}

#endif
