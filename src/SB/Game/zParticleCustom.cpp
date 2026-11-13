#include <types.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cmath>

#include "xMath.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xParticleBatch.h"
#include "xPtankPool.h"
#include "xRand.h"
#include "xstransvc.h"
#include "zGlobals.h"

namespace
{
    struct dust_particle
    {
        F32 age;
        F32 age_rate;
        F32 size;
        F32 scale_yoffset;
        xVec3 loc;
        xVec3 vel;
        xColor_tag color;
        U8 frame;
        U8 pad1;
        S8 xflip;
        S8 yflip;
    };

    struct glint_particle
    {
        F32 age;
        F32 age_rate;
        F32 length;
        F32 length_vel;
        F32 length_clamp;
        F32 width;
        xVec3 loc;
        xVec3 vel;
    };

    struct pebble_particle
    {
        xVec3 loc;
        xVec3 vel;
        F32 size;
        F32 plane_y;
    };

    struct spark_particle
    {
        F32 age;
        F32 age_rate;
        F32 size;
        xVec3 loc;
        xVec3 vel;
    };

    struct fire_smoke_particle
    {
        F32 age;
        F32 age_rate;
        F32 size;
        xVec3 loc;
        xVec3 vel;
        S8 xflip;
        S8 yflip;
        U8 pad1;
        U8 pad2;
    };

    xParticleBatchGroup particle_batch_group;

    struct
    {
        S32 spark;
        S32 glint;
        S32 pebble;
        S32 dust;
        S32 fire_smoke;
    } system_id;

    F32 dust_vel_decay;

    S32 update_fire_smoke_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    S32 update_dust_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    S32 update_pebble_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    S32 update_glint_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    S32 update_spark_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
} // namespace

namespace
{
    S32 update_spark_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size& _fptank_pool = (ptank_pool__pos_color_size&)apool;
        S32 _fptank_used = _fptank_pool.used;
        xVec3* pool_pos = _fptank_pool.pos;
        xColor_tag* pool_color = _fptank_pool.color;
        xVec2* pool_size = _fptank_pool.size;
        spark_particle* p = (spark_particle*)mem;
        spark_particle* end = p + count;
        F32 age1;
        F32 t1;
        xVec4 loc;

        while (p != end)
        {
            p->age += dt;
            t1 = p->age * p->age_rate;

            if (t1 >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            age1 = p->age;
            loc.x = p->loc.x + p->vel.x * age1;
            loc.y = p->loc.y + p->vel.y * age1 + -10.0f * (age1 * age1);
            loc.z = p->loc.z + p->vel.z * age1;
            loc.w = p->size;

            RwSphere worldsph;
            worldsph.center.x = loc.x;
            worldsph.center.y = loc.y;
            worldsph.center.z = loc.z;
            worldsph.radius = loc.w;
            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                if (_fptank_used >= 80)
                {
                    _fptank_pool.used = _fptank_used;
                    _fptank_pool.next();
                    _fptank_used = _fptank_pool.used;
                    pool_pos = _fptank_pool.pos;
                    pool_color = _fptank_pool.color;
                    pool_size = _fptank_pool.size;

                    if (!_fptank_pool.valid())
                    {
                        break;
                    }
                }
                else
                {
                    ++_fptank_used;
                    pool_pos = (xVec3*)((U8*)pool_pos + _fptank_pool.pos_stride);
                    pool_color = (xColor_tag*)((U8*)pool_color + _fptank_pool.color_stride);
                    pool_size = (xVec2*)((U8*)pool_size + _fptank_pool.size_stride);
                }

                // not in the DWARF: the gray level is replicated into all four channels.
                U32 c = 255.9f * (1.0f - t1);
                *(U32*)pool_color = c + (c << 8) + (c << 16) + (c << 24);
                *pool_pos = *(xVec3*)&loc;
                pool_size->assign(loc.w);
            }

            ++p;
        }

        _fptank_pool.used = _fptank_used;
        _fptank_pool.pos = pool_pos;
        _fptank_pool.color = pool_color;
        _fptank_pool.size = pool_size;

        return p - (spark_particle*)mem;
    }

    S32 update_glint_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__color_mat& _fptank_pool = (ptank_pool__color_mat&)apool;
        S32 _fptank_used = _fptank_pool.used;
        xColor_tag* pool_color = _fptank_pool.color;
        xMat4x3* pool_mat = _fptank_pool.mat;
        xMat4x3& cam_mat = *(xMat4x3*)RwFrameGetMatrix(RwCameraGetFrame(globals.screen->icam));
        F32 Ax = cam_mat.at.x;
        F32 Ay = cam_mat.at.y;
        F32 Az = cam_mat.at.z;
        glint_particle* p = (glint_particle*)mem;
        glint_particle* end = p + count;
        F32 age1;
        F32 t1;
        F32 length;
        F32 Vx;
        F32 Vy;
        F32 Vz;
        // Vmag2 and Rmag2 are not PS2 DWARF locals; names from Ratatouille's DWARF.
        F32 Vmag2;
        F32 Uscale;
        F32 Ux;
        F32 Uy;
        F32 Uz;
        xVec4 loc;
        F32 Rx;
        F32 Ry;
        F32 Rz;
        F32 Rmag2;
        F32 Rscale;

        while (p != end)
        {
            p->age += dt;
            t1 = p->age * p->age_rate;

            if (t1 >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            age1 = p->age;
            length = p->length + p->length_vel * age1;
            if (length > p->length_clamp)
            {
                length = p->length_clamp;
            }

            Vx = p->vel.x;
            Vy = p->vel.y;
            Vz = p->vel.z;
            Vmag2 = Vx * Vx + Vy * Vy + Vz * Vz;
            Uscale = length * (Vmag2 < 0.00001f ? 0.0f : xinvsqrt(Vmag2));
            Ux = Vx * Uscale;
            Uy = Vy * Uscale;
            Uz = Vz * Uscale;

            loc.x = p->loc.x + Vx * age1 - 0.5f * Ux;
            loc.y = p->loc.y + Vy * age1 + -10.0f * (age1 * age1) - 0.5f * Uy;
            loc.z = p->loc.z + Vz * age1 - 0.5f * Uz;
            loc.w = 0.5f * length;

            RwSphere worldsph;
            worldsph.center.x = loc.x;
            worldsph.center.y = loc.y;
            worldsph.center.z = loc.z;
            worldsph.radius = loc.w;
            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                if (_fptank_used >= 80)
                {
                    _fptank_pool.used = _fptank_used;
                    _fptank_pool.next();
                    _fptank_used = _fptank_pool.used;
                    pool_color = _fptank_pool.color;
                    pool_mat = _fptank_pool.mat;

                    if (!_fptank_pool.valid())
                    {
                        break;
                    }
                }
                else
                {
                    ++_fptank_used;
                    pool_color = (xColor_tag*)((U8*)pool_color + _fptank_pool.color_stride);
                    pool_mat = (xMat4x3*)((U8*)pool_mat + _fptank_pool.mat_stride);
                }

                // not in the DWARF: the gray level is replicated into all four channels.
                U32 c = 255.9f * (1.0f - t1);
                *(U32*)pool_color = c + (c << 8) + (c << 16) + (c << 24);

                Rx = Vy * Az - Ay * Vz;
                Ry = Vz * Ax - Az * Vx;
                Rz = Vx * Ay - Ax * Vy;
                Rmag2 = Rx * Rx + Ry * Ry + Rz * Rz;
                Rscale = p->width * (Rmag2 < 0.00001f ? 0.0f : xinvsqrt(Rmag2));

                pool_mat->right.x = Rx * Rscale;
                pool_mat->right.y = Ry * Rscale;
                pool_mat->right.z = Rz * Rscale;
                pool_mat->up.x = Ux;
                pool_mat->up.y = Uy;
                pool_mat->up.z = Uz;
                pool_mat->pos = *(xVec3*)&loc;
            }

            ++p;
        }

        _fptank_pool.used = _fptank_used;
        _fptank_pool.color = pool_color;
        _fptank_pool.mat = pool_mat;

        return p - (glint_particle*)mem;
    }

    // dv and asize are not PS2 DWARF locals; names from Ratatouille's DWARF of this function.
    S32 update_pebble_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size& _fptank_pool = (ptank_pool__pos_color_size&)apool;
        S32 _fptank_used = _fptank_pool.used;
        xVec3* pool_pos = _fptank_pool.pos;
        xColor_tag* pool_color = _fptank_pool.color;
        xVec2* pool_size = _fptank_pool.size;
        pebble_particle* p = (pebble_particle*)mem;
        pebble_particle* end = p + count;
        F32 oldy;
        F32 dv;
        F32 asize;
        F32 plane_y;
        F32 under;
        xVec4 loc;

        while (p != end)
        {
            oldy = p->loc.y;
            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt + -7.5f * (dt * dt);
            p->loc.z += p->vel.z * dt;
            dv = -15.0f * dt;
            p->vel.y += dv;

            asize = xabs(p->size);
            plane_y = p->plane_y;
            under = plane_y - p->loc.y;

            if (under >= 0.0f)
            {
                if (oldy < plane_y)
                {
                    if (under >= asize)
                    {
                        --end;
                        *p = *end;
                        continue;
                    }
                }
                else
                {
                    if (p->vel.y < 0.0f)
                    {
                        p->vel.x *= 0.4f;
                        p->vel.y *= -0.5f;
                        p->vel.z *= 0.4f;

                        if (p->vel.y < 1.0f - dv)
                        {
                            p->vel.y = 0.0f;
                        }
                        else
                        {
                            p->loc.y = plane_y;
                        }
                    }
                    else
                    {
                        p->loc.y = plane_y;
                    }
                }
            }

            loc.x = p->loc.x;
            loc.y = p->loc.y + 0.5f * asize;
            loc.z = p->loc.z;
            loc.w = asize;

            RwSphere worldsph;
            worldsph.center.x = loc.x;
            worldsph.center.y = loc.y;
            worldsph.center.z = loc.z;
            worldsph.radius = loc.w;
            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                if (_fptank_used >= 80)
                {
                    _fptank_pool.used = _fptank_used;
                    _fptank_pool.next();
                    _fptank_used = _fptank_pool.used;
                    pool_pos = _fptank_pool.pos;
                    pool_color = _fptank_pool.color;
                    pool_size = _fptank_pool.size;

                    if (!_fptank_pool.valid())
                    {
                        break;
                    }
                }
                else
                {
                    ++_fptank_used;
                    pool_pos = (xVec3*)((U8*)pool_pos + _fptank_pool.pos_stride);
                    pool_color = (xColor_tag*)((U8*)pool_color + _fptank_pool.color_stride);
                    pool_size = (xVec2*)((U8*)pool_size + _fptank_pool.size_stride);
                }

                *(U32*)pool_color = 0xFFFFFFFF;
                *pool_pos = *(xVec3*)&loc;
                pool_size->assign(p->size, asize);
            }

            ++p;
        }

        _fptank_pool.used = _fptank_used;
        _fptank_pool.pos = pool_pos;
        _fptank_pool.color = pool_color;
        _fptank_pool.size = pool_size;

        return p - (pebble_particle*)mem;
    }

    // Placeholder for dead-stripped code: retail pools -15.0f and -7.5f here, from a stripped
    // function (the Incredibles prototype has zParticleCustomEmitPebble at this spot). The body is
    // unknown.
    static void __deadstripped2_zParticleCustom(F32* consts)
    {
        consts[0] = -15.0f;
        consts[1] = -7.5f;
    }

    S32 update_dust_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size_uv2& _fptank_pool = (ptank_pool__pos_color_size_uv2&)apool;
        S32 _fptank_used = _fptank_pool.used;
        xVec3* pool_pos = _fptank_pool.pos;
        xColor_tag* pool_color = _fptank_pool.color;
        xVec2* pool_size = _fptank_pool.size;
        xVec2* pool_uv = _fptank_pool.uv;
        F32 vel_decay = dust_vel_decay;
        dust_particle* p = (dust_particle*)mem;
        dust_particle* end = p + count;
        F32 t1;
        F32 size;
        xVec4 loc;
        F32 t2;
        F32 max_alpha;
        F32 alpha;
        F32 u;
        F32 v;

        while (p != end)
        {
            p->age += dt;
            t1 = p->age * p->age_rate;

            if (t1 >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            p->vel *= vel_decay;
            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt;
            p->loc.z += p->vel.z * dt;

            size = p->size + 2.0f * (p->size * t1);
            loc.x = p->loc.x;
            loc.y = p->loc.y + p->scale_yoffset * size;
            loc.z = p->loc.z;
            loc.w = size;

            RwSphere worldsph;
            worldsph.center.x = loc.x;
            worldsph.center.y = loc.y;
            worldsph.center.z = loc.z;
            worldsph.radius = loc.w;
            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                if (_fptank_used >= 80)
                {
                    _fptank_pool.used = _fptank_used;
                    _fptank_pool.next();
                    _fptank_used = _fptank_pool.used;
                    pool_pos = _fptank_pool.pos;
                    pool_color = _fptank_pool.color;
                    pool_size = _fptank_pool.size;
                    pool_uv = _fptank_pool.uv;

                    if (!_fptank_pool.valid())
                    {
                        break;
                    }
                }
                else
                {
                    ++_fptank_used;
                    pool_pos = (xVec3*)((U8*)pool_pos + _fptank_pool.pos_stride);
                    pool_color = (xColor_tag*)((U8*)pool_color + _fptank_pool.color_stride);
                    pool_size = (xVec2*)((U8*)pool_size + _fptank_pool.size_stride);
                    pool_uv = (xVec2*)((U8*)pool_uv + _fptank_pool.uv_stride);
                }

                *pool_color = p->color;

                t2 = t1 * t1;
                max_alpha = p->color.a;
                // retail creates 12.88889f before 17.77778f and -30.66667f: the cubic term comes first.
                alpha = max_alpha * (12.88889f * (t1 * t2) + (17.77778f * t1 + -30.66667f * t2));
                if (alpha < max_alpha)
                {
                    pool_color->a = alpha + 0.5f;
                }

                *pool_pos = *(xVec3*)&loc;
                pool_size->assign(size * p->xflip, size * p->yflip);

                u = 0.5f * (p->frame & 1);
                v = 0.25f * (p->frame & 2);
                pool_uv[0].assign(u, v);
                pool_uv[1].assign(0.5f + u, 0.5f + v);
            }

            ++p;
        }

        _fptank_pool.used = _fptank_used;
        _fptank_pool.pos = pool_pos;
        _fptank_pool.color = pool_color;
        _fptank_pool.size = pool_size;
        _fptank_pool.uv = pool_uv;

        return p - (dust_particle*)mem;
    }

    // Placeholder for dead-stripped code: retail pools 0.5f here, from a stripped function (the
    // Incredibles prototype has zParticleCustomEmitDust at this spot). The body is unknown.
    static void __deadstripped_zParticleCustom(F32* consts)
    {
        consts[0] = 0.5f;
    }

    S32 update_fire_smoke_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size& _fptank_pool = (ptank_pool__pos_color_size&)apool;
        S32 _fptank_used = _fptank_pool.used;
        xVec3* pool_pos = _fptank_pool.pos;
        xColor_tag* pool_color = _fptank_pool.color;
        xVec2* pool_size = _fptank_pool.size;
        U32 color;
        fire_smoke_particle* p = (fire_smoke_particle*)mem;
        fire_smoke_particle* end = p + count;
        F32 age1;
        F32 t1;
        F32 size;
        xVec4 loc;
        F32 t2;
        F32 alpha;

        while (p != end)
        {
            p->age += dt;
            t1 = p->age * p->age_rate;

            if (t1 >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            age1 = p->age;
            size = p->size + p->size * t1;
            loc.x = p->loc.x + p->vel.x * age1;
            loc.y = p->loc.y + p->vel.y * age1;
            loc.z = p->loc.z + p->vel.z * age1;
            loc.w = size;

            RwSphere worldsph;
            worldsph.center.x = loc.x;
            worldsph.center.y = loc.y;
            worldsph.center.z = loc.z;
            worldsph.radius = loc.w;
            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                if (_fptank_used >= 80)
                {
                    _fptank_pool.used = _fptank_used;
                    _fptank_pool.next();
                    _fptank_used = _fptank_pool.used;
                    pool_pos = _fptank_pool.pos;
                    pool_color = _fptank_pool.color;
                    pool_size = _fptank_pool.size;

                    if (!_fptank_pool.valid())
                    {
                        break;
                    }
                }
                else
                {
                    ++_fptank_used;
                    pool_pos = (xVec3*)((U8*)pool_pos + _fptank_pool.pos_stride);
                    pool_color = (xColor_tag*)((U8*)pool_color + _fptank_pool.color_stride);
                    pool_size = (xVec2*)((U8*)pool_size + _fptank_pool.size_stride);
                }

                t2 = t1 * t1;
                alpha = 1720.336f * (t1 * t2) + (2064.4033f * t1 + -3784.7395f * t2);
                if (alpha > 255.9f)
                {
                    alpha = 255.9f;
                }

                color = 0x66666600 | (S32)alpha;
                *(U32*)pool_color = color;
                *pool_pos = *(xVec3*)&loc;
                pool_size->assign(size * p->xflip, size * p->yflip);
            }

            ++p;
        }

        _fptank_pool.used = _fptank_used;
        _fptank_pool.pos = pool_pos;
        _fptank_pool.color = pool_color;
        _fptank_pool.size = pool_size;

        return p - (fire_smoke_particle*)mem;
    }
} // namespace

void zParticleCustomEmitFireSmoke(const xVec3* loc, const xVec3* vel, S32 amount, F32 life_min,
                                  F32 life_range, F32 size_min, F32 size_range, F32 dt)
{
    F32 age;
    F32 dage;
    U8* mem;
    S32 have;
    fire_smoke_particle* p;
    fire_smoke_particle* endp;

    life_range *= 2.3283064e-10f;
    size_range *= 2.3283064e-10f;
    age = 0.0f;
    dage = dt / amount;

    while (amount > 0)
    {
        have = xParticleBatchEmit(system_id.fire_smoke, amount, &mem);
        if (have == 0)
        {
            break;
        }

        p = (fire_smoke_particle*)mem;
        endp = p + have;

        while (p != endp)
        {
            // life, size and r are not PS2 DWARF locals; both random calls come before any store
            // to *p.
            F32 life = life_min + life_range * xrand_RandomInt32();
            F32 size = size_min + size_range * xrand_RandomInt32();
            p->size = size;
            p->age = age;
            p->age_rate = 1.0f / life;
            p->vel = *vel;
            p->loc.x = loc->x + vel->x * age;
            p->loc.y = loc->y + vel->y * age;
            p->loc.z = loc->z + vel->z * age;

            U32 r = xrand_RandomInt32();
            p->xflip = 1 - ((r >> 1) & 0x2);
            p->yflip = 1 - ((r >> 2) & 0x2);

            age += dage;
            ++loc;
            ++vel;
            ++p;
        }

        amount -= have;
    }
}

void zParticleCustomSceneEnter()
{
    particle_batch_group.create();

    {
        xParticleBatchSystem system;
        system.pool_type = PGT_POS_COLOR_SIZE;
        system.rs.flags = 0xC;
        system.rs.src_blend = rwBLENDONE;
        system.rs.dst_blend = rwBLENDONE;
        system.rs.texture = (RwTexture*)xSTFindAsset(0xE2F60F70, NULL);
        system.order_group = 'HOBO';
        system.order_index = 8;
        system.stride = sizeof(spark_particle);
        system.update = update_spark_particles;
        system.context = NULL;
        system_id.spark = particle_batch_group.create_system(system);
    }

    {
        xParticleBatchSystem system;
        system.pool_type = PGT_COLOR_MAT;
        system.rs.flags = 0xC;
        system.rs.src_blend = rwBLENDONE;
        system.rs.dst_blend = rwBLENDONE;
        system.rs.texture = (RwTexture*)xSTFindAsset(0xE2F60F70, NULL);
        system.order_group = 'HOBO';
        system.order_index = 8;
        system.stride = sizeof(glint_particle);
        system.update = update_glint_particles;
        system.context = NULL;
        system_id.glint = particle_batch_group.create_system(system);
    }

    {
        xParticleBatchSystem system;
        system.pool_type = PGT_POS_COLOR_SIZE;
        system.rs.flags = 0x6;
        system.rs.src_blend = rwBLENDSRCALPHA;
        system.rs.dst_blend = rwBLENDINVSRCALPHA;
        system.rs.texture = (RwTexture*)xSTFindAsset(0x4E81027C, NULL);
        system.order_group = 'HOBO';
        system.order_index = 0;
        system.stride = sizeof(pebble_particle);
        system.update = update_pebble_particles;
        system.context = NULL;
        system_id.pebble = particle_batch_group.create_system(system);
    }

    {
        xParticleBatchSystem system;
        system.pool_type = PGT_POS_COLOR_SIZE_UV2;
        system.rs.flags = 0x4;
        system.rs.src_blend = rwBLENDSRCALPHA;
        system.rs.dst_blend = rwBLENDINVSRCALPHA;
        system.rs.texture = (RwTexture*)xSTFindAsset(0xDCDD4B8B, NULL);
        system.order_group = 'HOBO';
        system.order_index = 6;
        system.stride = sizeof(dust_particle);
        system.update = update_dust_particles;
        system.context = NULL;
        system_id.dust = particle_batch_group.create_system(system);
    }

    {
        xParticleBatchSystem system;
        system.pool_type = PGT_POS_COLOR_SIZE;
        system.rs.flags = 0x4;
        system.rs.src_blend = rwBLENDSRCALPHA;
        system.rs.dst_blend = rwBLENDINVSRCALPHA;
        system.rs.texture = (RwTexture*)xSTFindAsset(0x453C0740, NULL);
        system.order_group = 'HOBO';
        system.order_index = 6;
        system.stride = sizeof(fire_smoke_particle);
        system.update = update_fire_smoke_particles;
        system.context = NULL;
        system_id.fire_smoke = particle_batch_group.create_system(system);
    }
}

void zParticleCustomUpdate(F32 dt)
{
    dust_vel_decay = std::expf(-5.0f * dt);
}
