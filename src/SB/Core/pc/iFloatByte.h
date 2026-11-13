#ifndef IFLOATBYTE_H
#define IFLOATBYTE_H

#include <types.h>

// A float stored into a byte the way the console does it: fctiwz to a 32-bit
// integer, which saturates (0x7FFFFFFF above the range, 0x80000000 below it and
// for NaN), then stb, which keeps the low eight bits. In C++ a float to U8
// conversion out of range is undefined, and the hosts disagree: x86's cvttss2si
// gives 0x80000000 (a byte of 0) where ARM saturates (255). Values inside the
// S32 range convert as they always did; only the saturating cases change.
inline U8 iFloatToByte(F32 f)
{
    if (!(f == f) || f < -2147483648.0f)
    {
        return 0;
    }
    if (f >= 2147483648.0f)
    {
        return 0xFF;
    }
    return (U8)(S32)f;
}

#endif
