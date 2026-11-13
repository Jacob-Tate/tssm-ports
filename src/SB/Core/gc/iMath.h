#ifndef IMATH_H
#define IMATH_H

#include <types.h>
#include <intrin.h>

#ifdef __MWERKS__
// The argument is parenthesized: casting only its first operand changes codegen
// (xBoxInitBoundOBB then reloads the matrix instead of reusing its loads).
#define iabs(x) (float)(__fabs((float)(x)))
#else
#define iabs(x) fabsf(x)
#endif

// Reciprocal square root primitives used by the x-layer sqrt inlines (xMathInlines.h).
// On GameCube they are the Gekko estimate instructions written as asm, so every operand
// must be a 'register' F32 variable.
// clang-format off
#ifdef __MWERKS__
// rsqrt = 1/sqrt(x): the frsqrte estimate refined by one Newton-Raphson step,
// rsqrt' = (c_three - rsqrt * rsqrt * x) * (rsqrt * c_half). nwork0/nwork1 are scratch.
// The operand roles are fixed: other arrangements, or the __frsqrte/__fnmsubs intrinsics, change
// the code of every inlined xsqrt.
#define iRSqrtNewton(rsqrt, nwork0, nwork1, x, c_half, c_three) \
    asm                                                         \
    {                                                           \
        frsqrte rsqrt, x;                                       \
        fmuls nwork0, rsqrt, rsqrt;                             \
        fmuls nwork1, rsqrt, c_half;                            \
        fnmsubs nwork0, nwork0, x, c_three;                     \
        fmuls rsqrt, nwork0, nwork1                             \
    }

// out = estimated 1/sqrt(x) (raw frsqrte, no refinement)
#define iRSqrtEst(out, x) \
    asm                   \
    {                     \
        frsqrte out, x    \
    }

// out = estimated sqrt(x) = 1 / frsqrte(x) (fres of the raw estimate)
#define iSqrtEst(out, x) \
    asm                  \
    {                    \
        frsqrte out, x;  \
        fres out, out    \
    }
#else
#include <math.h>

#define iRSqrtNewton(rsqrt, nwork0, nwork1, x, c_half, c_three) \
    {                                                           \
        rsqrt = 1.0f / sqrtf(x);                                \
        nwork0 = rsqrt * rsqrt;                                 \
        nwork1 = rsqrt * c_half;                                \
        nwork0 = c_three - nwork0 * x;                          \
        rsqrt = nwork0 * nwork1;                                \
    }
#define iRSqrtEst(out, x) ((out) = 1.0f / sqrtf(x))
#define iSqrtEst(out, x) ((out) = sqrtf(x))
#endif
// clang-format on

F32 isin(F32 f);
F32 icos(F32 f);
F32 itan(F32 f);

#endif
