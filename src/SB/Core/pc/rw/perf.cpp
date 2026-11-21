// The frame metrics (TSSM_PERF); see iPerf.h. Port-only.
//
// In the shim because the frame's boundaries are here (rw/camera.cpp) and the
// GPU counters are librw's (gl3perf.inc). Plain C library only, as the rest of
// the shim: no SDL, no platform layer.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(__MINGW32__)
// __rdtsc. <intrin.h> is compat/intrin.h on this include path; clang under the
// MSVC ABI has __rdtsc built in, GCC declares it in <x86intrin.h>.
#include <x86intrin.h>
#else
#include <intrin.h>
#endif
#endif

#include "rw.h"

#include "backend.h"
#include "iPerf.h"
#include "iPerfSampler.h"
#include "iWindow.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Each thread's CPU time is read from /proc/self/task, which the Switch has not.
#if !defined(_WIN32) && !defined(__SWITCH__)
#define TSSM_PERF_THREADS
#endif

namespace
{
    // --- clocks ---------------------------------------------------------------

#if defined(_WIN32)
    double sQpcToNs;
    double sTscPerNs;

    void ClockInit()
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        sQpcToNs = 1.0e9 / (double)f.QuadPart;

        // QueryThreadCycleTime counts at the TSC's rate; measure it once.
        LARGE_INTEGER q0, q1;
        QueryPerformanceCounter(&q0);
        const unsigned long long t0 = __rdtsc();
        Sleep(30);
        QueryPerformanceCounter(&q1);
        const unsigned long long t1 = __rdtsc();
        sTscPerNs = (double)(t1 - t0) / ((double)(q1.QuadPart - q0.QuadPart) * sQpcToNs);
    }

    double WallNs()
    {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return (double)c.QuadPart * sQpcToNs;
    }

    double CpuNs()
    {
        ULONG64 cycles = 0;
        QueryThreadCycleTime(GetCurrentThread(), &cycles);
        return (double)cycles / sTscPerNs;
    }
#else
    void ClockInit()
    {
    }

    double WallNs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1.0e9 + ts.tv_nsec;
    }

    double CpuNs()
    {
#ifdef CLOCK_THREAD_CPUTIME_ID
        struct timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return ts.tv_sec * 1.0e9 + ts.tv_nsec;
#else
        // newlib on the Switch has no per-thread clock: the columns read as wall time there.
        return WallNs();
#endif
    }
#endif

    // --- one frame ------------------------------------------------------------

    enum Phase
    {
        PH_UPDATE,
        PH_RENDER,
        PH_PRESENT,
        PH_SWAP,
        PH_PACE,
        PH_COUNT
    };

    const char* const kPhaseNames[PH_COUNT] = { "update", "render", "present", "swap", "pace" };

    struct Row
    {
        U32 frame;
        double tMs;
        float interval;
        S32 rate;
        S32 missed;
        float wall[PH_COUNT];
        float cpu[PH_COUNT];
        float gpu; // -1: none
        S32 cameras;
        bool gpuDone;
#ifdef RW_GL3
        rw::gl3::PerfCounts counts;
#endif
        // The host's side (iPerf.h): event pumps, the controllers' read, the pacer.
        S32 pumps;
        float pumpMs;
        float pumpMaxMs;
        float padMs;
        float inputAgeMs;
        float paceWantMs;
        float paceLateMs;
    };

    struct Mark
    {
        double wall;
        double cpu;
    };

    S32 sState = -1; // -1 not yet looked at, 0 off, 1 on
    double sWindowSec = 5.0;
    FILE* sCsv;
    char sCsvPath[512];
    bool sGl3;
    bool sGpuTimers;

    U32 sFrame; // frames measured so far
    bool sInFrame; // a frame's start has been marked
    bool sRenderSeen; // this frame's first BeginUpdate has been seen
    S32 sCameras;
    Mark sMark[PH_COUNT + 1]; // the start of each phase, then the frame's end
    double sFirstSwapEnd;
    double sLastSwapEnd;
    Row sCurrent;

    // The host's side of the frame in progress, reset at each frame's start.
    S32 sPumps;
    double sPumpNs, sPumpMaxNs;
    double sPumpStart; // 0 outside a pump
    double sLastPumpEnd; // across frames: the input's age is from the last pump
    double sPadAt; // 0 until the controllers are read this frame
    double sInputAge;
    double sPaceWantNs, sPaceLateNs;

    // Rows wait here for their GPU time, which arrives a few frames late.
    const int kPending = 32;
    const U32 kGpuWait = 12; // frames to wait for a GPU time before giving up
    Row sPending[kPending];
    int sPendHead, sPendCount;

    // --- the summary window ---------------------------------------------------

    const int kWindowMax = 16384;
    float* sIntervals;
    float* sGpuTimes;
    int sWinFrames, sWinGpu;
    double sWinStartMs;
    double sWinWall[PH_COUNT], sWinCpu[PH_COUNT];
    double sWinCounts[10];
    double sWinCameras;
    int sWinMissed;
    S32 sWinRate;
    Row sWorst[3];
    int sWorstCount;

#ifdef TSSM_PERF_THREADS
    // Every thread's CPU time, from /proc/self/task, for the summary.
    struct ThreadTicks
    {
        int tid;
        unsigned long long ticks;
        bool seen;
    };
    const int kThreads = 256;
    ThreadTicks sThreads[kThreads];
    int sThreadCount;
    double sThreadsAtMs;
    FILE* sThreadCsv;
#endif

    void Now(Mark* m)
    {
        m->wall = WallNs();
        m->cpu = CpuNs();
    }

    void SetPhase(int phase)
    {
        iPerfSamplerSetPhase(phase);
    }

    // The default place for the files: Android's private directory, the
    // working directory elsewhere.
    void DefaultPath(const char* name, char* out, size_t size)
    {
#ifdef __ANDROID__
        const char* internal = getenv("TSSM_ANDROID_INTERNAL");
        if (internal != NULL && internal[0] != '\0')
        {
            char dir[512];
            snprintf(dir, sizeof(dir), "%s/perf-test", internal);
            struct stat st;
            if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode))
            {
                snprintf(out, size, "%s/%s", dir, name);
            }
            else
            {
                snprintf(out, size, "%s/%s", internal, name);
            }
            return;
        }
#endif
        snprintf(out, size, "%s", name);
    }

    void SiblingPath(const char* of, const char* name, char* out, size_t size)
    {
        const char* slash = strrchr(of, '/');
        const char* back = strrchr(of, '\\');
        if (back != NULL && (slash == NULL || back > slash))
        {
            slash = back;
        }
        if (slash == NULL)
        {
            snprintf(out, size, "%s", name);
        }
        else
        {
            snprintf(out, size, "%.*s%s", (int)(slash - of + 1), of, name);
        }
    }

    int CmpFloat(const void* a, const void* b)
    {
        const float x = *(const float*)a, y = *(const float*)b;
        return x < y ? -1 : x > y;
    }

    void ResetWindow(double atMs)
    {
        sWinFrames = 0;
        sWinGpu = 0;
        sWinStartMs = atMs;
        memset(sWinWall, 0, sizeof(sWinWall));
        memset(sWinCpu, 0, sizeof(sWinCpu));
        memset(sWinCounts, 0, sizeof(sWinCounts));
        sWinCameras = 0;
        sWinMissed = 0;
        sWorstCount = 0;
    }

#ifdef TSSM_PERF_THREADS
    void ThreadsReport(double atMs)
    {
        static long hz = 0;
        if (hz == 0)
        {
            hz = sysconf(_SC_CLK_TCK);
            if (hz <= 0)
            {
                hz = 100;
            }
        }
        const double spanS = (atMs - sThreadsAtMs) / 1000.0;
        DIR* d = opendir("/proc/self/task");
        if (d == NULL)
        {
            return;
        }
        for (int i = 0; i < sThreadCount; i++)
        {
            sThreads[i].seen = false;
        }
        struct Top
        {
            char name[32];
            int tid;
            double pct;
        } top[6];
        int topCount = 0;
        struct dirent* e;
        while ((e = readdir(d)) != NULL)
        {
            const int tid = atoi(e->d_name);
            if (tid <= 0)
            {
                continue;
            }
            char path[64], buf[512];
            snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
            FILE* f = fopen(path, "r");
            if (f == NULL)
            {
                continue;
            }
            const size_t got = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            buf[got] = '\0';
            char* open = strchr(buf, '(');
            char* close = strrchr(buf, ')');
            if (open == NULL || close == NULL)
            {
                continue;
            }
            char name[32];
            snprintf(name, sizeof(name), "%.*s", (int)(close - open - 1), open + 1);
            for (char* c = name; *c; c++)
            {
                if (*c == ' ' || *c == ',')
                {
                    *c = '_';
                }
            }
            // After ") ": state(3) ... utime(14) stime(15) ... processor(39).
            unsigned long long ut = 0, st = 0;
            int cpu = -1;
            char* p = close + 2;
            int field = 3;
            for (char* tok = strtok(p, " "); tok != NULL; tok = strtok(NULL, " "), field++)
            {
                if (field == 14)
                {
                    ut = strtoull(tok, NULL, 10);
                }
                else if (field == 15)
                {
                    st = strtoull(tok, NULL, 10);
                }
                else if (field == 39)
                {
                    cpu = atoi(tok);
                    break;
                }
            }
            const unsigned long long ticks = ut + st;
            int slot = -1;
            for (int i = 0; i < sThreadCount; i++)
            {
                if (sThreads[i].tid == tid)
                {
                    slot = i;
                    break;
                }
            }
            unsigned long long before = 0;
            bool known = slot >= 0;
            if (slot < 0 && sThreadCount < kThreads)
            {
                slot = sThreadCount++;
                sThreads[slot].tid = tid;
            }
            if (slot >= 0)
            {
                before = sThreads[slot].ticks;
                sThreads[slot].ticks = ticks;
                sThreads[slot].seen = true;
            }
            if (!known || spanS <= 0.0)
            {
                continue;
            }
            const double pct = (double)(ticks - before) / (double)hz / spanS * 100.0;
            if (sThreadCsv != NULL && pct > 0.0)
            {
                fprintf(sThreadCsv, "%.3f,%d,%s,%.1f,%d\n", atMs / 1000.0, tid, name, pct, cpu);
            }
            // The six busiest.
            int at = topCount;
            while (at > 0 && top[at - 1].pct < pct)
            {
                at--;
            }
            if (at < 6)
            {
                const int last = topCount < 6 ? topCount : 5;
                for (int i = last; i > at; i--)
                {
                    top[i] = top[i - 1];
                }
                snprintf(top[at].name, sizeof(top[at].name), "%s", name);
                top[at].tid = tid;
                top[at].pct = pct;
                if (topCount < 6)
                {
                    topCount++;
                }
            }
        }
        closedir(d);
        sThreadsAtMs = atMs;
        if (topCount > 0)
        {
            char line[512];
            int len = snprintf(line, sizeof(line), "tssm: perf: threads (%% of a core):");
            for (int i = 0; i < topCount && len < (int)sizeof(line); i++)
            {
                len += snprintf(line + len, sizeof(line) - len, " %s/%d %.0f%%", top[i].name,
                                top[i].tid, top[i].pct);
            }
            printf("%s\n", line);
        }
        if (sThreadCsv != NULL)
        {
            fflush(sThreadCsv);
        }
    }
#endif

    void Summary(double atMs)
    {
        const int n = sWinFrames;
        if (n == 0)
        {
            return;
        }
        const double span = (atMs - sWinStartMs) / 1000.0;
        qsort(sIntervals, n, sizeof(float), CmpFloat);
        double sum = 0.0;
        for (int i = 0; i < n; i++)
        {
            sum += sIntervals[i];
        }
        // The rate over the slowest 1% (0.1%) of the frames, at least one.
        int k1 = n / 100, k01 = n / 1000;
        k1 = k1 < 1 ? 1 : k1;
        k01 = k01 < 1 ? 1 : k01;
        double s1 = 0.0, s01 = 0.0;
        for (int i = 0; i < k1; i++)
        {
            s1 += sIntervals[n - 1 - i];
        }
        for (int i = 0; i < k01; i++)
        {
            s01 += sIntervals[n - 1 - i];
        }
        double cpuTotal = 0.0;
        for (int p = 0; p < PH_COUNT; p++)
        {
            cpuTotal += sWinCpu[p];
        }
        printf("tssm: perf: %.1f s, %d frames, %.1f fps (paced %d), interval avg %.2f p50 %.2f "
               "p99 %.2f max %.2f ms, missed %d, 1%% low %.1f fps, 0.1%% low %.1f fps\n",
               span, n, n / (span > 0 ? span : 1), (int)sWinRate, sum / n, sIntervals[n / 2],
               sIntervals[(n * 99) / 100 < n ? (n * 99) / 100 : n - 1], sIntervals[n - 1],
               sWinMissed, 1000.0 * k1 / s1, 1000.0 * k01 / s01);
        printf("tssm: perf:   cpu ms update %.2f render %.2f present %.2f swap %.2f (wall %.2f) "
               "pace %.2f (wall %.2f), total %.2f = %.0f%% of a core\n",
               sWinCpu[PH_UPDATE] / n, sWinCpu[PH_RENDER] / n, sWinCpu[PH_PRESENT] / n,
               sWinCpu[PH_SWAP] / n, sWinWall[PH_SWAP] / n, sWinCpu[PH_PACE] / n,
               sWinWall[PH_PACE] / n, cpuTotal / n, span > 0 ? cpuTotal / 10.0 / span : 0.0);
        if (sGl3)
        {
            char gpu[96] = "no GPU timer";
            if (sWinGpu > 0)
            {
                qsort(sGpuTimes, sWinGpu, sizeof(float), CmpFloat);
                double g = 0.0;
                for (int i = 0; i < sWinGpu; i++)
                {
                    g += sGpuTimes[i];
                }
                snprintf(gpu, sizeof(gpu), "gpu ms avg %.2f p95 %.2f max %.2f (%d frames)",
                         g / sWinGpu, sGpuTimes[(sWinGpu * 95) / 100], sGpuTimes[sWinGpu - 1],
                         sWinGpu);
            }
            printf("tssm: perf:   %s; per frame: draws %.0f, tris %.0f, passes %.1f (screen %.1f), "
                   "blits %.1f, clears %.1f, programs %.1f, texture uploads %.2f, buffer uploads "
                   "%.0f (%.0f KB), cameras %.1f\n",
                   gpu, sWinCounts[0] / n, sWinCounts[1] / n, sWinCounts[2] / n, sWinCounts[3] / n,
                   sWinCounts[4] / n, sWinCounts[5] / n, sWinCounts[6] / n, sWinCounts[7] / n,
                   sWinCounts[8] / n, sWinCounts[9] / n / 1024.0, sWinCameras / n);
        }
        for (int i = 0; i < sWorstCount; i++)
        {
            const Row& r = sWorst[i];
            printf("tssm: perf:   worst %d: frame %u, %.2f ms; wall/cpu ms update %.2f/%.2f render "
                   "%.2f/%.2f present %.2f/%.2f swap %.2f/%.2f pace %.2f/%.2f, gpu %.2f\n",
                   i + 1, (unsigned)r.frame, r.interval, r.wall[0], r.cpu[0], r.wall[1], r.cpu[1],
                   r.wall[2], r.cpu[2], r.wall[3], r.cpu[3], r.wall[4], r.cpu[4], r.gpu);
        }
#ifdef TSSM_PERF_THREADS
        ThreadsReport(atMs);
#endif
        fflush(stdout);
        if (sCsv != NULL)
        {
            fflush(sCsv);
        }
        ResetWindow(atMs);
    }

    void Emit(const Row& r)
    {
        if (sCsv != NULL)
        {
            fprintf(sCsv, "%u,%.3f,%.3f,%d,%d", (unsigned)r.frame, r.tMs, r.interval, (int)r.rate,
                    (int)r.missed);
            for (int p = 0; p < PH_COUNT; p++)
            {
                fprintf(sCsv, ",%.3f,%.3f", r.wall[p], r.cpu[p]);
            }
            fprintf(sCsv, ",%.3f,%d", r.gpu, (int)r.cameras);
#ifdef RW_GL3
            const rw::gl3::PerfCounts& c = r.counts;
            fprintf(sCsv, ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%.1f,%u,%.1f", c.draws, c.tris, c.passes,
                    c.screenPasses, c.blits, c.clears, c.programs, c.textureBinds, c.texUploads,
                    c.texBytes / 1024.0, c.bufUploads, c.bufBytes / 1024.0);
#else
            fprintf(sCsv, ",,,,,,,,,,,,");
#endif
            fprintf(sCsv, ",%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", (int)r.pumps, r.pumpMs, r.pumpMaxMs,
                    r.padMs, r.inputAgeMs, r.paceWantMs, r.paceLateMs);
        }

        // The window.
        if (sWinFrames < kWindowMax)
        {
            sIntervals[sWinFrames] = r.interval;
        }
        sWinFrames++;
        if (r.gpu >= 0.0f && sWinGpu < kWindowMax)
        {
            sGpuTimes[sWinGpu++] = r.gpu;
        }
        for (int p = 0; p < PH_COUNT; p++)
        {
            sWinWall[p] += r.wall[p];
            sWinCpu[p] += r.cpu[p];
        }
        sWinCameras += r.cameras;
        sWinMissed += r.missed;
        sWinRate = r.rate;
#ifdef RW_GL3
        sWinCounts[0] += r.counts.draws;
        sWinCounts[1] += r.counts.tris;
        sWinCounts[2] += r.counts.passes;
        sWinCounts[3] += r.counts.screenPasses;
        sWinCounts[4] += r.counts.blits;
        sWinCounts[5] += r.counts.clears;
        sWinCounts[6] += r.counts.programs;
        sWinCounts[7] += r.counts.texUploads;
        sWinCounts[8] += r.counts.bufUploads;
        sWinCounts[9] += r.counts.bufBytes;
#endif
        // The three slowest.
        int at = sWorstCount;
        while (at > 0 && sWorst[at - 1].interval < r.interval)
        {
            at--;
        }
        if (at < 3)
        {
            const int last = sWorstCount < 3 ? sWorstCount : 2;
            for (int i = last; i > at; i--)
            {
                sWorst[i] = sWorst[i - 1];
            }
            sWorst[at] = r;
            if (sWorstCount < 3)
            {
                sWorstCount++;
            }
        }

        if (sWinFrames > kWindowMax || (r.tMs - sWinStartMs) >= sWindowSec * 1000.0)
        {
            if (sWinFrames > kWindowMax)
            {
                sWinFrames = kWindowMax;
            }
            Summary(r.tMs);
        }
    }

    // Rows whose GPU time came, or will not come, leave in order.
    void Drain(bool all)
    {
        while (sPendCount > 0)
        {
            Row& r = sPending[sPendHead];
            if (!all && !r.gpuDone && sGpuTimers && sFrame - r.frame < kGpuWait)
            {
                break;
            }
            Emit(r);
            sPendHead = (sPendHead + 1) % kPending;
            sPendCount--;
        }
    }

    void PollGpu()
    {
#ifdef RW_GL3
        U32 frame;
        float ms;
        while (rw::gl3::perfGpuPoll(&frame, &ms))
        {
            for (int i = 0; i < sPendCount; i++)
            {
                Row& r = sPending[(sPendHead + i) % kPending];
                if (r.frame == frame)
                {
                    r.gpu = ms;
                    r.gpuDone = true;
                    break;
                }
            }
        }
#endif
    }

    void AtExit()
    {
        if (sState != 1)
        {
            return;
        }
        Drain(true);
        if (sWinFrames > 0 && sLastSwapEnd > 0.0)
        {
            Summary((sLastSwapEnd - sFirstSwapEnd) / 1.0e6);
        }
        if (sCsv != NULL)
        {
            fclose(sCsv);
            sCsv = NULL;
        }
#ifdef TSSM_PERF_THREADS
        if (sThreadCsv != NULL)
        {
            fclose(sThreadCsv);
            sThreadCsv = NULL;
        }
#endif
        printf("tssm: perf: %u frames measured; %s\n", (unsigned)sFrame, sCsvPath);
        fflush(stdout);
    }

    void Init()
    {
        sState = 0;
        const char* on = getenv("TSSM_PERF");
        if (on == NULL || on[0] == '\0' || atof(on) <= 0.0)
        {
            return;
        }
        sWindowSec = atof(on);
        ClockInit();

        sIntervals = (float*)malloc(sizeof(float) * kWindowMax);
        sGpuTimes = (float*)malloc(sizeof(float) * kWindowMax);
        if (sIntervals == NULL || sGpuTimes == NULL)
        {
            printf("tssm: perf: no memory; off\n");
            return;
        }

        const char* csv = getenv("TSSM_PERF_CSV");
        if (csv != NULL && csv[0] != '\0')
        {
            snprintf(sCsvPath, sizeof(sCsvPath), "%s", csv);
        }
        else
        {
            DefaultPath("perf.csv", sCsvPath, sizeof(sCsvPath));
        }
        sCsv = fopen(sCsvPath, "w");
        if (sCsv == NULL)
        {
            printf("tssm: perf: could not open %s; the summary lines only\n", sCsvPath);
        }
        else
        {
            static char buf[1 << 16];
            setvbuf(sCsv, buf, _IOFBF, sizeof(buf));
        }

        sGl3 = false;
        char device[512] = "";
#ifdef RW_GL3
        if (iBackendIsGL3() && rw::gl3::perfStart())
        {
            sGl3 = true;
            sGpuTimers = rw::gl3::perfTimers() != 0;
            rw::gl3::PerfInfo info;
            rw::gl3::perfInfo(&info);
            snprintf(device, sizeof(device),
                     "%s; %s; render %dx%d, %d of %d samples; window %dx%d; GPU timer %s",
                     info.renderer ? info.renderer : "?", info.version ? info.version : "?",
                     (int)info.screenWidth, (int)info.screenHeight, (int)info.samples,
                     (int)info.samplesWanted, (int)info.windowWidth, (int)info.windowHeight,
                     info.timers ? "yes" : "no");
        }
#endif
        if (device[0] == '\0')
        {
            snprintf(device, sizeof(device), "%s backend: CPU columns only",
                     iScreenBackendName(iScreenGetBackend()));
        }

        if (sCsv != NULL)
        {
            fprintf(sCsv, "# tssm perf 1\n# backend %s\n# device %s\n# vsync %d, paced %d\n",
                    iScreenBackendName(iScreenGetBackend()), device, (int)iWindowGetVSync(),
                    (int)iWindowGetPacedRate());
            fprintf(sCsv, "frame,t_ms,interval_ms,rate,missed");
            for (int p = 0; p < PH_COUNT; p++)
            {
                fprintf(sCsv, ",%s_wall,%s_cpu", kPhaseNames[p], kPhaseNames[p]);
            }
            fprintf(sCsv, ",gpu_ms,cameras,draws,tris,passes,screen_passes,blits,clears,programs,"
                          "tex_binds,tex_uploads,tex_kb,buf_uploads,buf_kb,pumps,pump_ms,pump_max_ms,"
                          "pad_ms,input_age_ms,pace_want_ms,pace_late_ms\n");
        }

#ifdef TSSM_PERF_THREADS
        {
            char path[512];
            SiblingPath(sCsvPath, "perf-threads.csv", path, sizeof(path));
            sThreadCsv = fopen(path, "w");
            if (sThreadCsv != NULL)
            {
                fprintf(sThreadCsv, "t_s,tid,name,cpu_pct,last_cpu\n");
            }
        }
#endif

        printf("tssm: perf: on, a summary every %.1f s, frames to %s; %s\n", sWindowSec, sCsvPath,
               device);

        const char* prof = getenv("TSSM_PERF_PROFILE");
        if (prof != NULL && atoi(prof) > 0)
        {
            char out[512];
            const char* o = getenv("TSSM_PERF_PROFILE_OUT");
            if (o != NULL && o[0] != '\0')
            {
                snprintf(out, sizeof(out), "%s", o);
            }
            else
            {
                SiblingPath(sCsvPath, "perf-samples.txt", out, sizeof(out));
            }
            iPerfSamplerStart(atoi(prof), out);
        }
        fflush(stdout);

        atexit(AtExit);
        sState = 1;
    }

    // The frame's start: the end of the previous frame's pacing.
    void BeginFrame(const Mark& at)
    {
        sMark[PH_UPDATE] = at;
        sInFrame = true;
        sRenderSeen = false;
        sCameras = 0;
        sPumps = 0;
        sPumpNs = 0.0;
        sPumpMaxNs = 0.0;
        sPadAt = 0.0;
        sInputAge = 0.0;
        sPaceWantNs = 0.0;
        sPaceLateNs = 0.0;
        SetPhase(PH_UPDATE);
    }
} // namespace

S32 iPerfOn()
{
    return sState == 1;
}

void iPerfRenderBegin()
{
    if (sState != 1)
    {
        return;
    }
    sCameras++;
    if (sInFrame && !sRenderSeen)
    {
        sRenderSeen = true;
        Now(&sMark[PH_RENDER]);
        SetPhase(PH_RENDER);
#ifdef RW_GL3
        // The GPU time is from here to the swap: not the pacer's sleep, nor
        // the update, before it.
        if (sGl3)
        {
            rw::gl3::perfFrameBegin();
        }
#endif
    }
}

void iPerfPresentBegin()
{
    if (sState < 0)
    {
        Init();
    }
    if (sState != 1)
    {
        return;
    }
    Now(&sMark[PH_PRESENT]);
    if (!sRenderSeen)
    {
        // Nothing was drawn through a camera (a movie, a blank frame): no
        // render phase.
        sMark[PH_RENDER] = sMark[PH_PRESENT];
    }
    SetPhase(PH_PRESENT);
}

void iPerfSwapBegin()
{
    if (sState != 1)
    {
        return;
    }
    Now(&sMark[PH_SWAP]);
    SetPhase(PH_SWAP);
}

void iPerfSwapEnd()
{
    if (sState != 1)
    {
        return;
    }
    Now(&sMark[PH_PACE]);
    SetPhase(PH_PACE);

    const double now = sMark[PH_PACE].wall;
    if (sFirstSwapEnd == 0.0)
    {
        sFirstSwapEnd = now;
        sLastSwapEnd = now;
        ResetWindow(0.0);
#ifdef TSSM_PERF_THREADS
        ThreadsReport(0.0);
#endif
    }
    Row& r = sCurrent;
    memset(&r, 0, sizeof(r));
    r.frame = ++sFrame;
    r.tMs = (now - sFirstSwapEnd) / 1.0e6;
    r.interval = (float)((now - sLastSwapEnd) / 1.0e6);
    sLastSwapEnd = now;
    r.rate = iWindowGetPacedRate();
    if (r.rate > 0 && sFrame > 1)
    {
        const S32 periods = (S32)floor(r.interval * r.rate / 1000.0 + 0.5);
        r.missed = periods > 1 ? periods - 1 : 0;
    }
    r.gpu = -1.0f;
    r.cameras = sCameras;
#ifdef RW_GL3
    if (sGl3)
    {
        rw::gl3::perfFrame(sFrame, &r.counts);
    }
#endif
}

void iPerfFrameEnd()
{
    if (sState != 1)
    {
        return;
    }
    Mark end;
    Now(&end);
    sMark[PH_COUNT] = end;

    Row& r = sCurrent;
    if (sInFrame && r.frame == sFrame && sFrame > 1)
    {
        for (int p = 0; p < PH_COUNT; p++)
        {
            r.wall[p] = (float)((sMark[p + 1].wall - sMark[p].wall) / 1.0e6);
            r.cpu[p] = (float)((sMark[p + 1].cpu - sMark[p].cpu) / 1.0e6);
            if (r.wall[p] < 0.0f)
            {
                r.wall[p] = 0.0f;
            }
            if (r.cpu[p] < 0.0f)
            {
                r.cpu[p] = 0.0f;
            }
        }
        r.pumps = sPumps;
        r.pumpMs = (float)(sPumpNs / 1.0e6);
        r.pumpMaxMs = (float)(sPumpMaxNs / 1.0e6);
        r.padMs = sPadAt > 0.0 ? (float)((sPadAt - sMark[PH_UPDATE].wall) / 1.0e6) : -1.0f;
        r.inputAgeMs = sPadAt > 0.0 ? (float)(sInputAge / 1.0e6) : -1.0f;
        r.paceWantMs = (float)(sPaceWantNs / 1.0e6);
        r.paceLateMs = (float)(sPaceLateNs / 1.0e6);
        if (sPendCount == kPending)
        {
            Drain(true);
        }
        sPending[(sPendHead + sPendCount) % kPending] = r;
        sPendCount++;
    }
    if (sGl3)
    {
        PollGpu();
    }
    Drain(false);
    BeginFrame(end);
}

void iPerfPumpBegin()
{
    if (sState != 1)
    {
        return;
    }
    sPumpStart = WallNs();
}

void iPerfPumpEnd()
{
    if (sState != 1 || sPumpStart == 0.0)
    {
        return;
    }
    const double end = WallNs();
    const double ns = end - sPumpStart;
    sPumps++;
    sPumpNs += ns;
    if (ns > sPumpMaxNs)
    {
        sPumpMaxNs = ns;
    }
    sPumpStart = 0.0;
    sLastPumpEnd = end;
}

void iPerfInputRead()
{
    if (sState != 1 || sPadAt != 0.0)
    {
        return;
    }
    sPadAt = WallNs();
    sInputAge = sLastPumpEnd > 0.0 ? sPadAt - sLastPumpEnd : 0.0;
}

void iPerfPace(U64 wantNs, S64 lateNs)
{
    if (sState != 1)
    {
        return;
    }
    sPaceWantNs = (double)wantNs;
    sPaceLateNs = (double)lateNs;
}
