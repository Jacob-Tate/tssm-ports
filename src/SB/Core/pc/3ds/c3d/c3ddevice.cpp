// rw::c3d: librw's Device: the camera, the render states, the
// immediate modes, and the state the object pipelines set for each draw.
//
// The render states follow the GL3 device's rules, cut-out rule included, in
// the PC port's form: a keyed texture on a
// depth-writing draw that is not blended is cut at the middle of its edge,
// and so is keyed art the application blends when the object pipeline says
// the geometry is opaque, for that one draw.

#include "c3dimpl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// iCapture.h's S32 iCaptureFrameCount() (frames presented so far), declared
// here because that header brings in types.h, which is kept out of the
// RenderWare side; S32 is the 3DS's int (types.h spells it out).
int iCaptureFrameCount();

namespace rw
{
    namespace c3d
    {
        namespace
        {
            // What librw has been asked for.
            struct RwState
            {
                Raster* raster;
                uint32 filter;
                uint32 addrU;
                uint32 addrV;
                bool32 appVertexAlpha;
                bool32 vertexAlpha;
                uint32 srcblend;
                uint32 destblend;
                bool32 ztest;
                bool32 zwrite;
                bool32 fogEnable;
                uint32 fogColor;
                uint32 cullmode;
                uint32 stencilenable, stencilfail, stencilzfail, stencilpass;
                uint32 stencilfunc, stencilref, stencilmask, stencilwritemask;
                uint32 alphafunc;
                uint32 alpharef;
                uint32 gsalpha;
                uint32 gsalpharef;
                uint32 colorwritemask;

                bool32 textureAlpha;
                bool32 textureKeyed;
                bool32 im2DActive;
                bool32 pipelineOpaque;
                bool32 pipelineFresh;

                // The environment map, for the next draw only.
                Texture* envTex;
                uint32 envColor; // TEV constant, 0xAABBGGRR
                bool32 envByAlpha;
            };

            // What citro3d was last given.
            struct Applied
            {
                bool valid;
                bool blend;
                uint32 src, dst;
                bool test;
                uint32 func;
                uint32 ref;
                bool depthEnable;
                uint32 depthFunc;
                uint32 writeMask;
                uint32 cull;
                C3D_Tex* tex[2]; // units 0 and 1
                uint32 texGen[2];
                int tev; // TEV_*
                uint32 envColor;
                bool envByAlpha;
                int fog; // stage 4: 1 fog, 0 passes the colour on, -1 not known
                uint32 fogColor;
                bool orthoDepth; // the depth map is a parallel camera's (beginUpdate)
            };

            // The combiner presets.
            enum
            {
                TEV_COLOR, // the vertex colour
                TEV_TEXTURED, // texture 0 x vertex colour
                TEV_ENV_COLOR, // TEV_ENV with the vertex colour for the base
                TEV_ENV // MatFX's environment map over TEV_TEXTURED
            };

            RwState rs;
            Applied ap;

            bool sDiscard; // this camera's draws are left out (modes)
            Mat4 sViewProj;
            // A parallel camera: its clip z runs from 0 (near) to -1 (far),
            // and flushState's depth map turns that into the depth every other
            // draw gets (beginUpdate).
            bool sOrthoDepth;
            // Drawing into a camera texture (sIm2DW x sIm2DH, untilted), and
            // whether it is drawn upside down (rttFlip: the winding turns too).
            bool sTexTarget;
            bool sTexFlip;
            // The current camera's fog, as u_fog's x and y (setCameraFog).
            float sFogScale;
            float sFogBias = 1.0f;
            float sFogPlane; // the camera's, for the log
            float sFarPlane;
            bool sScreenCamera;
            V3d sCamPos; // the screen camera's, the origin of its world Im3D
            float sIm2DW = SCREEN_W;
            float sIm2DH = SCREEN_H;
            int32 sDraws;
            // The current camera, for the HUD's depth window and
            // TSSM_C3D_HUDLOG.
            bool sParallel;
            float sCamNear, sCamFar, sCamVwX, sCamVwY;

            // A parallel camera's depth window (hudz, beginUpdate): the
            // HUD's models are drawn with their camera z from the near plane
            // to kHudDepth past it spread over the whole depth range, not
            // the camera's 0.3 to 10000. A model there is about 0.01 deep
            // (render_model flattens it to a hundredth), so over 10000 its
            // depth changes by 1e-6 near 1, where a float24 (16 bits of
            // mantissa) steps by 7.6e-6: every face gets the same depth and
            // the faces draw in their order, the inside over the front.
            // Over kHudDepth the same model spans about 60 steps.
            const float kHudDepth = 16.0f;
            bool sHasWindow;
            float sWindowRow[4]; // the window's clip z row, view included
            float sViewZ[4]; // the view's camera z row
            // setWorldMatrix's next matrix (an atomic's, atomicDepth): 0 the
            // camera's, 1 the window's, 2 flattened (TSSM_C3D_HUDSIM).
            int sNextDepth;
            int sLastDepth; // the last setWorldMatrix's, for the log
            float sFlatZ; // 2: every vertex's clip z
            int sHudSim = -1; // TSSM_C3D_HUDSIM=1

            // TSSM_C3D_HUDLOG=<frame>[,<frame>...]: in those frames, every 3D
            // draw under a parallel camera (the HUD's and the menus' models):
            // what is drawn and where its depth lands, then the state and
            // the program it is drawn with.
            int sHudFrames[8];
            int sHudNum = -1;
            int sHudLines;

            bool hudLogOn(void)
            {
                if (sHudNum < 0)
                {
                    sHudNum = 0;
                    const char* e = getenv("TSSM_C3D_HUDLOG");
                    while (e != nil && *e != '\0' && sHudNum < 8)
                    {
                        char* end;
                        long v = strtol(e, &end, 10);
                        if (end == e)
                        {
                            break;
                        }
                        sHudFrames[sHudNum++] = (int)v;
                        e = *end == ',' ? end + 1 : end;
                    }
                }
                if (sHudNum == 0 || !sParallel || sHudLines >= 400)
                {
                    return false;
                }
                const int f = iCaptureFrameCount();
                for (int i = 0; i < sHudNum; i++)
                {
                    if (sHudFrames[i] == f)
                    {
                        return true;
                    }
                }
                return false;
            }

            // The step of a float with 16 bits of mantissa (the PICA's
            // float24) at v.
            float f24Step(float v)
            {
                v = fabsf(v);
                if (v < 1e-30f)
                {
                    return 1e-30f;
                }
                return ldexpf(1.0f, ilogbf(v) - 16);
            }

            // im3d
            void* sIm3DVerts;
            int32 sIm3DCount;

            // A frame's stats line, once a second, with TSSM_C3D_STATS=1.
            int sStatsEnv = -1;
            u64 sStatsAt;
            u64 sStatsPeriod = 1000;
            // The camera texture drawn into is the shadow's (gpusplit).
            bool sShadowTarget;

            const Mat4 kTilt = { { { 0.0f, 1.0f, 0.0f, 0.0f },
                                   { -1.0f, 0.0f, 0.0f, 0.0f },
                                   { 0.0f, 0.0f, 1.0f, 0.0f },
                                   { 0.0f, 0.0f, 0.0f, 1.0f } } };

            GPU_BLENDFACTOR blendFactor(uint32 f)
            {
                switch (f)
                {
                case BLENDZERO:
                    return GPU_ZERO;
                case BLENDONE:
                    return GPU_ONE;
                case BLENDSRCCOLOR:
                    return GPU_SRC_COLOR;
                case BLENDINVSRCCOLOR:
                    return GPU_ONE_MINUS_SRC_COLOR;
                case BLENDSRCALPHA:
                    return GPU_SRC_ALPHA;
                case BLENDINVSRCALPHA:
                    return GPU_ONE_MINUS_SRC_ALPHA;
                case BLENDDESTALPHA:
                    return GPU_DST_ALPHA;
                case BLENDINVDESTALPHA:
                    return GPU_ONE_MINUS_DST_ALPHA;
                case BLENDDESTCOLOR:
                    return GPU_DST_COLOR;
                case BLENDINVDESTCOLOR:
                    return GPU_ONE_MINUS_DST_COLOR;
                case BLENDSRCALPHASAT:
                    return GPU_SRC_ALPHA_SATURATE;
                default:
                    return GPU_ONE;
                }
            }

            // A blend factor that reads the target's alpha.
            bool readsDstAlpha(uint32 f)
            {
                return f == BLENDDESTALPHA || f == BLENDINVDESTALPHA || f == BLENDSRCALPHASAT;
            }

            GPU_TEXTURE_WRAP_PARAM wrapFor(uint32 a)
            {
                switch (a)
                {
                case Texture::MIRROR:
                    return GPU_MIRRORED_REPEAT;
                case Texture::CLAMP:
                    return GPU_CLAMP_TO_EDGE;
                case Texture::BORDER:
                    return GPU_CLAMP_TO_BORDER;
                default:
                    return GPU_REPEAT;
                }
            }

            void defaultStates(void)
            {
                memset(&rs, 0, sizeof(rs));
                rs.filter = Texture::LINEAR;
                rs.addrU = Texture::WRAP;
                rs.addrV = Texture::WRAP;
                rs.srcblend = BLENDSRCALPHA;
                rs.destblend = BLENDINVSRCALPHA;
                rs.ztest = 1;
                rs.zwrite = 1;
                rs.cullmode = CULLNONE;
                rs.alphafunc = ALPHAGREATEREQUAL;
                rs.alpharef = 10;
                rs.colorwritemask = COLORWRITEALL;
                ap.valid = false;
            }

            void setRaster(Raster* raster)
            {
                rs.raster = raster;
                bool32 alpha = 0;
                bool32 keyed = 0;
                if (raster != nil && raster->platform == PLATFORM_C3D)
                {
                    C3dRaster* natras = GETC3DRASTEREXT(raster);
                    alpha = natras->alphaKind != ALPHAOPAQUE;
                    keyed = natras->alphaKind == ALPHAKEYED;
                }
                rs.textureAlpha = alpha;
                rs.textureKeyed = keyed;
            }

            // TRIANGLES, STRIP, FAN; FALSE for lines and points, which the
            // game does not draw and the PICA has not.
            bool primFor(PrimitiveType t, uint32* prim)
            {
                switch (t)
                {
                case PRIMTYPETRILIST:
                    *prim = GPU_TRIANGLES;
                    return true;
                case PRIMTYPETRISTRIP:
                    *prim = GPU_TRIANGLE_STRIP;
                    return true;
                case PRIMTYPETRIFAN:
                    *prim = GPU_TRIANGLE_FAN;
                    return true;
                default:
                    logOnce("lines", "a line or point list was not drawn (the PICA has neither)");
                    return false;
                }
            }

            // alphafill: xFXHighDynamicRangeStartRender's quad, one alpha over the
            // whole raster with alpha writes only: a strip of four at its
            // corners, untextured, nothing blended or tested, no depth.
            // Its alpha, or -1.
            int32 alphaResetOf(uint32 prim, const Im2DVertex* v, int32 n)
            {
                const bool vertexAlpha =
                    !rs.pipelineFresh && rs.pipelineOpaque ? rs.appVertexAlpha : rs.vertexAlpha;
                if (prim != GPU_TRIANGLE_STRIP || n != 4 || rs.raster != nil ||
                    rs.colorwritemask != COLORWRITEALPHA || rs.ztest || rs.zwrite || vertexAlpha ||
                    rs.textureAlpha || sTexTarget || screenPassRunning() >= 0)
                {
                    return -1;
                }
                float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
                for (int32 i = 0; i < 4; i++)
                {
                    if (v[i].a != v[0].a || !(v[i].w > 0.0f))
                    {
                        return -1;
                    }
                    x0 = v[i].x < x0 ? v[i].x : x0;
                    x1 = v[i].x > x1 ? v[i].x : x1;
                    y0 = v[i].y < y0 ? v[i].y : y0;
                    y1 = v[i].y > y1 ? v[i].y : y1;
                }
                if (x0 > 0.0f || y0 > 0.0f || x1 < sIm2DW || y1 < sIm2DH)
                {
                    return -1;
                }
                // Each corner once, the strip's two triangles across both
                // diagonals (v0-v3 and v1-v2).
                for (int32 i = 0; i < 4; i++)
                {
                    if ((v[i].x != x0 && v[i].x != x1) || (v[i].y != y0 && v[i].y != y1))
                    {
                        return -1;
                    }
                }
                if (v[0].x == v[3].x || v[0].y == v[3].y || v[1].x == v[2].x || v[1].y == v[2].y)
                {
                    return -1;
                }
                return (int32)v[0].a;
            }

            void setIm2DMatrix(void)
            {
                if (sTexTarget)
                {
                    // A camera texture: x' = 2x/w - 1, y' = 1 - 2y/h (upside
                    // down with rttFlip).
                    const float k = sTexFlip ? -1.0f : 1.0f;
                    setUniform4(REG_SCRMAT, 2.0f / sIm2DW, 0.0f, -1.0f, 0.0f);
                    setUniform4(REG_SCRMAT + 1, 0.0f, -2.0f * k / sIm2DH, k, 0.0f);
                    return;
                }
                // Pixels of the camera's raster to clip x, y, turned for the
                // sideways screen: x' = 1 - 2y/h (up), y' = 1 - 2x/w.
                setUniform4(REG_SCRMAT, 0.0f, -2.0f / sIm2DH, 1.0f, 0.0f);
                setUniform4(REG_SCRMAT + 1, -2.0f / sIm2DW, 0.0f, 1.0f, 0.0f);
            }

            // A 2D vertex's w is the camera z the game gave it, and the HUD's
            // and the menus' (zUIImage and the others, at the near screen z
            // of 0) is 1e-5 (rwplcore.h, RwIm2DCameraZFromRecip). vs_im2d
            // hands the PICA its clip position times w, and a clip w that
            // small is at the clipper's threshold: Azahar's software clipper
            // takes w > 1e-5, and the console's is not known. Every w of a
            // draw is divided by the largest, which leaves the picture as it
            // was: x, y and depth are divided by w again, and perspective-
            // correct interpolation depends only on the w's ratios. Returns
            // the factor taken off, which fog multiplies back (vs_im2d).
            float normaliseIm2DW(Im2DVertex* v, int32 n)
            {
                float maxW = 0.0f;
                for (int32 i = 0; i < n; i++)
                {
                    if (v[i].w > maxW)
                    {
                        maxW = v[i].w;
                    }
                }
                if (maxW > 0.0f && maxW != 1.0f)
                {
                    const float k = 1.0f / maxW;
                    for (int32 i = 0; i < n; i++)
                    {
                        v[i].w *= k;
                    }
                    return maxW;
                }
                return 1.0f;
            }

            // u_fog for an Im2D draw: its w's were divided by `wFactor`.
            void setIm2DFog(float wFactor)
            {
                setUniform4(REG_FOG, sFogScale, sFogBias, wFactor, 0.0f);
            }

            // GL3's fog factor, f = clamp((w - far) / (fogPlane - far), 0, 1),
            // as u_fog's scale and bias onto the ramp's u: the ramp's texel i
            // holds i / 63, so u = (63 f + 0.5) / 64 lands f between texel
            // centres, and the clamp is the texture's clamp to edge.
            void setCameraFog(Camera* cam)
            {
                float d = cam->fogPlane - cam->farPlane;
                if (fabsf(d) < 1e-6f)
                {
                    d = -1e-6f; // GL3's fogRangeOf: a step, not a division by zero
                }
                const float range = 1.0f / d;
                sFogPlane = cam->fogPlane;
                sFarPlane = cam->farPlane;
                sFogScale = 63.0f / 64.0f * range;
                sFogBias = (0.5f - 63.0f * range * cam->farPlane) / 64.0f;
                setUniform4(REG_FOG, sFogScale, sFogBias, 1.0f, 0.0f);
            }

            void setRasterUVMatrix(void)
            {
                float us = 1.0f;
                float vs = 1.0f;
                if (rs.raster != nil && rs.raster->platform == PLATFORM_C3D)
                {
                    C3dRaster* natras = GETC3DRASTEREXT(rs.raster);
                    if (natras->screenCopy != COPY_NONE)
                    {
                        float rows[8];
                        screenCopyUV(rows);
                        setUniform4(REG_UVMAT, rows[0], rows[1], rows[2], rows[3]);
                        setUniform4(REG_UVMAT + 1, rows[4], rows[5], rows[6], rows[7]);
                        return;
                    }
                    us = natras->uScale;
                    vs = natras->vScale;
                }
                setUniform4(REG_UVMAT, us, 0.0f, 0.0f, 0.0f);
                setUniform4(REG_UVMAT + 1, 0.0f, vs, 0.0f, 0.0f);
            }

            double tickMs(u64 t, uint32 n)
            {
                return n > 0 ? (double)t * 1000.0 / (double)SYSCLOCK_ARM11 / n : 0.0;
            }

            // The frames since the last stats line, averaged: where the time
            // went, and how many vertices each program was given.
            void printFrameTimes(void)
            {
                FrameTimes f;
                uint32 n;
                u64 wallMax;
                takeFrameSums(&f, &n, &wallMax);
                if (n == 0)
                {
                    return;
                }
                char mt[224];
                const double wall = tickMs(f.wall, n);
                const double known =
                    tickMs(f.t[TIME_RECORD] + f.t[TIME_SYNC] + f.t[TIME_GPU] + f.t[TIME_END], n);
                printf("tssm: c3d: frame times (mean of %u frames, %s): wall %.2f ms (max %.2f), "
                       "recording %.2f ms (CPU skinning %.2f), vblank wait %.2f ms, GPU wait "
                       "%.2f ms, FrameEnd %.2f ms, the rest %.2f ms; GPU %.2f ms a frame; vertices "
                       "a frame: world %u, lit %u, im2d %u, im3d %u, skin %u, env %u, skinenv "
                       "%u; the timing itself %u tick reads a frame, about %.2f ms\n",
                       (unsigned)n, modesText(&modes, mt, sizeof(mt)), wall, tickMs(wallMax, 1),
                       tickMs(f.t[TIME_RECORD], n), tickMs(f.t[TIME_CPUSKIN], n),
                       tickMs(f.t[TIME_SYNC], n), tickMs(f.t[TIME_GPU], n),
                       tickMs(f.t[TIME_END], n), wall > known ? wall - known : 0.0,
                       tickMs(f.t[TIME_DRAWN], n), f.verts[PROG_WORLD] / n, f.verts[PROG_LIT] / n,
                       f.verts[PROG_IM2D] / n, f.verts[PROG_IM3D] / n, f.verts[PROG_SKIN] / n,
                       f.verts[PROG_ENV] / n, f.verts[PROG_SKINENV] / n, f.reads / n,
                       (double)f.reads / n * tickReadNs() / 1e6);
            }

            void statsTick(void)
            {
                if (sStatsEnv < 0)
                {
                    // TSSM_C3D_STATS=1 a line a second; =<n> every n seconds.
                    const char* e = getenv("TSSM_C3D_STATS");
                    sStatsEnv = e != nil && e[0] != '\0' && e[0] != '0';
                    const int n = e != nil ? atoi(e) : 0;
                    sStatsPeriod = n > 1 ? (u64)n * 1000 : 1000;
                }
                if (!sStatsEnv)
                {
                    return;
                }
                u64 now = osGetTime();
                if (sStatsAt == 0)
                {
                    sStatsAt = now;
                }
                if (now - sStatsAt < sStatsPeriod)
                {
                    return;
                }
                if (!modes.statsLine)
                {
                    // statsline=off (probe 14): is the line's own SD write a
                    // frame's cost? The sums still restart.
                    sStatsAt = now;
                    FrameTimes f;
                    uint32 n;
                    u64 wallMax;
                    takeFrameSums(&f, &n, &wallMax);
                    return;
                }
                sStatsAt = now;
                Stats st;
                getStats(&st);
                printf(
                    "tssm: c3d: frame %u: %u draws (peak %u), command buffer peak %u KB, ring peak "
                    "%u KB, GX queue peak %u; %u textures (VRAM %u KB, linear %u KB); VRAM free "
                    "%u KB, linear free %u KB; skinned (peaks) %u vertices in %u pieces, %u bone "
                    "registers, bones %u us; %u environment-map draws; %u hard splits; (peaks a "
                    "frame) %u fogged draws, %u target binds, %u draws and %u clears into camera "
                    "textures, %u read back, %u screen copies; %u camera textures and copy blocks (%u KB "
                    "VRAM)\n",
                    st.frames, st.draws, st.drawsPeak, st.cmdBufPeak >> 10, st.ringPeak >> 10,
                    st.queuePeak, st.textures, st.texVram >> 10, st.texLinear >> 10,
                    st.vramFree >> 10, st.linearFree >> 10, st.skinVertsPeak, st.skinPiecesPeak,
                    st.boneRegsPeak, st.skinUsPeak, st.envDrawsPeak, st.hardSplits, st.fogDrawsPeak,
                    st.targetBindsPeak, st.rttDrawsPeak, st.rttClearsPeak, st.readbacksPeak,
                    st.copiesPeak, st.targets, st.targetVram >> 10);
                printFrameTimes();
                submitStereoStats();
                submitAlphaStats();
                fflush(stdout);
            }
        } // namespace

        L3Counts gL3;

        // ------------------------------------------------------------ matrices

        void mat4Mul(Mat4* dst, const Mat4* a, const Mat4* b)
        {
            Mat4 r;
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] +
                                a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
                }
            }
            *dst = r;
        }

        const Mat4* viewProj(void)
        {
            return &sViewProj;
        }

        bool32 cameraIsScreen(void)
        {
            return sScreenCamera;
        }

        // ------------------------------------------------------- pipeline state

        void setPipelineVertexAlpha(bool32 enable)
        {
            rs.pipelineFresh = 1;
            rs.pipelineOpaque = !enable;
            rs.vertexAlpha = enable || rs.appVertexAlpha;
        }

        void setTexture(Texture* tex)
        {
            if (tex == nil || tex->raster == nil)
            {
                setRaster(nil);
                return;
            }
            setRaster(tex->raster);
            rs.filter = tex->getFilter();
            rs.addrU = tex->getAddressU();
            rs.addrV = tex->getAddressV();
        }

        void setMaterial(const RGBA& color, const SurfaceProperties& surf)
        {
            // c / 255.0f for each byte, divided once (L3: four divisions a
            // draw); the same quotients, so the same uniforms.
            static float div255[256];
            static bool made;
            if (!made)
            {
                for (int i = 0; i < 256; i++)
                {
                    div255[i] = (float)i / 255.0f;
                }
                made = true;
            }
            if (!modes.fast)
            {
                setUniform4(REG_MATCOLOR, color.red / 255.0f, color.green / 255.0f,
                            color.blue / 255.0f, color.alpha / 255.0f);
            }
            else
            {
                setUniform4(REG_MATCOLOR, div255[color.red], div255[color.green],
                            div255[color.blue], div255[color.alpha]);
            }
            setUniform4(REG_SURF, surf.ambient, 0.0f, surf.diffuse, 1.0f / 127.0f);
        }

        void setWorldMatrix(Matrix* world, const V3d* rebase)
        {
            Mat4 w;
            V3d pos = world->pos;
            if (rebase != nil)
            {
                pos.x +=
                    world->right.x * rebase->x + world->up.x * rebase->y + world->at.x * rebase->z;
                pos.y +=
                    world->right.y * rebase->x + world->up.y * rebase->y + world->at.y * rebase->z;
                pos.z +=
                    world->right.z * rebase->x + world->up.z * rebase->y + world->at.z * rebase->z;
            }
            w.m[0][0] = world->right.x;
            w.m[0][1] = world->up.x;
            w.m[0][2] = world->at.x;
            w.m[0][3] = pos.x;
            w.m[1][0] = world->right.y;
            w.m[1][1] = world->up.y;
            w.m[1][2] = world->at.y;
            w.m[1][3] = pos.y;
            w.m[2][0] = world->right.z;
            w.m[2][1] = world->up.z;
            w.m[2][2] = world->at.z;
            w.m[2][3] = pos.z;
            w.m[3][0] = 0.0f;
            w.m[3][1] = 0.0f;
            w.m[3][2] = 0.0f;
            w.m[3][3] = 1.0f;
            Mat4 mvp;
            mat4Mul(&mvp, &sViewProj, &w);
            if (sNextDepth == 1)
            {
                for (int c = 0; c < 4; c++)
                {
                    mvp.m[2][c] = sWindowRow[0] * w.m[0][c] + sWindowRow[1] * w.m[1][c] +
                                  sWindowRow[2] * w.m[2][c] + sWindowRow[3] * w.m[3][c];
                }
            }
            else if (sNextDepth == 2)
            {
                mvp.m[2][0] = mvp.m[2][1] = mvp.m[2][2] = 0.0f;
                mvp.m[2][3] = sFlatZ;
            }
            sLastDepth = sNextDepth;
            sNextDepth = 0; // an Im3D draw after it keeps the camera's
            setUniformRows(REG_MVP, &mvp.m[0][0], 4);
        }

        void atomicDepth(Atomic* atomic)
        {
            sNextDepth = 0;
            if (!sParallel || sDiscard)
            {
                return;
            }
            if (sHudSim < 0)
            {
                const char* e = getenv("TSSM_C3D_HUDSIM");
                sHudSim = e != nil && e[0] == '1';
                if (sHudSim)
                {
                    printf("tssm: c3d: TSSM_C3D_HUDSIM=1: a model under a parallel camera whose "
                           "depth spans less than two float24 steps is drawn at one depth (a "
                           "simulation of the console's precision, for tests)\n");
                    fflush(stdout);
                }
            }
            // The bounding sphere in camera z, its radius through the scaled
            // axes (an upper bound).
            Geometry* geo = atomic->geometry;
            if (geo->numMorphTargets < 1)
            {
                return;
            }
            const Sphere& bs = geo->morphTargets[0].boundingSphere;
            Matrix* m = atomic->getFrame()->getLTM();
            const V3d c = { m->right.x * bs.center.x + m->up.x * bs.center.y +
                                m->at.x * bs.center.z + m->pos.x,
                            m->right.y * bs.center.x + m->up.y * bs.center.y +
                                m->at.y * bs.center.z + m->pos.y,
                            m->right.z * bs.center.x + m->up.z * bs.center.y +
                                m->at.z * bs.center.z + m->pos.z };
            const float* v = sViewZ;
            const float cz = v[0] * c.x + v[1] * c.y + v[2] * c.z + v[3];
            const float rz =
                bs.radius * (fabsf(v[0] * m->right.x + v[1] * m->right.y + v[2] * m->right.z) +
                             fabsf(v[0] * m->up.x + v[1] * m->up.y + v[2] * m->up.z) +
                             fabsf(v[0] * m->at.x + v[1] * m->at.y + v[2] * m->at.z));
            // Anything reaching past the window (none of the HUD's models
            // does) keeps the camera's whole range, as before.
            if (sHasWindow && cz + 2.0f * rz + 1.0f <= sCamNear + kHudDepth)
            {
                sNextDepth = 1;
                return;
            }
            if (sHasWindow)
            {
                logOnce("hudzpast", "hudz: a model under a parallel camera reaches past the "
                                    "depth window; drawn over the camera's whole range");
            }
            if (sHudSim)
            {
                // The depth the map gives (flushState) at the centre and how
                // far it moves over the model.
                const float range = sCamFar - sCamNear;
                const float zc = sOrthoDepth ? -(cz - sCamNear) / range : (cz - sCamFar) / range;
                const float d = sOrthoDepth ? 1.0f + zc : -zc;
                const float span = 2.0f * rz / range;
                const float step = ldexpf(1.0f, ilogbf(d) - 16);
                if (span < 2.0f * step)
                {
                    const float dq = roundf(d / step) * step;
                    sFlatZ = sOrthoDepth ? dq - 1.0f : -dq;
                    sNextDepth = 2;
                }
            }
        }

        void setUVMatrix(const float32* xf)
        {
            if (xf == nil)
            {
                setUniform4(REG_UVMAT, 1.0f, 0.0f, 0.0f, 0.0f);
                setUniform4(REG_UVMAT + 1, 0.0f, 1.0f, 0.0f, 0.0f);
                return;
            }
            setUniform4(REG_UVMAT, xf[0], xf[1], xf[2], xf[3]);
            setUniform4(REG_UVMAT + 1, xf[4], xf[5], xf[6], xf[7]);
        }

        // The GL device's lighting, moved into object space: with N not
        // normalised, dot(Mn n, -L) = dot(n, Mn^T (-L)), so each directional is
        // turned once per atomic and the shader never builds a world normal.
        // Mn is the world matrix with its axes made unit length.
        void setLights(Atomic* atomic)
        {
            float amb[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float dir[4][4];
            float col[4][4];
            memset(dir, 0, sizeof(dir));
            memset(col, 0, sizeof(col));

            World* world = (World*)engine->currentWorld;
            if (atomic != nil && world != nil && (atomic->geometry->flags & Geometry::LIGHT))
            {
                WorldLights wl;
                Light* directionals[8];
                Light* locals[8];
                wl.directionals = directionals;
                wl.numDirectionals = 8;
                wl.locals = locals;
                wl.numLocals = 8;
                world->enumerateLights(atomic, &wl);

                amb[0] = wl.ambient.red;
                amb[1] = wl.ambient.green;
                amb[2] = wl.ambient.blue;
                if (wl.numDirectionals > 4)
                {
                    logOnce("dirs",
                            "an atomic is lit by %d directional lights; the first 4 are drawn",
                            (int)wl.numDirectionals);
                }
                if (wl.numLocals > 0)
                {
                    logOnce("locals", "point and spot lights are not drawn yet");
                }

                Matrix* ltm = atomic->getFrame()->getLTM();
                const int n = wl.numDirectionals < 4 ? wl.numDirectionals : 4;
                V3d Ls[4];
                for (int i = 0; i < n; i++)
                {
                    Light* l = wl.directionals[i];
                    Ls[i] = l->getFrame()->getLTM()->at; // where the light travels
                    col[i][0] = l->color.red;
                    col[i][1] = l->color.green;
                    col[i][2] = l->color.blue;
                }
                // L3's light cache: the last atomic's inputs and directions.
                // Atomics in a row often share their axes (the world's are
                // all identity) and the lights rarely move within a frame.
                static V3d lastAxes[3], lastLs[4];
                static int lastN = -1;
                static float lastDir[4][4];
                const bool same =
                    modes.fast && lastN == n &&
                    memcmp(lastAxes, &ltm->right, sizeof(V3d)) == 0 &&
                    memcmp(&lastAxes[1], &ltm->up, sizeof(V3d)) == 0 &&
                    memcmp(&lastAxes[2], &ltm->at, sizeof(V3d)) == 0 &&
                    memcmp(lastLs, Ls, sizeof(V3d) * (size_t)n) == 0;
                gL3.lightCalls++;
                if (same)
                {
                    gL3.lightHits++;
                    memcpy(dir, lastDir, sizeof(dir));
                }
                else
                {
                    V3d axes[3] = { ltm->right, ltm->up, ltm->at };
                    lastAxes[0] = axes[0];
                    lastAxes[1] = axes[1];
                    lastAxes[2] = axes[2];
                    for (int a = 0; a < 3; a++)
                    {
                        float len = sqrtf(axes[a].x * axes[a].x + axes[a].y * axes[a].y +
                                          axes[a].z * axes[a].z);
                        if (len > 1e-8f)
                        {
                            axes[a].x /= len;
                            axes[a].y /= len;
                            axes[a].z /= len;
                        }
                    }
                    for (int i = 0; i < n; i++)
                    {
                        const V3d& L = Ls[i];
                        for (int a = 0; a < 3; a++)
                        {
                            dir[i][a] = -(axes[a].x * L.x + axes[a].y * L.y + axes[a].z * L.z);
                        }
                        lastLs[i] = L;
                    }
                    lastN = n;
                    memcpy(lastDir, dir, sizeof(dir));
                }
            }

            setUniform4(REG_AMBIENT, amb[0], amb[1], amb[2], 0.0f);
            for (int i = 0; i < 4; i++)
            {
                setUniform4(REG_LIGHTDIR + i, dir[i][0], dir[i][1], dir[i][2], 0.0f);
                setUniform4(REG_LIGHTCOL + i, col[i][0], col[i][1], col[i][2], 0.0f);
            }
        }

        void setIm2DActive(bool32 active)
        {
            rs.im2DActive = active;
        }

        bool32 drawsDiscarded(void)
        {
            return sDiscard || !submitIsOpen();
        }

        void deviceDescribe(char* buf, int32 size)
        {
            const Raster* r = rs.raster;
            snprintf(buf, (size_t)size,
                     "raster %dx%d type %d, src %u dst %u, vertex alpha %d (app %d), texture alpha "
                     "%d, z test %d write %d, colour mask %#x, alpha func %u ref %u",
                     r != nil ? (int)r->width : 0, r != nil ? (int)r->height : 0,
                     r != nil ? (int)r->type : -1, (unsigned)rs.srcblend, (unsigned)rs.destblend,
                     (int)rs.vertexAlpha, (int)rs.appVertexAlpha, (int)rs.textureAlpha,
                     (int)rs.ztest, (int)rs.zwrite, (unsigned)rs.colorwritemask,
                     (unsigned)rs.alphafunc, (unsigned)rs.alpharef);
        }

        int32 devicePass(void)
        {
            const int32 sp = screenPassRunning();
            if (sp == 0)
            {
                return PASS_GLOW;
            }
            if (sp == 1)
            {
                return PASS_WARP;
            }
            if (sTexTarget)
            {
                return sShadowTarget ? PASS_SHADOW : PASS_RTT;
            }
            return sParallel || rs.im2DActive ? PASS_HUD : PASS_3D;
        }

        void deviceStateDirty(void)
        {
            ap.valid = false;
        }

        void deviceResync(void)
        {
            // Unit 0 bound again by the next textured draw (citro3d must not
            // keep a pointer the filter has forgotten); unit 1 back on the
            // dummy if anything was on it; every stage and test set again.
            C3D_TexBind(0, nil);
            ap.tex[0] = nil;
            ap.texGen[0] = 0;
            C3D_Tex* d = dummyTexture();
            if (ap.tex[1] != nil && d != nil)
            {
                C3D_TexBind(1, d);
                ap.tex[1] = d;
            }
            ap.texGen[1] = 0;
            ap.valid = false;
            ap.tev = -2;
            ap.fog = -1;
        }

        void deviceResetStats(void)
        {
            sDraws = 0;
        }

        void deviceForgetTexture(const C3D_Tex* tex)
        {
            // citro3d keeps the pointer it was bound with, and a return from
            // HOME marks every texture unit for sending again
            // (C3Di_AptEventHook), from that pointer, even for a draw without
            // a texture; and a raster made later at the same address must not
            // pass the filter as already bound.
            if (ap.tex[0] == tex)
            {
                C3D_TexBind(0, nil);
                ap.tex[0] = nil;
            }
            if (ap.tex[1] == tex)
            {
                C3D_Tex* d = dummyTexture();
                if (d != nil)
                {
                    C3D_TexBind(1, d);
                }
                ap.tex[1] = d;
            }
        }

        void setEnvMap(Texture* env, const float* rows, uint32 rgba, bool32 modulateByAlpha)
        {
            rs.envTex = env;
            if (env == nil)
            {
                return;
            }
            setUniformRows(REG_ENVMAT, rows, 2);
            rs.envColor = rgba;
            rs.envByAlpha = modulateByAlpha;
        }

        namespace
        {
            // Stages 1-3 back to passing the colour on, after an environment
            // map's.
            void resetEnvStages(void)
            {
                for (int i = 1; i <= 3; i++)
                {
                    C3D_TexEnvInit(C3D_GetTexEnv(i));
                }
                C3D_TexEnvBufUpdate(C3D_Both, 0);
            }

            // Stage 4: the colour so far towards the fog's by
            // texture 2's alpha, the ramp read at vs_*'s outtc2; alpha passes.
            void setFogStage(bool on, uint32 rgba)
            {
                C3D_TexEnv* e = C3D_GetTexEnv(4);
                C3D_TexEnvInit(e);
                if (on)
                {
                    bindFogRamp("the first fogged draw since start-up or fog=off");
                    C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_TEXTURE2);
                    C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR,
                                    GPU_TEVOP_RGB_SRC_ALPHA);
                    C3D_TexEnvFunc(e, C3D_RGB, GPU_INTERPOLATE);
                    C3D_TexEnvColor(e, rgba);
                }
                ap.fog = on ? 1 : 0;
                ap.fogColor = rgba;

                // The fog each time a new one is first drawn with (a level's,
                // or a transition's steps), for comparing with the PC; a far
                // plane a unit off (GG02 alternates 899 and 900) is the same.
                static float lastPlane = -1.0f, lastFar = -1.0f;
                static uint32 lastColor;
                static int logged;
                if (on && logged < 16 &&
                    (fabsf(sFogPlane - lastPlane) > 1.0f || fabsf(sFarPlane - lastFar) > 1.0f ||
                     rgba != lastColor))
                {
                    logged++;
                    lastPlane = sFogPlane;
                    lastFar = sFarPlane;
                    lastColor = rgba;
                    printf("tssm: c3d: fog from %.1f to %.1f, colour %02x %02x %02x (r g b)\n",
                           (double)sFogPlane, (double)sFarPlane, (unsigned)(rgba & 0xFF),
                           (unsigned)((rgba >> 8) & 0xFF), (unsigned)((rgba >> 16) & 0xFF));
                    fflush(stdout);
                }
            }
        } // namespace

        void deviceTevPlain(void)
        {
            if (ap.tev == TEV_ENV || ap.tev == TEV_ENV_COLOR || ap.tev < 0)
            {
                resetEnvStages();
            }
            // Unit 1 keeps its texture: no stage reads it.
            ap.tev = -1;
            if (ap.fog != 0)
            {
                C3D_TexEnvInit(C3D_GetTexEnv(4));
                ap.fog = 0;
            }
        }

        namespace
        {
            // A raster on a texture unit, with the filter and addressing a
            // draw asks for (they live in the texture's own parameters, so a
            // change there means binding it again).
            void bindUnit(int unit, C3dRaster* natras, uint32 f, uint32 wantU, uint32 wantV)
            {
                bool changed = false;
                if (natras->filter != f)
                {
                    const bool lin = f == Texture::LINEAR || f == Texture::MIPLINEAR ||
                                     f == Texture::LINEARMIPLINEAR;
                    const bool mipLin =
                        f == Texture::LINEARMIPNEAREST || f == Texture::LINEARMIPLINEAR;
                    C3D_TexSetFilter(&natras->tex, lin ? GPU_LINEAR : GPU_NEAREST,
                                     lin ? GPU_LINEAR : GPU_NEAREST);
                    C3D_TexSetFilterMipmap(&natras->tex, mipLin ? GPU_LINEAR : GPU_NEAREST);
                    natras->filter = (uint8)f;
                    changed = true;
                }
                // A picture padded onto a bigger texture (a movie frame) is
                // clamped: wrapping would reach the padding.
                const bool padded = natras->uScale < 1.0f || natras->vScale < 1.0f;
                const uint32 addrU = padded ? (uint32)Texture::CLAMP : wantU;
                const uint32 addrV = padded ? (uint32)Texture::CLAMP : wantV;
                if (natras->addrU != addrU || natras->addrV != addrV)
                {
                    C3D_TexSetWrap(&natras->tex, wrapFor(addrU), wrapFor(addrV));
                    natras->addrU = (uint8)addrU;
                    natras->addrV = (uint8)addrV;
                    changed = true;
                }
                if (!ap.valid || changed || ap.tex[unit] != &natras->tex ||
                    ap.texGen[unit] != natras->generation)
                {
                    C3D_TexBind(unit, &natras->tex);
                    noteCount(COUNT_TEX_BINDS, 1);
                    ap.tex[unit] = &natras->tex;
                    ap.texGen[unit] = natras->generation;
                }
            }

            // Stage 0, and for an environment map stages 1-3, as
            // matfx_env.frag: rgb = base.rgb * a + env.rgb * a, alpha = a,
            // with base = texture 0 x vertex colour and env = texture 1 x the
            // constant. The base goes to the combiner buffer, which only
            // stage 3 reads: a stage reads what the buffer held two stages
            // back, and with no other stage writing it, one stage back would
            // read the same.
            void setTev(int tev)
            {
                C3D_TexEnv* e0 = C3D_GetTexEnv(0);
                C3D_TexEnvInit(e0);
                if (tev == TEV_TEXTURED || tev == TEV_ENV)
                {
                    C3D_TexEnvSrc(e0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                    C3D_TexEnvFunc(e0, C3D_Both, GPU_MODULATE);
                }
                else
                {
                    C3D_TexEnvSrc(e0, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR,
                                  GPU_PRIMARY_COLOR);
                    C3D_TexEnvFunc(e0, C3D_Both, GPU_REPLACE);
                }
                if (tev != TEV_ENV && tev != TEV_ENV_COLOR)
                {
                    return;
                }
                C3D_TexEnvBufUpdate(C3D_Both, BIT(0));

                C3D_TexEnv* e1 = C3D_GetTexEnv(1);
                C3D_TexEnvInit(e1);
                C3D_TexEnvSrc(e1, C3D_RGB, GPU_TEXTURE1, GPU_CONSTANT, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e1, C3D_RGB, GPU_MODULATE);

                // env.rgb * a (stage 1 passed the alpha on)
                C3D_TexEnv* e2 = C3D_GetTexEnv(2);
                C3D_TexEnvInit(e2);
                C3D_TexEnvSrc(e2, C3D_RGB, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
                C3D_TexEnvOpRgb(e2, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA,
                                GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e2, C3D_RGB, GPU_MODULATE);

                // + base.rgb * a; alpha = a
                C3D_TexEnv* e3 = C3D_GetTexEnv(3);
                C3D_TexEnvInit(e3);
                C3D_TexEnvSrc(e3, C3D_RGB, GPU_PREVIOUS_BUFFER, GPU_PREVIOUS_BUFFER, GPU_PREVIOUS);
                C3D_TexEnvOpRgb(e3, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA,
                                GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e3, C3D_RGB, GPU_MULTIPLY_ADD);
            }
        } // namespace

        namespace
        {
            bool32 flushRest(C3dRaster* natras, bool depthEnable, uint32 depthFunc, uint32 cull,
                             uint32 back, bool orthoDepth, bool blend, uint32 src, uint32 dst,
                             bool test, uint32 func, uint32 ref);
        } // namespace

        // The render states into citro3d's. FALSE: do not draw (the texture
        // is a camera texture nothing has rendered yet).
        bool32 flushState(void)
        {
            ProfScope prof(PROF_FLUSH);
            // The pipeline's word holds for the draw it was given for, not
            // for an immediate-mode draw after it.
            if (!rs.pipelineFresh && rs.pipelineOpaque)
            {
                rs.pipelineOpaque = 0;
                rs.vertexAlpha = rs.appVertexAlpha;
            }
            rs.pipelineFresh = 0;

            C3dRaster* natras = nil;
            if (rs.raster != nil && rs.raster->platform == PLATFORM_C3D)
            {
                natras = GETC3DRASTEREXT(rs.raster);
                if (natras->isCamera ? !rttSampleable(natras) : !natras->hasTex)
                {
                    return 0;
                }
            }

            if (sTexTarget && (rs.ztest || rs.zwrite))
            {
                eyeFreeDepth(); // eyeskip
            }

            // alphafill: the top screen's alpha before the glow's reset.
            const bool mayBlend = rs.vertexAlpha || rs.textureAlpha;
            const bool readsDst = mayBlend && (readsDstAlpha(rs.srcblend) || readsDstAlpha(rs.destblend));
            const bool holdAlpha =
                topAlphaDraw((rs.colorwritemask & COLORWRITEALPHA) != 0, readsDst) != 0;

            // L3's fast path: what the part down to the depth map reads.
            struct Key
            {
                uint32 textureAlpha, textureKeyed, vertexAlpha, zwrite, im2DActive,
                    pipelineOpaque, alpharef, alphafunc, srcblend, destblend, colorwritemask,
                    ztest, cullmode, texTarget, texFlip, orthoDepth, holdAlpha;
            };
            const Key key = { (uint32)rs.textureAlpha, (uint32)rs.textureKeyed,
                              (uint32)rs.vertexAlpha,  (uint32)rs.zwrite,
                              (uint32)rs.im2DActive,   (uint32)rs.pipelineOpaque,
                              rs.alpharef,             rs.alphafunc,
                              rs.srcblend,             rs.destblend,
                              rs.colorwritemask,       (uint32)rs.ztest,
                              rs.cullmode,             (uint32)sTexTarget,
                              (uint32)sTexFlip,        (uint32)sOrthoDepth,
                              (uint32)holdAlpha };
            static Key lastKey;
            static bool lastKeyValid;
            static uint32 lastCull, lastBack;
            static bool lastDepthEnable, lastBlend, lastTest;
            static uint32 lastDepthFunc, lastSrc, lastDst, lastFunc, lastRef;
            gL3.stateCalls++;
            if (modes.fast && ap.valid && lastKeyValid && memcmp(&key, &lastKey, sizeof(key)) == 0)
            {
                gL3.stateHits++;
                return flushRest(natras, lastDepthEnable, lastDepthFunc, lastCull, lastBack,
                                 sOrthoDepth && !rs.im2DActive, lastBlend, lastSrc, lastDst,
                                 lastTest, lastFunc, lastRef);
            }

            const bool cutout = rs.textureAlpha && rs.textureKeyed && !rs.vertexAlpha &&
                                rs.zwrite && !rs.im2DActive;
            const bool solidKeyed = rs.textureAlpha && rs.textureKeyed && rs.pipelineOpaque &&
                                    rs.zwrite && !rs.im2DActive;
            const bool test = rs.vertexAlpha || rs.textureAlpha;
            const bool blend = rs.vertexAlpha || (rs.textureAlpha && !cutout);
            const uint32 ref =
                (cutout || solidKeyed) && rs.alpharef <= 1 ? (uint32)ALPHACUTOUTREF : rs.alpharef;

            const uint32 func = !test                             ? GPU_ALWAYS :
                                rs.alphafunc == ALPHAGREATEREQUAL ? GPU_GEQUAL :
                                rs.alphafunc == ALPHALESS         ? GPU_LESS :
                                                                    GPU_ALWAYS;
            const uint32 src = blend ? blendFactor(rs.srcblend) : GPU_ONE;
            const uint32 dst = blend ? blendFactor(rs.destblend) : GPU_ZERO;

            uint32 mask = 0;
            if (rs.colorwritemask & COLORWRITERED)
                mask |= GPU_WRITE_RED;
            if (rs.colorwritemask & COLORWRITEGREEN)
                mask |= GPU_WRITE_GREEN;
            if (rs.colorwritemask & COLORWRITEBLUE)
                mask |= GPU_WRITE_BLUE;
            if ((rs.colorwritemask & COLORWRITEALPHA) && !holdAlpha)
                mask |= GPU_WRITE_ALPHA;
            if (rs.zwrite)
                mask |= GPU_WRITE_DEPTH;
            // A depth write with the test off is a test that always passes,
            // as on D3D.
            const bool depthEnable = rs.ztest || rs.zwrite;
            const uint32 depthFunc = rs.ztest ? GPU_GEQUAL : GPU_ALWAYS;

            // A picture drawn upside down turns the winding round.
            const bool flip = sTexTarget && sTexFlip;
            const uint32 back = flip ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
            const uint32 front = flip ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
            const uint32 cull = rs.cullmode == CULLBACK  ? back :
                                rs.cullmode == CULLFRONT ? front :
                                                           GPU_CULL_NONE;

            if (!ap.valid || ap.blend != blend || ap.src != src || ap.dst != dst)
            {
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, (GPU_BLENDFACTOR)src,
                               (GPU_BLENDFACTOR)dst, (GPU_BLENDFACTOR)src, (GPU_BLENDFACTOR)dst);
                noteCount(COUNT_BLEND, 1);
                ap.blend = blend;
                ap.src = src;
                ap.dst = dst;
            }
            if (!ap.valid || ap.test != test || ap.func != func || ap.ref != ref)
            {
                C3D_AlphaTest(test, (GPU_TESTFUNC)func, (int)ref);
                noteCount(COUNT_ALPHA, 1);
                ap.test = test;
                ap.func = func;
                ap.ref = ref;
            }
            if (!ap.valid || ap.depthEnable != depthEnable || ap.depthFunc != depthFunc ||
                ap.writeMask != mask)
            {
                C3D_DepthTest(depthEnable, (GPU_TESTFUNC)depthFunc, (GPU_WRITEMASK)mask);
                noteCount(COUNT_DEPTH, 1);
                ap.depthEnable = depthEnable;
                ap.depthFunc = depthFunc;
                ap.writeMask = mask;
            }
            if (!ap.valid || ap.cull != cull)
            {
                C3D_CullFace((GPU_CULLMODE)cull);
                noteCount(COUNT_CULL, 1);
                ap.cull = cull;
            }
            // 2D keeps its own z (vs_im2d: -1 near) under any camera.
            const bool orthoDepth = sOrthoDepth && !rs.im2DActive;
            if (!ap.valid || ap.orthoDepth != orthoDepth)
            {
                if (orthoDepth)
                {
                    C3D_DepthMap(true, 1.0f, 1.0f);
                }
                else
                {
                    C3D_DepthMap(true, -1.0f, 0.0f);
                }
                noteCount(COUNT_DEPTHMAP, 1);
                ap.orthoDepth = orthoDepth;
            }
            lastKey = key;
            lastKeyValid = true;
            lastCull = cull;
            lastBack = back;
            lastDepthEnable = depthEnable;
            lastDepthFunc = depthFunc;
            lastBlend = blend;
            lastSrc = src;
            lastDst = dst;
            lastTest = test;
            lastFunc = func;
            lastRef = ref;
            return flushRest(natras, depthEnable, depthFunc, cull, back, orthoDepth, blend, src,
                             dst, test, func, ref);
        }

        namespace
        {
            // flushState from the textures on: the units, the combiners, the
            // fog stage, and the draw counted.
            bool32 flushRest(C3dRaster* natras, bool depthEnable, uint32 depthFunc, uint32 cull,
                             uint32 back, bool orthoDepth, bool blend, uint32 src, uint32 dst,
                             bool test, uint32 func, uint32 ref)
            {

            // The environment map holds for this draw only.
            C3dRaster* envRas = nil;
            Texture* envTex = rs.envTex;
            rs.envTex = nil;
            if (envTex != nil && envTex->raster != nil && envTex->raster->platform == PLATFORM_C3D)
            {
                envRas = GETC3DRASTEREXT(envTex->raster);
                if (envRas->isCamera ? !rttSampleable(envRas) : !envRas->hasTex)
                {
                    envRas = nil;
                }
            }

            if (!ap.valid)
            {
                ap.tev = -2; // not known: every stage is set again
            }
            if (natras != nil)
            {
                bindUnit(0, natras, rs.filter, rs.addrU, rs.addrV);
            }
            int tev = natras != nil ? TEV_TEXTURED : TEV_COLOR;
            if (envRas != nil)
            {
                bindUnit(1, envRas, envTex->getFilter(), envTex->getAddressU(),
                         envTex->getAddressV());
                tev = natras != nil ? TEV_ENV : TEV_ENV_COLOR;
                noteCount(COUNT_ENV_DRAWS, 1);
            }
            else if (ap.tev == TEV_ENV || ap.tev == TEV_ENV_COLOR || ap.tev < 0)
            {
                resetEnvStages();
                ap.tev = -1;
                // Unit 1 cannot be unbound and is sampled by every later
                // draw: the 8x8 dummy keeps those reads cheap.
                C3D_Tex* d = dummyTexture();
                if (d != nil && ap.tex[1] != nil && ap.tex[1] != d)
                {
                    C3D_TexBind(1, d);
                    noteCount(COUNT_TEX_BINDS, 1);
                    ap.tex[1] = d;
                }
            }
            if (ap.tev != tev)
            {
                setTev(tev);
                noteCount(COUNT_TEV, 1);
                ap.tev = tev;
                ap.envColor = ~rs.envColor; // the constant is set below
                ap.envByAlpha = !rs.envByAlpha;
            }
            if ((tev == TEV_ENV || tev == TEV_ENV_COLOR) &&
                (ap.envColor != rs.envColor || ap.envByAlpha != (bool)rs.envByAlpha))
            {
                C3D_TexEnvColor(C3D_GetTexEnv(1), rs.envColor);
                C3D_TexEnv* e2 = C3D_GetTexEnv(2);
                // With the base's alpha off (MatFX's fbAlpha clear and
                // envMapModulateByAlpha off), stage 2 passes the env through.
                C3D_TexEnvFunc(e2, C3D_RGB, rs.envByAlpha ? GPU_MODULATE : GPU_REPLACE);
                ap.envColor = rs.envColor;
                ap.envByAlpha = rs.envByAlpha;
            }

            const bool fog = rs.fogEnable && modes.fog && fogRamp() != nil;
            if (!ap.valid || ap.fog != (fog ? 1 : 0) || (fog && ap.fogColor != rs.fogColor))
            {
                setFogStage(fog, rs.fogColor);
                noteCount(COUNT_FOGSTAGE, 1);
            }
            if (fog)
            {
                bindFogRamp("the first fogged draw since start-up or fog=off");
                noteCount(COUNT_FOG_DRAWS, 1);
            }
            ap.valid = true;
            sDraws++;
            if (!rs.im2DActive && hudLogOn())
            {
                sHudLines++;
                printf("tssm: c3d: hud %d:   state depth %s func %s write %d, depth map %s, cull %s, "
                       "blend %d (%u %u), alpha test %d (%u ref %u), unit0 %s, unit1 %s, tev %d, "
                       "fog %d, target %s %gx%g\n",
                       iCaptureFrameCount(), depthEnable ? "on" : "off",
                       depthFunc == GPU_GEQUAL ? "GEQUAL" : "ALWAYS", rs.zwrite ? 1 : 0,
                       orthoDepth ? "parallel (1 + z)" : "usual (-z)",
                       cull == GPU_CULL_NONE ? "none" : cull == back ? "back" : "front",
                       blend ? 1 : 0, (unsigned)src, (unsigned)dst, test ? 1 : 0, (unsigned)func,
                       (unsigned)ref,
                       natras != nil ? (natras->isCamera ? "camera texture" : "texture") : "-",
                       envRas != nil ? "environment map" : "dummy", tev, fog ? 1 : 0,
                       sTexTarget ? "texture" : "screen", (double)sIm2DW, (double)sIm2DH);
                fflush(stdout);
            }
            return 1;
        }
        } // namespace

        // TSSM_C3D_HUDLOG: an atomic about to be drawn (c3dpipe.cpp).
        void hudLogAtomic(Atomic* atomic, const char* path)
        {
            if (!hudLogOn())
            {
                return;
            }
            sHudLines++;
            Geometry* geo = atomic->geometry;
            Matrix* ltm = atomic->getFrame()->getLTM();
            Mat4 w;
            const V3d* ax[4] = { &ltm->right, &ltm->up, &ltm->at, &ltm->pos };
            for (int r = 0; r < 3; r++)
            {
                for (int c = 0; c < 4; c++)
                {
                    const V3d* v = ax[c];
                    w.m[r][c] = r == 0 ? v->x : r == 1 ? v->y : v->z;
                }
            }
            w.m[3][0] = w.m[3][1] = w.m[3][2] = 0.0f;
            w.m[3][3] = 1.0f;
            Mat4 mvp;
            mat4Mul(&mvp, &sViewProj, &w);
            if (sLastDepth == 1)
            {
                for (int c = 0; c < 4; c++)
                {
                    mvp.m[2][c] = sWindowRow[0] * w.m[0][c] + sWindowRow[1] * w.m[1][c] +
                                  sWindowRow[2] * w.m[2][c] + sWindowRow[3] * w.m[3][c];
                }
            }
            else if (sLastDepth == 2)
            {
                mvp.m[2][0] = mvp.m[2][1] = mvp.m[2][2] = 0.0f;
                mvp.m[2][3] = sFlatZ;
            }
            float zmin = 1e30f, zmax = -1e30f, wmin = 1e30f, wmax = -1e30f;
            const V3d* verts = geo->numMorphTargets > 0 ? geo->morphTargets[0].vertices : nil;
            const int32 n = verts != nil ? geo->numVertices : 0;
            for (int32 i = 0; i < n; i++)
            {
                const V3d& p = verts[i];
                const float z =
                    mvp.m[2][0] * p.x + mvp.m[2][1] * p.y + mvp.m[2][2] * p.z + mvp.m[2][3];
                const float ww =
                    mvp.m[3][0] * p.x + mvp.m[3][1] * p.y + mvp.m[3][2] * p.z + mvp.m[3][3];
                const float zw = ww != 0.0f ? z / ww : z;
                zmin = zw < zmin ? zw : zmin;
                zmax = zw > zmax ? zw : zmax;
                wmin = ww < wmin ? ww : wmin;
                wmax = ww > wmax ? ww : wmax;
            }
            // The depth the map gives (flushState): 1 + z/w for a parallel
            // camera under orthoz, -z/w otherwise; and how many float24
            // steps the model spans there.
            const float d0 = sOrthoDepth ? 1.0f + zmax : -zmax;
            const float d1 = sOrthoDepth ? 1.0f + zmin : -zmin;
            const float dlo = d0 < d1 ? d0 : d1;
            const float dhi = d0 < d1 ? d1 : d0;
            const float zabs = fabsf(zmin) > fabsf(zmax) ? fabsf(zmin) : fabsf(zmax);
            int nlights = 0;
            World* world = (World*)engine->currentWorld;
            if (world != nil && (geo->flags & Geometry::LIGHT))
            {
                WorldLights wl;
                Light* d[8];
                Light* l[8];
                wl.directionals = d;
                wl.numDirectionals = 8;
                wl.locals = l;
                wl.numLocals = 8;
                world->enumerateLights(atomic, &wl);
                nlights = wl.numDirectionals;
            }
            int envs = 0;
            for (int32 i = 0; i < geo->matList.numMaterials; i++)
            {
                MatFX* fx = MatFX::get(geo->matList.materials[i]);
                envs += fx != nil && fx->type == MatFX::ENVMAP;
            }
            const float axR = sqrtf(ltm->right.x * ltm->right.x + ltm->right.y * ltm->right.y +
                                    ltm->right.z * ltm->right.z);
            const float axA =
                sqrtf(ltm->at.x * ltm->at.x + ltm->at.y * ltm->at.y + ltm->at.z * ltm->at.z);
            printf("tssm: c3d: hud %d: atomic %p (%s) %d verts %d tris %d materials, %s%s%d with "
                   "env maps, %d directionals; camera near %g far %g window %g x %g, clip z %s, "
                   "depth %s; "
                   "axes right %.4f at %.4f; clip z/w %.7f..%.7f w %.4f..%.4f -> depth "
                   "%.7f..%.7f = %.1f float24 steps of the depth, %.1f of clip z\n",
                   iCaptureFrameCount(), (void*)atomic, path, (int)geo->numVertices,
                   (int)geo->numTriangles, (int)geo->matList.numMaterials,
                   (geo->flags & Geometry::LIGHT) ? "lit, " : "unlit, ",
                   Skin::get(geo) != nil ? "skinned, " : "", envs, nlights, (double)sCamNear,
                   (double)sCamFar, (double)sCamVwX, (double)sCamVwY,
                   sOrthoDepth ? "0 near to -1 far (orthoz)" : "-1 near to 0 far",
                   sLastDepth == 1   ? "the 16-unit window (hudz)" :
                   sLastDepth == 2   ? "flattened (TSSM_C3D_HUDSIM)" :
                                       "the camera's whole range",
                   (double)axR,
                   (double)axA, (double)zmin, (double)zmax, (double)wmin, (double)wmax,
                   (double)dlo, (double)dhi, (double)((dhi - dlo) / f24Step(dhi)),
                   (double)((zmax - zmin) / f24Step(zabs)));
            fflush(stdout);
        }

        void hudLogProgram(int32 prog)
        {
            if (hudLogOn())
            {
                sHudLines++;
                static const char* names[] = { "vs_world", "vs_lit",  "vs_im2d",   "vs_im3d",
                                               "vs_skin",  "vs_env", "vs_skinenv" };
                printf("tssm: c3d: hud %d:   program %s\n", iCaptureFrameCount(),
                       prog >= 0 && prog < 7 ? names[prog] : "?");
                fflush(stdout);
            }
        }

        // ------------------------------------------------------------- camera

        static void beginUpdate(Camera* cam)
        {
            Raster* fb = cam->frameBuffer;
            sDiscard = false;
            sScreenCamera = false;
            sTexTarget = false;
            sTexFlip = false;
            sShadowTarget = false;
            sOrthoDepth = false;
            sParallel = cam->projection == Camera::PARALLEL;
            sCamNear = cam->nearPlane;
            sCamFar = cam->farPlane;
            sCamVwX = cam->viewWindow.x;
            sCamVwY = cam->viewWindow.y;
            if (!submitIsOpen())
            {
                return;
            }
            if (fb == nil || fb->type != Raster::CAMERA)
            {
                // A camera texture (the shadow camera, xFXCameraTexture): its
                // own target, untilted (c3drtt.cpp).
                if (fb == nil || !rttBegin(cam, fb))
                {
                    sDiscard = true;
                    return;
                }
                sTexTarget = true;
                sTexFlip = modes.rttFlip != 0;
                sShadowTarget = fb->platform == PLATFORM_C3D && GETC3DRASTEREXT(fb)->isShadow;
            }
            else
            {
                sScreenCamera = true;
                sCamPos = cam->getFrame()->getLTM()->pos;
            }

            // Stereo: in a 3D frame the main camera's draws are shifted per
            // eye (b_eye, u_eyeCam; c3dstereo.cpp); every other camera's not.
            {
                float eyeCam[4];
                const bool eye = stereoCamera(cam, sScreenCamera ? 1 : 0, eyeCam) != 0;
                setEyeShift(eye ? 1 : 0);
                if (eye)
                {
                    setUniform4(REG_EYECAM, eyeCam[0], eyeCam[1], eyeCam[2], eyeCam[3]);
                }
            }

            // The view matrix is the GL3 device's: the camera's frame
            // inverted and x flipped (RenderWare's camera looks down +z).
            Matrix inv;
            Matrix::invert(&inv, cam->getFrame()->getLTM());
            Mat4 view = { { { -inv.right.x, -inv.up.x, -inv.at.x, -inv.pos.x },
                            { inv.right.y, inv.up.y, inv.at.y, inv.pos.y },
                            { inv.right.z, inv.up.z, inv.at.z, inv.pos.z },
                            { 0.0f, 0.0f, 0.0f, 1.0f } } };

            // The projection's x and y rows are GL3's; z goes from -1 at the
            // near plane to 0 at the far one, as the PICA wants.
            const float iwx = 1.0f / cam->viewWindow.x;
            const float iwy = 1.0f / cam->viewWindow.y;
            const float n = cam->nearPlane;
            const float f = cam->farPlane;
            const float ifn = 1.0f / (f - n);
            Mat4 proj;
            memset(&proj, 0, sizeof(proj));
            proj.m[0][0] = iwx;
            proj.m[0][2] = cam->viewOffset.x * iwx;
            proj.m[0][3] = -cam->viewOffset.x * iwx;
            proj.m[1][1] = iwy;
            proj.m[1][2] = cam->viewOffset.y * iwy;
            proj.m[1][3] = -cam->viewOffset.y * iwy;
            if (cam->projection == Camera::PERSPECTIVE)
            {
                proj.m[2][2] = n * ifn;
                proj.m[2][3] = -n * f * ifn;
                proj.m[3][2] = 1.0f;
            }
            else if (modes.orthoZ)
            {
                // Not -1 (near) to 0 (far) as above: the shader's floats keep
                // 16 bits, and a HUD model a few units deep under the HUD
                // camera's 0.3 to 10000 spans 1e-4 of that range around -1,
                // too little to keep its faces in order (the Extras chest
                // showed its inside). From 0 (near) to -1 (far) it sits near
                // 0, where the floats are fine, and the depth map (flushState)
                // gives the same depth: 1 near, 0 far. Whether the console
                // applies that map's offset at full precision only it can
                // show: orthoz=off is the other way.
                proj.m[2][2] = -ifn;
                proj.m[2][3] = n * ifn;
                proj.m[3][3] = 1.0f;
                sOrthoDepth = true;
                logOnce("orthozon", "orthoz=on: a parallel camera's clip z from 0 (near) to -1 "
                                    "(far), the depth map's offset (first use)");
            }
            else
            {
                logOnce("orthozoff", "orthoz=off: parallel cameras' clip z from -1 (near) to 0 "
                                     "(far), the usual depth map");
                proj.m[2][2] = ifn;
                proj.m[2][3] = -f * ifn;
                proj.m[3][3] = 1.0f;
            }

            // hudz: a parallel camera's models over a window of its depth
            // (kHudDepth), in the same direction as the camera's clip z
            // above. Only the z row differs; the tilt and the flip leave it.
            sHasWindow = false;
            for (int c = 0; c < 4; c++)
            {
                sViewZ[c] = view.m[2][c];
            }
            if (cam->projection == Camera::PARALLEL && modes.hudZ && f - n > kHudDepth)
            {
                const float ir = 1.0f / kHudDepth;
                const float z2 = sOrthoDepth ? -ir : ir;
                const float z3 = sOrthoDepth ? n * ir : -(n + kHudDepth) * ir;
                for (int c = 0; c < 4; c++)
                {
                    sWindowRow[c] = z2 * view.m[2][c] + z3 * view.m[3][c];
                }
                sHasWindow = true;
                logOnce("hudzon", "hudz=on: a parallel camera's models within 16 units of its "
                                  "near plane get the whole depth range (first use)");
            }

            Mat4 pv;
            mat4Mul(&pv, &proj, &view);
            if (sTexTarget)
            {
                sViewProj = pv;
                if (sTexFlip)
                {
                    for (int c = 0; c < 4; c++)
                    {
                        sViewProj.m[1][c] = -sViewProj.m[1][c];
                    }
                }
            }
            else
            {
                mat4Mul(&sViewProj, &kTilt, &pv);
            }
            setCameraFog(cam);

            // The device matrices, as GL3 keeps them (column-major GL), for
            // anything that reads them back.
            float* dv = (float*)&cam->devView;
            float* dp = (float*)&cam->devProj;
            for (int c = 0; c < 4; c++)
            {
                for (int r = 0; r < 4; r++)
                {
                    dv[c * 4 + r] = view.m[r][c];
                }
            }
            memset(dp, 0, 16 * sizeof(float));
            dp[0] = iwx;
            dp[5] = iwy;
            dp[8] = cam->viewOffset.x * iwx;
            dp[9] = cam->viewOffset.y * iwy;
            dp[12] = -dp[8];
            dp[13] = -dp[9];
            if (cam->projection == Camera::PERSPECTIVE)
            {
                dp[10] = (f + n) * ifn;
                dp[11] = 1.0f;
                dp[14] = -2.0f * n * f * ifn;
            }
            else
            {
                dp[10] = 2.0f * ifn;
                dp[14] = -(f + n) * ifn;
                dp[15] = 1.0f;
            }

            sIm2DW = fb->width > 0 ? (float)fb->width : (float)SCREEN_W;
            sIm2DH = fb->height > 0 ? (float)fb->height : (float)SCREEN_H;

            if (sTexTarget)
            {
                return; // rttBegin set the target's viewport
            }
            if (fb->parent != nil && fb->parent != fb && fb->parent->width > 0 &&
                fb->parent->height > 0)
            {
                // A part of the screen.
                const float sx = (float)SCREEN_W / fb->parent->width;
                const float sy = (float)SCREEN_H / fb->parent->height;
                setScreenViewport((int32)(fb->offsetX * sx), (int32)(fb->offsetY * sy),
                                  (int32)(fb->width * sx), (int32)(fb->height * sy));
            }
            else
            {
                setScreenViewport(0, 0, SCREEN_W, SCREEN_H);
            }
        }

        static void endUpdate(Camera* cam)
        {
            (void)cam;
            sDiscard = false;
        }

        static void clearCamera(Camera* cam, RGBA* col, uint32 mode)
        {
            RecordScope timed;
            if (!submitIsOpen())
            {
                return;
            }
            Raster* fb = cam->frameBuffer;
            if (fb == nil)
            {
                return;
            }
            RGBA black = { 0, 0, 0, 255 };
            if (col == nil)
            {
                col = &black;
            }
            const uint32 rgba = ((uint32)col->red << 24) | ((uint32)col->green << 16) |
                                ((uint32)col->blue << 8) | (uint32)col->alpha;
            if (fb->type != Raster::CAMERA)
            {
                rttClear(cam, fb, rgba, (mode & Camera::CLEARIMAGE) != 0,
                         (mode & Camera::CLEARZ) != 0);
                return;
            }
            if (fb->parent != nil && fb->parent != fb)
            {
                logOnce("subclear", "a clear of part of the screen cleared all of it");
            }
            clearScreen(rgba, (mode & Camera::CLEARIMAGE) != 0, (mode & Camera::CLEARZ) != 0);
        }

        static void showRaster(Raster* raster, uint32 flags)
        {
            (void)raster;
            (void)flags;
            if (!submitIsOpen())
            {
                return;
            }
            noteStats(sDraws);
            frameEnd();
            statsTick();
            vbVramMigrate();

            // TSSM_C3D_BENCH=1: the draw micro-benchmark, once (c3dbench.cpp);
            // =2 its variants as well.
            static int benchAt = -1;
            static bool bench2;
            if (benchAt < 0)
            {
                const char* e = getenv("TSSM_C3D_BENCH");
                benchAt = e != nil && e[0] != '\0' && e[0] != '0' ? 30 : 0;
                bench2 = e != nil && e[0] == '2';
            }
            if (benchAt > 0 && --benchAt == 0)
            {
                runBench();
                if (bench2)
                {
                    runBench2();
                }
            }
        }

        static bool32 rasterRenderFast(Raster* raster, int32 x, int32 y)
        {
            (void)raster;
            (void)x;
            (void)y;
            logOnce("renderfast", "RwRasterRenderFast is not drawn");
            return 0;
        }

        // ------------------------------------------------------- render states

        static void setRenderState(int32 state, void* pvalue)
        {
            uint32 value = (uint32)(uintptr)pvalue;
            switch ((RenderState)state)
            {
            case TEXTURERASTER:
                setRaster((Raster*)pvalue);
                break;
            case TEXTUREADDRESS:
                rs.addrU = value;
                rs.addrV = value;
                break;
            case TEXTUREADDRESSU:
                rs.addrU = value;
                break;
            case TEXTUREADDRESSV:
                rs.addrV = value;
                break;
            case TEXTUREFILTER:
                rs.filter = value;
                break;
            case VERTEXALPHA:
                rs.appVertexAlpha = value;
                rs.vertexAlpha = value;
                break;
            case SRCBLEND:
                rs.srcblend = value;
                break;
            case DESTBLEND:
                rs.destblend = value;
                break;
            case ZTESTENABLE:
                rs.ztest = value;
                break;
            case ZWRITEENABLE:
                rs.zwrite = value;
                break;
            case FOGENABLE:
                rs.fogEnable = value;
                break;
            case FOGCOLOR:
                rs.fogColor = value;
                break;
            case CULLMODE:
                rs.cullmode = value;
                break;
            case STENCILENABLE:
                rs.stencilenable = value;
                break;
            case STENCILFAIL:
                rs.stencilfail = value;
                break;
            case STENCILZFAIL:
                rs.stencilzfail = value;
                break;
            case STENCILPASS:
                rs.stencilpass = value;
                break;
            case STENCILFUNCTION:
                rs.stencilfunc = value;
                break;
            case STENCILFUNCTIONREF:
                rs.stencilref = value;
                break;
            case STENCILFUNCTIONMASK:
                rs.stencilmask = value;
                break;
            case STENCILFUNCTIONWRITEMASK:
                rs.stencilwritemask = value;
                break;
            case ALPHATESTFUNC:
                rs.alphafunc = value;
                break;
            case ALPHATESTREF:
                rs.alpharef = value;
                break;
            case GSALPHATEST:
                rs.gsalpha = value;
                break;
            case GSALPHATESTREF:
                rs.gsalpharef = value;
                break;
            case COLORWRITEMASK:
                rs.colorwritemask = value;
                break;
            }
        }

        static void* getRenderState(int32 state)
        {
            uint32 v = 0;
            switch ((RenderState)state)
            {
            case TEXTURERASTER:
                return rs.raster;
            case TEXTUREADDRESS:
                v = rs.addrU == rs.addrV ? rs.addrU : 0;
                break;
            case TEXTUREADDRESSU:
                v = rs.addrU;
                break;
            case TEXTUREADDRESSV:
                v = rs.addrV;
                break;
            case TEXTUREFILTER:
                v = rs.filter;
                break;
            case VERTEXALPHA:
                v = rs.appVertexAlpha;
                break;
            case SRCBLEND:
                v = rs.srcblend;
                break;
            case DESTBLEND:
                v = rs.destblend;
                break;
            case ZTESTENABLE:
                v = rs.ztest;
                break;
            case ZWRITEENABLE:
                v = rs.zwrite;
                break;
            case FOGENABLE:
                v = rs.fogEnable;
                break;
            case FOGCOLOR:
                v = rs.fogColor;
                break;
            case CULLMODE:
                v = rs.cullmode;
                break;
            case STENCILENABLE:
                v = rs.stencilenable;
                break;
            case STENCILFAIL:
                v = rs.stencilfail;
                break;
            case STENCILZFAIL:
                v = rs.stencilzfail;
                break;
            case STENCILPASS:
                v = rs.stencilpass;
                break;
            case STENCILFUNCTION:
                v = rs.stencilfunc;
                break;
            case STENCILFUNCTIONREF:
                v = rs.stencilref;
                break;
            case STENCILFUNCTIONMASK:
                v = rs.stencilmask;
                break;
            case STENCILFUNCTIONWRITEMASK:
                v = rs.stencilwritemask;
                break;
            case ALPHATESTFUNC:
                v = rs.alphafunc;
                break;
            case ALPHATESTREF:
                v = rs.alpharef;
                break;
            case GSALPHATEST:
                v = rs.gsalpha;
                break;
            case GSALPHATESTREF:
                v = rs.gsalpharef;
                break;
            case COLORWRITEMASK:
                v = rs.colorwritemask;
                break;
            }
            return (void*)(uintptr)v;
        }

        // --------------------------------------------------------------- Im2D

        static void im2DRenderPrimitive(PrimitiveType primType, void* vertices, int32 numVertices)
        {
            RecordScope timed;
            uint32 prim;
            if (drawsDiscarded() || numVertices <= 0 || !primFor(primType, &prim))
            {
                return;
            }
            const int32 resetA = alphaResetOf(prim, (const Im2DVertex*)vertices, numVertices);
            if (resetA >= 0 && alphaResetSkip((uint32)resetA))
            {
                return;
            }
            Im2DVertex* v;
            float wFactor;
            {
                ProfScope prof(PROF_IM);
                v = (Im2DVertex*)ringAlloc(numVertices * sizeof(Im2DVertex));
                if (v == nil)
                {
                    return;
                }
                memcpy(v, vertices, numVertices * sizeof(Im2DVertex));
                wFactor = normaliseIm2DW(v, numVertices);
            }
            setIm2DActive(1);
            if (flushState())
            {
                ProfScope prof(PROF_C3D);
                useProgram(PROG_IM2D);
                useFormat(FORMAT_IM2D, v, sizeof(Im2DVertex));
                setIm2DMatrix();
                setRasterUVMatrix();
                setIm2DFog(wFactor);
                drawArrays(prim, 0, numVertices);
                if (resetA >= 0)
                {
                    alphaResetDrawn((uint32)resetA);
                }
            }
            setIm2DActive(0);
        }

        static void im2DRenderIndexedPrimitive(PrimitiveType primType, void* vertices,
                                               int32 numVertices, void* indices, int32 numIndices)
        {
            RecordScope timed;
            uint32 prim;
            if (drawsDiscarded() || numVertices <= 0 || numIndices <= 0 ||
                !primFor(primType, &prim))
            {
                return;
            }
            // One block: a ring split between two would leave the vertices
            // behind.
            const uint32 vbytes = (numVertices * sizeof(Im2DVertex) + 15) & ~15u;
            Im2DVertex* v;
            uint16* idx;
            float wFactor;
            {
                ProfScope prof(PROF_IM);
                uint8* block = (uint8*)ringAlloc(vbytes + numIndices * sizeof(uint16));
                if (block == nil)
                {
                    return;
                }
                v = (Im2DVertex*)block;
                idx = (uint16*)(block + vbytes);
                memcpy(v, vertices, numVertices * sizeof(Im2DVertex));
                wFactor = normaliseIm2DW(v, numVertices);
                memcpy(idx, indices, numIndices * sizeof(uint16));
            }
            setIm2DActive(1);
            if (flushState())
            {
                ProfScope prof(PROF_C3D);
                useProgram(PROG_IM2D);
                useFormat(FORMAT_IM2D, v, sizeof(Im2DVertex));
                setIm2DMatrix();
                setRasterUVMatrix();
                setIm2DFog(wFactor);
                drawElements(prim, numIndices, idx);
            }
            setIm2DActive(0);
        }

        static Im2DVertex sTmpPrim[3];

        static void im2DRenderLine(void* vertices, int32 numVertices, int32 vert1, int32 vert2)
        {
            (void)vertices;
            (void)numVertices;
            (void)vert1;
            (void)vert2;
            logOnce("lines", "a line or point list was not drawn (the PICA has neither)");
        }

        static void im2DRenderTriangle(void* vertices, int32 numVertices, int32 vert1, int32 vert2,
                                       int32 vert3)
        {
            (void)numVertices;
            Im2DVertex* verts = (Im2DVertex*)vertices;
            sTmpPrim[0] = verts[vert1];
            sTmpPrim[1] = verts[vert2];
            sTmpPrim[2] = verts[vert3];
            im2DRenderPrimitive(PRIMTYPETRILIST, sTmpPrim, 3);
        }

        // --------------------------------------------------------------- Im3D

        static V3d im3DBoxMiddle(const Im3DVertex* v, int32 numVertices)
        {
            V3d lo = { v[0].x, v[0].y, v[0].z };
            V3d hi = lo;
            for (int32 i = 1; i < numVertices; i++)
            {
                lo.x = v[i].x < lo.x ? v[i].x : lo.x;
                lo.y = v[i].y < lo.y ? v[i].y : lo.y;
                lo.z = v[i].z < lo.z ? v[i].z : lo.z;
                hi.x = v[i].x > hi.x ? v[i].x : hi.x;
                hi.y = v[i].y > hi.y ? v[i].y : hi.y;
                hi.z = v[i].z > hi.z ? v[i].z : hi.z;
            }
            V3d c = { 0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z) };
            // A vertex that is not a number would take every other one with it.
            if (!(fabsf(c.x) < 1e30f && fabsf(c.y) < 1e30f && fabsf(c.z) < 1e30f))
            {
                c.x = c.y = c.z = 0.0f;
            }
            return c;
        }

        static void im3DTransform(void* vertices, int32 numVertices, Matrix* world, uint32 flags)
        {
            RecordScope timed;
            sIm3DVerts = nil;
            sIm3DCount = 0;
            ringPin(nil, 0);
            if (drawsDiscarded() || numVertices <= 0)
            {
                return;
            }
            ProfScope prof(PROF_IM);
            Im3DVertex* v = (Im3DVertex*)ringAlloc(numVertices * sizeof(Im3DVertex));
            if (v == nil)
            {
                return;
            }
            memcpy(v, vertices, numVertices * sizeof(Im3DVertex));

            // Rebased on the middle of their box, as the models are on their
            // centre (c3dpipe.cpp): most of the game's Im3D comes in world
            // coordinates with no matrix (the shadows, particles, lightning,
            // lasers, streaks), hundreds of units out, and the PICA's 24-bit
            // floats would take the camera's translation off them in the
            // shader, two large numbers cancelling with 16 bits of mantissa
            // (a step of about 0.004 at 500 units: xShadowSimple lifts its
            // blob 0.02 off the ground). The matrix takes the centre back on
            // the CPU, in 32 bits.
            // World coordinates under the screen's camera are rebased on the
            // camera instead, one origin for the whole frame: two shadows
            // close together draw the same floor triangles, the later one only
            // where it is no farther than the first (the depth writes stay on,
            // as on the GameCube), and on two centres the 24-bit maths gave
            // the same floor two depths, leaving holes in the second shadow.
            const V3d c =
                world == nil && sScreenCamera ? sCamPos : im3DBoxMiddle(v, numVertices);
            for (int32 i = 0; i < numVertices; i++)
            {
                v[i].x -= c.x;
                v[i].y -= c.y;
                v[i].z -= c.z;
            }
            sIm3DVerts = v;
            sIm3DCount = numVertices;
            ringPin(v, numVertices * sizeof(Im3DVertex));

            static Matrix ident;
            if (world == nil)
            {
                ident.setIdentity();
                world = &ident;
            }
            setWorldMatrix(world, &c);
            if (flags & im3d::LIGHTING)
            {
                logOnce("im3dlight", "lit Im3D is drawn unlit");
            }
            if ((flags & im3d::VERTEXUV) == 0)
            {
                SetRenderStatePtr(TEXTURERASTER, nil);
            }
        }

        // An Im3D draw under a parallel camera with a depth window keeps the
        // camera's whole range (setWorldMatrix), which near the camera is
        // deeper than every windowed model's depth, so with the depth test on
        // it would always cover them. None is seen; logged so one would show.
        static void noteIm3DUnderWindow(void)
        {
            if (sHasWindow && rs.ztest)
            {
                logOnce("hudzim3d", "hudz: an Im3D draw with the depth test on under a parallel "
                                    "camera; drawn over the camera's whole range");
            }
        }

        static void im3DRenderPrimitive(PrimitiveType primType)
        {
            RecordScope timed;
            uint32 prim;
            if (sIm3DVerts == nil || drawsDiscarded() || !primFor(primType, &prim))
            {
                return;
            }
            noteIm3DUnderWindow();
            if (flushState())
            {
                ProfScope prof(PROF_C3D);
                useProgram(PROG_IM3D);
                useFormat(FORMAT_IM3D, sIm3DVerts, sizeof(Im3DVertex));
                setRasterUVMatrix();
                drawArrays(prim, 0, sIm3DCount);
            }
        }

        static void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices,
                                               int32 numIndices)
        {
            RecordScope timed;
            uint32 prim;
            if (sIm3DVerts == nil || drawsDiscarded() || numIndices <= 0 ||
                !primFor(primType, &prim))
            {
                return;
            }
            uint16* idx;
            {
                ProfScope prof(PROF_IM);
                idx = (uint16*)ringAlloc(numIndices * sizeof(uint16));
                sIm3DVerts = (Im3DVertex*)ringPinned(); // a ring split moves them
                if (idx == nil || sIm3DVerts == nil)
                {
                    return;
                }
                memcpy(idx, indices, numIndices * sizeof(uint16));
            }
            noteIm3DUnderWindow();
            if (flushState())
            {
                ProfScope prof(PROF_C3D);
                useProgram(PROG_IM3D);
                useFormat(FORMAT_IM3D, sIm3DVerts, sizeof(Im3DVertex));
                setRasterUVMatrix();
                drawElements(prim, numIndices, idx);
            }
        }

        static void im3DEnd(void)
        {
            sIm3DVerts = nil;
            sIm3DCount = 0;
            ringPin(nil, 0);
        }

        // ------------------------------------------------------------- system

        static int deviceSystem(DeviceReq req, void* arg, int32 n)
        {
            switch (req)
            {
            case DEVICEOPEN:
                defaultStates();
                return submitOpen((const OpenParams*)arg);
            case DEVICECLOSE:
                submitClose();
                return 1;
            case DEVICEINIT:
            case DEVICETERM:
            case DEVICEFINALIZE:
                return 1;
            case DEVICEGETNUMSUBSYSTEMS:
                return 1;
            case DEVICEGETCURRENTSUBSYSTEM:
                return 0;
            case DEVICESETSUBSYSTEM:
                return n == 0;
            case DEVICEGETSUBSSYSTEMINFO:
                if (n != 0 || arg == nil)
                {
                    return 0;
                }
                strcpy(((SubSystemInfo*)arg)->name, "3DS (citro3d)");
                return 1;
            case DEVICEGETNUMVIDEOMODES:
                return 1;
            case DEVICEGETCURRENTVIDEOMODE:
                return 0;
            case DEVICESETVIDEOMODE:
                return n == 0;
            case DEVICEGETVIDEOMODEINFO:
            {
                if (n != 0 || arg == nil)
                {
                    return 0;
                }
                VideoMode* vm = (VideoMode*)arg;
                vm->width = SCREEN_W;
                vm->height = SCREEN_H;
                vm->depth = 32;
                vm->flags = 0;
                return 1;
            }
            case DEVICEGETMAXMULTISAMPLINGLEVELS:
            case DEVICEGETMULTISAMPLINGLEVELS:
                return 1;
            case DEVICESETMULTISAMPLINGLEVELS:
                return n <= 1;
            default:
                return 1;
            }
        }

        int deviceOpen(void)
        {
            return submitIsOpen();
        }

        int pacesFrames(void)
        {
            return submitIsOpen();
        }

        void getStats(Stats* out)
        {
            memset(out, 0, sizeof(*out));
            getSubmitStats(out);
        }

        Device renderdevice = { 0.0f,
                                1.0f,
                                c3d::beginUpdate,
                                c3d::endUpdate,
                                c3d::clearCamera,
                                c3d::showRaster,
                                c3d::rasterRenderFast,
                                c3d::setRenderState,
                                c3d::getRenderState,
                                c3d::im2DRenderLine,
                                c3d::im2DRenderTriangle,
                                c3d::im2DRenderPrimitive,
                                c3d::im2DRenderIndexedPrimitive,
                                c3d::im3DTransform,
                                c3d::im3DRenderPrimitive,
                                c3d::im3DRenderIndexedPrimitive,
                                c3d::im3DEnd,
                                c3d::deviceSystem };
    } // namespace c3d
} // namespace rw
