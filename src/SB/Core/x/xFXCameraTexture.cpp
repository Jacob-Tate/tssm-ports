#include "xFX.h"

#include "iCamera.h"
#include "iDraw.h"
#include "xLightKit.h"
#include "xMemMgr.h"
#include "zGlobals.h"
#include "zRenderState.h"

#include <rwcore.h>
#include <rpworld.h>
#include <string.h>
#include <types.h>

namespace
{
    // No symbol: inlined into render_background (same helper as BfBB's
    // zNPCTypePrawn television::set_vert).
    inline void set_vert(rwGameCube2DVertex& vert, F32 x, F32 y, F32 u, F32 v)
    {
        vert.x = x;
        vert.y = y;
        vert.z = 1.0f;
        vert.u = u;
        vert.v = v;
        vert.emissiveColor.red = 0xff;
        vert.emissiveColor.green = 0xff;
        vert.emissiveColor.blue = 0xff;
        vert.emissiveColor.alpha = 0xff;
    }

    // No symbol: inlined twice into create. Smallest power-of-two exponent covering size.
    inline S32 texture_bits(S32 size)
    {
        S32 bits = 0;
        S32 n = size;
        while (n > 1)
        {
            n >>= 1;
            bits++;
        }
        if ((1 << bits) < size)
        {
            bits++;
        }
        return bits;
    }
} // namespace

U8 xFXCameraTexture::create(S32 w, S32 h, bool need_zwrite, S32 bpp, S32 zbpp)
{
    cam = NULL;
    zraster = NULL;
    bgraster = NULL;
    raster = NULL;
    world = NULL;
    memset(&bgcolor, 0, sizeof(bgcolor));

    this->w = w;
    this->h = h;

    cam = iCameraRwCreate();
    if (cam == NULL)
    {
        destroy();
        return FALSE;
    }

    RwBBox bbox;
    bbox.sup.x = bbox.sup.y = bbox.sup.z = 100000.0f;
    bbox.inf.x = bbox.inf.y = bbox.inf.z = -100000.0f;

    world = RpWorldCreate(&bbox);
    if (world == NULL)
    {
        destroy();
        return FALSE;
    }

    RpWorldAddCamera(world, cam);
    RwCameraSetFrame(cam, RwFrameCreate());

    RwV2d vw = { 1.0f, 1.0f };
    RwCameraSetViewWindow(cam, &vw);
    RwCameraSetProjection(cam, rwPERSPECTIVE);
    RwCameraSetNearClipPlane(cam, 0.3f);
    RwCameraSetFarClipPlane(cam, 10000.0f);

    if (RwCameraGetFrame(cam) == NULL)
    {
        destroy();
        return FALSE;
    }

    raster = RwRasterCreate(w, h, bpp, rwRASTERTYPECAMERATEXTURE);
    if (raster == NULL)
    {
        destroy();
        return FALSE;
    }

    if (RwCameraSetRaster(cam, raster) == NULL)
    {
        destroy();
        return FALSE;
    }

    texture = RwTextureCreate(raster);
    if (texture == NULL)
    {
        destroy();
        return FALSE;
    }
    RwTextureSetFilterMode(texture, rwFILTERLINEAR);

    if (need_zwrite)
    {
        zraster = RwRasterCreate(w, h, zbpp, rwRASTERTYPECAMERATEXTURE | rwRASTERDONTALLOCATE);
        if (zraster == NULL)
        {
            destroy();
            return FALSE;
        }
    }

    max_tw = texture_bits(w);
    max_th = texture_bits(h);

    return TRUE;
}

void xFXCameraTexture::destroy()
{
    if (texture != NULL)
    {
        RwTextureDestroy(texture);
    }
    else if (raster != NULL)
    {
        RwRasterDestroy(raster);
    }

    if (zraster != NULL)
    {
        RwRasterDestroy(zraster);
    }

    if (cam != NULL)
    {
        RwFrame* tmpframe = RwCameraGetFrame(cam);
        if (tmpframe != NULL)
        {
            RwCameraSetFrame(cam, NULL);
            _rwFrameSyncDirty();
            RwFrameDestroy(tmpframe);
        }
        RpWorldRemoveCamera(world, cam);
        RwCameraDestroy(cam);
    }

    if (world != NULL)
    {
        RpWorldDestroy(world);
    }

    cam = NULL;
    raster = NULL;
    world = NULL;
}

void xFXCameraTexture::begin_render(S32 clear_flags, U32 fbmsk_write, U32 fbmsk_finish,
                                    F32 near_plane, F32 far_plane)
{
    oldworld = globals.currWorld;
    xLightKit_Enable(NULL, oldworld);
    globals.currWorld = world;

    oldcam = RwCameraGetCurrentCamera();
    if (oldcam != NULL)
    {
        RwCameraEndUpdate(oldcam);
        RwGameCubeCameraTextureFlush(oldcam->frameBuffer, 0);
    }

    old_near_plane = old_far_plane = 0.0f;

    if (near_plane > 0.0f)
    {
        if (oldcam != NULL)
        {
            old_near_plane = RwCameraGetNearClipPlane(oldcam);
        }
        RwCameraSetNearClipPlane(cam, near_plane);
    }

    if (far_plane > 0.0f)
    {
        if (oldcam != NULL)
        {
            old_far_plane = RwCameraGetFarClipPlane(oldcam);
        }
        RwCameraSetFarClipPlane(cam, far_plane);
    }

    if (fbmsk_write != lastSetFBMSK)
    {
        iDrawSetFBMSK(fbmsk_write, 0xFFFFFFFF, true);
    }

    RwCameraClear(cam, &bgcolor.rgba, clear_flags);
    RwFrameGetLTM(RwCameraGetFrame(cam))->flags |= rwMATRIXTYPEORTHONORMAL;
    RwCameraBeginUpdateWrapper(cam);

    if ((clear_flags & rwCAMERACLEARIMAGE) && bgraster != NULL)
    {
        render_background();
    }
}

void xFXCameraTexture::end_render()
{
    xLightKit_Enable(NULL, world);
    globals.currWorld = oldworld;

    RwCameraEndUpdate(cam);
    RwGameCubeCameraTextureFlush(cam->frameBuffer, 0);

    if (oldcam != NULL)
    {
        if (old_near_plane > 0.0f)
        {
            RwCameraSetNearClipPlane(oldcam, old_near_plane);
        }
        if (old_far_plane > 0.0f)
        {
            RwCameraSetFarClipPlane(oldcam, old_far_plane);
        }
        RwCameraBeginUpdateWrapper(oldcam);
    }
}

void xFXCameraTexture::render_background()
{
    zRenderState(SDRS_Fill);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, bgraster);

    rwGameCube2DVertex* vert = (rwGameCube2DVertex*)xMemPushTemp(6 * sizeof(rwGameCube2DVertex));

    set_vert(vert[0], 0.0f, 0.0f, 0.0f, 0.0f);
    set_vert(vert[1], 0.0f, h, 0.0f, 1.0f);
    set_vert(vert[2], w, 0.0f, 1.0f, 0.0f);
    vert[3] = vert[2];
    vert[4] = vert[1];
    set_vert(vert[5], w, h, 1.0f, 1.0f);

    RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, vert, 6);
    xMemPopTemp(vert);
}

// Unreferenced on GC (its PS2 caller xModelBlur::render does not exist here), so the linker
// dead-strips it.
void xFXCameraTexture::setup_subview(RwCamera* src_cam, const xFRect& src_rect,
                                     const xFRect& dst_rect)
{
    RwCameraSetNearClipPlane(cam, RwCameraGetNearClipPlane(src_cam));
    RwCameraSetFarClipPlane(cam, RwCameraGetFarClipPlane(src_cam));
    RwCameraSetFogDistance(cam, RwCameraGetFogDistance(src_cam));
    RwCameraSetProjection(cam, RwCameraGetProjection(src_cam));

    xMat4x3 ltm;
    xVec2 view_window;
    xVec2 view_offset;
    get_subview(ltm, view_window, view_offset, src_cam, src_rect, dst_rect);

    RwFrameTransform(RwCameraGetFrame(cam), (const RwMatrix*)&ltm, rwCOMBINEREPLACE);
    RwCameraSetViewWindow(cam, (const RwV2d*)&view_window);
    RwCameraSetViewOffset(cam, (const RwV2d*)&view_offset);
}

// Unreferenced on GC (only setup_subview calls it), so the linker dead-strips it. Its 1.0f
// still comes before render_background's 0.0f in .sdata2, as in retail.
void xFXCameraTexture::get_subview(xMat4x3& ltm, xVec2& view_window, xVec2& view_offset,
                                   RwCamera* src_cam, const xFRect& src_rect,
                                   const xFRect& dst_rect) const
{
    RwRaster* src_raster = RwCameraGetRaster(src_cam);
    const xMat4x3& src_ltm = *(const xMat4x3*)RwFrameGetMatrix(RwCameraGetFrame(src_cam));
    const xVec2& src_view_window = *(const xVec2*)RwCameraGetViewWindow(src_cam);
    const xVec2& src_view_offset = *(const xVec2*)RwCameraGetViewOffset(src_cam);

    F32 src_iwidth = 1.0f / src_raster->originalWidth;
    F32 src_iheight = 1.0f / src_raster->originalHeight;
    F32 dst_iwidth = 1.0f / raster->originalWidth;
    F32 dst_iheight = 1.0f / raster->originalHeight;

    F32 rw = src_rect.w * src_iwidth * src_view_window.x / (dst_rect.w * dst_iwidth);
    F32 rh = src_rect.h * src_iheight * src_view_window.y / (dst_rect.h * dst_iheight);

    F32 mx = -2.0f * (0.5f * rw + ((src_rect.x * src_iwidth - 0.5f) * src_view_window.x -
                                   dst_rect.x * dst_iwidth * rw));
    F32 my = -2.0f * (0.5f * rh + ((src_rect.y * src_iheight - 0.5f) * src_view_window.y -
                                   dst_rect.y * dst_iheight * rh));

    *(xMat3x3*)&ltm = src_ltm;
    ltm.pos.x = src_ltm.pos.x + ltm.right.x * mx + ltm.up.x * my;
    ltm.pos.y = src_ltm.pos.y + ltm.right.y * mx + ltm.up.y * my;
    ltm.pos.z = src_ltm.pos.z + ltm.right.z * mx + ltm.up.z * my;

    view_window.assign(rw, rh);
    view_offset.assign(src_view_offset.x + mx, src_view_offset.y - my);
}

namespace
{
    // No symbol: inlined into xFXFastRaster::clip_rect (Ratatouille GC DWARF: static clip_rect).
    inline bool clip_rect(S32& x, S32& y, S32& w, S32& h, S32 wclip, S32 hclip)
    {
        if (w <= 0 || x >= wclip || x + w <= 0 || h <= 0 || y >= hclip || y + h <= 0)
        {
            return false;
        }

        if (x < 0)
        {
            w += x;
            x = 0;
        }

        if (x + w > wclip)
        {
            w = wclip - x;
        }

        if (y < 0)
        {
            h += y;
            y = 0;
        }

        if (y + h > hclip)
        {
            h = hclip - y;
        }

        return true;
    }
} // namespace

// Unreferenced on GC (its PS2 callers in xModelBlur do not exist here), so the linker strips it.
// Its int->float conversions still create the unit's first .sdata2 literal, as in retail.
bool xFXFastRaster::clip_rect(xFRect& dst_rect, RwRaster* dst_raster)
{
    S32 width = dst_raster->originalWidth * 16;
    S32 height = dst_raster->originalHeight * 16;
    S32 dst_x = 16.0f * dst_rect.x;
    S32 dst_w = 16.0f * dst_rect.w;
    S32 dst_y = 16.0f * dst_rect.y;
    S32 dst_h = 16.0f * dst_rect.h;
    S32 x = dst_x;
    S32 y = dst_y;
    S32 w = dst_w;
    S32 h = dst_h;

    if (!::clip_rect(x, y, w, h, width, height))
    {
        return false;
    }

    if (x != dst_x)
    {
        dst_rect.x = 1 / 16.0f * x;
    }

    if (w != dst_w)
    {
        dst_rect.w = 1 / 16.0f * w;
    }

    if (y != dst_y)
    {
        dst_rect.y = 1 / 16.0f * y;
    }

    if (h != dst_h)
    {
        dst_rect.h = 1 / 16.0f * h;
    }

    return true;
}

// Unreferenced on GC, dead-stripped like the overload above.
bool xFXFastRaster::clip_rect(xFRect& dst_rect, RwRaster* dst_raster, xFRect& src_rect)
{
    F32 oldx = dst_rect.x;
    F32 oldw = dst_rect.w;
    F32 oldy = dst_rect.y;
    F32 oldh = dst_rect.h;

    if (!clip_rect(dst_rect, dst_raster))
    {
        return false;
    }

    if (dst_rect.x != oldx)
    {
        src_rect.x += src_rect.w / oldw * (dst_rect.x - oldx);
    }

    if (dst_rect.w != oldw)
    {
        src_rect.w += src_rect.w / oldw * (dst_rect.w - oldw);
    }

    if (dst_rect.y != oldy)
    {
        src_rect.y += src_rect.h / oldh * (dst_rect.y - oldy);
    }

    if (dst_rect.h != oldh)
    {
        src_rect.h += src_rect.h / oldh * (dst_rect.h - oldh);
    }

    return true;
}
