#ifndef XDECAL_H
#define XDECAL_H

#include "xMath2.h"
#include "xMath3.h"
#include "containers.h"
#include "iColor.h"
#include "xMath.h"
#include "xstransvc.h"

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

struct xDecalEmitter
{
    enum texture_mode
    {
        TM_DEFAULT,
        TM_RANDOM,
        TM_CYCLE,
        MAX_TM,
        FORCE_INT_TM = 0xffffffff
    };

    struct config
    {
        U32 flags;
        F32 life_time;
        U32 blend_src;
        U32 blend_dst;
        struct
        {
            xVec2 uv[2];
            U8 rows;
            U8 cols;
            texture_mode mode;
        } texture;
    };

    struct unit_data
    {
        U8 flags;
        U8 curve_index;
        U8 u;
        U8 v;
        F32 frac;
        F32 age;
        F32 cull_size;
        xMat4x3 mat;
    };

    struct curve_node
    {
        F32 time;
        iColor_tag color;
        F32 scale;
    };

    config cfg;
    struct
    {
        RwTexture* asset;
        U32 units;
        xVec2 size;
        xVec2 isize;
        S32 prev;
    } texture;
    static_queue<unit_data> units;
    curve_node* curve;
    U32 curve_size;
    U32 curve_index;
    F32 ilife;

    void get_render_data(const unit_data& unit, F32 scale, xColor_tag& color, xMat4x3& mat,
                         xVec2& uv0, xVec2& uv1, S32 min_alpha);
    void update(F32 dt);
    void update_frac(unit_data& unit);
    void emit(const xMat4x3& mat, const xVec3& scale, S32 texture_index);
    void set_curve(const curve_node* curve, size_t size);
    void refresh_config();
    void set_texture(const char* name);
    void set_default_config();
    void init(S32 max_size, const char*);
    S32 select_texture_unit();

    void set_texture(U32 id)
    {
        RwTexture* tex;
        if (id == 0)
        {
            tex = NULL;
        }
        else
        {
            tex = (RwTexture*)xSTFindAsset(id, NULL);
        }

        set_texture(tex);
    }

    void set_texture(RwTexture* tex)
    {
        texture.asset = tex;
        texture.prev = 0;
    }

    bool need_update() const
    {
        return !units.empty() || debug_need_update();
    }

    bool debug_need_update() const
    {
        return false;
    }

    void debug_init(const char* texture_name)
    {
    }

    void debug_update_curve()
    {
    }
};

namespace xDecal
{
    struct emit_context
    {
        U32 owner;
        S32 flags;
        S32 priority;
        S32 max_owned;
        S32 hard_max_owned;
        F32 life;
        F32 pause_time;
        F32 accel_aging;
        F32 max_dist;
        F32 lod_dist;
        xVec2 size;
        S32 blend_mode;
        RwTexture* texture;
        xVec2 uv[2];
        xResponseCurve* curve;
        void (*emitAtomicCB)(RpAtomic*, const xMat4x3*);
        void (*destAtomicCB)(RpAtomic*);
    };

    void emit_env(const emit_context& ec, const xMat4x3& mat);
    void emit(const emit_context& ec, const xMat4x3& mat, RpAtomic* model, const xMat4x3* model_mat,
              bool moving_model);
    void sphere_emit(const emit_context& ec, const xVec3& center, F32 radius, S32 check_flags);
    void emit(const emit_context& ec, const xMat4x3& mat, S32 check_flags);
    void render_all();
    void update(F32 dt);
    void reset();
    void scene_exit();
    void scene_enter();
} // namespace xDecal

void xDecalInit();
void xDecalUpdate(F32 dt);
void xDecalDestroy();
void xDecalRender();

#endif
