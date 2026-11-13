#ifndef ICAPTURE_H
#define ICAPTURE_H

#include <types.h>

// PC-only debug capture, so a run can be checked without anyone watching it.
//
//   TSSM_CAPTURE_FRAMES=<list>  frames whose back buffer is written as a PNG.
//                               A list of numbers and ranges, comma separated:
//                               "1,3,100-400/50" is frames 1, 3, 100, 150, ...
//                               400. Frame 1 is the first frame presented.
//   TSSM_CAPTURE_DIR=<dir>      where they go, as frame_00001.png and so on.
//                               The working directory when unset. The folder
//                               must exist.
//   TSSM_EXIT_AFTER=<n>         quit, with exit code 0, once n frames have been
//                               presented.
//   TSSM_INPUT=<list>           scripted controller on port 0: entries
//                               "frame:button[+button...][/frames]", comma
//                               separated, e.g. "700:start,900:a/10". Each
//                               holds its buttons from that frame for the given
//                               number of frames (6 when not given). Button
//                               names are config.ini's [pad] names (a, b, x, y,
//                               start, up, ...). While set, real controllers and
//                               the keyboard are ignored, so a run does not
//                               depend on what is plugged in.
//   TSSM_TEST_WARP=<list>,      debug events at given frames, through the same
//   TSSM_TEST_PLACE=<list>,     hook; see iTestEvents.h.
//   TSSM_TEST_CAMLOG=<n>
//
// A "frame" is one RwCameraShowRaster: everything the game, the boot screens
// and the movie player present counts, in the order they present it. The
// capture is taken before the flip, from the same copy of the frame buffer the
// loading-screen still uses (iFrameGrabWrite), so it is the game's own render
// target at the render size, never the desktop.
//
// Every capture, press and the exit are logged ("tssm: capture: ..."), with the
// time since the first frame, so a log alone says how far a run got.
//
// None of this is on unless one of the variables is set; the frame hook is not
// installed otherwise.
void iCaptureInit();

// Frames presented so far.
S32 iCaptureFrameCount();

#endif
