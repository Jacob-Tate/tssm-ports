// rw::c3d: the drawing modes and the one-run A/B switch (docs/3DS.md).
//
// A console run needs the user and the game cannot be restarted cheaply, so
// TSSM_C3D_AB switches the modes at given frames of one run:
//
//   TSSM_C3D_AB=<frame>:<settings>,<frame>:<settings>,...
//   settings: skin=gpu|cpu|bind|off; light, env, fog, rtt, shadow, rttflip,
//   glow, warp, copyflip, orthoz, copyedge, copy, hudz =on|off; joined by '+'
//
// fog=on draws fog only when the fog ramp was made at the open (fog on then,
// by [3ds] effects or TSSM_C3D_FOG).
//
// From the frame after <frame> the settings apply. Each switch logs the
// segment that ended (frames, seconds, fps, the frame times' median and
// where they went); at TSSM_EXIT_AFTER's frame every segment is listed once
// more. Frames over a second (a capture's PNG, a load) are counted but left
// out of the steady figures.

#include "c3dimpl.h"

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
        // a console (atomicDepth).
        Modes modes = { SKIN_GPU, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1 };
        bool32 selfTestOn;

        namespace
        {
            bool setOne(Modes* m, const char* key, uint32 klen, const char* val, uint32 vlen)
            {
                struct Word
                {
                    const char* key;
                    const char* val;
                    int32 which; // 0 skin, else the bool32 it sets (fields())
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
                    { "hudz", "off", 13, 0 },
                };
                for (const Word& w : kWords)
                {
                    if (strlen(w.key) == klen && strncmp(w.key, key, klen) == 0 &&
                        strlen(w.val) == vlen && strncmp(w.val, val, vlen) == 0)
                    {
                        bool32* flags[] = { nil,      &m->light,    &m->env,     &m->fog,
                                            &m->rtt,  &m->shadow,   &m->rttFlip, &m->glow,
                                            &m->warp, &m->copyFlip, &m->orthoZ,  &m->copyEdge,
                                            &m->copy,     &m->hudZ };
                        if (w.which == 0)
                            m->skin = w.to;
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
            };

            // A stretch of frames under one setting.
            struct Segment
            {
                int32 first, last; // the capture hook's frame numbers
                Modes m;
                uint32 frames; // presents timed
                uint32 slow; // of them over a second, left out below
                FrameTimes sum; // of the others
                double medianMs, p90Ms, p95Ms, maxMs;
                // Steady frames over 33.3 ms, and over 40 ms: the screen's two
                // vblanks are 33.43 ms (59.83 Hz), so at the 30 fps cap
                // nearly every frame is over 33.3 ms and one over 40 missed
                // its pair of vblanks.
                uint32 over33, over40;
                double seconds; // every frame's wall time
            };

            const int32 kMaxSwitches = 32;
            const int32 kMaxWalls = 16384;
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
                Modes m = modes;
                int32 lastFrame = 0;
                const char* p = e;
                while (*p != '\0' && sNumSwitches < kMaxSwitches)
                {
                    const char* end = strchr(p, ',');
                    const uint32 len = end != nil ? (uint32)(end - p) : (uint32)strlen(p);
                    char entry[128];
                    snprintf(entry, sizeof(entry), "%.*s", (int)len, p);
                    p += len + (end != nil ? 1 : 0);

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
                memset(&sSegs[0], 0, sizeof(sSegs[0]));
                sSegs[0].first = 1;
                sSegs[0].m = modes;
                char mt[224];
                printf("tssm: c3d: ab: %d switches; frames 1-%d %s\n", (int)sNumSwitches,
                       (int)sSwitches[0].frame, modesText(&modes, mt, sizeof(mt)));
                fflush(stdout);
            }

            int cmpU32(const void* a, const void* b)
            {
                const uint32 x = *(const uint32*)a, y = *(const uint32*)b;
                return x < y ? -1 : x > y ? 1 : 0;
            }

            void printSegment(int32 i, const Segment* g)
            {
                char mt[224];
                const uint32 n = g->frames - g->slow;
                const double mean = ms(g->sum.wall, n);
                const double busy = mean - ms(g->sum.t[TIME_SYNC], n);
                printf(
                    "tssm: c3d: ab: segment %d, frames %d-%d, %s: %u frames in %.2f s = %.1f fps; "
                    "steady (%u over 1 s left out): %.1f fps, frame ms median %.2f, p90 %.2f, "
                    "p95 %.2f, max %.2f, over 33.3 ms %u, over 40 ms %u, mean %.2f = vblank wait "
                    "%.2f + busy "
                    "%.2f (recording %.2f, CPU "
                    "skinning %.2f, GPU wait %.2f, FrameEnd %.2f); GPU %.2f ms; vertices a "
                    "frame: world %u, lit %u, im2d %u, im3d %u, skin %u, env %u, skinenv %u; "
                    "the timing itself about %.2f ms a frame\n",
                    (int)i, (int)g->first, (int)g->last, modesText(&g->m, mt, sizeof(mt)),
                    (unsigned)g->frames, g->seconds,
                    g->seconds > 0.0 ? g->frames / g->seconds : 0.0, (unsigned)g->slow,
                    mean > 0.0 ? 1000.0 / mean : 0.0, g->medianMs, g->p90Ms, g->p95Ms, g->maxMs,
                    (unsigned)g->over33, (unsigned)g->over40, mean,
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

            // TSSM_C3D_AB_LOG=<file>: each segment's figures appended to it as
            // well, under TSSM_C3D_AB_TAG (the frame-rate probe's
            // fps-effects.log, one line a level): the median, p95, the share
            // of frames over 33.3 ms, and the CPU's and the GPU's busy time.
            void logSegment(int32 i, const Segment* g)
            {
                const char* path = getenv("TSSM_C3D_AB_LOG");
                if (path == nil || path[0] == '\0' || i == 0)
                {
                    return;
                }
                FILE* f = fopen(path, "a");
                if (f == nil)
                {
                    return;
                }
                const char* tag = getenv("TSSM_C3D_AB_TAG");
                const uint32 n = g->frames - g->slow;
                const double mean = ms(g->sum.wall, n);
                const double gpu = ms(g->sum.t[TIME_DRAWN], n);
                const double cpu = mean - ms(g->sum.t[TIME_SYNC], n) - ms(g->sum.t[TIME_GPU], n);
                char mt[224];
                fprintf(f,
                        "%s: frames %d-%d (%u, %u over 1 s left out), %.1f fps; frame ms median "
                        "%.2f, p95 %.2f, max %.2f; over 33.3 ms: %u of %u (%.1f%%; two vblanks "
                        "are 33.43 ms), over 40 ms (a vblank missed at the 30 cap): %u (%.1f%%); "
                        "CPU busy "
                        "%.2f ms a frame (%.0f%%), GPU busy %.2f ms (%.0f%%), vblank wait %.2f "
                        "ms, GPU wait %.2f ms; %s\n",
                        tag != nil ? tag : "segment", (int)g->first, (int)g->last,
                        (unsigned)g->frames, (unsigned)g->slow, mean > 0.0 ? 1000.0 / mean : 0.0,
                        g->medianMs, g->p95Ms, g->maxMs, (unsigned)g->over33, (unsigned)n,
                        n > 0 ? 100.0 * g->over33 / n : 0.0, (unsigned)g->over40,
                        n > 0 ? 100.0 * g->over40 / n : 0.0, cpu, mean > 0.0 ? 100.0 * cpu / mean : 0.0,
                        gpu, mean > 0.0 ? 100.0 * gpu / mean : 0.0, ms(g->sum.t[TIME_SYNC], n),
                        ms(g->sum.t[TIME_GPU], n), modesText(&g->m, mt, sizeof(mt)));
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
            snprintf(buf, size,
                     "skin=%s light=%s env=%s fog=%s rtt=%s shadow=%s rttflip=%s glow=%s warp=%s "
                     "copyflip=%s orthoz=%s copyedge=%s copy=%s hudz=%s",
                     kSkin[m->skin & 3], m->light ? "on" : "off", m->env ? "on" : "off",
                     m->fog ? "on" : "off", m->rtt ? "on" : "off", m->shadow ? "on" : "off",
                     m->rttFlip ? "on" : "off", m->glow ? "on" : "off", m->warp ? "on" : "off",
                     m->copyFlip ? "on" : "off", m->orthoZ ? "on" : "off",
                     m->copyEdge ? "on" : "off", m->copy ? "on" : "off",
                     m->hudZ ? "on" : "off");
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
            g->sum.reads += f->reads + 5;
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
                memset(g, 0, sizeof(*g));
                g->first = frame + 1;
                g->m = modes;
                char mt[224];
                printf("tssm: c3d: ab: frame %d: %s from the next frame\n", frame,
                       modesText(&modes, mt, sizeof(mt)));
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
    } // namespace c3d
} // namespace rw
