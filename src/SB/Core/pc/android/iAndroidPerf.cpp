// iAndroidPerf.cpp: the game thread's work, reported to Android's performance
// hints (APerformanceHint, Android 13 and later). Port-only.
//
// Without hints the CPU governor sees a thread that runs for a few
// milliseconds 120 times a second and sleeps in between, and it answers what
// it sees: high clocks the moment the thread wakes, held for a while after,
// whatever the frame needed. A hint session tells it instead how long each
// frame's work may take (the paced frame time, 8.33 ms at 120 fps) and how long
// it did take. The governor can then run the game's core as slowly as still
// meets the target, raise the clock as soon as a frame runs long, and on
// Android 15 and later prefer the most power-efficient way of doing so.
//
// The work is what iWindowFrameWorkDone measures (iWindow.h): from the end of
// the last pace to the moment the frame is handed to the display, so the swap
// and the pacer's sleep are not counted as work. The driver can still wait for
// the display inside the frame, at its first draw (DE01 on the S25 Ultra: 6 ms
// of wall time in the render phase for 1.3 ms of CPU); that wait is reported as
// work, which errs on the side of a faster clock and never a missed frame.
//
// Looked up at run time (dlsym), because the app installs from Android 7
// (API 24) and the functions are API 33 and 35. Where they are missing, or the
// device has no hint support (APerformanceHint_getManager or createSession
// returns NULL), nothing happens and the log says so once.
//
// TSSM_PERF_HINT=0 turns the hints off, 2 keeps them on without asking for
// power efficiency; for comparing runs.

#include "iAndroid.h"

#include "../iWindow.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

namespace
{
    struct HintManager;
    struct HintSession;

    typedef HintManager* (*GetManagerFn)();
    typedef HintSession* (*CreateSessionFn)(HintManager*, const int32_t*, size_t, int64_t);
    typedef int (*UpdateTargetFn)(HintSession*, int64_t);
    typedef int (*ReportActualFn)(HintSession*, int64_t);
    typedef int (*SetPreferPowerEfficiencyFn)(HintSession*, bool);

    ReportActualFn sReport;
    UpdateTargetFn sUpdateTarget;
    HintSession* sSession;
    int64_t sTargetNs;
    bool sGaveUp;

    int64_t TargetFor(S32 pacedRate)
    {
        // Uncapped has no frame time of its own; the display's rate is the
        // nearest thing to one, and 60 where that is unknown.
        S32 rate = pacedRate > 0 ? pacedRate : iWindowGetDisplayRefreshRate();
        if (rate <= 0)
        {
            rate = 60;
        }
        return 1000000000LL / rate;
    }

    // On the game thread, on its first frame: the session belongs to the
    // thread that reports, which is this one.
    bool OpenSession(S32 pacedRate)
    {
        sGaveUp = true;

        const char* mode = getenv("TSSM_PERF_HINT");
        const int hintMode = (mode != NULL && *mode != '\0') ? atoi(mode) : 1;
        if (hintMode == 0)
        {
            printf("tssm: performance hints: off (TSSM_PERF_HINT=0)\n");
            return false;
        }

        void* android = dlopen("libandroid.so", RTLD_NOW);
        if (android == NULL)
        {
            return false;
        }
        GetManagerFn getManager = (GetManagerFn)dlsym(android, "APerformanceHint_getManager");
        CreateSessionFn createSession =
            (CreateSessionFn)dlsym(android, "APerformanceHint_createSession");
        sUpdateTarget = (UpdateTargetFn)dlsym(android, "APerformanceHint_updateTargetWorkDuration");
        sReport = (ReportActualFn)dlsym(android, "APerformanceHint_reportActualWorkDuration");
        SetPreferPowerEfficiencyFn setPreferPowerEfficiency =
            (SetPreferPowerEfficiencyFn)dlsym(android, "APerformanceHint_setPreferPowerEfficiency");
        if (getManager == NULL || createSession == NULL || sUpdateTarget == NULL || sReport == NULL)
        {
            printf("tssm: performance hints: not on this Android (they are Android 13 and later)\n");
            return false;
        }

        HintManager* manager = getManager();
        const int32_t tid = (int32_t)gettid();
        sTargetNs = TargetFor(pacedRate);
        sSession = manager != NULL ? createSession(manager, &tid, 1, sTargetNs) : NULL;
        if (sSession == NULL)
        {
            printf("tssm: performance hints: this device does not take them\n");
            return false;
        }

        const bool efficiency = hintMode != 2 && setPreferPowerEfficiency != NULL &&
                                setPreferPowerEfficiency(sSession, true) == 0;
        printf("tssm: performance hints: on for the game thread, %.2f ms a frame%s\n",
               sTargetNs / 1e6, efficiency ? ", preferring power efficiency" : "");
        sGaveUp = false;
        return true;
    }

    void FrameWork(U64 workNs, S32 pacedRate)
    {
        if (sSession == NULL && (sGaveUp || !OpenSession(pacedRate)))
        {
            return;
        }

        const int64_t target = TargetFor(pacedRate);
        if (target != sTargetNs && sUpdateTarget(sSession, target) == 0)
        {
            sTargetNs = target;
        }

        // A frame of four or more frame times is not the steady work the hint
        // is about: a level loading, or the first frame back from the
        // background, which counts the time the game was stopped. Reporting it
        // would only ask for full clocks after the fact.
        if (workNs == 0 || (int64_t)workNs >= 4 * sTargetNs)
        {
            return;
        }
        sReport(sSession, (int64_t)workNs);
    }
} // namespace

void iAndroidPerfStart()
{
    iWindowSetFrameWorkHook(FrameWork);
}
