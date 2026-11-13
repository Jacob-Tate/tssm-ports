#ifndef ZFIRE_H
#define ZFIRE_H

#include <types.h>
#include "xMath2.h"
#include "xMath3.h"
#include "iSnd.h"
#include "xSpacePartitionTree.h"
#include "xScreenWarp.h"

struct xEnt;
struct zFlame;

namespace zFire
{
    struct property_set
    {
        F32 damage;
        F32 knockback;
        iSndGroupHandle sound_group;
    };

    struct handle
    {
        xSpacePartitionTree2D::entry_data* node;
    };

    struct spawn_data
    {
        U32 flags;
        xVec3 loc;
        xVec3 up;
        F32 radius;
        F32 fuel;
        F32 heat;
        F32 heat_absorb;
        xEnt* driver;
        property_set* props;
    };

    extern bool _enabled;
    extern bool frame_changed;
    extern zFlame* flames;
    extern U32 flames_size;
    extern U32 flames_used;

    void destroy(handle h);
    void emit_sphere(const spawn_data& params, const xVec3& center, F32 radius);
    handle emit(const spawn_data& params, bool* success);
    void render();
    void pre_render();
    void update(F32 dt);
    void reset();
    void setup();
    void scene_exit();
    void scene_enter();
} // namespace zFire

// total size: 0xA0
struct zFlame
{
    S32 flags;
    F32 radius;
    F32 fuel;
    F32 heat;
    xMat4x3 mat; // 0x10
    F32 heat_absorb; // 0x50
    F32 heat_nearby;
    F32 effects_heat;
    F32 alpha;
    xVec2 scale; // 0x60
    F32 smoke_emitted;
    F32 wisps_emitted;
    xScreenWarp::warper* warp; // 0x70
    const zFire::property_set* props; // 0x74; read-only (emit reloads sound_group after the check)
    zFire::handle h;
    U8 nearby_size; // 0x7C
    U8 pad1;
    U8 face_frame;
    U8 floor_frame;
    zFlame* nearby[6]; // 0x80
    iSndHandle sound; // 0x98
    S8 face_flip;
    S8 floor_flip;
    U8 pad2;
    U8 pad3;

    void refresh_nearby();
    void unlink_nearby();
    void relink_nearby(zFlame* old_addr);
    void emit_smoke(F32 dheat, F32 dt);
    bool create(const zFire::spawn_data& info);
    void refuel(F32 fuel, S32 flags);
    void update_effects(F32 dt);
    void update(F32 dt, F32 heat_decay);

    // No symbol in any TSSM build (always inlined); name and signature from the Ratatouille GC
    // DWARF. Takes what it can of dh into heat_absorb and returns the remainder.
    F32 absorb_heat(F32 dh)
    {
        if (dh >= heat_absorb)
        {
            dh -= heat_absorb;
            heat_absorb = 0.0f;
            return dh;
        }

        heat_absorb -= dh;
        return 0.0f;
    }

    // No symbol in any TSSM build (always inlined into update_flames); name and body from
    // Ratatouille's zFire.h, with TSSM's flag bits.
    bool ready_to_die() const
    {
        return (flags & 0x804) == 0x800 && effects_heat <= 0.0f;
    }
};

// Always inlined (no symbol in the GC build).
inline void zFlame::unlink_nearby()
{
    zFlame** n = nearby;
    zFlame** endn = n + nearby_size;

    for (; n != endn; ++n)
    {
        zFlame& nf = **n;
        zFlame** m = nf.nearby;

        while (*m != this)
        {
            ++m;
        }

        nf.nearby_size--;
        *m = nf.nearby[nf.nearby_size];
    }

    nearby_size = 0;
}

// Always inlined (no symbol in the GC build).
inline void zFlame::relink_nearby(zFlame* old_addr)
{
    zFlame** n = nearby;
    zFlame** endn = n + nearby_size;

    for (; n != endn; ++n)
    {
        zFlame& bro = **n;
        zFlame** m = bro.nearby;

        while (*m != old_addr)
        {
            ++m;
        }

        *m = this;
    }
}

#endif
