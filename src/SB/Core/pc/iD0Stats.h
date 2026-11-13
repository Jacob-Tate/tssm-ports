#ifndef ID0STATS_H
#define ID0STATS_H

#include <types.h>

// PC-only measurement hooks for the 3DS measurements: what a level costs in memory, draw work and frame time on a
// 32-bit build, so a smaller machine's pools and frame budget can be sized from
// numbers rather than guesses.
//
// Off unless TSSM_D0 names a file. Then:
//
//   TSSM_D0=<file>         one line per presented frame (tab-separated, the
//                          header says what each column is) and a summary per
//                          scene, written as the scene ends and at exit.
//   TSSM_D0_INSTANCE=1     instance every geometry for the active backend right
//                          after it is converted off the Xbox, instead of on its
//                          first draw, so the load-time split can count it.
//   TSSM_D0_TEX3DS=1       also run each texture through what a 3DS build would
//                          do with it at load (decode, half size, 16-bit, 8x8
//                          Morton tiles) and count the time. Nothing is kept.
//   TSSM_D0_PICA=1         keep the 16-bit half-size result instead: every
//                          texture is replaced by its 3DS look (the "PICA
//                          profile"). Implies TSSM_D0_TEX3DS.
//
// Every hook below returns at once when the file is not set, so a normal run pays
// one predictable branch per call.

#ifdef __cplusplus
namespace rw
{
    struct Atomic;
    struct Texture;
} // namespace rw
#endif

struct RwCamera;
struct RpAtomic;

S32 iD0Enabled();

// TSSM_LIGHTLOG=<frame>,...: each lit atomic drawn in those frames is logged
// with the lights it gets (rw/d0rw.cpp), on any backend. Called once a frame
// with the frame just presented (iCapture.cpp's frame number).
void iD0RwLightLogFrame(S32 frame);

// iSystem, after the capture hooks are read.
void iD0Init();

// rw/camera.cpp: RwCameraBeginUpdate on any camera, and the present (before
// the flip). The first begin on the main camera after a present ends the
// frame's update half.
void iD0CameraBegin(RwCamera* camera);
void iD0FrameEnd();

// iMemMgr.cpp: after every game-arena allocation.
void iD0MemSample();

// rw/im.cpp: one immediate-mode draw of `count` vertices or indices of
// RenderWare primitive type `primType`.
void iD0CountIm(S32 is3d, S32 primType, S32 count);

// iModel.cpp: a model the console would have split into 10-bone pieces.
void iD0SkinGeometry(RpAtomic* atomic);

// Load-time buckets: file reads, geometry conversion and instancing, texture
// conversion, and the simulated 3DS texture path.
enum iD0Bucket
{
    iD0_FILE,
    iD0_GEOM_CONVERT,
    iD0_GEOM_INSTANCE,
    iD0_TEX_CONVERT,
    iD0_TEX_3DS,
    iD0_OVERHEAD, // the measuring itself (the skin analysis), to take out of a load's time
    iD0_BUCKETS
};
// For the 3DS spike (iSpike.h), which times the console with these same
// counters: what iD0FrameEnd measured for the frame just presented, and the
// current scene's peaks so far.
struct iD0FrameResult
{
    S32 loop; // a game-loop frame (the others are loading screens and the like)
    S32 play; // and in play: game mode, play state, no cutscene
    S64 workUs; // top of zGameLoop to the present; -1 when not a loop frame
    S64 logicUs; // to the main camera's first begin; -1 when unknown
    S64 renderUs; // from there to the present; -1 when unknown
    S32 meshes;
    S32 tris;
    S32 voices;
};
void iD0LastFrame(iD0FrameResult* out);

struct iD0ScenePeaks
{
    char name[8];
    U32 arenaBytes; // fixed pools + gxHeap[0..2] + the general heap, at most
    U32 heap0Bytes;
    S64 rwHeapBytes; // -1 without TSSM_RWHEAP
    U32 sndCacheBytes;
    S32 voices;
    S64 loadUs; // the scene's load, as iD0 times it (0 until its first loop frame)
    S64 overheadUs; // of which the measuring itself
    S64 fileUs; // of which reading files
    U64 fileBytes;
};
void iD0ScenePeaksNow(iD0ScenePeaks* out);

S64 iD0Now();
void iD0Add(iD0Bucket bucket, S64 startUs, U32 bytes);

#ifdef __cplusplus
// rw/convert.cpp, after an Xbox geometry is made portable.
void iD0AfterConvert(rw::Atomic* atomic);
// rw/texture.cpp, before the Xbox raster is converted for the backend. `opaque`
// when the raster's fourth byte is padding. TRUE when TSSM_D0_PICA replaced the
// raster with one of the backend's own, so there is nothing left to convert.
S32 iD0Texture(rw::Texture* texture, S32 opaque);
#endif

#endif
