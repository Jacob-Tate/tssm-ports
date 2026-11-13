#ifndef XRAND_H
#define XRAND_H

#include <types.h>

enum xRandomChoiceRule
{
    xRandomChoiceRule_ANY_EXCEPT_LAST,
    xRandomChoiceRule_ANY,
    xRandomChoiceRule_ANY_NOT_CHOSEN,
    xRandomChoiceRule_IN_ORDER,
    xRandomChoiceRule_IN_ORDER_RANDOM,
    xRandomChoiceRule_COUNT
};

U32 xrand_RandomChoice(U32& chosenMask, U8& setBits, U32 maxCount, xRandomChoiceRule rule);
F32 xrand_GenRandFloat();
U32 xrand_GenRandInt32();
void xrand_SetRandomSeed(U32 nNewSeed);
void xrand_RandomInit();

inline U32 xrand_RandomInt32()
{
    return xrand_GenRandInt32();
}

// In xRand.h, as in Ratatouille. xRand.h is not precompiled: retail zWadEnt has
// xrand_RandomInt32 open an xRand.h section that also holds xurand.
inline F32 xurand()
{
    return 2.3283064e-10f * xrand_RandomInt32();
}

// Always inlined (no symbol). These four are Ratatouille's xRand.h inlines: RandomChoice ->
// RandomInt16 -> RandomInt32 and RandomFloatRange -> RandomFloat are separate inline levels.
inline U16 xrand_RandomInt16()
{
    return (U16)xrand_RandomInt32();
}

// Returns a value in [0, nChoice).
inline U32 xrand_RandomChoice(U32 nChoice)
{
    return (nChoice * xrand_RandomInt16()) / 0x10000;
}

inline F32 xrand_RandomFloat()
{
    return xrand_GenRandFloat();
}

// Returns a value between fMin and fMax.
inline F32 xrand_RandomFloatRange(F32 fMin, F32 fMax)
{
    F32 fRandUnitFloat = xrand_RandomFloat();
    return fRandUnitFloat * fMax + (1.0f - fRandUnitFloat) * fMin;
}

#endif
