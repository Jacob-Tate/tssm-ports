#ifndef IXBOXWATER_H
#define IXBOXWATER_H

#include "xBase.h"
#include "xDynAsset.h"

#include <rwcore.h>

#include <stddef.h>

// PC only, compiled with the game units: the Xbox release's water, as an option
// ([extras] water = xbox | gamecube, xbox by default; iExtras.h, docs/EXTRAS.md).
//
// The Xbox draws three water bodies, all in its own level files: SC02's
// WATER_BODY_01 and WATER_BODY_02 and B301's WATER_BODY_XBOX_02. Each is a DYNA
// "effect:water_body" naming a body (a model whose mesh gives the water's
// extent and height) and two lists of models: one drawn into a refraction
// target, one drawn mirrored into a reflection target. All three are hidden
// from the normal drawing, and the water is a grid of flat tiles drawn over the
// body's bounds with a 17-frame du/dv bump animation bending the two targets.
// The GameCube build has none of it: its files draw water as plain models.
// Where this water does not run, iWaterBody.h gives the Xbox files' water
// bodies the GameCube look.
//
// This file is the game half. The names and the order follow the PS2 build's
// symbols (Game/zWater.cpp in the PS2 DWARF); what each part does follows the
// Xbox executable (the addresses below are DEFAULT.XBE's). iXboxWaterGrid.h
// has the tile arithmetic.
//
// The hooks (all under PLATFORM_PC, placed by the calls around the Xbox's):
//
//   zScene.cpp  dynamic_types[]   the "effect:water_body" entry (its objects
//                                 are made whichever water a scene gets)
//               zSceneInit        xWaterScenePrepare, before the pipe tables
//                                 are read (PC only: the decision from the
//                                 files), then iWaterBodyScenePrepare when
//                                 !iXboxWaterWillRun()
//               zSceneInit        xWaterSceneInit, after xClimateInit (0x742CB)
//               zSceneUpdate      xWaterUpdate at the head of the effects'
//                                 updates (0x70AB4); zWaterBody::update_all
//                                 after xFXFireworksUpdate (0x70AFC)
//               zSceneReset       zWaterBody::reset_all after the spotlights'
//                                 reset (0x70391)
//               zSceneExit        xWaterSceneExit after xLightVolume's (0x6ECAF)
//               zSceneRenderPreFX xWaterRender between xLightVolume::render_all
//                                 and xDecal::render_all (0x6F226)
//   zGame.cpp   zGameSetup        zWaterBody::setup_all after the SceneBegin
//                                 and LevelBegin events (0x52A48)
//               zGameLoop         xWaterPreRender beside zScenePreRenderShadows,
//                                 before the scene's camera begins (PC only)
//
// The refraction and reflection targets (iXboxWaterPass.h) are drawn before
// the scene by default, every body into a pair of its own, so that a phone's
// GPU need not leave the frame and enter it again for them in the middle of
// the scene. They are drawn as the Xbox would have drawn them at its own place
// (between xLightVolume::render_all and xDecal::render_all): under the render
// state that place had in the frame before, and the state they leave is put in
// place there. When that place's render state or the fog turns out different
// this frame, or the scene's camera has moved since (a cutscene's), or a
// Direct3D 9 device was lost or reset in between (which can empty them), the
// targets are drawn again there. TSSM_WATER_EARLY=0 draws them only there, as
// the Xbox does, with one pair for every body.
//
// A body none of whose tiles is in view this frame (all culled or masked)
// draws its two passes into a 16x16 stand-in pair instead of its own
// (iXboxWaterPass.h): nothing reads its targets, but the passes' calls have
// effects the game sees, so they are all still made.
//
// Drawing the player into a refraction target before the scene (rather than
// in the middle of it, as the Xbox does) moves the effects of its Render
// earlier in the frame: the lasso's skip-a-frame bit (zEntPlayer.cpp) is used
// up there, so on the frame Patrick's lasso starts the scene draws it, where
// on the Xbox the scene leaves it out and the target has it; and the effect
// models its Render adds to the alpha list (the cart shield, the macho bowl's
// sphere) are sorted in with the scene's, where the Xbox adds them after the
// sort. Only in SC02 and B301, only while those effects show.
//
// The surface is drawn at the Xbox's place in the scene, from those targets
// and the bump frame of the moment (iXboxWaterDraw.h, rw/xboxwater.cpp). When
// the Xbox water cannot run in a scene -- the setting, GameCube or incomplete
// files, Direct3D 9's fixed path, Vulkan, shaders or targets that could not be
// made, body objects that cannot be used -- the scene gets the GameCube look
// (iWaterBody.h) and one line says why.
//
// Environment, for tests:
//   TSSM_WATER_DEBUG=tiles   each tile's outline (green drawn, red culled, grey
//                            left out by SC02's mask)
//   TSSM_WATER_DEBUG=rt      each body's two targets in the top right of the
//                            screen (refraction, then reflection; a row per
//                            body) and the bump frame now drawn beside them
//                            (both: tiles,rt)
//   TSSM_WATER_DUMP=<frames> comma-separated frame numbers (TSSM_CAPTURE_FRAMES'
//                            numbering): each body's targets and the bump frame
//                            written as PNGs into TSSM_CAPTURE_DIR, as
//                            water_<frame>_b<body>_refract.png, _reflect.png
//                            and water_<frame>_bump<n>.png
//   TSSM_WATER_EARLY=0       the targets drawn only where the Xbox draws them
//   TSSM_WATER_VERIFY=1      with the targets drawn early, every body's are
//                            drawn again at the Xbox's place into a spare pair
//                            and compared, pixels read back, with the render
//                            state each leaves; the counts are logged
//                            (desktop: it reads the targets back)
//   TSSM_WATER_RT=<n>        n x n targets (512 is the Xbox's) whatever the
//                            render size
//   TSSM_WATER_DEBUG=flat    the surface with no ripples (both bump matrices
//                            zero); =refract the refraction target alone,
//                            untinted; =nosurface no surface (words combine)
//   TSSM_WATER_FAIL=<what>   the GameCube water, as if `bumps` were missing
//                            from plat.HIP, the `shaders` or the `targets`
//                            could not be made, or the body `objects` could
//                            not be used though their assets checked out
//                            (the fallback's tests)
//   TSSM_WATER_STANDIN=0     a body out of view draws its own targets, full
//                            size (the stand-in's check)

struct xEnt;

// The water body object: the PS2's zWaterBody without body_id and visible,
// which the Xbox has not (no visibility events: its event handler is empty).
// PC: made in every scene whose files have water bodies; for a body the Xbox
// water does not take (usable false), its handler gives SceneBegin the
// GameCube look (iWaterBodySceneBegin) and nothing else of it runs.
struct zWaterBody : xBase
{
    // The DYNA asset, version 1, 40 bytes; links follow it.
    struct asset_type : xDynAsset
    {
        U32 flags; // not read on the Xbox
        U32 motion_type; // a water.ini section; not read on the Xbox
        U32 body;
        U32 facade_refract; // a model or a group of them
        U32 facade_reflect;
        U32 light_dir; // not read on the Xbox
    };

    const asset_type* asset;
    xEnt* body_ent;
    xEnt** refract_ents;
    S32 refract_ents_size;
    xEnt** reflect_ents;
    S32 reflect_ents_size;

    // PC: whether the Xbox water takes this body, set by the scene-init
    // decision where the Xbox water runs, when the body checked out (it exists,
    // is an entity, and its model has vertices) and is one of the first 16
    // that did; false everywhere else. setup and the rest skip a body that is
    // not usable, where the Xbox would crash; its SceneBegin gives it the
    // GameCube look.
    bool usable;

    static void load(xBase& data, xDynAsset& asset, size_t size);
    static void setup_all();
    static void update_all();
    static void reset_all();

    void setup();
};

// zSceneInit, before the pipe tables ('PIPT') are read and the scene's
// objects are made from their assets, so that the GameCube look's asset step
// (iWaterBodyScenePrepare) can run right after it: the decision as far as the
// files and the renderer can make it (iXboxWaterWillRun). It makes the bump
// textures, the shaders and the targets when they are needed.
void xWaterScenePrepare();

// Whether the Xbox water takes this scene's water bodies: final from
// xWaterScenePrepare until the scene exits. True when the level's [extras]
// decision is xbox (iExtrasInForce: the setting, the level's files are the
// Xbox release's, and not Direct3D 9's fixed path), plat.HIP has the 17 bump frames, the renderer has the water shaders, the
// bump textures, shaders and targets were made, and at least one
// effect:water_body asset is whole and names a body asset that is an entity
// with a model asset. While it is false, zSceneInit runs the GameCube look's
// asset step (iWaterBodyScenePrepare: the helper models' visible bits cleared
// in their assets; SC02's water mesh given the GameCube pipe entry and the
// WATER_NGC_SURF surface).
bool iXboxWaterWillRun();

// zSceneInit, once the scene's objects exist: forget the last scene's bodies,
// check the body objects, and log the decision in one line ("tssm: water:
// ...", with what the GameCube look's asset step did) when the scene's files
// have water bodies.
void xWaterSceneInit();

// Whether the Xbox water runs in the current scene: final from xWaterSceneInit
// (once the objects are made; before zGameSetup and its SceneBegin events)
// until the scene exits. It is iXboxWaterWillRun() and at least one body
// object usable (an entity whose model has vertices); the two differ only with
// modded files, whose body asset checks out but whose object does not
// (TSSM_WATER_FAIL=objects makes that case).
//
// Every SceneBegin reaching a water body object that is not usable runs the
// GameCube look's SceneBegin step (iWaterBodySceneBegin: the helpers hidden,
// B301's GameCube sea and sky dome shown, SC02's water made goo): every body
// while this is false, and while it is true, a body whose object failed
// beside one that works. Where iXboxWaterWillRun() is true, the asset step has
// not run, so the SceneBegin step also clears that body's helpers' visible
// bits in their assets (later resets keep them hidden), but SC02's water keeps
// the Xbox file's pipe entry and surface: it wobbles, but is not blended or
// scrolled the GameCube way.
bool iXboxWaterActive();

// zSceneUpdate, while the game runs: the bump clock and each tile's cull.
void xWaterUpdate(F32 dt);

// zGameLoop, before the scene's camera begins (no camera in an update): the
// refraction and reflection targets drawn early (above).
void xWaterPreRender(RwCamera* cam);

// zSceneRenderPreFX, the main camera.
void xWaterRender(RwCamera* cam);

// Co-op (docs/MULTIPLAYER.md, on the co-op branches; docs/RENDERING.md,
// "Water", says the same): the function that draws
// the partner, which every refraction pass calls right after the player's
// Render, so that a partner wading in the water keeps their legs (on the Xbox
// only the player is drawn there; the water is opaque, so what is under the
// surface shows only through that target). NULL (the default) draws no one.
//
// It is called inside the refraction target's camera (a copy of the scene's,
// in its own RpWorld with no lights; xglobals->currWorld is that world, and
// each model's light kit is enabled into it as xModelRenderSingle does), with
// SDRS_OpaqueModels set afresh (zRenderStateBegin, then zRenderState: what
// the player's Render left is not kept) and the model buckets as the scene
// left them; anything else it needs, it sets. It must
// only draw the partner's model -- not move it, animate it further, or work
// out the name tag from the scene's camera: with the targets drawn before the
// scene it runs before the scene's camera has begun this frame, and once per
// body per frame. The co-op branch fills it once at start-up, e.g. with the
// body-drawing half of iCoopRenderPartner.
typedef void (*iXboxWaterDrawFn)();
void iXboxWaterSetPartnerDraw(iXboxWaterDrawFn draw);

// zSceneExit.
void xWaterSceneExit();

#endif
