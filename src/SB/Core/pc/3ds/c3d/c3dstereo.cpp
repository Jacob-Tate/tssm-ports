// rw::c3d: stereoscopic 3D (docs/3DS.md, "Stereoscopic 3D").
//
// What is decided here, once a frame: whether the frame is drawn for two eyes
// (the 3D slider, eased; [video] stereo and stereo_depth; TSSM_C3D_STEREO; the
// speed guard), the eye terms (stereomath.h), and the convergence plane C,
// which follows the player's camera z and is held while the camera does not
// follow the player. c3dsubmit.cpp records the frame's command lists and
// sends them to the GPU again for the right eye; the vertex shaders' eye
// procedure shifts the main camera's draws by u_eyeSide, which is written
// only in front of each eye's lists.
//
// The speed guard: a 3D frame costs the GPU about twice. When 3 of the last
// 30 3D frames missed their pair of vblanks (over 40 ms), or the GPU's mean
// went over 31 ms, 3D eases out over 8 frames and the frames are 2D; it comes
// back after a wait (3 s, doubled each time, at most 60 s, reset at a level
// load) once no more than 2 of the last 90 frames predict that a 3D frame
// would not fit. Frames over 250 ms (a load, a capture's file write, a
// hitch) are left out of both.

#include "c3dimpl.h"

#include "../../rw/stereomath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int iCaptureFrameCount();

namespace rw
{
    namespace c3d
    {
        namespace
        {
            const int32 kWindow = 30; // 3D frames the guard looks back over
            const int32 kFadeFrames = 8;
            const int32 kRoomFrames = 90;
            const int32 kRoomMisses = 2; // of kRoomFrames that may predict no room
            const float kHitchMs = 250.0f;
            const double kFirstWait = 3.0; // seconds
            const double kMaxWait = 60.0;
            const float kOverMs = 40.0f; // a missed pair of vblanks
            const float kGpuLimitMs = 31.0f;
            const float kFitMs = 31.0f;
            // 3D frames the guard leaves out after 3D starts or comes back:
            // the first ones carry the switch's own one-off work.
            const int32 kWarmFrames = 15;
            const float kSliderDead = 0.04f;
            const float kEase = 0.3f; // of the way to the slider each frame
            const float kCLo = 1.0f, kCHi = 50.0f; // the convergence plane's bounds
            const float kCNone = 8.0f; // before any gameplay
            const float kCHalfLife = 0.2f; // seconds

            enum
            {
                GUARD_ON, // 3D as the slider says
                GUARD_FADE, // tripped: easing out
                GUARD_2D // tripped: 2D until it predicts room
            };

            struct Stereo
            {
                bool open;
                int32 mode; // what modes.stereo was last frame
                float depth; // stereo_depth
                stereomath::Setting env; // TSSM_C3D_STEREO
                bool envSet;
                bool nearCap;
                bool guardOn;
                int32 guardTest; // TSSM_C3D_STEREO_GUARDTEST's frame (0: none)

                float s; // the slider, eased
                float sEff; // times the guard's fade
                bool frame3D;
                float k;
                float wMin;
                float C;
                bool cInit;
                bool cDone; // computed this frame
                float gameC; // the latest C that followed the player
                bool haveGameC;
                StereoFocusHook hook;
                Camera* focusCam;
                bool hasPlayer;
                float player[3];
                bool fixed;
                bool flat; // the game is in a menu: nothing is drawn per eye
                bool otherLogged;

                int32 guard;
                int32 fadeLeft;
                uint8 over[kWindow];
                float gpu[kWindow];
                int32 nWin, atWin;
                uint32 trips; // since the last level load
                uint32 tripsTotal;
                double wait;
                u64 tripAt; // osGetTime, ms
                u64 in2DFrom;
                double sec2D;
                uint8 roomOver[kRoomFrames]; // 1: that frame predicted no room
                int32 roomN, roomAt;
                float lastPredict;
                float replayMs; // the right eye's CPU a 3D frame, averaged
                int32 warm; // two-eye frames in a row
                uint32 frameNo; // frames since the open, for the log lines
                double g3, g2; // GPU ms sums, 3D and 2D frames
                uint32 n3, n2;
                bool prevWas3D;
                bool prevJudged; // the frame before went into the guard's window
                bool havePrev;

                // Since the last stats line.
                uint32 frames, frames3D;
                uint32 drops[NUM_DROPS];
                uint32 changes;
                bool lastLogged3D;
                bool captureLeft;

                // Probe 14 (stereoProbeSet): a forced segment's depth, the
                // guard as the setting had it, and the counts since the take.
                bool probeDepthSet;
                float probeDepth;
                bool guardSetting;
                StereoProbeStats probe;
            };

            Stereo st;

            // The capture's frame count where there is one (the probes' segment
            // labels use it), else the frames since the open.
            int logFrame(void)
            {
                const int f = iCaptureFrameCount();
                return f > 0 ? f : (int)st.frameNo;
            }

            const char* modeName(int32 m)
            {
                return m == stereomath::MODE_OFF   ? "off" :
                       m == stereomath::MODE_FORCE ? "force" :
                                                     "auto";
            }

            float depthNow(void)
            {
                if (modes.stereo == stereomath::MODE_FORCE && st.probeDepthSet)
                {
                    return st.probeDepth;
                }
                return modes.stereo == stereomath::MODE_FORCE && st.envSet && st.env.hasDepth ?
                           st.env.depth :
                           st.depth;
            }

            float farPx(float sEff)
            {
                return sEff * depthNow() / 100.0f * stereomath::kMaxParallaxPx;
            }

            double nowSec(void)
            {
                return (double)osGetTime() / 1000.0;
            }

            const char* dropName(int32 d)
            {
                static const char* kNames[] = { "none",     "hard split", "GX queue",
                                                "read-back", "flat",       "room" };
                return d >= 0 && d < NUM_DROPS ? kNames[d] : "?";
            }

            // The plane the eyes converge on, once a frame, at the main
            // camera's first beginUpdate.
            void computeC(Camera* cam)
            {
                float target;
                bool follows = false;
                if (modes.stereo == stereomath::MODE_FORCE && st.envSet && st.env.hasC)
                {
                    st.C = st.env.C;
                    st.cInit = true;
                    return;
                }
                if (!st.fixed && st.hasPlayer)
                {
                    Matrix* m = cam->getFrame()->getLTM();
                    const float dx = st.player[0] - m->pos.x;
                    const float dy = st.player[1] - m->pos.y;
                    const float dz = st.player[2] - m->pos.z;
                    target = m->at.x * dx + m->at.y * dy + m->at.z * dz;
                    target = target < kCLo ? kCLo : target > kCHi ? kCHi : target;
                    follows = true;
                }
                else
                {
                    target = st.haveGameC ? st.gameC : kCNone;
                }
                if (!st.cInit)
                {
                    st.C = target;
                    st.cInit = true;
                }
                else
                {
                    // A 0.2 s half-life at the 30 fps frame.
                    const float a = 1.0f - powf(0.5f, (1.0f / 30.0f) / kCHalfLife);
                    st.C += (target - st.C) * a;
                }
                if (follows)
                {
                    st.gameC = st.C;
                    st.haveGameC = true;
                }
            }

            void logChange(bool on)
            {
                st.changes++;
                if (st.changes > 24 && st.changes % 100 != 0)
                {
                    return;
                }
                if (on)
                {
                    printf("tssm: c3d: stereo: 3D on at frame %d (slider %.2f, depth %.0f: far "
                           "parallax %.1f px)\n",
                           logFrame(), (double)st.sEff, (double)depthNow(),
                           (double)farPx(st.sEff));
                }
                else
                {
                    printf("tssm: c3d: stereo: 3D off at frame %d\n", logFrame());
                }
                fflush(stdout);
            }

            void guardTrip(uint32 over, float gpuMean)
            {
                st.probe.guardTrips++;
                st.trips++;
                st.tripsTotal++;
                st.wait = kFirstWait * (double)(1u << (st.trips - 1 < 5 ? st.trips - 1 : 5));
                if (st.wait > kMaxWait)
                {
                    st.wait = kMaxWait;
                }
                st.guard = GUARD_FADE;
                st.fadeLeft = kFadeFrames;
                st.tripAt = osGetTime();
                printf("tssm: c3d: stereo: guard: %u of the last %d 3D frames over %.0f ms, GPU "
                       "%.2f ms a frame: 3D eases out over %d frames, then 2D for at least %.0f s "
                       "(trip %u, frame %d)\n",
                       (unsigned)over, (int)st.nWin, (double)kOverMs, (double)gpuMean,
                       (int)kFadeFrames, st.wait, (unsigned)st.tripsTotal, logFrame());
                fflush(stdout);
            }
        } // namespace

        void setStereoFocusHook(StereoFocusHook hook)
        {
            st.hook = hook;
        }

        void stereoFocus(Camera* mainCamera, const float* playerPos, int fixed, int flat)
        {
            st.flat = flat != 0;
            st.focusCam = mainCamera;
            st.hasPlayer = playerPos != nil;
            if (playerPos != nil)
            {
                st.player[0] = playerPos[0];
                st.player[1] = playerPos[1];
                st.player[2] = playerPos[2];
            }
            st.fixed = fixed != 0;
        }

        void stereoOpen(const char* setting, int32 depth)
        {
            // The game's hook is set before the device opens.
            const StereoFocusHook keep = st.hook;
            memset(&st, 0, sizeof(st));
            st.hook = keep;
            stereomath::Setting cfg;
            cfg.mode = stereomath::MODE_AUTO;
            // [video] stereo takes auto or off (force is a test switch's).
            if (setting != nil && setting[0] != '\0' &&
                (!stereomath::parse(setting, &cfg) || cfg.mode == stereomath::MODE_FORCE))
            {
                printf("tssm: c3d: stereo: [video] stereo = %s is not auto or off; auto\n",
                       setting);
                cfg.mode = stereomath::MODE_AUTO;
            }
            st.depth = (float)(depth < 0 ? 0 : depth > 100 ? 100 : depth);
            int32 mode = cfg.mode;
            const char* e = getenv("TSSM_C3D_STEREO");
            if (e != nil && e[0] != '\0')
            {
                if (stereomath::parse(e, &st.env))
                {
                    st.envSet = true;
                    mode = st.env.mode;
                }
                else
                {
                    printf("tssm: c3d: stereo: TSSM_C3D_STEREO=%s is not off, auto or "
                           "force[:depth[:C]]; ignored\n",
                           e);
                }
            }
            const char* nc = getenv("TSSM_C3D_STEREO_NEARCAP");
            st.nearCap = !(nc != nil && strcmp(nc, "off") == 0);
            // The guard watches the slider's 3D; a forced 3D is a measurement
            // and has it off unless TSSM_C3D_STEREO_GUARD=on.
            const char* g = getenv("TSSM_C3D_STEREO_GUARD");
            st.guardOn = g != nil && strcmp(g, "on") == 0    ? true :
                         g != nil && strcmp(g, "off") == 0   ? false :
                         mode != stereomath::MODE_FORCE;
            const char* gt = getenv("TSSM_C3D_STEREO_GUARDTEST");
            st.guardTest = gt != nil ? atoi(gt) : 0;
            modes.stereo = mode;
            st.mode = mode;
            st.guard = GUARD_ON;
            st.guardSetting = st.guardOn;
            st.open = true;
            gfxSet3D(mode != stereomath::MODE_OFF);

            const float full = st.depth / 100.0f * stereomath::kMaxParallaxPx;
            printf("tssm: c3d: stereo: %s, slider %.2f, depth %.0f -> far parallax %.1f px at the "
                   "slider's top, near cap %s; the right eye = the frame's lists again (no second "
                   "target); guard %s",
                   modeName(mode), (double)osGet3DSliderState(), (double)st.depth, (double)full,
                   st.nearCap ? "half of it" : "off (the camera's near plane)",
                   st.guardOn ? "on" : "off");
            if (st.envSet && mode == stereomath::MODE_FORCE)
            {
                printf("; TSSM_C3D_STEREO=%s: the slider taken as full%s%s", e,
                       st.env.hasDepth ? ", its depth" : "", st.env.hasC ? ", C held" : "");
            }
            if (st.guardTest > 0)
            {
                printf("; TSSM_C3D_STEREO_GUARDTEST: 15 ms more a frame from frame %d for 2 s",
                       (int)st.guardTest);
            }
            printf("\n");
            fflush(stdout);
        }

        bool32 stereoEnabled(void)
        {
            return st.open && modes.stereo != stereomath::MODE_OFF;
        }

        bool32 stereoFrameStart(void)
        {
            st.frame3D = false;
            st.cDone = false;
            if (!st.open)
            {
                return 0;
            }
            if (modes.stereo != st.mode)
            {
                // TSSM_C3D_AB's stereo= word.
                gfxSet3D(modes.stereo != stereomath::MODE_OFF);
                printf("tssm: c3d: stereo: %s from frame %d\n", modeName(modes.stereo),
                       logFrame());
                fflush(stdout);
                st.mode = modes.stereo;
                if (modes.stereo == stereomath::MODE_FORCE)
                {
                    st.s = 1.0f;
                }
            }
            if (modes.stereo == stereomath::MODE_OFF || modes.gpuSplit)
            {
                // gpusplit (probe 14) measures 2D frames only.
                st.s = 0.0f;
                st.sEff = 0.0f;
                if (st.lastLogged3D)
                {
                    st.lastLogged3D = false;
                    logChange(false);
                }
                return 0;
            }
            st.focusCam = nil;
            st.flat = false;
            if (st.hook != nil)
            {
                st.hook();
            }

            if (modes.stereo == stereomath::MODE_FORCE)
            {
                st.s = 1.0f;
            }
            else
            {
                const float raw = osGet3DSliderState();
                const float target = raw < kSliderDead ? 0.0f : raw > 1.0f ? 1.0f : raw;
                st.s += (target - st.s) * kEase;
                if (target == 0.0f && st.s < 0.01f)
                {
                    st.s = 0.0f;
                }
            }
            float mul = 1.0f;
            if (st.guard == GUARD_FADE)
            {
                mul = (float)st.fadeLeft / (float)(kFadeFrames + 1);
            }
            else if (st.guard == GUARD_2D)
            {
                mul = 0.0f;
            }
            st.sEff = st.s * mul;
            bool want = modes.stereo == stereomath::MODE_FORCE ? st.guard != GUARD_2D :
                                                                 st.sEff > 0.0f && st.depth > 0.0f;
            const stereomath::Terms t =
                stereomath::terms(st.sEff, depthNow(), st.cInit ? st.C : kCNone, st.nearCap, 0.0f);
            st.k = t.k;
            st.frame3D = want;
            if (want != st.lastLogged3D)
            {
                st.lastLogged3D = want;
                logChange(want);
            }
            return want ? 1 : 0;
        }

        float stereoEyeSide(int32 eye)
        {
            const float sigma = eye == EYE_LEFT ? -1.0f : 1.0f;
            return -sigma * 0.5f * st.k;
        }

        bool32 stereoCamera(Camera* cam, bool32 screen, float eyeCam[4])
        {
            if (!st.frame3D && modes.eyeTest == EYETEST_2D && st.open && !st.flat && screen &&
                cam->projection == Camera::PERSPECTIVE &&
                (st.focusCam == nil || cam == st.focusCam))
            {
                // eyetest=2d: the eye code with a shift of 0 (the submit side
                // writes u_eyeSide 0), its C and w_min as a 3D frame's.
                if (!st.cDone)
                {
                    computeC(cam);
                    st.cDone = true;
                }
                const stereomath::Terms t = stereomath::terms(
                    1.0f, depthNow() > 0.0f ? depthNow() : 50.0f, st.C, st.nearCap, cam->nearPlane);
                eyeCam[0] = st.C;
                eyeCam[1] = t.wMin;
                eyeCam[2] = 0.0f;
                eyeCam[3] = 0.0f;
                return 1;
            }
            if (!st.frame3D || st.flat || !screen || cam->projection != Camera::PERSPECTIVE)
            {
                return 0;
            }
            if (st.focusCam != nil && cam != st.focusCam)
            {
                if (!st.otherLogged)
                {
                    st.otherLogged = true;
                    printf("tssm: c3d: stereo: a perspective camera on the top screen that is not "
                           "the game's main one is drawn flat (first at frame %d)\n",
                           logFrame());
                    fflush(stdout);
                }
                return 0;
            }
            if (!st.cDone)
            {
                computeC(cam);
                st.cDone = true;
            }
            const stereomath::Terms t =
                stereomath::terms(st.sEff, depthNow(), st.C, st.nearCap, cam->nearPlane);
            st.k = t.k;
            st.wMin = t.wMin;
            eyeCam[0] = st.C;
            eyeCam[1] = t.wMin;
            eyeCam[2] = 0.0f;
            eyeCam[3] = 0.0f;
            return 1;
        }

        void stereoFrameDone(bool32 was3D, int32 drop, u64 wall, u64 busy, u64 record,
                             u64 replay, float gpuMsBefore, bool32 pipelined)
        {
            if (!st.open)
            {
                return;
            }
            st.frameNo++;
            st.frames++;
            st.probe.frames++;
            if (was3D)
            {
                st.frames3D++;
                st.probe.frames3d++;
                if (drop > DROP_NONE && drop < NUM_DROPS)
                {
                    st.drops[drop]++;
                }
                switch (drop)
                {
                case DROP_NONE: st.probe.rightDrawn++; break;
                case DROP_SPLIT: st.probe.droppedSplit++; break;
                case DROP_QUEUE: st.probe.droppedQueue++; break;
                case DROP_READBACK: st.probe.droppedReadback++; break;
                case DROP_FLAT: st.probe.droppedFlat++; break;
                case DROP_ROOM: st.probe.droppedRoom++; break;
                default: break;
                }
            }
            if (st.guardOn && st.guard != GUARD_ON)
            {
                st.probe.guardFrames++;
            }
            const bool two = was3D && drop == DROP_NONE;
            const float wallMs = (float)((double)wall * 1000.0 / (double)SYSCLOCK_ARM11);
            const float busyMs = (float)((double)busy * 1000.0 / (double)SYSCLOCK_ARM11);
            const float recMs = (float)((double)record * 1000.0 / (double)SYSCLOCK_ARM11);

            // The GPU time citro3d has now is the frame before's.
            float gpuPrev = -1.0f;
            if (st.havePrev)
            {
                gpuPrev = gpuMsBefore;
                if (st.prevWas3D)
                {
                    st.g3 += gpuPrev;
                    st.n3++;
                    if (st.nWin > 0 && st.prevJudged)
                    {
                        st.gpu[(st.atWin + kWindow - 1) % kWindow] = gpuPrev;
                    }
                }
                else
                {
                    st.g2 += gpuPrev;
                    st.n2++;
                }
            }
            st.havePrev = true;
            st.prevWas3D = two;
            st.prevJudged = false;
            if (two)
            {
                const float r = (float)((double)replay * 1000.0 / (double)SYSCLOCK_ARM11);
                st.replayMs += (r - st.replayMs) * 0.1f;
                st.warm++;
            }
            else
            {
                st.warm = 0;
            }

            if (!st.guardOn || wall == 0)
            {
                return;
            }
            // A frame over a second is a load or a capture's file write, not
            // the drawing: left out, as the A/B segments leave it out.
            if (wallMs >= kHitchMs)
            {
                return;
            }
            if (st.guard == GUARD_ON && two && st.warm > kWarmFrames)
            {
                st.prevJudged = true;
                st.over[st.atWin] = wallMs > kOverMs ? 1 : 0;
                st.gpu[st.atWin] = 0.0f; // filled in at the next present
                st.atWin = (st.atWin + 1) % kWindow;
                if (st.nWin < kWindow)
                {
                    st.nWin++;
                }
                uint32 over = 0;
                float gsum = 0.0f;
                int32 gn = 0;
                for (int32 i = 0; i < st.nWin; i++)
                {
                    over += st.over[i];
                    if (st.gpu[i] > 0.0f)
                    {
                        gsum += st.gpu[i];
                        gn++;
                    }
                }
                const float gmean = gn > 0 ? gsum / (float)gn : 0.0f;
                if (over >= 3 || (gn >= kWindow - 1 && gmean > kGpuLimitMs))
                {
                    guardTrip(over, gmean);
                }
            }
            else if (st.guard == GUARD_FADE)
            {
                if (--st.fadeLeft <= 0)
                {
                    st.guard = GUARD_2D;
                    st.in2DFrom = osGetTime();
                    st.roomN = 0;
                    st.roomAt = 0;
                }
            }
            else if (st.guard == GUARD_2D && !was3D)
            {
                const double waited = (double)(osGetTime() - st.tripAt) / 1000.0;
                if (waited >= st.wait && record > 0 && gpuPrev >= 0.0f)
                {
                    float ratio = 2.05f;
                    if (st.n3 > 0 && st.n2 > 0 && st.g2 > 0.0)
                    {
                        ratio = (float)((st.g3 / st.n3) / (st.g2 / st.n2));
                        ratio = ratio < 1.5f ? 1.5f : ratio > 3.0f ? 3.0f : ratio;
                    }
                    const float overlap = busyMs > recMs ? busyMs - recMs : 0.0f;
                    const float gpu3 = ratio * gpuPrev;
                    // Pipelined (L4), a frame takes about the longer of the CPU's
                    // whole frame and the GPU's; without it the recording comes
                    // first and only the rest overlaps the GPU.
                    const float cpu3 = busyMs + st.replayMs;
                    const float predict =
                        pipelined ? 0.2f + (cpu3 > gpu3 ? cpu3 : gpu3)
                                  : recMs + 0.2f + (overlap > gpu3 ? overlap : gpu3);
                    st.lastPredict = predict;
                    st.roomOver[st.roomAt] = predict > kFitMs ? 1 : 0;
                    st.roomAt = (st.roomAt + 1) % kRoomFrames;
                    if (st.roomN < kRoomFrames)
                    {
                        st.roomN++;
                    }
                    int32 misses = 0;
                    for (int32 i = 0; i < st.roomN; i++)
                    {
                        misses += st.roomOver[i];
                    }
                    if (st.roomN >= kRoomFrames && misses <= kRoomMisses)
                    {
                        const double in2D = (double)(osGetTime() - st.in2DFrom) / 1000.0;
                        st.sec2D += in2D;
                        st.guard = GUARD_ON;
                        st.nWin = 0;
                        st.atWin = 0;
                        printf("tssm: c3d: stereo: guard: back to 3D at frame %d after %.1f s in "
                               "2D (%d of the last %d frames predicted a 3D frame in %.1f ms or "
                               "less; the last %.1f ms: recording %.1f, the rest %.1f, GPU %.1f x "
                               "%.2f, the right eye %.2f ms, %s)\n",
                               logFrame(), in2D, (int)(kRoomFrames - misses),
                               (int)kRoomFrames, (double)kFitMs, (double)predict, (double)recMs,
                               (double)overlap, (double)gpuPrev, (double)ratio,
                               (double)st.replayMs,
                               pipelined ? "pipelined: the longer of CPU and GPU" :
                                           "recording, then the longer of the rest and GPU");
                        fflush(stdout);
                    }
                }
            }
            (void)nowSec;
        }

        bool32 stereoWantsRecordTiming(void)
        {
            return st.open && st.guardOn && st.guard == GUARD_2D;
        }

        void stereoLevelLoaded(void)
        {
            st.trips = 0;
        }

        bool32 stereoProbeSet(int32 mode, bool32 guard, int32 depth)
        {
            if (!st.open)
            {
                return 0;
            }
            if (mode == stereomath::MODE_FORCE)
            {
                st.probeDepthSet = depth >= 0;
                st.probeDepth = (float)(depth < 0 ? 0 : depth > 100 ? 100 : depth);
                st.guardOn = guard != 0;
            }
            else
            {
                st.probeDepthSet = false;
                st.guardOn = st.guardSetting;
            }
            // Each segment's guard from the start: no trip, no wait, empty
            // windows.
            st.guard = GUARD_ON;
            st.fadeLeft = 0;
            st.trips = 0;
            st.wait = 0.0;
            st.nWin = 0;
            st.atWin = 0;
            st.roomN = 0;
            st.roomAt = 0;
            printf("tssm: c3d: stereo: probe segment: %s, depth %.0f, guard %s\n",
                   modeName(mode), (double)depthNow(), st.guardOn ? "on" : "off");
            fflush(stdout);
            return 1;
        }

        bool32 stereoProbeTake(StereoProbeStats* out)
        {
            *out = st.probe;
            memset(&st.probe, 0, sizeof(st.probe));
            return st.open ? 1 : 0;
        }

        void stereoProbeQueue(uint32 entries)
        {
            if (entries > st.probe.gxEntriesPeak)
            {
                st.probe.gxEntriesPeak = entries;
            }
        }

        void stereoGuardTestSleep(void)
        {
            if (st.guardTest <= 0)
            {
                return;
            }
            const int f = iCaptureFrameCount();
            if (f >= st.guardTest && f < st.guardTest + 60)
            {
                svcSleepThread(15000000LL);
            }
        }

        bool32 stereoCaptureLeftWanted(void)
        {
            return st.captureLeft;
        }

        void stereoCaptureLeft(int on)
        {
            st.captureLeft = on != 0;
        }

        void stereoStats(uint32 replayed, uint32 replayBytes, uint32 queuePeak3D, u64 replayTicks)
        {
            if (!st.open || modes.stereo == stereomath::MODE_OFF)
            {
                return;
            }
            const uint32 f3 = st.frames3D;
            uint32 two = f3;
            for (int32 i = 1; i < NUM_DROPS; i++)
            {
                two -= st.drops[i] < two ? st.drops[i] : two;
            }
            double in2D = st.sec2D;
            if (st.guard == GUARD_2D)
            {
                in2D += (double)(osGetTime() - st.in2DFrom) / 1000.0;
            }
            printf("tssm: c3d: stereo: %u of %u frames in 3D (%u with two eyes); right eyes "
                   "dropped: hard split %u, GX queue %u, read-back %u, flat %u, room %u; slider "
                   "%.2f (eased %.2f, with the guard %.2f), C %.2f, k %.5f (far parallax %.1f "
                   "px, near cap w %.2f); lists replayed %.1f a 3D frame (%.1f KB, the right eye's "
                   "CPU %.0f us); GX queue peak %u; guard %s, %u trips, %.1f s in 2D under it "
                   "(the last prediction of a 3D frame %.1f ms)\n",
                   (unsigned)f3, (unsigned)st.frames, (unsigned)two, (unsigned)st.drops[DROP_SPLIT],
                   (unsigned)st.drops[DROP_QUEUE], (unsigned)st.drops[DROP_READBACK],
                   (unsigned)st.drops[DROP_FLAT], (unsigned)st.drops[DROP_ROOM],
                   (double)osGet3DSliderState(), (double)st.s, (double)st.sEff, (double)st.C,
                   (double)st.k, (double)(st.k * stereomath::kPxPerNdc), (double)st.wMin,
                   two > 0 ? (double)replayed / two : 0.0,
                   two > 0 ? (double)replayBytes / two / 1024.0 : 0.0,
                   two > 0 ? (double)replayTicks * 1e6 / SYSCLOCK_ARM11 / two : 0.0,
                   (unsigned)queuePeak3D,
                   !st.guardOn               ? "off" :
                   st.guard == GUARD_ON      ? "on" :
                   st.guard == GUARD_FADE    ? "easing out" :
                                               "2D",
                   (unsigned)st.tripsTotal, in2D, (double)st.lastPredict);
            st.frames = 0;
            st.frames3D = 0;
            memset(st.drops, 0, sizeof(st.drops));
            (void)dropName;
        }
    } // namespace c3d
} // namespace rw
