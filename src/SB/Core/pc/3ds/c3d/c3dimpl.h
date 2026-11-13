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
            NUM_REGS = 82
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
        };

        void* destroyNativeData(void* object, int32, int32);
        ObjPipeline* makeDefaultPipeline(void);
        ObjPipeline* makeUVTransformPipeline(void);
        ObjPipeline* makeMatFXPipeline(void);
        ObjPipeline* makeSkinPipeline(bool32 matfx);

        // c3dskin.cpp. instanceSkin: nil for a geometry without its weights.
        InstanceDataHeader* instanceSkin(Geometry* geo);
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
        };
        extern Modes modes;
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

        // ------------------------------------------------------------ submit
        // c3dsubmit.cpp: citro3d itself. The frame, the top screen's target,
        // the frame ring, deferred frees, programs and formats, uniforms, the
        // draw calls, clears, texture storage and the read-back.
        bool32 submitOpen(const OpenParams* params);
        void submitClose(void);
        bool32 submitIsOpen(void);

        void frameEnsure(void); // open a frame if none is (logged)
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
            NUM_COUNTS
        };
        void noteCount(int32 which, uint32 n);
        void noteSkinTicks(u64 ticks); // the bone matrices, on the CPU
        void getSubmitStats(Stats* out);

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
            NUM_TIMES
        };
        struct FrameTimes
        {
            u64 wall;
            u64 t[NUM_TIMES];
            uint32 verts[NUM_PROGRAMS]; // indices (or vertices) drawn per program
            uint32 reads; // the timing's own tick reads
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
            RecordScope(void);
            ~RecordScope(void);
        };
        void abNoteFrame(const FrameTimes* f); // c3dab.cpp, at each present
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
