#ifndef XMATHINLINES_H
#define XMATHINLINES_H

#include <types.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cmath>

#include "xMath.h"
// This header is precompiled (src/SB/SBPrefix.h), so it must not pull in xDebug.h or
// xDebugTweak.h: retail parses the tweak header per unit (its tweak_callback templates have
// per-unit label numbers and stay in .rodata, which GC/2.6 does not do for precompiled ones).
// Its asserts compile out (NDEBUG).

// One Newton-Raphson step on the reciprocal square root estimate, then 1/rsqrt. The register
// variables are the operands of the asm in iRSqrtNewton (gc/iMath.h).
inline F32 xsqrt(register F32 x)
{
    register F32 c_half = 0.5f;
    register F32 c_three = 3.0f;
    register F32 rsqrt;
    register F32 nwork0;
    register F32 nwork1;

    if (x <= 0.0f || isinf(x))
    {
        return x;
    }

    iRSqrtNewton(rsqrt, nwork0, nwork1, x, c_half, c_three);

    if (rsqrt > 0.00001f)
    {
        return 1.0f / rsqrt;
    }

    return 100000.0f;
}

// No symbol on GC (always inlined); the target calls fmod directly.
inline F32 xfmod(F32 x, F32 y)
{
    return (F32)fmod((F64)x, (F64)y);
}

// Wraps an angle into [0, 2*PI)
inline F32 xrmod(F32 ang)
{
    F32 frac = 0.15915494f * ang;

    if (frac < 0.0f)
    {
        return (frac - std::ceilf(frac) + 1.0f) * 6.2831855f;
    }
    else if (frac >= 1.0f)
    {
        return (frac - std::floorf(frac)) * 6.2831855f;
    }

    return ang;
}

inline F32 xatan2(F32 y, F32 x)
{
    return xAngleClampFast(std::atan2f(y, x));
}

inline F32 xasin(F32 x)
{
    if (x > 1.0f)
    {
        x = 1.0f;
    }

    if (x < -1.0f)
    {
        x = -1.0f;
    }

    return std::asinf(x);
}

inline F32 xacos(F32 x)
{
    if (x > 1.0f)
    {
        x = 1.0f;
    }

    if (x < -1.0f)
    {
        x = -1.0f;
    }

    return std::acosf(x);
}

inline F32 xexp(F32 x)
{
    return std::expf(x);
}

inline F32 xpow(F32 x, F32 y)
{
    return (F32)pow((F64)x, (F64)y);
}

inline F32 SQ(F32 x)
{
    return x * x;
}

// No GC symbol (always inlined); BfBB has it as LERP__FfUcUc.
inline U8 LERP(F32 x, U8 y, U8 z)
{
    return (U8)(x * (z - y)) + y;
}

inline F32 LERP(F32 x, F32 y, F32 z)
{
    return (x * (z - y)) + y;
}

inline F32 EASE(F32 rhs)
{
    return rhs * ((rhs * 3.0f) - (rhs * 2.0f) * rhs);
}

// No symbols in TSSM (always inlined); BfBB has them as the weak QUB__Ff, BOWL__Ff, BOWL3__Ff and
// ARCH3__Ff. Shaping curves over [0,1]: BOWL is 1 at the ends and 0 in the middle, ARCH3 the
// reverse (cubic).
inline F32 QUB(F32 x)
{
    return x * x * x;
}

inline F32 BOWL(F32 x)
{
    return SQ(2.0f * (x - 0.5f));
}

// No symbol in TSSM (always inlined); BfBB has it as the weak ARCH__Ff (1.0f - BOWL).
inline F32 ARCH(F32 x)
{
    return 1.0f - BOWL(x);
}

inline F32 BOWL3(F32 x)
{
    return QUB(2.0f * xabs(x - 0.5f));
}

inline F32 ARCH3(F32 x)
{
    return 1.0f - BOWL3(x);
}

inline F32 SMOOTH(F32 x, F32 y, F32 z)
{
    return (z - y) * EASE(x) + y;
}

// Estimated 1/sqrt(fVal): the raw estimate, no Newton step and no range check (callers guard
// small values themselves). Name from the Ratatouille GC DWARF; fRetVal is written straight
// from fVal, both register variables.
inline F32 xinvsqrt(register F32 fVal)
{
    register F32 fRetVal;

    iRSqrtEst(fRetVal, fVal);
    return fRetVal;
}

// Estimated square root: 1 / rsqrt estimate(fVal). Retail stores fRetVal to o unconditionally,
// so for fVal <= 0 o gets fRetVal unset (port: treat as 0); the negated '<=' test lets NaN
// through, as in retail.
inline void xsqrtfast(F32& o, register F32 fVal)
{
#ifdef PLATFORM_PC
    // PLATFORM_PC: a zero length leaves fRetVal unset (the console stores whatever the
    // register held); a host compiler reads an undefined value. Start it at fVal so a zero
    // length gives 0 (the form noted above as compiling identically on the console).
    register F32 fRetVal = fVal;
#else
    register F32 fRetVal;
#endif

    if (!(fVal <= 0.0f))
    {
        iSqrtEst(fRetVal, fVal);
    }

    o = fRetVal;
}

// Fast sine for angles in [-PI, PI] (odd polynomial), as in Ratatouille's xsinfast.
inline void xsinfast(F32& fResult, F32 fAngle)
{
    F32 fASqr = fAngle * fAngle;

    fResult = 0.00761f;
    fResult *= fASqr;
    fResult -= 0.16605f;
    fResult *= fASqr;
    fResult += 1.0f;
    fResult *= fAngle;
}

#endif
