#ifndef IXBOXWATERDRAW_H
#define IXBOXWATERDRAW_H

#include <rwcore.h>

#include <types.h>

// PC only, in tssm_rw: the renderer's half of the Xbox release's water
// (iXboxWater.h), the surface draw of 0x1AD570's last part -- iWaterBeginRender
// (0x1C2CF0), iWaterSetCameraConstants (0x1C3140), iWaterSetTextures
// (0x1C34F0), then per tile iWaterPatchLOD (0x1C3B60) and iWaterDrawPatch
// (0x1C3A10), then iWaterEndRender (0x1C30D0). rw/xboxwater.cpp has one arm for
// Direct3D 9's shader path and Direct3D 11 (rw::d3d) and one for OpenGL 3 and
// OpenGL ES 3 (rw::gl3). The shaders are shaders/water_VS.hlsl and
// water_PS.hlsl, and shadersgl/water.vert and water.frag.
//
// Vulkan has no water shaders, nor does Direct3D 9's fixed path, the 3DS or a
// build with no renderer: there iXboxWaterDrawOpen says why, and the scene
// keeps the GameCube water.
//
// The states, as the Xbox sets them for the surface (iWaterPushRenderStates,
// 0x1C2430): blend ONE/ZERO (the surface is opaque, its alpha the refraction's),
// no alpha test, z test and z write on; the cull mode and the colour write mask
// as the scene has them. The bump frame wraps, the two targets clamp, all three
// filter bilinear. Everything the draw changes is put back at the end.

// The grids the Xbox draws: 17 vertices a side (SC02 and the default preset)
// and 33 (B301).
const U32 iXBOXWATERDRAW_GRIDS = 2;

// The shaders and the two grids' vertex and index buffers, made the first
// time and kept for the run (about 34 KB). NULL when they are there, otherwise
// why not, for the log line that says the scene keeps the GameCube water.
// Not while a camera is in an update. TSSM_WATER_FAIL=shaders makes it fail,
// for the fallback's test.
const char* iXboxWaterDrawOpen();

// Whether a grid of `vertsPerSide` vertices a side can be drawn.
bool iXboxWaterDrawHasGrid(U32 vertsPerSide);

struct iXboxWaterSurface
{
    RwTexture* bump; // this frame's du/dv (wrap, bilinear)
    RwTexture* refraction; // the targets (clamp, bilinear)
    RwTexture* reflection;

    // The body atomic's frame LTM, which the Xbox's vertex shader multiplies
    // the world-space tiles by; NULL for none.
    const RwMatrix* world;

    F32 tint[4]; // pixel constant 4
    U32 vertsPerSide; // 17 or 33
};

// Inside the scene's camera update. False, having changed nothing, when the
// renderer's half is not open or has no such grid.
bool iXboxWaterDrawBegin(const iXboxWaterSurface& surface);

// One tile: its corner, height and cell size (x0, y, z0, cell), and the two
// bump matrices already scaled for its distance, as (m00, m01, m10, m11).
void iXboxWaterDrawTile(const F32 tile[4], const F32 refract[4], const F32 reflect[4]);

void iXboxWaterDrawEnd();

// What the current level's water bodies got, for the overlay's Settings tab
// and the [extras] line ("Now: GameCube water in SC02 -- the Vulkan renderer
// has no water shaders."), or NULL when the level's files have no water body
// (and before the first level). The game half (iXboxWater.cpp) sets it; kept
// here so that the platform layer reads it without the game's headers.
void iXboxWaterSetStatus(const char* status);
const char* iXboxWaterStatus();

// iXboxWater.h's: whether the Xbox water runs in the current level. Declared
// here too for iExtras.cpp, which reads it without the game's headers.
bool iXboxWaterActive();

// The GL3 uniform the shaders read (u_water), named to librw's registry before
// any shader is made (RwEngineOpen), as iGlowRegisterShaderUniforms does.
// Nothing on the backends whose constants are numbered.
void iXboxWaterRegisterShaderUniforms(void);

#endif
