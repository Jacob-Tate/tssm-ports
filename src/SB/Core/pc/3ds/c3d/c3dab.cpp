// rw::c3d: the drawing modes and the one-run A/B switch (docs/3DS.md).
//
// A console run needs the user and the game cannot be restarted cheaply, so
// TSSM_C3D_AB switches the modes at given frames of one run:
//
//   TSSM_C3D_AB=<frame>:<settings>[#<label>],<frame>:<settings>[#<label>],...
//   settings: skin=gpu|cpu|bind|off; light, env, fog, rtt, shadow, rttflip,
//   glow, warp, copyflip, orthoz, copyedge, copy, hudz =on|off; joined by '+'
//   probe 14's measuring words: profile, gpusplit, fill, statsline =on|off;
//   flush=heap|cmdlist; stereo=off|force|guard; speed2's A/B words: refresh,
//   fast, pipe =on|off; speed3's: eyeskip, alphafill, skintrim, glowclear, savevram =on|off;
//   probe 16's measuring word eyetest=normal|2d|noshift|noright|nosave
//
// fog=on draws fog only when the fog ramp was made at the open (fog on then,
// by [3ds] effects or TSSM_C3D_FOG).
//
// From the frame after <frame> the settings apply. Each switch logs the
// segment that ended (frames, seconds, fps, the frame times' median and
// where they went); at TSSM_EXIT_AFTER's frame every segment is listed once
// more. Frames over a second (a capture's PNG, a load) are counted but left
// out of the steady figures, and so are the first two after each switch.
// TSSM_C3D_AB_LOG=<file> appends each segment to a summary (the probes'),
// TSSM_C3D_AB_DETAIL=1 with probe 14's lines under it (docs/3DS.md).

#include "c3dimpl.h"

#include "../../rw/stereomath.h"

#include "../i3DS.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace rw
{
    namespace c3d
    {
        // The skinning, the lighting and the environment maps; the effects
        // come from [3ds] effects (all by default: each was shown to work on
        // a console by its probe) and the TSSM_C3D_* switches
        // (modesInit).
        // rttFlip on: camera textures sampled as on the GameCube and the PC's
        // backends (v = 0 their camera's top); off is the PICA's own order.
        // hudz is on whatever [3ds] effects says: the HUD's models need it on
        // a console (atomicDepth). The measuring words are off: fill on,
        // the stats line on, no stereo asked for.
        // The stereo mode is [video] stereo's, set at the open (stereoOpen).
        Modes modes = { SKIN_GPU, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1,
                        0,        0, 1, 0, 1, stereomath::MODE_OFF, 0, 1, 1, 1, 1, 0, 1, 0,
                        EYETEST_NORMAL, 1 };
        bool32 selfTestOn;

        namespace
        {
            bool setOne(Modes* m, const char* key, uint32 klen, const char* val, uint32 vlen)
            {
                struct Word
                {
                    const char* key;
                    const char* val;
                    int32 which; // 0 skin, 100 stereo, 102 eyetest, else the bool32 it sets (flags below)
                    int32 to;
                };
                static const Word kWords[] = {
                    { "skin", "gpu", 0, SKIN_GPU },   { "skin", "cpu", 0, SKIN_CPU },
                    { "skin", "bind", 0, SKIN_BIND }, { "skin", "off", 0, SKIN_OFF },
                    { "light", "on", 1, 1 },          { "light", "shader", 1, 1 },
                    { "light", "off", 1, 0 },         { "env", "on", 2, 1 },
                    { "env", "off", 2, 0 },           { "fog", "on", 3, 1 },
                    { "fog", "off", 3, 0 },           { "rtt", "on", 4, 1 },
                    { "rtt", "off", 4, 0 },           { "shadow", "on", 5, 1 },
                    { "shadow", "off", 5, 0 },        { "rttflip", "on", 6, 1 },
                    { "rttflip", "off", 6, 0 },       { "glow", "on", 7, 1 },
                    { "glow", "off", 7, 0 },          { "warp", "on", 8, 1 },
                    { "warp", "off", 8, 0 },          { "copyflip", "on", 9, 1 },
                    { "copyflip", "off", 9, 0 },      { "orthoz", "on", 10, 1 },
                    { "orthoz", "off", 10, 0 },       { "copyedge", "on", 11, 1 },
                    { "copyedge", "off", 11, 0 },     { "copy", "on", 12, 1 },
                    { "copy", "off", 12, 0 },         { "hudz", "on", 13, 1 },
                    { "hudz", "off", 13, 0 },         { "profile", "on", 14, 1 },
                    { "profile", "off", 14, 0 },      { "gpusplit", "on", 15, 1 },
                    { "gpusplit", "off", 15, 0 },     { "fill", "on", 16, 1 },
                    { "fill", "off", 16, 0 },         { "flush", "cmdlist", 17, 1 },
                    { "flush", "heap", 17, 0 },       { "statsline", "on", 18, 1 },
                    { "statsline", "off", 18, 0 },    { "refresh", "on", 19, 1 },
                    { "refresh", "off", 19, 0 },      { "fast", "on", 20, 1 },
                    { "fast", "off", 20, 0 },         { "pipe", "on", 21, 1 },
                    { "pipe", "off", 21, 0 },         { "eyeskip", "on", 22, 1 },
                    { "eyeskip", "off", 22, 0 },      { "alphafill", "on", 23, 1 },
                    { "alphafill", "off", 23, 0 },    { "skintrim", "on", 24, 1 },
                    { "skintrim", "off", 24, 0 },     { "glowclear", "on", 25, 1 },
                    { "glowclear", "off", 25, 0 },    { "savevram", "on", 26, 1 },
                    { "savevram", "off", 26, 0 },
                    { "stereo", "off", 100, stereomath::MODE_OFF },
                    { "stereo", "auto", 100, stereomath::MODE_AUTO },
                    { "stereo", "force", 100, stereomath::MODE_FORCE },
                    { "stereo", "guard", 101, stereomath::MODE_FORCE },
                    { "eyetest", "normal", 102, EYETEST_NORMAL },
                    { "eyetest", "2d", 102, EYETEST_2D },
                    { "eyetest", "noshift", 102, EYETEST_NOSHIFT },
                    { "eyetest", "noright", 102, EYETEST_NORIGHT },
                    { "eyetest", "nosave", 102, EYETEST_NOSAVE },
                };
                for (const Word& w : kWords)
                {
                    if (strlen(w.key) == klen && strncmp(w.key, key, klen) == 0 &&
                        strlen(w.val) == vlen && strncmp(w.val, val, vlen) == 0)
                    {
                        bool32* flags[] = { nil,          &m->light,    &m->env,      &m->fog,
                                            &m->rtt,      &m->shadow,   &m->rttFlip,  &m->glow,
                                            &m->warp,     &m->copyFlip, &m->orthoZ,   &m->copyEdge,
                                            &m->copy,     &m->hudZ,     &m->profile,  &m->gpuSplit,
                                            &m->fill,     &m->cmdFlush, &m->statsLine,
                                            &m->refresh,  &m->fast,     &m->pipe,
                                            &m->eyeSkip,  &m->alphaFill, &m->skinTrim,
                                            &m->glowClear, &m->saveVram };
                        if (w.which == 0)
                            m->skin = w.to;
                        else if (w.which == 102)
                            m->eyeTest = w.to;
                        else if (w.which == 100 || w.which == 101)
                        {
                            m->stereo = w.to;
                            m->stereoGuard = w.which == 101;
                        }
                        else
                            *flags[w.which] = w.to;
                        return true;
                    }
                }
                return false;
            }

            // TRUE when the switch is set and understood.
            bool fromEnv(const char* name, const char* key, Modes* m)
            {
                const char* e = getenv(name);
                if (e == nil || e[0] == '\0')
                {
                    return false;
                }
                if (!setOne(m, key, (uint32)strlen(key), e, (uint32)strlen(e)))
                {
                    printf("tssm: c3d: %s=%s is not a mode this build knows; ignored\n", name, e);
                    return false;
                }
                return true;
            }

            // [3ds] effects' words. A word turns on what it draws through as
            // well: the shadow and the glow camera textures, the glow and the
            // warp screen copies.
            enum
            {
                FX_FOG = 1,
                FX_RTT = 2,
                FX_SHADOW = 4,
                FX_GLOW = 8,
                FX_WARP = 16,
                FX_COPY = 32,
                FX_ORTHOZ = 64,
                FX_SELFTEST = 128,
                // Every effect; the self-test only when named.
                FX_ALL = 127
            };
            struct Effect
            {
                const char* name;
                uint32 bits;
            };
            const Effect kEffects[] = {
                { "none", 0 },
                { "fog", FX_FOG },
                { "rtt", FX_RTT },
                { "shadow", FX_SHADOW | FX_RTT },
                { "glow", FX_GLOW | FX_RTT | FX_COPY },
                { "warp", FX_WARP | FX_COPY },
                { "copy", FX_COPY },
                { "orthoz", FX_ORTHOZ },
                { "selftest", FX_SELFTEST },
                { "all", FX_ALL },
            };

            // Words separated by commas, spaces or '+', any case.
            uint32 parseEffects(const char* text)
            {
                uint32 bits = 0;
                const char* p = text != nil ? text : "";
                while (*p != '\0')
                {
                    while (*p == ',' || *p == ' ' || *p == '\t' || *p == '+')
                    {
                        p++;
                    }
                    const char* w = p;
                    while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t' && *p != '+')
                    {
                        p++;
                    }
                    const uint32 len = (uint32)(p - w);
                    if (len == 0)
                    {
                        continue;
                    }
                    bool known = false;
                    for (const Effect& e : kEffects)
                    {
                        if (strlen(e.name) != len)
                        {
                            continue;
                        }
                        uint32 i = 0;
                        while (i < len && (w[i] | 0x20) == e.name[i])
                        {
                            i++;
                        }
                        if (i == len)
                        {
                            bits |= e.bits;
                            known = true;
                            break;
                        }
                    }
                    if (!known)
                    {
                        printf("tssm: c3d: [3ds] effects: '%.*s' is not an effect this build "
                               "knows (fog, rtt, shadow, glow, warp, copy, orthoz, selftest, "
                               "all, none); ignored\n",
                               (int)len, w);
                    }
                }
                return bits;
            }

            double ms(u64 t, uint32 n)
            {
                return n > 0 ? (double)t * 1000.0 / (double)SYSCLOCK_ARM11 / n : 0.0;
            }

            struct Switch
            {
                int32 frame;
                Modes to;
                char label[40];
            };

            // A stretch of frames under one setting.
            struct Segment
            {
                int32 first, last; // the capture hook's frame numbers
                Modes m;
                char label[40];
                uint32 frames; // presents timed
                uint32 slow; // of them over a second, left out below
                uint32 skipped; // the first two after the switch, left out below
                FrameTimes sum; // of the others
                double medianMs, p90Ms, p95Ms, maxMs;
                // Steady frames over 33.3 ms, and over 40 ms: the screen's two
                // vblanks are 33.43 ms (59.83 Hz), so at the 30 fps cap
                // nearly every frame is over 33.3 ms and one over 40 missed
                // its pair of vblanks.
                uint32 over33, over40;
                double seconds; // every frame's wall time
                // Probe 14's: peaks a frame, the least memory free, the
                // slider, the audio thread, the stereo hook.
                uint32 countPeak[NUM_COUNTS];
                uint32 drawsPeak, cmdPeak, ringPeak, queuePeak;
                float gpuPeakMs;
                uint32 vramMin, linearMin;
                double slider;
                bool stereoAsked; // stereo=force|guard and this build has no stereo
                bool stereoOn; // ... and the stereo code took it
            };

            const int32 kMaxSwitches = 48;
            const int32 kMaxWalls = 16384;
            const uint32 kSkipAfterSwitch = 2;
            Switch sSwitches[kMaxSwitches];
            int32 sNumSwitches;
            int32 sNextSwitch;
            Segment sSegs[kMaxSwitches + 1];
            int32 sNumSegs;
            uint32* sWalls; // the open segment's frame times, microseconds
            uint32 sNumWalls;
            int32 sExitAfter;
            bool sOn;
            bool sParsed;
            bool sDone;
            bool sDetail; // TSSM_C3D_AB_DETAIL=1: probe 14's lines in the summary

            void openSegment(Segment* g, int32 first, const char* label)
            {
                memset(g, 0, sizeof(*g));
                g->first = first;
                g->m = modes;
                g->vramMin = 0xFFFFFFFFu;
                g->linearMin = 0xFFFFFFFFu;
                snprintf(g->label, sizeof(g->label), "%s", label != nil ? label : "");
                // The audio thread's mixing from here (iHost3DS.cpp).
                uint32_t calls = 0;
                uint64_t ns = 0, maxNs = 0;
                i3DSAudioMixTake(&calls, &ns, &maxNs);
                if (modes.stereo == stereomath::MODE_FORCE)
                {
                    // Probe 14's stereo=force|guard: depth 50, the guard as asked.
                    g->stereoOn = stereoProbeSet(modes.stereo, modes.stereoGuard, 50) != 0;
                    g->stereoAsked = !g->stereoOn;
                }
                else
                {
                    stereoProbeSet(modes.stereo, 0, -1);
                }
                StereoProbeStats st;
                stereoProbeTake(&st);
            }

            void parse(void)
            {
                sParsed = true;
                const char* e = getenv("TSSM_C3D_AB");
                if (e == nil || e[0] == '\0')
                {
                    return;
                }
                const char* x = getenv("TSSM_EXIT_AFTER");
                sExitAfter = x != nil ? atoi(x) : 0;
                const char* d = getenv("TSSM_C3D_AB_DETAIL");
                sDetail = d != nil && d[0] == '1';
                Modes m = modes;
                int32 lastFrame = 0;
                const char* p = e;
                while (*p != '\0' && sNumSwitches < kMaxSwitches)
                {
                    const char* end = strchr(p, ',');
                    const uint32 len = end != nil ? (uint32)(end - p) : (uint32)strlen(p);
                    char entry[400];
                    snprintf(entry, sizeof(entry), "%.*s", (int)len, p);
                    p += len + (end != nil ? 1 : 0);

                    char* hash = strchr(entry, '#');
                    if (hash != nil)
                    {
                        *hash = '\0';
                    }
                    char* colon = strchr(entry, ':');
                    const int32 frame = atoi(entry);
                    if (colon == nil || frame <= lastFrame || !parseModes(colon + 1, &m))
                    {
                        printf("tssm: c3d: TSSM_C3D_AB: '%s' is not <frame>:<settings> after "
                               "frame %d; ignored\n",
                               entry, (int)lastFrame);
                        m = sNumSwitches > 0 ? sSwitches[sNumSwitches - 1].to : modes;
                        continue;
                    }
                    sSwitches[sNumSwitches].frame = frame;
                    sSwitches[sNumSwitches].to = m;
                    snprintf(sSwitches[sNumSwitches].label, sizeof(sSwitches[0].label), "%s",
                             hash != nil ? hash + 1 : "");
                    sNumSwitches++;
                    lastFrame = frame;
                }
                sWalls = (uint32*)malloc(kMaxWalls * sizeof(uint32));
                sOn = sNumSwitches > 0 && sWalls != nil;
                if (!sOn)
                {
                    return;
                }
                sNumSegs = 1;
                openSegment(&sSegs[0], 1, "before the first switch");
                char mt[320];
                printf("tssm: c3d: ab: %d switches; frames 1-%d %s\n", (int)sNumSwitches,
                       (int)sSwitches[0].frame, modesText(&modes, mt, sizeof(mt)));
                fflush(stdout);
            }

            int cmpU32(const void* a, const void* b)
            {
                const uint32 x = *(const uint32*)a, y = *(const uint32*)b;
                return x < y ? -1 : x > y ? 1 : 0;
            }

            uint32 timedFrames(const Segment* g)
            {
                const uint32 out = g->slow + g->skipped;
                return g->frames > out ? g->frames - out : 0;
            }

            void printSegment(int32 i, const Segment* g)
            {
                char mt[320];
                const uint32 n = timedFrames(g);
                const double mean = ms(g->sum.wall, n);
                const double busy = mean - ms(g->sum.t[TIME_SYNC], n);
                printf(
                    "tssm: c3d: ab: segment %d%s%s, frames %d-%d, %s: %u frames in %.2f s = %.1f "
                    "fps; steady (%u over 1 s and %u after the switch left out): %.1f fps, frame "
                    "ms median %.2f, p90 %.2f, "
                    "p95 %.2f, max %.2f, over 33.3 ms %u, over 40 ms %u, mean %.2f = vblank wait "
                    "%.2f + busy "
                    "%.2f (recording %.2f, CPU "
                    "skinning %.2f, GPU wait %.2f, FrameEnd %.2f); GPU %.2f ms; vertices a "
                    "frame: world %u, lit %u, im2d %u, im3d %u, skin %u, env %u, skinenv %u; "
                    "the timing itself about %.2f ms a frame\n",
                    (int)i, g->label[0] != '\0' ? " " : "", g->label, (int)g->first, (int)g->last,
                    modesText(&g->m, mt, sizeof(mt)), (unsigned)g->frames, g->seconds,
                    g->seconds > 0.0 ? g->frames / g->seconds : 0.0, (unsigned)g->slow,
                    (unsigned)g->skipped, mean > 0.0 ? 1000.0 / mean : 0.0, g->medianMs, g->p90Ms,
                    g->p95Ms, g->maxMs, (unsigned)g->over33, (unsigned)g->over40, mean,
                    ms(g->sum.t[TIME_SYNC], n), busy, ms(g->sum.t[TIME_RECORD], n),
                    ms(g->sum.t[TIME_CPUSKIN], n), ms(g->sum.t[TIME_GPU], n),
                    ms(g->sum.t[TIME_END], n), ms(g->sum.t[TIME_DRAWN], n),
                    n > 0 ? g->sum.verts[PROG_WORLD] / n : 0,
                    n > 0 ? g->sum.verts[PROG_LIT] / n : 0, n > 0 ? g->sum.verts[PROG_IM2D] / n : 0,
                    n > 0 ? g->sum.verts[PROG_IM3D] / n : 0,
                    n > 0 ? g->sum.verts[PROG_SKIN] / n : 0, n > 0 ? g->sum.verts[PROG_ENV] / n : 0,
                    n > 0 ? g->sum.verts[PROG_SKINENV] / n : 0,
                    n > 0 ? (double)g->sum.reads / n * tickReadNs() / 1e6 : 0.0);
            }

            // Probe 14's lines under a segment's (TSSM_C3D_AB_DETAIL=1): where
            // the CPU's time goes, the profile, the GPU per pass, the counts,
            // memory, audio, the slider and the stereo hook.
            void logDetail(FILE* f, const Segment* g, double audioMs, uint32 audioCalls,
                           double audioMaxMs, double audioSeconds)
            {
                const uint32 n = timedFrames(g);
                if (n == 0)
                {
                    fprintf(f, "    (no frame timed)\n");
                    return;
                }
                const FrameTimes& s = g->sum;
                const double wall = ms(s.wall, n);
                const double update = ms(s.t[TIME_UPDATE], n);
                const double sync = ms(s.t[TIME_SYNC], n);
                const double gpuWait = ms(s.t[TIME_GPU], n);
                const double rec = ms(s.t[TIME_RECORD], n);
                const double end = ms(s.t[TIME_END], n);
                double walk = wall - update - sync - gpuWait - rec - end;
                walk = walk > 0.0 ? walk : 0.0;
                const double draws = (double)s.draws / n;
                fprintf(f,
                        "    cpu (ms a frame): update %.2f, scene walk %.2f, recording %.2f (bones "
                        "%.2f, CPU skinning %.2f; %.1f us a draw over %.0f draws), FrameEnd %.2f; "
                        "waits: vblank %.2f, GPU %.2f; busy %.2f of %.2f\n",
                        update, walk, rec, ms(s.t[TIME_BONES], n), ms(s.t[TIME_CPUSKIN], n),
                        draws > 0.0 ? rec * 1000.0 / draws : 0.0, draws, end, sync, gpuWait,
                        wall - sync - gpuWait, wall);
                if (g->m.profile)
                {
                    double known = 0.0;
                    for (int i = 0; i < NUM_PROF; i++)
                    {
                        known += i == PROF_FLUSH ? 0.0 : ms(s.prof[i], n);
                    }
                    const double perDraw = draws > 0.0 ? 1000.0 / draws : 0.0;
                    // A part's two tick reads cost about one read inside it
                    // and one around it (in the rest): that half comes off
                    // the rest.
                    const double own = (double)s.profReads / n * tickReadNs() / 1e6;
                    const double rest = rec - known - own * 0.5;
                    fprintf(f,
                            "    profile (ms a frame, us a draw): matrix %.2f (%.2f), lights %.2f "
                            "(%.2f), mesh state %.2f (%.2f; of it flushState %.2f (%.2f)), "
                            "citro3d %.2f (%.2f), skin palette %.2f (%.2f), Im2D/Im3D "
                            "preparation %.2f (%.2f), the rest of recording %.2f (%.2f); the "
                            "profile's own tick reads %.2f ms a frame (%u ns each; each part "
                            "holds about one read a call, the rest is net of the others)\n",
                            ms(s.prof[PROF_MATRIX], n), ms(s.prof[PROF_MATRIX], n) * perDraw,
                            ms(s.prof[PROF_LIGHTS], n), ms(s.prof[PROF_LIGHTS], n) * perDraw,
                            ms(s.prof[PROF_MESH], n), ms(s.prof[PROF_MESH], n) * perDraw,
                            ms(s.prof[PROF_FLUSH], n), ms(s.prof[PROF_FLUSH], n) * perDraw,
                            ms(s.prof[PROF_C3D], n), ms(s.prof[PROF_C3D], n) * perDraw,
                            ms(s.prof[PROF_PALETTE], n), ms(s.prof[PROF_PALETTE], n) * perDraw,
                            ms(s.prof[PROF_IM], n), ms(s.prof[PROF_IM], n) * perDraw,
                            rest, rest * perDraw, own, (unsigned)tickReadNs());
                }
                if (g->m.gpuSplit)
                {
                    static const char* kPass[NUM_PASSES] = { "3D",          "HUD and 2D",
                                                             "shadow",      "other camera textures",
                                                             "glow",        "warp",
                                                             "screen copies", "bottom screen",
                                                             "present" };
                    char parts[512];
                    uint32 used = 0;
                    double total = 0.0;
                    parts[0] = '\0';
                    for (int i = 0; i < NUM_PASSES; i++)
                    {
                        const double v = (double)s.passMs[i] / n;
                        total += v;
                        used += (uint32)snprintf(parts + used, sizeof(parts) - used, "%s%s %.2f",
                                                 i > 0 ? ", " : "", kPass[i], v);
                        if (used >= sizeof(parts))
                        {
                            break;
                        }
                    }
                    fprintf(f,
                            "    gpu per pass (gpusplit; ms a frame): %s; sum %.2f in %.1f pieces "
                            "a frame (this segment's frame rate and CPU times mean nothing)\n",
                            parts, total, (double)s.pieces / n);
                }
                fprintf(f,
                        "    gpu: busy %.2f ms a frame (C3D_GetDrawingTime of the frame before), "
                        "peak %.2f%s%s\n",
                        ms(s.t[TIME_DRAWN], n), (double)g->gpuPeakMs,
                        g->m.fill ? "" : "; fill=off: what is left is vertices and set-up",
                        g->m.gpuSplit ? "; under gpusplit this is the present's alone" : "");
                const uint32* c = s.counts;
                const uint32* pk = g->countPeak;
                fprintf(f,
                        "    counts a frame (mean/peak): draws %.0f/%u; texture binds %.0f/%u, "
                        "program binds %.0f/%u, vertex formats %.0f/%u, vertex buffers %.0f/%u, "
                        "uniform registers %.0f/%u; blend %.0f, alpha test %.0f, depth %.0f, cull "
                        "%.0f, depth map %.0f, combiner %.0f, fog stage %.0f; target binds %.0f/%u, "
                        "copies %.0f/%u, env-map draws %.0f, fogged draws %.0f; command buffer "
                        "%.0f/%u KB, ring %.0f/%u KB, GX queue %.1f/%u, hard splits %u; vertices: "
                        "world %u, lit %u, im2d %u, im3d %u, skin %u, env %u, skinenv %u\n",
                        draws, (unsigned)g->drawsPeak, (double)c[COUNT_TEX_BINDS] / n,
                        (unsigned)pk[COUNT_TEX_BINDS], (double)c[COUNT_PROG_BINDS] / n,
                        (unsigned)pk[COUNT_PROG_BINDS], (double)c[COUNT_ATTR_SETS] / n,
                        (unsigned)pk[COUNT_ATTR_SETS], (double)c[COUNT_BUF_SETS] / n,
                        (unsigned)pk[COUNT_BUF_SETS], (double)c[COUNT_UNIFORM_REGS] / n,
                        (unsigned)pk[COUNT_UNIFORM_REGS], (double)c[COUNT_BLEND] / n,
                        (double)c[COUNT_ALPHA] / n, (double)c[COUNT_DEPTH] / n,
                        (double)c[COUNT_CULL] / n, (double)c[COUNT_DEPTHMAP] / n,
                        (double)c[COUNT_TEV] / n, (double)c[COUNT_FOGSTAGE] / n,
                        (double)c[COUNT_TARGET_BINDS] / n, (unsigned)pk[COUNT_TARGET_BINDS],
                        (double)c[COUNT_COPIES] / n, (unsigned)pk[COUNT_COPIES],
                        (double)c[COUNT_ENV_DRAWS] / n, (double)c[COUNT_FOG_DRAWS] / n,
                        (double)s.cmdBytes / n / 1024.0, (unsigned)(g->cmdPeak >> 10),
                        (double)s.ringBytes / n / 1024.0, (unsigned)(g->ringPeak >> 10),
                        (double)s.queue / n, (unsigned)g->queuePeak, (unsigned)s.splits,
                        s.verts[PROG_WORLD] / n, s.verts[PROG_LIT] / n, s.verts[PROG_IM2D] / n,
                        s.verts[PROG_IM3D] / n, s.verts[PROG_SKIN] / n, s.verts[PROG_ENV] / n,
                        s.verts[PROG_SKINENV] / n);
                Stats st;
                getMemoryStats(&st);
                uint32 vbMoved = 0, vbStayed = 0;
                vbVramStats(&vbMoved, &vbStayed);
                fprintf(f,
                        "    memory: VRAM free at least %u KB, linear free at least %u KB; %u "
                        "textures (VRAM %u KB, linear %u KB); camera textures and copy blocks "
                        "%u (%u KB of VRAM)%s",
                        (unsigned)(g->vramMin >> 10), (unsigned)(g->linearMin >> 10),
                        st.textures, st.texVram >> 10, st.texLinear >> 10, st.targets,
                        st.targetVram >> 10, vbVramOn() ? "" : "\n");
                if (vbVramOn())
                {
                    fprintf(f, "; vbvram: %u KB of vertex buffers in VRAM, %u KB left in linear "
                               "memory\n",
                            (unsigned)vbMoved, (unsigned)vbStayed);
                }
                StereoProbeStats so;
                const bool haveStereo = stereoProbeTake(&so) != 0;
                char stereo[256];
                const char* what = g->m.stereo == stereomath::MODE_OFF    ? "off" :
                                   g->m.stereo == stereomath::MODE_AUTO   ? "auto (the slider)" :
                                   g->m.stereo == stereomath::MODE_ALWAYS ? "always (the slider, no guard)" :
                                   g->m.stereoGuard                       ? "force:50 with the guard" :
                                                                            "force:50";
                if (!haveStereo || g->stereoAsked)
                {
                    snprintf(stereo, sizeof(stereo), "%s%s", what,
                             g->m.stereo == stereomath::MODE_OFF ? "" :
                                                                   " asked for; not taken, drawn in 2D");
                }
                else
                {
                    snprintf(stereo, sizeof(stereo),
                             "%s: %u frames, %u drawn as 3D frames, %u of them with two eyes; "
                             "right eyes dropped: %u hard split, %u GX queue, %u read-back, %u "
                             "flat, %u room; %u frames 2D under the guard (%u trips); GX entries "
                             "peak %u",
                             what, (unsigned)so.frames, (unsigned)so.frames3d,
                             (unsigned)so.rightDrawn, (unsigned)so.droppedSplit,
                             (unsigned)so.droppedQueue, (unsigned)so.droppedReadback,
                             (unsigned)so.droppedFlat, (unsigned)so.droppedRoom,
                             (unsigned)so.guardFrames, (unsigned)so.guardTrips,
                             (unsigned)so.gxEntriesPeak);
                }
                // Core 1 lends the game 300 ms a second; core 2 is the game's
                // alone (i3DS.cpp's threadCreate puts the mixer there).
                const int core = i3DSAudioCore();
                fprintf(f,
                        "    audio (core %d): %.1f ms a second inside the mixing callback (%.1f%% of "
                        "%s; wall time, so the callback's "
                        "waits for the core and the sound lock count too), %u callbacks, longest "
                        "%.2f ms%s; 3D slider %.2f; stereo: %s\n",
                        core, audioSeconds > 0.0 ? audioMs / audioSeconds : 0.0,
                        audioSeconds > 0.0 ? audioMs / audioSeconds /
                                                 (core == 1 ? 300.0 : 1000.0) * 100.0 :
                                             0.0,
                        core == 1 ? "the 300 ms a second core 1 lends the game" :
                                    "core 2's second, which has no time limit",
                        (unsigned)audioCalls, audioMaxMs,
                        audioCalls == 0 ? " (no audio device)" : "", g->slider / n, stereo);
            }

            // TSSM_C3D_AB_LOG=<file>: each segment's figures appended to it as
            // well, under TSSM_C3D_AB_TAG (the frame-rate probe's
            // fps-effects.log, one line a level): the median, p95, the share
            // of frames over 33.3 ms, and the CPU's and the GPU's busy time.
            void logSegment(int32 i, const Segment* g)
            {
                uint32_t audioCalls = 0;
                uint64_t audioNs = 0, audioMaxNs = 0;
                i3DSAudioMixTake(&audioCalls, &audioNs, &audioMaxNs);
                const char* path = getenv("TSSM_C3D_AB_LOG");
                if (path == nil || path[0] == '\0' || i == 0)
                {
                    StereoProbeStats so;
                    stereoProbeTake(&so);
                    return;
                }
                FILE* f = fopen(path, "a");
                if (f == nil)
                {
                    return;
                }
                const char* tag = getenv("TSSM_C3D_AB_TAG");
                const uint32 n = timedFrames(g);
                const double mean = ms(g->sum.wall, n);
                const double gpu = ms(g->sum.t[TIME_DRAWN], n);
                const double cpu = mean - ms(g->sum.t[TIME_SYNC], n) - ms(g->sum.t[TIME_GPU], n);
                char mt[320];
                fprintf(f,
                        "%s%s%s: frames %d-%d (%u, %u over 1 s left out), %.1f fps; frame ms median "
                        "%.2f, p95 %.2f, max %.2f; over 33.3 ms: %u of %u (%.1f%%; two vblanks "
                        "are 33.43 ms), over 40 ms (a vblank missed at the 30 cap): %u (%.1f%%); "
                        "CPU busy "
                        "%.2f ms a frame (%.0f%%), GPU busy %.2f ms (%.0f%%), vblank wait %.2f "
                        "ms, GPU wait %.2f ms; %s\n",
                        tag != nil ? tag : "segment", g->label[0] != '\0' ? " " : "", g->label,
                        (int)g->first, (int)g->last, (unsigned)g->frames, (unsigned)g->slow,
                        mean > 0.0 ? 1000.0 / mean : 0.0, g->medianMs, g->p95Ms, g->maxMs,
                        (unsigned)g->over33, (unsigned)n, n > 0 ? 100.0 * g->over33 / n : 0.0,
                        (unsigned)g->over40, n > 0 ? 100.0 * g->over40 / n : 0.0, cpu,
                        mean > 0.0 ? 100.0 * cpu / mean : 0.0, gpu,
                        mean > 0.0 ? 100.0 * gpu / mean : 0.0, ms(g->sum.t[TIME_SYNC], n),
                        ms(g->sum.t[TIME_GPU], n), modesText(&g->m, mt, sizeof(mt)));
                if (sDetail)
                {
                    logDetail(f, g, (double)audioNs / 1e6, audioCalls, (double)audioMaxNs / 1e6,
                              g->seconds);
                }
                fclose(f);
            }

            void closeSegment(int32 lastFrame)
            {
                Segment* g = &sSegs[sNumSegs - 1];
                g->last = lastFrame;
                if (sNumWalls > 0)
                {
                    qsort(sWalls, sNumWalls, sizeof(uint32), cmpU32);
                    g->medianMs = sWalls[sNumWalls / 2] / 1000.0;
                    g->p90Ms = sWalls[(sNumWalls * 9) / 10] / 1000.0;
                    g->p95Ms = sWalls[(sNumWalls * 19) / 20] / 1000.0;
                    uint32 over = 0, missed = 0;
                    for (uint32 i = 0; i < sNumWalls; i++)
                    {
                        over += sWalls[i] > 33333 ? 1 : 0;
                        missed += sWalls[i] > 40000 ? 1 : 0;
                    }
                    g->over33 = over;
                    g->over40 = missed;
                    g->maxMs = sWalls[sNumWalls - 1] / 1000.0;
                }
                printSegment(sNumSegs - 1, g);
                logSegment(sNumSegs - 1, g);
                fflush(stdout);
                sNumWalls = 0;
            }
        } // namespace

        bool32 parseModes(const char* text, Modes* m)
        {
            const char* p = text;
            while (*p != '\0')
            {
                const char* end = strchr(p, '+');
                const uint32 len = end != nil ? (uint32)(end - p) : (uint32)strlen(p);
                const char* eq = (const char*)memchr(p, '=', len);
                if (eq == nil ||
                    !setOne(m, p, (uint32)(eq - p), eq + 1, len - (uint32)(eq - p) - 1))
                {
                    return 0;
                }
                p += len + (end != nil ? 1 : 0);
            }
            return 1;
        }

        const char* modesText(const Modes* m, char* buf, uint32 size)
        {
            static const char* kSkin[] = { "gpu", "cpu", "bind", "off" };
            static const char* kStereo[] = { "off", "auto", "force", "always" };
            // The measuring words only when they are not the default, so the
            // lines of a normal run read as before.
            snprintf(buf, size,
                     "skin=%s light=%s env=%s fog=%s rtt=%s shadow=%s rttflip=%s glow=%s warp=%s "
                     "copyflip=%s orthoz=%s copyedge=%s copy=%s hudz=%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
                     kSkin[m->skin & 3], m->light ? "on" : "off", m->env ? "on" : "off",
                     m->fog ? "on" : "off", m->rtt ? "on" : "off", m->shadow ? "on" : "off",
                     m->rttFlip ? "on" : "off", m->glow ? "on" : "off", m->warp ? "on" : "off",
                     m->copyFlip ? "on" : "off", m->orthoZ ? "on" : "off",
                     m->copyEdge ? "on" : "off", m->copy ? "on" : "off",
                     m->hudZ ? "on" : "off", m->profile ? " profile=on" : "",
                     m->gpuSplit ? " gpusplit=on" : "", m->fill ? "" : " fill=off",
                     m->cmdFlush ? " flush=cmdlist" : "", m->statsLine ? "" : " statsline=off",
                     m->refresh ? "" : " refresh=off", m->fast ? "" : " fast=off",
                     m->pipe ? "" : " pipe=off", m->eyeSkip ? "" : " eyeskip=off",
                     m->alphaFill ? " alphafill=on" : "", m->skinTrim ? "" : " skintrim=off",
                     m->glowClear ? " glowclear=on" : "", m->saveVram ? "" : " savevram=off",
                     m->eyeTest == EYETEST_2D      ? " eyetest=2d" :
                     m->eyeTest == EYETEST_NOSHIFT ? " eyetest=noshift" :
                     m->eyeTest == EYETEST_NORIGHT ? " eyetest=noright" :
                     m->eyeTest == EYETEST_NOSAVE  ? " eyetest=nosave" :
                                                     "",
                     m->stereo != stereomath::MODE_OFF ? " stereo=" : "",
                     m->stereo == stereomath::MODE_OFF ? "" :
                     m->stereoGuard                    ? "guard" :
                                                         kStereo[m->stereo & 3]);
            return buf;
        }

        void modesInit(const char* effects)
        {
            static bool done;
            if (done)
            {
                return;
            }
            done = true;
            const uint32 fx = parseEffects(effects);
            modes.fog = (fx & FX_FOG) != 0;
            modes.rtt = (fx & FX_RTT) != 0;
            modes.shadow = (fx & FX_SHADOW) != 0;
            modes.glow = (fx & FX_GLOW) != 0;
            modes.warp = (fx & FX_WARP) != 0;
            modes.copy = (fx & FX_COPY) != 0;
            modes.orthoZ = (fx & FX_ORTHOZ) != 0;
            selfTestOn = (fx & FX_SELFTEST) != 0;

            // The switches win, one each, nothing implied.
            struct Env
            {
                const char* name;
                const char* key;
            };
            static const Env kEnv[] = {
                { "TSSM_C3D_SKIN", "skin" },       { "TSSM_C3D_LIGHT", "light" },
                { "TSSM_C3D_ENV", "env" },         { "TSSM_C3D_FOG", "fog" },
                { "TSSM_C3D_RTT", "rtt" },         { "TSSM_C3D_SHADOW", "shadow" },
                { "TSSM_C3D_RTTFLIP", "rttflip" }, { "TSSM_C3D_GLOW", "glow" },
                { "TSSM_C3D_WARP", "warp" },       { "TSSM_C3D_COPYFLIP", "copyflip" },
                { "TSSM_C3D_ORTHOZ", "orthoz" },   { "TSSM_C3D_COPYEDGE", "copyedge" },
                { "TSSM_C3D_COPY", "copy" },       { "TSSM_C3D_HUDZ", "hudz" },
                { "TSSM_C3D_REFRESH", "refresh" }, { "TSSM_C3D_FAST", "fast" },
                { "TSSM_C3D_EYESKIP", "eyeskip" }, { "TSSM_C3D_ALPHAFILL", "alphafill" },
                { "TSSM_C3D_SKINTRIM", "skintrim" }, { "TSSM_C3D_GLOWCLEAR", "glowclear" },
                { "TSSM_C3D_SAVEVRAM", "savevram" },
            };
            char set[320];
            uint32 setLen = 0;
            set[0] = '\0';
            for (const Env& e : kEnv)
            {
                if (fromEnv(e.name, e.key, &modes) && setLen < sizeof(set))
                {
                    setLen += (uint32)snprintf(set + setLen, sizeof(set) - setLen, " %s=%s", e.name,
                                               getenv(e.name));
                }
            }
            const char* st = getenv("TSSM_C3D_SELFTEST");
            if (st != nil && (strcmp(st, "on") == 0 || strcmp(st, "off") == 0))
            {
                selfTestOn = st[1] == 'n';
                if (setLen < sizeof(set))
                {
                    setLen += (uint32)snprintf(set + setLen, sizeof(set) - setLen,
                                               " TSSM_C3D_SELFTEST=%s", st);
                }
            }
            else if (st != nil && st[0] != '\0')
            {
                printf("tssm: c3d: TSSM_C3D_SELFTEST=%s is not on or off; ignored\n", st);
            }

            // The one line that says which effects this run has.
            const struct
            {
                const char* name;
                bool32 on;
            } kOn[] = { { "fog", modes.fog },       { "rtt", modes.rtt },
                        { "shadow", modes.shadow }, { "glow", modes.glow },
                        { "warp", modes.warp },     { "copy", modes.copy },
                        { "orthoz", modes.orthoZ }, { "selftest", selfTestOn } };
            char on[96], off[96];
            uint32 onLen = 0, offLen = 0;
            on[0] = off[0] = '\0';
            for (const auto& k : kOn)
            {
                char* b = k.on ? on : off;
                uint32* l = k.on ? &onLen : &offLen;
                *l += (uint32)snprintf(b + *l, sizeof(on) - *l, "%s%s", *l > 0 ? "," : "", k.name);
            }
            printf("tssm: c3d: effects on: %s (off: %s); config.ini [3ds] effects = %s (the "
                   "default: all, which leaves out only the self-test)%s%s\n",
                   onLen > 0 ? on : "none", offLen > 0 ? off : "none",
                   effects != nil && effects[0] != '\0' ? effects : "none",
                   setLen > 0 ? "; switches set:" : "", set);

            char mt[240];
            printf("tssm: c3d: modes %s (TSSM_C3D_SKIN=gpu|cpu|bind|off; TSSM_C3D_LIGHT, "
                   "TSSM_C3D_ENV, TSSM_C3D_FOG, TSSM_C3D_RTT, TSSM_C3D_SHADOW, TSSM_C3D_RTTFLIP, "
                   "TSSM_C3D_GLOW, TSSM_C3D_WARP, TSSM_C3D_COPYFLIP, TSSM_C3D_ORTHOZ, "
                   "TSSM_C3D_COPYEDGE, TSSM_C3D_COPY, TSSM_C3D_HUDZ, TSSM_C3D_SELFTEST =on|off; "
                   "TSSM_C3D_AB)\n",
                   modesText(&modes, mt, sizeof(mt)));
            if (!modes.light && modes.skin == SKIN_GPU)
            {
                printf("tssm: c3d: light=off leaves vs_skin's lighting on (it has no unlit "
                       "variant)\n");
            }
            fflush(stdout);
        }

        void abNoteFrame(const FrameTimes* f)
        {
            if (!sOn || sDone)
            {
                return;
            }
            Segment* g = &sSegs[sNumSegs - 1];
            g->frames++;
            g->seconds += ms(f->wall, 1) / 1000.0;
            if (g->frames <= kSkipAfterSwitch && sNumSegs > 1)
            {
                // A switch's own work (a fog ramp dropped, the summary
                // line's SD write) lands in the first frames after it.
                g->skipped++;
                return;
            }
            if (f->wall > (u64)SYSCLOCK_ARM11)
            {
                g->slow++;
                return;
            }
            g->sum.wall += f->wall;
            for (int i = 0; i < NUM_TIMES; i++)
            {
                g->sum.t[i] += f->t[i];
            }
            for (int i = 0; i < NUM_PROGRAMS; i++)
            {
                g->sum.verts[i] += f->verts[i];
            }
            for (int i = 0; i < NUM_PROF; i++)
            {
                g->sum.prof[i] += f->prof[i];
            }
            for (int i = 0; i < NUM_PASSES; i++)
            {
                g->sum.passMs[i] += f->passMs[i];
            }
            for (int i = 0; i < NUM_COUNTS; i++)
            {
                g->sum.counts[i] += f->counts[i];
                if (f->counts[i] > g->countPeak[i])
                {
                    g->countPeak[i] = f->counts[i];
                }
            }
            g->sum.pieces += f->pieces;
            g->sum.profReads += f->profReads;
            g->sum.draws += f->draws;
            g->sum.cmdBytes += f->cmdBytes;
            g->sum.ringBytes += f->ringBytes;
            g->sum.queue += f->queue;
            g->sum.splits += f->splits;
            g->drawsPeak = f->draws > g->drawsPeak ? f->draws : g->drawsPeak;
            g->cmdPeak = f->cmdBytes > g->cmdPeak ? f->cmdBytes : g->cmdPeak;
            g->ringPeak = f->ringBytes > g->ringPeak ? f->ringBytes : g->ringPeak;
            g->queuePeak = f->queue > g->queuePeak ? f->queue : g->queuePeak;
            const float gpu = (float)ms(f->t[TIME_DRAWN], 1);
            g->gpuPeakMs = gpu > g->gpuPeakMs ? gpu : g->gpuPeakMs;
            g->sum.reads += f->reads + 5;
            if (sDetail)
            {
                const uint32 v = (uint32)vramSpaceFree();
                const uint32 l = (uint32)linearSpaceFree();
                g->vramMin = v < g->vramMin ? v : g->vramMin;
                g->linearMin = l < g->linearMin ? l : g->linearMin;
                g->slider += osGet3DSliderState();
            }
            if (sNumWalls < (uint32)kMaxWalls)
            {
                sWalls[sNumWalls++] = (uint32)(ms(f->wall, 1) * 1000.0);
            }
        }

        void testFrame(int frame)
        {
            if (!sParsed)
            {
                parse();
            }
            if (!sOn || sDone || !submitIsOpen())
            {
                return;
            }
            if (sNextSwitch < sNumSwitches && frame >= sSwitches[sNextSwitch].frame)
            {
                closeSegment(frame);
                const bool32 hadFog = modes.fog;
                modes = sSwitches[sNextSwitch].to;
                if (hadFog && !modes.fog)
                {
                    dropFogRamp("fog=off");
                }
                Segment* g = &sSegs[sNumSegs++];
                openSegment(g, frame + 1, sSwitches[sNextSwitch].label);
                char mt[320];
                printf("tssm: c3d: ab: frame %d: %s%s%s from the next frame\n", frame,
                       modesText(&modes, mt, sizeof(mt)), g->label[0] != '\0' ? " #" : "",
                       g->label);
                if (g->stereoAsked)
                {
                    printf("tssm: c3d: ab: stereo=%s asked for; the stereo code could not take it "
                           "(no 3D at the open), so the segment draws in 2D\n",
                           modes.stereoGuard ? "guard" : "force");
                }
                fflush(stdout);
                sNextSwitch++;
            }
            if (sExitAfter > 0 && frame >= sExitAfter)
            {
                closeSegment(frame);
                sDone = true;
                printf("tssm: c3d: ab: all %d segments:\n", (int)sNumSegs);
                for (int32 i = 0; i < sNumSegs; i++)
                {
                    printSegment(i, &sSegs[i]);
                }
                fflush(stdout);
            }
        }

        void abLog(const char* fmt, ...)
        {
            const char* path = getenv("TSSM_C3D_AB_LOG");
            if (path == nil || path[0] == '\0')
            {
                return;
            }
            FILE* f = fopen(path, "a");
            if (f == nil)
            {
                return;
            }
            const char* tag = getenv("TSSM_C3D_AB_TAG");
            fprintf(f, "%s: ", tag != nil ? tag : "c3d");
            va_list ap;
            va_start(ap, fmt);
            vfprintf(f, fmt, ap);
            va_end(ap);
            fputc('\n', f);
            fclose(f);
        }
    } // namespace c3d
} // namespace rw
