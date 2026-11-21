// PC counterpart of gc/iAsync.cpp, compiled inside pc/iWad.cpp.
//
// On the console zGameInit starts zAsync::RenderThread on a second OS thread
// while a level loads. The two share a single core: the thread sleeps on the
// retrace, wakes above the main thread, draws one whole frame of the loading
// screen with interrupts off (iAsyncBeginUpdate, iAsyncEndUpdate) and sleeps
// again. So the load is cut into slices by whole loading frames, sixty a
// second, and nothing ever runs at the same time as anything else.
//
// A host thread would run truly in parallel, and the frame is drawn through
// RenderWare, the game's heap and the window, none of which may be used from
// two threads at once. A lock around all of that gives back exactly the
// console's arrangement -- one side stopped while the other runs -- with a
// hand-over added. So there is no thread here. The game thread draws the
// frame itself, between pieces of the load: the file layer and the RenderWare
// readers call iLoadFramePump (iLoadFrame.h) as each piece ends, and when a
// sixtieth of a second has gone by since the last frame, that call draws the
// next one, the way one pass of zAsync::RenderThread's loop does. The same
// picture, the same rule for skipping a frame, and it works wherever the port
// has one thread to draw on.
//
// What differs from the console, and why:
//   - A frame can only start where a piece of the load ends, so the gaps
//     between frames are as long as the longest piece. TSSM_TEST_LOADLOG
//     reports them.
//   - The flip does not wait for the display (rw/camera.cpp): the wait would
//     be the load's, not a spare thread's.
//   - A frame's cost is the load's too, so the next one is held off until the
//     load has had nine times the last one's cost, or a quarter of a second
//     if that is sooner. The screen takes at most a tenth of a load while a
//     frame costs under 28 ms, and one hitch cannot freeze it for seconds.
//   - The disc-error screen never suspends anything: iTRC.cpp has no such
//     screen, and there is no thread to suspend.

#include "iAsync.h"

#include "iCamera.h"
#include "iFrameGrab.h"
#include "iHost.h"
#include "iLoadFrame.h"
#include "iTime.h"
#include "iWindow.h"
#include "xDebug.h"
#include "xMemMgr.h"
#include "zAsync.h"
#include "zGlobals.h"

#include <rwcore.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool asyncThreadDone = true;
RwLinkList asyncDirtyFrameList;

namespace
{
    // One frame per retrace on the console; its display is 60 Hz.
    const U64 kFrameNs = 1000000000ull / 60;

    // The load gets at least this many times a frame's cost between frames.
    const U64 kCostShare = 9;

    // ...but one slow frame never holds the next back longer than this, or
    // the screen would stand still for nine times a hitch.
    const U64 kMaxHoldNs = 250000000ull;

    bool sActive;
    bool sInFrame;
    iTime sLastTime; // RenderThread's lastTime
    U64 sLastFrameNs; // when the last frame was started, or skipped
    U64 sNotBeforeNs; // the cost guard

    // TSSM_TEST_LOADLOG, TSSM_CAPTURE_LOAD, TSSM_TEST_LOADSLOW, TSSM_TEST_LOADEVENT
    // (iTestEvents.h).
    bool sEnvRead;
    S32 sLog;
    U64 sLastPumpNs;
    S32 sCaptureCount;
    S32 sSlowMs;
    S32 sEventFrame;
    char sEvent[16];
    char sCaptureDir[512];
    U64 sCaptureNs; // what the last frame's capture took: the test's time, not the frame's

    S32 sLoads;
    U64 sStartNs;
    U64 sCostNs;
    U64 sLongestCostNs;
    U64 sLongestGapNs;
    S32 sLongestGapPoint;
    S32 sFrames;
    S32 sSkipped;
    S32 sFramesAt[iLOADPUMP_COUNT];
    U32 sPumps;

    const char* const kPointNames[iLOADPUMP_COUNT] = { "start",    "load step", "file read",
                                                       "textures", "clump",     "atomic",
                                                       "world",    "scene setup" };

    void ReadEnv()
    {
        sEnvRead = true;

        const char* log = getenv("TSSM_TEST_LOADLOG");
        sLog = log != NULL ? atoi(log) : 0;

        const char* capture = getenv("TSSM_CAPTURE_LOAD");
        sCaptureCount = capture != NULL ? atoi(capture) : 0;

        const char* slow = getenv("TSSM_TEST_LOADSLOW");
        sSlowMs = slow != NULL ? atoi(slow) : 0;

        const char* event = getenv("TSSM_TEST_LOADEVENT");
        if (event != NULL && sscanf(event, "%d:%15s", &sEventFrame, sEvent) != 2)
        {
            sEventFrame = 0;
        }

        const char* dir = getenv("TSSM_CAPTURE_DIR");
        if (dir != NULL && dir[0] != '\0')
        {
            const size_t n = strlen(dir);
            const bool slash = dir[n - 1] == '/' || dir[n - 1] == '\\';
            snprintf(sCaptureDir, sizeof(sCaptureDir), "%s%s", dir, slash ? "" : "/");
        }
    }

    // RwCameraShowRaster's call, just before a loading frame's flip.
    void OnShow()
    {
        if (sFrames < sCaptureCount)
        {
            const U64 beginNs = iHostMonotonicNs();
            char path[640];
            snprintf(path, sizeof(path), "%sload%02d_%03d.png", sCaptureDir, (int)sLoads,
                     (int)sFrames + 1);
            if (!iFrameGrabWrite(path, 0))
            {
                printf("tssm: load: no frame to read, or %s could not be written\n", path);
                fflush(stdout);
            }
            sCaptureNs = iHostMonotonicNs() - beginNs;
        }
    }

    // TSSM_TEST_LOADEVENT: what a player could do to the window in the middle of a
    // load, which no frame-numbered script can, because a load is one frame.
    void TestEvent()
    {
        if (strcmp(sEvent, "close") == 0)
        {
            iWindowRequestClose();
        }
        else if (strcmp(sEvent, "mode") == 0)
        {
            // Alt+Enter.
            const S32 ok = iWindowSetMode(iWindowGetMode() == iWINDOW_WINDOWED ?
                                              iWINDOW_BORDERLESS :
                                              iWINDOW_WINDOWED);
            S32 w = 0;
            S32 h = 0;
            iWindowGetSize(&w, &h);
            printf("tssm: load %d: frame %d: window mode switched (%s), now %d x %d\n",
                   (int)sLoads, (int)sFrames + 1, ok ? "done" : "refused", (int)w, (int)h);
        }
        fflush(stdout);
    }

    // TSSM_TEST_LOADLOG=2: the stretches of a load that reach no pump, which is
    // where a slower machine's loading screen would stand still.
    void Stretch(const char* endedBy)
    {
        const U64 now = iHostMonotonicNs();
        if (now - sLastPumpNs >= 5000000ull)
        {
            printf("tssm: load %d: %.1f ms with no pump, from %.1f ms, ended by %s\n",
                   (int)sLoads, (now - sLastPumpNs) / 1e6, (sLastPumpNs - sStartNs) / 1e6,
                   endedBy);
        }
        sLastPumpNs = now;
    }

    // One pass of zAsync::RenderThread's loop (zAsync.cpp), on the game thread.
    void Frame(S32 point)
    {
        const U64 beginNs = iHostMonotonicNs();

        if (sLastFrameNs != 0 && beginNs - sLastFrameNs > sLongestGapNs)
        {
            sLongestGapNs = beginNs - sLastFrameNs;
            sLongestGapPoint = point;
        }
        sLastFrameNs = beginNs;

        const iTime curTime = iTimeGet();
        F32 dt = iTimeDiffSec(sLastTime, curTime);
        if (dt == 0.0f)
        {
            dt = 0.0001f;
        }
        sLastTime = curTime;

        // The console's rule: no frame while the load has a camera open or
        // scratch memory in use. The screen's text uses that memory.
        if (RwCameraGetCurrentCamera() != NULL || scratch_mem_used)
        {
            sSkipped++;
            return;
        }

        if (sEventFrame > 0 && sLoads == 1 && sFrames + 1 == sEventFrame)
        {
            TestEvent();
        }

        RwRGBA bg = { 0x12, 0x24, 0x4B, 0x00 };

        sInFrame = true;
        sCaptureNs = 0;
        iLoadFrameSetDrawing(TRUE);

        RwCameraClear(zAsync::cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
        RwCameraBeginUpdateWrapper(zAsync::cam);

        if (zAsync::screen)
        {
            zAsync::screen->Update(dt);
            zAsync::screen->Render();
        }

        RwCameraEndUpdate(zAsync::cam);
        RwCameraShowRaster(zAsync::cam, NULL, 1);

        gFrameCount++;

        iLoadFrameSetDrawing(FALSE);
        sInFrame = false;

        const U64 endNs = iHostMonotonicNs();
        const U64 cost = endNs - beginNs - sCaptureNs;
        const U64 hold = cost * kCostShare;
        sNotBeforeNs = endNs + (hold < kMaxHoldNs ? hold : kMaxHoldNs);
        sCostNs += cost;
        if (cost > sLongestCostNs)
        {
            sLongestCostNs = cost;
        }
        sFramesAt[point]++;
        sFrames++;
    }

    void Pump(S32 point)
    {
        if (!sActive || sInFrame)
        {
            return;
        }

        sPumps++;

        if (sLog >= 2)
        {
            Stretch(kPointNames[point]);
        }

        if (sSlowMs > 0 && point == iLOADPUMP_STEP)
        {
            // TSSM_TEST_LOADSLOW: a slower machine's load, a step at a time, with
            // the pumps a long read would give.
            const U64 until = iHostMonotonicNs() + (U64)sSlowMs * 1000000ull;
            while (iHostMonotonicNs() < until)
            {
                iHostSleepUntilNs(iHostMonotonicNs() + 1000000ull);
                Pump(iLOADPUMP_READ);
            }
        }

        const U64 now = iHostMonotonicNs();
        if (now - sLastFrameNs < kFrameNs || now < sNotBeforeNs)
        {
            return;
        }

        Frame(point);
    }
} // namespace

void iAsyncStart(void* (*func)(void*))
{
    rwLinkListInitialize(&asyncDirtyFrameList);

    if (func != zAsync::RenderThread || zAsync::cam == NULL)
    {
        // Nothing else in the game starts one, and only the loading screen
        // can be drawn in slices from here: anything else is not run.
        printf("tssm: iAsyncStart: only the loading screen is run here\n");
        fflush(stdout);
        asyncThreadDone = true;
        return;
    }

    if (!sEnvRead)
    {
        ReadEnv();
    }

    asyncThreadDone = false;
    sActive = true;
    sLoads++;
    sStartNs = iHostMonotonicNs();
    sCostNs = 0;
    sLongestCostNs = 0;
    sLongestGapNs = 0;
    sLongestGapPoint = iLOADPUMP_START;
    sFrames = 0;
    sSkipped = 0;
    sPumps = 0;
    for (S32 i = 0; i < iLOADPUMP_COUNT; i++)
    {
        sFramesAt[i] = 0;
    }

    // RenderThread's first lines, and its first frame: on the console the
    // thread is above the main one and has drawn before iAsyncStart returns.
    sLastTime = iTimeGet();
    sLastFrameNs = 0;
    sNotBeforeNs = 0;
    sLastPumpNs = sStartNs;
    globals.inLoadingScreen = TRUE;

    iLoadFrameSetShowHook(OnShow);
    iLoadFramePumpFn = Pump;
    Frame(iLOADPUMP_START);
}

S32 iAsyncIsDone()
{
    return asyncThreadDone;
}

void iAsyncTerminate()
{
    asyncThreadDone = true;

    if (!sActive)
    {
        return;
    }

    // RenderThread's last line, once its loop sees the flag.
    sActive = false;
    iLoadFramePumpFn = NULL;
    iLoadFrameSetShowHook(NULL);
    globals.inLoadingScreen = FALSE;

    if (sLog >= 2)
    {
        Stretch("the load's end");
    }

    if (sLog)
    {
        const U64 now = iHostMonotonicNs();
        const U64 tail = now - sLastFrameNs;
        if (tail > sLongestGapNs)
        {
            sLongestGapNs = tail;
            sLongestGapPoint = -1;
        }

        printf("tssm: load %d: %.1f ms, %d loading frames (%d skipped), %.1f ms drawing them "
               "(longest %.2f ms), longest gap %.1f ms ended by %s, %u pumps; frames by pump:",
               (int)sLoads, (now - sStartNs) / 1e6, (int)sFrames, (int)sSkipped, sCostNs / 1e6,
               sLongestCostNs / 1e6, sLongestGapNs / 1e6,
               sLongestGapPoint < 0 ? "the load's end" : kPointNames[sLongestGapPoint],
               (unsigned)sPumps);
        for (S32 i = 0; i < iLOADPUMP_COUNT; i++)
        {
            if (sFramesAt[i] != 0)
            {
                printf(" %s %d", kPointNames[i], (int)sFramesAt[i]);
            }
        }
        printf("\n");
        fflush(stdout);
    }
}

// Interrupts off and on, on the console: its lock. One thread needs none.
void iAsyncBeginUpdate()
{
}

void iAsyncEndUpdate()
{
}

// For the console's disc-error screen (gc/iSystem.cpp); see the top.
void iAsyncSuspend()
{
}

void iAsyncResume()
{
}
