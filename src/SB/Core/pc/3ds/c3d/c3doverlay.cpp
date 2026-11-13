// rw::c3d: the bottom screen, for the settings overlay (rwc3d.h; docs/3DS.md,
// "The settings overlay").
//
// Everything here is the device's own machinery: vs_im2d (which writes every
// part of every output it declares), the Im2D vertex format, the frame ring,
// one combiner stage on texture unit 0. Units 1 and 2 are never touched: unit 2
// keeps the fog ramp once it is bound, and unit 1 cannot be unbound.
//
// The bottom LCD is mounted sideways like the top one: its target is 240 x
// 320, and landscape pixels are turned into it by the same arithmetic as the
// top screen's 2D (c3ddevice.cpp's u_scrMat, setScreenViewport).

#include "c3dimpl.h"

#include <stdio.h>
#include <stdlib.h>

namespace rw
{
    namespace c3d
    {
        namespace
        {
            const int32 kBottomW = 320;
            const int32 kBottomH = 240;
            const int32 kMaxTex = 4;

            struct OvTex
            {
                C3D_Tex tex;
                int32 w, h; // the picture
                int32 pw, ph; // the texture (powers of two)
                bool used;
            };

            // Handle i is sTex[i - 1].
            OvTex sTex[kMaxTex];
            bool sOpen;
            OverlayStats sStats;
            Im2DVertex* sClear; // overlayBegin's four for the fill, then the batch's
            const uint16* sIndex; // the batch's indices

            int32 pow2(int32 v)
            {
                int32 p = 8;
                while (p < v && p < 1024)
                {
                    p <<= 1;
                }
                return p;
            }

            // c3draster.cpp's tiledIndex: rows top first, stored bottom up.
            inline uint32 tiled(uint32 x, uint32 y, uint32 w, uint32 h)
            {
                const uint32 fy = h - 1 - y;
                const uint32 tile = (fy >> 3) * (w >> 3) + (x >> 3);
                const uint32 tx = x & 7;
                const uint32 ty = fy & 7;
                return tile * 64 + ((tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) | ((ty & 2) << 2) |
                                    ((tx & 4) << 2) | ((ty & 4) << 3));
            }

            OvTex* texFor(int handle)
            {
                if (handle < 1 || handle > kMaxTex || !sTex[handle - 1].used)
                {
                    return nil;
                }
                return &sTex[handle - 1];
            }

            void vertex(Im2DVertex* v, float x, float y, uint32 rgba)
            {
                v->x = x;
                v->y = y;
                v->z = 0.5f;
                v->w = 1.0f;
                v->r = (uint8)(rgba >> 24);
                v->g = (uint8)(rgba >> 16);
                v->b = (uint8)(rgba >> 8);
                v->a = (uint8)rgba;
                v->u = 0.0f;
                v->v = 0.0f;
            }
        } // namespace

        int overlayOpen(int depth)
        {
            if (sOpen)
            {
                return 1;
            }
            if (!submitIsOpen())
            {
                printf("tssm: c3d: overlay: the device is not open; overlay off\n");
                fflush(stdout);
                return 0;
            }
            const uint32 vramBefore = (uint32)vramSpaceFree();
            if (!bottomCreate(depth))
            {
                printf("tssm: c3d: overlay: no VRAM for the bottom screen's target (%u KB free); "
                       "overlay off\n",
                       (unsigned)(vramBefore >> 10));
                fflush(stdout);
                return 0;
            }
            sOpen = true;
            printf("tssm: c3d: overlay: the bottom screen's target, 240x320 RGBA8%s (%u KB of "
                   "VRAM); VRAM free %u KB, linear free %u KB; textures A8 in linear memory\n",
                   depth ? " with D16 (TSSM_3DS_GUI_DEPTH=1)" : ", no depth",
                   (unsigned)((vramBefore - (uint32)vramSpaceFree()) >> 10),
                   (unsigned)(vramSpaceFree() >> 10), (unsigned)(linearSpaceFree() >> 10));
            fflush(stdout);
            return 1;
        }

        int overlayIsOpen(void)
        {
            return sOpen && submitIsOpen() && bottomExists();
        }

        int overlayTexCreate(int w, int h)
        {
            if (!overlayIsOpen() || w <= 0 || h <= 0 || w > 1024 || h > 1024)
            {
                return 0;
            }
            for (int32 i = 0; i < kMaxTex; i++)
            {
                OvTex* t = &sTex[i];
                if (t->used)
                {
                    continue;
                }
                C3D_TexInitParams p;
                memset(&p, 0, sizeof(p));
                p.width = (u16)pow2(w);
                p.height = (u16)pow2(h);
                p.format = GPU_A8;
                p.type = GPU_TEX_2D;
                p.onVram = false;
                if (!C3D_TexInitWithParams(&t->tex, nil, p))
                {
                    logOnce("ovtex", "overlay: no linear memory for a %dx%d texture", w, h);
                    return 0;
                }
                memset(t->tex.data, 0, t->tex.size);
                GSPGPU_FlushDataCache(t->tex.data, t->tex.size);
                C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
                C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                t->w = w;
                t->h = h;
                t->pw = p.width;
                t->ph = p.height;
                t->used = true;
                printf("tssm: c3d: overlay: texture %d, %dx%d A8 (%d KB of linear memory)\n",
                       (int)(i + 1), (int)t->pw, (int)t->ph, (int)(t->tex.size >> 10));
                fflush(stdout);
                return i + 1;
            }
            return 0;
        }

        void overlayTexWrite(int handle, int x, int y, int w, int h, const unsigned char* alpha,
                             int pitch)
        {
            OvTex* t = texFor(handle);
            if (t == nil || alpha == nil || x < 0 || y < 0 || x + w > t->w || y + h > t->h)
            {
                return;
            }
            // Inside the frame: the frame before, which may have sampled it,
            // was waited for when this one opened.
            frameEnsure();
            uint8* data = (uint8*)t->tex.data;
            for (int32 yy = 0; yy < h; yy++)
            {
                const unsigned char* row = alpha + (size_t)yy * pitch;
                for (int32 xx = 0; xx < w; xx++)
                {
                    data[tiled((uint32)(x + xx), (uint32)(y + yy), (uint32)t->pw, (uint32)t->ph)] =
                        row[xx];
                }
            }
            GSPGPU_FlushDataCache(t->tex.data, t->tex.size);
        }

        void overlayTexDestroy(int handle)
        {
            OvTex* t = texFor(handle);
            if (t == nil)
            {
                return;
            }
            deviceForgetTexture(&t->tex);
            deferTexDelete(&t->tex);
            t->used = false;
        }

        void overlayClose(void)
        {
            for (int32 i = 0; i < kMaxTex; i++)
            {
                if (sTex[i].used)
                {
                    C3D_TexDelete(&sTex[i].tex);
                    sTex[i].used = false;
                }
            }
            sOpen = false;
        }

        Im2DVertex* overlayBegin(unsigned vertices, unsigned indices, int cmds,
                                 unsigned short** indexOut)
        {
            sClear = nil;
            if (!overlayIsOpen() || indexOut == nil)
            {
                return nil;
            }
            frameEnsure();

            // The command buffer first: drawGuard splits at 90% full, so this
            // leaves that and the batch's own (a draw is under 512 bytes).
            u32* buf = nil;
            u32 size = 0;
            u32 offset = 0;
            GPUCMD_GetBuffer(&buf, &size, &offset);
            const u32 need = (u32)(cmds + 4) * 128 + 512;
            if (buf != nil && size > 0 && offset + need > size - size / 10)
            {
                hardSplit("overlay", false);
            }

            // Then the ring, in one piece: a ring split happens here or not at
            // all. Four vertices for the fill come first.
            const uint32 vbytes = ((vertices + 4) * (uint32)sizeof(Im2DVertex) + 15) & ~15u;
            const uint32 ibytes = indices * 2;
            uint8* p = (uint8*)ringAlloc(vbytes + ibytes);
            if (p == nil)
            {
                return nil;
            }
            sStats.ringBytes += vbytes + ibytes;
            sClear = (Im2DVertex*)p;
            sIndex = (const uint16*)(p + vbytes);
            *indexOut = (unsigned short*)sIndex;
            return sClear + 4;
        }

        void overlayDraw(const OverlayCmd* cmds, int count, unsigned clear)
        {
            if (sClear == nil || !overlayIsOpen())
            {
                return;
            }
            RecordScope scope;
            bindBottom();

            useProgram(PROG_IM2D);
            // Pixels to clip: x down the screen's 240, y along its 320.
            setUniform4(REG_SCRMAT, 0.0f, -2.0f / (float)kBottomH, 1.0f, 0.0f);
            setUniform4(REG_SCRMAT + 1, -2.0f / (float)kBottomW, 0.0f, 1.0f, 0.0f);
            setUniform4(REG_UVMAT, 1.0f, 0.0f, 0.0f, 0.0f);
            setUniform4(REG_UVMAT + 1, 0.0f, 1.0f, 0.0f, 0.0f);
            setUniform4(REG_FOG, 0.0f, 0.0f, 0.0f, 0.0f); // the fog stage is off below

            deviceTevPlain();
            C3D_AlphaTest(false, GPU_ALWAYS, 0);
            C3D_CullFace(GPU_CULL_NONE); // ImGui's winding is not fixed, and the turn flips it
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);

            // The fill: a quad, not a GX fill (no queue entry in the frame).
            vertex(&sClear[0], 0.0f, 0.0f, clear);
            vertex(&sClear[1], 0.0f, (float)kBottomH, clear);
            vertex(&sClear[2], (float)kBottomW, 0.0f, clear);
            vertex(&sClear[3], (float)kBottomW, (float)kBottomH, clear);
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
            useFormat(FORMAT_IM2D, sClear, sizeof(Im2DVertex));
            drawArrays(GPU_TRIANGLE_STRIP, 0, 4);

            // ImGui's: the vertex colour, its alpha times the texture's.
            env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
            C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                           GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);

            const Im2DVertex* verts = sClear + 4;
            int32 boundTex = -1;
            for (int32 i = 0; i < count; i++)
            {
                const OverlayCmd& c = cmds[i];
                OvTex* t = texFor(c.tex);
                if (t == nil || c.count == 0)
                {
                    continue;
                }
                float x0 = c.clip[0] < 0.0f ? 0.0f : c.clip[0];
                float y0 = c.clip[1] < 0.0f ? 0.0f : c.clip[1];
                float x1 = c.clip[2] > (float)kBottomW ? (float)kBottomW : c.clip[2];
                float y1 = c.clip[3] > (float)kBottomH ? (float)kBottomH : c.clip[3];
                const int32 ix0 = (int32)(x0 + 0.5f);
                const int32 iy0 = (int32)(y0 + 0.5f);
                const int32 ix1 = (int32)(x1 + 0.5f);
                const int32 iy1 = (int32)(y1 + 0.5f);
                if (ix1 <= ix0 || iy1 <= iy0)
                {
                    continue;
                }
                // Landscape to the target's portrait pixels, as setScreenViewport.
                C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)(kBottomH - iy1), (u32)(kBottomW - ix1),
                               (u32)(kBottomH - iy0), (u32)(kBottomW - ix0));
                if (c.tex != boundTex)
                {
                    C3D_TexBind(0, &t->tex);
                    setUniform4(REG_UVMAT, (float)t->w / (float)t->pw, 0.0f, 0.0f, 0.0f);
                    setUniform4(REG_UVMAT + 1, 0.0f, (float)t->h / (float)t->ph, 0.0f, 0.0f);
                    boundTex = c.tex;
                }
                useFormat(FORMAT_IM2D, verts + c.firstVertex, sizeof(Im2DVertex));
                drawElements(GPU_TRIANGLES, (int32)c.count, sIndex + c.firstIndex);
                sStats.draws++;
                sStats.indices += c.count;
            }
            C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
            sStats.frames++;
            sClear = nil;

            // The device sets its own state again from the next draw, and
            // that draw goes to the top screen.
            deviceStateDirty();
            bindScreen();
        }

        int readBottom(unsigned char* rgba, int width, int height)
        {
            if (width != kBottomW || height != kBottomH || !overlayIsOpen())
            {
                return 0;
            }
            return readBottomTarget(rgba);
        }

        void overlayTakeStats(OverlayStats* out)
        {
            sStats.textures = 0;
            sStats.texBytes = 0;
            for (int32 i = 0; i < kMaxTex; i++)
            {
                if (sTex[i].used)
                {
                    sStats.textures++;
                    sStats.texBytes += sTex[i].tex.size;
                }
            }
            *out = sStats;
            memset(&sStats, 0, sizeof(sStats));
        }
    } // namespace c3d
} // namespace rw
