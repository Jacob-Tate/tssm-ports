#ifndef IDECALFILTER_H
#define IDECALFILTER_H

#include <rwcore.h>

// PC-only: draw a projected decal from its top texture level, as the GameCube does.
//
// xShadowVertical_DrawCache and xShadowReceiveShadow lay a decal's texture over whole ground and
// model triangles, with UVs far outside 0..1 and CLAMP addressing, so the texture's transparent
// border is all that hides the rest of each triangle. Every decal in the game (bowling_lane,
// directional, shadow_round, target, fx_shadow_ball) has that border on its top level. On the
// GameCube the top level is the only one. The Xbox art the port loads has a full mip chain, and box
// filtering pulled the art into the edge rows of the smaller levels; CLAMP then repeats those rows
// across the triangle, which is the dark band along the SpongeBowl's aiming lane.
//
// The decal binds a bare raster, so it would sample with whatever filter the last material left on
// the stage. Begin sets rwFILTERLINEAR there and returns the stage's filter; End puts it back, so
// nothing else drawn in the frame changes. A NULL raster (the shadow camera's own target, which has
// one level) leaves the stage alone. See docs/RENDERING.md, "Decals: the top level only".
RwTextureFilterMode iDecalFilterBegin(const RwRaster* decal);
void iDecalFilterEnd(const RwRaster* decal, RwTextureFilterMode stageFilter);

#endif
