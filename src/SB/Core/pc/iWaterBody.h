#ifndef IWATERBODY_H
#define IWATERBODY_H

// PC only, compiled with the game units: the Xbox files' water bodies drawn the GameCube way, when
// the Xbox water (iXboxWater.h, extras.water) does not take them.
//
// The Xbox and PS2 builds draw water with a water-body object (DYNA effect:water_body, zWaterBody
// in the PS2 symbols). It keeps its water model and its reflection and refraction helper models
// out of the normal drawing and renders them itself. The GameCube build has no such object: its
// level files draw the water as plain models (some of them goo, which wobbles). Only two levels
// have water bodies in the Xbox files: B301 and SC02. Without either renderer the helper models
// are drawn as plain models and B301's sea never shows.
//
// Two steps, so the Xbox files look as the GameCube files do:
//   - iWaterBodyScenePrepare, from zSceneInit when !iXboxWaterWillRun(), before the pipe tables
//     are read and the scene's objects are made from their assets: each water body's reflection
//     and refraction models start hidden and stay hidden through every reset (the GameCube files
//     have them hidden or not at all). SC02's water models get the GameCube water's pipe
//     settings, which the Xbox package holds for its unused GameCube-style meshes, and the
//     GameCube water's surface, so they are blended and scroll as on the GameCube.
//   - iWaterBodySceneBegin, from the event handler of each water body the Xbox water does not take
//     (zWaterBody::usable false: every body when !iXboxWaterActive()). At every
//     SceneBegin, which reaches the water bodies after the dispatchers' SceneBegin links (the
//     dynamic types come after sInitTable's in the scene's list), it hides the helpers again in
//     case a link showed one, then does what the GameCube file does in the same place: B301 shows
//     the sea and the reflected sky and makes the sky a sky dome (the GameCube file's
//     WATER_NGC_DISP); SC02's water becomes goo with the GameCube wobble.
// When the Xbox water was to run (iXboxWaterWillRun()) but a body's object cannot be used (modded
// files, or TSSM_WATER_FAIL=objects for all of them), only the second step runs for that body,
// whether or not another body runs the Xbox water. It then also hides the helpers in their assets,
// so that later resets keep them hidden as the first step would have; SC02's water keeps the Xbox
// file's pipe settings and surface (the pipe tables have been read by then), so it wobbles but is
// neither blended nor scrolled the GameCube way.
// The files may be modded, so an asset is read or written only when it is the type the step
// expects (SIMP, PLAT or GRUP in the file), large enough, and starts with its own ID; a group's
// items past its end are not read, and a walk through groups stops after 1024 assets or 4 groups
// deep.
// With the GameCube files (no water bodies) none of this runs. Collision and gameplay are those of
// the Xbox files.

struct zWaterBody;

void iWaterBodyScenePrepare();

// What the last iWaterBodyScenePrepare did, for the scene's one water line in the log.
const char* iWaterBodyPrepared();

void iWaterBodySceneBegin(const zWaterBody& body);

#endif
