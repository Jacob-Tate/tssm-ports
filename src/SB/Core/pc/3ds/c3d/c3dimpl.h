#ifndef C3DIMPL_H
#define C3DIMPL_H

// rw::c3d's own declarations, shared by its files and nothing else. These
// files are compiled with libctru's short enums and include citro3d and
// librw's headers together (rwc3d.h says why that is safe); nothing of the
// game's is included.

#include <3ds.h>
#include <citro3d.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rwbase.h"
#include "rwerror.h"
#include "rwplg.h"
#include "rwrender.h"
#include "rwengine.h"
#include "rwpipeline.h"
#include "rwobjects.h"
#include "rwanim.h"
#include "rwplugins.h"

#include "rwc3d.h"

namespace rw
{
    namespace c3d
    {
        // ------------------------------------------------------------ shaders
        // The DVLEs of the one SHBIN, in the order CMakeLists.txt hands the
        // files to picasso.
        enum ProgramId
        {
            PROG_WORLD,
            PROG_LIT,
            PROG_IM2D,
            PROG_IM3D,
            PROG_SKIN,
            PROG_ENV,
            PROG_SKINENV,
            NUM_PROGRAMS
        };

        // shaders/c3d_uniforms.pinc's register map.
        enum
        {
            REG_MVP = 0,
            REG_MATCOLOR = 4,
            REG_AMBIENT = 5,
            REG_SURF = 6,
            REG_LIGHTDIR = 7,
            REG_LIGHTCOL = 11,
            REG_UVMAT = 15,
            REG_ENVMAT = 17,
            REG_SCRMAT = 19,
            REG_BONES = 21,
            REG_FOG = 81,
            REG_EYECAM = 82, // stereo: (C, w_min, 0, 0), in the lists
            NUM_REGS = 83, // shadowed; c83 (u_eyeSide) is written raw, per eye
            REG_EYESIDE = 83,
            // b_eye (b0), as citro3d's C3D_BoolUnifSet numbers bools: from
            // 0x68, the DVLE's register number for b0.
            REG_BEYE = 0x68,
            REG_BTRIM = 0x69 // b_trim (b1): skintrim
        };

        // ------------------------------------------------------ vertex formats
        // A model's vertex: position (float x 3), then what the geometry has of
        // normal (signed bytes x 4), colour (bytes r, g, b, a) and uv (float x
        // 2); what it lacks is a fixed attribute. A skinned one ends with its
        // four palette slots x 3 and four weights (bytes x 4 each). MODEL_*
        // are bits of a model format's number.
        enum
        {
            MODEL_NORMAL = 1,
            MODEL_COLOR = 2,
            MODEL_UV = 4,
            MODEL_SKIN = 8,
            NUM_MODEL_FORMATS = 16,
            FORMAT_IM2D = NUM_MODEL_FORMATS,
            FORMAT_IM3D,
            NUM_FORMATS
        };

        uint32 modelStride(uint32 format);

        // ---------------------------------------------------- native rasters
        struct C3dRaster
        {
            C3D_Tex tex;
            uint8 hasTex; // tex holds storage
            uint8 onVram; // in VRAM (else in linear memory)
            uint8 alphaKind; // ALPHAOPAQUE, ALPHAKEYED or ALPHAGRADED
            uint8 filter; // what tex.param says now (Texture::FilterMode)
            uint8 addrU; // (Texture::Addressing)
            uint8 addrV;
            uint8 isCamera; // a camera texture (c3drtt.cpp)
            uint8 locked;
            uint8 rendered; // a camera texture drawn into (or cleared) since it was made
            uint8 isShadow; // its camera has a Z raster of its size (xShadow's)
            uint8 screenCopy; // COPY_SHARED or COPY_OWN: a copy of the top screen (copyScreen)
            uint32 generation; // bumped whenever the texels change
            // A camera texture's render target (made at its first use) and
            // the camera's picture within it, from (0, 0).
            C3D_RenderTarget* target;
            int16 usedW;
            int16 usedH;
            // A locked level's texels, BGRA8, top row first. Kept between
            // locks of level 0 for writing (a movie locks its raster every
            // frame), freed with the raster.
            uint8* lockPixels;
            uint32 lockBytes;
            int32 lockLevel;
            float uScale; // the picture's share of the texture
            float vScale;
            // A font atlas's level 0 as it was read (RGBA8, the raster's own
            // size): xFont measures each character from the texels
            // (RwImageSetFromRaster), and the GPU's copy cannot be read back.
            uint8* keep;
        };

        // A screen copy's storage: the passes' copies share one
        // block, the loading-screen still keeps its own.
        enum
        {
            COPY_NONE,
            COPY_SHARED,
            COPY_OWN
        };

        extern int32 nativeRasterOffset;
#define GETC3DRASTEREXT(raster)                                                                    \
    PLUGINOFFSET(rw::c3d::C3dRaster, raster, rw::c3d::nativeRasterOffset)

        void registerNativeRaster(void);
        Raster* rasterCreate(Raster* raster);
        uint8* rasterLock(Raster* raster, int32 level, int32 lockMode);
        void rasterUnlock(Raster* raster, int32 level);
        int32 rasterNumLevels(Raster* raster);
        bool32 imageFindRasterFormat(Image* img, int32 type, int32* width, int32* height,
                                     int32* depth, int32* format);
        bool32 rasterFromImage(Raster* raster, Image* image);
        Image* rasterToImage(Raster* raster);

        // --------------------------------------------------------- geometry
        struct InstanceData
        {
            uint32 numIndex;
            uint32 minVert;
            int32 numVertices;
            Material* material;
            bool32 vertexAlpha;
            uint32 offset; // first index, in the header's index buffer
        };

        // The bones a skinned model's vertex shader sees at once (u_bones).
        enum
        {
            PALETTE_BONES = 20
        };

        // A part of a skinned mesh that needs at most PALETTE_BONES bones:
        // its triangles (an index range) and which bone sits in each palette
        // slot.
        struct SkinPiece
        {
            uint32 mesh; // into the header's inst
            uint32 offset; // first index
            uint32 numIndex;
            // The vertices this piece's palette skins on the CPU (an unsplit
            // geometry's first piece has them all, the others none).
            uint32 firstVert;
            uint32 numVerts;
            uint8 numSlots;
            uint8 bones[PALETTE_BONES]; // the skin's bone in each slot
        };

        struct InstanceDataHeader : rw::InstanceDataHeader
        {
            uint32 serialNumber;
            uint32 numMeshes;
            uint32 primType; // GPU_Primitive_t
            uint32 format; // a model format
            uint32 stride;
            uint32 totalNumIndex;
            uint32 totalNumVertex;
            uint16* indexBuffer; // linear memory
            uint8* vertexBuffer; // linear memory
            V3d center; // subtracted from every position
            InstanceData* inst;
            // Skinned: the pieces in mesh order, drawn as primType (a split
            // mesh's pieces are triangle lists).
            SkinPiece* pieces;
            uint32 numPieces;
            // A split skin's source vertex for each of its vertices (nil:
            // the geometry's own order), so that new positions can be
            // written without splitting it again (refreshInstance).
            uint16* srcMap;
            uint32 srcVertices; // the geometry's numVertices when instanced
        };

        void* destroyNativeData(void* object, int32, int32);
        ObjPipeline* makeDefaultPipeline(void);
        ObjPipeline* makeUVTransformPipeline(void);
        ObjPipeline* makeMatFXPipeline(void);
        ObjPipeline* makeSkinPipeline(bool32 matfx);

        // c3dskin.cpp. instanceSkin: nil for a geometry without its weights.
        // `again`: made again after a change (TSSM_C3D_STATS counts and
        // prints only a geometry's first time).
        InstanceDataHeader* instanceSkin(Geometry* geo, bool again);
        // TSSM_C3D_INSTPROF=1 (a measurement): where instancing's time goes,
        // a line every 2 s. Ticks, summed since the last line.
        struct InstProf
        {
            bool on;
            u64 prep, stats, alloc, write, flush, print, mesh, freeing, refresh, total;
            uint32 skins, meshes, skinVerts, meshVerts, first, refreshed, refreshVerts;
        };
        extern InstProf gInstProf;
        // L3's caches, how often they were asked and how often they had it
        // (c3ddevice.cpp; TSSM_C3D_INSTPROF's lines print them).
        struct L3Counts
        {
            uint32 lightCalls, lightHits, stateCalls, stateHits;
        };
        extern L3Counts gL3;
        void skinBegin(Atomic* atomic, bool32 bindPose); // this atomic's bones
        int32 skinPalette(const SkinPiece* piece); // the registers sent
        // After skinBegin: what vs_skin computes, on the CPU, from the same
        // bytes, into `dst` in the format without MODEL_SKIN.
        void skinCpu(const InstanceDataHeader* h, uint8* dst);

        // ------------------------------------------------------------- modes
        // What is drawn how, switchable while the game runs (c3dab.cpp):
        // TSSM_C3D_SKIN, TSSM_C3D_LIGHT, TSSM_C3D_ENV, and TSSM_C3D_AB's
        // switches at given frames.
        enum
        {
            SKIN_GPU, // vs_skin
            SKIN_CPU, // skinCpu, drawn with vs_lit / vs_world / vs_env
            SKIN_BIND, // vs_skin with the bind pose
            SKIN_OFF // skinned models not drawn
        };
        struct Modes
        {
            int32 skin;
            bool32 light; // FALSE: lit draws use vs_world (not under SKIN_GPU)
            bool32 env; // MatFX environment maps
            bool32 fog; // FALSE: the fog stage passes the colour through
            bool32 rtt; // FALSE: cameras drawing into textures are left out
            bool32 shadow; // FALSE: the shadow camera's texture is left out
            bool32 rttFlip; // camera textures drawn upside down: v = 0 their top
            bool32 glow; // the GameCube HDR glow (rw/hdrglow.cpp)
            bool32 warp; // the screen warp (rw/screenwarp.cpp)
            bool32 copyFlip; // a screen copy's rows the other way up (copyScreen)
            bool32 orthoZ; // parallel cameras' clip z from 0 (near) to -1 (far), not -1 to 0
            bool32 copyEdge; // a screen copy's block drawn past the picture (extendCopyEdges)
            bool32 copy; // screen copies (copyScreen: the glow, the warp, the loading snapshot)
            bool32 hudZ; // parallel cameras' models over a window of their depth (atomicDepth)
            // Probe 14's measuring words (TSSM_C3D_AB; all off in a normal run):
            bool32 profile; // tick reads around the parts of a draw's recording (PROF_*)
            bool32 gpuSplit; // the GPU's time per pass: a split and a wait at each pass change
            bool32 fill; // FALSE: the top screen scissored to one pixel (fill=off)
            bool32 cmdFlush; // C3D_FrameEnd(GX_CMDLIST_FLUSH), the command buffer and ring by hand (L2)
            bool32 statsLine; // FALSE: TSSM_C3D_STATS's line is not written
            int32 stereo; // stereomath::MODE_OFF, _AUTO, _FORCE or _ALWAYS (c3dstereo.cpp)
            bool32 stereoGuard; // stereo=guard: MODE_FORCE with the speed guard on (probe 14)
            // FALSE (refresh=off): a model whose vertices changed is instanced
            // again from the start, as before refreshInstance (c3dpipe.cpp).
            bool32 refresh;
            // FALSE (fast=off): L3's caches off (the light cache, the material's
            // table, flushState's fast path), for A/B segments.
            bool32 fast;
            // FALSE (pipe=off): every frame waits for the one before, as before
            // L4, though its buffers stay (TSSM_C3D_PIPE=off leaves them out).
            bool32 pipe;
            // FALSE (eyeskip=off): the right eye replays the shadow camera's
            // pass too, as before speed3 (c3dsubmit.cpp, eyeFreeBegin).
            bool32 eyeSkip;
            // TRUE (alphafill=on): the glow's alpha reset is left out when
            // the frame's fill already gave that alpha (c3dsubmit.cpp,
            // alphaResetSkip). Off by default: untested where it applies (a
            // scene that fills colour), and a wrong prediction costs a frame.
            bool32 alphaFill;
            // FALSE (skintrim=off): the skinned programs' path as before
            // speed3 (b_trim off; shaders/vs_skin.vsh, skin1).
            bool32 skinTrim;
            // TRUE (glowclear=on): the glow's halving and bright targets are
            // cleared before they are drawn over whole, as before speed3 (a
            // tiled GPU's saving; on the PICA a full-target draw for nothing).
            bool32 glowClear;
            // Probe 16's measuring word eyetest (EYETEST_*; normal in a
            // normal run): where the 3D frame's extra GPU time goes.
            int32 eyeTest;
            // FALSE (savevram=off): a 3D frame's starting picture (topSave)
            // is kept in linear memory, as before speed3; on: in VRAM when
            // there is room (c3dsubmit.cpp, saveTopColour).
            bool32 saveVram;
        };
        extern Modes modes;
        enum
        {
            EYETEST_NORMAL,
            // eyetest=2d: 2D frames draw the main camera through the eye code
            // with no shift (b_eye on, u_eyeSide 0): the same picture, the
            // eye code's vertex cost.
            EYETEST_2D,
            // eyetest=noshift: 3D frames with b_eye off: both eyes the same
            // picture, the replay without the eye code.
            EYETEST_NOSHIFT,
            // eyetest=noright: 3D frames whose right eye is only its prologue
            // (the clear, and the starting picture copied back where the
            // frame clears only depth) and its transfer, its lists not sent:
            // the right picture is the clear colour or that starting picture.
            EYETEST_NORIGHT,
            // eyetest=nosave: 3D frames that do not clear the top screen's
            // colour leave out the copy of their starting picture and its
            // copy back for the right eye (two GX copies of 375 KB): the
            // right eye starts on the left eye's picture.
            EYETEST_NOSAVE
        };
        // The start-up self-test's GPU steps (rttSelfTest).
        extern bool32 selfTestOn;
        // Once, at the open: what the console has not shown to work is off
        // unless `effects` (config.ini's [3ds] effects) or a TSSM_C3D_*
        // switch turns it on; the switches win. Logged in one line.
        void modesInit(const char* effects);
        // "skin=cpu+light=off+fog=on" (any of them) over *m; FALSE when a
        // part is not understood (*m is then as far as it got).
        bool32 parseModes(const char* text, Modes* m);
        const char* modesText(const Modes* m, char* buf, uint32 size);

        // ------------------------------------------------------------ device
        // c3ddevice.cpp: the render states as librw asks for them, and the
        // per-draw state the pipelines set; flushState() turns it into
        // citro3d's.
        void setPipelineVertexAlpha(bool32 enable);
        void setTexture(Texture* tex);
        void setMaterial(const RGBA& color, const SurfaceProperties& surf);
        void setWorldMatrix(Matrix* world, const V3d* rebase);
        // Before an atomic's setWorldMatrix: under a parallel camera its
        // depth comes from the camera's depth window (hudz) when it lies in
        // it (c3ddevice.cpp).
        void atomicDepth(Atomic* atomic);
        void setUVMatrix(const float32* xform); // nil: identity
        // A MatFX environment map for the next draws (env nil: none): its
        // texture on unit 1, `rows` (u_envMat) and the pass's constant colour.
        void setEnvMap(Texture* env, const float* rows, uint32 rgba, bool32 modulateByAlpha);
        void setLights(Atomic* atomic); // nil: no lights
        // TSSM_C3D_HUDLOG (c3ddevice.cpp): an atomic under a parallel camera
        // and the program it is drawn with.
        void hudLogAtomic(Atomic* atomic, const char* path);
        void hudLogProgram(int32 prog);
        void setIm2DActive(bool32 active);
        bool32 drawsDiscarded(void); // this camera's draws are left out (modes)
        bool32 flushState(void); // FALSE: leave this draw out
        void deviceStateDirty(void);
        // The combiner back to one stage, for a draw that sets stage 0
        // itself (the clear quad, the benchmark).
        void deviceTevPlain(void);
        void deviceResetStats(void);
        // A texture is going away: unbound in citro3d (which would otherwise
        // read it again on a return from HOME) and in the state filter.
        void deviceForgetTexture(const C3D_Tex* tex);

        // ------------------------------------------------------ camera textures
        // c3drtt.cpp: a Raster::CAMERATEXTURE as a VRAM texture with a render
        // target, drawn into in mid-frame and sampled later in the same frame
        //. rttBegin binds it for the camera's draws (FALSE: they
        // are left out); rttClear clears it with a quad (a GX fill would run
        // before the frame's draws).
        bool32 rttBegin(Camera* cam, Raster* fb);
        void rttClear(Camera* cam, Raster* fb, uint32 rgba, bool32 color, bool32 depth);
        // May a draw sample this camera texture? FALSE leaves the draw out.
        bool32 rttSampleable(const C3dRaster* natras);
        // A screen copy's uv matrix (u_uvMat's two rows): the game's (u, v)
        // over the landscape picture into the portrait copy.
        void screenCopyUV(float rows[8]);
        Image* rttToImage(Raster* raster);
        // At the device's open: draws into two small targets, samples one in
        // the other, fogs a third, reads them back and says what it found.
        void rttSelfTest(void);

        // ------------------------------------------------------------ stereo
        // c3dstereo.cpp: the slider, the eye terms, the convergence plane,
        // the speed guard and their log lines. c3dsubmit.cpp records the
        // frame's lists and sends them again for the right eye.
        enum
        {
            EYE_LEFT,
            EYE_RIGHT
        };
        // Why a 3D frame showed one picture to both eyes.
        enum
        {
            DROP_NONE,
            DROP_SPLIT, // a hard split: the lists before it are gone
            DROP_QUEUE, // the GX queue (or the frame list) has no room for the replay
            DROP_READBACK, // a camera texture read back inside the frame
            DROP_FLAT, // nothing drawn per eye (a menu, a movie, a loading screen)
            DROP_ROOM, // the command buffer or the ring has no room for the prologue
            NUM_DROPS
        };
        void stereoOpen(const char* setting, int32 depth); // at the device's open
        bool32 stereoEnabled(void); // not off: the slider is read, the screen is in 3D mode
        // At each present, before the frame's figures: the settings menu's
        // mode and depth (stereoSetMode, stereoSetDepth) from here.
        void stereoApplySettings(void);
        // Once a frame, before C3D_FrameBegin: the slider, its easing and the
        // guard. TRUE: this frame is drawn for two eyes.
        bool32 stereoFrameStart(void);
        float stereoEyeSide(int32 eye); // u_eyeSide.x: -sigma k / 2
        // beginUpdate: TRUE when this camera's draws are shifted per eye;
        // eyeCam gets u_eyeCam (C, w_min, 0, 0). Called for every camera.
        bool32 stereoCamera(Camera* cam, bool32 screen, float eyeCam[4]);
        // At the present: what the frame was. wall, busy, record and replay in
        // ticks (busy is wall less the waits; record 0 when not timed; replay
        // the right eye's CPU). pipelined: frames may record ahead (L4).
        void stereoFrameDone(bool32 was3D, int32 drop, u64 wall, u64 busy, u64 record,
                             u64 replay, float gpuMsBefore, bool32 pipelined);
        bool32 stereoWantsRecordTiming(void); // the guard predicts from the recording time
        void stereoLevelLoaded(void);
        void stereoStats(uint32 replayed, uint32 replayBytes, uint32 queuePeak3D, u64 replayTicks);
        // The test hook's sleep (TSSM_C3D_STEREO_GUARDTEST), in frameEnsure.
        void stereoGuardTestSleep(void);
        bool32 stereoCaptureLeftWanted(void);
        void submitStereoStats(void); // the stereo line, with the replay's counts

        // ------------------------------------------------------------ submit
        // c3dsubmit.cpp: citro3d itself. The frame, the top screen's target,
        // the frame ring, deferred frees, programs and formats, uniforms, the
        // draw calls, clears, texture storage and the read-back.
        bool32 submitOpen(const OpenParams* params);
        void submitClose(void);
        bool32 submitIsOpen(void);

        void frameEnsure(void); // open a frame if none is (logged)
        // L4: a frame recorded ahead waits for the one before and joins the
        // GPU's queue now (`why` for the log; nil: the present).
        void frameCatchUp(const char* why);

        void frameEnd(void); // C3D_FrameEnd: the frame goes to the GPU
        void frameEndWith(uint8 flags); // the same, with C3D_FrameEnd's flags
        uint32 ringUsed(void);
        void ringFlush(void); // the frame ring's used part, out of the CPU's cache
        bool32 inFrame(void);
        void bindScreen(void); // draw on the top screen from here
        void setScreenViewport(int32 x, int32 y, int32 w, int32 h); // landscape pixels

        // The top screen in pixels, as the game sees it (landscape).
        enum
        {
            SCREEN_W = 400,
            SCREEN_H = 240
        };

        void clearScreen(uint32 rgba, bool32 color, bool32 depth);

        // Stereo: b_eye for the draws from here (set only when it changes;
        // never on in a 2D frame), u_eyeCam through the shadow, and whether
        // this frame is a 3D one.
        void setEyeShift(bool32 on);
        bool32 frameIsStereo(void);
        // The device's half of a full re-send (c3ddevice.cpp): every filter
        // forgets what citro3d was given, so the next draw sets it all.
        void deviceResync(void);

        void* ringAlloc(uint32 bytes); // linear memory that lives this frame
        // A block from ringAlloc that later draws still read (Im3D's
        // vertices): a ring split moves it to the ring's start, so read its
        // address back with ringPinned after each ringAlloc. nil: none.
        void ringPin(void* p, uint32 bytes);
        void* ringPinned(void);
        // The frame so far drawn and waited for, unseen; recording goes on
        //. ringToo: the frame ring starts again.
        void hardSplit(const char* why, bool ringToo);

        void deferFree(void* linearPtr); // linearFree at the next frame
        void deferTexDelete(C3D_Tex* tex); // a copy; C3D_TexDelete at the next frame
        // C3D_RenderTargetDelete, which citro3d refuses inside a frame, before
        // the next C3D_FrameBegin.
        void deferTargetDelete(C3D_RenderTarget* target);

        // Draw on `target` (a camera texture's) from here, its viewport (0, 0,
        // w, h) in target pixels; bindScreen goes back to the top screen.
        void bindTarget(C3D_RenderTarget* target, int32 w, int32 h);
        // Stereo (eyeskip): `target`'s pass from here, up to the next change
        // of target, is the same for both eyes (the shadow camera's, which
        // starts with a clear of the whole target), so the right eye leaves
        // its list out and reads what the left eye drew. Before its bind.
        void eyeFreeBegin(C3D_RenderTarget* target);
        void eyeFreeDepth(void); // a draw with the depth test or write: that pass is sent again
        // alphafill (c3dsubmit.cpp): the top screen's alpha, the glow's mask.
        // xFXHighDynamicRangeStartRender sets it all to one value with a quad
        // (alpha writes only, nothing blended or tested). When the frame's
        // first fill can give it that value already (the value of the frame
        // before's reset), and nothing writes alpha in between (alpha writes
        // into the top screen are held until the reset), the quad changes
        // nothing and is left out.
        // topAlphaDraw: a draw about to be set up (flushState); TRUE: its
        // alpha write is held (left out). alphaResetSkip: the reset quad, with
        // its alpha; TRUE: leave it out. alphaResetDrawn: it was drawn.
        bool32 topAlphaDraw(bool32 writesAlpha, bool32 readsDstAlpha);
        bool32 alphaResetSkip(uint32 a);
        void alphaResetDrawn(uint32 a);
        void submitAlphaStats(void); // the stats line's alphafill line
        bool32 targetIsScreen(void); // the top screen is bound (or none yet)
        C3D_RenderTarget* boundTarget(void); // nil: none yet this frame
        // A quad over the bound target's viewport: colour and/or depth (0).
        void clearQuad(uint32 rgba, bool32 color, bool32 depth);
        // A camera texture's storage and target: RGBA8 in VRAM, D16 when
        // `depth`. FALSE: no room.
        bool32 targetCreate(C3dRaster* natras, int32 w, int32 h, bool32 depth);
        // `tex` (RGBA8, in VRAM) read back as bytes A, B, G, R in rows, the
        // first the target's top (clip y = +1). Inside a frame the frame so
        // far is drawn and waited for first (a hard split): debug only.
        bool32 readTexture(const C3D_Tex* tex, uint8* abgr);
        // Outside a frame: waits until the GPU and the GX engine are done
        // with everything sent (the self-test's "GPU finished" lines).
        void gpuWait(void);
        // The fog ramp (6.5): 64 x 8 A8, texel i = i / 63; nil if not made.
        C3D_Tex* fogRamp(void);
        // The ramp onto texture unit 2, once: the unit cannot be unbound, so
        // every later draw samples it (logged when it happens).
        void bindFogRamp(const char* why);
        // Unit 2 onto the 8x8 dummy (it cannot be unbound), so fog=off in
        // TSSM_C3D_AB leaves out the ramp's reads too.
        void dropFogRamp(const char* why);
        // The top screen's colour so far into the raster's copy block (made at
        // its first copy: `own` its own, else the one the passes share), as a
        // 256 x 512 RGBA8 texture, portrait, the picture at target pixels
        // (0, 0)-(240, 400). Inside a frame the frame so far is split off and
        // the copy queued after it (two GX queue entries). FALSE: no memory.
        bool32 copyScreenInto(C3dRaster* natras, bool32 own);
        // The top screen drawn on but not shown (the self-test's frames).
        void screenOutput(bool32 on);

        // The bottom screen's target (the overlay's, c3doverlay.cpp): 240 x
        // 320 RGBA8 in VRAM, shown at the frame's end when drawn on. Made once;
        // FALSE: no VRAM. bindBottom draws on it from here, its whole viewport.
        bool32 bottomCreate(bool32 depth);
        bool32 bottomExists(void);
        void bindBottom(void);
        // The bottom target as the LCD shows it: 320 x 240 RGBA, top row
        // first. Outside a frame. FALSE: none, or no memory.
        bool32 readBottomTarget(uint8* rgba);
        // c3doverlay.cpp: its textures freed, at the close (the GPU is idle).
        void overlayClose(void);

        void useProgram(int32 prog);
        void useFormat(int32 format, const void* vertices, uint32 stride);
        C3D_FVec* uniformPtr(int32 reg, int32 count); // marks them dirty
        // Each returns what it sent (a register equal to its shadow is not).
        bool32 setUniform4(int32 reg, float x, float y, float z, float w);
        int32 setUniformRows(int32 reg, const float* rows, int32 count); // rows of 4

        // An 8x8 texture that lives as long as citro3d: C3D_TexBind cannot
        // take nil for units 1 and 2 (it reads the texture's type).
        C3D_Tex* dummyTexture(void);

        void drawArrays(uint32 prim, int32 first, int32 count);
        void drawElements(uint32 prim, int32 count, const uint16* indices);

        // Texture storage. `levels` is the whole chain, tiled and packed,
        // level 0 first. Outside a frame it goes to VRAM by a GX copy from a
        // flushed staging buffer, inside one (or when VRAM is full) into
        // linear memory by the CPU.
        bool32 texCreate(C3dRaster* natras, int32 w, int32 h, int32 maxLevel, uint32 gpuFormat,
                         const void* levels, uint32 bytes);
        void texDestroy(C3dRaster* natras);
        // A texture's texels written again in place (the same size, format
        // and levels; `bytes` is the whole chain), outside a frame:
        // texWriteBegin says where to write them (nil: it cannot be done, make
        // a new texture), texWriteEnd puts them in.
        void* texWriteBegin(C3dRaster* natras, uint32 bytes);
        void texWriteEnd(C3dRaster* natras, void* at, uint32 bytes);

        void noteStats(int32 drawsThisFrame);
        // Per-frame counters for the stats line.
        enum
        {
            COUNT_SKIN_VERTS, // vertices skinned on the GPU
            COUNT_SKIN_PIECES, // skin pieces drawn
            COUNT_BONE_REGS, // palette registers sent
            COUNT_ENV_DRAWS, // draws with an environment map
            COUNT_FOG_DRAWS, // draws through the fog stage
            COUNT_TARGET_BINDS, // render targets bound (the top screen too)
            COUNT_RTT_DRAWS, // draws into camera textures
            COUNT_RTT_CLEARS, // camera texture clears (quads)
            COUNT_READBACKS, // camera textures read back (each a hard split)
            COUNT_COPIES, // screen copies (each a split and a GX copy)
            // What reached citro3d past the device's own filters (probe 14):
            COUNT_TEX_BINDS, // C3D_TexBind
            COUNT_PROG_BINDS, // C3D_BindProgram
            COUNT_ATTR_SETS, // C3D_SetAttrInfo (a vertex format change)
            COUNT_BUF_SETS, // BufInfo_Add (a new vertex pointer)
            COUNT_UNIFORM_REGS, // vertex shader uniform registers written
            COUNT_BLEND, // C3D_AlphaBlend
            COUNT_ALPHA, // C3D_AlphaTest
            COUNT_DEPTH, // C3D_DepthTest
            COUNT_CULL, // C3D_CullFace
            COUNT_DEPTHMAP, // C3D_DepthMap
            COUNT_TEV, // combiner set-ups (setTev, the plain one)
            COUNT_FOGSTAGE, // the fog stage set
            NUM_COUNTS
        };
        void noteCount(int32 which, uint32 n);
        void noteSkinTicks(u64 ticks); // the bone matrices, on the CPU
        void getSubmitStats(Stats* out);
        // The textures' and camera textures' counts and bytes only, the
        // peaks left alone (probe 14's segments).
        void getMemoryStats(Stats* out);

        // Where a frame's time goes (TSSM_C3D_STATS or TSSM_C3D_AB), in
        // system ticks. Wall is present to present; recording is the CPU in
        // the device's draw and clear calls, without the waits below.
        enum
        {
            TIME_RECORD,
            TIME_CPUSKIN, // part of TIME_RECORD
            TIME_SYNC, // C3D_FrameSync: the vblank pacing
            TIME_GPU, // C3D_FrameBegin after it: the frame before still drawing
            TIME_END, // C3D_FrameEnd (the linear heap's cache flush)
            TIME_DRAWN, // citro3d's GPU time of the frame before (C3D_GetDrawingTime)
            TIME_UPDATE, // the present before to this frame's first draw or clear: the update
            TIME_BONES, // the bone matrices and palettes (part of TIME_RECORD)
            NUM_TIMES
        };
        // profile=on (probe 14): where a draw's recording goes, each part
        // with its own waits left out. PROF_FLUSH is inside PROF_MESH.
        enum
        {
            PROF_MATRIX, // an atomic's matrix (atomicDepth, setWorldMatrix)
            PROF_LIGHTS, // setLights (enumerateLights, the axes, the light rows)
            PROF_MESH, // beginMesh: material, texture, MatFX, flushState
            PROF_FLUSH, // flushState alone
            PROF_C3D, // useProgram, useFormat, the draw call (and Im2D's uniforms)
            PROF_PALETTE, // the skin palette's registers
            PROF_IM, // Im2D/Im3D: the ring copy, w normalised, Im3D's box, rebase and matrix
            NUM_PROF
        };
        // gpusplit=on (probe 14): the GPU's time a frame per pass, from a split
        // at each change of pass, each piece waited for and timed by citro3d.
        enum
        {
            PASS_3D, // the top screen under a perspective camera
            PASS_HUD, // the top screen under a parallel camera, or 2D: the HUD, menus
            PASS_SHADOW, // the shadow camera's texture
            PASS_RTT, // other camera textures outside the screen passes
            PASS_GLOW, // inside the HDR glow (its half, blur and glow targets, composite)
            PASS_WARP, // inside the screen warp
            PASS_COPY, // screen copies (the GX engine)
            PASS_BOTTOM, // the bottom screen (the overlay)
            PASS_PRESENT, // the frame's display transfers
            NUM_PASSES
        };
        struct FrameTimes
        {
            u64 wall;
            u64 t[NUM_TIMES];
            uint32 verts[NUM_PROGRAMS]; // indices (or vertices) drawn per program
            uint32 reads; // the timing's own tick reads
            // Probe 14's (c3dab.cpp's segments):
            u64 prof[NUM_PROF];
            float passMs[NUM_PASSES];
            uint32 pieces; // gpusplit's timed pieces
            uint32 profReads; // profile=on's own tick reads (in reads too)
            uint32 counts[NUM_COUNTS];
            uint32 draws; // draw calls recorded
            uint32 cmdBytes; // command buffer used
            uint32 ringBytes;
            uint32 queue; // GX queue entries
            uint32 splits; // hard splits (not gpusplit's)
        };
        bool32 timingOn(void);
        void noteTicks(int32 which, u64 ticks);
        // Sums over the frames since the last call (the stats line).
        void takeFrameSums(FrameTimes* sum, uint32* frames, u64* wallMax);
        uint32 tickReadNs(void); // what one svcGetSystemTick costs
        // A device call's CPU time into TIME_RECORD, waits left out; an
        // inner one (Im2D's triangle through its primitive) is not counted
        // twice.
        struct RecordScope
        {
            u64 t0, w0;
            bool on; // counted in the depth (the timing was on when it began)
            RecordScope(void);
            ~RecordScope(void);
        };
        // profile=on: the scope's CPU time, its waits left out, into
        // FrameTimes::prof[which]. Nothing otherwise.
        struct ProfScope
        {
            int32 which;
            u64 t0, w0;
            explicit ProfScope(int32 which);
            ~ProfScope(void);
        };
        void noteProf(int32 which, u64 ticks); // ticks read anyway (the palette)
        // gpusplit: the pass the next draw belongs to (c3ddevice.cpp), and
        // the screen pass running (-1 none, 0 the glow, 1 the warp: rw's
        // iScreenPassBegin/End through screenPassScope, c3drtt.cpp).
        int32 devicePass(void);
        // TSSM_C3D_GEOMSTAT: the next draw's raster and blend, in words.
        void deviceDescribe(char* buf, int32 size);
        int32 screenPassRunning(void);
        void abNoteFrame(const FrameTimes* f); // c3dab.cpp, at each present
        // A line in TSSM_C3D_AB_LOG (probe 14's speed14.log), when it is set.
        void abLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
        void runBench2(void); // c3dbench.cpp: TSSM_C3D_BENCH=2's variants

        // TSSM_C3D_VBVRAM=on (probe 14's run 1b): geometry's vertex buffers
        // moved into VRAM after the frame they were made in, while VRAM
        // keeps a margin (c3dpipe.cpp).
        bool32 vbVramOn(void);
        void vbVramQueue(InstanceDataHeader* h);
        void vbVramForget(InstanceDataHeader* h); // freed before it moved
        void vbVramMigrate(void); // outside a frame (showRaster)
        void vbVramStats(uint32* movedKB, uint32* stayedKB); // since the open
        // linearFree or vramFree, by where `p` is.
        void gpuFree(void* p);

        // Probe 14's 3D segments (TSSM_C3D_AB's stereo=off|force|guard;
        // c3dstereo.cpp): what a segment drew, since the last take.
        struct StereoProbeStats
        {
            uint32 frames; // frames presented
            uint32 frames3d; // drawn as 3D frames (recorded for two eyes)
            uint32 rightDrawn; // ... and shown with both eyes
            uint32 droppedSplit; // right eyes dropped for a hard split (DROP_SPLIT)
            uint32 droppedQueue; // ... the GX queue or the frame list (DROP_QUEUE)
            uint32 droppedReadback; // ... a read-back inside the frame (DROP_READBACK)
            uint32 droppedFlat; // ... nothing drawn per eye (DROP_FLAT)
            uint32 droppedRoom; // ... no room for the prologue (DROP_ROOM)
            uint32 guardFrames; // frames 2D or easing out under the speed guard
            uint32 guardTrips;
            uint32 gxEntriesPeak; // GX queue entries in a two-eye frame
        };
        // At a segment's start. mode MODE_FORCE: both eyes, the slider taken
        // as full, the guard on or off, `depth` for stereo_depth, the guard's
        // state reset; otherwise the setting's guard and depth again. FALSE:
        // no 3D (the device has none).
        bool32 stereoProbeSet(int32 mode, bool32 guard, int32 depth);
        // The counts since the last call; FALSE: no 3D.
        bool32 stereoProbeTake(StereoProbeStats* out);
        // A two-eye frame's GX queue entries (c3dsubmit.cpp), for the take.
        void stereoProbeQueue(uint32 entries);
        void runBench(void); // c3dbench.cpp: TSSM_C3D_BENCH=1
        void logOnce(const char* key, const char* fmt, ...);

        // --------------------------------------------------------- matrices
        // Row-major 4x4, for column vectors (clip = M * p): what the shaders'
        // dp4 rows want.
        struct Mat4
        {
            float m[4][4];
        };
        void mat4Mul(Mat4* dst, const Mat4* a, const Mat4* b); // a * b
        // The current camera's view-projection (tilt included), from
        // beginUpdate.
        const Mat4* viewProj(void);
        bool32 cameraIsScreen(void);
    } // namespace c3d
} // namespace rw

#endif
