// rw::c3d: camera textures, render to texture.
//
// A Raster::CAMERATEXTURE gets, at its first clear or camera update, a VRAM
// RGBA8 texture (powers of two, the picture from texel (0, 0) with the
// raster's uv scale) and a citro3d render target made from it, with a D16
// depth buffer when its camera has a Z raster of its own size (xShadow's).
// The camera's draws go to that target in mid-frame; the target switch back
// to the screen flushes it (citro3d), and the texture's generation makes the
// next draw that samples it bind it again, which clears the texture cache.
// Nothing reaches the GPU before C3D_FrameEnd, so several targets in a frame
// cost no GX queue entries: clears are quads, never fills.
//
// What only a console can prove is off unless config.ini's [3ds] effects or a
// switch turns it on (TSSM_C3D_RTT, TSSM_C3D_SHADOW, TSSM_C3D_RTTFLIP,
// TSSM_C3D_COPY, TSSM_C3D_AB; c3dab.cpp), and with selftest on it is tried
// once at start-up (rttSelfTest), each step in a frame of its own, logged
// before it runs and again when the GPU is through it, and each read-back
// logged before it, so that a lock-up names its step.

#include "c3dimpl.h"

#include <stdio.h>
#include <stdlib.h>

namespace rw
{
    namespace c3d
    {
        namespace
        {
            int32 pow2At8(int32 v)
            {
                int32 p = 8;
                while (p < v && p < 1024)
                {
                    p <<= 1;
                }
                return p;
            }

            // xShadow's camera: a Z raster of its own size.
            bool hasOwnDepth(const Camera* cam, const Raster* fb)
            {
                const Raster* z = cam != nil ? cam->zBuffer : nil;
                return z != nil && z->type == Raster::ZBUFFER && z->width == fb->width &&
                       z->height == fb->height;
            }

            // The raster's storage and target, made at its first use.
            C3dRaster* ensure(Camera* cam, Raster* fb)
            {
                Raster* ras = fb->parent != nil ? fb->parent : fb;
                if (ras != fb)
                {
                    logOnce("rttsub", "a camera draws into part of a camera texture; it is drawn "
                                      "into the whole");
                }
                if (ras->platform != PLATFORM_C3D || ras->type != Raster::CAMERATEXTURE)
                {
                    return nil;
                }
                C3dRaster* natras = GETC3DRASTEREXT(ras);
                if (natras->screenCopy != COPY_NONE)
                {
                    logOnce("rttcopy", "a camera draws into a screen copy; it is left out");
                    return nil;
                }
                const bool shadow = hasOwnDepth(cam, ras);
                if (natras->target == nil)
                {
                    natras->isShadow = shadow ? 1 : 0;
                }
                if (!modes.rtt)
                {
                    logOnce("rttoff", "rtt=off: a camera drawing into a texture is left out");
                    return nil;
                }
                if (natras->isShadow && !modes.shadow)
                {
                    logOnce("shadowoff", "shadow=off: the shadow camera is left out");
                    return nil;
                }
                if (natras->target != nil)
                {
                    return natras;
                }
                const int32 w = pow2At8(ras->width);
                const int32 h = pow2At8(ras->height);
                if (!targetCreate(natras, w, h, natras->isShadow))
                {
                    logOnce("rttvram",
                            "a %dx%d camera texture could not be made (VRAM %u KB "
                            "free); its camera is left out",
                            (int)w, (int)h, (unsigned)(vramSpaceFree() >> 10));
                    return nil;
                }
                natras->usedW = (int16)(ras->width < w ? ras->width : w);
                natras->usedH = (int16)(ras->height < h ? ras->height : h);
                natras->uScale = (float)natras->usedW / (float)w;
                natras->vScale = (float)natras->usedH / (float)h;
                natras->rendered = 0;
                natras->alphaKind = ALPHAGRADED;
                printf("tssm: c3d: camera texture %dx%d: %dx%d RGBA8%s in VRAM (%u KB)%s; VRAM "
                       "free %u KB\n",
                       (int)ras->width, (int)ras->height, (int)w, (int)h,
                       natras->isShadow ? " + D16" : "",
                       (unsigned)((w * h * 4 + (natras->isShadow ? w * h * 2 : 0)) >> 10),
                       natras->isShadow ? ", the shadow camera's" : "",
                       (unsigned)(vramSpaceFree() >> 10));
                fflush(stdout);
                return natras;
            }

            // ----------------------------------------------------- self-test

            struct TestVertex
            {
                float x, y, z, w;
                uint8 b, g, r, a;
                float u, v;
            };

            // An Im2D rectangle into the bound tw x th target, in its pixels:
            // y = 0 at clip +k (k = -1: the target's own rows, bottom up). uv:
            // u_uvMat's rows (nil: u, v = x / 16, y / 16 as they are).
            void testRectIn(float tw, float th, float k, const float* uv, float x0, float y0,
                            float x1, float y1, float w, uint32 rgba, bool textured)
            {
                TestVertex* v = (TestVertex*)ringAlloc(4 * sizeof(TestVertex));
                if (v == nil)
                {
                    return;
                }
                const float xs[4] = { x0, x0, x1, x1 };
                const float ys[4] = { y0, y1, y0, y1 };
                for (int i = 0; i < 4; i++)
                {
                    v[i].x = xs[i];
                    v[i].y = ys[i];
                    v[i].z = 0.0f;
                    v[i].w = w;
                    v[i].r = (uint8)(rgba >> 24);
                    v[i].g = (uint8)(rgba >> 16);
                    v[i].b = (uint8)(rgba >> 8);
                    v[i].a = (uint8)rgba;
                    v[i].u = xs[i] / 16.0f;
                    v[i].v = ys[i] / 16.0f;
                }
                useProgram(PROG_IM2D);
                useFormat(FORMAT_IM2D, v, sizeof(TestVertex));
                setUniform4(REG_SCRMAT, 2.0f / tw, 0.0f, -1.0f, 0.0f);
                setUniform4(REG_SCRMAT + 1, 0.0f, -2.0f * k / th, k, 0.0f);
                static const float kIdentity[8] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
                const float* m = uv != nil ? uv : kIdentity;
                setUniform4(REG_UVMAT, m[0], m[1], m[2], m[3]);
                setUniform4(REG_UVMAT + 1, m[4], m[5], m[6], m[7]);
                C3D_TexEnv* e = C3D_GetTexEnv(0);
                C3D_TexEnvInit(e);
                C3D_TexEnvSrc(e, C3D_Both, textured ? GPU_TEXTURE0 : GPU_PRIMARY_COLOR,
                              GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
                C3D_AlphaTest(false, GPU_ALWAYS, 0);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                C3D_CullFace(GPU_CULL_NONE);
                C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
                drawArrays(GPU_TRIANGLE_STRIP, 0, 4);
            }

            // As the device draws a camera texture's 2D (c3ddevice.cpp's
            // setIm2DMatrix): y = 0 at clip +1, or -1 with rttFlip.
            void testRect(float x0, float y0, float x1, float y1, float w, uint32 rgba,
                          bool textured)
            {
                testRectIn(16.0f, 16.0f, modes.rttFlip ? -1.0f : 1.0f, nil, x0, y0, x1, y1, w, rgba,
                           textured);
            }

            // One step in a frame of its own: logged first, so that a GPU
            // lock-up (C3D_FrameBegin waiting for ever) names it.
            void testStep(const char* what)
            {
                printf("tssm: c3d: self-test: %s\n", what);
                fflush(stdout);
            }

            // The step's frame sent, then waited for: the line after it says
            // the GPU got through it (a lock-up leaves the step's own line
            // last).
            void testEnd(const char* tag)
            {
                frameEnd();
                deviceStateDirty();
                gpuWait();
                printf("tssm: c3d: self-test: %s: its GPU work finished\n", tag);
                fflush(stdout);
            }

            // Before a read-back: logged, so that a lock-up there names it.
            void testRead(const char* what)
            {
                printf("tssm: c3d: self-test: reading back %s (a raw GX copy)\n", what);
                fflush(stdout);
            }

            // Bytes A, B, G, R at row y (0 = the target's top), column x.
            const uint8* px(const uint8* buf, int x, int y)
            {
                return buf + 4 * (y * 16 + x);
            }

            bool near(int v, int want)
            {
                return v >= want - 12 && v <= want + 12;
            }

            // Which half of a read-back 16 x 16 (column 8) is green: 0 the
            // top rows, 1 the bottom ones, -1 neither as drawn.
            int greenHalf(const uint8* buf)
            {
                const uint8* top = px(buf, 8, 3);
                const uint8* bot = px(buf, 8, 12);
                const bool topG = top[2] > 200 && top[3] < 50;
                const bool botG = bot[2] > 200 && bot[3] < 50;
                const bool topR = top[3] > 200 && top[2] < 50;
                const bool botR = bot[3] > 200 && bot[2] < 50;
                return topG && botR ? 0 : botG && topR ? 1 : -1;
            }
        } // namespace

        namespace
        {
            // Green rather than red at (x, y) of a read-back RGBA8 picture `w`
            // wide. Relative, not by level: in Azahar the top screen of these
            // unshown frames reads back at half the level drawn (the log gives
            // the levels).
            bool greenIn(const uint8* buf, int w, int x, int y)
            {
                const uint8* p = buf + 4 * (y * w + x);
                return p[2] > 24 && p[2] > 2 * p[3];
            }

            // The screen copy: the top screen, not shown, drawn
            // red with its target corner (0, 0)-(120, 200) green and copied.
            // The block is read back: with the copy as copyScreenInto means
            // it, target pixel (x, y) is block pixel (x, y), whose read-back
            // row is 511 - y, so the green is at read-back (60, 411). Then a
            // 16 x 16 is drawn from the copy through its uv matrix as a
            // landscape picture (the green at landscape X > 200, Y > 120: the
            // 16 x 16's right half and, its rows bottom up, its read-back's
            // top half), for what sampling the copy gives.
            void testCopy(void)
            {
                C3dRaster d, t;
                memset(&d, 0, sizeof(d));
                memset(&t, 0, sizeof(t));
                if (!targetCreate(&t, 16, 16, 0))
                {
                    printf("tssm: c3d: self-test: copy: no VRAM for a 16x16 target\n");
                    return;
                }
                testStep("copy 1/2: the top screen, not shown, drawn red with one corner green "
                         "and copied into a 256x512 block (a GX copy after a frame split)");
                frameEnsure();
                screenOutput(0);
                setScreenViewport(0, 0, SCREEN_W, SCREEN_H);
                clearQuad(0xFF0000FFu, 1, 0);
                testRectIn(240.0f, 400.0f, -1.0f, nil, 0.0f, 0.0f, 120.0f, 200.0f, 1.0f,
                           0x00FF00FFu, false);
                const bool copied = copyScreenInto(&d, 1) != 0;
                testEnd("copy 1/2");
                screenOutput(1);
                int q = -1;
                char seen[96] = "";
                if (copied)
                {
                    uint8* rd = (uint8*)malloc(256 * 512 * 4);
                    testRead("the copy's 256x512 block");
                    if (rd != nil && readTexture(&d.tex, rd))
                    {
                        // 0: as meant; 1: the rows the other way (copyflip);
                        // 2: the columns; 3: both.
                        const bool g[4] = { greenIn(rd, 256, 60, 411), greenIn(rd, 256, 60, 211),
                                            greenIn(rd, 256, 180, 411),
                                            greenIn(rd, 256, 180, 211) };
                        const int n =
                            (g[0] ? 1 : 0) + (g[1] ? 1 : 0) + (g[2] ? 1 : 0) + (g[3] ? 1 : 0);
                        q = n != 1 ? -1 : g[0] ? 0 : g[1] ? 1 : g[2] ? 2 : 3;
                    }
                    free(rd);
                    if (modes.copyFlip && (q == 0 || q == 1))
                    {
                        q ^= 1; // the uv matrix expects the rows the other way
                    }
                    testStep("copy 2/2: a 16x16 camera texture drawn from the copy through its uv "
                             "matrix");
                    frameEnsure();
                    bindTarget(t.target, 16, 16);
                    C3D_TexSetFilter(&d.tex, GPU_NEAREST, GPU_NEAREST);
                    C3D_TexSetWrap(&d.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                    C3D_TexBind(0, &d.tex);
                    float uv[8];
                    screenCopyUV(uv);
                    testRectIn(16.0f, 16.0f, -1.0f, uv, 0.0f, 0.0f, 16.0f, 16.0f, 1.0f, 0xFFFFFFFFu,
                               true);
                    testEnd("copy 2/2");
                    C3D_TexBind(0, nil);
                    deviceForgetTexture(&d.tex);
                    uint8 rt[16 * 16 * 4];
                    testRead("the 16x16 drawn from the copy");
                    if (readTexture(&t.tex, rt))
                    {
                        // Bytes R G B of the four quarters' samples, for the log.
                        const int at[4][2] = { { 12, 3 }, { 3, 3 }, { 12, 12 }, { 3, 12 } };
                        int o = 0;
                        for (int i = 0; i < 4; i++)
                        {
                            const uint8* p = px(rt, at[i][0], at[i][1]);
                            o += snprintf(seen + o, sizeof(seen) - (size_t)o, " %02x%02x%02x", p[3],
                                          p[2], p[1]);
                        }
                    }
                }
                static const char* kWhat[] = {
                    "the copy lands as meant",
                    "the copy's rows land the other way up: set TSSM_C3D_COPYFLIP to the other "
                    "value",
                    "the copy's columns land the other way round (not a known case)",
                    "the copy lands turned half way round (not a known case)",
                };
                printf("tssm: c3d: self-test: copy %s (copyflip=%s): %s; drawn from it, its "
                       "quarters are (r g b, the green one first)%s\n",
                       copied && q == 0 ? "ran" : "FAILED", modes.copyFlip ? "on" : "off",
                       !copied ? "the copy could not be made" :
                       q < 0   ? "no single green quarter in the block read back" :
                                 kWhat[q],
                       seen[0] != '\0' ? seen : " not read");
                texDestroy(&d);
                texDestroy(&t);
            }
        } // namespace

        bool32 rttBegin(Camera* cam, Raster* fb)
        {
            C3dRaster* natras = ensure(cam, fb);
            if (natras == nil)
            {
                return 0;
            }
            bindTarget(natras->target, natras->usedW, natras->usedH);
            natras->rendered = 1;
            natras->generation++;
            return 1;
        }

        void rttClear(Camera* cam, Raster* fb, uint32 rgba, bool32 color, bool32 depth)
        {
            C3dRaster* natras = ensure(cam, fb);
            // Only the shadow's target has depth: a depth-only clear of
            // another is nothing to draw.
            if (natras == nil || (!color && !(depth && natras->isShadow)))
            {
                return;
            }
            bindTarget(natras->target, natras->usedW, natras->usedH);
            clearQuad(rgba, color, depth && natras->isShadow);
            natras->rendered = 1;
            natras->generation++;
            noteCount(COUNT_RTT_CLEARS, 1);
        }

        bool32 rttSampleable(const C3dRaster* natras)
        {
            if (natras->screenCopy != COPY_NONE)
            {
                return natras->hasTex && natras->rendered;
            }
            if (!modes.rtt || (natras->isShadow && !modes.shadow))
            {
                return 0;
            }
            if (natras->target == nil || !natras->rendered)
            {
                logOnce("rttempty", "a draw that samples a camera texture nothing has drawn "
                                    "into was left out");
                return 0;
            }
            if (natras->target == boundTarget())
            {
                logOnce("rttloop", "a draw that samples the camera texture it draws into was "
                                   "left out");
                return 0;
            }
            return 1;
        }

        void screenCopyUV(float rows[8])
        {
            // Landscape (X, Y) = (400 u, 240 v) is target pixel (240 - Y,
            // 400 - X) (the screen's turn, c3ddevice.cpp), sampled at
            // (x / 256, y / 512); copyflip takes the target's rows the other
            // way up.
            const float su = 240.0f / 256.0f;
            const float sv = 400.0f / 512.0f;
            rows[0] = 0.0f;
            rows[1] = -su;
            rows[2] = su;
            rows[3] = 0.0f;
            rows[4] = modes.copyFlip ? sv : -sv;
            rows[5] = 0.0f;
            rows[6] = modes.copyFlip ? 0.0f : sv;
            rows[7] = 0.0f;
        }

        int copyScreen(Raster* dst, int own)
        {
            if (!modes.copy)
            {
                logOnce("copyoff", "copy=off: no screen copies (the loading-screen snapshot, the "
                                   "glow and the warp go without)");
                return 0;
            }
            if (dst == nil || dst->platform != PLATFORM_C3D || dst->type != Raster::CAMERATEXTURE)
            {
                logOnce("copynot", "a screen copy into a raster that is not a camera texture");
                return 0;
            }
            return copyScreenInto(GETC3DRASTEREXT(dst), own != 0) ? 1 : 0;
        }

        int screenPassOn(int pass)
        {
            // The glow draws through camera textures: with rtt=off its
            // composite would be left out after its copy was paid for. Both
            // start from a screen copy.
            const bool on =
                pass == 0 ? modes.glow && modes.rtt && modes.copy : modes.warp && modes.copy;
            if (pass != 0 && !on)
            {
                logOnce("warpoff", "%s",
                        modes.warp ? "copy=off: the screen warp is not drawn (it needs "
                                     "screen copies)" :
                                     "warp=off: the screen warp is not drawn");
            }
            else if (!on && !modes.glow)
            {
                logOnce("glowoff", "glow=off: the HDR glow is not drawn");
            }
            else if (!on)
            {
                logOnce("glowrtt", "rtt or copy off: the HDR glow is not drawn (it needs camera "
                                   "textures and screen copies)");
            }
            return on ? 1 : 0;
        }

        Image* rttToImage(Raster* raster)
        {
            C3dRaster* natras = GETC3DRASTEREXT(raster);
            if (natras->target == nil || !natras->rendered)
            {
                logOnce("rttread", "a camera texture nothing has drawn into cannot be read back");
                return nil;
            }
            const int32 w = natras->tex.width;
            const int32 h = natras->tex.height;
            uint8* buf = (uint8*)malloc((size_t)w * h * 4);
            if (buf == nil || !readTexture(&natras->tex, buf))
            {
                free(buf);
                return nil;
            }
            // The camera's picture is the viewport from the target's bottom
            // left; its top row is the target's top (clip +1) unless drawn
            // upside down. Row 0 of the image is the camera's top, as GL3's
            // rasterToImage gives it.
            const int32 uw = natras->usedW;
            const int32 uh = natras->usedH;
            Image* img = Image::create(uw, uh, 32);
            img->allocate();
            for (int32 y = 0; y < uh; y++)
            {
                const int32 row = modes.rttFlip ? h - 1 - y : h - uh + y;
                const uint8* src = buf + (size_t)row * w * 4;
                uint8* dst = img->pixels + (size_t)y * img->stride;
                for (int32 x = 0; x < uw; x++)
                {
                    dst[4 * x + 0] = src[4 * x + 3];
                    dst[4 * x + 1] = src[4 * x + 2];
                    dst[4 * x + 2] = src[4 * x + 1];
                    dst[4 * x + 3] = src[4 * x + 0];
                }
            }
            free(buf);
            return img;
        }

        void rttSelfTest(void)
        {
            if (!selfTestOn)
            {
                printf("tssm: c3d: self-test: off (selftest in [3ds] effects or "
                       "TSSM_C3D_SELFTEST=on runs it)\n");
                fflush(stdout);
                return;
            }
            C3dRaster a, b, c;
            memset(&a, 0, sizeof(a));
            memset(&b, 0, sizeof(b));
            memset(&c, 0, sizeof(c));
            const char* flip = modes.rttFlip ? "on" : "off";

            if (modes.rtt)
            {
                if (!targetCreate(&a, 16, 16, 0) || !targetCreate(&b, 16, 16, 0))
                {
                    printf("tssm: c3d: self-test: rtt: no VRAM for two 16x16 targets\n");
                }
                else
                {
                    testStep("rtt 1/2: a 16x16 camera texture cleared red, its top half (2D "
                             "rows 0-7) drawn green, in mid-frame");
                    frameEnsure();
                    bindTarget(a.target, 16, 16);
                    clearQuad(0xFF0000FFu, 1, 0);
                    testRect(0.0f, 0.0f, 16.0f, 8.0f, 1.0f, 0x00FF00FFu, false);
                    testEnd("rtt 1/2");
                    testStep("rtt 2/2: a second one drawn from the first (v = 0 at its 2D row "
                             "0)");
                    frameEnsure();
                    bindTarget(b.target, 16, 16);
                    C3D_TexSetFilter(&a.tex, GPU_NEAREST, GPU_NEAREST);
                    C3D_TexSetWrap(&a.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                    C3D_TexBind(0, &a.tex);
                    testRect(0.0f, 0.0f, 16.0f, 16.0f, 1.0f, 0xFFFFFFFFu, true);
                    testEnd("rtt 2/2");
                    uint8 ra[16 * 16 * 4];
                    uint8 rb[16 * 16 * 4];
                    testRead("the two 16x16 camera textures");
                    const bool read = readTexture(&a.tex, ra) && readTexture(&b.tex, rb);
                    const int ha = read ? greenHalf(ra) : -1;
                    const int hb = read ? greenHalf(rb) : -1;
                    printf("tssm: c3d: self-test: rtt %s (rttflip=%s): drawing into a texture "
                           "%s; read back, the green half is %s; sampled at v = 0..0.5 it is "
                           "%s, so a camera texture's v = 0 is its camera's %s (%s order)\n",
                           ha >= 0 && hb >= 0 ? "ran" : "FAILED", flip,
                           ha >= 0 ? "works" : "FAILED (no red and green halves)",
                           ha == 0 ? "the target's top rows (clip +1)" :
                           ha == 1 ? "the target's bottom rows (clip -1)" :
                                     "not found",
                           hb < 0   ? "not found" :
                           hb == ha ? "the same picture" :
                                      "upside down",
                           hb < 0 || ha < 0 ? "?" :
                           hb == ha         ? "top" :
                                              "bottom",
                           hb < 0 || ha < 0 ? "?" :
                           hb == ha         ? "Direct3D's and the GameCube's" :
                                              "OpenGL's");
                    C3D_TexBind(0, nil);
                    deviceForgetTexture(&a.tex);
                }
            }
            else
            {
                printf("tssm: c3d: self-test: rtt=off, render to texture not tried\n");
            }

            // The fog's and the copy's parts read their results through a
            // 16x16 target of their own whatever rtt is, so that each part
            // runs with only its own effect on (the console probes).
            if (modes.fog && fogRamp() != nil)
            {
                if (!targetCreate(&c, 16, 16, 0))
                {
                    printf("tssm: c3d: self-test: fog: no VRAM for a 16x16 target\n");
                }
                else
                {
                    testStep("fog: white drawn at w 0.5 and 1.5 under fog from 0 to 2, black "
                             "fog (texture unit 2, combiner stage 4)");
                    frameEnsure();
                    bindTarget(c.target, 16, 16);
                    clearQuad(0x000000FFu, 1, 0);
                    // u_fog as setCameraFog for fogPlane 0, farPlane 2: f = 1 - w/2.
                    const float range = -0.5f;
                    setUniform4(REG_FOG, 63.0f / 64.0f * range,
                                (0.5f - 63.0f * range * 2.0f) / 64.0f, 1.0f, 0.0f);
                    bindFogRamp("the start-up self-test's fog step");
                    C3D_TexEnv* f = C3D_GetTexEnv(4);
                    C3D_TexEnvInit(f);
                    C3D_TexEnvSrc(f, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_TEXTURE2);
                    C3D_TexEnvOpRgb(f, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR,
                                    GPU_TEVOP_RGB_SRC_ALPHA);
                    C3D_TexEnvFunc(f, C3D_RGB, GPU_INTERPOLATE);
                    C3D_TexEnvColor(f, 0xFF000000u);
                    testRect(0.0f, 0.0f, 8.0f, 16.0f, 0.5f, 0xFFFFFFFFu, false);
                    testRect(8.0f, 0.0f, 16.0f, 16.0f, 1.5f, 0xFFFFFFFFu, false);
                    C3D_TexEnvInit(f);
                    testEnd("fog");
                    uint8 rc[16 * 16 * 4];
                    testRead("the fog's 16x16");
                    const bool read = readTexture(&c.tex, rc);
                    const int l = read ? px(rc, 3, 8)[3] : -1;
                    const int r = read ? px(rc, 12, 8)[3] : -1;
                    printf("tssm: c3d: self-test: fog %s: red %d where 191 is wanted (f 0.75), %d "
                           "where 64 is (f 0.25)\n",
                           near(l, 191) && near(r, 64) ? "ran" : "FAILED", l, r);
                }
            }
            else
            {
                printf("tssm: c3d: self-test: fog not tried (fog=%s, ramp %s)\n",
                       modes.fog ? "on" : "off", fogRamp() != nil ? "made" : "missing");
            }
            if (modes.copy)
            {
                testCopy();
            }
            else
            {
                printf("tssm: c3d: self-test: copy not tried (copy=off)\n");
            }
            texDestroy(&a);
            texDestroy(&b);
            texDestroy(&c);
            printf("tssm: c3d: self-test: done\n");
            fflush(stdout);
        }
    } // namespace c3d
} // namespace rw
