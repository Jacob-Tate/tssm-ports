// xGrid.h comes before zFire.h (which pulls in xSpacePartitionTree.h): CW emits the weak template
// sections in reverse order of their definitions, and the target places the xGridCheckBound
// instances after the xSpacePartitionTree2D ones.
#include "xGrid.h"

#include "zFire.h"

#include <types.h>
#include <stdio.h>

#include "xBound.h"
#include "xCollide.h"
// retail's .rodata opens with xDebugTweak.h's seven tweak_callback brace-initializer templates
#include "xDebug.h"
#include "xDebugTweak.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xFXHighDynamicRange.h"
#include "xGlobals.h"
#include "xCam.h"
#include "xIM.h"
#include "xMemMgr.h"
#include "xParticleBatch.h"
#include "xPtankPool.h"
#include "xRand.h"
#include "xRay3.h"
#include "xScreenWarp.h"
#include "xSnd.h"
#include "xString.h"
#include "xTransient.h"
#include "xstransvc.h"
#include "xutil.h"
#include "xVec3Inlines.h"
#include "zExplosion.h"
#include "zEntSimpleObj.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zParticleCustom.h"
#include "zPlatform.h"
#include "zRenderState.h"
#include "zScene.h"

struct card_info
{
    U32 textures;
    char* format;
    S32 start_index;
    S32 rows;
    S32 cols;
    F32 bias_v0;
    F32 bias_v1;
};

struct zSmokeEmitter;

namespace zFire
{
    bool _enabled;
    bool frame_changed;
    zFlame* flames;
    U32 flames_size;
    U32 flames_used;

    const card_info short_card_info = { 1, "FireShort_4volume", 0, 8, 4, 0.0078125f, -0.001953125f };
    const card_info tall_card_info = { 1, "FireLong4Volume", 0, 8, 4, 0.0009765625f, 0.0f };
} // namespace zFire

namespace
{
    struct flame_distance_pair
    {
        F32 dist;
        zFlame* flame;
    };

    // total size: 0x40
    struct wisp_particle
    {
        xVec3 loc;
        F32 size;
        xVec3 vel;
        F32 life;
        F32 dsize;
        F32 alpha;
        F32 dalpha;
        F32 pad1;
        F32 rot;
        F32 drot;
        F32 pad2;
        F32 pad3;
    };

    struct cb_flame_sphere_check
    {
        zFlame** found;
        U32 max_found;
        const xVec3* loc;
        F32 r;
        U32 total;

        cb_flame_sphere_check(zFlame** found, U32 max_found, const xVec3& loc, F32 r)
            : found(found), max_found(max_found), loc(&loc), r(r), total(0)
        {
        }

        bool operator()(xSpacePartitionTree2D::entry_data* entry)
        {
            if (entry->type != 1)
            {
                return true;
            }

            if (total >= max_found)
            {
                return false;
            }

            zFlame* flame = (zFlame*)entry->value;
            xVec3 offset = *loc;
            offset -= flame->mat.pos;
            F32 max_dist = r + flame->radius;
            F32 dist2 = offset.length2();
            if (dist2 <= max_dist * max_dist)
            {
                found[total] = flame;
                total++;
            }

            return total < max_found;
        }
    };

    struct cb_flame_circle_check
    {
        zFlame** found;
        U32 max_found;
        F32 x;
        F32 z;
        F32 r;
        U32 total;

        cb_flame_circle_check(zFlame** found, U32 max_found, F32 x, F32 z, F32 r)
            : found(found), max_found(max_found), x(x), z(z), r(r), total(0)
        {
        }

        bool operator()(xSpacePartitionTree2D::entry_data* entry)
        {
            if (entry->type != 1)
            {
                return true;
            }

            if (total >= max_found)
            {
                return false;
            }

            zFlame* flame = (zFlame*)entry->value;
            F32 dx = x - flame->mat.pos.x;
            F32 dz = z - flame->mat.pos.z;
            F32 max_dist = r + flame->radius;
            F32 dist2 = dx * dx + dz * dz;
            if (dist2 <= max_dist * max_dist)
            {
                found[total] = flame;
                total++;
            }

            return total < max_found;
        }
    };

    RwTexture* face_texture;
    RwTexture* floor_texture;
    F32 anim_frame_time;
    S32 anim_frame;
    zFlame* im_flames[64];
    S32 im_flames_size;
    S32 last_brightness;
    S32 last_background_glow;
    zSmokeEmitter* smoke_emitter;
    RwTexture* wisp_texture;
    S32 wisp_system_id;
    S32 spawn_explosion_type;

    S32 update_wisp_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    void update_flames(F32 dt);
    void free_flame(zFlame& flame);

    inline void spread_heat(F32 dt)
    {
        F32 spread_delta = dt;
        zFlame* flame = zFire::flames;
        zFlame* end_flame = flame + zFire::flames_used;

        for (; flame != end_flame; ++flame)
        {
            if (flame->nearby_size == 0)
            {
                continue;
            }

            F32 spread = spread_delta / flame->nearby_size;
            zFlame** n = flame->nearby;
            zFlame** endn = n + flame->nearby_size;

            for (; n != endn; ++n)
            {
                (*n)->heat_nearby += spread;
            }
        }
    }
} // namespace

namespace
{
    // No TSSM symbol (always inlined); the name is Ratatouille's.
    inline zFlame* alloc_flame()
    {
        if (zFire::flames_used >= zFire::flames_size)
        {
            return NULL;
        }

        zFlame& flame = zFire::flames[zFire::flames_used];
        ++zFire::flames_used;
        return &flame;
    }

    void free_flame(zFlame& flame)
    {
        --zFire::flames_used;
        zFlame* moved = &zFire::flames[zFire::flames_used];

        if (&flame == moved)
        {
            return;
        }

        flame = *moved;
        flame.relink_nearby(moved);
        flame.h.node->value = &flame;
    }

    void update_flames(F32 dt)
    {
        F32 heat_decay = xexp(-1.0f * dt);
        zFlame* flame = zFire::flames;
        zFlame* end_flame = flame + zFire::flames_used;

        while (flame != end_flame)
        {
            flame->update(dt, heat_decay);
            flame->update_effects(dt);

            if (zFire::frame_changed)
            {
                flame->scale.x = 4.0f * flame->radius;
                flame->scale.y = 12.0f * (flame->radius * flame->effects_heat);
            }

            if (flame->ready_to_die())
            {
                xTransient::space_tree.erase(flame->h.node);

                if (flame->sound != (iSndHandle)-1)
                {
                    xSndMgrStop(flame->sound);
                }

                if (flame->warp != NULL)
                {
                    xScreenWarp::remove(flame->warp);
                }

                flame->unlink_nearby();
                free_flame(*flame);
                --end_flame;
            }
            else
            {
                ++flame;
            }
        }
    }

    S32 update_wisp_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size_rot& pool = (ptank_pool__pos_color_size_rot&)apool;
        wisp_particle* p = (wisp_particle*)mem;
        wisp_particle* end = p + count;

        while (p != end)
        {
            p->life -= dt;
            if (p->life <= 0.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt;
            p->loc.z += p->vel.z * dt;
            p->size += p->dsize * dt;
            p->alpha += p->dalpha * dt;
            p->rot += p->drot * dt;

            if (p->rot > PI)
            {
                p->rot -= 2.0f * PI;
            }

            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                pool.color->b = pool.color->g = pool.color->r = (U8)(0.5f + p->alpha);
                pool.color->a = (U8)(0.078431375f * p->alpha + 0.5f);
                *pool.pos = p->loc;
                pool.size->assign(p->size);
                *pool.rot = p->rot;
            }

            ++p;
        }

        return end - (wisp_particle*)mem;
    }
} // namespace

void zFire::scene_enter()
{
    char buffer[128];
    sprintf(buffer, short_card_info.format, short_card_info.start_index);
    _enabled = xSTFindAsset(xStrHash(buffer), NULL) != NULL;

    if (!_enabled)
    {
        return;
    }

    flames_size = 500;
    flames_used = 0;
    flames = (zFlame*)xMemAlloc(gActiveHeap, flames_size * sizeof(zFlame), 0);

    anim_frame_time = 0.0f;
    anim_frame = 0;

    face_texture = (RwTexture*)xSTFindAsset(xStrHash(tall_card_info.format), NULL);
    floor_texture = (RwTexture*)xSTFindAsset(xStrHash(short_card_info.format), NULL);

    last_brightness = last_background_glow = -1;
    wisp_texture = NULL;

    // not in the DWARF: the descriptor passed to xParticleBatchAddSystem.
    xParticleBatchSystem system;
    system.pool_type = PGT_POS_COLOR_SIZE_ROT;
    system.rs.texture = wisp_texture;
    system.rs.src_blend = rwBLENDONE;
    system.rs.dst_blend = rwBLENDONE;
    system.rs.flags = 0xC;
    system.order_group = 'HOBO';
    system.order_index = 5;
    system.stride = sizeof(wisp_particle);
    system.update = update_wisp_particles;
    system.context = NULL;
    wisp_system_id = xParticleBatchAddSystem(system);
}

void zFire::scene_exit()
{
    if (!_enabled)
    {
        return;
    }
}

void zFire::setup()
{
    if (!_enabled)
    {
        return;
    }

    smoke_emitter = (zSmokeEmitter*)zSceneFindObject(0x0E0896ED);
    spawn_explosion_type = zExplosion::find_type(0x9231F7EF);
}

void zFire::reset()
{
    if (!_enabled)
    {
        return;
    }

    flames_used = 0;
}

void zFire::update(F32 dt)
{
    if (!_enabled)
    {
        return;
    }

    anim_frame_time += 30.0f * dt;
    if (anim_frame_time >= 1.0f)
    {
        anim_frame++;
        anim_frame_time -= 1.0f;
        frame_changed = true;
    }
    else
    {
        frame_changed = false;
    }

    spread_heat(dt);
    update_flames(dt);
}

namespace
{
    // No TSSM symbol (always inlined); the name is Ratatouille's, where the brightness is a tweak
    // (TSSM uses a fixed 20).
    inline void tune_textures()
    {
        S32 min_alpha = xFXHighDynamicRangeGetBackgroundGlow();

        if (last_brightness != 20 || min_alpha != last_background_glow)
        {
            F32 base_alpha = 20 / 255.0f;

            xFXHighDynamicRangeTunePalette(face_texture->raster, base_alpha, true);
            xFXHighDynamicRangeTunePalette(floor_texture->raster, base_alpha, true);

            if (wisp_texture != NULL)
            {
                xFXHighDynamicRangeTunePalette(wisp_texture->raster, base_alpha, true);
            }

            last_brightness = 20;
            last_background_glow = min_alpha;
        }
    }
} // namespace

void zFire::pre_render()
{
    im_flames_size = 0;

    if (!_enabled)
    {
        return;
    }

    tune_textures();

    S32 rs_flags = 0;
    if (xFXHighDynamicRangeGetBackgroundGlow() < 20)
    {
        rs_flags |= 0x8;
    }

    S32 min_glow = xFXHighDynamicRangeGetBackgroundGlow();

    ptank_pool__color_mat_uv2 face_pool;
    face_pool.rs.flags = rs_flags;
    face_pool.rs.src_blend = rwBLENDONE;
    face_pool.rs.dst_blend = rwBLENDONE;
    face_pool.rs.texture = face_texture;
    face_pool.order_group = 'HOBO';
    face_pool.order_index = 5;
    face_pool.reset();

    ptank_pool__color_mat_uv2 floor_pool;
    floor_pool.rs.flags = rs_flags;
    floor_pool.rs.src_blend = rwBLENDONE;
    floor_pool.rs.dst_blend = rwBLENDONE;
    floor_pool.rs.texture = floor_texture;
    floor_pool.order_group = 'HOBO';
    floor_pool.order_index = 5;
    floor_pool.reset();

    xMat4x3& cam_mat = globals.cam->mat;
    F32 Cx = cam_mat.pos.x;
    F32 Cy = cam_mat.pos.y;
    F32 Cz = cam_mat.pos.z;

    zFlame* flame = flames;
    zFlame* end_flame = flame + flames_used;
    for (; flame != end_flame; ++flame)
    {
        F32 Px = flame->mat.pos.x;
        F32 Py = flame->mat.pos.y;
        F32 Pz = flame->mat.pos.z;
        F32 PCx = Cx - Px;
        F32 PCy = Cy - Py;
        F32 PCz = Cz - Pz;

        F32 effect_radius = 0.5f * flame->scale.y;
        xVec4 effect_loc = { Px, Py + effect_radius, Pz, effect_radius };

        if (PCx * PCx + PCz * PCz < 4.0f * (effect_radius * effect_radius) && im_flames_size < 64)
        {
            im_flames[im_flames_size] = flame;
            im_flames_size++;
            continue;
        }

        // GC only: the PS2 build tests effect_loc against the frustum planes with VU code.
        RwSphere worldsph;
        worldsph.center.x = effect_loc.x;
        worldsph.center.y = effect_loc.y;
        worldsph.center.z = effect_loc.z;
        worldsph.radius = effect_loc.w;

        if (!RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
        {
            continue;
        }

        face_pool.next();
        if (!face_pool.valid())
        {
            break;
        }

        S32 intensity;
        S32 alpha = 255.99f * flame->alpha;
        intensity = 20.0f * flame->alpha;
        if (intensity < min_glow)
        {
            intensity = min_glow;
        }

        // Packed big-endian RGBA: r = g = b = alpha, a = intensity.
        *(U32*)face_pool.color = intensity + (alpha << 8) + (alpha << 16) + (alpha << 24);

        S32 uvindex = flame->face_frame + anim_frame;
        F32 u = 0.25f * (uvindex & 3U);
        F32 v = 0.125f * ((uvindex & 31) / 4);
        face_pool.uv[0].assign(u, 0.0009765625f + v);
        face_pool.uv[1].assign(0.25f + u, 0.125f + v);

        F32 Ux = flame->mat.up.x;
        F32 Uy = flame->mat.up.y;
        F32 Uz = flame->mat.up.z;
        F32 PCxUx = PCy * Uz - Uy * PCz;
        F32 PCxUy = PCz * Ux - Uz * PCx;
        F32 PCxUz = PCx * Uy - Ux * PCy;
        // PCxUmag2/Rscale/PCxRmag2: plain copies, dropped from the PS2 DWARF.
        F32 PCxUmag2 = PCxUx * PCxUx + PCxUy * PCxUy + PCxUz * PCxUz;
        F32 Rscale = PCxUmag2 < 0.00001f ? 0.0f : xinvsqrt(PCxUmag2);
        F32 Rx = PCxUx * Rscale;
        F32 Ry = PCxUy * Rscale;
        F32 Rz = PCxUz * Rscale;

        F32 width = flame->scale.x;
        F32 height = flame->scale.y;
        F32 face_width = width * flame->face_flip;

        face_pool.mat->up.assign(0.0f, height, 0.0f);
        face_pool.mat->right.assign(Rx * face_width, Ry * face_width, Rz * face_width);
        face_pool.mat->pos.assign(Px, Py + 0.5f * height, Pz);

        floor_pool.next();
        if (!floor_pool.valid())
        {
            continue;
        }

        F32 floor_width = flame->floor_flip * width;
        F32 floor_height = 0.75f * width;

        *(U32*)floor_pool.color = intensity + (alpha << 8) + (alpha << 16) + (alpha << 24);

        uvindex = flame->floor_frame + anim_frame;
        u = 0.25f * (uvindex & 3U);
        v = 0.125f * ((uvindex & 31) / 4);
        floor_pool.uv[0].assign(u, 0.0078125f + v);
        floor_pool.uv[1].assign(0.25f + u, 0.123046875f + v);

        F32 PCxRx = Ry * PCz - PCy * Rz;
        F32 PCxRy = Rz * PCx - PCz * Rx;
        F32 PCxRz = Rx * PCy - PCx * Ry;
        F32 PCxRmag2 = PCxRx * PCxRx + PCxRy * PCxRy + PCxRz * PCxRz;
        F32 Vscale = floor_height * (PCxRmag2 < 0.00001f ? 0.0f : xinvsqrt(PCxRmag2));
        F32 Vx = PCxRx * Vscale;
        F32 Vy = PCxRy * Vscale;
        F32 Vz = PCxRz * Vscale;

        floor_pool.mat->right.assign(Rx * floor_width, Ry * floor_width, Rz * floor_width);
        floor_pool.mat->up.assign(Vx, Vy, Vz);
        floor_pool.mat->pos.assign(Px + 0.5f * Vx, Py + 0.5f * Vy, Pz + 0.5f * Vz);

        floor_pool.next();
        if (!floor_pool.valid())
        {
            continue;
        }

        *(U32*)floor_pool.color = intensity + (alpha << 8) + (alpha << 16) + (alpha << 24);
        floor_pool.uv[0].assign(u, 0.0078125f + v);
        floor_pool.uv[1].assign(0.25f + u, 0.123046875f + v);

        F32 Wx = floor_height * (Uy * Rz - Ry * Uz);
        F32 Wy = floor_height * (Uz * Rx - Rz * Ux);
        F32 Wz = floor_height * (Ux * Ry - Rx * Uy);

        floor_pool.mat->right.assign(Rx * floor_width, Ry * floor_width, Rz * floor_width);
        floor_pool.mat->up.assign(Wx, Wy, Wz);
        floor_pool.mat->pos.assign(Px + 0.5f * Wx, Py + 0.5f * Wy, Pz + 0.5f * Wz);
    }

    face_pool.flush();
    floor_pool.flush();
}

void zFire::render()
{
    if (!_enabled)
    {
        return;
    }

    if (im_flames_size <= 0)
    {
        return;
    }

    zRenderState(SDRS_FireCards);

    S32 min_alpha = xFXHighDynamicRangeGetBackgroundGlow();
    if (min_alpha < 20)
    {
        xFXHighDynamicRangeBrighten(true);
        xFXHighDynamicRangeSetGlowClamp(min_alpha + 1, false);
    }

    const xMat4x3& cam_mat = globals.cam->mat;
    S32 floor_verts = (im_flames_size - 1) * 8 + 6;

    xIMLockContext face_lock;
    xIMLockContext floor_lock;
    xIMLock(face_lock, XIM_TRILIST, im_flames_size * 6);
    xIMLock(floor_lock, XIM_TRISTRIP, floor_verts);

    xVec3* face_pos = face_lock.pos;
    xVec3* floor_pos = floor_lock.pos;
    xVec2* face_uv = face_lock.uv;
    xVec2* floor_uv = floor_lock.uv;
    U32* face_color = (U32*)face_lock.color;
    U32* floor_color = (U32*)floor_lock.color;
    S32 i;

    {
        F32 Cx = cam_mat.pos.x;
        F32 Cy = cam_mat.pos.y;
        F32 Cz = cam_mat.pos.z;

        i = 0;
        while (true)
        {
            zFlame* flame = im_flames[i];
            F32 s;
            F32 Ex;
            F32 Ey;
            F32 Ez;

            F32 Px = flame->mat.pos.x;
            F32 Py = flame->mat.pos.y;
            F32 Pz = flame->mat.pos.z;
            F32 CPx = Px - Cx;
            F32 CPy = Py - Cy;
            F32 CPz = Pz - Cz;
            F32 Ux = flame->mat.up.x;
            F32 Uy = flame->mat.up.y;
            F32 Uz = flame->mat.up.z;

            F32 UxCPx = Uy * CPz - CPy * Uz;
            F32 UxCPy = Uz * CPx - CPz * Ux;
            F32 UxCPz = Ux * CPy - CPx * Uy;

            // UxCPmag2: a plain copy, dropped from the PS2 DWARF.
            F32 UxCPmag2 = UxCPx * UxCPx + UxCPy * UxCPy + UxCPz * UxCPz;
            s = UxCPmag2 < 0.00001f ? 0.0f : xinvsqrt(UxCPmag2);
            F32 Rx = s * UxCPx;
            F32 Ry = s * UxCPy;
            F32 Rz = s * UxCPz;

            F32 h = flame->scale.y;
            F32 Escale = 0.5f * flame->scale.x * flame->face_flip;
            Ex = Escale * Rx;
            Ey = Escale * Ry;
            Ez = Escale * Rz;

            face_pos[0].x = Px + Ex;
            face_pos[0].y = h + (Py + Ey);
            face_pos[0].z = Pz + Ez;
            face_pos[1].x = Px + Ex;
            face_pos[1].y = Py + Ey;
            face_pos[1].z = Pz + Ez;
            face_pos[2].x = Px - Ex;
            face_pos[2].y = h + (Py - Ey);
            face_pos[2].z = Pz - Ez;
            face_pos[3].x = Px + Ex;
            face_pos[3].y = Py + Ey;
            face_pos[3].z = Pz + Ez;
            face_pos[4].x = Px - Ex;
            face_pos[4].y = Py - Ey;
            face_pos[4].z = Pz - Ez;
            face_pos[5].x = Px - Ex;
            face_pos[5].y = h + (Py - Ey);
            face_pos[5].z = Pz - Ez;
            face_pos += 6;

            s = 0.5f * flame->scale.x;
            Escale = s * flame->floor_flip;
            Ex = Escale * Rx;
            Ey = Escale * Ry;
            Ez = Escale * Rz;

            F32 Vx = CPy * Rz - Ry * CPz;
            F32 Vy = CPz * Rx - Rz * CPx;
            F32 Vz = CPx * Ry - Rx * CPy;
            F32 Vscale = Vx * Vx + Vy * Vy + Vz * Vz;
            Vscale = s * (Vscale < 0.00001f ? 0.0f : xinvsqrt(Vscale));
            Vx *= Vscale;
            Vy *= Vscale;
            Vz *= Vscale;

            if (i != 0)
            {
                floor_pos->x = Vx + (Px + Ex);
                floor_pos->y = Vy + (Py + Ey);
                floor_pos->z = Vz + (Pz + Ez);
                ++floor_pos;
            }

            floor_pos[0].x = Vx + (Px + Ex);
            floor_pos[0].y = Vy + (Py + Ey);
            floor_pos[0].z = Vz + (Pz + Ez);
            floor_pos[1].x = Vx + (Px - Ex);
            floor_pos[1].y = Vy + (Py - Ey);
            floor_pos[1].z = Vz + (Pz - Ez);
            floor_pos[2].x = Px + Ex;
            floor_pos[2].y = Py + Ey;
            floor_pos[2].z = Pz + Ez;

            Vx = Uy * Rz - Ry * Uz;
            Vy = Uz * Rx - Rz * Ux;
            Vz = Ux * Ry - Rx * Uy;
            Vscale = Vx * Vx + Vy * Vy + Vz * Vz;
            s = 0.75f * flame->scale.x;
            Vscale = s * (Vscale < 0.00001f ? 0.0f : xinvsqrt(Vscale));
            Vx *= Vscale;
            Vy *= Vscale;
            Vz *= Vscale;

            floor_pos[3].x = Px - Ex;
            floor_pos[3].y = Py - Ey;
            floor_pos[3].z = Pz - Ez;
            floor_pos[4].x = Vx + (Px + Ex);
            floor_pos[4].y = Vy + (Py + Ey);
            floor_pos[4].z = Vz + (Pz + Ez);
            floor_pos[5].x = Vx + (Px - Ex);
            floor_pos[5].y = Vy + (Py - Ey);
            floor_pos[5].z = Vz + (Pz - Ez);

            ++i;
            if (i >= im_flames_size)
            {
                break;
            }

            floor_pos[6].x = Vx + (Px - Ex);
            floor_pos[6].y = Vy + (Py - Ey);
            floor_pos[6].z = Vz + (Pz - Ez);
            floor_pos += 7;
        }
    }

    i = 0;
    while (true)
    {
        zFlame* flame = im_flames[i];

        S32 uvindex = flame->face_frame + anim_frame;
        // the column is converted to float as unsigned in both the GC and the PS2 build
        F32 u0 = 0.25f * (uvindex & 3U);
        F32 v0 = 0.125f * ((uvindex & 31) / 4);
        F32 u1 = 0.25f + u0;
        F32 v1 = 0.125f + v0;
        v0 += 0.0009765625f;

        face_uv[0].x = u0;
        face_uv[0].y = v0;
        face_uv[1].x = u0;
        face_uv[1].y = v1;
        face_uv[2].x = u1;
        face_uv[2].y = v0;
        face_uv[3].x = u0;
        face_uv[3].y = v1;
        face_uv[4].x = u1;
        face_uv[4].y = v1;
        face_uv[5].x = u1;
        face_uv[5].y = v0;
        face_uv += 6;

        if (i != 0)
        {
            ++floor_uv;
        }

        uvindex = flame->floor_frame + anim_frame;
        u0 = 0.25f * (uvindex & 3U);
        v0 = 0.125f * ((uvindex & 31) / 4);
        u1 = 0.25f + u0;
        v1 = 0.123046875f + v0;
        v0 += 0.0078125f;

        floor_uv[0].x = u0;
        floor_uv[0].y = v0;
        floor_uv[1].x = u1;
        floor_uv[1].y = v0;
        floor_uv[2].x = u0;
        floor_uv[2].y = v1;
        floor_uv[3].x = u1;
        floor_uv[3].y = v1;
        floor_uv[4].x = u0;
        floor_uv[4].y = v0;
        floor_uv[5].x = u1;
        floor_uv[5].y = v0;

        ++i;
        if (i >= im_flames_size)
        {
            break;
        }

        floor_uv += 7;
    }

    i = 0;
    while (true)
    {
        zFlame* flame = im_flames[i];

        S32 intensity = 255.99f * flame->alpha;
        S32 alpha = 20.0f * flame->alpha;
        if (alpha < min_alpha)
        {
            alpha = min_alpha;
        }

        U32 color = alpha + (intensity << 8) + (intensity << 16) + (intensity << 24);

        face_color[0] = color;
        face_color[1] = color;
        face_color[2] = color;
        face_color[3] = color;
        face_color[4] = color;
        face_color[5] = color;
        face_color += 6;

        if (i != 0)
        {
            ++floor_color;
        }

        floor_color[0] = color;
        floor_color[1] = color;
        floor_color[2] = color;
        floor_color[3] = color;
        floor_color[4] = color;
        floor_color[5] = color;

        ++i;
        if (i >= im_flames_size)
        {
            break;
        }

        floor_color += 7;
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)face_texture->raster);
    xIMRender(face_lock, NULL);
    xIMUnlock(face_lock);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)floor_texture->raster);
    xIMRender(floor_lock, NULL);
    xIMUnlock(floor_lock);

    if (min_alpha < 20)
    {
        xFXHighDynamicRangeBrighten(true);
        xFXHighDynamicRangeResetGlowClamp();
    }
}

namespace zFire
{
    namespace
    {
        // Grid callback that sweeps against every visible static/platform, destructible or not.
        struct cb_ss
        {
            xSweptSphere& sws;
            xRay3& ray;

            cb_ss(xSweptSphere& sws, xRay3& ray) : sws(sws), ray(ray)
            {
            }

            bool operator()(xEnt& ent)
            {
                if ((ent.flags & 0x85) != 0x1)
                {
                    return true;
                }

                S32 type = ent.baseType;
                if (type != eBaseTypeStatic && type != eBaseTypePlatform)
                {
                    return true;
                }

                xSweptSphereToEnt(&sws, &ent, &ray);
                return true;
            }
        };

        // Same as cb_ss, but skips destructibles.
        struct cb_sss
        {
            xSweptSphere& sws;
            xRay3& ray;

            cb_sss(xSweptSphere& sws, xRay3& ray) : sws(sws), ray(ray)
            {
            }

            bool operator()(xEnt& ent)
            {
                if ((ent.flags & 0x85) != 0x1)
                {
                    return true;
                }

                S32 type = ent.baseType;
                if (type == eBaseTypeStatic)
                {
                    if (((zEntSimpleObj&)ent).destructible != NULL)
                    {
                        return true;
                    }
                }
                else if (type == eBaseTypePlatform)
                {
                    if (((zPlatform&)ent).destructible != NULL)
                    {
                        return true;
                    }
                }
                else
                {
                    return true;
                }

                if (ent.moreFlags & 0x10)
                {
                    return true;
                }

                xSweptSphereToEnt(&sws, &ent, &ray);
                return true;
            }
        };

        inline bool collide_swept_sphere_statics(xSweptSphere& sws, bool check_destructibles)
        {
            xSweptSphereToEnv(&sws, globals.sceneCur->env);

            xBound bound;
            bound.type = XBOUND_TYPE_BOX;
            bound.box.box = sws.box;
            bound.qcd = sws.qcd;
            xVec3Add(&bound.box.center, &sws.box.lower, &sws.box.upper);
            xVec3SMulBy(&bound.box.center, 0.5f);

            xRay3 ray;
            ray.origin = sws.start;
            ray.dir = sws.dir;
            ray.min_t = 0.0f;
            ray.max_t = sws.curdist;
            ray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;

            if (check_destructibles)
            {
                cb_ss cb(sws, ray);
                xGridCheckBound(colls_grid, bound, bound.qcd, cb);
                xGridCheckBound(colls_oso_grid, bound, bound.qcd, cb);
            }
            else
            {
                cb_sss cb(sws, ray);
                xGridCheckBound(colls_grid, bound, bound.qcd, cb);
                xGridCheckBound(colls_oso_grid, bound, bound.qcd, cb);
            }

            return sws.curdist < sws.dist;
        }
    } // namespace

    // No TSSM symbols (always inlined); the names and signatures are Ratatouille's.
    inline U32 find_in_sphere(zFlame** found, U32 max_found, const xVec3& loc, F32 r)
    {
        cb_flame_sphere_check cb(found, max_found, loc, r);
        xTransient::space_tree.intersect_circle(loc.x, loc.z, r, cb);
        return cb.total;
    }

    inline U32 find_in_circle(zFlame** found, U32 max_found, F32 x, F32 z, F32 r)
    {
        cb_flame_circle_check cb(found, max_found, x, z, r);
        xTransient::space_tree.intersect_circle(x, z, r, cb);
        return cb.total;
    }
} // namespace zFire

zFire::handle zFire::emit(const spawn_data& params, bool* success)
{
    static const F32 overlap_max = 0.75f;
    static const F32 ground_max_tilt = 0.70710677f;
    static const F32 ground_dist_bias = 0.1f;
    static const F32 ground_dist_max = 10.0f;

    handle h;
    h.node = NULL;

    if (success != NULL)
    {
        *success = false;
    }

    if (!_enabled)
    {
        return h;
    }

    spawn_data d = params;

    if ((d.flags & 0x402) == 0x400)
    {
        F32 overlap_radius = overlap_max * d.radius;

        if (d.flags & 0x40)
        {
            zFlame* nearbies[6];
            S32 nearbies_size = find_in_circle(nearbies, 6, d.loc.x, d.loc.z, overlap_radius);
            if (nearbies_size > 0)
            {
                zFlame** nearby = nearbies;
                zFlame** end_nearby = nearby + nearbies_size;
                for (; nearby != end_nearby; ++nearby)
                {
                    (*nearby)->refuel(d.fuel, d.flags);
                }

                return h;
            }
        }
        else
        {
            zFlame* nearby;
            if (find_in_circle(&nearby, 1, d.loc.x, d.loc.z, overlap_radius) != 0)
            {
                return h;
            }
        }
    }

    if (d.flags & 0x1)
    {
        xVec3 start = d.loc;
        start.y += d.radius;
        xVec3 end = start;
        end.y -= ground_dist_max;

        xSweptSphere sws;
        xSweptSpherePrepare(&sws, &start, &end, d.radius);

        collide_swept_sphere_statics(sws, (params.flags & 0x100) != 0);
        if (sws.curdist >= sws.dist)
        {
            return h;
        }

        xSweptSphereGetResults(&sws);

        d.loc = sws.worldContact;
        d.loc.y += ground_dist_bias;
        d.up = sws.worldNormal;

        if (d.up.y < ground_max_tilt)
        {
            d.up.y = ground_max_tilt;

            // Rescale x/z so the normal stays unit length: 0.5 = 1 - ground_max_tilt^2.
            F32 ixz = xsqrt(0.5f / (d.up.x * d.up.x + d.up.z * d.up.z));
            d.up.x *= ixz;
            d.up.z *= ixz;
        }
    }

    if ((d.flags & 0x402) == 0)
    {
        F32 overlap_radius = overlap_max * d.radius;

        if (d.flags & 0x40)
        {
            zFlame* nearbies[6];
            S32 nearbies_size = find_in_sphere(nearbies, 6, d.loc, overlap_radius);
            if (nearbies_size > 0)
            {
                zFlame** nearby = nearbies;
                zFlame** end_nearby = nearby + nearbies_size;
                for (; nearby != end_nearby; ++nearby)
                {
                    (*nearby)->refuel(d.fuel, d.flags);
                }

                return h;
            }
        }
        else
        {
            zFlame* nearby;
            if (find_in_sphere(&nearby, 1, d.loc, overlap_radius) != 0)
            {
                return h;
            }
        }
    }

    if (xTransient::space_tree.full())
    {
        return h;
    }

    zFlame* flame = alloc_flame();
    if (flame == NULL)
    {
        return h;
    }

    if (!flame->create(d))
    {
        free_flame(*flame);
        return h;
    }

    if (d.flags & 0x200)
    {
        if (flame->props != NULL && flame->props->sound_group != 0)
        {
            flame->sound =
                xSndMgrPlay(flame->props->sound_group, 0, &d.loc, NULL, NULL, NULL, NULL);
        }
        else
        {
            flame->sound = (iSndHandle)-1;
        }
    }

    flame->refresh_nearby();
    flame->h.node =
        xTransient::space_tree.insert(flame, 1, flame->mat.pos.x, flame->mat.pos.z, flame->radius);

    if (d.flags & 0x20)
    {
        zExplosion::emit(spawn_explosion_type, flame->mat.pos);
    }

    if (success != NULL)
    {
        *success = true;
    }

    return (d.flags & 0xC) ? flame->h : h;
}

void zFire::emit_sphere(const spawn_data& params, const xVec3& center, F32 radius)
{
    static const F32 rand_offset = 0.0f;
    static const F32 overlap_max = 0.875f;

    if (!_enabled)
    {
        return;
    }

    spawn_data d = params;

    F32 maxr = d.radius;
    F32 minr = overlap_max * d.radius;
    if (maxr < minr)
    {
        maxr = minr;
    }

    S32 min_fit = radius / maxr;
    S32 max_fit = radius / minr;
    if (max_fit <= 0)
    {
        return;
    }

    S32 fit = min_fit;
    F32 r = maxr;
    F32 totalr = fit * maxr;
    if (max_fit > fit && totalr < radius)
    {
        ++fit;
        r = radius / fit;
    }

    F32 start_theta = 2.0f * PI * xurand();
    xVec3 origin = center;

    if (fit % 2 == 0)
    {
        origin.x += r * isin(start_theta);
        origin.z += r * icos(start_theta);
    }

    S32 max_ring = fit / 2 + 1;
    F32 max_dist = radius - r + 0.01f;
    F32 max_dist2 = max_dist * max_dist;
    F32 diam = 0.0f;
    S32 ring = 0;
    S32 total = 1;

    while (ring < max_ring)
    {
        S32 amount = 0;
        F32 theta = start_theta;

        while (amount < total)
        {
            xVec3 v = { diam * isin(theta) + origin.x, origin.y, diam * icos(theta) + origin.z };
            theta += PI / 3.0f;
            xVec3 endv = { diam * isin(theta) + origin.x, origin.y, diam * icos(theta) + origin.z };
            xVec3 dv;

            if (ring > 0)
            {
                dv = (endv - v) * (1.0f / ring);
            }

            S32 i = 0;
            while (true)
            {
                xVec3 voff = { v.x - center.x, v.y - center.y, v.z - center.z };
                if (voff.length2() <= max_dist2)
                {
                    d.loc = v;
                    d.loc.x += rand_offset * xurand();
                    d.loc.z += rand_offset * xurand();
                    emit(d, NULL);
                }

                ++i;
                ++amount;
                if (i >= ring)
                {
                    break;
                }

                v += dv;
            }
        }

        diam += 2.0f * r;
        ++ring;
        total = 6 * ring;
    }
}

void zFire::destroy(handle h)
{
    if (!_enabled)
    {
        return;
    }

    zFlame& flame = *(zFlame*)h.node->value;
    xTransient::space_tree.erase(h.node);

    if (flame.sound != (iSndHandle)-1)
    {
        xSndMgrStop(flame.sound);
    }

    if (flame.warp != NULL)
    {
        xScreenWarp::remove(flame.warp);
    }

    flame.unlink_nearby();
    free_flame(flame);
}

// Always inlined here; name and signature from Ratatouille's zFlame::refuel.
inline void zFlame::refuel(F32 fuel, S32 flags)
{
    if ((this->flags & 0x8) || this->fuel > fuel)
    {
        return;
    }

    this->flags &= ~0x800;

    if ((flags & 0x20) && fuel - this->fuel >= 0.25f)
    {
        zExplosion::emit(spawn_explosion_type, mat.pos);
    }

    this->fuel = fuel;
}

// zFlame::update, update_effects and create are plain functions, as in the PS2 demo: GC/2.6
// auto-inlines them and strips the copies, but their literals stay at this source position, as in
// retail.
void zFlame::update(F32 dt, F32 heat_decay)
{
    F32 add_heat = heat_nearby;
    F32 dh;
    heat_nearby = 0.0f;

    if (!(flags & 0x8))
    {
        fuel -= dt;
        if (fuel <= 0.0f)
        {
            fuel = 0.0f;
            flags |= 0x800;
        }
    }

    if (flags & 0x800)
    {
        heat_nearby -= 0.25f * dt;
        if (heat_nearby <= 0.0f)
        {
            heat += heat_nearby;
            heat_nearby = 0.0f;
            if (heat <= 0.0f)
            {
                heat = 0.0f;
            }
        }
    }
    else
    {
        if (heat < 0.5f)
        {
            dh = dt;
            if (heat + dt > 0.5f)
            {
                dh = 0.5f - heat;
            }
            add_heat += dh;
        }

        dh = absorb_heat(add_heat);
        heat += dh;

        if (heat > 0.5f)
        {
            heat += (heat_decay - 1.0f) * heat + -0.3f * dt;
            if (heat < 0.5f)
            {
                heat = 0.5f;
            }
        }

        if (heat > 1.0f)
        {
            heat = 1.0f;
        }
    }
}

void zFlame::update_effects(F32 dt)
{
    F32 end_heat = heat + heat_nearby;
    F32 dh = 0.0f;
    F32 diff = end_heat - effects_heat;

    if (diff < 0.0f)
    {
        dh = -2.0f * dt;
        if (dh <= diff)
        {
            dh = diff;
            effects_heat = end_heat;
        }
        else
        {
            effects_heat += dh;
        }
    }
    else if (diff > 0.0f)
    {
        dh = 4.0f * dt;
        if (dh >= diff)
        {
            dh = diff;
            effects_heat = end_heat;
        }
        else
        {
            effects_heat += dh;
        }
    }

    if (diff != 0.0f && warp != NULL)
    {
        xVec3 warp_loc = { mat.pos.x, mat.pos.y + radius, mat.pos.z };
        xScreenWarp::modify(warp, warp_loc, 2.0f * radius, 1.0f, xScreenWarp::TYPE_HEAT_SHIMMER,
                            effects_heat, 1.0f, 4);
    }

    alpha = 3.4f * end_heat;
    if (alpha > 0.85f)
    {
        alpha = 0.85f;
    }

    emit_smoke(dh, dt);
}

bool zFlame::create(const zFire::spawn_data& info)
{
    mat.pos = info.loc;
    mat.up = info.up;
    flags = info.flags & 0xFF;
    radius = info.radius;
    fuel = info.fuel;
    heat = info.heat;
    heat_absorb = info.heat_absorb;
    effects_heat = info.heat;
    alpha = 1.0f;
    heat_nearby = 0.0f;
    props = info.props;

    U32 rval = xrand_GenRandInt32();
    face_flip = 1 - (rval & 0x2);
    floor_flip = 1 - ((rval >> 1) & 0x2);
    smoke_emitted = (rval & 0xFFFF0000) * (1.0f / 0xFFFF0000);

    if (info.flags & 0x80)
    {
        warp = NULL;
    }
    else
    {
        xVec3 warp_loc = { mat.pos.x, mat.pos.y + radius, mat.pos.z };
        warp = xScreenWarp::emit(warp_loc, 2.0f * radius, 1.0f, xScreenWarp::TYPE_HEAT_SHIMMER,
                                 effects_heat, 1.0f, 4);
    }

    nearby_size = 0;
    scale.x = 4.0f * radius;
    scale.y = 12.0f * (radius * effects_heat);
    face_frame = xrand_GenRandInt32() & 0x1F;
    floor_frame = xrand_GenRandInt32() & 0x1F;
    return true;
}

void zFlame::emit_smoke(F32 dheat, F32 dt)
{
    if (smoke_emitter == NULL)
    {
        return;
    }

    F32 rate = 4.0f * (1.0f - effects_heat) + -30.0f * dheat;
    if (rate <= 0.0f)
    {
        return;
    }

    if (rate > 40.0f)
    {
        rate = 40.0f;
    }

    smoke_emitted += rate * dt;

    S32 emit = (S32)smoke_emitted;
    if (emit <= 0)
    {
        return;
    }

    smoke_emitted -= emit;

    xVec3* buffer = (xVec3*)xMemPushTemp(emit * sizeof(xVec3) * 2);
    xVec3* vel = buffer + emit;

    // The ranges are pre-scaled by 1/2^32 so they can be applied directly to a random U32.
    F32 x_min = mat.pos.x + -0.1f * scale.x;
    F32 y_min = mat.pos.y + -0.2f * scale.x;
    F32 z_min = mat.pos.z + -0.1f * scale.x;
    F32 xz_range = 2.3283064e-10f * 0.2f * scale.x;
    F32 y_range = 2.3283064e-10f * 0.2f * scale.x;
    F32 vel_min = 1.2f * scale.x;
    F32 vel_range = 2.3283064e-10f * (1.4f * scale.x - vel_min);

    for (S32 i = 0; i < emit; ++i)
    {
        buffer[i].x = x_min + xz_range * xrand_RandomInt32();
        buffer[i].y = y_min + y_range * xrand_RandomInt32();
        buffer[i].z = z_min + xz_range * xrand_RandomInt32();
        vel[i].assign(0.0f, vel_min + vel_range * xrand_RandomInt32(), 0.0f);
    }

    F32 size_min = 0.6f * scale.x;
    F32 size_max = 0.8f * scale.x;
    zParticleCustomEmitFireSmoke(buffer, vel, emit, 1.3f, 1.5f - 1.3f, size_min,
                                 size_max - size_min, dt);

    xMemPopTemp(buffer);
}

void zFlame::refresh_nearby()
{
    unlink_nearby();

    zFlame* found[16];
    // not in the DWARF: space-tree callback object passed by reference.
    cb_flame_sphere_check cb(found, 16, mat.pos, 2.0f * radius);
    xTransient::space_tree.intersect_circle(mat.pos.x, mat.pos.z, cb.r, cb);

    S32 found_size = cb.total;
    zFlame** n;
    zFlame** endn;

    if (found_size > 6)
    {
        flame_distance_pair pairs[16];
        flame_distance_pair* p = pairs;

        n = found;
        endn = found + found_size;
        for (; n != endn; ++n)
        {
            zFlame& bro = **n;
            if (bro.nearby_size >= 6)
            {
                continue;
            }

            F32 dx = mat.pos.x - bro.mat.pos.x;
            F32 dy = mat.pos.y - bro.mat.pos.y;
            F32 dz = mat.pos.z - bro.mat.pos.z;
            p->flame = &bro;
            p->dist = xsqrt(dx * dx + dy * dy + dz * dz) - bro.radius;
            ++p;
        }

        found_size = p - pairs;
        if (found_size >= 6)
        {
            isort_float(pairs, found_size, sizeof(flame_distance_pair), 0);
            found_size = 6;
        }

        n = found;
        endn = found + found_size;
        p = pairs;
        for (; n != endn; ++n)
        {
            *n = p->flame;
            ++p;
        }
    }

    n = found;
    endn = found + found_size;
    for (; n != endn; ++n)
    {
        zFlame& bro = **n;
        if (bro.nearby_size >= 6)
        {
            continue;
        }

        nearby[nearby_size] = &bro;
        nearby_size++;
        bro.nearby[bro.nearby_size] = this;
        bro.nearby_size++;
    }
}

// Dead-stripped placeholder: retail's .sdata2 opens with 1.0f, 0.0f and the int->float double of a
// stripped function after refresh_nearby, likely the card_animator grid code that Ratatouille keeps
// here. Body unknown.
static void __deadstripped_zFire(F32* out, S32 n)
{
    out[0] = 1.0f;
    out[1] = 0.0f;
    out[2] = n;
}
