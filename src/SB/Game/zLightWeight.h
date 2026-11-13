#ifndef ZLIGHTWEIGHT_H
#define ZLIGHTWEIGHT_H

#include <types.h>

namespace z
{
    // Virtual order from the call slots in lightweight_manager (vtable 0x8..0x20); the
    // Incredibles DWARF gives z::lightweight_system<T, N> a 0x24-byte vtable (7 virtuals).
    struct lightweight_system_base
    {
        virtual void scene_setup() = 0;
        virtual void reset() = 0;
        virtual void update(F32 dt) = 0;
        virtual void render() = 0;
        virtual void post_render_2d() = 0;
        virtual void post_render_3d() = 0;
        virtual void exit() = 0;
    };

    struct lightweight_manager
    {
        static S32 system_count;
        static lightweight_system_base* system[10];

        static void setup();
        static void exit();
        static void reset();
        static void update(F32 dt);
        static void render();
        static void post_render_2d();
        static void post_render_3d();
        static void scene_setup();
    };
} // namespace z

#endif
