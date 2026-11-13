// RenderWare C API: RwIm2D and RwIm3D.
//
// Immediate mode is how the game draws everything that is not a model: text,
// shadows, particles, the screen fades, the aura and streak effects. The calls
// themselves are thin -- librw's rw::im2d and rw::im3d take the same arguments
// in the same order -- but the VERTEX FORMAT is not settled, and that is the
// thing to read before touching this file.
//
// RwIm2DVertex was the thing that kept this file from compiling against a real
// backend, and it is now resolved. rwplcore.h declares it per backend: the
// console keeps rwGameCube2DVertex, and PLATFORM_PC gets a 28-byte struct with
// librw's field order under RenderWare's field names, asserted against the
// backend's own struct in layout_im2d.cpp.
//
//   RxObjSpace3DVertex   x y z  nx ny nz  colour (4 bytes)  u v
//   gl3::Im3DVertex      x y z  nx ny nz  b g r a (bytes)   u v    -- agrees
//
//   rwGameCube2DVertex   x y z        RwRGBA        u v            -- 24 bytes
//   gl3::Im2DVertex      x y z w      b g r a       u v            -- 28 bytes
//   d3d::Im2DVertex      x y z w      uint32 ARGB   u v            -- 28 bytes
//
// The 3D vertex happens to line up with GL3's and needed nothing. The 2D one
// gained the `w` both real backends carry, and RwIm2DVertexSetRecipCameraZ --
// a no-op macro on the console, because the GameCube driver had no use for it
// -- now writes it.
//
// The trap that cost the most to find is recorded at the struct in rwplcore.h
// and is worth repeating here: on D3D9 a vertex whose w is left at zero draws
// NOTHING, because librw's im2d vertex shader multiplies the position by w
// before the hardware divides by it. Most of the game's 2D call sites never set
// a camera z, so RwIm2DVertex's default constructor puts 1.0 there.

#include <rwcore.h>

#include "rw.h"

// After rw.h: iScreen.h brings in types.h, which breaks librw's rwengine.h when
// it comes first.
#include "iScreen.h"

#include <vector>

// PS2 is the one backend still refused. Its Im2DVertex is a different shape
// again -- fixed point, interleaved with the GIF tag -- and nothing here has
// been written or checked against it.
#if defined(RW_PS2)
#error "RwIm2DVertex has no PS2 layout. See layout_im2d.cpp before rendering through the PS2 device."
#endif

// The GameCube driver binds the render state's raster before every Im2D and Im3D
// primitive (_rwDlTextureRasterFlush). librw draws with whatever its stage holds,
// and a model's draw leaves its own texture there, as before the text that follows
// a {model:} tag.
static void BindStateRaster()
{
    void* raster = NULL;
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &raster);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, raster);
}

// ---------------------------------------------------------------------------
// Im2D

// The screen-space Z range the 2D rasteriser wants, which the game reads to put
// its overlays at the front or the back of the depth buffer (xFont.cpp:425,
// zGame.cpp:848, zEntPlayerOOBState.cpp:220). librw keeps them on the device --
// they are the depth range it hands the hardware -- and under
// LIBRW_PLATFORM=NULL that range is the null device's 0.0 to 1.0.
RwReal RwIm2DGetNearScreenZ(void)
{
    return rw::im2d::GetNearZ();
}

RwReal RwIm2DGetFarScreenZ(void)
{
    return rw::im2d::GetFarZ();
}

// The UI box (iScreen.h). The game lays its 2D out in pixels of the render
// size, as a 640x480 screen scaled to it, so on a screen that is not 4:3 every
// draw into the screen is mapped into the centred 4:3 box here rather than
// stretched across the screen. One place rather than each of the game's
// emitters (xFont, xHudImage, zUIImage, zUIBox, zTextBox, xCM, ...), so nothing
// is missed and the game code is untouched.
//
// Left alone: a draw inside a screen-space scope (a full-screen layer), and a
// draw into anything but the screen -- render-to-texture targets are
// CAMERATEXTURE rasters or another size. On a 4:3 render size the box is the
// screen and nothing is copied.
//
// One exception is decided here rather than at a call site: a single quad as
// wide as the screen (as tall, on a screen taller than 4:3). Textured, it is
// full-bleed art -- the title's sand and surf, the menus' and the pause
// screen's sunbursts, the screen transition -- and it COVERS the screen: one
// uniform scale, centred, so it fills the width and loses a strip top and
// bottom instead of being stretched or leaving bars. Untextured it is a fill
// or a dim, a colour with no shape to keep, and it is drawn as given.
namespace
{
    enum Placement
    {
        PLACE_AS_GIVEN,
        PLACE_BOX,
        PLACE_COVER
    };

    Placement PlacementFor(const RwIm2DVertex* vertices, RwInt32 numVertices)
    {
        if (!iScreenUIBoxed() || iScreen2DInScreenSpace())
        {
            return PLACE_AS_GIVEN;
        }

        RwCamera* camera = RwCameraGetCurrentCamera();
        RwRaster* raster = camera != NULL ? RwCameraGetRaster(camera) : NULL;
        if (raster == NULL || RwRasterGetType(raster) != rwRASTERTYPECAMERA ||
            RwRasterGetWidth(raster) != iScreenWidth() ||
            RwRasterGetHeight(raster) != iScreenHeight())
        {
            return PLACE_AS_GIVEN;
        }

        // A quad, as a strip, a fan or two triangles. Text and the like come in
        // bigger batches and are always boxed, however wide a line runs.
        if (numVertices > 6)
        {
            return PLACE_BOX;
        }

        // Which axis the box is short of; the other one it fills.
        const bool wide = iScreenUIFracXF() < 1.0f;
        const F32 extent = wide ? iScreenWidthF() : iScreenHeightF();
        F32 lo = wide ? vertices[0].x : vertices[0].y;
        F32 hi = lo;
        for (RwInt32 i = 1; i < numVertices; i++)
        {
            const F32 v = wide ? vertices[i].x : vertices[i].y;
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        if (lo > 0.5f || hi < extent - 0.5f)
        {
            return PLACE_BOX;
        }

        void* texture = NULL;
        RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &texture);
        return texture != NULL ? PLACE_COVER : PLACE_AS_GIVEN;
    }

    // The mapped copy. The game draws from one thread (iAsync.cpp), so one
    // buffer, kept between calls.
    std::vector<RwIm2DVertex> sMapped;

    RwIm2DVertex* ToUIBox(RwIm2DVertex* vertices, RwInt32 numVertices)
    {
        const Placement placement = PlacementFor(vertices, numVertices);
        if (placement == PLACE_AS_GIVEN)
        {
            return vertices;
        }

        // x' = ox + sx * x. The box: the box's origin and share of the screen.
        // Cover: the box scaled about the screen's centre until its short side
        // fills the screen, which leaves the long one cropped.
        const F32 fx = iScreenUIFracXF();
        const F32 fy = iScreenUIFracYF();
        F32 sx = fx;
        F32 sy = fy;
        F32 ox = iScreenUIOriginXF();
        F32 oy = iScreenUIOriginYF();
        if (placement == PLACE_COVER)
        {
            const F32 fill = fx < fy ? fx : fy;
            const F32 cx = 0.5f * iScreenWidthF();
            const F32 cy = 0.5f * iScreenHeightF();
            sx = fx / fill;
            sy = fy / fill;
            ox = cx - cx * sx;
            oy = cy - cy * sy;
        }

        sMapped.assign(vertices, vertices + numVertices);
        for (RwInt32 i = 0; i < numVertices; i++)
        {
            sMapped[i].x = ox + sMapped[i].x * sx;
            sMapped[i].y = oy + sMapped[i].y * sy;
        }
        return sMapped.data();
    }
} // namespace

RwBool RwIm2DRenderPrimitive(RwPrimitiveType primType, RwIm2DVertex* vertices,
                             RwInt32 numVertices)
{
    if (vertices == NULL || numVertices <= 0)
    {
        return FALSE;
    }

    vertices = ToUIBox(vertices, numVertices);
    BindStateRaster();
    rw::im2d::RenderPrimitive((rw::PrimitiveType)primType, vertices, numVertices);
    return TRUE;
}

RwBool RwIm2DRenderIndexedPrimitive(RwPrimitiveType primType, RwIm2DVertex* vertices,
                                    RwInt32 numVertices, RwImVertexIndex* indices,
                                    RwInt32 numIndices)
{
    if (vertices == NULL || numVertices <= 0 || indices == NULL || numIndices <= 0)
    {
        return FALSE;
    }

    // RwImVertexIndex is an RwUInt16 and librw reads its indices as int16, so
    // the array goes through as it is.
    vertices = ToUIBox(vertices, numVertices);
    BindStateRaster();
    rw::im2d::RenderIndexedPrimitive((rw::PrimitiveType)primType, vertices, numVertices, indices,
                                     numIndices);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Im3D
//
// The transform flags are passed unconverted: rwIM3D_VERTEXUV, ALLOPAQUE,
// NOCLIP, VERTEXXYZ and VERTEXRGBA are 1, 2, 4, 8, 16 on both sides, asserted
// in layout_camera.cpp. That assertion matters more than most, because half the
// call sites pass the bits as a literal -- 0x19 in zFX.cpp, 0x1b in zLasso.cpp
// -- so a renumbering would not even change the source.

void* RwIm3DTransform(RwIm3DVertex* pVerts, RwUInt32 numVerts, RwMatrix* ltm, RwUInt32 flags)
{
    if (pVerts == NULL || numVerts == 0 || rw::engine == NULL)
    {
        return NULL;
    }

    rw::im3d::Transform(pVerts, (rw::int32)numVerts,
                        const_cast<rw::Matrix*>(reinterpret_cast<const rw::Matrix*>(ltm)), flags);

    // The return value is a SUCCESS FLAG, and nothing may dereference it.
    //
    // RenderWare returns a pointer into the immediate-mode heap where it put the
    // transformed vertices; librw's Transform returns void and leaves the
    // transformed copy inside the device. There is no equivalent pointer to
    // hand back. All twenty call sites in src/SB use the result the same way --
    //
    //     if (RwIm3DTransform(vert, count, NULL, 0x19) != NULL) { ...render... }
    //
    // -- as "may I render now", so that is the question this answers, and it
    // answers it with a real pointer to real vertices rather than a made-up
    // non-null value. Returning NULL unconditionally would be the other kind of
    // wrong: every effect in the game would silently stop drawing.
    return pVerts;
}

RwBool RwIm3DRenderPrimitive(RwPrimitiveType primType)
{
    BindStateRaster();
    rw::im3d::RenderPrimitive((rw::PrimitiveType)primType);
    return TRUE;
}

RwBool RwIm3DEnd(void)
{
    rw::im3d::End();
    return TRUE;
}

// The indexed 3D primitive, which iParMgr.cpp:540 is the first and only PC
// caller of -- iRenderTrianglesImmediate draws every particle system's
// triangles through it.
//
// Takes only the indices: the vertices are the ones RwIm3DTransform was handed,
// which librw is still holding. That is why this cannot be reordered with
// respect to the transform, and why there is nothing to pass but the indices.
// librw's rw::im3d::RenderIndexedPrimitive has the same three arguments in the
// same order.
RwBool RwIm3DRenderIndexedPrimitive(RwPrimitiveType primType, RwImVertexIndex* indices,
                                    RwInt32 numIndices)
{
    if (indices == NULL || numIndices <= 0)
    {
        return FALSE;
    }

    BindStateRaster();
    rw::im3d::RenderIndexedPrimitive((rw::PrimitiveType)primType, indices, numIndices);
    return TRUE;
}
