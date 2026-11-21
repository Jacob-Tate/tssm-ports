#ifndef IPERF_H
#define IPERF_H

// PC-only frame metrics, for benchmarks (tools/android_bench.py). Off unless
// TSSM_PERF is set; then every presented frame is measured and nothing else
// changes. Every platform, every backend; the GPU columns need GL3.
//
//   TSSM_PERF=<seconds>        on, with a summary line in the log every that
//                              many seconds ("tssm: perf: ...").
//   TSSM_PERF_CSV=<file>       one row per frame (below). perf.csv in the
//                              working directory when unset; on Android in the
//                              app's private files/perf-test (or files).
//   TSSM_PERF_PROFILE=<hz>     Linux and Android: samples the game thread's
//                              stack that many times per second of its CPU time
//                              (a SIGPROF timer on the thread's own CPU clock;
//                              iPerfSampler.h), written at exit.
//   TSSM_PERF_PROFILE_OUT=<file>  where the samples go: perf-samples.txt beside
//                              the CSV when unset.
//
// A frame is one RwCameraShowRaster, from the end of the previous one's pacing
// to the end of its own. The game thread's time in it is split into phases, each
// as wall time and as the thread's own CPU time (CLOCK_THREAD_CPUTIME_ID; on
// Windows QueryThreadCycleTime at the measured TSC rate):
//
//   update   to the frame's first RwCameraBeginUpdate: the game's update and
//            pre-render work (in zGameLoop, everything before zCamBeginRender)
//   render   to RwCameraShowRaster: the scene drawn, i.e. the game walking it
//            and the driver taking the draws
//   present  RwCameraShowRaster up to the flip: the window's events, the frame
//            hook (captures, test events)
//   swap     the backend's showRaster: the blit to the window, the overlay, the
//            swap; wall minus CPU here is mostly waiting for the display
//   pace     iWindowPaceFrame's sleep to the frame-rate cap
//
// The CSV's columns: frame, t_ms (the end of the swap, from the first frame),
// interval_ms (swap to swap), rate (the paced rate, 0 uncapped), missed (frames
// of the paced rate this interval missed: round(interval * rate) - 1), the five
// phases' wall and CPU ms, gpu_ms (GL3 timer query, -1 when not available),
// cameras (RwCameraBeginUpdate calls: the main camera, shadows, glow), and the
// GL3 counters: draws, tris, passes, screen_passes, blits, clears, programs,
// tex_binds, tex_uploads, tex_kb, buf_uploads, buf_kb; then the host's side of
// the frame: pumps (SDL event pumps in it; the window's, at the present, with
// its events' handling), pump_ms (their total), pump_max_ms (the longest),
// pad_ms (the frame's start to the controllers' read, iPadHostPoll; -1 when
// nothing read them), input_age_ms (how old the event
// pump's state was at that read: the time since the last pump ended), and the
// pacer: pace_want_ms (the sleep it asked for, 0 when the deadline had already
// passed) and pace_late_ms (how far past its deadline it let the frame go:
// the sleep's overshoot, or the overrun when it did not sleep). The summary's
// "1% low" is the frame rate over the slowest 1% of the window's frames.

#include <types.h>

// The frame's hooks, called by rw/camera.cpp.
void iPerfRenderBegin(); // RwCameraBeginUpdate
void iPerfPresentBegin(); // RwCameraShowRaster, on entry
void iPerfSwapBegin(); // just before the backend's showRaster
void iPerfSwapEnd(); // just after it
void iPerfFrameEnd(); // after iWindowPaceFrame

// The host's hooks: around each SDL_PumpEvents (the whole of iWindowSDL.cpp's
// iWindowPump), where the controllers are read,
// and the pacer's sleep (nanoseconds asked for, and how late it returned).
void iPerfPumpBegin();
void iPerfPumpEnd();
void iPerfInputRead();
void iPerfPace(U64 wantNs, S64 lateNs);

// TRUE once TSSM_PERF has turned the metrics on.
S32 iPerfOn();

#endif
