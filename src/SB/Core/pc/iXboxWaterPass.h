#ifndef IXBOXWATERPASS_H
#define IXBOXWATERPASS_H

#include <types.h>

#include <rwcore.h>

// PC only, compiled with the game units: the Xbox water's (iXboxWater.h)
// refraction and reflection targets, and the two passes that draw them -- the
// first half of the Xbox's per-body draw (0x1AD570). The surface that reads
// them is drawn after.
//
// For each body the Xbox draws, from a copy of the scene's camera (0xCAF80):
//
//   1. The refraction target, cleared to transparent black: the sky domes of
//      the refraction list under SDRS_WaterSkyDome, one at a time
//      (xSkyDome_RenderSingle), then the list's other models under
//      SDRS_OpaqueModels, then the player (zPlayer::Render, the vtable's
//      Render that 0x1AD5E8 calls). Here the co-op partner follows the player
//      (iXboxWaterSetPartnerDraw, iXboxWater.h).
//   2. The reflection target, the same way from the reflection list, each
//      model's matrix mirrored about the water's height for its draw, and no
//      player. No clip plane.
//
// The targets are xFXCameraTextures with depth of their own and the Xbox's
// 8888 colour (alpha included), cleared to transparent black, under the colour
// write mask the scene has (lastSetFBMSK), as the Xbox has them. The Xbox makes one pair, 512x512, in every scene
// (xWaterSceneEnter, 0x1AD1D0); here pairs are made only where the Xbox water
// runs, sized with the render size (iXboxWaterTargetSize), and there is a pair
// for each body when they are drawn before the scene, since body 1's pair is
// still to be read when body 2's would be drawn.

struct xEnt;

// What one body's two passes draw.
struct iXboxWaterFacades
{
    xEnt** refract;
    S32 refractCount;
    xEnt** reflect;
    S32 reflectCount;

    // The mirror's plane: the water's height, the y of the body's tile 0.
    F32 height;
};

// Make `pairs` pairs of targets at the size for the render size, or keep the
// ones there when the count and the size are the same (the render size can
// change while a level runs). Not while a camera is in an update. False when
// one could not be made; there are none then. With them, while any are made,
// a 16x16 stand-in pair (below).
bool iXboxWaterTargetsMake(S32 pairs);
void iXboxWaterTargetsFree();
S32 iXboxWaterTargetPairs();
void iXboxWaterTargetSizeNow(S32* w, S32* h);

// The stand-in: a 16x16 pair that nothing reads, for a body with no tile in
// view. Its passes still make every call the Xbox's make -- the player's
// Render has effects the game sees (it emits bubbles, drawing a random number,
// uses up the lasso's skip-a-frame bit and adds effect models to the alpha
// list), and the render state the passes leave is what the scene goes on
// with -- so only the pixels are left out. False when it could not be made
// (the body's own pair is drawn then).
const S32 iXBOXWATER_PAIR_STANDIN = -2;
bool iXboxWaterHasStandIn();

// Pair `pair`'s refraction and reflection passes (or the stand-in's), from
// `cam` (the scene's camera; it may be in an update -- it is ended for the
// passes and begun again after them, as begin_render and end_render do on the
// Xbox -- or not).
void iXboxWaterTargetsDraw(S32 pair, const iXboxWaterFacades& facades, RwCamera* cam);

RwRaster* iXboxWaterRefractRaster(S32 pair);
RwRaster* iXboxWaterReflectRaster(S32 pair);

// The same as textures, for the surface: bilinear and clamped, as the Xbox
// samples them (iWaterSetTextures, 0x1C34F0). NULL for a pair there is not.
RwTexture* iXboxWaterRefractTexture(S32 pair);
RwTexture* iXboxWaterReflectTexture(S32 pair);

// TSSM_WATER_DEBUG=rt: each pair's two targets in the top right of the
// screen, refraction left, reflection right, a row per pair, and `bump` (the
// bump frame now drawn) at the left of the first row. In the scene's camera.
void iXboxWaterTargetsDebugDraw(S32 pairs, RwRaster* bump);

// Write a raster as an RGB PNG (the dumps of TSSM_WATER_DUMP).
bool iXboxWaterWriteRaster(const char* path, RwRaster* raster);

// Two rasters of one size and format (8888), read back and compared: the
// pixels that differ in any channel, alpha included, and the largest channel
// difference. False when they cannot be read or are not alike.
bool iXboxWaterCompareRasters(RwRaster* a, RwRaster* b, S32* differ, S32* maxDiff);

#endif
