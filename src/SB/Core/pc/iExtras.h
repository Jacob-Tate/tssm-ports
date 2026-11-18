#ifndef IEXTRAS_H
#define IEXTRAS_H

#include <types.h>

#include "xLinkAsset.h"

#include <stddef.h>

// PC-only: the features of the Xbox release that the GameCube release cut or
// changed, each one switchable between the two releases' versions ([extras] in
// config.ini; docs/EXTRAS.md). The GameCube build has none of this: every call
// into it from game code is under PLATFORM_PC.
//
// Each feature is decided once per level, in iExtrasSceneEnter, from three
// things: the setting, whose files the level's package is (Xbox or GameCube),
// and the renderer. When the files or the renderer cannot give the Xbox version,
// the level gets the GameCube one, the log says so, and iExtrasStatus gives the
// overlay a sentence saying why. The glow and the frame lock are code only and
// follow the setting at once; the rest hold until the next level loads.

enum iExtrasFeature
{
    iEXTRAS_GLOW,
    iEXTRAS_SPOTLIGHTS,
    iEXTRAS_CROWDS,
    iEXTRAS_WATER,
    iEXTRAS_FRAME_LOCK,
    iEXTRAS_FEATURE_COUNT
};

// Which release's version of a feature.
enum iExtrasLook
{
    iEXTRAS_GAMECUBE,
    iEXTRAS_XBOX
};

// Whose files a level's package is: the PLAT chunk of the level's HIP, a mod's
// copy when a mod replaces it (iFilePackagePlatformOf, iGCPackage.h); before
// the first level, boot.HIP's (iFileAssetPlatform).
enum iExtrasFiles
{
    iEXTRAS_FILES_XBOX,
    iEXTRAS_FILES_GAMECUBE
};

// The renderer, as far as a feature cares.
enum iExtrasRenderer
{
    iEXTRAS_RENDERER_SHADERS, // D3D9's shader path, D3D11, OpenGL 3 / GLES 3, Vulkan
    iEXTRAS_RENDERER_FIXED,   // D3D9's fixed-function path
    iEXTRAS_RENDERER_3DS      // citro3d
};

// "extras.crowds", and the feature's name for the log ("crowds").
const char* iExtrasKey(iExtrasFeature feature);
const char* iExtrasName(iExtrasFeature feature);

// Whether this build has the Xbox version at all. A feature it does not have
// resolves to the GameCube version and its overlay row is hidden.
bool iExtrasBuilt(iExtrasFeature feature);

// What config.ini asks for now. A value that is neither xbox nor gamecube
// (any case) is the settings table's default (gamecube on the 3DS, xbox
// elsewhere), and the log says so once per key.
iExtrasLook iExtrasSetting(iExtrasFeature feature);

// config.ini's value when it is neither xbox nor gamecube, for the overlay's
// note on the row; NULL when it is one of them or not set.
const char* iExtrasBadValue(iExtrasFeature feature);

// The decision itself, with nothing read from anywhere: what a level gets for
// this setting, files and renderer. `why` is set to a sentence for the overlay
// when the answer is not the setting, and to NULL when it is. For the tests
// (tests/extrastest.cpp) as much as for iExtrasSceneEnter.
iExtrasLook iExtrasResolve(iExtrasFeature feature, iExtrasLook setting, iExtrasFiles files,
                           iExtrasRenderer renderer, U32 sceneID, const char** why);

// zSceneInit (zScene.cpp), right after the level's own package is read and
// before any of its objects is made: decides every feature for the level. The
// water's decision is read by xWaterScenePrepare (iXboxWater.h) right after.
void iExtrasSceneEnter(U32 sceneID);

// The end of zSceneInit, with the level made: logs the decisions in one line,
// with what was done (crowd objects held still) and the level's retrace count.
void iExtrasSceneReady(U32 sceneID, U32 minVSyncCnt);

// What is in force now. Before the first level, the setting.
iExtrasLook iExtrasInForce(iExtrasFeature feature);

// Whose files the current level's package is, as iExtrasSceneEnter found; before
// the first level, boot.HIP's.
iExtrasFiles iExtrasLevelFiles();

// A feature the level loaded with as xbox that its files turn out not to allow
// after all (iExtrasGC.h: a GameCube file that is not what the GameCube disc
// has where the Xbox version is made from it): the level gets the GameCube
// version, and `why` (a string constant) is the overlay's sentence for the row
// and the level's line's. Only between iExtrasSceneEnter and the level's end.
void iExtrasFallBack(iExtrasFeature feature, const char* why);

// A few words for the level's line after a feature's decision, e.g. "(11 given
// the skinned crowd)". At most one per feature and level; cleared by
// iExtrasSceneEnter.
void iExtrasNote(iExtrasFeature feature, const char* fmt, ...);

// The setting a feature had when the current level loaded: what the overlay's
// "next level" tag compares with.
iExtrasLook iExtrasAtLevelLoad(iExtrasFeature feature);

// TRUE, with a sentence in `out`, when the feature is not what its setting asks
// for in the current level (or, before a level, would not be), for the row's
// note in the overlay. FALSE and an empty `out` otherwise. Also when the level
// loaded with xbox and still got the GameCube version for a reason of its own:
// the water's (iXboxWaterActive: no bump frames in plat.HIP, Vulkan, shaders or
// targets not made), in the water's words, or its files' (iExtrasFallBack).
bool iExtrasStatus(iExtrasFeature feature, char* out, size_t outSize);

// --- frame_lock --------------------------------------------------------------------

// The GameCube release holds six levels at 30 frames a second: zSceneInit sets
// globals.minVSyncCnt to 2 for them (1 for every other), and zGameLoop passes it
// to RwGameCubeSetMinRetraceCount every frame, where the console's video driver
// then shows a new picture only every second retrace. The Xbox release reads
// the same value and never waits.
//
// rw/engine.cpp's RwGameCubeSetMinRetraceCount calls this with the count. With
// frame_lock = gamecube and a count above 1, the pacer holds 59.94/count frames
// a second (iWindowSetFrameLock). The count is kept until the next call, which
// only zGameLoop makes, so the menu after a held level stays held until the
// next level's first frame, as on the GameCube.
void iExtrasSetRetraceCount(S32 count);

// The count being held now, or 0 when nothing is (frame_lock = xbox, a count of
// 1, or a movie's frame-rate ceiling in force).
S32 iExtrasHeldRetraces();

// The Patty Wagon's steps for a frame of `dt` seconds while the hold paces the
// frame: one per 1/60 s, rounded (2 for a held frame), so the car drives as the
// Xbox's does at 60. `steps` (zEntPlayerCarUpdate's own count) otherwise.
//
// zEntPlayerCarUpdate makes ceilf(dt * 60) steps of a frame longer than 1.5/60
// s, and a held frame measures 2/60 s give or take its jitter, so it got 2 or
// 3 steps at random, each giving the full per-step push: a stronger car by
// chance. The GameCube does that too (in Dolphin, 69% of BB01's held frames
// got 3 steps and 31% got 2, docs/EXTRAS.md), which is not a steady 3 a frame;
// so the hold keeps the Xbox's driving.
S32 iExtrasHeldCarSteps(F32 dt, S32 steps);

// Re-reads the settings that apply at once (the frame lock, the glow) and
// applies them. Called by the overlay after it changes one (also in a menu,
// where no game frame passes the count again); otherwise they are read when a
// level loads.
void iExtrasApply();

// --- glow ----------------------------------------------------------------------------

// The Xbox glow's strength in a scene: the colour its composite is added at
// (iXboxGlow.h), from the scene ID compares at DEFAULT.XBE 0x1b0eb4-0x1b0f57.
// 0.45 (0x2cc364) for every scene not named: the menus, and any scene a mod
// adds; JK01-JK02 0.3; BB02, TR01-TR03 0.55; B401, PT01-PT03, SC02 0.6; TT01-TT02
// 0.7; GG02 0.75; BB03, B201 1.0.
F32 iExtrasGlowStrength(U32 sceneID);

// --- crowds --------------------------------------------------------------------------

// The Xbox release's skinned crowd models (crowd1_bind C49163C8, crowd2_bind
// C015AF3B, crowd3_bind BB99FAAE), used by the crowds of BB02 (No Cheese!) and
// BB01 (Sandwich Driving 101). The GameCube files give those crowds rigid
// models and no animation.
bool iExtrasCrowdModel(U32 modelID);

// TRUE when an object with this model is to stand still in this level (crowds
// are gamecube and it is a crowd model). zEntSimpleObj_Init and _Reset then
// set its animation's speed to 0 after the reset starts the idle cycle, so the
// crowd keeps the cycle's first pose. Counts the objects for the log.
bool iExtrasCrowdHeld(U32 modelID);

// --- spotlights ----------------------------------------------------------------------

// The spotlights the GameCube release keeps off in a level whose Xbox files
// switch them on (zSpotlight's reload_asset, reset and activate honour it with
// spotlights = gamecube, for a level from Xbox files: GameCube files keep
// them off with their own links). Keyed by scene, because SPOTLIGHT_02 is in BB02 and
// in B401. With xbox, the lights are drawn as on the Xbox: light volumes
// (iLightVolume.h) and cones that do not glow.
bool iExtrasSpotlightKeptOff(U32 sceneID, U32 id);

// --- a level's GameCube files ------------------------------------------------------------

// Where a level's GameCube files hold what a feature's Xbox version needs but
// use it otherwise, iExtrasGC.h makes them use it as the Xbox files do while the
// feature is xbox. Its tables are here, with no game code, for the tests.

// BB02's GameCube files give the crowd objects the rigid models crowd01-crowd03
// (51947E72-74) and no animation list, yet ship the skinned models and lists
// the Xbox files' crowd has. The skinned model and its list for one of the
// rigid models: crowd01 -> crowd1_bind C49163C8 and CROWD1_ANIMLIST 9AD84F76,
// crowd02 -> crowd2_bind C015AF3B and CROWD2_ANIMLIST 6EAEF0D9, crowd03 ->
// crowd3_bind BB99FAAE and CROWD3_ANIMLIST 4285923C. FALSE for any other model.
bool iExtrasCrowdSkinned(U32 rigidModel, U32* skinnedModel, U32* animList);

// The links by which a level's GameCube files switch spotlights off where the
// Xbox files' switch them on (the two discs' files differ there and nowhere
// else in these objects):
//
//   BB02  SPOTLIGHT_DISP_GC_ONLY (DPAT E9B0DDB1), only in the GameCube files:
//           SceneBegin -> Off SPOTLIGHT_01, SceneBegin -> Off SPOTLIGHT_02: dropped
//   B401  INTRO_CSNMANAGER (CSNM 2F702A12):
//           Done -> Off SPOTLIGHT_GROUP: Done -> On SPOTLIGHT_GROUP, Done -> Run SPOTLIGHT_SCRIPT
//   B401  SCENE_RESTART_DISP (DPAT A18EA13D):
//           SceneReset -> Off SPOTLIGHT_GROUP: SceneReset -> On SPOTLIGHT_GROUP,
//           SceneReset -> Run SPOTLIGHT_SCRIPT
//
// The objects of a scene whose links change: their IDs and asset types ('DPAT',
// 'CSNM') in `ids` and `types` (room for `max`); returns how many.
S32 iExtrasSpotlightLinkObjects(U32 sceneID, U32* ids, U32* types, S32 max);

// The `count` links of the object `objectID` in scene `sceneID`, written to
// `out` (room for `outMax`) as the Xbox files have them, in the same order,
// every other link as it was: returns the new count. -1 when one of the links
// the table changes is not there exactly once with no parameters, widget or
// check (the object is not what the GameCube disc has), or `out` is too small:
// nothing is to be changed then. An object the table does not name is copied
// as it is.
S32 iExtrasSpotlightLinks(U32 sceneID, U32 objectID, const xLinkAsset* in, S32 count,
                          xLinkAsset* out, S32 outMax);

#endif
