#ifndef ILOADFRAME_H
#define ILOADFRAME_H

#include <types.h>

// PC-only: the seam between a level load and the loading screen's frames.
//
// On the console the loading screen is drawn by a second thread that the
// retrace wakes (zAsync::RenderThread). Here the game thread draws it itself,
// between pieces of the load (iAsync.cpp). The pieces are port code that has
// no business knowing about the game's screens -- the file layer, the
// RenderWare readers -- so they call iLoadFramePump and iAsync.cpp answers
// through a pointer, as iHangDump.h does. Compiled into tssm_rw.

// Where a pump came from, for TSSM_TEST_LOADLOG.
enum iLoadPumpPoint
{
    iLOADPUMP_START, // iAsyncStart: the first frame
    iLOADPUMP_STEP, // iFileAsyncService: once per xSTLoadStep, i.e. per package layer
    iLOADPUMP_READ, // iFileRead
    iLOADPUMP_TEXDICT, // RwTexDictionaryStreamRead
    iLOADPUMP_CLUMP, // RpClumpStreamRead
    iLOADPUMP_ATOMIC, // RpAtomicStreamRead
    iLOADPUMP_WORLD, // RpWorldStreamRead
    iLOADPUMP_SCENE, // zSceneInit: between the parts of setting the scene up
    iLOADPUMP_COUNT
};

// A piece of a load has just finished: a chance to draw one loading frame.
// One load of a pointer when no level is loading.
typedef void (*iLoadPumpFn)(S32 point);
extern iLoadPumpFn iLoadFramePumpFn;

inline void iLoadFramePump(S32 point)
{
    if (iLoadFramePumpFn != NULL)
    {
        iLoadFramePumpFn(point);
    }
}

// TRUE while a loading frame is being drawn and presented. Such a frame is not
// one of the game's own: RwCameraShowRaster (rw/camera.cpp) still pumps the
// window and answers its close button, but does not wait for the display, pace,
// count the frame for the test scripts or the metrics, or keep it as the last
// frame of the level.
void iLoadFrameSetDrawing(S32 on);
S32 iLoadFrameDrawing();

// Called by RwCameraShowRaster just before a loading frame's flip, where the
// frame can still be read back (TSSM_CAPTURE_LOAD).
typedef void (*iLoadFrameShowFn)();
void iLoadFrameSetShowHook(iLoadFrameShowFn fn);
void iLoadFrameShow();

#endif
