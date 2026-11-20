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
//
// Frame pipelining (L4): a frame that opens while the GPU still draws the one
// before is recorded ahead, without waiting: into the other of two command
// buffers and the other of two frame rings, with what it would put in the GX
// queue (its lists, fills and copies) kept in order on the side. The first
// thing that needs the GPU done (the present, a hard split, a read-back)
// catches up: it waits for the frame before, opens citro3d's frame (whose own
// wait then returns at once), and queues what was kept, in its order, ahead
// of the rest. So the queue gets what it got before, in the same order, only
// later, and the CPU records frame N+1 while the GPU draws frame N. What the
// GPU may still read is freed only once the frame that read it has run (the
// deferred frees and dead targets carry the frame they were let go in).
// TSSM_C3D_PIPE=off records every frame after the wait, as before.
//
// Stereoscopic 3D (docs/3DS.md, "Stereoscopic 3D"; c3dstereo.cpp decides
// which frames): a 3D frame is recorded once. Its command lists stay in the
// command buffer until the next C3D_FrameBegin, so the right eye is those
// lists queued to the GPU again, with the screen copies between them, after
// a short prologue that writes the right eye's u_eyeSide and clears the one
// colour target (the left picture has gone to the left framebuffer by then).
// For the two eyes' words to draw the same, a 3D frame starts with every
// piece of state sent again (resyncState), nothing in a list depends on the
// eye but u_eyeSide (written raw in front of each eye's lists), and anything
// that cuts the frame (a hard split, a read-back) or has no room leaves the
// frame one picture for both eyes. A 2D frame is recorded exactly as before.

#include "c3dimpl.h"

#include "../../rw/stereomath.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "c3d_shbin.h" // kC3dShbin, made by CMakeLists.txt from shaders/*.vsh
#include "../i3DS.h"

// citro3d 1.7.1's own (source/internal.h), global in libcitro3d.a: the
// commands so far as one list, as C3D_FrameSplit takes it, without the
// frame C3D_FrameSplit wants open (L4's frames recorded ahead).
extern "C" bool C3Di_SplitFrame(u32** pBuf, u32* pSize);

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
            // Stereo: a frame's lists and copies, as many as the GX queue holds.
            const int32 kMaxOps = 32;
            const uint32 kCapBytes = 240 * 400 * 4; // the left eye kept for a capture

            enum
            {
                OP_LIST, // a command list, sent again for the right eye
                OP_LIST_BOTTOM, // the bottom screen's: drawn once
                OP_COPY, // a screen copy (copyScreenInto's GX copy)
                OP_LIST_EYEFREE // eyeskip: a pass the same for both eyes, drawn once
            };
            // eyeskip: the camera textures a frame's binds are kept for.
            const int32 kMaxEyeFree = 8;
            struct EyeFreeBind
            {
                C3D_RenderTarget* target;
                uint32 binds; // this frame
                int32 op; // the frame list entry of its eye-free pass (-1: none)
                bool wasFree; // it had one (sent again or not)
            };

            // What a 3D frame put in the GX queue, in order (frameEndStereo).
            struct FrameOp
            {
                uint8 kind;
                uint8 replay; // sent again for the right eye
                uint16 skip; // bytes the right eye leaves out (list 1: the left u_eyeSide)
                u32* addr; // a list's first word; a copy's source
                u32 bytes; // a list's size; a copy's size
                u32* dst; // a copy's
                u32 inDim, outDim, flags;
            };

            struct Deferred
            {
                void* linear; // linearFree
                C3D_Tex tex; // C3D_TexDelete, when hasTex
                bool hasTex;
                uint32 frame; // the last frame that may read it (freeUpTo)
            };

            // L4: what a frame recorded ahead would have put in the GX queue,
            // kept in order until it catches up (catchUp).
            enum
            {
                AHEAD_LIST, // GX_ProcessCommandList
                AHEAD_FILL, // C3D_RenderTargetClear
                AHEAD_COPY // GX_TextureCopy
            };
            struct AheadOp
            {
                uint8 kind;
                uint8 flags; // a list's
                u32* addr; // a list's first word; a copy's source
                u32 bytes; // a list's or a copy's size
                C3D_RenderTarget* target; // a fill's
                uint32 bits, color; // a fill's
                u32* dst; // a copy's
                u32 inDim, outDim, xflags; // a copy's
            };
            const int32 kMaxAhead = 32; // as many as the GX queue holds

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
                uint32* deadFrame; // the last frame that may draw into each
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

                uint8* ring; // this frame's (one of rings under L4)
                uint8* rings[2];
                u32* cmdBufs[2]; // L4: the command buffers, by frame (0 is citro3d's)
                bool pipe; // L4 on
                bool pipeForce; // TSSM_C3D_PIPE=force: every frame ahead (a test)
                bool ahead; // this frame is being recorded ahead (catchUp)
                AheadOp aheadOps[kMaxAhead];
                int32 numAhead;
                uint32 aheadFrames, catchUps; // since the stats line
                uint32 aheadEarly; // caught up before the present (a split, a read-back)
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
                uint32 splitsAtFrameStart; // hardSplits when this frame opened
                u32* cmdBase; // the command buffer's start (C3D_FrameBegin resets to it)

                // gpusplit (probe 14): the pass of the piece being recorded
                // (-1: none yet), whether it holds anything for the GPU, and
                // what a piece holding nothing costs (measured once).
                int32 piecePass;
                bool pieceWork;
                bool splitCalibrated;
                float splitEmptyMs;
                bool presentPending; // the frame before ended split: its present is timed
                // The first gpusplit frame's pieces (0 not yet, 1 this frame,
                // 2 logged).
                int32 splitSeqState;
                char splitSeq[400];
                // A block spilled past the frame ring this frame: C3D_FrameEnd
                // flushes the whole heap even under cmdflush.
                bool spilled;
                bool scissored; // fill=off's scissor is set
                bool recordOn; // RecordScope times: timing, or the stereo guard wants it

                // Stereo. rightView: a header on the top target's buffers,
                // linked to (GFX_TOP, GFX_RIGHT); only the right eye draws on it.
                C3D_RenderTarget* rightView;
                bool stereoReady; // after the open's self-test
                bool stereo; // this frame is drawn for two eyes
                bool nextStereo; // the next frame will be (decided at the present)
                bool stereoPrev; // the frame before was: its state is sent again
                bool lastStereo; // the frame just presented showed two pictures
                bool eyeBool; // b_eye as citro3d has it
                bool eyeWant; // the draws are the main camera's in a 3D frame (b_eye, eyetest aside)
                bool trimBool; // b_trim as citro3d has it (skintrim)
                int32 drop; // DROP_*: why this 3D frame stays one picture
                uint32 eyeDraws; // draws with b_eye into the top screen
                bool inBottom; // the bottom screen is bound (its list is not sent again)
                bool fillColorSet; // the frame's first top fill cleared colour
                bool fillDepthSet; // and depth
                // A 3D frame whose first top fill did not clear colour draws
                // over the frame before's picture: that is kept (a GX copy of
                // the target, queued before the first top draw) for the right
                // eye to start from too.
                uint8* topSave; // 240 x 400 RGBA8 tiles, linear memory, made once
                // savevram: the same in VRAM, made at a 3D frame's first need
                // when VRAM has room (kept until textures are made outside a
                // frame: a load, whose textures VRAM is for), and the one this
                // frame's start is in.
                uint8* topSaveV;
                uint8* topSaveAt;
                uint32 topSaveVFrom; // not made again before this frame (after a load let it go)
                bool topSaved; // this frame's start is in topSaveAt
                uint32 noDepthFrames; // 3D frames whose top depth was not cleared first
                uint32 fillColor;
                u32* cmdStart; // the frame's command buffer start
                u32* cmdNext; // where the frame list's next list must begin
                uint16 eyeSkip; // the left u_eyeSide's bytes there
                FrameOp ops[kMaxOps];
                int32 numOps;
                C3D_Tex* unit2; // what texture unit 2 holds (nil: nothing yet)
                uint8* capL; // the left eye of a capture frame (linear memory)
                bool capLValid;
                bool readLeft; // readScreen reads capL
                u64 frameWait; // this frame's vblank and GPU waits, ticks
                u64 stLastPresent;
                uint32 replayLists, replayBytes; // since the stats line
                u64 replayTicks; // presentRight's CPU, since the stats line
                uint32 queuePeak3D;
                // eyeskip (eyeFreeBegin): the pass being recorded that the
                // right eye leaves out (nil: none); this frame's camera texture
                // binds (numBinds past kMaxEyeFree: no more eye-free passes);
                // whether anything was drawn since the latest bind; and since
                // the stats line the lists left out, their bytes, those sent
                // again after all, and the two-eye frames.
                C3D_RenderTarget* eyeFree;
                EyeFreeBind binds[kMaxEyeFree];
                int32 numBinds;
                bool drawnSinceBind;
                uint32 eyeFreeLists, eyeFreeBytes, eyeFreeBack, twoEyeFrames;
                // alphafill (alphaResetSkip): the top screen's alpha where it
                // is one value all over (-1: not known); the reset's alpha the
                // next frame's first fill gives (-1: none), this frame's first
                // reset's; alpha writes held from the fill to the reset; frames
                // (alphaOff) no predictions after a hold ended early; since the stats line: resets
                // seen, left out, frames filled with the reset's alpha, alpha
                // writes held, holds ended early (by a draw reading the
                // screen's alpha, a screen copy, a clear quad) and frames
                // that held to their end with no reset.
                int32 topAlpha;
                int32 alphaPredict;
                int32 alphaNext;
                bool alphaHold;
                bool resetFull; // the reset quad about to be drawn covers the screen
                bool alphaOff;
                uint32 aResets, aSkipped, aFills, aHeld, aEndRead, aEndCopy, aEndClear, aEndPart, aMiss;
                uint32 aNoFill; // frames drawn whose top screen had no colour fill (nothing to predict with)
            };

            State s;

            void gpuIdle(void);

            // alphafill: a hold ended before the reset leaves that frame's
            // alpha wrong, so the prediction stops for the rest of the run.
            void alphaGiveUp(void)
            {
                if (!s.alphaOff)
                {
                    s.alphaOff = true;
                    printf("tssm: c3d: alphafill: a hold ended before the glow's reset (frame %u); "
                           "no more predictions this run\n",
                           (unsigned)s.frames);
                    fflush(stdout);
                }
            }

            // savevram: the block given back (a camera texture or a load
            // needs the VRAM, or the word is off). Outside a frame once the
            // GPU is done; inside one through the deferred list, as this
            // frame's copies may still read it. Not made again for `hold`
            // frames. TRUE: the VRAM is free now.
            bool topSaveRelease(uint32 hold)
            {
                if (s.topSaveV == nil)
                {
                    return false;
                }
                if (s.inFrame)
                {
                    deferFree(s.topSaveV);
                }
                else
                {
                    gpuIdle();
                    vramFree(s.topSaveV);
                }
                s.topSaveV = nil;
                s.topSaveVFrom = s.frames + hold;
                return !s.inFrame;
            }

            // L4's stand-ins for citro3d's frame calls (below).
            void drawOn(C3D_RenderTarget* target);
            void frameSplit(uint8 flags);
            void targetFill(C3D_RenderTarget* target, uint32 bits, uint32 color);
            void textureCopy(u32* from, u32 inDim, u32* to, u32 outDim, u32 bytes, u32 flags);
            void catchUp(const char* why);

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

            // Stereo: everything a draw depends on is sent again from the
            // next draw on, so that a list does not lean on what the GPU held
            // before it (the frame before, or for the right eye the left
            // eye's end). citro3d re-sends its own copies of the uniforms
            // (c0-c82; u_eyeSide is not among them, citro3d never has it),
            // b_eye as it is, the texture units, and the device's and these
            // filters forget what they last gave (deviceResync).
            void resyncState(void)
            {
                s.program = -1;
                s.format = -1;
                s.buffer = nil;
                (void)C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, 0, NUM_REGS);
                C3D_BoolUnifSet(GPU_VERTEX_SHADER, REG_BEYE, s.eyeBool);
                if (s.unit2 != nil)
                {
                    C3D_TexBind(2, s.unit2);
                }
                deviceResync();
            }

            // u_eyeSide (c83), raw: in front of an eye's lists, never inside
            // one, so the lists are the same words for both eyes. Returns the
            // bytes written (8 words: the config write and the four floats,
            // padded).
            uint16 writeEyeSide(int32 eye)
            {
                u32* buf = nil;
                u32 size = 0;
                u32 before = 0;
                GPUCMD_GetBuffer(&buf, &size, &before);
                const float x = eye < 0 ? 0.0f : stereoEyeSide(eye); // -1: eyetest=2d's 0
                u32 v[4];
                const float w4[4] = { 0.0f, 0.0f, 0.0f, x }; // C3D_FVec's order: w, z, y, x
                memcpy(v, w4, sizeof(v));
                GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG, 0x80000000u | (u32)REG_EYESIDE);
                GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA, v, 4);
                u32 after = 0;
                GPUCMD_GetBuffer(&buf, &size, &after);
                return (uint16)((after - before) * 4);
            }

            // Stereo: the frame so far into the GX queue as one list, noted
            // in the frame list (nothing recorded: no list).
            void splitHere(uint8 kind)
            {
                u32* b0 = nil;
                u32 size = 0;
                u32 off = 0;
                GPUCMD_GetBuffer(&b0, &size, &off);
                if (b0 != s.cmdNext && s.drop == DROP_NONE)
                {
                    // Something split the frame outside this list (a
                    // C3D_Sync* call): the list it sent is not in the frame
                    // list, so the right eye would miss its draws.
                    logOnce("stereogap", "stereo: a command list went to the GPU outside the frame "
                                         "list (%u bytes); such frames stay one picture",
                            (unsigned)((b0 - s.cmdNext) * 4));
                    s.drop = DROP_SPLIT;
                }
                if (off == 0)
                {
                    return;
                }
                frameSplit(0);
                u32* b1 = nil;
                GPUCMD_GetBuffer(&b1, &size, &off);
                s.cmdNext = b1;
                if (b1 == b0)
                {
                    return;
                }
                s.queueEntries++;
                if (s.numOps >= kMaxOps)
                {
                    s.drop = DROP_QUEUE;
                    return;
                }
                if (kind != OP_LIST_EYEFREE)
                {
                    // Anything else cutting an eye-free pass (a copy, the
                    // bottom screen): its list is sent again as any other.
                    s.eyeFree = nil;
                }
                FrameOp* op = &s.ops[s.numOps++];
                memset(op, 0, sizeof(*op));
                op->kind = kind;
                op->replay = kind == OP_LIST;
                op->addr = b0;
                op->bytes = (u32)(b1 - b0) * 4;
                op->skip = b0 == s.cmdStart ? s.eyeSkip : 0;
                if (op->bytes <= op->skip)
                {
                    op->replay = 0; // the left u_eyeSide alone: nothing for the right eye
                }
            }

            // eyeskip: the eye-free pass ends at a change of target. Its list
            // is drawn once (the right eye reads what the left one drew), so
            // what follows must not depend on what it left.
            void leaveEyeFree(void)
            {
                if (s.eyeFree == nil)
                {
                    return;
                }
                C3D_RenderTarget* const t = s.eyeFree;
                if (s.drop != DROP_NONE)
                {
                    s.eyeFree = nil; // one picture: nothing is sent again
                    return;
                }
                const int32 before = s.numOps;
                splitHere(OP_LIST_EYEFREE);
                s.eyeFree = nil;
                if (s.drop != DROP_NONE || s.numOps == before)
                {
                    return;
                }
                for (int32 i = 0; i < s.numBinds && i < kMaxEyeFree; i++)
                {
                    if (s.binds[i].target == t)
                    {
                        s.binds[i].op = before;
                        s.binds[i].wasFree = true;
                    }
                }
                resyncState();
            }

            // eyeskip: a camera texture bound (drawn on from here). Bound
            // again after its eye-free pass, the right eye would read the
            // later picture where the left one read the earlier, so that pass
            // is sent again after all (its list is whole, and what follows it
            // was re-sent).
            void eyeFreeBound(C3D_RenderTarget* t)
            {
                s.drawnSinceBind = false;
                for (int32 i = 0; i < s.numBinds && i < kMaxEyeFree; i++)
                {
                    EyeFreeBind& b = s.binds[i];
                    if (b.target == t)
                    {
                        b.binds++;
                        if (b.op >= 0)
                        {
                            s.ops[b.op].replay = 1;
                            b.op = -1;
                            s.eyeFreeBack++;
                        }
                        return;
                    }
                }
                if (s.numBinds < kMaxEyeFree)
                {
                    EyeFreeBind& b = s.binds[s.numBinds];
                    b.target = t;
                    b.binds = 1;
                    b.op = -1;
                    b.wasFree = false;
                }
                s.numBinds++; // past kMaxEyeFree: untracked, and no more eye-free passes
            }

            // Stereo: off the bottom screen. Its list is drawn once, so what
            // follows must not depend on what it left.
            void leaveBottom(void)
            {
                if (!s.inBottom)
                {
                    return;
                }
                splitHere(OP_LIST_BOTTOM);
                s.inBottom = false;
                resyncState();
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
                    gpuFree(d->linear);
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

            // The frame a block let go now may still be read by: this one
            // inside a frame, the one just presented outside.
            uint32 readerFrame(void)
            {
                return s.inFrame ? s.frames : s.frames - 1;
            }

            // Once the GPU has run frame `done`: what only it and the frames
            // before it read.
            void freeUpTo(uint32 done)
            {
                int32 kept = 0;
                for (int32 i = 0; i < s.numDeferred; i++)
                {
                    Deferred* d = &s.deferred[i];
                    if ((int32)(d->frame - done) <= 0)
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
                d->frame = readerFrame();
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

            // L4: the dead targets no frame after `done` draws into.
            void deleteDeadUpTo(uint32 done)
            {
                int32 kept = 0;
                for (int32 i = 0; i < s.numDead; i++)
                {
                    if ((int32)(s.deadFrame[i] - done) <= 0)
                    {
                        C3D_RenderTargetDelete(s.deadTargets[i]);
                    }
                    else
                    {
                        s.deadTargets[kept] = s.deadTargets[i];
                        s.deadFrame[kept] = s.deadFrame[i];
                        kept++;
                    }
                }
                s.numDead = kept;
            }

            // C3D_FrameDrawOn's own three lines, which do not need citro3d's
            // frame open (a frame recorded ahead has not opened it yet).
            void drawOn(C3D_RenderTarget* target)
            {
                target->used = true;
                C3D_SetFrameBuf(&target->frameBuf);
                C3D_SetViewport(0, 0, target->frameBuf.width, target->frameBuf.height);
            }

            void aheadPush(const AheadOp& op);

            // C3D_FrameSplit: the commands so far as a list for the GX queue,
            // or kept for it while the frame is ahead.
            void frameSplit(uint8 flags)
            {
                if (!s.ahead)
                {
                    C3D_FrameSplit(flags);
                    return;
                }
                u32* buf = nil;
                u32 size = 0;
                if (C3Di_SplitFrame(&buf, &size))
                {
                    AheadOp op;
                    memset(&op, 0, sizeof(op));
                    op.kind = AHEAD_LIST;
                    op.flags = flags;
                    op.addr = buf;
                    op.bytes = size * 4;
                    aheadPush(op);
                }
            }

            // C3D_RenderTargetClear (a GX fill), kept while ahead.
            void targetFill(C3D_RenderTarget* target, uint32 bits, uint32 color)
            {
                if (!s.ahead)
                {
                    C3D_RenderTargetClear(target, (C3D_ClearBits)bits, color, 0);
                    return;
                }
                AheadOp op;
                memset(&op, 0, sizeof(op));
                op.kind = AHEAD_FILL;
                op.target = target;
                op.bits = bits;
                op.color = color;
                aheadPush(op);
            }

            // GX_TextureCopy inside a frame, kept while ahead.
            void textureCopy(u32* from, u32 inDim, u32* to, u32 outDim, u32 bytes, u32 flags)
            {
                if (!s.ahead)
                {
                    GX_TextureCopy(from, inDim, to, outDim, bytes, flags);
                    return;
                }
                AheadOp op;
                memset(&op, 0, sizeof(op));
                op.kind = AHEAD_COPY;
                op.addr = from;
                op.inDim = inDim;
                op.dst = to;
                op.outDim = outDim;
                op.bytes = bytes;
                op.xflags = flags;
                aheadPush(op);
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

            s.topAlpha = -1;
            s.alphaPredict = -1; // nothing to predict from yet
            s.alphaNext = -1;
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
            // The right eye's header: the same colour and depth buffers, not
            // in citro3d's list of targets (it frees neither), on the right
            // side's output, which a frame uses only when it drew on it.
            s.rightView = (C3D_RenderTarget*)calloc(1, sizeof(C3D_RenderTarget));
            if (s.rightView != nil)
            {
                s.rightView->frameBuf = s.top->frameBuf;
                s.rightView->ownsColor = false;
                s.rightView->ownsDepth = false;
                C3D_RenderTargetSetOutput(s.rightView, GFX_TOP, GFX_RIGHT, flags);
            }

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
            s.rings[0] = s.ring;
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
                // L4: a second command buffer and frame ring (citro3d's buffer
                // is the first, found at the first frame).
                const char* pe = getenv("TSSM_C3D_PIPE");
                const char* be = getenv("TSSM_C3D_BENCH");
                const bool want = !(pe != nil && strcmp(pe, "off") == 0) &&
                                  !(be != nil && be[0] != '\0' && be[0] != '0');
                if (want)
                {
                    s.cmdBufs[1] = (u32*)linearAlloc((cmdBytes + 0xF) & ~0xFu);
                    s.rings[1] = (uint8*)linearAlloc(kRingBytes);
                    s.pipe = s.cmdBufs[1] != nil && s.rings[1] != nil;
                    // Azahar runs the GPU's lists as they are queued, so a
                    // frame there is hardly ever ahead by itself.
                    s.pipeForce = s.pipe && pe != nil && strcmp(pe, "force") == 0;
                    if (!s.pipe)
                    {
                        linearFree(s.cmdBufs[1]);
                        linearFree(s.rings[1]);
                        s.cmdBufs[1] = nil;
                        s.rings[1] = nil;
                    }
                }
                printf("tssm: c3d: frame pipelining (L4) %s\n",
                       s.pipeForce ? "forced: every frame recorded ahead (TSSM_C3D_PIPE=force)" :
                       s.pipe      ? "on: a frame is recorded while the GPU draws the one before" :
                       want   ? "off: no linear memory for the second command buffer and ring" :
                                "off (TSSM_C3D_PIPE=off or the benchmark)");
            }
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
            stereoOpen(params != nil ? params->stereo : nil, params != nil ? params->stereoDepth : 50);
            if (s.rightView == nil)
            {
                printf("tssm: c3d: stereo: no memory for the right eye's target header; 2D only\n");
                fflush(stdout);
            }
            s.stereoReady = true;
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
            free(s.deadFrame);
            s.deadTargets = nil;
            s.deadFrame = nil;
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
            linearFree(s.rings[0]);
            linearFree(s.rings[1]);
            linearFree(s.cmdBufs[1]);
            linearFree(s.staging);
            free(s.deferred);
            s.ring = s.staging = nil;
            s.rings[0] = s.rings[1] = nil;
            s.cmdBufs[0] = s.cmdBufs[1] = nil;
            s.pipe = false;
            s.deferred = nil;
            s.deferredCap = 0;
            if (s.bottom != nil)
            {
                quitStep("freeing the overlay's bottom-screen target and textures");
                overlayClose();
                C3D_RenderTargetDelete(s.bottom);
                s.bottom = nil;
            }
            if (s.rightView != nil)
            {
                quitStep("detaching and freeing the right eye's target header (stereo)");
                C3D_RenderTargetDetachOutput(s.rightView);
                free(s.rightView);
                s.rightView = nil;
            }
            if (s.capL != nil)
            {
                linearFree(s.capL);
                s.capL = nil;
            }
            if (s.topSave != nil)
            {
                linearFree(s.topSave);
                s.topSave = nil;
            }
            if (s.topSaveV != nil)
            {
                vramFree(s.topSaveV);
                s.topSaveV = nil;
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

        namespace
        {
            // fill=off: on the top screen nothing past one pixel is drawn, so
            // the GPU time left is the vertices' and the triangles' set-up.
            // Set at every bind and viewport (citro3d keeps it otherwise).
            void applyScissor(void)
            {
                const bool want = !modes.fill && s.bound != nil && s.bound == s.top;
                if (want)
                {
                    C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 1, 1);
                    s.scissored = true;
                }
                else if (s.scissored)
                {
                    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
                    s.scissored = false;
                }
            }

            // The frame so far to the GPU and waited for, nothing shown; the
            // same target and viewport after it. The GPU's registers keep
            // their state. Returns the wait.
            u64 splitWait(bool ringToo)
            {
                if (s.stereo)
                {
                    // The lists so far are gone with the command buffer: this
                    // frame shows one picture.
                    s.drop = DROP_SPLIT;
                    s.numOps = 0;
                }
                catchUp("a hard split");
                C3D_RenderTarget* bound = s.bound;
                detachOutputs();
                C3D_FrameEnd(0);
                const u64 t0 = svcGetSystemTick();
                C3D_FrameBegin(0); // waits for the GPU
                const u64 t = svcGetSystemTick() - t0;
                if (s.pipe && s.cmdBufs[0] != nil)
                {
                    // This frame's buffer from its start again: the GPU has
                    // run all of both.
                    GPUCMD_SetBuffer(s.cmdBufs[s.frames & 1], ((s.cmdBytes + 0xF) & ~0xFu) / 4,
                                     0);
                    u32 sz = 0;
                    u32 o = 0;
                    GPUCMD_GetBuffer(&s.cmdBase, &sz, &o);
                }
                if (s.timing)
                {
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
                    drawOn(bound);
                    s.bound = bound;
                    C3D_SetViewport((u32)s.viewport[0], (u32)s.viewport[1], (u32)s.viewport[2],
                                    (u32)s.viewport[3]);
                }
                applyScissor();
                return t;
            }

            // gpusplit: the piece recorded so far, split off, waited for and
            // timed by citro3d (from its C3D_FrameEnd to the queue's end),
            // less what a piece with nothing in it takes.
            void timedPiece(bool ringToo)
            {
                const int32 pass = s.piecePass;
                splitWait(ringToo);
                float ms = C3D_GetDrawingTime() - s.splitEmptyMs;
                if (pass >= 0 && pass < NUM_PASSES)
                {
                    s.cur.passMs[pass] += ms > 0.0f ? ms : 0.0f;
                }
                s.cur.pieces++;
                s.pieceWork = false;
                if (s.splitSeqState == 1)
                {
                    // The first gpusplit frame's pieces in order, for the log.
                    static const char* kName[NUM_PASSES] = { "3D",   "HUD",  "shadow",
                                                             "rtt",  "glow", "warp",
                                                             "copy", "bottom", "present" };
                    const size_t n = strlen(s.splitSeq);
                    if (n + 24 < sizeof(s.splitSeq))
                    {
                        snprintf(s.splitSeq + n, sizeof(s.splitSeq) - n, "%s%s %.2f",
                                 n > 0 ? ", " : "",
                                 pass >= 0 && pass < NUM_PASSES ? kName[pass] : "?",
                                 (double)(ms > 0.0f ? ms : 0.0f));
                    }
                }
            }

            // gpusplit: the next draw (or copy) belongs to `pass`; a change
            // ends the piece before it.
            void passBoundary(int32 pass)
            {
                if (!modes.gpuSplit || !s.inFrame || s.piecePass == pass)
                {
                    return;
                }
                if (s.piecePass >= 0 && s.pieceWork)
                {
                    timedPiece(false);
                }
                s.piecePass = pass;
            }

            // Once, at the first gpusplit frame: eight pieces holding only a
            // framebuffer flush (C3D_FrameSplit adds the rest), the median
            // taken off every piece after.
            void calibrateSplit(void)
            {
                s.splitCalibrated = true;
                float ms[8];
                for (int i = 0; i < 8; i++)
                {
                    GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
                    splitWait(false);
                    ms[i] = C3D_GetDrawingTime();
                }
                for (int i = 1; i < 8; i++)
                {
                    for (int j = i; j > 0 && ms[j] < ms[j - 1]; j--)
                    {
                        const float t = ms[j];
                        ms[j] = ms[j - 1];
                        ms[j - 1] = t;
                    }
                }
                s.splitEmptyMs = (ms[3] + ms[4]) * 0.5f;
                printf("tssm: c3d: gpusplit: a piece with nothing in it takes %.3f ms of the GPU "
                       "(median of 8, %.3f to %.3f); taken off every piece\n",
                       (double)s.splitEmptyMs, (double)ms[0], (double)ms[7]);
                fflush(stdout);
                abLog("gpusplit: a piece with nothing in it takes %.3f ms (median of 8, %.3f to "
                      "%.3f); taken off every piece",
                      (double)s.splitEmptyMs, (double)ms[0], (double)ms[7]);
            }
        } // namespace

        void frameEnsure(void)
        {
            if (s.inFrame || !s.open)
            {
                return;
            }
            // The stereo guard's frame times need the waits too.
            const bool stereoOn = s.stereoReady && stereoEnabled();
            if (stereoOn)
            {
                stereoGuardTestSleep();
            }
            const bool tick = s.timing || stereoOn;
            // C3D_FrameBegin(C3D_FRAME_SYNCDRAW) in its two waits, timed apart.
            const u64 t0 = tick ? svcGetSystemTick() : 0;
            C3D_FrameSync();
            const u64 t1 = tick ? svcGetSystemTick() : 0;
            // L4: the frame before still on the GPU, and nothing that wants
            // it done first: recorded ahead, into this frame's own buffer and
            // ring (catchUp). A dead target to delete waits, as before.
            s.ahead = false;
            if (s.pipe && modes.pipe && !modes.gpuSplit && s.numDead == 0 &&
                s.cmdBufs[0] != nil && (s.pipeForce || !C3D_FrameBegin(C3D_FRAME_NONBLOCK)))
            {
                s.ahead = true;
                s.numAhead = 0;
                s.aheadFrames++;
            }
            else if (!s.pipe || !modes.pipe || modes.gpuSplit || s.numDead != 0 ||
                     s.cmdBufs[0] == nil)
            {
                deleteDeadUpTo(s.frames - 1);
                C3D_FrameBegin(0);
            }
            if (s.pipe)
            {
                if (s.cmdBufs[0] == nil && !s.ahead)
                {
                    // citro3d's own buffer, as C3D_FrameBegin set it.
                    u32* b = nil;
                    u32 sz = 0;
                    u32 o = 0;
                    GPUCMD_GetBuffer(&b, &sz, &o);
                    s.cmdBufs[0] = b;
                }
                GPUCMD_SetBuffer(s.cmdBufs[s.frames & 1], ((s.cmdBytes + 0xF) & ~0xFu) / 4, 0);
                s.ring = s.rings[s.frames & 1];
            }
            {
                u32 size = 0;
                u32 off = 0;
                GPUCMD_GetBuffer(&s.cmdBase, &size, &off);
            }
            if (tick)
            {
                const u64 t2 = svcGetSystemTick();
                if (s.timing)
                {
                    if (s.lastPresent != 0 && t0 > s.lastPresent)
                    {
                        // The game's update: from the present to its first draw.
                        s.cur.t[TIME_UPDATE] += t0 - s.lastPresent;
                    }
                    s.cur.t[TIME_SYNC] += t1 - t0;
                    s.cur.t[TIME_GPU] += t2 - t1;
                    // Ahead: the frame before is still drawing (catchUp).
                    const float drawn = s.ahead ? 0.0f : C3D_GetDrawingTime();
                    s.cur.t[TIME_DRAWN] += (u64)((double)drawn * (double)SYSCLOCK_ARM11 / 1000.0);
                    if (s.presentPending)
                    {
                        // The frame before ended split: what it left was only its
                        // display transfers.
                        s.cur.passMs[PASS_PRESENT] += drawn;
                    }
                }
                s.waitTotal += t2 - t0;
                s.frameWait = t2 - t0;
            }
            s.presentPending = false;
            s.recordOn = s.timing || stereoWantsRecordTiming();
            s.inFrame = true;
            s.splitsAtFrameStart = s.hardSplits;
            s.piecePass = -1;
            s.pieceWork = false;
            s.spilled = false;
            // Stereo (decided at the last present, which the cameras of this
            // frame have seen): the state sent again after a 3D frame and in
            // one, and the left eye's u_eyeSide as the first words.
            s.stereo = s.nextStereo && !modes.gpuSplit;
            s.numOps = 0;
            s.drop = DROP_NONE;
            s.eyeFree = nil;
            s.numBinds = 0;
            s.topAlpha = -1; // the picture before, as far as this frame knows
            s.alphaHold = false;
            s.alphaNext = -1;
            s.eyeDraws = 0;
            s.inBottom = false;
            s.fillColorSet = false;
            s.fillDepthSet = false;
            s.topSaved = false;
            s.capLValid = false;
            if (s.trimBool != (modes.skinTrim != 0))
            {
                // skintrim, from this frame's first draw.
                C3D_BoolUnifSet(GPU_VERTEX_SHADER, REG_BTRIM, modes.skinTrim != 0);
                s.trimBool = modes.skinTrim != 0;
            }
            if (!s.stereo && modes.eyeTest == EYETEST_2D)
            {
                // eyetest=2d: the eye code with u_eyeSide 0.
                (void)writeEyeSide(-1);
            }
            else if (!s.stereo && s.eyeBool)
            {
                // A 2D frame after a 3D one: no eye shift from its first draw.
                C3D_BoolUnifSet(GPU_VERTEX_SHADER, REG_BEYE, false);
                s.eyeBool = false;
                s.eyeWant = false;
            }
            if (s.stereo || s.stereoPrev)
            {
                resyncState();
            }
            s.stereoPrev = false;
            if (s.stereo)
            {
                u32 size = 0;
                u32 off = 0;
                GPUCMD_GetBuffer(&s.cmdStart, &size, &off);
                s.cmdNext = s.cmdStart;
                s.eyeSkip = writeEyeSide(EYE_LEFT);
            }
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
            if (!s.ahead)
            {
                // The frame before has run (catchUp does this for a frame
                // recorded ahead).
                freeUpTo(s.frames - 1);
            }
            deviceResetStats();
            if (s.loadTextures >= 16)
            {
                // A level was loaded since the last frame: what it left
                // (the margins it keeps).
                // The C heap too (models, sounds, the level's own blocks): in
                // use, and the most malloc has taken from the system so far.
                const struct mallinfo heap = mallinfo();
                printf("tssm: c3d: memory after a load (%u textures made): %u textures, VRAM %u "
                       "KB, linear %u KB; %u camera textures, %u KB of VRAM; VRAM free %u KB, "
                       "linear free %u KB; C heap %u KB in use, %u KB obtained\n",
                       (unsigned)s.loadTextures, (unsigned)s.textures, (unsigned)(s.texVram >> 10),
                       (unsigned)(s.texLinear >> 10), (unsigned)s.targets,
                       (unsigned)(s.targetVram >> 10), (unsigned)(vramSpaceFree() >> 10),
                       (unsigned)(linearSpaceFree() >> 10), (unsigned)(heap.uordblks >> 10),
                       (unsigned)(heap.arena >> 10));
                fflush(stdout);
                abLog("memory after a load (%u textures made): %u textures, VRAM %u KB, linear %u "
                      "KB; %u camera textures and copy blocks, %u KB of VRAM; VRAM free %u KB, "
                      "linear free %u KB",
                      (unsigned)s.loadTextures, (unsigned)s.textures,
                      (unsigned)(s.texVram >> 10), (unsigned)(s.texLinear >> 10),
                      (unsigned)s.targets, (unsigned)(s.targetVram >> 10),
                      (unsigned)(vramSpaceFree() >> 10), (unsigned)(linearSpaceFree() >> 10));
                stereoLevelLoaded();
            }
            s.loadTextures = 0;
            drawPendingEdges();
            if (modes.gpuSplit && !s.splitCalibrated)
            {
                calibrateSplit();
            }
            else if (modes.gpuSplit && s.splitSeqState == 0)
            {
                s.splitSeqState = 1;
                s.splitSeq[0] = '\0';
            }
            else if (!modes.gpuSplit)
            {
                s.splitSeqState = 0; // the next gpusplit segment logs its own
            }
        }

        namespace
        {
            // L4: a frame recorded ahead joins the GPU's queue: the frame
            // before waited for, citro3d's frame opened on this one's buffer
            // where it got to, the frame before's blocks freed, and what this
            // one kept for the GX queue queued, in order.
            void catchUp(const char* why)
            {
                if (!s.ahead)
                {
                    return;
                }
                u32* buf = nil;
                u32 size = 0;
                u32 off = 0;
                GPUCMD_GetBuffer(&buf, &size, &off);
                const u64 t0 = svcGetSystemTick();
                C3D_FrameBegin(0); // the frame before's queue: waited for
                const u64 t = svcGetSystemTick() - t0;
                GPUCMD_SetBuffer(buf, size, off);
                s.ahead = false;
                s.catchUps++;
                if (why != nil)
                {
                    s.aheadEarly++;
                    if (s.aheadEarly <= 4)
                    {
                        printf("tssm: c3d: pipelining: a frame caught up before its present "
                               "(%s)\n",
                               why);
                        fflush(stdout);
                    }
                }
                if (s.timing)
                {
                    s.cur.t[TIME_GPU] += t;
                    const float drawn = C3D_GetDrawingTime();
                    s.cur.t[TIME_DRAWN] +=
                        (u64)((double)drawn * (double)SYSCLOCK_ARM11 / 1000.0);
                }
                s.waitTotal += t;
                s.frameWait += t;
                freeUpTo(s.frames - 1);
                for (int32 i = 0; i < s.numAhead; i++)
                {
                    const AheadOp& op = s.aheadOps[i];
                    if (op.kind == AHEAD_LIST)
                    {
                        GX_ProcessCommandList(op.addr, op.bytes, op.flags);
                    }
                    else if (op.kind == AHEAD_FILL)
                    {
                        C3D_RenderTargetClear(op.target, (C3D_ClearBits)op.bits, op.color, 0);
                    }
                    else
                    {
                        GX_TextureCopy(op.addr, op.inDim, op.dst, op.outDim, op.bytes,
                                       op.xflags);
                    }
                }
                s.numAhead = 0;
            }

            void aheadPush(const AheadOp& op)
            {
                if (s.numAhead >= kMaxAhead)
                {
                    catchUp("its kept queue entries");
                }
                if (!s.ahead)
                {
                    // Caught up: straight into the queue, in order after the rest.
                    if (op.kind == AHEAD_LIST)
                    {
                        GX_ProcessCommandList(op.addr, op.bytes, op.flags);
                    }
                    else if (op.kind == AHEAD_FILL)
                    {
                        C3D_RenderTargetClear(op.target, (C3D_ClearBits)op.bits, op.color, 0);
                    }
                    else
                    {
                        GX_TextureCopy(op.addr, op.inDim, op.dst, op.outDim, op.bytes,
                                       op.xflags);
                    }
                    return;
                }
                s.aheadOps[s.numAhead++] = op;
            }
        } // namespace

        void frameCatchUp(const char* why)
        {
            if (s.inFrame)
            {
                catchUp(why);
            }
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

        namespace
        {
            // The left eye's capture copy, made once (linear memory).
            bool ensureCapBuffer(void)
            {
                if (s.capL == nil)
                {
                    s.capL = (uint8*)linearAlloc(kCapBytes);
                    if (s.capL == nil)
                    {
                        logOnce("capl", "stereo: no linear memory for the left eye's capture");
                        return false;
                    }
                    GSPGPU_InvalidateDataCache(s.capL, kCapBytes);
                }
                return true;
            }

            // The present of a 3D frame (docs/3DS.md, "Stereoscopic 3D"): the
            // left eye's last list, its picture to the left framebuffer, the
            // right eye's prologue (its u_eyeSide, a clear of the target),
            // then the lists and copies again; C3D_FrameEnd sends the target
            // to the right framebuffer and swaps the top screen as stereo.
            // FALSE (s.drop set): one picture for both eyes, the left one.
            bool presentRight(void)
            {
                u32* buf = nil;
                u32 size = 0;
                u32 off = 0;
                GPUCMD_GetBuffer(&buf, &size, &off);
                // The prologue is under 1 KB; nothing may split the frame in it.
                if (buf == nil || off + 1024 > size - size / 10)
                {
                    s.drop = DROP_ROOM;
                    return false;
                }
                uint32 replays = 0;
                for (int32 i = 0; i < s.numOps; i++)
                {
                    replays += s.ops[i].replay;
                }
                const bool cap = stereoCaptureLeftWanted() && ensureCapBuffer();
                // The last list (and its replay), the left transfer, the
                // capture, the fill, the prologue, the replays, FrameEnd's
                // right and bottom transfers.
                const uint32 need = 2 + 1 + (cap ? 1 : 0) + (s.topSaved ? 1 : 0) + 1 + 1 +
                                    replays + 1 + (s.bottom != nil && s.bottom->used ? 1 : 0) +
                                    (s.eyeFree != nil ? 1 : 0);
                if (s.queueEntries + need > 30 || s.numOps >= kMaxOps)
                {
                    s.drop = DROP_QUEUE;
                    return false;
                }
                if (!s.fillDepthSet)
                {
                    // The left eye began on the depth the frame before left;
                    // the right one begins on a cleared one.
                    s.noDepthFrames++;
                    if (s.noDepthFrames <= 8)
                    {
                        printf("tssm: c3d: stereo: frame %u did not clear the top screen's depth "
                               "before drawing (%s)\n",
                               (unsigned)s.frames, s.fillColorSet ? "colour only" : "no fill");
                        fflush(stdout);
                    }
                }
                // P1, P2: the left eye out.
                leaveEyeFree();
                splitHere(s.inBottom ? (uint8)OP_LIST_BOTTOM : (uint8)OP_LIST);
                if (s.drop != DROP_NONE)
                {
                    return false;
                }
                s.inBottom = false;
                C3D_FrameBufTransfer(&s.top->frameBuf, GFX_TOP, GFX_LEFT, s.transferFlags);
                s.queueEntries++;
                // C3D_FrameEnd must not send the right picture to the left too.
                s.top->used = false;
                if (cap)
                {
                    GX_DisplayTransfer((u32*)s.top->frameBuf.colorBuf, GX_BUFFER_DIM(240, 400),
                                       (u32*)s.capL, GX_BUFFER_DIM(240, 400),
                                       GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                                           GX_TRANSFER_RAW_COPY(0) |
                                           GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
                    s.queueEntries++;
                    s.capLValid = true;
                }
                // P3: the right eye's start. The target cleared by the same
                // GX fill the left eye's frame began with (a quad's depth is
                // not exactly the fill's 0: things at the far plane then
                // fail the depth test in one eye only), depth always (the
                // buffer is shared), colour as the frame's first fill did;
                // then, on the header that shares the target, its u_eyeSide.
                if (s.topSaved)
                {
                    // The frame did not clear colour: the left eye began on
                    // the picture before it, and so does the right one.
                    GX_TextureCopy((u32*)s.topSaveAt, 0, (u32*)s.top->frameBuf.colorBuf, 0,
                                   240 * 400 * 4, GX_TRANSFER_RAW_COPY(1));
                    s.queueEntries++;
                }
                C3D_RenderTargetClear(s.top,
                                      (C3D_ClearBits)(C3D_CLEAR_DEPTH |
                                                      (s.fillColorSet ? C3D_CLEAR_COLOR : 0)),
                                      s.fillColorSet ? s.fillColor : 0, 0);
                s.queueEntries++;
                drawOn(s.rightView);
                s.bound = s.rightView;
                s.viewport[0] = 0;
                s.viewport[1] = 0;
                s.viewport[2] = 240;
                s.viewport[3] = 400;
                (void)writeEyeSide(EYE_RIGHT);
                frameSplit(0);
                s.queueEntries++;
                // P4: the frame again, in its order (eyetest=noright: not).
                for (int32 i = 0; i < s.numOps && modes.eyeTest != EYETEST_NORIGHT; i++)
                {
                    const FrameOp& op = s.ops[i];
                    if (!op.replay)
                    {
                        if (op.kind == OP_LIST_EYEFREE)
                        {
                            s.eyeFreeLists++;
                            s.eyeFreeBytes += op.bytes;
                        }
                        continue;
                    }
                    if (op.kind == OP_COPY)
                    {
                        GX_TextureCopy(op.addr, op.inDim, op.dst, op.outDim, op.bytes, op.flags);
                    }
                    else
                    {
                        GX_ProcessCommandList(op.addr + op.skip / 4, op.bytes - op.skip, 0);
                        s.replayBytes += op.bytes - op.skip;
                    }
                    s.replayLists++;
                    s.queueEntries++;
                }
                s.twoEyeFrames++;
                return true;
            }
        } // namespace

        namespace
        {
            bool geoOn(void);
            void geoFrameEnd(void);
        } // namespace

        void frameEndWith(uint8 flags)
        {
            if (!s.inFrame)
            {
                return;
            }
            catchUp(nil); // the present waits for the frame before
            if (!modes.saveVram && s.topSaveV != nil)
            {
                topSaveRelease(0);
            }
            if (s.alphaHold)
            {
                s.alphaHold = false;
                s.aMiss++;
                alphaGiveUp();
            }
            if (s.screenDrawn && !s.fillColorSet)
            {
                s.aNoFill++;
            }
            s.alphaPredict = s.alphaNext;
            if (s.pipe && (s.frames % 900) == 899)
            {
                printf("tssm: c3d: pipelining: %u of the last 900 frames recorded ahead of the "
                       "GPU; %u caught up before their present since the open\n",
                       (unsigned)s.aheadFrames, (unsigned)s.aheadEarly);
                fflush(stdout);
                s.aheadFrames = 0;
            }

            bool two = false;
            u64 replayDr = 0;
            if (s.stereo)
            {
                if (s.drop == DROP_NONE && s.eyeDraws == 0)
                {
                    s.drop = DROP_FLAT;
                }
                // The right eye's CPU time is recording time (the stats line
                // has it apart too).
                const u64 r0 = svcGetSystemTick();
                two = s.drop == DROP_NONE && presentRight();
                const u64 dr = svcGetSystemTick() - r0;
                s.replayTicks += dr;
                replayDr = dr;
                if (s.recordOn)
                {
                    s.cur.t[TIME_RECORD] += dr;
                }
            }
            // Live use of the command buffer, before FrameEnd hands it over.
            const uint32 used = cmdUsedBytes();
            if (used > s.cmdPeak)
            {
                s.cmdPeak = used;
            }
            if (modes.gpuSplit && s.piecePass >= 0)
            {
                // The last piece timed apart: what C3D_FrameEnd sends then is
                // the display transfers (timed at the next frame's start).
                if (s.pieceWork)
                {
                    timedPiece(false);
                }
                s.presentPending = true;
                if (s.splitSeqState == 1)
                {
                    s.splitSeqState = 2;
                    printf("tssm: c3d: gpusplit: one frame's pieces in order (ms of the GPU): %s; "
                           "then the present\n",
                           s.splitSeq);
                    fflush(stdout);
                    abLog("gpusplit: one frame's pieces in order (ms of the GPU): %s; then the "
                          "present",
                          s.splitSeq);
                }
            }
            if (modes.cmdFlush && flags == 0 && !s.spilled)
            {
                // L2: only what the frame wrote for the GPU is flushed, not
                // the whole linear heap. With GX_CMDLIST_FLUSH the GSP flushes
                // the frame's last list only; the lists an in-frame screen copy
                // split off (C3Di_SplitFrame) are queued unflushed, so the
                // command buffer from its start is flushed here. Every other
                // in-frame write flushes itself (texCreate, the overlay,
                // instancing); a block spilled past the ring does not.
                if (s.cmdBase != nil && used > 0)
                {
                    GSPGPU_FlushDataCache(s.cmdBase, used);
                }
                ringFlush();
                flags = GX_CMDLIST_FLUSH;
            }
            if (s.timing)
            {
                // This frame's counts, for the segments (c3dab.cpp).
                memcpy(s.cur.counts, s.count, sizeof(s.cur.counts));
                s.cur.draws = s.drawsThisFrame;
                s.cur.cmdBytes = used;
                s.cur.ringBytes = s.ringUsed;
                s.cur.queue = s.queueEntries + 2;
                s.cur.splits = s.hardSplits - s.splitsAtFrameStart;
                s.cur.t[TIME_BONES] = s.skinTicks;
            }
            const u64 t0 = s.timing ? svcGetSystemTick() : 0;
            const float gpuBefore = C3D_GetDrawingTime();
            C3D_FrameEnd(flags);
            if (s.stereoReady)
            {
                // The settings menu's 3D mode and depth, between two frames.
                stereoApplySettings();
            }
            if (s.stereoReady && stereoEnabled())
            {
                // The guard's figures, then whether the next frame is a 3D one
                // (the slider, once a frame).
                const u64 now = svcGetSystemTick();
                const u64 wall = s.stLastPresent != 0 ? now - s.stLastPresent : 0;
                const u64 busy = wall > s.frameWait ? wall - s.frameWait : 0;
                stereoFrameDone(s.stereo, s.drop, wall, busy,
                                s.recordOn ? s.cur.t[TIME_RECORD] : 0, replayDr, gpuBefore,
                                s.pipe && modes.pipe && !modes.gpuSplit);
                s.stLastPresent = now;
                if (two && s.queueEntries + 2 > s.queuePeak3D)
                {
                    s.queuePeak3D = s.queueEntries + 2;
                }
                if (two)
                {
                    stereoProbeQueue(s.queueEntries + 2);
                }
                // gpusplit (probe 14) is 2D only: its pieces end the frame's
                // lists apart and wait, which the right eye's replay cannot
                // follow (stereoFrameStart says no too).
                s.nextStereo = stereoFrameStart() && s.rightView != nil && !modes.gpuSplit;
            }
            else
            {
                s.nextStereo = false;
            }
            s.lastStereo = two;
            s.stereoPrev = s.stereo;
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
            else
            {
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
            if (geoOn())
            {
                geoFrameEnd();
            }
            s.frames++;
        }

        void bindScreen(void)
        {
            frameEnsure();
            if (s.bound != s.top)
            {
                leaveEyeFree();
                leaveBottom();
                // C3D_FrameDrawOn sets the whole target as the viewport.
                drawOn(s.top);
                s.bound = s.top;
                s.viewport[0] = 0;
                s.viewport[1] = 0;
                s.viewport[2] = 240;
                s.viewport[3] = 400;
                s.count[COUNT_TARGET_BINDS]++;
                applyScissor();
            }
        }

        void bindTarget(C3D_RenderTarget* target, int32 w, int32 h)
        {
            frameEnsure();
            if (s.bound != target)
            {
                if (target != s.eyeFree)
                {
                    leaveEyeFree();
                }
                if (s.stereo)
                {
                    eyeFreeBound(target);
                }
                leaveBottom();
                // citro3d flushes the target before (when drawn on) and
                // invalidates the new one's cache.
                drawOn(target);
                s.bound = target;
                s.count[COUNT_TARGET_BINDS]++;
            }
            s.viewport[0] = 0;
            s.viewport[1] = 0;
            s.viewport[2] = w;
            s.viewport[3] = h;
            C3D_SetViewport(0, 0, (u32)w, (u32)h);
            applyScissor();
        }

        bool32 topAlphaDraw(bool32 writesAlpha, bool32 readsDstAlpha)
        {
            if (!(s.bound == nil || s.bound == s.top))
            {
                return 0;
            }
            if (readsDstAlpha && s.alphaHold)
            {
                // It would read the reset's alpha where it read the fill's:
                // from here as before.
                s.alphaHold = false;
                s.aEndRead++;
                alphaGiveUp();
            }
            if (!writesAlpha)
            {
                return 0;
            }
            if (s.alphaHold)
            {
                s.aHeld++;
                return 1;
            }
            s.topAlpha = -1;
            return 0;
        }

        bool32 alphaResetSkip(uint32 a)
        {
            if (!s.inFrame || !(s.bound == nil || s.bound == s.top))
            {
                s.resetFull = false;
                return 0;
            }
            s.aResets++;
            // The whole screen: the viewport all of the target, no scissor.
            s.resetFull = s.bound == nil ||
                          (s.viewport[0] == 0 && s.viewport[1] == 0 && s.viewport[2] == 240 &&
                           s.viewport[3] == 400 && !s.scissored);
            static bool told;
            if (!told)
            {
                told = true;
                printf("tssm: c3d: alphafill: the first glow alpha reset (frame %u): alpha %u, "
                       "viewport %d %d %d %d%s, the top screen's alpha %d, predicted %d\n",
                       (unsigned)s.frames, (unsigned)a, (int)s.viewport[0], (int)s.viewport[1],
                       (int)s.viewport[2], (int)s.viewport[3], s.scissored ? " (scissored)" : "",
                       (int)s.topAlpha, (int)s.alphaPredict);
                fflush(stdout);
            }
            if (!s.resetFull)
            {
                if (s.alphaHold)
                {
                    s.alphaHold = false; // outside it, the fill's alpha stays
                    s.aEndPart++;
                    alphaGiveUp();
                }
                return 0;
            }
            if (s.alphaNext < 0)
            {
                s.alphaNext = (int32)a; // the next frame's fill
            }
            s.alphaHold = false;
            if (modes.alphaFill && s.topAlpha == (int32)a)
            {
                s.aSkipped++;
                return 1;
            }
            return 0;
        }

        void alphaResetDrawn(uint32 a)
        {
            if (s.resetFull)
            {
                s.topAlpha = (int32)a;
            }
            s.resetFull = false;
        }

        void submitAlphaStats(void)
        {
            if (s.aResets > 0 || s.aFills > 0)
            {
                printf("tssm: c3d: alphafill=%s: glow alpha resets %u, left out %u; frames filled "
                       "with the reset's alpha %u; alpha writes held %u; holds ended early by a "
                       "read of the screen's alpha %u, a screen copy %u, a clear quad %u, a reset "
                       "of part of the screen %u; holds with no reset %u; frames with no colour fill %u\n",
                       modes.alphaFill ? "on" : "off", (unsigned)s.aResets, (unsigned)s.aSkipped,
                       (unsigned)s.aFills, (unsigned)s.aHeld, (unsigned)s.aEndRead,
                       (unsigned)s.aEndCopy, (unsigned)s.aEndClear, (unsigned)s.aEndPart,
                       (unsigned)s.aMiss, (unsigned)s.aNoFill);
                fflush(stdout);
            }
            s.aResets = s.aSkipped = s.aFills = s.aHeld = 0;
            s.aEndRead = s.aEndCopy = s.aEndClear = s.aEndPart = s.aMiss = s.aNoFill = 0;
        }

        void eyeFreeDepth(void)
        {
            if (s.eyeFree != nil && s.bound == s.eyeFree)
            {
                // The depth buffer as the left eye left it would be read: the
                // pass is sent again as any other.
                s.eyeFree = nil;
                s.eyeFreeBack++;
            }
        }

        void eyeFreeBegin(C3D_RenderTarget* target)
        {
            if (!modes.eyeSkip || target == nil)
            {
                return;
            }
            frameEnsure();
            if (!s.stereo || s.drop != DROP_NONE || s.eyeFree != nil || s.numBinds > kMaxEyeFree)
            {
                return;
            }
            // Only a target's first pass in the frame: one drawn on before
            // would read differently in the right eye. Its bind may have come
            // just before (the camera's update), with nothing drawn since.
            bool known = false;
            for (int32 i = 0; i < s.numBinds && i < kMaxEyeFree; i++)
            {
                const EyeFreeBind& b = s.binds[i];
                if (b.target == target &&
                    (b.binds > 1 || b.wasFree || s.bound != target || s.drawnSinceBind))
                {
                    return;
                }
                known = known || b.target == target;
            }
            if (!known && s.numBinds >= kMaxEyeFree)
            {
                return; // its bind would go unrecorded, so a later bind could not send it again
            }
            // The lists so far apart; this pass's list starts here (its bind
            // reaches the GPU with its first draw).
            if (s.bound != target)
            {
                leaveBottom();
            }
            splitHere(OP_LIST);
            if (s.drop == DROP_NONE)
            {
                s.eyeFree = target;
            }
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
            applyScissor();
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
                if (color)
                {
                    if (modes.alphaFill && s.alphaPredict >= 0 && !s.alphaOff)
                    {
                        // The alpha the glow's reset will give it: until then
                        // alpha writes into the top screen are held.
                        rgba = (rgba & ~0xFFu) | (uint32)s.alphaPredict;
                        s.alphaHold = true;
                        s.aFills++;
                    }
                    s.topAlpha = (int32)(rgba & 0xFFu);
                }
                targetFill(s.top, (uint32)bits, rgba);
                s.queueEntries++;
                s.pieceWork = true;
                if (color)
                {
                    // The right eye's prologue clears with it (presentRight).
                    s.fillColorSet = true;
                    s.fillColor = rgba;
                }
                if (depth)
                {
                    s.fillDepthSet = true;
                }
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
            if (s.bound == s.top && color)
            {
                if (s.alphaHold)
                {
                    s.alphaHold = false;
                    s.aEndClear++;
                    alphaGiveUp();
                }
                s.topAlpha = -1;
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
            s.pieceWork = true;
            s.drawnSinceBind = true;
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
                s.spilled = true;
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
                gpuFree(linearPtr);
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
                uint32* frames =
                    (uint32*)realloc(s.deadFrame, (size_t)cap * sizeof(uint32));
                if (more != nil)
                {
                    s.deadTargets = more;
                }
                if (frames != nil)
                {
                    s.deadFrame = frames;
                }
                if (more == nil || frames == nil)
                {
                    // Leaked rather than deleted while the GPU may draw into it.
                    logOnce("deadfull", "a render target could not be queued for deleting");
                    return;
                }
                s.deadCap = cap;
            }
            s.deadFrame[s.numDead] = readerFrame();
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
                s.count[COUNT_PROG_BINDS]++;
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
                s.count[COUNT_ATTR_SETS]++;
                // A missing normal is no light, a missing colour black, a
                // missing uv zero (the GL3 device's choices); its w is 1, as
                // the loader gives a uv of two (skintrim's uvout1 reads it).
                if (s.fixedIndex[format][1] >= 0)
                    C3D_FixedAttribSet(s.fixedIndex[format][1], 0.0f, 0.0f, 0.0f, 0.0f);
                if (s.fixedIndex[format][2] >= 0)
                    C3D_FixedAttribSet(s.fixedIndex[format][2], 0.0f, 0.0f, 0.0f, 255.0f);
                if (s.fixedIndex[format][3] >= 0)
                    C3D_FixedAttribSet(s.fixedIndex[format][3], 0.0f, 0.0f, 0.0f, 1.0f);
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
                s.count[COUNT_BUF_SETS]++;
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
            s.count[COUNT_UNIFORM_REGS] += (uint32)count;
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
            s.count[COUNT_UNIFORM_REGS]++;
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
            if (modes.gpuSplit && s.piecePass >= 0 && s.pieceWork)
            {
                // Timed as the piece it ends, so gpusplit loses none.
                timedPiece(ringToo);
                return;
            }
            splitWait(ringToo);
            s.pieceWork = false;
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
            // Stereo: the target as the frame found it, before its first draw
            // into the top screen (the commands so far go first, then a raw
            // GX copy of its tiles into topSave).
            void saveTopColour(void)
            {
                const uint32 bytes = 240 * 400 * 4;
                // savevram: VRAM to VRAM where there is room, leaving a margin
                // for textures (the same bytes either way).
                const uint32 kSaveMargin = 512u * 1024u;
                if (modes.saveVram && s.topSaveV == nil && s.frames >= s.topSaveVFrom &&
                    vramSpaceFree() >= bytes + kSaveMargin)
                {
                    s.topSaveV = (uint8*)vramAlloc(bytes);
                    if (s.topSaveV != nil)
                    {
                        printf("tssm: c3d: stereo: the starting picture's copy in VRAM (375 KB); "
                               "VRAM free %u KB\n",
                               (unsigned)(vramSpaceFree() >> 10));
                        fflush(stdout);
                    }
                }
                if (modes.saveVram && s.topSaveV != nil)
                {
                    if (s.queueEntries + 2 > 24)
                    {
                        s.drop = DROP_QUEUE;
                        return;
                    }
                    splitHere(OP_LIST);
                    textureCopy((u32*)s.top->frameBuf.colorBuf, 0, (u32*)s.topSaveV, 0, bytes,
                                GX_TRANSFER_RAW_COPY(1));
                    s.queueEntries++;
                    s.topSaveAt = s.topSaveV;
                    s.topSaved = true;
                    return;
                }
                if (s.topSave == nil)
                {
                    s.topSave = (uint8*)linearAlloc(bytes);
                    if (s.topSave == nil)
                    {
                        logOnce("topsave", "stereo: no linear memory to keep a frame's start; "
                                           "such frames stay one picture");
                        s.drop = DROP_ROOM;
                        return;
                    }
                }
                if (s.queueEntries + 2 > 24)
                {
                    s.drop = DROP_QUEUE;
                    return;
                }
                splitHere(OP_LIST);
                textureCopy((u32*)s.top->frameBuf.colorBuf, 0, (u32*)s.topSave, 0, bytes,
                            GX_TRANSFER_RAW_COPY(1));
                s.queueEntries++;
                s.topSaveAt = s.topSave;
                s.topSaved = true;
            }

            void noteTarget(void)
            {
                if (s.bound == nil)
                {
                    bindScreen();
                }
                s.drawnSinceBind = true;
                if (s.bound == s.top)
                {
                    if (!s.screenDrawn && s.stereo && !s.fillColorSet && s.drop == DROP_NONE &&
                        modes.eyeTest != EYETEST_NOSAVE)
                    {
                        saveTopColour();
                    }
                    s.screenDrawn = true;
                }
                else if (s.bound != s.bottom)
                {
                    s.count[COUNT_RTT_DRAWS]++;
                }
                if (modes.gpuSplit)
                {
                    passBoundary(s.bottom != nil && s.bound == s.bottom ? (int32)PASS_BOTTOM :
                                                                          devicePass());
                    s.pieceWork = true;
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

            // TSSM_C3D_GEOMSTAT=1 (speed3 counts, Azahar only): what the
            // indexed draws give the vertex shader, read back on the CPU.
            // Kinds: 0 the main camera with the eye shift, 1 the top screen
            // otherwise, 2 camera textures.
            struct GeoStat
            {
                double draws[NUM_PROGRAMS][3], idx[NUM_PROGRAMS][3], uniq[NUM_PROGRAMS][3];
                double fifo[3][NUM_PROGRAMS][3]; // misses of a FIFO cache of 8, 16, 32
                double tris[NUM_PROGRAMS][3], out[NUM_PROGRAMS][3]; // out: wholly past a plane
                double outL[NUM_PROGRAMS], outR[NUM_PROGRAMS], outBoth[NUM_PROGRAMS];
                double idxFar[NUM_PROGRAMS], idxNear[NUM_PROGRAMS], vNear[NUM_PROGRAMS];
                double light[5]; // lit programs' indices by lights with a colour
                double skinW[5]; // skinned unique vertices by weights over 0
                double skinMax[5]; // skinned indices by their draw's most weights
                double bigDraw[NUM_PROGRAMS]; // main: indices in draws of over 1500
                double drawOut[NUM_PROGRAMS], idxOut[NUM_PROGRAMS]; // main: draws wholly outside
                // Im2D on the top screen (0: HUD and 2D, 1: inside the glow): draws,
                // pixels covered, pixels of triangles whose vertices all have alpha 0,
                // draws of over 20000 pixels.
                double i2Draws[2], i2Px[2], i2Px0[2], i2Big[2];
                uint32 frames;
            };
            GeoStat* sGeo;
            int32 sGeoOn = -1;
            uint32* sGeoStamp;
            float* sGeoW;
            uint32 sGeoGen;
            uint32 sGeoFrom;

            bool geoOn(void)
            {
                if (sGeoOn < 0)
                {
                    // 1: from the start; a frame number: from that frame.
                    const char* e = getenv("TSSM_C3D_GEOMSTAT");
                    sGeoOn = e != nil && e[0] >= '1' && e[0] <= '9';
                    sGeoFrom = sGeoOn ? (uint32)atoi(e) : 0;
                    sGeoFrom = sGeoFrom == 1 ? 0 : sGeoFrom;
                    if (sGeoOn)
                    {
                        sGeo = (GeoStat*)calloc(1, sizeof(GeoStat));
                        sGeoStamp = (uint32*)calloc(65536, sizeof(uint32));
                        sGeoW = (float*)calloc(65536 * 4, sizeof(float));
                        if (sGeo == nil || sGeoStamp == nil || sGeoW == nil)
                        {
                            sGeoOn = 0;
                        }
                    }
                }
                return sGeoOn != 0 && s.frames >= sGeoFrom;
            }

            float geoDot(const C3D_FVec& r, const float* p)
            {
                return r.x * p[0] + r.y * p[1] + r.z * p[2] + r.w * p[3];
            }

            // clip (x, y, z, w) of one vertex; false when it cannot be had.
            bool geoClip(const uint8* v, bool skin, uint32 offSkin, float* c)
            {
                const float* pos = (const float*)v;
                float p[4] = {pos[0], pos[1], pos[2], 1.0f};
                if (skin)
                {
                    const uint8* sl = v + offSkin;
                    const uint8* wt = sl + 4;
                    float q[3] = {0, 0, 0};
                    for (int i = 0; i < 4; i++)
                    {
                        if (wt[i] == 0)
                            continue;
                        const int32 r = REG_BONES + sl[i];
                        if (r + 2 >= NUM_REGS || !s.shadowValid[r] || !s.shadowValid[r + 1] ||
                            !s.shadowValid[r + 2])
                            return false;
                        const float f = wt[i] / 255.0f;
                        for (int k = 0; k < 3; k++)
                            q[k] += f * geoDot(s.shadow[r + k], p);
                    }
                    p[0] = q[0];
                    p[1] = q[1];
                    p[2] = q[2];
                }
                for (int k = 0; k < 4; k++)
                {
                    if (!s.shadowValid[REG_MVP + k])
                        return false;
                    c[k] = geoDot(s.shadow[REG_MVP + k], p);
                }
                return true;
            }

            void geoDraw(uint32 prim, int32 count, const uint16* ix)
            {
                const int32 pg = s.program;
                if (pg < 0 || pg >= NUM_PROGRAMS || ix == nil || count <= 0 || s.buffer == nil)
                    return;
                const int32 kind = (s.eyeBool && s.bound == s.top) ? 0 : (s.bound == s.top ? 1 : 2);
                GeoStat* g = sGeo;
                g->draws[pg][kind] += 1;
                g->idx[pg][kind] += count;
                const bool model = s.format >= 0 && s.format < NUM_MODEL_FORMATS;
                const bool skin = model && (s.format & MODEL_SKIN);
                const uint8* vb = (const uint8*)s.buffer;
                const uint32 stride = s.bufferStride;
                const uint32 offSkin = model ? modelStride((uint32)s.format) - 8 : 0;
                if (++sGeoGen == 0)
                {
                    memset(sGeoStamp, 0, 65536 * sizeof(uint32));
                    sGeoGen = 1;
                }
                const float wmin = s.shadowValid[REG_EYECAM] ? s.shadow[REG_EYECAM].y : 0.0f;
                const float cC = s.shadowValid[REG_EYECAM] ? s.shadow[REG_EYECAM].x : 0.0f;
                const float sideL = stereoEyeSide(EYE_LEFT);
                const float sideR = stereoEyeSide(EYE_RIGHT);
                // Unique vertices, their clip coordinates, the FIFO caches.
                uint16 f8[8], f16[16], f32[32];
                int n8 = 0, n16 = 0, n32 = 0, h8 = 0, h16 = 0, h32 = 0;
                uint32 uniq = 0, nNear = 0, maxW = 0;
                bool clipOk = model;
                for (int32 i = 0; i < count; i++)
                {
                    const uint16 x = ix[i];
                    bool hit = false;
                    for (int k = 0; k < n8 && !hit; k++)
                        hit = f8[k] == x;
                    if (!hit)
                    {
                        g->fifo[0][pg][kind] += 1;
                        f8[h8] = x;
                        h8 = (h8 + 1) & 7;
                        if (n8 < 8)
                            n8++;
                    }
                    hit = false;
                    for (int k = 0; k < n16 && !hit; k++)
                        hit = f16[k] == x;
                    if (!hit)
                    {
                        g->fifo[1][pg][kind] += 1;
                        f16[h16] = x;
                        h16 = (h16 + 1) & 15;
                        if (n16 < 16)
                            n16++;
                    }
                    hit = false;
                    for (int k = 0; k < n32 && !hit; k++)
                        hit = f32[k] == x;
                    if (!hit)
                    {
                        g->fifo[2][pg][kind] += 1;
                        f32[h32] = x;
                        h32 = (h32 + 1) & 31;
                        if (n32 < 32)
                            n32++;
                    }
                    if (sGeoStamp[x] == sGeoGen)
                        continue;
                    sGeoStamp[x] = sGeoGen;
                    uniq++;
                    const uint8* v = vb + (size_t)x * stride;
                    if (skin)
                    {
                        const uint8* wt = v + offSkin + 4;
                        uint32 nw = (wt[0] != 0) + (wt[1] != 0) + (wt[2] != 0) + (wt[3] != 0);
                        g->skinW[nw] += 1;
                        if (nw > maxW)
                            maxW = nw;
                    }
                    if (clipOk)
                    {
                        float* c = &sGeoW[(size_t)x * 4];
                        if (!geoClip(v, skin, offSkin, c))
                            clipOk = false;
                        else if (kind == 0 && c[3] < wmin)
                            nNear++;
                    }
                }
                g->uniq[pg][kind] += uniq;
                if (skin)
                    g->skinMax[maxW] += count;
                if (pg == PROG_LIT || pg == PROG_ENV || pg == PROG_SKIN || pg == PROG_SKINENV)
                {
                    int nl = 0;
                    for (int k = 0; k < 4; k++)
                    {
                        const C3D_FVec& lc = s.shadow[REG_LIGHTCOL + k];
                        if (s.shadowValid[REG_LIGHTCOL + k] && (lc.x != 0 || lc.y != 0 || lc.z != 0))
                            nl++;
                    }
                    g->light[nl] += count;
                }
                if (kind == 0)
                {
                    if (count > 1500)
                        g->bigDraw[pg] += count;
                    if (clipOk)
                    {
                        if (nNear == 0)
                            g->idxFar[pg] += count;
                        else
                            g->idxNear[pg] += count;
                        g->vNear[pg] += nNear;
                    }
                }
                // Triangles: lists and strips.
                const bool strip = prim == GPU_TRIANGLE_STRIP;
                if (prim != GPU_TRIANGLES && !strip)
                    return;
                const int32 nt = strip ? count - 2 : count / 3;
                int32 seen = 0, outN = 0;
                for (int32 t = 0; t < nt; t++)
                {
                    const uint16 a = strip ? ix[t] : ix[3 * t];
                    const uint16 b = strip ? ix[t + 1] : ix[3 * t + 1];
                    const uint16 c = strip ? ix[t + 2] : ix[3 * t + 2];
                    if (strip && (a == b || b == c || a == c))
                        continue;
                    g->tris[pg][kind] += 1;
                    if (!clipOk)
                        continue;
                    const float* p[3] = {&sGeoW[(size_t)a * 4], &sGeoW[(size_t)b * 4],
                                         &sGeoW[(size_t)c * 4]};
                    // Wholly past one plane: x, y in [-w, w], z in [-w, 0].
                    int m[6] = {0, 0, 0, 0, 0, 0};
                    int mL[2] = {0, 0}, mR[2] = {0, 0};
                    for (int k = 0; k < 3; k++)
                    {
                        const float x = p[k][0], y = p[k][1], z = p[k][2], w = p[k][3];
                        m[0] += x > w;
                        m[1] += x < -w;
                        m[2] += y > w;
                        m[3] += y < -w;
                        m[4] += z > 0;
                        m[5] += z < -w;
                        if (kind == 0)
                        {
                            const float we = w > wmin ? w : wmin;
                            const float f = we != 0 ? w * (1.0f - cC / we) : 0.0f;
                            const float yl = y + sideL * f, yr = y + sideR * f;
                            mL[0] += yl > w;
                            mL[1] += yl < -w;
                            mR[0] += yr > w;
                            mR[1] += yr < -w;
                        }
                    }
                    const bool o = m[0] == 3 || m[1] == 3 || m[2] == 3 || m[3] == 3 || m[4] == 3 ||
                                   m[5] == 3;
                    seen++;
                    outN += o;
                    if (o)
                        g->out[pg][kind] += 1;
                    if (kind == 0)
                    {
                        const bool oxz = m[0] == 3 || m[1] == 3 || m[4] == 3 || m[5] == 3;
                        const bool oL = oxz || mL[0] == 3 || mL[1] == 3;
                        const bool oR = oxz || mR[0] == 3 || mR[1] == 3;
                        g->outL[pg] += oL;
                        g->outR[pg] += oR;
                        g->outBoth[pg] += oL && oR;
                    }
                }
                if (kind == 0 && clipOk && seen > 0 && outN == seen)
                {
                    g->drawOut[pg] += 1;
                    g->idxOut[pg] += count;
                }
            }

            void geoIm2D(uint32 prim, int32 first, int32 count)
            {
                if (s.program != PROG_IM2D || s.bound != s.top || s.buffer == nil || count < 3)
                    return;
                GeoStat* g = sGeo;
                const int k = devicePass() == PASS_GLOW ? 1 : 0;
                const uint8* vb = (const uint8*)s.buffer + (size_t)first * s.bufferStride;
                const uint32 st = s.bufferStride;
                double px = 0, px0 = 0;
                const bool strip = prim == GPU_TRIANGLE_STRIP, fan = prim == GPU_TRIANGLE_FAN;
                const int32 nt = (strip || fan) ? count - 2 : count / 3;
                for (int32 i = 0; i < nt; i++)
                {
                    const int32 a = fan ? 0 : (strip ? i : 3 * i);
                    const int32 b = fan ? i + 1 : (strip ? i + 1 : 3 * i + 1);
                    const int32 c = fan ? i + 2 : (strip ? i + 2 : 3 * i + 2);
                    const float* pa = (const float*)(vb + (size_t)a * st);
                    const float* pb = (const float*)(vb + (size_t)b * st);
                    const float* pc = (const float*)(vb + (size_t)c * st);
                    double ar = 0.5 * ((pb[0] - pa[0]) * (pc[1] - pa[1]) -
                                       (pc[0] - pa[0]) * (pb[1] - pa[1]));
                    ar = ar < 0 ? -ar : ar;
                    px += ar;
                    if (vb[(size_t)a * st + 19] == 0 && vb[(size_t)b * st + 19] == 0 &&
                        vb[(size_t)c * st + 19] == 0)
                        px0 += ar;
                }
                static int logged = 0;
                if (px > 20000 && logged < 8)
                {
                    logged++;
                    char d[256];
                    deviceDescribe(d, sizeof(d));
                    printf("tssm: c3d: geomstat: big im2d draw (%s) at frame %u: %.0f px, %d "
                           "vertices, prim %#x, first vertex x %.1f y %.1f z %.4f, bgra %u %u %u %u; "
                           "%s\n",
                           k ? "glow" : "HUD and 2D", (unsigned)s.frames, px, (int)count,
                           (unsigned)prim, ((const float*)vb)[0], ((const float*)vb)[1],
                           ((const float*)vb)[2], vb[16], vb[17], vb[18], vb[19], d);
                    fflush(stdout);
                }
                g->i2Draws[k] += 1;
                g->i2Px[k] += px;
                g->i2Px0[k] += px0;
                g->i2Big[k] += px > 20000;
            }

            void geoFrameEnd(void)
            {
                GeoStat* g = sGeo;
                g->frames++;
                if (g->frames < 120)
                    return;
                const double n = g->frames;
                static const char* names[NUM_PROGRAMS] = {"world", "lit",  "im2d",   "im3d",
                                                          "skin",  "env", "skinenv"};
                static const char* kinds[3] = {"main", "top", "rtt"};
                printf("tssm: c3d: geomstat: %u frames (frame %u), a frame:\n", (unsigned)g->frames,
                       (unsigned)s.frames);
                for (int pg = 0; pg < NUM_PROGRAMS; pg++)
                    for (int k = 0; k < 3; k++)
                    {
                        if (g->draws[pg][k] == 0)
                            continue;
                        printf("tssm: c3d: geomstat:   %s %s: draws %.1f, indices %.0f, unique "
                               "%.0f, fifo8/16/32 misses %.0f/%.0f/%.0f, triangles %.0f, wholly "
                               "outside %.0f\n",
                               names[pg], kinds[k], g->draws[pg][k] / n, g->idx[pg][k] / n,
                               g->uniq[pg][k] / n, g->fifo[0][pg][k] / n, g->fifo[1][pg][k] / n,
                               g->fifo[2][pg][k] / n, g->tris[pg][k] / n, g->out[pg][k] / n);
                    }
                for (int pg = 0; pg < NUM_PROGRAMS; pg++)
                {
                    if (g->draws[pg][0] == 0)
                        continue;
                    printf("tssm: c3d: geomstat:   %s main: indices in draws all past w_min %.0f, "
                           "with a nearer vertex %.0f (nearer unique vertices %.0f); triangles "
                           "outside the left eye %.0f, the right %.0f, both %.0f; indices in draws "
                           "over 1500 %.0f; draws wholly outside %.1f (%.0f indices)\n",
                           names[pg], g->idxFar[pg] / n, g->idxNear[pg] / n, g->vNear[pg] / n,
                           g->outL[pg] / n, g->outR[pg] / n, g->outBoth[pg] / n, g->bigDraw[pg] / n,
                           g->drawOut[pg] / n, g->idxOut[pg] / n);
                }
                printf("tssm: c3d: geomstat:   im2d on the top screen: HUD and 2D %.1f draws, %.0f "
                       "pixels (%.0f of them alpha 0), %.1f draws over 20000 pixels; inside the "
                       "glow %.1f draws, %.0f pixels (%.0f alpha 0), %.1f over 20000\n",
                       g->i2Draws[0] / n, g->i2Px[0] / n, g->i2Px0[0] / n, g->i2Big[0] / n,
                       g->i2Draws[1] / n, g->i2Px[1] / n, g->i2Px0[1] / n, g->i2Big[1] / n);
                printf("tssm: c3d: geomstat:   lit indices by lights 0/1/2/3/4: %.0f/%.0f/%.0f/%.0f/"
                       "%.0f; skinned vertices by weights 0..4: %.0f/%.0f/%.0f/%.0f/%.0f; skinned "
                       "indices by their draw's most weights: %.0f/%.0f/%.0f/%.0f/%.0f\n",
                       g->light[0] / n, g->light[1] / n, g->light[2] / n, g->light[3] / n,
                       g->light[4] / n, g->skinW[0] / n, g->skinW[1] / n, g->skinW[2] / n,
                       g->skinW[3] / n, g->skinW[4] / n, g->skinMax[0] / n, g->skinMax[1] / n,
                       g->skinMax[2] / n, g->skinMax[3] / n, g->skinMax[4] / n);
                fflush(stdout);
                memset(g, 0, sizeof(*g));
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
            if (geoOn())
            {
                geoIm2D(prim, first, count);
            }
            C3D_DrawArrays((GPU_Primitive_t)prim, first, count);
            s.deferredDrawn = s.numDeferred;
            if (s.eyeWant && s.bound == s.top)
            {
                s.eyeDraws++;
            }
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
            if (geoOn())
            {
                geoDraw(prim, count, indices);
            }
            C3D_DrawElements((GPU_Primitive_t)prim, count, C3D_UNSIGNED_SHORT, indices);
            s.deferredDrawn = s.numDeferred;
            if (s.eyeWant && s.bound == s.top)
            {
                s.eyeDraws++;
            }
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
            if (vram && s.topSaveV != nil)
            {
                // A load: its textures come first. Outside a frame; the GPU
                // is let finish what may still read it.
                topSaveRelease(300); // after the load's textures, ten seconds on
            }
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
            if (!made && topSaveRelease(300))
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
            if (natras->target == nil && topSaveRelease(300))
            {
                // Its depth buffer found VRAM short: once more with the block's.
                natras->target = C3D_RenderTargetCreateFromTex(
                    &natras->tex, GPU_TEXFACE_2D, 0,
                    depth ? C3D_DEPTHTYPE(GPU_RB_DEPTH16) : C3D_DEPTHTYPE(-1));
            }
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
            s.unit2 = &s.ramp;
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
            s.unit2 = d;
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
                    drawOn(was);
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
            u32* const from = (u32*)s.top->frameBuf.colorBuf;
            u32* const to = (u32*)((uint8*)natras->tex.data + kCopySkip);
            const u32 inDim = GX_BUFFER_DIM(480, 0);
            const u32 outDim = GX_BUFFER_DIM(480, 32);
            const u32 copyFlags = GX_TRANSFER_RAW_COPY(1) | kTransferGaps;
            if (inside)
            {
                passBoundary(PASS_COPY);
            }
            if (inside && s.alphaHold)
            {
                // The copy has the top screen's alpha: from here as before.
                s.alphaHold = false;
                s.aEndCopy++;
                alphaGiveUp();
            }
            const bool noted = inside && s.stereo;
            if (noted)
            {
                // The list before it, apart (citro3d's own split in the copy
                // then has nothing left to send).
                splitHere(OP_LIST);
            }
            if (inside)
            {
                // citro3d's C3D_SyncTextureCopy inside a frame: a split, then
                // the copy (kept while the frame is ahead).
                frameSplit(0);
                textureCopy(from, inDim, to, outDim, 240 * 400 * 4, copyFlags);
            }
            else
            {
                C3D_SyncTextureCopy(from, inDim, to, outDim, 240 * 400 * 4, copyFlags);
            }
            if (inside)
            {
                s.queueEntries += noted ? 1 : 2;
                s.count[COUNT_COPIES]++;
                s.pieceWork = true;
            }
            if (noted)
            {
                if (s.numOps < kMaxOps)
                {
                    // The passes' copies again for the right eye, after the
                    // same list; the loading still keeps the left eye's.
                    FrameOp* op = &s.ops[s.numOps++];
                    memset(op, 0, sizeof(*op));
                    op->kind = OP_COPY;
                    op->replay = own ? 0 : 1;
                    op->addr = from;
                    op->bytes = 240 * 400 * 4;
                    op->dst = to;
                    op->inDim = inDim;
                    op->outDim = outDim;
                    op->flags = copyFlags;
                }
                else
                {
                    s.drop = DROP_QUEUE;
                }
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
            frameCatchUp("a screen output changed");
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
                leaveEyeFree();
                if (s.stereo && !s.inBottom)
                {
                    // The top's lists so far apart: the bottom's is drawn once.
                    splitHere(OP_LIST);
                    s.inBottom = true;
                }
                drawOn(s.bottom);
                s.bound = s.bottom;
                s.count[COUNT_TARGET_BINDS]++;
            }
            s.viewport[0] = 0;
            s.viewport[1] = 0;
            s.viewport[2] = 240;
            s.viewport[3] = 320;
            C3D_SetViewport(0, 0, 240, 320);
            applyScissor();
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
            if (wasInFrame)
            {
                catchUp("a read-back");
            }
            C3D_RenderTarget* bound = s.bound;
            if (wasInFrame)
            {
                if (s.stereo)
                {
                    s.drop = DROP_READBACK;
                    s.numOps = 0;
                }
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
                if (s.pipe && s.cmdBufs[0] != nil)
                {
                    GPUCMD_SetBuffer(s.cmdBufs[s.frames & 1], ((s.cmdBytes + 0xF) & ~0xFu) / 4,
                                     0);
                    u32 sz = 0;
                    u32 o = 0;
                    GPUCMD_GetBuffer(&s.cmdBase, &sz, &o);
                }
                s.inFrame = true;
                attachOutputs();
                s.queueEntries = 0;
                freeDeferredDrawn();
                s.bound = nil;
                if (bound != nil)
                {
                    drawOn(bound);
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
            if (s.readLeft && s.capLValid && s.capL != nil)
            {
                // The left eye, kept by its transfer in the frame (presentRight).
                gpuIdle();
                GSPGPU_InvalidateDataCache(s.capL, bytes);
                for (int y = 0; y < SCREEN_H; y++)
                {
                    for (int x = 0; x < SCREEN_W; x++)
                    {
                        const uint8* p = s.capL + 4 * (x * 240 + (239 - y));
                        uint8* d = rgba + 4 * (y * SCREEN_W + x);
                        d[0] = p[3];
                        d[1] = p[2];
                        d[2] = p[1];
                        d[3] = 255;
                    }
                }
                return 1;
            }
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
            on = false;
            if (s.recordOn && s.recordDepth++ == 0)
            {
                on = true;
                s.cur.reads += 2;
                t0 = svcGetSystemTick();
                w0 = s.waitTotal;
            }
            else if (s.recordOn)
            {
                on = true; // an inner one: counted by the outer
            }
        }

        RecordScope::~RecordScope(void)
        {
            if (on && --s.recordDepth == 0)
            {
                const u64 spent = svcGetSystemTick() - t0;
                const u64 waited = s.waitTotal - w0;
                s.cur.t[TIME_RECORD] += spent > waited ? spent - waited : 0;
            }
        }

        ProfScope::ProfScope(int32 w)
        {
            which = -1;
            t0 = 0;
            w0 = 0;
            if (modes.profile && s.timing)
            {
                which = w;
                s.cur.reads += 2;
                s.cur.profReads += 2;
                t0 = svcGetSystemTick();
                w0 = s.waitTotal;
            }
        }

        ProfScope::~ProfScope(void)
        {
            if (which >= 0)
            {
                const u64 spent = svcGetSystemTick() - t0;
                const u64 waited = s.waitTotal - w0;
                s.cur.prof[which] += spent > waited ? spent - waited : 0;
            }
        }

        void noteProf(int32 which, u64 ticks)
        {
            if (modes.profile && s.timing)
            {
                s.cur.prof[which] += ticks;
            }
        }

        namespace
        {
            // TSSM_C3D_VBVRAM=on: the geometry made since the last present,
            // waiting to move (vbVramMigrate).
            InstanceDataHeader** sVbQueue;
            int32 sVbNum;
            int32 sVbCap;
            int sVbOn = -1;
            uint32 sVbMoved, sVbMovedBytes, sVbStayed, sVbStayedBytes;
            // Kept free for camera textures, copy blocks and what a level
            // loads later.
            const uint32 kVbMargin = 256u * 1024u;
        } // namespace

        bool32 vbVramOn(void)
        {
            if (sVbOn < 0)
            {
                const char* e = getenv("TSSM_C3D_VBVRAM");
                sVbOn = e != nil && (strcmp(e, "on") == 0 || strcmp(e, "1") == 0);
                if (sVbOn)
                {
                    printf("tssm: c3d: TSSM_C3D_VBVRAM=on: models' vertex buffers move into VRAM "
                           "after the frame they were made in, while %u KB of VRAM stay free\n",
                           (unsigned)(kVbMargin >> 10));
                    fflush(stdout);
                }
            }
            return sVbOn;
        }

        void vbVramQueue(InstanceDataHeader* h)
        {
            if (h == nil || !vbVramOn() || h->vertexBuffer == nil || h->totalNumVertex == 0)
            {
                return;
            }
            if (sVbNum >= sVbCap)
            {
                const int32 cap = sVbCap > 0 ? sVbCap * 2 : 256;
                InstanceDataHeader** more = (InstanceDataHeader**)realloc(
                    sVbQueue, (size_t)cap * sizeof(InstanceDataHeader*));
                if (more == nil)
                {
                    return;
                }
                sVbQueue = more;
                sVbCap = cap;
            }
            sVbQueue[sVbNum++] = h;
        }

        void vbVramStats(uint32* movedKB, uint32* stayedKB)
        {
            *movedKB = sVbMovedBytes >> 10;
            *stayedKB = sVbStayedBytes >> 10;
        }

        void vbVramForget(InstanceDataHeader* h)
        {
            for (int32 i = 0; i < sVbNum; i++)
            {
                if (sVbQueue[i] == h)
                {
                    sVbQueue[i] = sVbQueue[--sVbNum];
                    return;
                }
            }
        }

        void vbVramMigrate(void)
        {
            if (sVbNum == 0 || !s.open || s.inFrame)
            {
                return;
            }
            // Each copy waits for the queue before it (the frame just
            // presented, still drawing from the old buffers); the GPU is
            // waited for once more after the last, then the old ones go.
            void* old[64];
            int32 nold = 0;
            uint32 moved = 0, movedBytes = 0;
            for (int32 i = 0; i < sVbNum; i++)
            {
                InstanceDataHeader* h = sVbQueue[i];
                const uint32 bytes = ((uint32)h->totalNumVertex * h->stride + 15u) & ~15u;
                void* v = nil;
                if (nold < 64 && vramSpaceFree() >= bytes + kVbMargin)
                {
                    v = vramAlloc(bytes);
                }
                if (v == nil)
                {
                    sVbStayed++;
                    sVbStayedBytes += bytes;
                    continue;
                }
                C3D_SyncTextureCopy((u32*)h->vertexBuffer, 0, (u32*)v, 0, bytes,
                                    GX_TRANSFER_RAW_COPY(1));
                old[nold++] = h->vertexBuffer;
                h->vertexBuffer = (uint8*)v;
                moved++;
                movedBytes += bytes;
                if (nold == 64)
                {
                    // In batches of 64.
                    gpuIdle();
                    for (int32 k = 0; k < nold; k++)
                    {
                        linearFree(old[k]);
                    }
                    nold = 0;
                }
            }
            if (nold > 0)
            {
                gpuIdle();
                for (int32 k = 0; k < nold; k++)
                {
                    linearFree(old[k]);
                }
            }
            sVbNum = 0;
            sVbMoved += moved;
            sVbMovedBytes += movedBytes;
            if (moved > 0)
            {
                printf("tssm: c3d: vbvram: %u vertex buffers (%u KB) moved into VRAM; so far %u "
                       "(%u KB) moved, %u (%u KB) left in linear memory; VRAM free %u KB\n",
                       (unsigned)moved, (unsigned)(movedBytes >> 10), (unsigned)sVbMoved,
                       (unsigned)(sVbMovedBytes >> 10), (unsigned)sVbStayed,
                       (unsigned)(sVbStayedBytes >> 10),
                       (unsigned)(vramSpaceFree() >> 10));
                fflush(stdout);
                // The device's buffer filter may hold an old pointer.
                s.buffer = nil;
            }
        }

        void gpuFree(void* p)
        {
            if (p == nil)
            {
                return;
            }
            // VRAM's virtual range (libctru's vramAlloc).
            const uintptr_t a = (uintptr_t)p;
            if (a >= 0x1F000000u && a < 0x1F600000u)
            {
                vramFree(p);
            }
            else
            {
                linearFree(p);
            }
        }

        void getMemoryStats(Stats* out)
        {
            memset(out, 0, sizeof(*out));
            out->textures = s.textures;
            out->texVram = s.texVram;
            out->texLinear = s.texLinear;
            out->targets = s.targets;
            out->targetVram = s.targetVram;
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

        void setEyeShift(bool32 on)
        {
            s.eyeWant = on && frameIsStereo();
            // eyetest: 2d draws a 2D frame's main camera through the eye code
            // (u_eyeSide 0); noshift draws a 3D frame's without it.
            const bool want = modes.eyeTest == EYETEST_2D ? on != 0 :
                              modes.eyeTest == EYETEST_NOSHIFT ? false :
                                                                 s.eyeWant;
            if (want != s.eyeBool)
            {
                C3D_BoolUnifSet(GPU_VERTEX_SHADER, REG_BEYE, want);
                s.eyeBool = want;
            }
        }

        bool32 frameIsStereo(void)
        {
            return s.inFrame ? s.stereo : s.nextStereo;
        }

        int frameWasStereo(void)
        {
            return s.lastStereo ? 1 : 0;
        }

        void readScreenEye(int left)
        {
            s.readLeft = left != 0;
        }

        void submitStereoStats(void)
        {
            stereoStats(s.replayLists, s.replayBytes, s.queuePeak3D, s.replayTicks);
            if (s.twoEyeFrames > 0)
            {
                printf("tssm: c3d: stereo: eyeskip=%s: the right eye left out %.2f eye-free "
                       "lists a two-eye frame (%.1f KB); %u sent again (their target drawn on "
                       "later in the frame)\n",
                       modes.eyeSkip ? "on" : "off", (double)s.eyeFreeLists / s.twoEyeFrames,
                       (double)s.eyeFreeBytes / s.twoEyeFrames / 1024.0, (unsigned)s.eyeFreeBack);
                fflush(stdout);
            }
            s.eyeFreeLists = 0;
            s.eyeFreeBytes = 0;
            s.eyeFreeBack = 0;
            s.twoEyeFrames = 0;
            s.replayTicks = 0;
            s.replayLists = 0;
            s.replayBytes = 0;
            s.queuePeak3D = 0;
        }

        void shutdown(void)
        {
            submitClose();
        }
    } // namespace c3d
} // namespace rw
