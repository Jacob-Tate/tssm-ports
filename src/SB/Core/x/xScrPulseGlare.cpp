#include "xScrPulseGlare.h"

#include "xRand.h"
#include "xMathInlines.h"
#include "xVec3Inlines.h"

#include <rwcore.h>

#define zMAX_PULSE_GLARES 128

namespace
{
    struct xPulse
    {
        F32 max_amp;
        F32 elapsed;
        F32 period;
        F32 freq;
        F32 noise;
    };

    struct xPulseGlare
    {
        struct config
        {
            F32 vis_angle;
            F32 amp_freq;
            F32 rot_freg;
        };

        config cfg;
        void (*cb_update_size)(xPulse&, F32);
        void (*cb_update_rot)(xPulse&, F32);
        xVec3 pos;
        xVec3 dir;
        F32 scale_dir;
        F32 elapsed;
        F32 lifetime;
        F32 size;
        F32 angle;
        xPulse p;
        S32 flags;
        RwRaster* raster;
        RwRGBAReal col;
    };

    inline void init_pulse(xPulse& p)
    {
        p.max_amp = 200.0f;
        p.elapsed = 0.0f;
        p.period = 2.0f;
        p.freq = 20.0f;
        p.noise = 0.0f;
    }

    void xPulseFn(xPulse& p, F32)
    {
        p.noise = (S32)xrand_GenRandInt32() % 100 / 4000.0f;
    }

    void init_pulse_glare(xPulseGlare& glare)
    {
        glare.cfg.amp_freq = 30.0f;
        glare.cfg.rot_freg = 1.0f;
        glare.cfg.vis_angle = std::cosf(90.0f);

        glare.lifetime = 0.1f;
        glare.elapsed = 0.0f;
        glare.raster = NULL;
        glare.cb_update_size = xPulseFn;
        glare.cb_update_rot = NULL;
        glare.flags = 0;
        glare.size = 0.3f;
        glare.col.red = 1.0f;
        glare.col.green = 1.0f;
        glare.col.blue = 1.0f;
        glare.col.alpha = 1.0f;
        glare.scale_dir = 1.0f;
        glare.angle = 0.0f;

        init_pulse(glare.p);
    }

    inline void update_pulse_glare(xPulseGlare& g, F32 dt)
    {
        if (g.flags & 0x2)
        {
            g.elapsed += dt;

            if (g.lifetime < g.elapsed)
            {
                g.elapsed = 0.0f;
                g.flags &= ~0x2;
                g.flags &= ~0x1;
            }
        }

        if (g.cb_update_size)
        {
            g.cb_update_size(g.p, dt);
        }

        g.angle += 0.2f;
        g.angle = (S32)g.angle % 360;
    }

    void render_pulse_glare(const xMat4x3& mat, xPulseGlare* g);
} // namespace

struct xPulseGlareMgr
{
    xPulseGlare glares[zMAX_PULSE_GLARES];

    void init()
    {
        for (S32 i = 0; i < zMAX_PULSE_GLARES; i++)
        {
            init_pulse_glare(glares[i]);
        }
    }

    void update(F32 dt)
    {
        for (S32 i = 0; i < zMAX_PULSE_GLARES; i++)
        {
            if (glares[i].flags & 0x1)
            {
                update_pulse_glare(glares[i], dt);
            }
        }
    }

    void render(const xMat4x3& mat)
    {
        for (S32 i = 0; i < zMAX_PULSE_GLARES; i++)
        {
            if (glares[i].flags & 0x1)
            {
                render_pulse_glare(mat, &glares[i]);
            }
        }
    }
};

// glare_mgr is defined before render_pulse_glare: retail's .bss has glare_mgr ahead of
// render_pulse_glare's sStripVert (local statics take their slot when their function is parsed).
namespace
{
    xPulseGlareMgr glare_mgr;

    void render_pulse_glare(const xMat4x3& mat, xPulseGlare* g)
    {
        xVec3 w;
        xVec3 h;
        xVec3 g_pos;
        xVec3 cam_dir;
        F32 dot;
        F32 noise;
        xMat3x3 m;

        h = mat.right;
        w = mat.up;

        g_pos = g->pos;
        if (g->flags & 0x20)
        {
            g_pos += g->dir * g->scale_dir;
        }

        xVec3Normalize(&h, &h);
        xVec3Normalize(&w, &w);

        cam_dir.x = mat.pos.x - g_pos.x;
        cam_dir.y = mat.pos.y - g_pos.y;
        cam_dir.z = mat.pos.z - g_pos.z;

        xVec3Normalize(&g->dir, &g->dir);
        xVec3Normalize(&cam_dir, &cam_dir);

        if (!(g->flags & 0x40))
        {
            xVec3Cross(&h, &w, &g->dir);
            xVec3Normalize(&h, &h);
        }

        dot = g->dir.dot(cam_dir);

        if (g->flags & 0x10)
        {
            dot = 1.0f - dot;
        }

        noise = 0.0f;
        if (g->flags & 0x80)
        {
            noise = g->p.noise;
        }

        if (!(g->flags & 0x4))
        {
            if (dot <= g->cfg.vis_angle)
            {
                return;
            }

            h *= dot * (g->size + noise);
            w *= dot * (g->size + noise);
        }
        else
        {
            h *= g->size + noise;
            w *= g->size + noise;
        }

        if (g->flags & 0x8)
        {
            xMat3x3RotC(&m, mat.at.x, mat.at.y, mat.at.z, 0.3181818f * (0.5f * (45.0f * dot)));
            xMat3x3RMulVec(&h, &m, &h);
            xMat3x3RMulVec(&w, &m, &w);
        }

        static RxObjSpace3DVertex sStripVert[4];

        RwIm3DVertexSetPos(&sStripVert[0], g_pos.x - w.x - h.x, g_pos.y - w.y - h.y,
                           g_pos.z - w.z - h.z);
        RwIm3DVertexSetUV(&sStripVert[0], 0.0f, 0.0f);
        RwIm3DVertexSetRGBA(&sStripVert[0], 255.0f * g->col.red, 255.0f * g->col.green,
                            255.0f * g->col.blue, 255.0f * g->col.alpha);

        RwIm3DVertexSetPos(&sStripVert[1], g_pos.x - w.x + h.x, g_pos.y - w.y + h.y,
                           g_pos.z - w.z + h.z);
        RwIm3DVertexSetUV(&sStripVert[1], 0.0f, 1.0f);
        RwIm3DVertexSetRGBA(&sStripVert[1], 255.0f * g->col.red, 255.0f * g->col.green,
                            255.0f * g->col.blue, 255.0f * g->col.alpha);

        RwIm3DVertexSetPos(&sStripVert[2], g_pos.x + w.x - h.x, g_pos.y + w.y - h.y,
                           g_pos.z + w.z - h.z);
        RwIm3DVertexSetUV(&sStripVert[2], 1.0f, 0.0f);
        RwIm3DVertexSetRGBA(&sStripVert[2], 255.0f * g->col.red, 255.0f * g->col.green,
                            255.0f * g->col.blue, 255.0f * g->col.alpha);

        RwIm3DVertexSetPos(&sStripVert[3], g_pos.x + w.x + h.x, g_pos.y + w.y + h.y,
                           g_pos.z + w.z + h.z);
        RwIm3DVertexSetUV(&sStripVert[3], 1.0f, 1.0f);
        RwIm3DVertexSetRGBA(&sStripVert[3], 255.0f * g->col.red, 255.0f * g->col.green,
                            255.0f * g->col.blue, 255.0f * g->col.alpha);

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g->raster);

        if (RwIm3DTransform(sStripVert, 4, NULL,
                            rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
            RwIm3DEnd();
        }
    }
} // namespace

void xScrFX_PulseGlaresRender(const xMat4x3& mat)
{
    glare_mgr.render(mat);
}

// Not inline: retail interns update_pulse_glare's 0.2f here, between xScrFX_PulseGlaresReset's
// and render_pulse_glare's literals. Inlined into xScrFxUpdate; its own copy is stripped at link.
void xScrFX_PulseGlaresUpdate(F32 dt)
{
    glare_mgr.update(dt);
}

void xScrFX_PulseGlaresReset()
{
    glare_mgr.init();
}
