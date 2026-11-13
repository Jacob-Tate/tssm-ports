#ifndef XSCREENWARP_H
#define XSCREENWARP_H

#include <types.h>

#include "xMath2.h"
#include "xVec3.h"

struct RwCamera;

namespace xScreenWarp
{
    enum type_enum
    {
        TYPE_INVALID = -1,
        TYPE_WARBLE,
        TYPE_LENS,
        TYPE_RIPPLE,
        TYPE_SHOCKWAVE,
        TYPE_HEAT_SHIMMER,
        TYPE_STATIC_LENS,
        MAX_TYPE
    };

    struct warper
    {
        warper* next; // 0x0
        warper* prev; // 0x4
        S32 flags; // 0x8
        F32 life; // 0xC
        F32 iduration; // 0x10
        F32 radius; // 0x14
        F32 intensity; // 0x18
        F32 freq; // 0x1C
        type_enum type; // 0x20
        union
        {
            xVec3 loc3;
            xVec2 loc2;
            const xVec3* ploc3;
            const xVec2* ploc2;
        }; // 0x24
    };

    extern char* common_warp_names[MAX_TYPE + 1];
    extern void (*common_warp_animate[MAX_TYPE])(xVec2*, warper&, const xVec2&, F32, F32, F32, F32);

    void scene_enter();
    void scene_exit();
    void reset();
    void update(F32 dt);
    void pre_render();
    void render(RwCamera* cam);
    type_enum find_type(U32 id);
    warper* emit(const xVec3& loc, F32 radius, F32 duration, type_enum type, F32 intensity,
                 F32 freq, S32 flags);
    void modify(warper* warp, const xVec3& loc, F32 radius, F32 duration, type_enum type,
                F32 intensity, F32 freq, S32 flags);
    void remove(warper* warp);
} // namespace xScreenWarp

#endif
