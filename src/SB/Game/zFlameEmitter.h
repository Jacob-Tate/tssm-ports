#ifndef ZFLAMEEMITTER_H
#define ZFLAMEEMITTER_H

#include <types.h>
#include <string.h>

#include "xBase.h"
#include "xDynAsset.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "zBase.h"
#include "zFire.h"

struct xBox;

// total size: 0x80
struct zFlameEmitter : xBase
{
    // total size: 0x44
    struct asset_type : xDynAsset
    {
        U32 flags; // 0x10
        xVec3 loc; // 0x14
        xVec3 dir; // 0x20
        xVec3 scale; // 0x2C
        F32 heat_random; // 0x38
        F32 damage; // 0x3C
        F32 knockback; // 0x40
    };

    const asset_type* asset; // 0x10
    xMat4x3 mat; // 0x20
    S32 flags; // 0x60
    zFire::handle* flames; // 0x64
    zFire::property_set props; // 0x68
    S32 flames_size; // 0x74
    S32 flames_allocated; // 0x78
    F32 oscil; // 0x7C

    // The member functions below have no symbol in the GC build (always inlined).
    void load(const asset_type& a)
    {
        memset(this, 0, sizeof(zFlameEmitter));
        xBaseInit(this, &a);
        baseType = eBaseTypeFlameEmitter;
        asset = &a;
        eventFunc = cb_dispatch;

        if (linkCount != 0)
        {
            link = (xLinkAsset*)(&a + 1);
        }

        xMat3x3Euler(&mat, &asset->dir);
        mat.pos = asset->loc;
        mat.pos += mat.up * (0.5f * asset->scale.y);

        props.damage = asset->damage;
        props.knockback = asset->knockback;
        flames_size = 0;
        flags = 0;
    }

    void reset()
    {
        if (asset->flags & 0x2)
        {
            show();
        }
        else
        {
            hide();
        }
    }

    void show()
    {
        if (!(flags & 0x2))
        {
            create_flames(0);
            flags |= 0x2;
        }
    }

    void hide()
    {
        if (flags & 0x2)
        {
            destroy_flames();
            flags &= ~0x2;
        }
    }

    void spawn()
    {
        if (!(flags & 0x2))
        {
            create_flames(0x20);
            flags |= 0x2;
        }
    }

    void kill()
    {
        if (flags & 0x2)
        {
            destroy_flames();
            flags &= ~0x2;
        }
    }

    void update(F32 dt)
    {
        oscil += xrmod(PI * dt);
    }

    void render()
    {
    }

    void setup()
    {
        flames = NULL;
        flames_size = 0;
        flames_allocated = -1;
        reset();
    }

    void destroy()
    {
        hide();
    }

    void destroy_flames()
    {
        if (zFire::_enabled)
        {
            zFire::handle* flame = flames;
            zFire::handle* end_flame = flame + flames_size;

            for (; flame != end_flame; ++flame)
            {
                zFire::destroy(*flame);
            }

            flames_size = 0;
        }
    }

    void dump_flames()
    {
        if (flags & 0x2)
        {
            flags &= ~0x2;
            flames_size = 0;
        }
    }

    void get_bounding_box(xBox& box) const;
    void create_flames(S32 flags);

    static void load(xBase& data, xDynAsset& asset, size_t);
    static void scene_enter();
    static void scene_exit();
    static void setup_all();
    static void update_all(F32 dt);
    static void render_all();
    static void fire_reset();
    static void cb_dispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32);
};

#endif
