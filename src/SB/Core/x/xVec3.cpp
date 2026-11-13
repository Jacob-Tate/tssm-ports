#include "xVec3.h"

const xVec3 xVec3::m_Null = { 0.0f, 0.0f, 0.0f };
const xVec3 xVec3::m_UnitAxisX = { 1.0f, 0.0f, 0.0f };
const xVec3 xVec3::m_UnitAxisY = { 0.0f, 1.0f, 0.0f };

F32 xVec3Normalize(xVec3* o, const xVec3* v)
{
    F32 len;
    xVec3NormalizeMacro(o, v, &len);
    return len;
}

F32 xVec3NormalizeFast(xVec3* o, const xVec3* v)
{
    F32 len;
    xVec3NormalizeFastMacro(o, v, &len);
    return len;
}

// Named register locals rather than fixed fp0/fp1: when inlined, the compiler allocates and
// schedules them instead of reserving fp0/fp1 for the whole caller.
#ifdef __MWERKS__
void xVec3Copy(register xVec3* o, const register xVec3* v)
{
    register __vec2x32float__ vxy;
    register __vec2x32float__ vz;

    // clang-format off
    asm
    {
        psq_l vxy, 0(v), 0, 0
        psq_l vz, 8(v), 1, 0
        psq_st vxy, 0(o), 0, 0
        psq_st vz, 8(o), 1, 0
    }
    // clang-format on
}

asm F32 xVec3Dot(const register xVec3* vec1, const register xVec3* vec2)
{
    PSVECDotProduct(vec1, vec2)
}
#else
// Host builds: the same operations without the Gekko paired-single instructions.
void xVec3Copy(xVec3* o, const xVec3* v)
{
    o->x = v->x;
    o->y = v->y;
    o->z = v->z;
}

F32 xVec3Dot(const xVec3* vec1, const xVec3* vec2)
{
    return vec1->x * vec2->x + vec1->y * vec2->y + vec1->z * vec2->z;
}
#endif
