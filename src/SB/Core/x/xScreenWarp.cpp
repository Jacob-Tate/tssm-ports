#include "xScreenWarp.h"

#include <string.h>
#include <math.h>
#include <rwcore.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>


#include "iMath.h"
#include "iMemMgr.h"
#include "xMath.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xString.h"
#include "xRand.h"

using namespace xScreenWarp;

namespace
{
    xVec2* uvmap_buffer;
    F32 uvmap_uoffset;
    F32 uvmap_voffset;
    F32 uvmap_uscale;
    F32 uvmap_vscale;
    F32 uvmap_udelta;
    F32 uvmap_vdelta;
    F32 uvmap_xscale;
    F32 uvmap_yscale;
    F32 uvmap_xdelta;
    F32 uvmap_ydelta;
    U32 common_warp_ids[MAX_TYPE];
    warper warp_buffer[128];
    warper warp_lists[MAX_TYPE];
    warper* warp_stack;
    U8 render_this_frame;

    void render_uvmap(RwCamera* cam);
    void warp_animate_static_lens(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                                  F32 base_intensity, F32, F32);
    void warp_animate_heat_shimmer(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                                   F32 base_intensity, F32, F32 s);
    void warp_animate_shockwave(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                                F32 base_intensity, F32, F32 s);
    void warp_animate_ripple(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                             F32 base_intensity, F32, F32 s);
    void warp_animate_lens(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                           F32 base_intensity, F32, F32 s);
    void warp_animate_warble(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                             F32 base_intensity, F32, F32 s);
    U8 animate_uvmap(xVec2* uvmap, RwCamera* cam);
} // namespace

char* xScreenWarp::common_warp_names[MAX_TYPE + 1] = {
    "Warble", "Lens", "Ripple", "Shockwave", "Heat Shimmer", "Static Lens", "Random",
};

void (*xScreenWarp::common_warp_animate[MAX_TYPE])(xVec2*, warper&, const xVec2&, F32, F32, F32,
                                                   F32) = {
    warp_animate_warble,    warp_animate_lens,         warp_animate_ripple,
    warp_animate_shockwave, warp_animate_heat_shimmer, warp_animate_static_lens,
};

namespace
{
    inline void get_uvmap_range(S32& istart, S32& iend, S32& jstart, S32& jend, F32 x, F32 y, F32 r)
    {
        istart = (S32)(16.0f * (x - r));
        if (istart < 0)
        {
            istart = 0;
        }

        iend = (S32)(16.0f * (x + r)) + 2;
        if (iend > 16)
        {
            iend = 16;
        }

        jstart = (S32)(16.0f * (y - r));
        if (jstart < 0)
        {
            jstart = 0;
        }

        jend = (S32)(16.0f * (y + r)) + 2;
        if (jend > 16)
        {
            jend = 16;
        }
    }

    inline warper* alloc_warp(type_enum type)
    {
        warper* warp = warp_stack;
        warp_stack = warp->next;

        warper& head = warp_lists[type];
        warp->next = head.next;
        warp->prev = &head;
        if (head.next != NULL)
        {
            head.next->prev = warp;
        }
        head.next = warp;

        return warp;
    }

    inline void free_warp(warper* warp)
    {
        if (warp->next != NULL)
        {
            warp->next->prev = warp->prev;
        }
        warp->prev->next = warp->next;

        warp->next = warp_stack;
        warp_stack = warp;
    }

    inline void change_warp_type(warper* warp, type_enum type)
    {
        if (warp->next != NULL)
        {
            warp->next->prev = warp->prev;
        }
        warp->prev->next = warp->next;

        warper& head = warp_lists[type];
        warp->next = head.next;
        warp->prev = &head;
        if (head.next != NULL)
        {
            head.next->prev = warp;
        }
        head.next = warp;

        warp->type = type;
    }

    inline void set_warp(warper& warp, F32 radius, F32 duration, type_enum type, F32 intensity,
                         F32 freq, S32 flags)
    {
        warp.iduration = 1.0f / duration;
        warp.radius = radius;
        warp.type = type;
        warp.intensity = intensity;
        warp.freq = freq;
        warp.flags = flags;
    }

    inline warper* create_warp(F32 radius, F32 duration, type_enum type, F32 intensity, F32 freq,
                               S32 flags)
    {
        warper* warp = alloc_warp(type);
        set_warp(*warp, radius, duration, type, intensity, freq, flags);
        warp->life = duration;
        return warp;
    }

    inline void clear()
    {
        memset(warp_lists, 0, sizeof(warp_lists));

        warper* warp = warp_buffer;
        warper* end_warp = warp_buffer + 127;
        while (warp != end_warp)
        {
            warp->next = warp + 1;
            warp++;
        }
        warp->next = NULL;

        warp_stack = warp_buffer;
    }

    inline void reset_uvmap(xVec2* uvmap)
    {
        xVec2* uv = uvmap;
        F32 du = uvmap_udelta;
        F32 dv = uvmap_vdelta;
        F32 u = uvmap_uoffset;

        for (S32 i = 0; i < 17; i++)
        {
            F32 v = uvmap_voffset;

            for (S32 j = 0; j < 17; j++, uv++)
            {
                uv->x = u;
                uv->y = v;
                v += dv;
            }

            u += du;
        }
    }

    U8 animate_uvmap(xVec2* uvmap, RwCamera* cam)
    {
        xMat4x3& view_mat = *(xMat4x3*)&cam->viewMatrix;
        U8 need_render = FALSE;

        for (S32 type = 0; type < MAX_TYPE; type++)
        {
            void (*animate_cb)(xVec2*, warper&, const xVec2&, F32, F32, F32, F32) =
                common_warp_animate[type];

            for (warper* warp = warp_lists[type].next; warp != NULL; warp = warp->next)
            {
                xVec2 loc;
                F32 radius;
                F32 iz;

                if (warp->flags & 0x2)
                {
                    loc = (warp->flags & 0x1) ? *warp->ploc2 : warp->loc2;
                    iz = 1.0f;
                    radius = warp->radius;
                }
                else
                {
                    xVec3 loc3;
                    if (warp->flags & 0x1)
                    {
                        xMat4x3Toworld(&loc3, &view_mat, warp->ploc3);
                    }
                    else
                    {
                        xMat4x3Toworld(&loc3, &view_mat, &warp->loc3);
                    }

                    if (loc3.z <= 0.01f)
                    {
                        continue;
                    }

                    iz = 1.0f / loc3.z;
                    loc.x = loc3.x * iz;
                    loc.y = loc3.y * iz;
                    radius = warp->radius * iz;
                }

                if (radius <= 0.1f)
                {
                    continue;
                }

                if (loc.x - radius > 1.0f)
                {
                    continue;
                }

                if (loc.x + radius < 0.0f)
                {
                    continue;
                }

                if (loc.y - radius > 1.0f)
                {
                    continue;
                }

                if (loc.y + radius < 0.0f)
                {
                    continue;
                }

                if (!need_render)
                {
                    reset_uvmap(uvmap);
                    need_render = TRUE;
                }

                F32 intensity;
                if (radius > 0.2f)
                {
                    intensity = warp->intensity;
                }
                else
                {
                    intensity = 10.0f * (warp->intensity * (radius - 0.1f));
                }

                loc.x *= uvmap_xscale;
                loc.y *= uvmap_yscale;

                animate_cb(uvmap, *warp, loc, radius, intensity, iz, warp->life * warp->iduration);
            }
        }

        return need_render;
    }
} // namespace

void xScreenWarp::scene_enter()
{
    uvmap_uscale = 1.0f;
    uvmap_vscale = 1.0f;
    uvmap_xscale = 1.0f;
    uvmap_yscale = (F32)FB_YRES / (F32)FB_XRES;
    uvmap_voffset = 0.0f;
    uvmap_uoffset = 0.0f;
    uvmap_udelta = 1.0f / 16;
    uvmap_vdelta = 1.0f / 16;
    uvmap_xdelta = uvmap_xscale * uvmap_udelta;
    uvmap_ydelta = uvmap_yscale * uvmap_vdelta;

    for (S32 i = 0; i < MAX_TYPE; i++)
    {
        common_warp_ids[i] = xStrHash(common_warp_names[i]);
    }

    reset();
}

void xScreenWarp::scene_exit()
{
}

void xScreenWarp::reset()
{
    clear();
}

void xScreenWarp::update(F32 dt)
{
    for (S32 type = 0; type < MAX_TYPE; type++)
    {
        warper* warp = warp_lists[type].next;
        while (warp != NULL)
        {
            warper* next_warp = warp->next;

            warp->life -= dt;

            if (warp->flags & 0x4)
            {
                if (warp->life < 0.0f)
                {
                    warp->life *= warp->iduration;
                    warp->life -= (S32)warp->life - 1.0f;
                    warp->life /= warp->iduration;
                }
            }
            else if (warp->life <= 0.0f)
            {
                remove(warp);
            }

            warp = next_warp;
        }
    }
}

void xScreenWarp::pre_render()
{
}

void xScreenWarp::render(RwCamera* cam)
{
    uvmap_buffer = (xVec2*)iMemPushTemp(sizeof(xVec2) * (17 * 17 + 1));
    if (uvmap_buffer != NULL)
    {
        render_this_frame = animate_uvmap(uvmap_buffer, cam);
        if (render_this_frame)
        {
            render_uvmap(cam);
        }

        iMemPopTemp(uvmap_buffer);
    }
}

type_enum xScreenWarp::find_type(U32 id)
{
    for (S32 i = 0; i < MAX_TYPE; i++)
    {
        if (id == common_warp_ids[i])
        {
            return (type_enum)i;
        }
    }

    return TYPE_INVALID;
}

warper* xScreenWarp::emit(const xVec3& loc, F32 radius, F32 duration, type_enum type, F32 intensity,
                          F32 freq, S32 flags)
{
    if (warp_stack == NULL)
    {
        return NULL;
    }

    warper* warp = create_warp(radius, duration, type, intensity, freq, flags);

    if (warp->flags & 0x1)
    {
        warp->ploc3 = &loc;
    }
    else
    {
        warp->loc3 = loc;
    }

    return (warp->flags & 0x4) ? warp : NULL;
}

void xScreenWarp::modify(warper* warp, const xVec3& loc, F32 radius, F32 duration, type_enum type,
                         F32 intensity, F32 freq, S32 flags)
{
    if (warp->type != type)
    {
        change_warp_type(warp, type);
    }

    set_warp(*warp, radius, duration, type, intensity, freq, flags);

    if (warp->life > duration)
    {
        warp->life = fmod(warp->life, duration);
    }

    if (warp->flags & 0x1)
    {
        warp->ploc3 = &loc;
    }
    else
    {
        warp->loc3 = loc;
    }
}

void xScreenWarp::remove(warper* warp)
{
    free_warp(warp);
}

namespace
{
    void warp_animate_warble(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                             F32 base_intensity, F32, F32 s)
    {
        F32 random_u;
        F32 random_v;
        if (warp.flags & 0x4)
        {
            random_u = 0.00625f * base_intensity;
            random_v = 0.00625f * base_intensity;
        }
        else
        {
            base_intensity = s * base_intensity;
            random_u = 0.00625f * base_intensity;
            random_v = 0.00625f * base_intensity;
        }

        F32 r2 = radius * radius;
        F32 ir2 = 1.0f / r2;
        random_u *= uvmap_uscale;
        random_v *= uvmap_vscale;

        S32 istart, iend, jstart, jend;
        get_uvmap_range(istart, iend, jstart, jend, loc.x, loc.y, radius);

        F32 dx = uvmap_xdelta;
        F32 dy = uvmap_ydelta;
        F32 x = dx * istart;

        for (S32 i = istart; i < iend; i++)
        {
            F32 y = dy * jstart;

            for (S32 j = jstart; j < jend; j++, y += dy)
            {
                F32 xoff = x - loc.x;
                F32 yoff = y - loc.y;
                F32 dist2 = xoff * xoff + yoff * yoff;

                if (dist2 >= r2)
                {
                    continue;
                }

                F32 intensity = 1.0f - dist2 * ir2;
                (uvmap + i * 17 + j)->x += intensity * (random_u * (2.0f * xurand() - 1.0f));
                (uvmap + i * 17 + j)->y += intensity * (random_v * (2.0f * xurand() - 1.0f));
            }

            x += dx;
        }
    }

    void warp_animate_lens(xVec2* uvmap, warper&, const xVec2& loc, F32 radius, F32 base_intensity,
                           F32, F32 s)
    {
        F32 mag = 0.02f * (base_intensity * (s * isin(3.0f * PI * (1.0f - s))));
        F32 r2 = radius * radius;
        F32 ir2 = 1.0f / r2;
        F32 umag = uvmap_uscale * mag;
        F32 vmag = uvmap_vscale * mag;

        S32 istart, iend, jstart, jend;
        get_uvmap_range(istart, iend, jstart, jend, loc.x, loc.y, radius);

        F32 dx = uvmap_xdelta;
        F32 dy = uvmap_ydelta;
        F32 x = dx * istart;

        for (S32 i = istart; i < iend; i++)
        {
            F32 y = dy * jstart;

            for (S32 j = jstart; j < jend; j++, y += dy)
            {
                F32 xoff = x - loc.x;
                F32 yoff = y - loc.y;
                F32 dist2 = xoff * xoff + yoff * yoff;

                if (dist2 >= r2)
                {
                    continue;
                }

                F32 scale = 1.0f - dist2 * ir2;
                (uvmap + i * 17 + j)->x -= umag * (xoff * scale);
                (uvmap + i * 17 + j)->y -= vmag * (yoff * scale);
            }

            x += dx;
        }
    }

    void warp_animate_ripple(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                             F32 base_intensity, F32, F32 s)
    {
        F32 mag;
        if (warp.flags & 0x4)
        {
            mag = 0.1f * base_intensity;
        }
        else
        {
            mag = 1.0f - 4.0f * (s - 0.5f) * (s - 0.5f);
            mag = 0.1f * (mag * base_intensity);
        }

        F32 umag = uvmap_uscale * mag;
        F32 vmag = uvmap_vscale * mag;
        F32 freq = 2.0f * PI * warp.freq;
        F32 r2 = radius * radius;
        F32 ir2 = 1.0f / r2;

        S32 istart, iend, jstart, jend;
        get_uvmap_range(istart, iend, jstart, jend, loc.x, loc.y, radius);

        F32 dx = uvmap_xdelta;
        F32 dy = uvmap_ydelta;
        F32 x = dx * istart;

        for (S32 i = istart; i < iend; i++)
        {
            F32 y = dy * jstart;

            for (S32 j = jstart; j < jend; j++, y += dy)
            {
                F32 xoff = x - loc.x;
                F32 yoff = y - loc.y;
                F32 dist2 = xoff * xoff + yoff * yoff;

                if (dist2 >= r2)
                {
                    continue;
                }

                F32 t = 1.0f - dist2 * ir2;
                F32 scale = t * isin(freq * (t + (1.0f - s)));
                (uvmap + i * 17 + j)->x -= umag * (xoff * scale);
                (uvmap + i * 17 + j)->y -= vmag * (yoff * scale);
            }

            x += dx;
        }
    }

    // The sine is Ratatouille's form (xsinfast, then divided by the distance); xsinfast's fASqr
    // local gives retail's register allocation.
    void warp_animate_shockwave(xVec2* uvmap, warper&, const xVec2& loc, F32 radius,
                                F32 base_intensity, F32, F32 s)
    {
        F32 mag = 0.02f * (s * base_intensity);
        F32 umag = uvmap_uscale * mag;
        F32 vmag = uvmap_vscale * mag;
        F32 r2 = radius * radius;
        F32 ir = 1.0f / radius;

        S32 istart, iend, jstart, jend;
        get_uvmap_range(istart, iend, jstart, jend, loc.x, loc.y, radius);

        F32 dx = uvmap_xdelta;
        F32 dy = uvmap_ydelta;
        F32 x = dx * istart;

        for (S32 i = istart; i < iend; i++)
        {
            F32 y = dy * jstart;

            for (S32 j = jstart; j < jend; j++, y += dy)
            {
                F32 xoff = x - loc.x;
                F32 yoff = y - loc.y;
                F32 dist2 = xoff * xoff + yoff * yoff;

                if (dist2 >= r2 || dist2 <= 0.00001f)
                {
                    continue;
                }

                dist2 = xsqrt(dist2);
                F32 theta = -5.0f * PI * (1.0f - s) + 3.0f * PI * (dist2 * ir);
                theta = MAX(-PI, MIN(theta, PI));

                F32 stheta;
                xsinfast(stheta, theta);
                F32 scale = stheta / dist2;
                (uvmap + i * 17 + j)->x -= umag * (xoff * scale);
                (uvmap + i * 17 + j)->y -= vmag * (yoff * scale);
            }

            x += dx;
        }
    }

    void warp_animate_heat_shimmer(xVec2* uvmap, warper& warp, const xVec2& loc, F32 radius,
                                   F32 base_intensity, F32, F32 s)
    {
        F32 mag = uvmap_uscale * (0.015f * base_intensity);
        if (!(warp.flags & 0x4))
        {
            mag *= s;
        }

        F32 start_theta = 8.0f * PI * (s * warp.freq);

        S32 istart, iend, jstart, jend;
        get_uvmap_range(istart, iend, jstart, jend, loc.x, loc.y, radius);

        // Constant-propagated out of the PS2 DWARF; through these locals retail interns 0.5f * PI
        // before 0.87266463f.
        F32 dthetax = 0.5f * PI;
        F32 dthetay = 0.87266463f;
        F32 phase = xrmod(start_theta + dthetax * istart + dthetay * jstart + PI) - PI;
        F32 startcos0 = mag * icos(phase);
        F32 startsin0 = mag * isin(phase);

        F32 ir = 1.0f / radius;
        F32 dx = uvmap_xdelta * ir;
        F32 dy = uvmap_ydelta * ir;
        F32 ystart = ir * (uvmap_ydelta * jstart - loc.y);
        S32 ysize = jend - jstart;
        xVec2* uv = uvmap + istart * 17 + jstart;
        xVec2* uv_xend = uv + (iend - istart) * 17;
        S32 uvstride = 17 - ysize;
        F32 x = ir * (uvmap_xdelta * istart - loc.x);

        while (uv < uv_xend)
        {
            F32 tempcos;
            F32 cos0 = startcos0;
            F32 sin0 = startsin0;
            F32 y = ystart;
            F32 x2 = x * x;
            xVec2* uv_yend = uv + ysize;

            while (uv < uv_yend)
            {
                F32 dist2 = x2 + y * y;
                if (dist2 < 1.0f)
                {
                    uv->x += (1.0f - dist2) * sin0;
                }

                uv++;
                y += dy;

                tempcos = cos0;
                cos0 = 0.6440866f * cos0 - 0.7649526f * sin0;
                sin0 = 0.6440866f * sin0 + 0.7649526f * tempcos;
            }

            uv += uvstride;
            x += dx;

            tempcos = startcos0;
            startcos0 = -startsin0;
            startsin0 = tempcos;
        }
    }

    void warp_animate_static_lens(xVec2* uvmap, warper&, const xVec2& loc, F32 radius,
                                  F32 base_intensity, F32, F32)
    {
        F32 mag = 0.02f * base_intensity;
        F32 r2 = radius * radius;
        F32 ir2 = 1.0f / r2;
        F32 umag = uvmap_uscale * mag;
        F32 vmag = uvmap_vscale * mag;

        S32 istart, iend, jstart, jend;
        get_uvmap_range(istart, iend, jstart, jend, loc.x, loc.y, radius);

        F32 dx = uvmap_xdelta;
        F32 dy = uvmap_ydelta;
        F32 x = dx * istart;

        for (S32 i = istart; i < iend; i++)
        {
            F32 y = dy * jstart;

            for (S32 j = jstart; j < jend; j++, y += dy)
            {
                F32 xoff = x - loc.x;
                F32 yoff = y - loc.y;
                F32 dist2 = xoff * xoff + yoff * yoff;

                if (dist2 >= r2)
                {
                    continue;
                }

                F32 scale = 1.0f - dist2 * ir2;
                (uvmap + i * 17 + j)->x -= umag * (xoff * scale);
                (uvmap + i * 17 + j)->y -= vmag * (yoff * scale);
            }

            x += dx;
        }
    }

    void render_uvmap(RwCamera* cam)
    {
        void* frame_buffer_raw = iMemPushTemp(FB_XRES * FB_YRES * 2 + 31);
        if (frame_buffer_raw == NULL)
        {
            return;
        }

        void* frame_buffer = (void*)(((UPtr)frame_buffer_raw + 31) & ~31);

        GXSetTexCopySrc(0, 0, FB_XRES, FB_YRES);
        GXSetTexCopyDst(FB_XRES, FB_YRES, GX_TF_RGB565, GX_FALSE);
        GXCopyTex(frame_buffer, GX_FALSE);
        GXPixModeSync();

        // Retail 16-byte aligns the stack frame for these matrices.
        Mtx44 mProjection ALIGNB(16);
        C_MTXOrtho(mProjection, 0.0f, FB_YRES, 0.0f, FB_XRES, 0.0f, 1.0f);
        GXSetProjection(mProjection, GX_ORTHOGRAPHIC);
        GXSetCurrentMtx(GX_PNMTX0);

        Mtx44 mIdentity ALIGNB(16);
        PSMTXIdentity(mIdentity);
        GXLoadPosMtxImm(mIdentity, GX_PNMTX0);

        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetNumChans(0);
        RwGameCubeSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
        GXSetCullMode(GX_CULL_NONE);
        GXSetNumTevStages(1);

        GXTexObj tex;
        GXInitTexObj(&tex, frame_buffer, FB_XRES, FB_YRES, GX_TF_RGB565, GX_CLAMP, GX_CLAMP,
                     GX_FALSE);
        GXLoadTexObj(&tex, GX_TEXMAP0);

        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_ONE, GX_CC_ZERO);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_ZERO);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
        GXSetNumTexGens(1);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE,
                          GX_PTIDENTITY);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_SET);

        S32 dx = (S32)FB_XRES / 16;
        S32 dy = (S32)FB_YRES / 16;
        S32 x = 0;
        xVec2* uv0 = uvmap_buffer;
        xVec2* uv1 = uvmap_buffer + 17;

        // Retail draws 17 strips although the map only has 16 cells per side: the last strip is
        // off-screen and reads one row past the uvmap.
        for (S32 i = 0; i < 17; i++)
        {
            GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 34);

            S32 y = 0;
            for (S32 j = 0; j < 17; j++)
            {
                GXPosition2f32(x, y);
                GXTexCoord2f32(uv0->x, uv0->y);
                uv0++;

                GXPosition2f32(x + dx, y);
                GXTexCoord2f32(uv1->x, uv1->y);
                uv1++;

                y += dy;
            }

            GXEnd();
            x += dx;
        }

        GXDrawDone();
        iMemPopTemp(frame_buffer_raw);
    }
} // namespace
