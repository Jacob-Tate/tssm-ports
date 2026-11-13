#include "xCollideFast.h"

U32 xRayHitsSphereFast(const xRay3* r, const xSphere* s)
{
    xVec3 length;
    xVec3Sub(&length, &r->origin, &s->center);
    F32 dVar3 = xVec3Dot(&length, &length) - SQR(s->r);
    if (dVar3 <= 0.0f)
    {
        return 1;
    }

    if (r->flags & (1 << 11) && dVar3 > r->max_t * (2.0f * s->r + r->max_t))
    {
        return 0;
    }

    F32 dot = xVec3Dot(&length, &r->dir);
    return (dot >= 0.0f) ? 0 : (SQR(dot) >= dVar3);
}

U32 xRayHitsBoxFast(const xRay3* r, const xBox* b)
{
    xIsect isx;
    iBoxIsectRay(b, r, &isx);
    return isx.penned <= 0.0f || isx.contained <= 0.0f;
}
