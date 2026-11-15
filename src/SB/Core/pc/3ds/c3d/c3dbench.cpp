// rw::c3d: the draw micro-benchmark, TSSM_C3D_BENCH=1.
//
// The planned CPU budget for the device is about 11 microseconds a draw, and
// the one figure ever measured, scaled to the console, is 17-23. This puts a
// number on it on the console itself: once, at the 30th present, it records
// 1000 small textured draws the way a mesh is drawn (a uniform for the
// material, four for the matrix, a texture bind, the program and the vertex
// format, the draw), three times, and times the recording, C3D_FrameEnd(0)
// (which flushes the whole linear heap from the CPU's cache) and
// C3D_FrameEnd(GX_CMDLIST_FLUSH) with only the frame's own memory flushed by
// hand. One line in the log says it. In an emulator the times mean nothing.

#include "c3dimpl.h"

#include <stdio.h>
#include <stdlib.h>

namespace rw
{
    namespace c3d
    {
        namespace
        {
            struct BenchVertex
            {
                float x, y, z;
                uint8 r, g, b, a;
                float u, v;
            };

            double ticksToMs(u64 t)
            {
                return (double)t * 1000.0 / (double)SYSCLOCK_ARM11;
            }

            const int kDraws = 1000;
            const int kTextures = 4;
        } // namespace

        void runBench(void)
        {
            if (!submitIsOpen() || inFrame())
            {
                return;
            }

            BenchVertex* v = (BenchVertex*)linearAlloc(4 * sizeof(BenchVertex));
            uint16* idx = (uint16*)linearAlloc(6 * sizeof(uint16));
            if (v == nil || idx == nil)
            {
                return;
            }
            const float xs[4] = { -0.02f, 0.02f, -0.02f, 0.02f };
            const float ys[4] = { -0.02f, -0.02f, 0.02f, 0.02f };
            for (int i = 0; i < 4; i++)
            {
                v[i].x = xs[i];
                v[i].y = ys[i];
                v[i].z = 0.0f;
                v[i].r = v[i].g = v[i].b = v[i].a = 255;
                v[i].u = xs[i] > 0.0f ? 1.0f : 0.0f;
                v[i].v = ys[i] > 0.0f ? 1.0f : 0.0f;
            }
            const uint16 quad[6] = { 0, 1, 2, 2, 1, 3 };
            memcpy(idx, quad, sizeof(quad));
            GSPGPU_FlushDataCache(v, 4 * sizeof(BenchVertex));
            GSPGPU_FlushDataCache(idx, sizeof(quad));

            // Four 64 x 64 RGB565 textures in VRAM, as most of the game's are.
            C3dRaster* tex = (C3dRaster*)calloc(kTextures, sizeof(C3dRaster));
            uint16* texels = (uint16*)malloc(64 * 64 * 2);
            for (int t = 0; t < kTextures && tex != nil && texels != nil; t++)
            {
                for (int i = 0; i < 64 * 64; i++)
                {
                    texels[i] = (uint16)(i * 37 + t * 4099);
                }
                texCreate(&tex[t], 64, 64, 0, GPU_RGB565, texels, 64 * 64 * 2);
            }
            free(texels);

            double recordMs[3] = { 0, 0, 0 };
            double endMs[3] = { 0, 0, 0 };
            float gpuMs[3] = { 0, 0, 0 };
            const uint32 format = MODEL_COLOR | MODEL_UV;

            for (int rep = 0; rep < 3; rep++)
            {
                frameEnsure();
                if (rep > 0)
                {
                    // C3D_FrameBegin waited for the frame before: its GPU time.
                    gpuMs[rep - 1] = C3D_GetDrawingTime();
                }
                setScreenViewport(0, 0, SCREEN_W, SCREEN_H);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                C3D_AlphaTest(false, GPU_ALWAYS, 0);
                C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
                C3D_CullFace(GPU_CULL_NONE);
                deviceTevPlain();
                C3D_TexEnv* env = C3D_GetTexEnv(0);
                C3D_TexEnvInit(env);
                C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
                setUniform4(REG_UVMAT, 1.0f, 0.0f, 0.0f, 0.0f);
                setUniform4(REG_UVMAT + 1, 0.0f, 1.0f, 0.0f, 0.0f);

                const u64 t0 = svcGetSystemTick();
                for (int i = 0; i < kDraws; i++)
                {
                    const float fx = (float)(i % 40) / 20.0f - 1.0f;
                    const float fy = (float)(i / 40) / 12.5f - 1.0f;
                    // Clip space straight from the quad: x', y' (tilted), z, w.
                    const float mvp[16] = { 0.0f, 1.0f, 0.0f, fy,    -1.0f, 0.0f, 0.0f, -fx,
                                            0.0f, 0.0f, 0.0f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f };
                    setUniformRows(REG_MVP, mvp, 4);
                    setUniform4(REG_MATCOLOR, (float)(i & 255) / 255.0f, 0.5f, 1.0f, 1.0f);
                    if (tex != nil && tex[i % kTextures].hasTex)
                    {
                        C3D_TexBind(0, &tex[i % kTextures].tex);
                    }
                    useProgram(PROG_WORLD);
                    useFormat((int32)format, v, sizeof(BenchVertex));
                    drawElements(GPU_TRIANGLES, 6, idx);
                }
                const u64 t1 = svcGetSystemTick();
                recordMs[rep] = ticksToMs(t1 - t0);

                const u64 t2 = svcGetSystemTick();
                if (rep < 2)
                {
                    frameEndWith(0);
                }
                else
                {
                    // What the frame wrote: the ring (nothing here) and the
                    // command buffer, which citro3d flushes itself with this
                    // flag.
                    ringFlush();
                    frameEndWith(GX_CMDLIST_FLUSH);
                }
                endMs[rep] = ticksToMs(svcGetSystemTick() - t2);
            }
            frameEnsure();
            gpuMs[2] = C3D_GetDrawingTime();
            frameEnd();

            printf("tssm: c3d: bench: %d draws recorded in %.2f / %.2f / %.2f ms (%.1f us a draw "
                   "at best); C3D_FrameEnd(0) %.2f / %.2f ms, C3D_FrameEnd(GX_CMDLIST_FLUSH) with "
                   "the ring flushed by hand %.2f ms; GPU %.2f / %.2f / %.2f ms\n",
                   kDraws, recordMs[0], recordMs[1], recordMs[2],
                   1000.0 * (recordMs[1] < recordMs[2] ? recordMs[1] : recordMs[2]) / kDraws,
                   endMs[0], endMs[1], endMs[2], gpuMs[0], gpuMs[1], gpuMs[2]);
            fflush(stdout);

            // citro3d still points at the last texture bound here, inside
            // `tex`, which is about to be freed (c3ddevice.cpp,
            // deviceForgetTexture, says why that matters).
            C3D_TexBind(0, nil);
            for (int t = 0; t < kTextures && tex != nil; t++)
            {
                texDestroy(&tex[t]);
            }
            free(tex);
            deferFree(v);
            deferFree(idx);
            deviceStateDirty();
        }

        // TSSM_C3D_BENCH=2 (probe 14): the same draw, with one cost added at a
        // time, so each part of a draw's citro3d price is known before the
        // ladder's own emitter (L6) or buffer offsets are written. 1000 draws,
        // three times each, the best time in microseconds a draw.
        void runBench2(void)
        {
            if (!submitIsOpen() || inFrame())
            {
                return;
            }
            const int kBuffers = 16; // vertex pointers taken in turn
            BenchVertex* v = (BenchVertex*)linearAlloc(kBuffers * 4 * sizeof(BenchVertex));
            uint16* idx = (uint16*)linearAlloc(6 * sizeof(uint16));
            C3dRaster* tex = (C3dRaster*)calloc(kTextures, sizeof(C3dRaster));
            uint16* texels = (uint16*)malloc(64 * 64 * 2);
            if (v == nil || idx == nil || tex == nil || texels == nil)
            {
                if (v != nil)
                    linearFree(v);
                if (idx != nil)
                    linearFree(idx);
                free(tex);
                free(texels);
                return;
            }
            const float xs[4] = { -0.02f, 0.02f, -0.02f, 0.02f };
            const float ys[4] = { -0.02f, -0.02f, 0.02f, 0.02f };
            for (int b = 0; b < kBuffers; b++)
            {
                for (int i = 0; i < 4; i++)
                {
                    BenchVertex* q = &v[b * 4 + i];
                    q->x = xs[i];
                    q->y = ys[i];
                    q->z = 0.0f;
                    q->r = q->g = q->b = q->a = 255;
                    q->u = xs[i] > 0.0f ? 1.0f : 0.0f;
                    q->v = ys[i] > 0.0f ? 1.0f : 0.0f;
                }
            }
            const uint16 quad[6] = { 0, 1, 2, 2, 1, 3 };
            memcpy(idx, quad, sizeof(quad));
            GSPGPU_FlushDataCache(v, kBuffers * 4 * sizeof(BenchVertex));
            GSPGPU_FlushDataCache(idx, sizeof(quad));
            for (int t = 0; t < kTextures; t++)
            {
                for (int i = 0; i < 64 * 64; i++)
                {
                    texels[i] = (uint16)(i * 37 + t * 4099);
                }
                texCreate(&tex[t], 64, 64, 0, GPU_RGB565, texels, 64 * 64 * 2);
            }
            free(texels);

            enum
            {
                B_CURRENT, // probe 10's: 5 uniforms and a texture a draw
                B_ALONE, // the draw only
                B_MVP, // + the 4 matrix registers
                B_TEX, // + a texture change
                B_BUF, // + a new vertex pointer (BufInfo), as each atomic has
                B_PROG, // + a program switch
                NUM_B
            };
            static const char* kName[NUM_B] = { "as the game (5 uniforms + a texture)",
                                                "the draw alone", "+ the 4 matrix registers",
                                                "+ a texture change", "+ a new vertex pointer",
                                                "+ a program switch" };
            const uint32 format = MODEL_COLOR | MODEL_UV;
            double best[NUM_B];
            for (int c = 0; c < NUM_B; c++)
            {
                best[c] = 1e30;
                for (int rep = 0; rep < 3; rep++)
                {
                    frameEnsure();
                    setScreenViewport(0, 0, SCREEN_W, SCREEN_H);
                    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE,
                                   GPU_ZERO);
                    C3D_AlphaTest(false, GPU_ALWAYS, 0);
                    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
                    C3D_CullFace(GPU_CULL_NONE);
                    deviceTevPlain();
                    C3D_TexEnv* env = C3D_GetTexEnv(0);
                    C3D_TexEnvInit(env);
                    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR,
                                  GPU_PRIMARY_COLOR);
                    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
                    setUniform4(REG_UVMAT, 1.0f, 0.0f, 0.0f, 0.0f);
                    setUniform4(REG_UVMAT + 1, 0.0f, 1.0f, 0.0f, 0.0f);
                    // One matrix and texture set before, for the cases that
                    // leave them as they are.
                    const float mvp0[16] = { 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f };
                    setUniformRows(REG_MVP, mvp0, 4);
                    C3D_TexBind(0, &tex[0].tex);
                    useProgram(PROG_WORLD);
                    useFormat((int32)format, v, sizeof(BenchVertex));

                    const u64 t0 = svcGetSystemTick();
                    for (int i = 0; i < kDraws; i++)
                    {
                        const float fx = (float)(i % 40) / 20.0f - 1.0f;
                        const float fy = (float)(i / 40) / 12.5f - 1.0f;
                        if (c == B_CURRENT || c >= B_MVP)
                        {
                            const float mvp[16] = { 0.0f,  1.0f, 0.0f, fy,    -1.0f, 0.0f,
                                                    0.0f,  -fx,  0.0f, 0.0f,  0.0f,  -0.5f,
                                                    0.0f,  0.0f, 0.0f, 1.0f };
                            setUniformRows(REG_MVP, mvp, 4);
                        }
                        if (c == B_CURRENT)
                        {
                            setUniform4(REG_MATCOLOR, (float)(i & 255) / 255.0f, 0.5f, 1.0f,
                                        1.0f);
                        }
                        if (c == B_CURRENT || c >= B_TEX)
                        {
                            C3D_TexBind(0, &tex[i % kTextures].tex);
                        }
                        if (c == B_PROG)
                        {
                            useProgram((i & 1) != 0 ? PROG_LIT : PROG_WORLD);
                        }
                        else
                        {
                            useProgram(PROG_WORLD);
                        }
                        useFormat((int32)format,
                                  c >= B_BUF ? (const void*)&v[(i % kBuffers) * 4] : (const void*)v,
                                  sizeof(BenchVertex));
                        drawElements(GPU_TRIANGLES, 6, idx);
                    }
                    const double t = ticksToMs(svcGetSystemTick() - t0);
                    best[c] = t < best[c] ? t : best[c];
                    frameEndWith(0);
                }
            }
            frameEnsure();
            frameEnd();
            char line[700];
            uint32 used = (uint32)snprintf(line, sizeof(line),
                                           "bench 2: %d draws, best of 3, us a draw:", kDraws);
            for (int c = 0; c < NUM_B && used < sizeof(line); c++)
            {
                used += (uint32)snprintf(line + used, sizeof(line) - used, "%s %s %.2f",
                                         c > 0 ? ";" : "", kName[c],
                                         1000.0 * best[c] / kDraws);
            }
            printf("tssm: c3d: %s\n", line);
            fflush(stdout);
            abLog("%s", line);

            C3D_TexBind(0, nil);
            for (int t = 0; t < kTextures; t++)
            {
                texDestroy(&tex[t]);
            }
            free(tex);
            deferFree(v);
            deferFree(idx);
            deviceStateDirty();
        }
    } // namespace c3d
} // namespace rw
