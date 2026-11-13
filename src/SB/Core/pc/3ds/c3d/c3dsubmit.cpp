// rw::c3d: the part that talks to citro3d.
//
// The frame: citro3d records into one command buffer and starts the GPU only
// at C3D_FrameEnd, so nothing drawn reaches the screen before the present,
// and the GPU draws frame N while the game updates frame N+1. A frame opens
// at the first clear or draw after a present (frameEnsure) and closes at the
// present (frameEnd). C3D_FrameBegin waits for the GPU to finish the frame
// before, which is what makes the frame ring and the deferred frees safe.
//
// The CPU's data cache: the GPU and the GX engine read memory, not the cache.
// C3D_FrameEnd(0) flushes the whole linear heap, which covers everything a
// frame writes; a texture copied to VRAM between frames runs at once, so its
// staging buffer is flushed by hand first.
//
// Waiting for the GX engine outside a frame: citro3d's C3D_Sync* calls wait
// for their transfer with gspWaitForEvent(GSPGPU_EVENT_PPF, false), and that
// event is sticky and never cleared by anything else here, so once a frame
// has been shown (its own transfer to the screen raised it) the wait returns
// at once, with the transfer still running. Only on a console: Azahar runs a
// transfer when it is submitted. The queue itself knows when it is done, so
// what must have finished is waited for with gpuIdle(), and the two staging
// halves take turns (stagingFor).

#include "c3dimpl.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "c3d_shbin.h" // kC3dShbin, made by CMakeLists.txt from shaders/*.vsh
#include "../i3DS.h"

namespace rw
{
    namespace c3d
    {
        namespace
        {
            const uint32 kDefaultCmdBuf = 2u << 20;
            const uint32 kRingBytes = 1u << 20;
            const uint32 kStagingBytes = 1u << 20; // two halves, used in turn
            const uint32 kStagingHalf = kStagingBytes / 2;
            const int32 kFirstDeferred = 1024; // the list grows as it must

            struct Deferred
            {
                void* linear; // linearFree
                C3D_Tex tex; // C3D_TexDelete, when hasTex
                bool hasTex;
            };

            struct State
            {
                bool open;
                bool inFrame;
                C3D_RenderTarget* bound; // the target draws go to (nil: none yet this frame)
                bool screenDrawn; // a draw into the top screen was recorded this frame
                C3D_RenderTarget* top;
                C3D_RenderTarget* bottom; // the overlay's (nil: none)
                uint32 bottomFlags; // its output
                DVLB_s* dvlb;
                shaderProgram_s programs[NUM_PROGRAMS];
                C3D_Tex dummy; // texture unit 1 when nothing is on it
                bool hasDummy;
                C3D_Tex ramp; // the fog ramp on texture unit 2
                bool hasRamp;
                bool rampBound; // unit 2 holds it (bindFogRamp)
                C3D_Tex copy; // the screen passes' copy block (copyScreenInto)
                C3D_RenderTarget* copyTarget; // drawn into for its edges (nil: linear memory)
                bool hasCopy;
                bool copyOnVram;
                int32 copyUsers; // rasters on it
                C3D_Tex edgeTex; // a block, nearest, while its edges are drawn
                // A copy made outside a frame: its edges are drawn when the
                // next frame opens (nil: none).
                C3D_RenderTarget* edgePending;
                C3D_Tex edgePendingTex;
                // Camera textures and own copy blocks with storage, freed at
                // the close (the shared block is s.copy).
                C3dRaster** live;
                int32 numLive;
                int32 liveCap;
                C3D_RenderTarget** deadTargets; // deferTargetDelete's
                int32 numDead;
                int32 deadCap;
                int32 program;
                C3D_AttrInfo attr[NUM_FORMATS];
                int32 fixedIndex[NUM_FORMATS][4]; // attribute index of a fixed v0..v3, -1
                int32 format;
                const void* buffer;
                uint32 bufferStride;
                C3D_FVec shadow[NUM_REGS];
                bool shadowValid[NUM_REGS];

                uint8* ring;
                uint32 ringUsed;
                uint32 ringPeak;
                uint8* pin; // see ringPin
                uint32 pinBytes;
                int32 viewport[4]; // the bound target's viewport, target pixels
                uint8* staging;
                uint32 stagingBusy; // the half the latest GX copy read from

                Deferred* deferred;
                int32 numDeferred;
                int32 deferredCap;
                int32 deferredDrawn; // entries made before the latest draw

                uint32 transferFlags; // the top target's output
                uint32 cmdBytes; // the command buffer's size
                uint32 hardSplits; // since the device opened
                uint32 drawsThisFrame; // recorded, splits included
                uint32 queueEntries; // GX queue entries this frame
                uint32 queuePeak;
                uint32 cmdPeak;
                uint32 drawsPeak;
                uint32 drawsLast;
                uint32 frames;
                uint32 outsideFrame;

                uint32 textures;
                uint32 texVram;
                uint32 texLinear;
                uint32 inFrameUploads;
                uint32 targets; // camera textures with storage
                uint32 targetVram; // their colour and depth
                uint32 loadTextures; // made outside a frame since the last memory line
                // TSSM_C3D_SPLITTEST=1: a hard split after the first draw into a
                // camera texture in each frame, to prove the split keeps the
                // target and what its draws read.
                bool splitTest;
                bool splitDone;

                uint32 count[NUM_COUNTS]; // this frame
                uint32 countPeak[NUM_COUNTS];
                u64 skinTicks;
                u64 skinTicksPeak;

                // Frame timing (timingOn): this frame's, and the sums since
                // the stats line took them.
                bool timing;
                FrameTimes cur;
                FrameTimes sum;
                uint32 sumFrames;
                u64 wallMax;
                u64 lastPresent;
                u64 waitTotal; // TIME_SYNC + TIME_GPU since the device opened
                int32 recordDepth;
                uint32 tickNs;
            };

            State s;

            // A line of the quit's (3ds/i3DS.h), only while quitting.
            void quitStep(const char* what)
            {
                if (i3DSQuitting())
                {
                    i3DSQuitLine("c3d: %s", what);
                }
            }

            // A frame cut short (a hard split, a read-back) is not shown:
            // neither screen's target may go out with it.
            void detachOutputs(void)
            {
                C3D_RenderTargetDetachOutput(s.top);
                if (s.bottom != nil)
                {
                    C3D_RenderTargetDetachOutput(s.bottom);
                }
            }

            void attachOutputs(void)
            {
                C3D_RenderTargetSetOutput(s.top, GFX_TOP, GFX_LEFT, s.transferFlags);
                if (s.bottom != nil)
                {
                    C3D_RenderTargetSetOutput(s.bottom, GFX_BOTTOM, GFX_LEFT, s.bottomFlags);
                }
            }

            uint32 transferFormatFor(GSPGPU_FramebufferFormat f)
            {
                switch (f)
                {
                case GSP_RGBA8_OES:
                    return GX_TRANSFER_FMT_RGBA8;
                case GSP_BGR8_OES:
                    return GX_TRANSFER_FMT_RGB8;
                case GSP_RGB565_OES:
                    return GX_TRANSFER_FMT_RGB565;
                case GSP_RGB5_A1_OES:
                    return GX_TRANSFER_FMT_RGB5A1;
                case GSP_RGBA4_OES:
                    return GX_TRANSFER_FMT_RGBA4;
                default:
                    return GX_TRANSFER_FMT_RGB8;
                }
            }

            void buildFormats(void)
            {
                for (int32 f = 0; f < NUM_FORMATS; f++)
                {
                    C3D_AttrInfo* ai = &s.attr[f];
                    AttrInfo_Init(ai);
                    for (int i = 0; i < 4; i++)
                    {
                        s.fixedIndex[f][i] = -1;
                    }

                    if (f < NUM_MODEL_FORMATS)
                    {
                        AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
                        if (f & MODEL_NORMAL)
                            AttrInfo_AddLoader(ai, 1, GPU_BYTE, 4);
                        else
                            s.fixedIndex[f][1] = AttrInfo_AddFixed(ai, 1);
                        if (f & MODEL_COLOR)
                            AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
                        else
                            s.fixedIndex[f][2] = AttrInfo_AddFixed(ai, 2);
                        if (f & MODEL_UV)
                            AttrInfo_AddLoader(ai, 3, GPU_FLOAT, 2);
                        else
                            s.fixedIndex[f][3] = AttrInfo_AddFixed(ai, 3);
                        if (f & MODEL_SKIN)
                        {
                            AttrInfo_AddLoader(ai, 4, GPU_UNSIGNED_BYTE, 4);
                            AttrInfo_AddLoader(ai, 5, GPU_UNSIGNED_BYTE, 4);
                        }
                    }
                    else if (f == FORMAT_IM2D)
                    {
                        AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 4);
                        AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
                        AttrInfo_AddLoader(ai, 3, GPU_FLOAT, 2);
                    }
                    else
                    {
                        AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
                        AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3);
                        AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
                        AttrInfo_AddLoader(ai, 3, GPU_FLOAT, 2);
                    }
                }
            }

            // The loaders of a format, in buffer order, as BufInfo_Add's
            // permutation (attribute indices, first in the low nibble).
            void bufferLayout(int32 f, int* count, uint64* perm)
            {
                int n = 0;
                uint64 p = 0;
                const C3D_AttrInfo* ai = &s.attr[f];
                for (int i = 0; i < ai->attrCount; i++)
                {
                    bool fixed = false;
                    for (int r = 0; r < 4; r++)
                    {
                        if (s.fixedIndex[f][r] == i)
                        {
                            fixed = true;
                        }
                    }
                    if (!fixed)
                    {
                        p |= (uint64)i << (4 * n);
                        n++;
                    }
                }
                *count = n;
                *perm = p;
            }

            void freeEntry(Deferred* d)
            {
                if (d->linear != nil)
                {
                    linearFree(d->linear);
                }
                if (d->hasTex)
                {
                    C3D_TexDelete(&d->tex);
                }
            }

            void freeDeferred(void)
            {
                for (int32 i = 0; i < s.numDeferred; i++)
                {
                    freeEntry(&s.deferred[i]);
                }
                s.numDeferred = 0;
                s.deferredDrawn = 0;
            }

            // After a hard split: what the draws recorded so far used can go;
            // what came after the latest draw may be the next draw's own
            // (a spilled buffer), and the pinned block is still read.
            void freeDeferredDrawn(void)
            {
                int32 kept = 0;
                for (int32 i = 0; i < s.numDeferred; i++)
                {
                    Deferred* d = &s.deferred[i];
                    if (i < s.deferredDrawn && !(d->linear != nil && d->linear == s.pin))
                    {
                        freeEntry(d);
                    }
                    else
                    {
                        s.deferred[kept++] = *d;
                    }
                }
                s.numDeferred = kept;
                s.deferredDrawn = 0;
            }

            // Outside a frame: waits until the GPU and the GX engine have done
            // everything queued so far. It is an empty frame: C3D_FrameBegin
            // waits for the queue (gxCmdQueueWait, which tracks completion
            // itself, unlike the PPF event; see the top of the file), nothing
            // is recorded, no target was drawn on so nothing is shown, and
            // GX_CMDLIST_FLUSH spares it C3D_FrameEnd's flush of the whole
            // linear heap.
            void gpuIdle(void)
            {
                if (s.inFrame)
                {
                    return;
                }
                C3D_FrameBegin(0);
                C3D_FrameEnd(GX_CMDLIST_FLUSH);
            }

            // Outside a frame, what the deferred list holds can go as soon as
            // the GPU is idle. A level's unload lets go of its textures
            // between frames, and the next level's load may follow with no
            // frame in between: without this its textures would find VRAM
            // still taken and go to linear memory. FALSE: nothing was freed.
            void deleteDeadTargets(void);

            bool reclaim(void)
            {
                if (s.inFrame || (s.numDeferred == 0 && s.numDead == 0))
                {
                    return false;
                }
                logOnce("reclaim", "memory ran short between frames: what was freed since the last "
                                   "frame was let go first");
                gpuIdle();
                freeDeferred();
                deleteDeadTargets();
                return true;
            }

            Deferred* newDeferred(void)
            {
                if (s.numDeferred >= s.deferredCap)
                {
                    // Full: twice the room. Emptying it is not an option
                    // inside a frame, whose own draws may still use what it
                    // holds.
                    const int32 cap = s.deferredCap * 2;
                    Deferred* more = (Deferred*)realloc(s.deferred, (size_t)cap * sizeof(Deferred));
                    if (more != nil)
                    {
                        s.deferred = more;
                        s.deferredCap = cap;
                    }
                    else if (!reclaim())
                    {
                        // No memory for a bigger list, inside a frame: the
                        // frame is cut short so that the GPU can be waited for.
                        logOnce("deferfull",
                                "the deferred-free list could not grow; a frame was cut "
                                "short to empty it");
                        frameEnd();
                        gpuIdle();
                        freeDeferred();
                    }
                }
                Deferred* d = &s.deferred[s.numDeferred++];
                d->linear = nil;
                d->hasTex = false;
                return d;
            }

            // Linear memory to stage `bytes` of texels in for a GX copy made
            // outside a frame (copyToVram). Each C3D_SyncTextureCopy waits for
            // the queue before it starts, but not reliably for its own copy
            // (the top of the file), so only the latest copy may still be
            // reading: the half of the staging buffer it did not read from is
            // free. A chain too big for a half gets linear memory of its own,
            // which *own says and the caller defers freeing.
            uint8* stagingFor(uint32 bytes, bool* own)
            {
                if (bytes <= kStagingHalf)
                {
                    *own = false;
                    return s.staging + (s.stagingBusy ^ 1) * kStagingHalf;
                }
                *own = true;
                uint8* p = (uint8*)linearAlloc(bytes);
                if (p == nil && reclaim())
                {
                    p = (uint8*)linearAlloc(bytes);
                }
                return p;
            }

            // Staged texels into a VRAM texture: flushed from the CPU's cache
            // (citro3d does not), then a GX copy, which outside a frame runs
            // now, after the queue before it.
            void copyToVram(C3D_Tex* tex, uint8* from, uint32 bytes)
            {
                GSPGPU_FlushDataCache(from, bytes);
                C3D_TexLoadImage(tex, from, GPU_TEXFACE_2D, -1);
                if (from >= s.staging && from < s.staging + kStagingBytes)
                {
                    s.stagingBusy = (uint32)(from - s.staging) / kStagingHalf;
                }
            }

            bool initTex(C3D_Tex* tex, C3D_TexInitParams p, bool onVram)
            {
                p.onVram = onVram;
                return C3D_TexInitWithParams(tex, nil, p);
            }

            // The fog ramp: 64 x 8 A8, texel column i = i / 63,
            // linear and clamped, so the shaders' outtc2 reads GL3's factor.
            bool makeRamp(void)
            {
                C3D_TexInitParams p;
                memset(&p, 0, sizeof(p));
                p.width = 64;
                p.height = 8;
                p.format = GPU_A8;
                p.type = GPU_TEX_2D;
                bool vram = initTex(&s.ramp, p, true);
                if (!vram && !initTex(&s.ramp, p, false))
                {
                    return false;
                }
                uint8 texels[64 * 8];
                for (uint32 y = 0; y < 8; y++)
                {
                    for (uint32 x = 0; x < 64; x++)
                    {
                        // c3draster.cpp's tiledIndex for a 64 x 8 picture
                        const uint32 fy = 7 - y;
                        const uint32 tile = (fy >> 3) * 8 + (x >> 3);
                        const uint32 tx = x & 7;
                        const uint32 ty = fy & 7;
                        const uint32 morton = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) |
                                              ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
                        texels[tile * 64 + morton] = (uint8)((x * 255 + 31) / 63);
                    }
                }
                if (vram)
                {
                    memcpy(s.staging, texels, sizeof(texels));
                    copyToVram(&s.ramp, s.staging, sizeof(texels));
                    gpuIdle();
                }
                else
                {
                    memcpy(s.ramp.data, texels, sizeof(texels));
                    C3D_TexFlush(&s.ramp);
                }
                C3D_TexSetFilter(&s.ramp, GPU_LINEAR, GPU_LINEAR);
                C3D_TexSetWrap(&s.ramp, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                printf("tssm: c3d: fog: the ramp (64x8 A8, %s) for texture unit 2 and combiner "
                       "stage 4 is made; fog=%s\n",
                       vram ? "VRAM" : "linear memory", modes.fog ? "on" : "off");
                return true;
            }

            // A screen copy's block: 256 x 512 RGBA8, the top target's 240 x
            // 400 written into it row of tiles for row of tiles (30 tiles to
            // a row there, 32 here), after 14 rows of tiles, so that target
            // pixel (x, y) is where a 256 x 512 target's would be: (x / 256,
            // y / 512), as a camera texture samples (c3drtt.cpp).
            const uint32 kCopyW = 256;
            const uint32 kCopyH = 512;
            const uint32 kCopyBytes = kCopyW * kCopyH * 4;
            const uint32 kCopySkip = 14 * (kCopyW / 8) * 256;
            // Bit 2 of the transfer flags, for a raw copy with gaps between
            // its lines (read from 3dbrew, GPU/External Registers: TextureCopy;
            // not confirmed on a console). libctru has no name for it and
            // citro3d never sets it.
            const uint32 kTransferGaps = 1u << 2;

            bool makeCopyTex(C3D_Tex* tex, bool* onVram)
            {
                C3D_TexInitParams p;
                memset(&p, 0, sizeof(p));
                p.width = (u16)kCopyW;
                p.height = (u16)kCopyH;
                p.format = GPU_RGBA8;
                p.type = GPU_TEX_2D;
                bool vram = initTex(tex, p, true);
                if (!vram && reclaim())
                {
                    vram = initTex(tex, p, true);
                }
                if (!vram)
                {
                    if (!initTex(tex, p, false))
                    {
                        return false;
                    }
                    // No dirty line of an earlier owner may be written back
                    // over what the GX engine copies in.
                    GSPGPU_FlushDataCache(tex->data, kCopyBytes);
                }
                *onVram = vram;
                s.targets++;
                s.targetVram += kCopyBytes;
                return true;
            }

            // A block's own target, for its edges (citro3d draws only into
            // VRAM: nil for a block in linear memory).
            C3D_RenderTarget* copyTargetFor(C3D_Tex* tex, bool onVram)
            {
                return onVram ? C3D_RenderTargetCreateFromTex(tex, GPU_TEXFACE_2D, 0,
                                                              C3D_DEPTHTYPE(-1)) :
                                nil;
            }

            // What this frame has used of the command buffer. A frame split (a
            // screen copy) moves the buffer's start past what it sent, so the
            // offset alone counts only what came after the last one.
            uint32 cmdUsedBytes(void)
            {
                u32* buf = nil;
                u32 size = 0;
                u32 offset = 0;
                GPUCMD_GetBuffer(&buf, &size, &offset);
                const uint32 left = (size - offset) * 4;
                return left < s.cmdBytes ? s.cmdBytes - left : 0;
            }

            void deleteDeadTargets(void)
            {
                // citro3d waits for the GPU itself here (the target is not
                // linked to a screen) and refuses to do it inside a frame.
                for (int32 i = 0; i < s.numDead; i++)
                {
                    C3D_RenderTargetDelete(s.deadTargets[i]);
                }
                s.numDead = 0;
            }

            void liveAdd(C3dRaster* natras)
            {
                if (s.numLive >= s.liveCap)
                {
                    const int32 cap = s.liveCap > 0 ? s.liveCap * 2 : 16;
                    C3dRaster** more =
                        (C3dRaster**)realloc(s.live, (size_t)cap * sizeof(C3dRaster*));
                    if (more == nil)
                    {
                        logOnce("livefull", "a camera texture could not be listed; the close "
                                            "leaves it to C3D_Fini");
                        return;
                    }
                    s.live = more;
                    s.liveCap = cap;
                }
                s.live[s.numLive++] = natras;
            }

            void liveRemove(C3dRaster* natras)
            {
                for (int32 i = 0; i < s.numLive; i++)
                {
                    if (s.live[i] == natras)
                    {
                        s.live[i] = s.live[--s.numLive];
                        return;
                    }
                }
            }

            // The passes' copy block, made once: freed and made again in a
            // level it would leave a hole among that level's textures.
            bool ensureSharedCopy(void)
            {
                if (s.hasCopy)
                {
                    return true;
                }
                if (!makeCopyTex(&s.copy, &s.copyOnVram))
                {
                    return false;
                }
                s.hasCopy = true;
                s.copyUsers = 0;
                s.copyTarget = copyTargetFor(&s.copy, s.copyOnVram);
                printf("tssm: c3d: screen copy: the passes' block, 256x512 RGBA8 in %s (512 KB); "
                       "VRAM free %u KB\n",
                       s.copyOnVram ? "VRAM" : "linear memory", (unsigned)(vramSpaceFree() >> 10));
                fflush(stdout);
                return true;
            }

            void drawPendingEdges(void);
        } // namespace

        void logOnce(const char* key, const char* fmt, ...)
        {
            static const char* seen[64];
            static int nseen;
            for (int i = 0; i < nseen; i++)
            {
                if (seen[i] == key || strcmp(seen[i], key) == 0)
                {
                    return;
                }
            }
            if (nseen < 64)
            {
                seen[nseen++] = key;
            }
            char line[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(line, sizeof(line), fmt, ap);
            va_end(ap);
            printf("tssm: c3d: %s\n", line);
            fflush(stdout);
        }

        uint32 modelStride(uint32 format)
        {
            return 12 + ((format & MODEL_NORMAL) ? 4 : 0) + ((format & MODEL_COLOR) ? 4 : 0) +
                   ((format & MODEL_UV) ? 8 : 0) + ((format & MODEL_SKIN) ? 8 : 0);
        }

        namespace
        {
            // video.framerate as C3D_FrameRate's rate, the same before the
            // device opens and after: 0 (no cap) is every vblank.
            int32 vblankRate(int32 fps)
            {
                return fps > 0 ? fps : 60;
            }
        } // namespace

        bool32 submitOpen(const OpenParams* params)
        {
            if (s.open)
            {
                return 1;
            }

            uint32 cmdBytes = params != nil && params->cmdBufBytes > 0 ?
                                  (uint32)params->cmdBufBytes :
                                  kDefaultCmdBuf;
            int32 fps = params != nil ? vblankRate(params->frameRate) : 30;

            if (!C3D_Init(cmdBytes))
            {
                printf("tssm: c3d: C3D_Init(%u) failed (linear memory?)\n", (unsigned)cmdBytes);
                fflush(stdout);
                return 0;
            }

            // The top screen as one 240 x 400 target (the LCD is mounted
            // sideways): what every Raster::CAMERA draws into.
            s.top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
            if (s.top == nil)
            {
                printf("tssm: c3d: the top screen's render target could not be made (VRAM)\n");
                fflush(stdout);
                C3D_Fini();
                return 0;
            }

            // SDL's video set the screens up (gfxInit, RGBA8 on the 3DS);
            // the transfer writes whatever format the top one has.
            GSPGPU_FramebufferFormat screenFormat = gfxGetScreenFormat(GFX_TOP);
            uint32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                           GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                           GX_TRANSFER_OUT_FORMAT(transferFormatFor(screenFormat)) |
                           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
            C3D_RenderTargetSetOutput(s.top, GFX_TOP, GFX_LEFT, flags);
            s.transferFlags = flags;
            s.cmdBytes = cmdBytes;

            s.dvlb = DVLB_ParseFile((u32*)kC3dShbin, sizeof(kC3dShbin));
            if (s.dvlb == nil || s.dvlb->numDVLE < NUM_PROGRAMS)
            {
                printf("tssm: c3d: the shader binary has %d programs, %d wanted\n",
                       s.dvlb != nil ? (int)s.dvlb->numDVLE : 0, (int)NUM_PROGRAMS);
                fflush(stdout);
                C3D_RenderTargetDelete(s.top);
                C3D_Fini();
                return 0;
            }
            for (int32 i = 0; i < NUM_PROGRAMS; i++)
            {
                shaderProgramInit(&s.programs[i]);
                shaderProgramSetVsh(&s.programs[i], &s.dvlb->DVLE[i]);
            }
            s.program = -1;
            s.hasDummy = C3D_TexInit(&s.dummy, 8, 8, GPU_RGBA4);
            if (s.hasDummy)
            {
                memset(s.dummy.data, 0, s.dummy.size);
                GSPGPU_FlushDataCache(s.dummy.data, s.dummy.size);
            }

            buildFormats();
            s.format = -1;
            s.buffer = nil;

            s.ring = (uint8*)linearAlloc(kRingBytes);
            s.staging = (uint8*)linearAlloc(kStagingBytes);
            s.stagingBusy = 1;
            s.deferred = (Deferred*)calloc(kFirstDeferred, sizeof(Deferred));
            s.deferredCap = s.deferred != nil ? kFirstDeferred : 0;
            if (s.ring == nil || s.staging == nil || s.deferred == nil)
            {
                printf("tssm: c3d: no room for the frame ring and the staging buffer\n");
                fflush(stdout);
                return 0;
            }
            modesInit(params != nil ? params->effects : nil);
            // Only with fog on at the open: the ramp's texture unit 2 and
            // combiner stage 4 are not yet shown to work on a console.
            s.hasRamp = modes.fog ? makeRamp() : false;

            // The PICA's depth: clip z/w from -1 (near) to 0 (far), stored as
            // 1 (near) to 0 (far); the test is GEQUAL and a clear is 0.
            C3D_DepthMap(true, -1.0f, 0.0f);
            C3D_CullFace(GPU_CULL_NONE);
            C3D_FrameRate((float)fps);

            for (int i = 0; i < 6; i++)
            {
                C3D_TexEnv* env = C3D_GetTexEnv(i);
                C3D_TexEnvInit(env);
            }

            for (int i = 0; i < NUM_REGS; i++)
            {
                s.shadowValid[i] = false;
            }

            s.timing = timingOn() != 0;
            {
                const char* st = getenv("TSSM_C3D_SPLITTEST");
                s.splitTest = st != nil && st[0] == '1';
                if (s.splitTest)
                {
                    printf("tssm: c3d: TSSM_C3D_SPLITTEST=1: a hard split after the first draw "
                           "into a camera texture in each frame\n");
                }
            }
            if (s.timing)
            {
                const u64 t0 = svcGetSystemTick();
                for (int i = 0; i < 1000; i++)
                {
                    (void)svcGetSystemTick();
                }
                s.tickNs = (uint32)((double)(svcGetSystemTick() - t0) * 1000000.0 /
                                    (double)SYSCLOCK_ARM11);
                printf("tssm: c3d: frame timing on (a tick read costs %u ns)\n",
                       (unsigned)s.tickNs);
            }

            s.open = true;
            printf("tssm: c3d: citro3d up: top screen 400x240 (%s), command buffer %u KB, %d fps, "
                   "VRAM free %u KB, linear free %u KB\n",
                   screenFormat == GSP_RGBA8_OES ? "RGBA8" :
                   screenFormat == GSP_BGR8_OES  ? "BGR8" :
                                                   "16-bit",
                   (unsigned)(cmdBytes >> 10), (int)fps, (unsigned)(vramSpaceFree() >> 10),
                   (unsigned)(linearSpaceFree() >> 10));
            fflush(stdout);
            if (modes.copy && (modes.glow || modes.warp))
            {
                // Below every level's textures.
                ensureSharedCopy();
            }
            rttSelfTest();
            return 1;
        }

        void submitClose(void)
        {
            if (!s.open)
            {
                return;
            }
            if (s.inFrame)
            {
                quitStep("a frame was open: it is ended and shown");
                frameEnd();
            }
            // Wait for the GPU, then free what it may have been reading.
            quitStep("waiting for the GPU and the GX queue");
            gpuIdle();
            {
                char line[192];
                snprintf(line, sizeof(line),
                         "freeing %d camera textures and copy blocks, the deferred blocks and "
                         "render targets, the shaders, the unit-1 and fog textures, the ring and "
                         "the staging buffer",
                         (int)s.numLive);
                quitStep(line);
            }
            for (int32 i = 0; i < s.numLive; i++)
            {
                C3dRaster* r = s.live[i];
                if (r->target != nil)
                {
                    C3D_RenderTargetDelete(r->target);
                    r->target = nil;
                }
                C3D_TexDelete(&r->tex);
                r->hasTex = 0;
                r->rendered = 0;
                r->screenCopy = COPY_NONE;
            }
            free(s.live);
            s.live = nil;
            s.numLive = 0;
            s.liveCap = 0;
            s.edgePending = nil;
            freeDeferred();
            deleteDeadTargets();
            free(s.deadTargets);
            s.deadTargets = nil;
            s.deadCap = 0;
            for (int32 i = 0; i < NUM_PROGRAMS; i++)
            {
                shaderProgramFree(&s.programs[i]);
            }
            DVLB_Free(s.dvlb);
            s.dvlb = nil;
            if (s.hasDummy)
            {
                C3D_TexDelete(&s.dummy);
                s.hasDummy = false;
            }
            if (s.hasRamp)
            {
                C3D_TexDelete(&s.ramp);
                s.hasRamp = false;
            }
            s.rampBound = false;
            if (s.hasCopy)
            {
                if (s.copyTarget != nil)
                {
                    C3D_RenderTargetDelete(s.copyTarget);
                    s.copyTarget = nil;
                }
                C3D_TexDelete(&s.copy);
                s.hasCopy = false;
            }
            linearFree(s.ring);
            linearFree(s.staging);
            free(s.deferred);
            s.ring = s.staging = nil;
            s.deferred = nil;
            s.deferredCap = 0;
            if (s.bottom != nil)
            {
                quitStep("freeing the overlay's bottom-screen target and textures");
                overlayClose();
                C3D_RenderTargetDelete(s.bottom);
                s.bottom = nil;
            }
            C3D_RenderTargetDelete(s.top);
            s.top = nil;
            quitStep("C3D_Fini");
            C3D_Fini();
            s.open = false;
        }

        bool32 submitIsOpen(void)
        {
            return s.open;
        }

        void setFrameRate(int fps)
        {
            if (s.open)
            {
                C3D_FrameRate((float)vblankRate(fps));
            }
        }

        bool32 inFrame(void)
        {
            return s.inFrame;
        }

        void frameEnsure(void)
        {
            if (s.inFrame || !s.open)
            {
                return;
            }
            // C3D_FrameBegin(C3D_FRAME_SYNCDRAW) in its two waits, timed apart.
            const u64 t0 = s.timing ? svcGetSystemTick() : 0;
            C3D_FrameSync();
            const u64 t1 = s.timing ? svcGetSystemTick() : 0;
            deleteDeadTargets();
            C3D_FrameBegin(0);
            if (s.timing)
            {
                const u64 t2 = svcGetSystemTick();
                s.cur.t[TIME_SYNC] += t1 - t0;
                s.cur.t[TIME_GPU] += t2 - t1;
                s.cur.t[TIME_DRAWN] +=
                    (u64)((double)C3D_GetDrawingTime() * (double)SYSCLOCK_ARM11 / 1000.0);
                s.waitTotal += t2 - t0;
            }
            s.inFrame = true;
            s.bound = nil;
            s.screenDrawn = false;
            s.splitDone = false;
            s.queueEntries = 0;
            s.drawsThisFrame = 0;
            s.ringUsed = 0;
            s.pin = nil;
            s.pinBytes = 0;
            s.viewport[0] = 0;
            s.viewport[1] = 0;
            s.viewport[2] = 240;
            s.viewport[3] = 400;
            memset(s.count, 0, sizeof(s.count));
            s.skinTicks = 0;
            freeDeferred();
            deviceResetStats();
            if (s.loadTextures >= 16)
            {
                // A level was loaded since the last frame: what it left
                // (the margins it keeps).
                printf("tssm: c3d: memory after a load (%u textures made): %u textures, VRAM %u "
                       "KB, linear %u KB; %u camera textures, %u KB of VRAM; VRAM free %u KB, "
                       "linear free %u KB\n",
                       (unsigned)s.loadTextures, (unsigned)s.textures, (unsigned)(s.texVram >> 10),
                       (unsigned)(s.texLinear >> 10), (unsigned)s.targets,
                       (unsigned)(s.targetVram >> 10), (unsigned)(vramSpaceFree() >> 10),
                       (unsigned)(linearSpaceFree() >> 10));
                fflush(stdout);
            }
            s.loadTextures = 0;
            drawPendingEdges();
        }

        void frameEnd(void)
        {
            frameEndWith(0);
        }

        uint32 ringUsed(void)
        {
            return s.ringUsed;
        }

        void ringFlush(void)
        {
            if (s.ringUsed > 0)
            {
                GSPGPU_FlushDataCache(s.ring, s.ringUsed);
            }
        }

        void frameEndWith(uint8 flags)
        {
            if (!s.inFrame)
            {
                return;
            }
            // Live use of the command buffer, before FrameEnd hands it over.
            const uint32 used = cmdUsedBytes();
            if (used > s.cmdPeak)
            {
                s.cmdPeak = used;
            }
            const u64 t0 = s.timing ? svcGetSystemTick() : 0;
            C3D_FrameEnd(flags);
            if (s.timing)
            {
                const u64 now = svcGetSystemTick();
                s.cur.t[TIME_END] += now - t0;
                if (s.lastPresent != 0)
                {
                    s.cur.wall = now - s.lastPresent;
                    s.sum.wall += s.cur.wall;
                    for (int i = 0; i < NUM_TIMES; i++)
                    {
                        s.sum.t[i] += s.cur.t[i];
                    }
                    for (int i = 0; i < NUM_PROGRAMS; i++)
                    {
                        s.sum.verts[i] += s.cur.verts[i];
                    }
                    s.sum.reads += s.cur.reads + 5;
                    s.sumFrames++;
                    if (s.cur.wall > s.wallMax)
                    {
                        s.wallMax = s.cur.wall;
                    }
                    abNoteFrame(&s.cur);
                }
                s.lastPresent = now;
                memset(&s.cur, 0, sizeof(s.cur));
            }
            s.queueEntries += 2;
            if (s.queueEntries > s.queuePeak)
            {
                s.queuePeak = s.queueEntries;
            }
            for (int i = 0; i < NUM_COUNTS; i++)
            {
                if (s.count[i] > s.countPeak[i])
                {
                    s.countPeak[i] = s.count[i];
                }
            }
            if (s.skinTicks > s.skinTicksPeak)
            {
                s.skinTicksPeak = s.skinTicks;
            }
            s.inFrame = false;
            s.bound = nil;
            s.frames++;
        }

        void bindScreen(void)
        {
            frameEnsure();
            if (s.bound != s.top)
            {
                // C3D_FrameDrawOn sets the whole target as the viewport.
                C3D_FrameDrawOn(s.top);
                s.bound = s.top;
                s.viewport[0] = 0;
                s.viewport[1] = 0;
                s.viewport[2] = 240;
                s.viewport[3] = 400;
                s.count[COUNT_TARGET_BINDS]++;
            }
        }

        void bindTarget(C3D_RenderTarget* target, int32 w, int32 h)
        {
            frameEnsure();
            if (s.bound != target)
            {
                // citro3d flushes the target before (when drawn on) and
                // invalidates the new one's cache.
                C3D_FrameDrawOn(target);
                s.bound = target;
                s.count[COUNT_TARGET_BINDS]++;
            }
            s.viewport[0] = 0;
            s.viewport[1] = 0;
            s.viewport[2] = w;
            s.viewport[3] = h;
            C3D_SetViewport(0, 0, (u32)w, (u32)h);
        }

        bool32 targetIsScreen(void)
        {
            return s.bound == nil || s.bound == s.top;
        }

        C3D_RenderTarget* boundTarget(void)
        {
            return s.bound;
        }

        void setScreenViewport(int32 x, int32 y, int32 w, int32 h)
        {
            bindScreen();
            // Landscape pixels to the target's (portrait) ones: the GPU's x
            // runs up the screen and its y from the right edge to the left.
            int32 vx = SCREEN_H - (y + h);
            int32 vy = SCREEN_W - (x + w);
            if (vx < 0 || vy < 0 || w <= 0 || h <= 0)
            {
                vx = vy = 0;
                h = 240;
                w = 400;
            }
            // Kept for a hard split, whose C3D_FrameDrawOn sets the whole target.
            s.viewport[0] = vx;
            s.viewport[1] = vy;
            s.viewport[2] = h;
            s.viewport[3] = w;
            C3D_SetViewport((u32)vx, (u32)vy, (u32)h, (u32)w);
        }

        void clearScreen(uint32 rgba, bool32 color, bool32 depth)
        {
            if (!s.open || (!color && !depth))
            {
                return;
            }
            frameEnsure();

            if (!s.screenDrawn)
            {
                // Nothing recorded into the top screen yet this frame, so a
                // fill (queued before this frame's command list) is in order.
                int bits = (color ? C3D_CLEAR_COLOR : 0) | (depth ? C3D_CLEAR_DEPTH : 0);
                C3D_RenderTargetClear(s.top, (C3D_ClearBits)bits, rgba, 0);
                s.queueEntries++;
                bindScreen();
                return;
            }

            // Later in the frame: a fill would run before the draws recorded
            // so far, so it is a quad in the command list instead.
            bindScreen();
            clearQuad(rgba, color, depth);
        }

        void clearQuad(uint32 rgba, bool32 color, bool32 depth)
        {
            Im2DVertex* v = (Im2DVertex*)ringAlloc(4 * sizeof(Im2DVertex));
            if (v == nil)
            {
                return;
            }
            if (s.bound == nil)
            {
                bindScreen();
            }
            // Clip x and y straight from (0..2) - 1: the whole viewport, the
            // screen's turn or not.
            const float xs[4] = { 0.0f, 0.0f, 2.0f, 2.0f };
            const float ys[4] = { 0.0f, 2.0f, 0.0f, 2.0f };
            for (int i = 0; i < 4; i++)
            {
                v[i].x = xs[i];
                v[i].y = ys[i];
                v[i].z = 1.0f; // far: depth 0, what a clear writes
                v[i].w = 1.0f;
                v[i].r = (uint8)(rgba >> 24);
                v[i].g = (uint8)(rgba >> 16);
                v[i].b = (uint8)(rgba >> 8);
                v[i].a = (uint8)rgba;
                v[i].u = 0.0f;
                v[i].v = 0.0f;
            }
            useProgram(PROG_IM2D);
            useFormat(FORMAT_IM2D, v, sizeof(Im2DVertex));
            setUniform4(REG_SCRMAT, 1.0f, 0.0f, -1.0f, 0.0f);
            setUniform4(REG_SCRMAT + 1, 0.0f, 1.0f, -1.0f, 0.0f);
            setUniform4(REG_UVMAT, 1.0f, 0.0f, 0.0f, 0.0f);
            setUniform4(REG_UVMAT + 1, 0.0f, 1.0f, 0.0f, 0.0f);

            deviceTevPlain();
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            C3D_DepthMap(true, -1.0f, 0.0f); // the quad's z is the 2D's: depth 0
            C3D_AlphaTest(false, GPU_ALWAYS, 0);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
            C3D_CullFace(GPU_CULL_NONE);
            C3D_DepthTest(true, GPU_ALWAYS,
                          (GPU_WRITEMASK)((color ? GPU_WRITE_COLOR : 0) |
                                          (depth ? GPU_WRITE_DEPTH : 0)));
            C3D_DrawArrays(GPU_TRIANGLE_STRIP, 0, 4);
            if (s.bound == s.top)
            {
                s.screenDrawn = true;
            }
            // The device's state is what the next draw has to set again.
            deviceStateDirty();
        }

        void* ringAlloc(uint32 bytes)
        {
            frameEnsure();
            bytes = (bytes + 15) & ~15u;
            if (s.ringUsed + bytes <= kRingBytes)
            {
                void* p = s.ring + s.ringUsed;
                s.ringUsed += bytes;
                if (s.ringUsed > s.ringPeak)
                {
                    s.ringPeak = s.ringUsed;
                }
                return p;
            }
            // Full: the frame so far is drawn and the ring starts again, with
            // the pinned block moved to its start.
            const bool keep = s.pin >= s.ring && s.pin < s.ring + kRingBytes;
            const uint32 kept = keep ? s.pinBytes : 0;
            if (kept + bytes <= kRingBytes)
            {
                hardSplit("frame ring", true);
                if (keep)
                {
                    memmove(s.ring, s.pin, kept);
                    s.pin = s.ring;
                }
                void* p = s.ring + kept;
                s.ringUsed = kept + bytes;
                if (s.ringUsed > s.ringPeak)
                {
                    s.ringPeak = s.ringUsed;
                }
                return p;
            }
            logOnce("ringfull",
                    "a draw of %u KB does not fit the frame ring (%u KB); spilling "
                    "into linear memory",
                    (unsigned)(bytes >> 10), (unsigned)(kRingBytes >> 10));
            void* p = linearAlloc(bytes);
            if (p != nil)
            {
                newDeferred()->linear = p;
            }
            return p;
        }

        void ringPin(void* p, uint32 bytes)
        {
            s.pin = (uint8*)p;
            s.pinBytes = (bytes + 15) & ~15u;
        }

        void* ringPinned(void)
        {
            return s.pin;
        }

        void deferFree(void* linearPtr)
        {
            if (linearPtr == nil)
            {
                return;
            }
            if (!s.open)
            {
                linearFree(linearPtr);
                return;
            }
            newDeferred()->linear = linearPtr;
        }

        void deferTexDelete(C3D_Tex* tex)
        {
            if (!s.open)
            {
                C3D_TexDelete(tex);
                return;
            }
            Deferred* d = newDeferred();
            d->tex = *tex;
            d->hasTex = true;
        }

        void deferTargetDelete(C3D_RenderTarget* target)
        {
            if (target == nil)
            {
                return;
            }
            if (s.bound == target)
            {
                s.bound = nil; // the next draw binds a target again
            }
            if (s.edgePending == target)
            {
                s.edgePending = nil;
            }
            if (!s.open)
            {
                C3D_RenderTargetDelete(target);
                return;
            }
            if (s.numDead >= s.deadCap)
            {
                const int32 cap = s.deadCap > 0 ? s.deadCap * 2 : 16;
                C3D_RenderTarget** more = (C3D_RenderTarget**)realloc(
                    s.deadTargets, (size_t)cap * sizeof(C3D_RenderTarget*));
                if (more == nil)
                {
                    // Leaked rather than deleted while the GPU may draw into it.
                    logOnce("deadfull", "a render target could not be queued for deleting");
                    return;
                }
                s.deadTargets = more;
                s.deadCap = cap;
            }
            s.deadTargets[s.numDead++] = target;
        }

        void useProgram(int32 prog)
        {
            if (prog != PROG_IM2D)
            {
                hudLogProgram(prog);
            }
            if (s.program != prog)
            {
                C3D_BindProgram(&s.programs[prog]);
                s.program = prog;
                // Binding a program marks the attribute layout for sending
                // again in citro3d; the format's fixed values with it.
                s.format = -1;
            }
        }

        void useFormat(int32 format, const void* vertices, uint32 stride)
        {
            if (s.format != format)
            {
                C3D_SetAttrInfo(&s.attr[format]);
                // A missing normal is no light, a missing colour black, a
                // missing uv zero (the GL3 device's choices).
                if (s.fixedIndex[format][1] >= 0)
                    C3D_FixedAttribSet(s.fixedIndex[format][1], 0.0f, 0.0f, 0.0f, 0.0f);
                if (s.fixedIndex[format][2] >= 0)
                    C3D_FixedAttribSet(s.fixedIndex[format][2], 0.0f, 0.0f, 0.0f, 255.0f);
                if (s.fixedIndex[format][3] >= 0)
                    C3D_FixedAttribSet(s.fixedIndex[format][3], 0.0f, 0.0f, 0.0f, 0.0f);
                s.format = format;
                s.buffer = nil;
            }
            if (s.buffer != vertices || s.bufferStride != stride)
            {
                int count;
                uint64 perm;
                bufferLayout(format, &count, &perm);
                C3D_BufInfo* bi = C3D_GetBufInfo();
                BufInfo_Init(bi);
                BufInfo_Add(bi, vertices, stride, count, perm);
                s.buffer = vertices;
                s.bufferStride = stride;
            }
        }

        C3D_FVec* uniformPtr(int32 reg, int32 count)
        {
            for (int32 i = 0; i < count; i++)
            {
                s.shadowValid[reg + i] = false;
            }
            return C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, reg, count);
        }

        bool32 setUniform4(int32 reg, float x, float y, float z, float w)
        {
            C3D_FVec* sh = &s.shadow[reg];
            if (s.shadowValid[reg] && sh->x == x && sh->y == y && sh->z == z && sh->w == w)
            {
                return 0;
            }
            sh->x = x;
            sh->y = y;
            sh->z = z;
            sh->w = w;
            s.shadowValid[reg] = true;
            C3D_FVec* p = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, reg, 1);
            p->x = x;
            p->y = y;
            p->z = z;
            p->w = w;
            return 1;
        }

        int32 setUniformRows(int32 reg, const float* rows, int32 count)
        {
            int32 sent = 0;
            for (int32 i = 0; i < count; i++)
            {
                sent += setUniform4(reg + i, rows[4 * i], rows[4 * i + 1], rows[4 * i + 2],
                                    rows[4 * i + 3]);
            }
            return sent;
        }

        C3D_Tex* dummyTexture(void)
        {
            return s.hasDummy ? &s.dummy : nil;
        }

        // The hard split: the frame so far goes to the GPU and is
        // waited for, with nothing shown, and recording goes on in a new
        // frame; the GPU's registers keep their state. Only for a command
        // buffer or frame ring about to overflow: it stalls the CPU.
        void hardSplit(const char* why, bool ringToo)
        {
            if (!s.inFrame)
            {
                return;
            }
            s.hardSplits++;
            if (s.hardSplits <= 8 || (s.hardSplits & 255) == 0)
            {
                printf("tssm: c3d: hard split %u (%s): %u draws in this frame, command buffer "
                       "%u KB, ring %u KB\n",
                       (unsigned)s.hardSplits, why, (unsigned)s.drawsThisFrame,
                       (unsigned)(cmdUsedBytes() >> 10), (unsigned)(s.ringUsed >> 10));
                fflush(stdout);
            }
            C3D_RenderTarget* bound = s.bound;
            detachOutputs();
            C3D_FrameEnd(0);
            const u64 t0 = s.timing ? svcGetSystemTick() : 0;
            C3D_FrameBegin(0); // waits for the GPU
            if (s.timing)
            {
                const u64 t = svcGetSystemTick() - t0;
                s.cur.t[TIME_GPU] += t;
                s.waitTotal += t;
            }
            attachOutputs();
            s.queueEntries = 0;
            if (ringToo)
            {
                // Not for a split before a draw: its vertices may be in the
                // ring already.
                s.ringUsed = 0;
            }
            freeDeferredDrawn();
            s.bound = nil;
            if (bound != nil)
            {
                C3D_FrameDrawOn(bound);
                s.bound = bound;
                C3D_SetViewport((u32)s.viewport[0], (u32)s.viewport[1], (u32)s.viewport[2],
                                (u32)s.viewport[3]);
            }
        }

        namespace
        {
            // Before a draw: room for it in the command buffer.
            void drawGuard(void)
            {
                u32* buf = nil;
                u32 size = 0;
                u32 offset = 0;
                GPUCMD_GetBuffer(&buf, &size, &offset);
                if (buf != nil && size > 0 && offset > size - size / 10)
                {
                    hardSplit("command buffer", false);
                }
                s.drawsThisFrame++;
            }
        } // namespace

        namespace
        {
            // Before a draw: the top screen unless a camera texture is bound.
            void noteTarget(void)
            {
                if (s.bound == nil)
                {
                    bindScreen();
                }
                if (s.bound == s.top)
                {
                    s.screenDrawn = true;
                }
                else if (s.bound != s.bottom)
                {
                    s.count[COUNT_RTT_DRAWS]++;
                }
            }

            void splitTest(void)
            {
                if (s.splitTest && !s.splitDone && s.bound != s.top)
                {
                    s.splitDone = true;
                    hardSplit("TSSM_C3D_SPLITTEST, inside a camera texture", false);
                }
            }
        } // namespace

        void drawArrays(uint32 prim, int32 first, int32 count)
        {
            drawGuard();
            noteTarget();
            if (s.program >= 0)
            {
                s.cur.verts[s.program] += (uint32)count;
            }
            C3D_DrawArrays((GPU_Primitive_t)prim, first, count);
            s.deferredDrawn = s.numDeferred;
            splitTest();
        }

        void drawElements(uint32 prim, int32 count, const uint16* indices)
        {
            drawGuard();
            noteTarget();
            if (s.program >= 0)
            {
                s.cur.verts[s.program] += (uint32)count;
            }
            C3D_DrawElements((GPU_Primitive_t)prim, count, C3D_UNSIGNED_SHORT, indices);
            s.deferredDrawn = s.numDeferred;
            splitTest();
        }

        bool32 texCreate(C3dRaster* natras, int32 w, int32 h, int32 maxLevel, uint32 gpuFormat,
                         const void* levels, uint32 bytes)
        {
            C3D_TexInitParams p;
            memset(&p, 0, sizeof(p));
            p.width = (u16)w;
            p.height = (u16)h;
            p.maxLevel = (u8)maxLevel;
            p.format = (GPU_TEXCOLOR)gpuFormat;
            p.type = GPU_TEX_2D;

            // Inside a frame a GX copy would only be queued (two of the
            // frame's 32 queue entries, and the staging buffer kept until the
            // frame has run), so such a texture is written into linear memory
            // by the CPU instead; FrameEnd's flush covers it.
            bool vram = !s.inFrame;
            if (s.inFrame)
            {
                s.inFrameUploads++;
                logOnce("inframe",
                        "a texture was made inside a frame (%dx%d); it is kept in "
                        "linear memory",
                        (int)w, (int)h);
            }

            bool made = initTex(&natras->tex, p, vram);
            if (!made && reclaim())
            {
                // What was let go since the last frame (a level's unload)
                // still held the memory.
                made = initTex(&natras->tex, p, vram);
            }
            if (!made && vram)
            {
                logOnce("vramfull", "VRAM is full (%u KB free); textures go to linear memory",
                        (unsigned)(vramSpaceFree() >> 10));
                vram = false;
                made = initTex(&natras->tex, p, false);
            }
            if (!made)
            {
                return 0;
            }

            u32 size = 0;
            C3D_TexGetImagePtr(&natras->tex, natras->tex.data, -1, &size);
            if (size > bytes)
            {
                size = bytes;
            }

            if (vram)
            {
                bool own = false;
                uint8* staging = stagingFor(size, &own);
                if (staging == nil)
                {
                    C3D_TexDelete(&natras->tex);
                    return 0;
                }
                memcpy(staging, levels, size);
                copyToVram(&natras->tex, staging, size);
                if (own)
                {
                    deferFree(staging);
                }
                s.texVram += size;
            }
            else
            {
                memcpy(natras->tex.data, levels, size);
                C3D_TexFlush(&natras->tex);
                s.texLinear += size;
            }

            natras->hasTex = 1;
            natras->onVram = vram ? 1 : 0;
            natras->generation++;
            s.textures++;
            if (!s.inFrame)
            {
                s.loadTextures++;
            }
            return 1;
        }

        bool32 targetCreate(C3dRaster* natras, int32 w, int32 h, bool32 depth)
        {
            C3D_TexInitParams p;
            memset(&p, 0, sizeof(p));
            p.width = (u16)w;
            p.height = (u16)h;
            p.format = GPU_RGBA8;
            p.type = GPU_TEX_2D;
            p.onVram = true;
            bool made = C3D_TexInitWithParams(&natras->tex, nil, p);
            if (!made && reclaim())
            {
                made = C3D_TexInitWithParams(&natras->tex, nil, p);
            }
            if (!made)
            {
                return 0;
            }
            // citro3d draws only into VRAM.
            natras->target = C3D_RenderTargetCreateFromTex(&natras->tex, GPU_TEXFACE_2D, 0,
                                                           depth ? C3D_DEPTHTYPE(GPU_RB_DEPTH16) :
                                                                   C3D_DEPTHTYPE(-1));
            if (natras->target == nil)
            {
                C3D_TexDelete(&natras->tex);
                return 0;
            }
            natras->hasTex = 1;
            natras->onVram = 1;
            natras->generation++;
            s.targets++;
            s.targetVram += (uint32)(w * h * 4 + (depth ? w * h * 2 : 0));
            liveAdd(natras);
            return 1;
        }

        void texDestroy(C3dRaster* natras)
        {
            if (!natras->hasTex)
            {
                return;
            }
            liveRemove(natras);
            if (natras->screenCopy != COPY_NONE)
            {
                deviceForgetTexture(&natras->tex);
                if (natras->screenCopy == COPY_OWN)
                {
                    deferTargetDelete(natras->target);
                    deferTexDelete(&natras->tex);
                    s.targets--;
                    s.targetVram -= kCopyBytes < s.targetVram ? kCopyBytes : s.targetVram;
                }
                else if (s.copyUsers > 0)
                {
                    s.copyUsers--; // the block itself stays until the close
                }
                natras->target = nil;
                natras->screenCopy = COPY_NONE;
                natras->hasTex = 0;
                natras->rendered = 0;
                return;
            }
            u32 size = 0;
            C3D_TexGetImagePtr(&natras->tex, natras->tex.data, -1, &size);
            const bool camera = natras->target != nil;
            if (camera)
            {
                const uint32 depth = natras->target->frameBuf.depthBuf != nil ? size / 2 : 0;
                s.targetVram -= size + depth < s.targetVram ? size + depth : s.targetVram;
                s.targets--;
                deferTargetDelete(natras->target);
                natras->target = nil;
                natras->rendered = 0;
            }
            else if (natras->onVram)
            {
                s.texVram -= size < s.texVram ? size : s.texVram;
            }
            else
            {
                s.texLinear -= size < s.texLinear ? size : s.texLinear;
            }
            if (!camera)
            {
                s.textures--; // targets are counted apart
            }
            // Neither citro3d nor the device's state filter may keep it bound.
            deviceForgetTexture(&natras->tex);
            deferTexDelete(&natras->tex);
            natras->hasTex = 0;
        }

        void* texWriteBegin(C3dRaster* natras, uint32 bytes)
        {
            if (!s.open || s.inFrame || !natras->hasTex)
            {
                return nil;
            }
            u32 size = 0;
            C3D_TexGetImagePtr(&natras->tex, natras->tex.data, -1, &size);
            if (bytes != size)
            {
                return nil;
            }
            if (natras->onVram)
            {
                // The GX copy waits for the frame in flight, which may still
                // be sampling the texture, before it writes.
                if (bytes > kStagingHalf)
                {
                    return nil;
                }
                bool own = false;
                return stagingFor(bytes, &own);
            }
            // In linear memory the CPU writes the texture itself: not before
            // the GPU is done with the frame that samples it.
            gpuIdle();
            return natras->tex.data;
        }

        void texWriteEnd(C3dRaster* natras, void* at, uint32 bytes)
        {
            if (natras->onVram)
            {
                copyToVram(&natras->tex, (uint8*)at, bytes);
            }
            else
            {
                GSPGPU_FlushDataCache(at, bytes);
            }
            // The texture cache must not keep the old texels: a new
            // generation makes the next draw bind it again.
            natras->generation++;
        }

        C3D_Tex* fogRamp(void)
        {
            return s.hasRamp ? &s.ramp : nil;
        }

        void bindFogRamp(const char* why)
        {
            if (!s.hasRamp || s.rampBound)
            {
                return;
            }
            C3D_TexBind(2, &s.ramp);
            s.rampBound = true;
            printf("tssm: c3d: fog: texture unit 2 holds the ramp from here (%s); every later draw "
                   "samples it\n",
                   why);
            fflush(stdout);
        }

        void dropFogRamp(const char* why)
        {
            C3D_Tex* d = dummyTexture();
            if (!s.rampBound || d == nil)
            {
                return;
            }
            C3D_TexBind(2, d);
            s.rampBound = false;
            printf("tssm: c3d: fog: texture unit 2 back on the 8x8 dummy (%s); the next fogged "
                   "draw binds the ramp again\n",
                   why);
            fflush(stdout);
        }

        namespace
        {
            // A block's picture is 240 x 400 of its 256 x 512, so clamping at
            // the texture's edge would reach the rest, not the picture's edge
            // (the warp pushes its coordinates past the picture). The rest is
            // drawn as the picture's last column and row, so the texture's
            // clamp gives what a copy of the picture's own size gives on the
            // PC. It reads only the picture and writes only the rest, so the
            // block can be drawn into while sampled; nearest, at texel
            // centres. After the copy in the command stream (the copy split
            // the frame).
            void extendCopyEdges(C3D_Tex* tex, C3D_RenderTarget* target)
            {
                // It samples the block while drawing into other tiles of it,
                // which only a console can prove: copyedge=off leaves it out.
                if (!modes.copyEdge)
                {
                    logOnce("copyedgeoff", "copyedge=off: a screen copy's block is not drawn "
                                           "past the picture (the warp may show its border)");
                    return;
                }
                if (target == nil)
                {
                    logOnce("copyedge", "a screen copy in linear memory: its edges are not "
                                        "extended (the warp may show its border)");
                    return;
                }
                if (!s.inFrame)
                {
                    // Drawn when the next frame opens, not in a frame of its
                    // own (which would wait for a vblank).
                    s.edgePending = target;
                    s.edgePendingTex = *tex;
                    return;
                }
                Im2DVertex* v = (Im2DVertex*)ringAlloc(18 * sizeof(Im2DVertex));
                if (v == nil)
                {
                    return;
                }
                C3D_RenderTarget* was = s.bound;
                int32 vp[4];
                memcpy(vp, s.viewport, sizeof(vp));
                bindTarget(target, (int32)kCopyW, (int32)kCopyH);

                // x, y in the block's pixels (rows bottom up); s, t = the texel
                // read. The last column (239) out to 256, the last row (399)
                // up to 512, and the corner.
                const float cx = 239.5f / (float)kCopyW;
                const float cy = 399.5f / (float)kCopyH;
                const float quads[3][8] = {
                    { 240.0f, 0.0f, 256.0f, 400.0f, cx, 0.0f, cx, 400.0f / (float)kCopyH },
                    { 0.0f, 400.0f, 240.0f, 512.0f, 0.0f, cy, 240.0f / (float)kCopyW, cy },
                    { 240.0f, 400.0f, 256.0f, 512.0f, cx, cy, cx, cy },
                };
                static const int kCorner[6][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 },
                                                   { 1, 0 }, { 0, 1 }, { 1, 1 } };
                for (int q = 0; q < 3; q++)
                {
                    for (int i = 0; i < 6; i++)
                    {
                        Im2DVertex* p = &v[q * 6 + i];
                        const int cxi = kCorner[i][0];
                        const int cyi = kCorner[i][1];
                        p->x = quads[q][cxi ? 2 : 0];
                        p->y = quads[q][cyi ? 3 : 1];
                        p->z = 0.0f;
                        p->w = 1.0f;
                        p->r = p->g = p->b = p->a = 255;
                        p->u = quads[q][cxi ? 6 : 4];
                        p->v = quads[q][cyi ? 7 : 5];
                    }
                }
                deviceTevPlain();
                s.edgeTex = *tex;
                C3D_TexSetFilter(&s.edgeTex, GPU_NEAREST, GPU_NEAREST);
                C3D_TexSetWrap(&s.edgeTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                C3D_TexBind(0, &s.edgeTex);
                useProgram(PROG_IM2D);
                useFormat(FORMAT_IM2D, v, sizeof(Im2DVertex));
                setUniform4(REG_SCRMAT, 2.0f / (float)kCopyW, 0.0f, -1.0f, 0.0f);
                setUniform4(REG_SCRMAT + 1, 0.0f, 2.0f / (float)kCopyH, -1.0f, 0.0f);
                setUniform4(REG_UVMAT, 1.0f, 0.0f, 0.0f, 0.0f);
                setUniform4(REG_UVMAT + 1, 0.0f, 1.0f, 0.0f, 0.0f);
                C3D_TexEnv* env = C3D_GetTexEnv(0);
                C3D_TexEnvInit(env);
                C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
                C3D_AlphaTest(false, GPU_ALWAYS, 0);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                C3D_CullFace(GPU_CULL_NONE);
                C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
                drawArrays(GPU_TRIANGLES, 0, 18);
                // Not left on a block's storage, which may go while citro3d
                // still holds it (a return from HOME sends it again).
                C3D_TexBind(0, dummyTexture());

                // Back to the target and viewport the caller had; with none,
                // the top screen, as the next draw would bind.
                if (was != nil && was != target)
                {
                    C3D_FrameDrawOn(was);
                    s.bound = was;
                    memcpy(s.viewport, vp, sizeof(vp));
                    C3D_SetViewport((u32)vp[0], (u32)vp[1], (u32)vp[2], (u32)vp[3]);
                    s.count[COUNT_TARGET_BINDS]++;
                }
                else if (was == nil)
                {
                    bindScreen();
                }
            }

            void drawPendingEdges(void)
            {
                if (s.edgePending == nil)
                {
                    return;
                }
                C3D_RenderTarget* target = s.edgePending;
                C3D_Tex tex = s.edgePendingTex;
                s.edgePending = nil;
                extendCopyEdges(&tex, target);
                deviceStateDirty();
            }
        } // namespace

        bool32 copyScreenInto(C3dRaster* natras, bool32 own)
        {
            if (!s.open)
            {
                return 0;
            }
            const uint8 kind = own ? (uint8)COPY_OWN : (uint8)COPY_SHARED;
            if (natras->screenCopy != kind)
            {
                texDestroy(natras); // a camera texture's target, or the other kind of block
                bool vram = false;
                if (own)
                {
                    if (!makeCopyTex(&natras->tex, &vram))
                    {
                        logOnce("copyown", "no memory for a screen copy of its own");
                        return 0;
                    }
                    natras->target = copyTargetFor(&natras->tex, vram);
                    liveAdd(natras);
                }
                else
                {
                    if (!ensureSharedCopy())
                    {
                        logOnce("copyshared", "no memory for the screen passes' copy");
                        return 0;
                    }
                    natras->tex = s.copy;
                    vram = s.copyOnVram;
                    s.copyUsers++;
                }
                natras->screenCopy = kind;
                natras->hasTex = 1;
                natras->onVram = vram ? 1 : 0;
                natras->alphaKind = ALPHAGRADED;
                natras->filter = 0;
                natras->addrU = 0;
                natras->addrV = 0;
                natras->usedW = 240;
                natras->usedH = 400;
                natras->uScale = 240.0f / (float)kCopyW;
                natras->vScale = 400.0f / (float)kCopyH;
                if (natras->screenCopy == COPY_SHARED)
                {
                    natras->target = nil; // the block's is s.copyTarget
                }
            }
            // Inside a frame citro3d splits the frame there (its command
            // list ends with a framebuffer flush) and queues the copy after
            // it; outside one its wait returns early on a console (the top of
            // the file), so the queue is waited for.
            const bool inside = s.inFrame;
            if (inside && s.queueEntries + 2 > 30)
            {
                // The GX queue holds 32 and the frame's end needs two more;
                // overflowing it stops the program.
                hardSplit("GX queue", false);
            }
            C3D_SyncTextureCopy((u32*)s.top->frameBuf.colorBuf, GX_BUFFER_DIM(480, 0),
                                (u32*)((uint8*)natras->tex.data + kCopySkip),
                                GX_BUFFER_DIM(480, 32), 240 * 400 * 4,
                                GX_TRANSFER_RAW_COPY(1) | kTransferGaps);
            if (inside)
            {
                s.queueEntries += 2;
                s.count[COUNT_COPIES]++;
            }
            else
            {
                gpuIdle();
            }
            extendCopyEdges(&natras->tex,
                            natras->screenCopy == COPY_OWN ? natras->target : s.copyTarget);
            natras->rendered = 1;
            natras->generation++;
            // The passes' rasters share the block: every unit binds again, so
            // the texture cache cannot keep the old picture.
            deviceStateDirty();
            return 1;
        }

        void screenOutput(bool32 on)
        {
            if (on)
            {
                C3D_RenderTargetSetOutput(s.top, GFX_TOP, GFX_LEFT, s.transferFlags);
            }
            else
            {
                C3D_RenderTargetDetachOutput(s.top);
            }
        }

        bool32 bottomCreate(bool32 depth)
        {
            if (!s.open || s.bottom != nil)
            {
                return s.bottom != nil;
            }
            // RGBA8, as the screen's own buffer (SDL's gfxInit): the transfer
            // copies without widening 16-bit colour, which only a console
            // could prove.
            s.bottom = C3D_RenderTargetCreate(
                240, 320, GPU_RB_RGBA8, depth ? C3D_DEPTHTYPE(GPU_RB_DEPTH16) : C3D_DEPTHTYPE(-1));
            if (s.bottom == nil)
            {
                return 0;
            }
            const GSPGPU_FramebufferFormat f = gfxGetScreenFormat(GFX_BOTTOM);
            s.bottomFlags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                            GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                            GX_TRANSFER_OUT_FORMAT(transferFormatFor(f)) |
                            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
            C3D_RenderTargetSetOutput(s.bottom, GFX_BOTTOM, GFX_LEFT, s.bottomFlags);
            return 1;
        }

        bool32 bottomExists(void)
        {
            return s.bottom != nil;
        }

        void bindBottom(void)
        {
            frameEnsure();
            if (s.bottom == nil)
            {
                return;
            }
            if (s.bound != s.bottom)
            {
                C3D_FrameDrawOn(s.bottom);
                s.bound = s.bottom;
                s.count[COUNT_TARGET_BINDS]++;
            }
            s.viewport[0] = 0;
            s.viewport[1] = 0;
            s.viewport[2] = 240;
            s.viewport[3] = 320;
            C3D_SetViewport(0, 0, 240, 320);
        }

        bool32 readBottomTarget(uint8* rgba)
        {
            if (!s.open || s.inFrame || s.bottom == nil || rgba == nil)
            {
                return 0;
            }
            const uint32 bytes = 240 * 320 * 4;
            uint8* buf = (uint8*)linearAlloc(bytes);
            if (buf == nil)
            {
                return 0;
            }
            // As readScreen.
            GSPGPU_InvalidateDataCache(buf, bytes);
            C3D_SyncDisplayTransfer((u32*)s.bottom->frameBuf.colorBuf, GX_BUFFER_DIM(240, 320),
                                    (u32*)buf, GX_BUFFER_DIM(240, 320),
                                    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                                        GX_TRANSFER_RAW_COPY(0) |
                                        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
            gpuIdle();
            GSPGPU_InvalidateDataCache(buf, bytes);
            for (int y = 0; y < 240; y++)
            {
                for (int x = 0; x < 320; x++)
                {
                    const uint8* p = buf + 4 * (x * 240 + (239 - y));
                    uint8* d = rgba + 4 * (y * 320 + x);
                    d[0] = p[3];
                    d[1] = p[2];
                    d[2] = p[1];
                    d[3] = 255;
                }
            }
            linearFree(buf);
            return 1;
        }

        bool32 readTexture(const C3D_Tex* tex, uint8* abgr)
        {
            if (!s.open || tex == nil || tex->fmt != GPU_RGBA8)
            {
                return 0;
            }
            const uint32 w = tex->width;
            const uint32 h = tex->height;
            const uint32 bytes = w * h * 4;
            uint8* buf = (uint8*)linearAlloc(bytes);
            if (buf == nil)
            {
                return 0;
            }
            GSPGPU_InvalidateDataCache(buf, bytes);
            // Inside a frame the draws into it have not run yet: the frame so
            // far goes to the GPU, unseen, and is waited for (the hard split's
            // first half), and recording goes on after.
            const bool wasInFrame = s.inFrame;
            C3D_RenderTarget* bound = s.bound;
            if (wasInFrame)
            {
                s.hardSplits++;
                s.count[COUNT_READBACKS]++;
                detachOutputs();
                C3D_FrameEnd(0);
                s.inFrame = false;
            }
            gpuIdle();
            // A raw copy of the tiles, untiled here. Not a display transfer:
            // its tiled-to-linear mode wants at least 64 x 16 pixels
            // (3dbrew, GPU/External Registers); the first console run of the
            // self-test locked up right at one of 16 x 16, and a camera
            // texture can be 8 x 8. The raw copy only wants 16 bytes or more,
            // as citro3d's own texture uploads use it.
            C3D_SyncTextureCopy((u32*)tex->data, 0, (u32*)buf, 0, bytes, GX_TRANSFER_RAW_COPY(1));
            gpuIdle();
            GSPGPU_InvalidateDataCache(buf, bytes);
            // What the display transfer gives: row y of the result is the
            // texture's row y (8 x 8 tiles in rows, Morton order inside).
            for (uint32 y = 0; y < h; y++)
            {
                for (uint32 x = 0; x < w; x++)
                {
                    const uint32 tx = x & 7;
                    const uint32 ty = y & 7;
                    const uint32 morton = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) |
                                          ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
                    const uint32 at = ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + morton;
                    memcpy(abgr + 4 * (y * w + x), buf + 4 * at, 4);
                }
            }
            linearFree(buf);
            if (wasInFrame)
            {
                C3D_FrameBegin(0);
                s.inFrame = true;
                attachOutputs();
                s.queueEntries = 0;
                freeDeferredDrawn();
                s.bound = nil;
                if (bound != nil)
                {
                    C3D_FrameDrawOn(bound);
                    s.bound = bound;
                    C3D_SetViewport((u32)s.viewport[0], (u32)s.viewport[1], (u32)s.viewport[2],
                                    (u32)s.viewport[3]);
                }
                // citro3d's state was kept, but the device's filters must not
                // trust it across the frames.
                deviceStateDirty();
            }
            return 1;
        }

        void gpuWait(void)
        {
            gpuIdle();
        }

        int readScreen(unsigned char* rgba, int width, int height)
        {
            if (!s.open || s.inFrame || rgba == nil || width != SCREEN_W || height != SCREEN_H)
            {
                return 0;
            }
            const uint32 bytes = 240 * 400 * 4;
            uint8* buf = (uint8*)linearAlloc(bytes);
            if (buf == nil)
            {
                return 0;
            }
            // No line of it may be in the CPU's cache, dirty from an earlier
            // owner (written back over the transfer's) or clean (read instead
            // of it).
            GSPGPU_InvalidateDataCache(buf, bytes);
            // Outside a frame this waits for the frame in flight and starts
            // the transfer, but its own wait returns early on a console (the
            // top of the file): the queue is waited for before the read.
            C3D_SyncDisplayTransfer((u32*)s.top->frameBuf.colorBuf, GX_BUFFER_DIM(240, 400),
                                    (u32*)buf, GX_BUFFER_DIM(240, 400),
                                    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                                        GX_TRANSFER_RAW_COPY(0) |
                                        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
            gpuIdle();
            GSPGPU_InvalidateDataCache(buf, bytes);

            // The buffer is the screen as the LCD scans it: a column of 240
            // pixels per screen column, bottom to top. Bytes A, B, G, R.
            for (int y = 0; y < SCREEN_H; y++)
            {
                for (int x = 0; x < SCREEN_W; x++)
                {
                    const uint8* p = buf + 4 * (x * 240 + (239 - y));
                    uint8* d = rgba + 4 * (y * SCREEN_W + x);
                    d[0] = p[3];
                    d[1] = p[2];
                    d[2] = p[1];
                    d[3] = 255;
                }
            }
            linearFree(buf);
            return 1;
        }

        void noteStats(int32 drawsThisFrame)
        {
            s.drawsLast = (uint32)drawsThisFrame;
            if ((uint32)drawsThisFrame > s.drawsPeak)
            {
                s.drawsPeak = (uint32)drawsThisFrame;
            }
        }

        void noteCount(int32 which, uint32 n)
        {
            s.count[which] += n;
        }

        void noteSkinTicks(u64 ticks)
        {
            s.skinTicks += ticks;
        }

        bool32 timingOn(void)
        {
            static int on = -1;
            if (on < 0)
            {
                const char* st = getenv("TSSM_C3D_STATS");
                const char* ab = getenv("TSSM_C3D_AB");
                on = (st != nil && st[0] != '\0' && st[0] != '0') || (ab != nil && ab[0] != '\0');
            }
            return on;
        }

        void noteTicks(int32 which, u64 ticks)
        {
            s.cur.t[which] += ticks;
        }

        void takeFrameSums(FrameTimes* sum, uint32* frames, u64* wallMax)
        {
            *sum = s.sum;
            *frames = s.sumFrames;
            *wallMax = s.wallMax;
            memset(&s.sum, 0, sizeof(s.sum));
            s.sumFrames = 0;
            s.wallMax = 0;
        }

        uint32 tickReadNs(void)
        {
            return s.tickNs;
        }

        RecordScope::RecordScope(void)
        {
            t0 = 0;
            w0 = 0;
            if (s.timing && s.recordDepth++ == 0)
            {
                s.cur.reads += 2;
                t0 = svcGetSystemTick();
                w0 = s.waitTotal;
            }
        }

        RecordScope::~RecordScope(void)
        {
            if (s.timing && --s.recordDepth == 0)
            {
                const u64 spent = svcGetSystemTick() - t0;
                const u64 waited = s.waitTotal - w0;
                s.cur.t[TIME_RECORD] += spent > waited ? spent - waited : 0;
            }
        }

        void getSubmitStats(Stats* out)
        {
            out->frames = s.frames;
            out->draws = s.drawsLast;
            out->drawsPeak = s.drawsPeak;
            out->cmdBufPeak = s.cmdPeak;
            out->ringPeak = s.ringPeak;
            out->queuePeak = s.queuePeak;
            out->textures = s.textures;
            out->texVram = s.texVram;
            out->texLinear = s.texLinear;
            out->vramFree = (unsigned)vramSpaceFree();
            out->linearFree = (unsigned)linearSpaceFree();
            out->skinVertsPeak = s.countPeak[COUNT_SKIN_VERTS];
            out->skinPiecesPeak = s.countPeak[COUNT_SKIN_PIECES];
            out->boneRegsPeak = s.countPeak[COUNT_BONE_REGS];
            out->envDrawsPeak = s.countPeak[COUNT_ENV_DRAWS];
            out->hardSplits = s.hardSplits;
            out->targets = s.targets;
            out->targetVram = s.targetVram;
            out->fogDrawsPeak = s.countPeak[COUNT_FOG_DRAWS];
            out->targetBindsPeak = s.countPeak[COUNT_TARGET_BINDS];
            out->rttDrawsPeak = s.countPeak[COUNT_RTT_DRAWS];
            out->rttClearsPeak = s.countPeak[COUNT_RTT_CLEARS];
            out->readbacksPeak = s.countPeak[COUNT_READBACKS];
            out->copiesPeak = s.countPeak[COUNT_COPIES];
            out->skinUsPeak = (unsigned)((double)s.skinTicksPeak * 1000000.0 / SYSCLOCK_ARM11);
            memset(s.countPeak, 0, sizeof(s.countPeak));
            s.skinTicksPeak = 0;
            s.drawsPeak = 0;
            s.cmdPeak = 0;
            s.ringPeak = 0;
            s.queuePeak = 0;
        }

        void shutdown(void)
        {
            submitClose();
        }
    } // namespace c3d
} // namespace rw
