// The 3DS speed test; see iSpike.h. Port-only. On a PC it runs the same
// levels with the same script, for the calibration line.

#include "iSpike.h"

#include "iCapture.h"
#include "iD0Stats.h"
#include "iHost.h"
#include "iPadBind.h"
#include "iSndHost.h"
#include "iTime.h"
#include "xSnd.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zPortal.h"
#include "zScene.h"

#ifdef __3DS__
#include "3ds/i3DS.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TSSM_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

#include <algorithm>
#include <vector>

namespace
{
    // ---------------------------------------------------------------- settings

    // The speed test's scripted controller, version 4, at 30 frames a second:
    // skip the opening
    // cutscene or fly-through, close the task menu some levels open, then walk,
    // jump, turn and attack for a minute.
    const char kScript30[] =
        "90:a/6,135:a/6,180:b/6,210:b/6,240:b/6,270:b/6,300:b/6,330:b/6,360:b/6,420:up/120,"
        "480:a/9,570:right/60,645:b/6,690:up/120,720:a/9,780:a/9,840:left/90,945:b/6,990:x/6,"
        "1020:x/6,1050:down/90,1155:up/180,1200:a/9,1260:a/9,1344:b/6,1380:right/150,1440:a/9,"
        "1545:b/6,1590:up/150,1650:a/9,1755:b/6";
    const char kScriptName[] = "speed test script v4";

    // The game's time step: a 30 fps display's.
    const U64 kStepNs = 1000000000ULL / 30;
    const S32 kFps = 30;
    // Frames timed from 14 s of a level on, as in the 3DS measurements: before that the level is
    // still in its opening cutscene or fly-through.
    const S32 kSkipFrames = 14 * kFps;

    const char kDefaultLevels[] = "BB02,TR01,GG02,JK01";
    const S32 kMaxLevels = 8;

    // The same run on the PC this was calibrated against: Ryzen 9 5900XT, the
    // port's 32-bit Release build (clang -O3) with the NULL renderer, this
    // spike's own levels and script.
    // Work per frame, microseconds: median, p95; each the median of three
    // runs, frames back to back as on the console.
    struct Calibration
    {
        const char* id;
        S32 medUs;
        S32 p95Us;
    };
    const Calibration kPc[] = {
        { "BB02", 300, 370 },
        { "TR01", 530, 710 },
        { "GG02", 330, 380 },
        { "JK01", 390, 430 },
    };
    const char kPcWhat[] =
        "Ryzen 9 5900XT, the port's 32-bit Release build (clang -O3), NULL renderer, this spike's "
        "levels and script, frames back to back; median of 3 runs. Its movie test: Story1.bik "
        "decodes in 0.26 ms a frame";

    // ------------------------------------------------------------------ state

    struct Stats
    {
        S32 n;
        double minMs;
        double medMs;
        double p90Ms;
        double p95Ms;
        double maxMs;
        double meanMs;
    };

    struct Level
    {
        char id[8];
        bool started;
        bool loaded;
        bool finished;
        char note[160];

        double loadMs; // raw wall time, switch to first game-loop frame
        double d0LoadMs;
        double d0OverheadMs;
        double fileMs;
        double fileMB;

        S32 loopFrames;
        S32 playFrames;
        std::vector<S32> work;
        std::vector<S32> logic;
        std::vector<S32> render;
        S64 sumMeshes;
        S32 maxMeshes;
        S64 sumTris;
        S32 maxTris;
        S32 statFrames;

        iD0ScenePeaks peaks;
        U32 mallocUsedMax;
        U32 mallocArena;
        U32 linearFreeMin;
        U32 appFreeMin;

        bool mixOk;
        iSndHostMixStats mix;
        double wallS; // the level's frames, start to end
    };

    enum Stage
    {
        STAGE_PROBE,
        STAGE_LOAD,
        STAGE_RUN,
        STAGE_LEAVE,
        STAGE_DONE
    };

    S32 sOn = -1;
    char sDir[400];
    char sReport[400];
    char sAssets[512];

    Level sLevels[kMaxLevels];
    S32 sLevelCount;
    S32 sSeconds = 60;
    S32 sSdMB = 64;
    S32 sVolume = 50;
    // The RenderWare heap (iLowHeap.h), so its peak can be measured.
    S32 sRwHeap = 1;
    // The movie the decode test plays, under the game's FMV folder, and how
    // much of it.
    char sMovie[64] = "Story1.bik";
    S32 sMovieSeconds = 10;

    Stage sStage = STAGE_PROBE;
    S32 sCur;
    U64 sLoadStartNs;
    U64 sRunStartNs;
    S32 sLeaveFrames;
    U64 sStartNs;

    // The probes' findings, for the report.
    char sConsoleText[2048];
    char sThreadText[1024];
    char sSdText[1024];
    double sSdMBps = -1.0;
    char sMovieText[512];
    // The thread probe on the final screen: y, n, mv (moved: the thread ran
    // on another core) or ? (on which core, the console did not say). Two
    // letters at most, so "c2 mv c1 mv" still fits the 25 columns.
    const char* sCore2 = "?";
    const char* sCore1 = "?";
    bool sFast;

    // Files the run opened, relative to the game's folder.
    struct Opened
    {
        char path[96];
        long length;
    };
    Opened sOpened[160];
    S32 sOpenedCount;

    _zPortal sPortal;
    xPortalAsset sPortalAsset;

    // ---------------------------------------------------------------- helpers

    double NowMs()
    {
        return (double)iHostMonotonicNs() / 1e6;
    }

    void Append(std::vector<char>& out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    void Append(std::vector<char>& out, const char* fmt, ...)
    {
        char line[1024];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        if (n <= 0)
        {
            return;
        }
        if (n >= (int)sizeof(line))
        {
            n = (int)sizeof(line) - 1;
        }
        if (!out.empty() && out.back() == '\0')
        {
            out.pop_back();
        }
        out.insert(out.end(), line, line + n);
        out.push_back('\0');
    }

    void Join(char* out, size_t size, const char* a, const char* b)
    {
        snprintf(out, size, "%s/%s", a, b);
    }

    // The log, and on the console the bottom screen.
    void Say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

    void Say(const char* fmt, ...)
    {
        char line[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        printf("tssm: spike: %s\n", line);
        fflush(stdout);
#ifdef __3DS__
        char shown[600];
        snprintf(shown, sizeof(shown), "%s\n", line);
        i3DSSpikeBottom(shown);
#endif
    }

    Stats Summarise(std::vector<S32> v)
    {
        Stats s;
        memset(&s, 0, sizeof(s));
        s.n = (S32)v.size();
        if (v.empty())
        {
            return s;
        }
        std::sort(v.begin(), v.end());
        double sum = 0.0;
        for (size_t i = 0; i < v.size(); i++)
        {
            sum += v[i];
        }
        const size_t last = v.size() - 1;
        s.minMs = v[0] / 1000.0;
        s.medMs = v[last / 2] / 1000.0;
        s.p90Ms = v[last * 90 / 100] / 1000.0;
        s.p95Ms = v[last * 95 / 100] / 1000.0;
        s.maxMs = v[last] / 1000.0;
        s.meanMs = sum / v.size() / 1000.0;
        return s;
    }

    const Calibration* PcFor(const char* id)
    {
        for (size_t i = 0; i < sizeof(kPc) / sizeof(kPc[0]); i++)
        {
            if (iHostStrCaseCmp(kPc[i].id, id) == 0 && kPc[i].medUs > 0)
            {
                return &kPc[i];
            }
        }
        return NULL;
    }

    U32 SceneId()
    {
        return globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
    }

    U32 IdOf(const char* id)
    {
        return ((U32)(U8)id[0] << 24) | ((U32)(U8)id[1] << 16) | ((U32)(U8)id[2] << 8) |
               (U32)(U8)id[3];
    }

    void NameOf(U32 id, char* out)
    {
        for (int i = 0; i < 4; i++)
        {
            const char c = (char)(id >> (24 - 8 * i));
            out[i] = (c >= 32 && c < 127) ? c : '?';
        }
        out[4] = '\0';
    }

    // ------------------------------------------------------------ the folder

    void WriteText(const char* path, const char* text)
    {
        FILE* f = fopen(path, "wb");
        if (f == NULL)
        {
            Say("could not write %s", path);
            return;
        }
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }

    void EmptyFolder(const char* dir)
    {
        iHostDir* d = iHostDirOpen(dir);
        if (d == NULL)
        {
            return;
        }
        std::vector<std::vector<char> > names;
        for (const char* n = iHostDirNext(d); n != NULL; n = iHostDirNext(d))
        {
            names.push_back(std::vector<char>(n, n + strlen(n) + 1));
        }
        iHostDirClose(d);
        for (size_t i = 0; i < names.size(); i++)
        {
            char path[600];
            Join(path, sizeof(path), dir, &names[i][0]);
            iHostRemoveFile(path);
        }
    }

    // spike.ini (3DS) or the environment (PC).
    void ReadSettings(char* levels, size_t size)
    {
        snprintf(levels, size, "%s", kDefaultLevels);

        char ini[400];
#ifdef __3DS__
        Join(ini, sizeof(ini), k3DSRoot, "spike.ini");
#else
        Join(ini, sizeof(ini), sDir, "spike.ini");
#endif
        FILE* f = fopen(ini, "rb");
        if (f != NULL)
        {
            char line[256];
            while (fgets(line, sizeof(line), f) != NULL)
            {
                char* eq = strchr(line, '=');
                if (eq == NULL || line[0] == ';' || line[0] == '#')
                {
                    continue;
                }
                *eq = '\0';
                char key[64];
                char value[192];
                if (sscanf(line, " %63s", key) != 1 || sscanf(eq + 1, " %191[^\r\n]", value) != 1)
                {
                    continue;
                }
                if (iHostStrCaseCmp(key, "levels") == 0)
                {
                    snprintf(levels, size, "%s", value);
                }
                else if (iHostStrCaseCmp(key, "seconds") == 0)
                {
                    sSeconds = atoi(value);
                }
                else if (iHostStrCaseCmp(key, "sdmb") == 0)
                {
                    sSdMB = atoi(value);
                }
                else if (iHostStrCaseCmp(key, "volume") == 0)
                {
                    sVolume = atoi(value);
                }
                else if (iHostStrCaseCmp(key, "rwheap") == 0)
                {
                    sRwHeap = atoi(value);
                }
                else if (iHostStrCaseCmp(key, "movie") == 0)
                {
                    snprintf(sMovie, sizeof(sMovie), "%s", value);
                }
            }
            fclose(f);
            Say("settings from %s", ini);
        }

        const char* env = getenv("TSSM_SPIKE_LEVELS");
        if (env != NULL && env[0] != '\0')
        {
            snprintf(levels, size, "%s", env);
        }
        env = getenv("TSSM_SPIKE_SECONDS");
        if (env != NULL && env[0] != '\0')
        {
            sSeconds = atoi(env);
        }
        env = getenv("TSSM_SPIKE_SDMB");
        if (env != NULL && env[0] != '\0')
        {
            sSdMB = atoi(env);
        }
        env = getenv("TSSM_SPIKE_VOLUME");
        if (env != NULL && env[0] != '\0')
        {
            sVolume = atoi(env);
        }
        sSeconds = std::max(20, std::min(sSeconds, 600));
        sSdMB = std::max(0, std::min(sSdMB, 512));
        sVolume = std::max(0, std::min(sVolume, 100));
    }

    void ParseLevels(const char* list)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", list);
        for (char* tok = strtok(buf, ", \t"); tok != NULL && sLevelCount < kMaxLevels;
             tok = strtok(NULL, ", \t"))
        {
            if (strlen(tok) != 4)
            {
                Say("'%s' is not a four-character scene id; skipped", tok);
                continue;
            }
            Level& l = sLevels[sLevelCount++];
            for (int i = 0; i < 4; i++)
            {
                l.id[i] = (char)((tok[i] >= 'a' && tok[i] <= 'z') ? tok[i] - 32 : tok[i]);
            }
            l.id[4] = '\0';
        }
    }

    // The game's own sb04.ini with NoMovies = 1, as a mod, as the measuring runs
    // had it: the level-intro movies would otherwise hold a level's start.
    void WriteModIni(const char* modDir)
    {
        char from[600];
        Join(from, sizeof(from), sAssets, "sb04.ini");
        FILE* f = fopen(from, "rb");
        if (f == NULL)
        {
            Say("no %s, so no mod (the level-intro movies are skipped anyway without a decoder)",
                from);
            return;
        }
        std::vector<char> text;
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        {
            text.insert(text.end(), chunk, chunk + n);
        }
        fclose(f);

        std::vector<char> out;
        bool replaced = false;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = start;
            while (end < text.size() && text[end] != '\n')
            {
                end++;
            }
            const size_t lineEnd = end < text.size() ? end + 1 : end;
            size_t k = start;
            while (k < end && (text[k] == ' ' || text[k] == '\t'))
            {
                k++;
            }
            if (end - k >= 8 && strncmp(&text[k], "NoMovies", 8) == 0 &&
                (k + 8 == end || text[k + 8] == ' ' || text[k + 8] == '=' || text[k + 8] == '\t'))
            {
                const char* line = "NoMovies = 1\r\n";
                out.insert(out.end(), line, line + strlen(line));
                replaced = true;
            }
            else
            {
                out.insert(out.end(), text.begin() + start, text.begin() + lineEnd);
            }
            start = lineEnd;
        }
        if (!replaced)
        {
            const char* line = "\r\nNoMovies = 1\r\n";
            out.insert(out.end(), line, line + strlen(line));
        }
        out.push_back('\0');

        char to[600];
        Join(to, sizeof(to), modDir, "sb04.ini");
        WriteText(to, &out[0]);
    }

    // ------------------------------------------------------------ the screens

    void FmtMs(char* out, size_t size, double ms)
    {
        if (ms >= 100.0)
        {
            snprintf(out, size, "%4.0f", ms);
        }
        else
        {
            snprintf(out, size, "%4.1f", ms);
        }
    }

    const char* Verdict(double worstP95, double worstMed)
    {
        if (worstP95 <= 0.0)
        {
            return "NO RESULT";
        }
        if (worstP95 <= 25.0)
        {
            return "GO";
        }
        if (worstP95 <= 30.0 || worstMed <= 25.0)
        {
            return "BORDERLINE";
        }
        return "NO-GO";
    }

    void Worst(double* p95, double* med)
    {
        *p95 = 0.0;
        *med = 0.0;
        for (S32 i = 0; i < sLevelCount; i++)
        {
            const Stats s = Summarise(sLevels[i].work);
            if (s.n > 0)
            {
                *p95 = std::max(*p95, s.p95Ms);
                *med = std::max(*med, s.medMs);
            }
        }
    }

    // Levels that timed at least half the frames a whole run of one does. One
    // that did not load, stopped the run or left early is not covered, and a
    // verdict over fewer than every level says so: the heavy levels are the
    // ones most likely to be missing.
    S32 Covered()
    {
        const S32 full = sSeconds * kFps - kSkipFrames;
        S32 n = 0;
        for (S32 i = 0; i < sLevelCount; i++)
        {
            if (full > 0 && (S32)sLevels[i].work.size() * 2 >= full)
            {
                n++;
            }
        }
        return n;
    }

    void ShowTop(const char* status, const char* detail1, const char* detail2)
    {
#ifdef __3DS__
        static char lines[15][32];
        const char* ptrs[15];
        S32 n = 0;
        snprintf(lines[n++], 32, "TSSM 3DS spike %s", sStage == STAGE_DONE ? "- DONE" : "");
        const char* ip = i3DSAddress();
        snprintf(lines[n++], 32, "IP %s", ip[0] != '\0' ? ip : "(no network)");
        snprintf(lines[n++], 32, "%s", status != NULL ? status : "");
        snprintf(lines[n++], 32, "%s", detail1 != NULL ? detail1 : "");
        snprintf(lines[n++], 32, "%s", detail2 != NULL ? detail2 : "");
        snprintf(lines[n++], 32, "work ms  med  p95  xPC");
        for (S32 i = 0; i < sLevelCount && n < 15; i++)
        {
            const Level& l = sLevels[i];
            if (!l.finished && !(sStage == STAGE_RUN && i == sCur))
            {
                snprintf(lines[n++], 32, "%-6s    -", l.id);
                continue;
            }
            const Stats s = Summarise(l.work);
            char med[16];
            char p95[16];
            FmtMs(med, sizeof(med), s.medMs);
            FmtMs(p95, sizeof(p95), s.p95Ms);
            const Calibration* pc = PcFor(l.id);
            char factor[16] = "  -";
            if (pc != NULL && s.n > 0)
            {
                snprintf(factor, sizeof(factor), "%3.0f", s.medMs * 1000.0 / pc->medUs);
            }
            if (s.n == 0)
            {
                snprintf(lines[n++], 32, "%-6s %s", l.id, l.note[0] ? "failed" : "...");
            }
            else
            {
                snprintf(lines[n++], 32, "%-6s %s %s  %s", l.id, med, p95, factor);
            }
        }
        for (S32 i = 0; i < n; i++)
        {
            ptrs[i] = lines[i];
        }
        i3DSSpikeTop(ptrs, n);
#else
        (void)status;
        (void)detail1;
        (void)detail2;
#endif
    }

    // ------------------------------------------------------------- the report

    void WriteReport()
    {
        std::vector<char> r;
        Append(r, "TSSM 3DS speed test report\n");
        Append(r, "build: %s %s, %s\n", __DATE__, __TIME__,
#ifdef __3DS__
               "tssm-spike.3dsx (New 3DS, librw NULL renderer)"
#else
               sizeof(void*) == 4 ? "PC 32-bit (calibration)" : "PC 64-bit (calibration)"
#endif
        );
        Append(r, "state: %s\n",
               sStage == STAGE_DONE ? "finished" : "IN PROGRESS (or it crashed: see crash.log)");
        Append(r, "levels: ");
        for (S32 i = 0; i < sLevelCount; i++)
        {
            Append(r, "%s%s", i ? ", " : "", sLevels[i].id);
        }
        Append(r, "; %d s of game time each at a 30 fps time step, frames run back to back\n",
               sSeconds);
        Append(r, "input: %s; timed: frames in play (no cutscene, no pause) from 14 s on\n\n",
               kScriptName);

        Append(r, "== 1. Console ==\n%s\n", sConsoleText[0] ? sConsoleText : "(not measured)\n");
        Append(r, "== 2. Threads ==\n%s\n", sThreadText[0] ? sThreadText : "(not measured)\n");
        Append(r, "== 3. SD card ==\n%s\n", sSdText[0] ? sSdText : "(not measured)\n");

        Append(r, "== 4. Game logic (the main answer) ==\n");
        Append(r,
               "work = the top of zGameLoop to the present: update + scene walk/submit with the\n"
               "NULL renderer (nothing drawn). update = to the main camera's first begin;\n"
               "scene = from there to the present. Times in ms.\n\n");
        Append(r, "level  frames  work: min    med    p90    p95    max   mean | update med  p95 "
                  "| scene med  p95\n");
        for (S32 i = 0; i < sLevelCount; i++)
        {
            const Level& l = sLevels[i];
            const Stats w = Summarise(l.work);
            const Stats u = Summarise(l.logic);
            const Stats s = Summarise(l.render);
            if (w.n == 0)
            {
                Append(r, "%-6s %6d  (no timed frames)%s%s\n", l.id, w.n, l.note[0] ? " " : "",
                       l.note);
                continue;
            }
            Append(r,
                   "%-6s %6d  %10.2f %6.2f %6.2f %6.2f %6.2f %6.2f | %10.2f %5.2f | %9.2f %5.2f\n",
                   l.id, w.n, w.minMs, w.medMs, w.p90Ms, w.p95Ms, w.maxMs, w.meanMs, u.medMs,
                   u.p95Ms, s.medMs, s.p95Ms);
        }
        Append(r, "\nload and memory per level (peaks; MB = 2^20 bytes):\n");
        Append(r, "level  load s (file MB, file s, measuring s) | arena  RW heap  sound | malloc "
                  "used  malloc taken | linear free min  app free min | meshes avg/max  tris "
                  "avg/max | real s\n");
        const double mb = 1.0 / 1048576.0;
        for (S32 i = 0; i < sLevelCount; i++)
        {
            const Level& l = sLevels[i];
            if (!l.started)
            {
                Append(r, "%-6s not reached\n", l.id);
                continue;
            }
            const S32 nf = l.statFrames > 0 ? l.statFrames : 1;
            Append(r,
                   "%-6s %6.1f (%5.1f, %5.2f, %5.2f) | %5.1f  %6.1f  %5.1f | %10.1f  %12.1f | "
                   "%15.1f  %12.1f | %5lld/%-5d  %6lld/%-6d | %6.1f\n",
                   l.id, l.loadMs / 1000.0, l.fileMB, l.fileMs / 1000.0, l.d0OverheadMs / 1000.0,
                   l.peaks.arenaBytes * mb,
                   l.peaks.rwHeapBytes >= 0 ? l.peaks.rwHeapBytes * mb : -1.0,
                   l.peaks.sndCacheBytes * mb, l.mallocUsedMax * mb, l.mallocArena * mb,
                   l.linearFreeMin * mb, l.appFreeMin * mb, (long long)(l.sumMeshes / nf),
                   l.maxMeshes, (long long)(l.sumTris / nf), l.maxTris, l.wallS);
            if (l.note[0] != '\0')
            {
                Append(r, "       note: %s\n", l.note);
            }
        }

        Append(r, "\n== 5. Audio ==\n");
        bool anyMix = false;
        for (S32 i = 0; i < sLevelCount; i++)
        {
            const Level& l = sLevels[i];
            if (!l.mixOk || l.mix.calls == 0 || l.mix.rate == 0)
            {
                continue;
            }
            anyMix = true;
            const double avgMs = l.mix.totalNs / 1e6 / l.mix.calls;
            const double periodMs = (double)l.mix.frames / l.mix.calls * 1000.0 / l.mix.rate;
            const double audioMs = (double)l.mix.frames * 1000.0 / l.mix.rate;
            Append(r,
                   "%-6s %u callbacks, %.0f frames each at %u Hz (a period of %.2f ms): mixing "
                   "took %.3f ms on average, %.3f ms at most; %.1f%% of the audio's own time\n",
                   l.id, (unsigned)l.mix.calls, (double)l.mix.frames / l.mix.calls,
                   (unsigned)l.mix.rate, periodMs, avgMs, l.mix.maxNs / 1e6,
                   audioMs > 0 ? l.mix.totalNs / 1e6 / audioMs * 100.0 : 0.0);
        }
        if (!anyMix)
        {
#ifdef __3DS__
            if (!i3DSSpikeHasDsp())
            {
                Append(r,
                       "no DSP firmware dump (sdmc:/3ds/dspfirm.cdc), so no sound and nothing "
                       "to time. Make it once: Rosalina menu (L + Down + SELECT) > Miscellaneous "
                       "options > Dump DSP firmware; then run the spike again.\n");
            }
            else
#endif
            {
                Append(r, "no audio device ran (see last_run.log), so nothing to time.\n");
            }
        }
        Append(r, "(the mixer runs on SDL's audio thread, which SDL makes for core 1 on a New 3DS "
                  "as the thread probe does: section 2 says where such a thread really ran)\n");

        Append(r, "\n== 6. Movie decode ==\n%s\n",
               sMovieText[0] ? sMovieText : "not in this spike\n");

        Append(r, "== 7. Calibration ==\n");
        Append(r, "PC: %s\n", kPcWhat);
        Append(r, "level   3DS med  PC med (us)  factor | 3DS p95  PC p95 (us)  factor\n");
        for (S32 i = 0; i < sLevelCount; i++)
        {
            const Level& l = sLevels[i];
            const Calibration* pc = PcFor(l.id);
            const Stats w = Summarise(l.work);
            if (pc == NULL || w.n == 0)
            {
                Append(r, "%-6s  %s\n", l.id, w.n == 0 ? "(no 3DS frames)" : "(no PC figure)");
                continue;
            }
            Append(r, "%-6s %8.2f  %11d  %6.1f | %7.2f  %11d  %6.1f\n", l.id, w.medMs, pc->medUs,
                   w.medMs * 1000.0 / pc->medUs, w.p95Ms, pc->p95Us,
                   pc->p95Us > 0 ? w.p95Ms * 1000.0 / pc->p95Us : 0.0);
        }

        double worstP95;
        double worstMed;
        Worst(&worstP95, &worstMed);
        Append(r, "\n== 8. Verdict ==\n");
        Append(r, "worst level: median %.2f ms, p95 %.2f ms of game logic per frame -> %s\n",
               worstMed, worstP95, Verdict(worstP95, worstMed));
        Append(r,
               "(rule: every level's p95 within 25 ms GO; within 30 ms, or medians within 25 ms, "
               "BORDERLINE; otherwise NO-GO. Drawing has to fit in what is left of 33.3 ms.)\n");
        if (Covered() < sLevelCount)
        {
            Append(r,
                   "WARNING: only %d of %d levels ran their whole time (section 4), so the verdict "
                   "covers only those; it is not a verdict on the game.\n",
                   (int)Covered(), (int)sLevelCount);
        }
#ifdef __3DS__
        if (sConsoleText[0] != '\0' && !sFast)
        {
            Append(r,
                   "WARNING: the clock check did not see 804 MHz (section 1), so these are not a "
                   "New 3DS's times: an emulator, or the speed-up did not take.\n");
        }
#endif

        Append(r, "\n== 9. Game files this run opened ==\n");
        U64 total = 0;
        for (S32 i = 0; i < sOpenedCount; i++)
        {
            Append(r, "%10ld  %s\n", sOpened[i].length, sOpened[i].path);
            total += (U64)sOpened[i].length;
        }
        Append(r, "%10.1f MB in %d files\n", total * mb, (int)sOpenedCount);

        WriteText(sReport, &r[0]);
    }

    // -------------------------------------------------------------- the probes

    void ProbeSd()
    {
        if (sSdMB <= 0)
        {
            snprintf(sSdText, sizeof(sSdText), "skipped (sdmb = 0)\n");
            return;
        }
        // The levels' packages, the run's biggest files, read in order until
        // the amount is reached.
        std::vector<char> buf(128 * 1024);
        const U64 want = (U64)sSdMB * 1048576;
        U64 got = 0;
        const U64 t0 = iHostMonotonicNs();
        S32 files = 0;
        for (S32 i = 0; i < sLevelCount && got < want; i++)
        {
            char rel[64];
            snprintf(rel, sizeof(rel), "%c%c/%c%c%c%c.HOP", sLevels[i].id[0], sLevels[i].id[1],
                     sLevels[i].id[0] | 0x20, sLevels[i].id[1] | 0x20, sLevels[i].id[2],
                     sLevels[i].id[3]);
            char path[700];
            Join(path, sizeof(path), sAssets, rel);
            FILE* f = fopen(path, "rb");
            if (f == NULL)
            {
                continue;
            }
            setvbuf(f, NULL, _IONBF, 0);
            files++;
            size_t n;
            while (got < want && (n = fread(&buf[0], 1, buf.size(), f)) > 0)
            {
                got += n;
            }
            fclose(f);
        }
        const double s = (iHostMonotonicNs() - t0) / 1e9;
        if (got == 0)
        {
            snprintf(sSdText, sizeof(sSdText), "no level package found to read under %s\n",
                     sAssets);
            return;
        }
        sSdMBps = s > 0 ? got / 1048576.0 / s : 0.0;
        int n = snprintf(sSdText, sizeof(sSdText),
                         "%.1f MB from %d level package(s) in 128 KB reads, unbuffered: %.2f s, "
                         "%.2f MB/s\n",
                         got / 1048576.0, (int)files, s, sSdMBps);

        // The same in 4 KB reads, for what a read size does.
        char path[700];
        char rel[64];
        snprintf(rel, sizeof(rel), "%c%c/%c%c%c%c.HOP", sLevels[0].id[0], sLevels[0].id[1],
                 sLevels[0].id[0] | 0x20, sLevels[0].id[1] | 0x20, sLevels[0].id[2],
                 sLevels[0].id[3]);
        Join(path, sizeof(path), sAssets, rel);
        FILE* f = fopen(path, "rb");
        if (f != NULL && n > 0 && n < (int)sizeof(sSdText))
        {
            setvbuf(f, NULL, _IONBF, 0);
            const U64 small = 4 * 1048576;
            U64 got4 = 0;
            const U64 t1 = iHostMonotonicNs();
            size_t k;
            while (got4 < small && (k = fread(&buf[0], 1, 4096, f)) > 0)
            {
                got4 += k;
            }
            const double s4 = (iHostMonotonicNs() - t1) / 1e9;
            fclose(f);
            snprintf(sSdText + n, sizeof(sSdText) - n, "%.1f MB in 4 KB reads: %.2f s, %.2f MB/s\n",
                     got4 / 1048576.0, s4, s4 > 0 ? got4 / 1048576.0 / s4 : 0.0);
        }
#ifndef __3DS__
        const size_t used = strlen(sSdText);
        snprintf(sSdText + used, sizeof(sSdText) - used,
                 "(a PC's file cache: says nothing about a console's SD card)\n");
#endif
    }

    // Section 6: the first seconds of one of the game's Bink movies through
    // FFmpeg's decoders, as fast as they go (the port's movie player is the
    // same decoders, iFMVDecoderFFmpeg.cpp). Video and audio decoding are
    // timed apart from the file reads.
    void ProbeMovie()
    {
#ifdef TSSM_HAVE_FFMPEG
        char rel[100];
        snprintf(rel, sizeof(rel), "FMV/%s", sMovie);
        char path[700];
        Join(path, sizeof(path), sAssets, rel);
        AVFormatContext* fmt = NULL;
#ifdef __3DS__
        // "sdmc:" would be taken for a protocol (iFMVDecoderFFmpeg.cpp).
        char url[720];
        snprintf(url, sizeof(url), "file:%s", path);
#else
        const char* url = path;
#endif
        if (avformat_open_input(&fmt, url, NULL, NULL) < 0)
        {
            snprintf(sMovieText, sizeof(sMovieText), "%s could not be opened, so not timed\n",
                     path);
            return;
        }
        avformat_find_stream_info(fmt, NULL);
        const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        const int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        AVCodecContext* dec[2] = { NULL, NULL };
        const int idx[2] = { vs, as };
        for (int i = 0; i < 2; i++)
        {
            if (idx[i] < 0)
            {
                continue;
            }
            const AVCodecParameters* par = fmt->streams[idx[i]]->codecpar;
            const AVCodec* codec = avcodec_find_decoder(par->codec_id);
            if (codec == NULL)
            {
                continue;
            }
            dec[i] = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(dec[i], par);
            if (avcodec_open2(dec[i], codec, NULL) < 0)
            {
                avcodec_free_context(&dec[i]);
            }
        }
        if (dec[0] == NULL)
        {
            snprintf(sMovieText, sizeof(sMovieText), "%s: no video decoder opened\n", path);
            avformat_close_input(&fmt);
            return;
        }
        const AVRational fr = fmt->streams[vs]->avg_frame_rate;
        const double fps = (fr.num > 0 && fr.den > 0) ? (double)fr.num / fr.den : 30.0;
        const S32 wantFrames = (S32)(fps * sMovieSeconds + 0.5);

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        U64 readNs = 0;
        U64 videoNs = 0;
        U64 audioNs = 0;
        U64 maxFrameNs = 0;
        S32 frames = 0;
        S32 audioFrames = 0;
        for (;;)
        {
            const U64 r0 = iHostMonotonicNs();
            const int got = av_read_frame(fmt, pkt);
            readNs += iHostMonotonicNs() - r0;
            if (got < 0 || frames >= wantFrames)
            {
                av_packet_unref(pkt);
                break;
            }
            const int which = pkt->stream_index == vs ? 0 : (pkt->stream_index == as ? 1 : -1);
            if (which >= 0 && dec[which] != NULL)
            {
                const U64 t0 = iHostMonotonicNs();
                if (avcodec_send_packet(dec[which], pkt) >= 0)
                {
                    while (avcodec_receive_frame(dec[which], frame) >= 0)
                    {
                        if (which == 0)
                        {
                            frames++;
                        }
                        else
                        {
                            audioFrames++;
                        }
                        av_frame_unref(frame);
                    }
                }
                const U64 took = iHostMonotonicNs() - t0;
                if (which == 0)
                {
                    videoNs += took;
                    maxFrameNs = std::max(maxFrameNs, took);
                }
                else
                {
                    audioNs += took;
                }
            }
            av_packet_unref(pkt);
        }
        const AVCodecParameters* vp = fmt->streams[vs]->codecpar;
        const double perFrame = frames > 0 ? videoNs / 1e6 / frames : 0.0;
        const double movieS = frames / fps;
        snprintf(sMovieText, sizeof(sMovieText),
                 "%s: %dx%d at %.2f fps (%s video, %s audio); %d frames = %.1f s of movie\n"
                 "video decode: %.2f ms a frame on average, %.2f ms at most (a frame is due "
                 "every %.1f ms)\n"
                 "audio decode: %.1f ms for %.1f s (%d blocks); file reads %.1f ms\n"
                 "decode alone, all of it: %.0f%% of real time on one core\n",
                 rel, vp->width, vp->height, fps, avcodec_get_name(vp->codec_id),
                 as >= 0 ? avcodec_get_name(fmt->streams[as]->codecpar->codec_id) : "no",
                 (int)frames, movieS, perFrame, maxFrameNs / 1e6, 1000.0 / fps, audioNs / 1e6,
                 movieS, (int)audioFrames, readNs / 1e6,
                 movieS > 0 ? (videoNs + audioNs) / 1e9 / movieS * 100.0 : 0.0);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        for (int i = 0; i < 2; i++)
        {
            avcodec_free_context(&dec[i]);
        }
        avformat_close_input(&fmt);
#else
        snprintf(sMovieText, sizeof(sMovieText), "not in this spike (built without FFmpeg)\n");
#endif
    }

#ifdef __3DS__
    // The final screen's word for a core, from the thread probe's text
    // (i3DSSpikeThreads): "YES?" before "YES", which it contains.
    const char* CoreMark(const char* core)
    {
        static const char* const kMarks[][2] = {
            { "YES?", "?" }, { "YES", "y" }, { "MOVED", "mv" }, { "NO", "n" }
        };
        for (const auto& m : kMarks)
        {
            char key[32];
            snprintf(key, sizeof(key), "%s: %s", core, m[0]);
            if (strstr(sThreadText, key) != NULL)
            {
                return m[1];
            }
        }
        return "?";
    }
#endif

    void Probe()
    {
        ShowTop("1/4 console", "model, clock, memory", NULL);
#ifdef __3DS__
        i3DSSpikeConsole(sConsoleText, sizeof(sConsoleText));
        sFast = strstr(sConsoleText, "804 MHz is on") != NULL;
        Say("%s", sConsoleText);
        ShowTop("2/4 threads", "core 2, core 1", NULL);
        i3DSSpikeThreads(sThreadText, sizeof(sThreadText));
        sCore2 = CoreMark("core 2");
        sCore1 = CoreMark("core 1");
        Say("%s", sThreadText);
#else
        snprintf(sConsoleText, sizeof(sConsoleText), "PC, %d-bit build (the calibration run)\n",
                 (int)(sizeof(void*) * 8));
        snprintf(sThreadText, sizeof(sThreadText), "(a 3DS question)\n");
#endif
        char line[32];
        snprintf(line, sizeof(line), "reading %d MB", (int)sSdMB);
        ShowTop("3/4 SD card speed", line, NULL);
        ProbeSd();
        Say("%s", sSdText);
        WriteReport();

        snprintf(line, sizeof(line), "%d s of %s", (int)sMovieSeconds, sMovie);
        ShowTop("3/4 movie decode", line, NULL);
        ProbeMovie();
        Say("%s", sMovieText);
        WriteReport();
    }

    // ------------------------------------------------------------- the levels

    void SampleMemory(Level& l)
    {
#ifdef __3DS__
        uint32_t appFree;
        uint32_t linearFree;
        uint32_t used;
        uint32_t arena;
        i3DSSpikeMemory(&appFree, &linearFree, &used, &arena);
        l.mallocUsedMax = std::max(l.mallocUsedMax, (U32)used);
        l.mallocArena = std::max(l.mallocArena, (U32)arena);
        l.linearFreeMin =
            l.linearFreeMin == 0 ? linearFree : std::min(l.linearFreeMin, (U32)linearFree);
        l.appFreeMin = l.appFreeMin == 0 ? appFree : std::min(l.appFreeMin, (U32)appFree);
#else
        (void)l;
#endif
    }

    void StartLoading(S32 index)
    {
        sCur = index;
        Level& l = sLevels[index];
        l.started = true;
        sLoadStartNs = iHostMonotonicNs();
        sStage = STAGE_LOAD;
        Say("level %d/%d: loading %s", (int)index + 1, (int)sLevelCount, l.id);
        char line[32];
        snprintf(line, sizeof(line), "loading %s (%d/%d)", l.id, (int)index + 1, (int)sLevelCount);
        ShowTop("4/4 game logic", line, NULL);
    }

    void BeginLevel()
    {
        Level& l = sLevels[sCur];
        l.loaded = true;
        l.loadMs = (iHostMonotonicNs() - sLoadStartNs) / 1e6;
        iCaptureScriptRestart();
        iSndHostMixStats ignored;
        iSndHostMixStatsTake(&ignored, true);
        SampleMemory(l);
        sRunStartNs = iHostMonotonicNs();
        sStage = STAGE_RUN;
        Say("%s loaded in %.1f s; playing %d s of it", l.id, l.loadMs / 1000.0, (int)sSeconds);
    }

    void EndLevel(const char* note)
    {
        Level& l = sLevels[sCur];
        l.finished = true;
        if (note != NULL)
        {
            snprintf(l.note, sizeof(l.note), "%s", note);
        }
        l.wallS = (iHostMonotonicNs() - sRunStartNs) / 1e9;
        iD0ScenePeaksNow(&l.peaks);
        l.d0LoadMs = l.peaks.loadUs / 1000.0;
        l.d0OverheadMs = l.peaks.overheadUs / 1000.0;
        l.fileMs = l.peaks.fileUs / 1000.0;
        l.fileMB = l.peaks.fileBytes / 1048576.0;
        SampleMemory(l);
        l.mixOk = iSndHostMixStatsTake(&l.mix, true);
        const Stats w = Summarise(l.work);
        Say("%s: %d frames timed, work median %.2f ms, p95 %.2f ms, max %.2f ms%s%s", l.id,
            (int)w.n, w.medMs, w.p95Ms, w.maxMs, note ? "; " : "", note ? note : "");
        WriteReport();
        sStage = STAGE_LEAVE;
        sLeaveFrames = 0;
    }

    void Finish()
    {
        sStage = STAGE_DONE;
        iCaptureScriptOverride(TRUE, 0);
        WriteReport();
        double worstP95;
        double worstMed;
        Worst(&worstP95, &worstMed);
        const S32 covered = Covered();
        Say("done in %.0f s: %s%s (worst median %.2f ms, p95 %.2f ms; %d of %d levels whole). "
            "Report: %s",
            (iHostMonotonicNs() - sStartNs) / 1e9, Verdict(worstP95, worstMed),
            covered < sLevelCount ? ", PARTIAL" : "", worstMed, worstP95, (int)covered,
            (int)sLevelCount, sReport);
#ifdef __3DS__
        // Quiet while the last screen waits, as the game's own stall does.
        xSndMgrPauseSounds(0, TRUE, TRUE);
        xSndMgrPauseSounds(1, TRUE, TRUE);
        xSndMgrPauseSounds(2, TRUE, FALSE);
        // 25 columns a line: the warnings go on the START line, so that a
        // photo of the screen cannot show a bare GO.
        char verdict[32];
        snprintf(verdict, sizeof(verdict), "verdict: %s", Verdict(worstP95, worstMed));
        char extra[32];
        snprintf(extra, sizeof(extra), "SD %.1fMB/s c2 %s c1 %s", sSdMBps, sCore2, sCore1);
        char warn[32];
        if (!sFast && covered < sLevelCount)
        {
            snprintf(warn, sizeof(warn), "!804MHz %d/%dlv START:exit", (int)covered,
                     (int)sLevelCount);
        }
        else if (!sFast)
        {
            snprintf(warn, sizeof(warn), "no 804MHz! START: exit");
        }
        else if (covered < sLevelCount)
        {
            snprintf(warn, sizeof(warn), "only %d/%d lvls START:exit", (int)covered,
                     (int)sLevelCount);
        }
        else
        {
            snprintf(warn, sizeof(warn), "START: exit");
        }
        ShowTop(verdict, extra, warn);
        // The top screen as a picture too, beside the report.
        char shot[420];
        Join(shot, sizeof(shot), sDir, "top-screen.bmp");
        i3DSSpikeSaveTop(shot);
        Say("Report: sdmc:/3ds/tssm/spike-report.txt; on the PC: fetch_report.py %s. "
            "START: exit.",
            i3DSAddress());
        i3DSSpikeWaitStart();
#endif
        fflush(stdout);
        exit(0);
    }

    void SwitchTo(const Level& next)
    {
        // As the game's own soak test leaves a level (zGame.cpp,
        // zGameUpdateMode): a portal to the next scene's start, and the
        // scene-switch state, which the next game-loop frame acts on.
        memset(&sPortalAsset, 0, sizeof(sPortalAsset));
        sPortalAsset.sceneID = IdOf(next.id);
        sPortal.passet = &sPortalAsset;
        globals.sceneCur->pendingPortal = &sPortal;
        gGameState = eGameState_SceneSwitch;
    }
} // namespace

S32 iSpikeOn()
{
    if (sOn < 0)
    {
#ifdef TSSM_3DS_SPIKE
        sOn = 1;
#else
        const char* env = getenv("TSSM_SPIKE");
        sOn = (env != NULL && atoi(env) > 0) ? 1 : 0;
#endif
    }
    return sOn;
}

void iSpikeEarly()
{
    if (!iSpikeOn())
    {
        return;
    }
    sStartNs = iHostMonotonicNs();

#ifdef __3DS__
    Join(sDir, sizeof(sDir), k3DSRoot, "spike");
    Join(sReport, sizeof(sReport), k3DSRoot, "spike-report.txt");
    i3DSServeFile(sReport);
#else
    const char* dir = getenv("TSSM_SPIKE_DIR");
    snprintf(sDir, sizeof(sDir), "%s", dir != NULL && dir[0] != '\0' ? dir : "spike");
    Join(sReport, sizeof(sReport), sDir, "spike-report.txt");
#endif
    iHostMakeDir(sDir);

    const char* assets = getenv("TSSM_ASSETS");
    snprintf(sAssets, sizeof(sAssets), "%s", assets != NULL ? assets : "");

    char levels[256];
    ReadSettings(levels, sizeof(levels));
    ParseLevels(levels);
    if (sLevelCount == 0)
    {
        ParseLevels(kDefaultLevels);
    }

    char path[600];
    char save[500];
    char mods[500];
    char mod[500];
    Join(save, sizeof(save), sDir, "save");
    Join(mods, sizeof(mods), sDir, "mods-none");
    Join(mod, sizeof(mod), sDir, "mod");
    iHostMakeDir(save);
    iHostMakeDir(mods);
    iHostMakeDir(mod);
    EmptyFolder(save);
    WriteModIni(mod);

#ifdef __3DS__
    // Nothing drawn: the spike times the game's own work, and its text has
    // both screens (3ds/i3DSSpike.cpp). In a citro3d build started with
    // TSSM_SPIKE=1 (test-env.txt) the device would take the top screen and
    // swap it under the spike's drawing.
    const char* backend = "backend = null\n";
#else
    const char* backend = "";
#endif
    char config[1024];
    snprintf(config, sizeof(config),
             "; Written by the spike (iSpike.cpp) at every start; not the player's settings.\n"
             "[assets]\nmods = off\n"
             "[game]\nboot = %s\nintro_movies = off\n"
             "[video]\nframerate = 30\nvsync = off\n%s"
             "[audio]\nmaster_volume = %d\n"
             "[gui]\nlauncher = off\n",
             sLevels[0].id, backend, (int)sVolume);
    Join(path, sizeof(path), sDir, "config.ini");
    WriteText(path, config);

    iHostSetEnv("TSSM_CONFIG", path);
    iHostSetEnv("TSSM_SAVE_DIR", save);
    iHostSetEnv("TSSM_MODS_DIR", mods);
    iHostSetEnv("TSSM_MOD", mod);
    Join(path, sizeof(path), sDir, "d0.tsv");
    iHostSetEnv("TSSM_D0", path);
    iHostSetEnv("TSSM_RWHEAP", sRwHeap ? "1" : NULL);
    iHostSetEnv("TSSM_INPUT", kScript30);
    // On the console the frames run back to back (iWindow3DS.cpp) and the
    // game's time step is kept at a 30 fps display's in iSpikeFrame. A PC run
    // is back to back too unless TSSM_NULL_PACE=1 paces it as the measuring runs were
    // (iWindowNull.cpp); the time step is the same either way.

    Say("on: %d level(s) from %s, %d s each; folder %s", (int)sLevelCount, sLevels[0].id,
        (int)sSeconds, sDir);
    WriteReport();
}

void iSpikeFrame()
{
    if (!iSpikeOn() || sStage == STAGE_DONE)
    {
        return;
    }

    if (sStage == STAGE_PROBE)
    {
        Probe();
        StartLoading(0);
        return;
    }

    iD0FrameResult f;
    iD0LastFrame(&f);
    const U32 scene = SceneId();

    // The game's time step: at least a 30 fps display's frame, whatever the
    // frame took. Only game-loop frames; a load's screens keep real time.
    if (f.loop)
    {
        iTimeFrameAtLeast(kStepNs);
    }

    switch (sStage)
    {
    case STAGE_LOAD:
    {
        Level& l = sLevels[sCur];
        if (f.loop && scene == IdOf(l.id))
        {
            BeginLevel();
        }
        else if (iHostMonotonicNs() - sLoadStartNs > 240ULL * 1000000000ULL)
        {
            snprintf(l.note, sizeof(l.note), "did not load within 240 s");
            Say("%s did not load within 240 s; stopping", l.id);
            Finish();
        }
        break;
    }

    case STAGE_RUN:
    {
        Level& l = sLevels[sCur];
        if (!f.loop)
        {
            break;
        }
        if (scene != IdOf(l.id))
        {
            char now[8];
            NameOf(scene, now);
            char note[160];
            snprintf(note, sizeof(note), "the game moved on to %s after %d frames", now,
                     (int)l.loopFrames);
            EndLevel(note);
            break;
        }
        l.loopFrames++;
        if (f.play)
        {
            l.playFrames++;
            if (l.loopFrames > kSkipFrames && f.workUs >= 0)
            {
                l.work.push_back((S32)f.workUs);
                if (f.logicUs >= 0)
                {
                    l.logic.push_back((S32)f.logicUs);
                    l.render.push_back((S32)f.renderUs);
                }
                l.sumMeshes += f.meshes;
                l.sumTris += f.tris;
                l.maxMeshes = std::max(l.maxMeshes, f.meshes);
                l.maxTris = std::max(l.maxTris, f.tris);
                l.statFrames++;
            }
        }
        if (l.loopFrames % 30 == 0)
        {
            SampleMemory(l);
            char line1[32];
            char line2[32];
            snprintf(line1, sizeof(line1), "%s frame %d/%d", l.id, (int)l.loopFrames,
                     (int)(sSeconds * kFps));
            snprintf(line2, sizeof(line2), "last frame %.1f ms", f.workUs / 1000.0);
            ShowTop("4/4 game logic", line1, line2);
        }
        if (l.loopFrames >= sSeconds * kFps)
        {
            EndLevel(NULL);
        }
        break;
    }

    case STAGE_LEAVE:
    {
        if (sCur + 1 >= sLevelCount)
        {
            Finish();
            break;
        }
        // Out of a menu or a cutscene first: B, a press now and then.
        sLeaveFrames++;
        const bool ready = gGameMode == eGameMode_Game && gGameState == eGameState_Play &&
                           globals.cmgr == NULL && globals.sceneCur != NULL;
        if (ready)
        {
            iCaptureScriptOverride(TRUE, 0);
            SwitchTo(sLevels[sCur + 1]);
            StartLoading(sCur + 1);
            break;
        }
        const iPadBindButton* b = iPadBindFind("b");
        iCaptureScriptOverride(TRUE, (b != NULL && sLeaveFrames % 20 < 3) ? b->mask : 0);
        if (sLeaveFrames > 30 * kFps)
        {
            Say("could not leave %s (mode %d, state %d); stopping", sLevels[sCur].id,
                (int)gGameMode, (int)gGameState);
            Finish();
        }
        break;
    }

    default:
        break;
    }

#ifdef __3DS__
    i3DSSpikeYield();
#endif
}

void iSpikeFileOpened(const char* path, long length)
{
    if (!iSpikeOn() || path == NULL)
    {
        return;
    }
    const size_t n = strlen(sAssets);
    const char* rel = path;
    if (n > 0 && strncmp(path, sAssets, n) == 0)
    {
        rel = path + n;
        while (*rel == '/' || *rel == '\\')
        {
            rel++;
        }
    }
    for (S32 i = 0; i < sOpenedCount; i++)
    {
        if (iHostStrCaseCmp(sOpened[i].path, rel) == 0)
        {
            return;
        }
    }
    if (sOpenedCount < (S32)(sizeof(sOpened) / sizeof(sOpened[0])))
    {
        snprintf(sOpened[sOpenedCount].path, sizeof(sOpened[0].path), "%s", rel);
        sOpened[sOpenedCount].length = length;
        sOpenedCount++;
    }
}
