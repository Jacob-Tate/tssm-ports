// The debug capture; see iCapture.h. Port-only.

#include "iCapture.h"

#include "iAudioSDLTest.h"
#include "iFrameGrab.h"
#include "iLowHeap.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iSnapshot.h"
#include "iTestEvents.h"
#include "iTestSend.h"
#include "iTime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // One entry of TSSM_CAPTURE_FRAMES: frames first..last, every step-th.
    struct Range
    {
        S32 first;
        S32 last;
        S32 step;
    };

    const S32 kMaxRanges = 64;

    // One entry of TSSM_INPUT: hold `buttons` (XPAD_BUTTON_*) for `frames`
    // frames from frame `at`.
    struct Press
    {
        S32 at;
        S32 frames;
        U32 buttons;
        char text[48];
    };

    // A benchmark's scripted walk (tools/android_bench.py) is a few minutes of
    // presses.
    const S32 kMaxPresses = 256;
    const S32 kDefaultHold = 6;

    Range sRanges[kMaxRanges];
    S32 sRangeCount;
    Press sPresses[kMaxPresses];
    S32 sPressCount;
    bool sScripted;
    S32 sExitAfter;
    S32 sFrame;
    S32 sCaptured;
    // iCaptureRequest: a file name for the next frame presented.
    char sRequest[96];
    bool sRequested;
    bool sHooked;
    iTime sFirstFrameAt;
    char sDir[512];

    // "700:start", "900:a+up/10". FALSE on anything else.
    bool ParsePress(const char* text, Press* out)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", text);
        snprintf(out->text, sizeof(out->text), "%s", text);

        char* end;
        long at = strtol(buf, &end, 10);
        if (end == buf || at < 1 || *end != ':')
        {
            return false;
        }

        char* names = end + 1;
        long frames = kDefaultHold;
        char* slash = strchr(names, '/');
        if (slash != NULL)
        {
            *slash = '\0';
            frames = strtol(slash + 1, &end, 10);
            if (end == slash + 1 || *end != '\0' || frames < 1)
            {
                return false;
            }
        }

        U32 mask = 0;
        for (char* name = strtok(names, "+"); name != NULL; name = strtok(NULL, "+"))
        {
            const iPadBindButton* b = iPadBindFind(name);
            if (b == NULL)
            {
                return false;
            }
            mask |= b->mask;
        }
        if (mask == 0)
        {
            return false;
        }

        out->at = (S32)at;
        out->frames = (S32)frames;
        out->buttons = mask;
        return true;
    }

    void ParseInput(const char* list)
    {
        static char buf[8192];
        snprintf(buf, sizeof(buf), "%s", list);

        // Entries are split by hand: ParsePress uses strtok for the names.
        char* p = buf;
        while (p != NULL && *p != '\0')
        {
            char* comma = strchr(p, ',');
            if (comma != NULL)
            {
                *comma = '\0';
            }
            while (*p == ' ' || *p == '\t')
            {
                p++;
            }
            if (*p != '\0')
            {
                if (sPressCount == kMaxPresses)
                {
                    printf("tssm: capture: TSSM_INPUT has more than %d entries; the rest are "
                           "ignored\n",
                           (int)kMaxPresses);
                    break;
                }
                if (ParsePress(p, &sPresses[sPressCount]))
                {
                    sPressCount++;
                }
                else
                {
                    printf("tssm: capture: TSSM_INPUT: '%s' is not frame:button[+button][/frames] "
                           "with [pad] button names; ignored\n",
                           p);
                }
            }
            p = comma != NULL ? comma + 1 : NULL;
        }
    }

    // What port 0 holds on this frame: every press whose span covers it.
    void ApplyInput()
    {
        U32 held = 0;
        for (S32 i = 0; i < sPressCount; i++)
        {
            const Press& pr = sPresses[i];
            if (sFrame >= pr.at && sFrame < pr.at + pr.frames)
            {
                held |= pr.buttons;
            }
            if (sFrame == pr.at)
            {
                printf("tssm: capture: frame %d (%.2f s): press %s\n", (int)sFrame,
                       iTimeDiffSec(sFirstFrameAt, iTimeGet()), pr.text);
                fflush(stdout);
            }
        }
        iPadHostScript(TRUE, held);
    }

    // "12", "100-400" or "100-400/50". FALSE on anything else.
    bool ParseRange(const char* text, Range* out)
    {
        char* end;
        long first = strtol(text, &end, 10);
        long last = first;
        long step = 1;

        if (end == text || first < 1)
        {
            return false;
        }
        if (*end == '-')
        {
            const char* p = end + 1;
            last = strtol(p, &end, 10);
            if (end == p || last < first)
            {
                return false;
            }
            if (*end == '/')
            {
                p = end + 1;
                step = strtol(p, &end, 10);
                if (end == p || step < 1)
                {
                    return false;
                }
            }
        }
        if (*end != '\0')
        {
            return false;
        }

        out->first = (S32)first;
        out->last = (S32)last;
        out->step = (S32)step;
        return true;
    }

    void ParseList(const char* list)
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", list);

        for (char* tok = strtok(buf, ", \t"); tok != NULL; tok = strtok(NULL, ", \t"))
        {
            if (sRangeCount == kMaxRanges)
            {
                printf("tssm: capture: TSSM_CAPTURE_FRAMES has more than %d entries; the rest "
                       "are ignored\n",
                       (int)kMaxRanges);
                break;
            }
            if (!ParseRange(tok, &sRanges[sRangeCount]))
            {
                printf("tssm: capture: TSSM_CAPTURE_FRAMES: '%s' is not a frame number or "
                       "range (n, a-b, a-b/step); ignored\n",
                       tok);
                continue;
            }
            sRangeCount++;
        }
    }

    bool Wanted(S32 frame)
    {
        for (S32 i = 0; i < sRangeCount; i++)
        {
            const Range& r = sRanges[i];
            if (frame >= r.first && frame <= r.last && (frame - r.first) % r.step == 0)
            {
                return true;
            }
        }
        return false;
    }

    // Called by iSnapshotCapture once per presented frame, before the flip.
    void OnFrame()
    {
        sFrame++;
        if (sFrame == 1)
        {
            sFirstFrameAt = iTimeGet();
        }

        if (Wanted(sFrame))
        {
            char path[640];
            snprintf(path, sizeof(path), "%sframe_%05d.png", sDir, (int)sFrame);

            const F32 at = iTimeDiffSec(sFirstFrameAt, iTimeGet());
            if (iFrameGrabWrite(path, 0))
            {
                sCaptured++;
                printf("tssm: capture: frame %d (%.2f s) -> %s\n", (int)sFrame, at, path);
            }
            else
            {
                printf("tssm: capture: frame %d (%.2f s): no frame to read, or %s could not be "
                       "written\n",
                       (int)sFrame, at, path);
            }
            fflush(stdout);
        }

        if (sRequested)
        {
            sRequested = false;
            char path[640];
            snprintf(path, sizeof(path), "%s%s", sDir, sRequest);
            if (iFrameGrabWrite(path, 0))
            {
                sCaptured++;
                printf("tssm: capture: frame %d -> %s (requested)\n", (int)sFrame, path);
            }
            fflush(stdout);
        }

        iTestEventsFrame(sFrame);
        iTestSendFrame(sFrame);
        iAudioSDLTestFrame(sFrame);

        if (sScripted)
        {
            // Set after the frame is drawn, so it is what the next one reads.
            ApplyInput();
        }

        if (sExitAfter > 0 && sFrame >= sExitAfter)
        {
            printf("tssm: capture: TSSM_EXIT_AFTER reached at frame %d (%.2f s), %d captured; "
                   "exiting\n",
                   (int)sFrame, iTimeDiffSec(sFirstFrameAt, iTimeGet()), (int)sCaptured);
            iLowHeapReport("at exit");
            fflush(stdout);
            exit(0);
        }
    }
} // namespace

void iCaptureInit()
{
    const char* frames = getenv("TSSM_CAPTURE_FRAMES");
    const char* exitAfter = getenv("TSSM_EXIT_AFTER");
    const char* input = getenv("TSSM_INPUT");

    if (frames != NULL && frames[0] != '\0')
    {
        ParseList(frames);
    }
    if (input != NULL && input[0] != '\0')
    {
        // Scripted even when every entry was rejected: the run still should
        // not take input from whatever is plugged in.
        ParseInput(input);
        sScripted = true;
        iPadHostScript(TRUE, 0);
    }
    if (exitAfter != NULL && exitAfter[0] != '\0')
    {
        sExitAfter = atoi(exitAfter);
        if (sExitAfter < 1)
        {
            printf("tssm: capture: TSSM_EXIT_AFTER '%s' is not a frame count; ignored\n",
                   exitAfter);
            sExitAfter = 0;
        }
    }

    const char* testWarp = getenv("TSSM_TEST_WARP");
    const char* testPlace = getenv("TSSM_TEST_PLACE");
    const char* testCrash = getenv("TSSM_TEST_CRASH");
    const char* testCamLog = getenv("TSSM_TEST_CAMLOG");
    const char* testSend = getenv("TSSM_TEST_SEND");

    // TSSM_TEST_AUDIO_RESTART (iAudioSDLTest.h) counts its frames here too.
    iAudioSDLTestInit();

    const bool tests = (testWarp != NULL && testWarp[0] != '\0') ||
                       (testPlace != NULL && testPlace[0] != '\0') ||
                       (testCrash != NULL && atoi(testCrash) > 1) ||
                       (testCamLog != NULL && atoi(testCamLog) > 0) ||
                       (testSend != NULL && testSend[0] != '\0') || iAudioSDLTestOn();

    if (sRangeCount == 0 && sExitAfter == 0 && !sScripted && !tests)
    {
        return;
    }

    const char* dir = getenv("TSSM_CAPTURE_DIR");
    if (dir != NULL && dir[0] != '\0')
    {
        const size_t n = strlen(dir);
        const bool slash = dir[n - 1] == '/' || dir[n - 1] == '\\';
        snprintf(sDir, sizeof(sDir), "%s%s", dir, slash ? "" : "/");
    }

    iSnapshotSetFrameHook(OnFrame);
    sHooked = true;

    printf("tssm: capture: %d frame range(s) into %s; exit after %s; %s\n", (int)sRangeCount,
           sDir[0] != '\0' ? sDir : "the working directory",
           sExitAfter > 0 ? exitAfter : "never",
           sScripted ? "scripted input, real controllers ignored" : "live input");
    fflush(stdout);
}

S32 iCaptureFrameCount()
{
    return sFrame;
}

bool iCaptureRequest(const char* name)
{
    // A plain file name of the caller's own making, written into
    // TSSM_CAPTURE_DIR; only while the capture hook runs (a test run).
    if (!sHooked || name == NULL || strlen(name) >= sizeof(sRequest) || strchr(name, '/') != NULL ||
        strchr(name, '\\') != NULL || strstr(name, "..") != NULL)
    {
        return false;
    }
    snprintf(sRequest, sizeof(sRequest), "%s", name);
    sRequested = true;
    return true;
}
