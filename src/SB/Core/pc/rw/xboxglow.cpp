// The Xbox release's glow as a PC pass. What the Xbox does and how this follows
// it are in iXboxGlow.h; the steps it shares with the GameCube glow are in
// screenpass.h.

#include <rwcore.h>

#if defined(RW_D3D9) || defined(RW_D3D11)
#include <windows.h>
#define WITH_D3D
#endif
#ifdef RW_D3D9
#include <d3d9.h>
#endif
#ifdef RW_D3D11
#include <d3d11.h>
#endif

#include "rw.h"

#ifdef RW_D3D_ANY
#include "src/d3d/rwd3dimpl.h"
#endif

#include "backend.h"
#include "iGpuOpt.h"
#include "iHDRGlow.h"
#include "iXboxGlow.h"
#include "screenpass.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 0x2cc368 (7 passes), 0x2cc36c (0.05) and the doubling threshold the title
// screen's glow leaves behind (0x12f220: 5, 7, 0.05); the menus' own, set by
// 0x12f190 before their pass (6, 8, 0.02).
const iXboxGlowChain kXboxGlowScene = { 7, 0.05f, 5 };
const iXboxGlowChain kXboxGlowMenu = { 8, 0.02f, 6 };

// Picked against TCRF's capture of the Xbox's options screen (docs/EXTRAS.md):
// no glow round the selected label there, where 0.5 already adds 12 to 27
// levels. The console's leftover stack value draws nothing visible.
static const F32 kMenuStrength = 0.0f;

F32 iXboxGlowMenuStrength()
{
    static F32 strength = -1.0f;
    if (strength < 0.0f)
    {
        strength = kMenuStrength;
        const char* env = getenv("TSSM_TEST_MENU_GLOW");
        if (env != NULL && env[0] != '\0')
        {
            const F32 v = (F32)atof(env);
            if (v >= 0.0f && v <= 8.0f)
            {
                strength = v;
            }
        }
    }
    return strength;
}

#if defined(RW_D3D_ANY) || defined(RW_GL3)

// The BfBB glow's blur shader (glow.cpp, shaders/glow_blur_PS.hlsl): a sum of
// four taps, each with its own weight and offset. Compiled here a second time
// under this file's own handle; glow.cpp's stays unmade (iGlow is off).
#ifdef RW_D3D_ANY
#ifdef RW_D3D9
namespace sm2
{
#include "shaders/glow_blur_PS.h"
} // namespace sm2
#endif
#ifdef RW_D3D11
namespace sm4
{
#include "shaders11/glow_blur_PS.h"
} // namespace sm4
#endif
#ifdef RW_VULKAN
namespace spv
{
#include "shadersvk/glow_blur_PS.h"
} // namespace spv
#endif
#endif

#ifdef RW_GL3
namespace
{
#include "shadersgl/glow_blur_gl.inc"
} // namespace
#endif

// The Xbox's targets: 640 x 0.38 by 480 x 0.38 (0x21a5e0, 0x21a610), the
// blur loop starting from the float extents and the targets from their whole
// part (243x182).
static const F32 kTargetHeight = 480.0f * 0.38f;

// Above this the frame is halved before the copy.
static const RwInt32 kCopyMaxHeight = 480;
#define XBOXGLOW_MAX_HALVINGS 6

// The blur's tap distance per pass (0x2cc370, 0x2cc374), in texels.
static const F32 kTapStep = 0.2f;

// Each tap's weight, 0x40 in the combiner constants of 0x2cc698; the doubling
// shader (0x2cc788) scales the sum by 2.
static const U8 kTapWeight = 0x40;

enum
{
    VIEW_NONE,
    VIEW_MASK,
    VIEW_GLOW,
    VIEW_BASE
};

static RwRaster* sScreen;
static iScreenPassTarget sHalf[XBOXGLOW_MAX_HALVINGS];
static S32 sHalvings;
static iScreenPassTarget sTarget[2];
static void* sBlurShader;
static bool sShaderTried;
static S32 sFailed;
static S32 sReported;
static S32 sView = -1;

#ifdef RW_GL3
static rw::int32 sWeightsUniform = -1;
static rw::int32 sOffs01Uniform = -1;
static rw::int32 sOffs23Uniform = -1;
#endif

static void glowFail(const char* what)
{
    sFailed = 1;
    printf("tssm: the Xbox glow is off -- %s\n", what);
    fflush(stdout);
}

// Whether the four taps go through the blur shader. Not on Direct3D 9's fixed
// path, which has no pixel shader of the port's own.
static bool useShader()
{
#ifdef RW_D3D9
    if (iBackendIsD3D9() && rw::d3d::getFixedFunction())
    {
        return false;
    }
#endif

    if (!sShaderTried)
    {
        sShaderTried = true;
#ifdef RW_D3D_ANY
        if (iBackendIsD3D() || iBackendIsVulkan())
        {
            sBlurShader = rw::d3d::createPixelShader(RWD3D_SHADER(glow_blur_PS));
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            // The uniforms are glow.cpp's, registered at startup
            // (iGlowRegisterShaderUniforms); registering a name again returns
            // the one already there.
            sWeightsUniform = rw::gl3::registerUniform("u_glowWeights", rw::gl3::UNIFORM_VEC4);
            sOffs01Uniform = rw::gl3::registerUniform("u_glowOffs01", rw::gl3::UNIFORM_VEC4);
            sOffs23Uniform = rw::gl3::registerUniform("u_glowOffs23", rw::gl3::UNIFORM_VEC4);

            const char* vs[] = { rw::gl3::shaderDecl, rw::gl3::header_vert_src,
                                 rw::gl3::im2d_vert_src, NULL };
            const char* fs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src,
                                 glow_blur_frag_src, NULL };
            sBlurShader = (void*)rw::gl3::Shader::create(vs, fs);
        }
#endif
        if (sBlurShader == NULL)
        {
            printf("tssm: Xbox glow: the blur shader would not compile; drawing its taps "
                   "one at a time\n");
            fflush(stdout);
        }
    }
    return sBlurShader != NULL;
}

static void bindShader(void* shader)
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D() || iBackendIsVulkan())
    {
        rw::d3d::im2dOverridePS = shader;
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        rw::gl3::im2dOverrideShader = (rw::gl3::Shader*)shader;
    }
#endif
}

static void uploadTaps(F32* weights, F32* offs01, F32* offs23)
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D() || iBackendIsVulkan())
    {
        // c1 to c3: librw keeps c0 for the fog colour.
        rw::d3d::setPixelShaderConstantF(1, weights, 1);
        rw::d3d::setPixelShaderConstantF(2, offs01, 1);
        rw::d3d::setPixelShaderConstantF(3, offs23, 1);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3() && sWeightsUniform >= 0)
    {
        rw::gl3::setUniform(sWeightsUniform, weights);
        rw::gl3::setUniform(sOffs01Uniform, offs01);
        rw::gl3::setUniform(sOffs23Uniform, offs23);
    }
#endif
}

// A target cleared to a colour (iScreenPassClear clears to transparent black).
static void clearTarget(iScreenPassTarget* t, U8 a)
{
    U32 mask = rw::GetRenderState(rw::COLORWRITEMASK);
    rw::SetRenderState(rw::COLORWRITEMASK, ISCREENPASS_WRITE_ALL);

    RwRGBA clear = { 0, 0, 0, a };
    RwCameraClear(t->camera, &clear, rwCAMERACLEARIMAGE);

    rw::SetRenderState(rw::COLORWRITEMASK, mask);
}

// The halving chain and the two targets, sized from the picture.
static bool makeTargets(RwInt32 w, RwInt32 h, F32* fw, F32* fh)
{
    S32 n = 0;
    RwInt32 hw = w;
    RwInt32 hh = h;
    while (hh > kCopyMaxHeight && n < XBOXGLOW_MAX_HALVINGS)
    {
        hw = hw / 2 > 0 ? hw / 2 : 1;
        hh = hh / 2 > 0 ? hh / 2 : 1;
        if (!iScreenPassMakeTarget(&sHalf[n], hw, hh))
        {
            return false;
        }
        n++;
    }
    for (S32 i = n; i < XBOXGLOW_MAX_HALVINGS; i++)
    {
        iScreenPassDestroyTarget(&sHalf[i]);
    }
    sHalvings = n;

    *fh = kTargetHeight;
    *fw = kTargetHeight * (F32)w / (F32)h;
    RwInt32 tw = (RwInt32)*fw;
    RwInt32 th = (RwInt32)*fh;
    if (tw < 1)
    {
        tw = 1;
    }
    return iScreenPassMakeTarget(&sTarget[0], tw, th) && iScreenPassMakeTarget(&sTarget[1], tw, th);
}

// One blur pass: `src`'s rectangle (sw x sh texels from its top left) drawn
// into `dst`'s (dw x dh pixels), each pixel the sum of four diagonal taps `o`
// texels away, each times `weight`/255.
static void blurPass(iScreenPassTarget* dst, iScreenPassTarget* src, F32 sw, F32 sh, F32 dw, F32 dh,
                     F32 o, U8 weight, bool shader)
{
    const F32 tw = (F32)src->width;
    const F32 th = (F32)src->height;
    const F32 u1 = sw / tw;
    const F32 v1 = sh / th;
    const F32 du = o / tw;
    const F32 dv = o / th;

    // The taps of the vertex shader at 0x2cc610: (-o, +o), (+o, +o), (+o, -o)
    // and (-o, -o).
    const F32 tap[4][2] = { { -du, dv }, { du, dv }, { du, -dv }, { -du, -dv } };

    RwCameraBeginUpdate(dst->camera);
    if (shader)
    {
        F32 w = weight / 255.0f;
        F32 weights[4] = { w, w, w, w };
        F32 offs01[4] = { tap[0][0], tap[0][1], tap[1][0], tap[1][1] };
        F32 offs23[4] = { tap[2][0], tap[2][1], tap[3][0], tap[3][1] };
        uploadTaps(weights, offs01, offs23);
        bindShader(sBlurShader);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iScreenPassQuad(src->raster, 0.0f, 0.0f, dw, dh, 0.0f, 0.0f, u1, v1, 0xFF, 0xFF, 0xFF,
                        0xFF);
        bindShader(NULL);
    }
    else
    {
        for (S32 k = 0; k < 4; k++)
        {
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)(k == 0 ? rwBLENDZERO : rwBLENDONE));
            iScreenPassQuad(src->raster, 0.0f, 0.0f, dw, dh, tap[k][0], tap[k][1], u1 + tap[k][0],
                            v1 + tap[k][1], weight, weight, weight, weight);
        }
    }
    RwCameraEndUpdate(dst->camera);
}

void iXboxGlowRender(RwCamera* cam, S32 glow, F32 strength, const iXboxGlowChain& chain)
{
    if (!iHDRGlowEnabled() || sFailed || cam == NULL || !iScreenPassReady())
    {
        return;
    }

    if (sView < 0)
    {
        const char* view = getenv("TSSM_HDR_VIEW");
        sView = VIEW_NONE;
        if (view != NULL && strcmp(view, "mask") == 0)
        {
            sView = VIEW_MASK;
        }
        else if (view != NULL && strcmp(view, "glow") == 0)
        {
            sView = VIEW_GLOW;
        }
        else if (view != NULL && strcmp(view, "base") == 0)
        {
            sView = VIEW_BASE;
        }
    }

    if (glow < 1)
    {
        glow = 1;
    }
    if (glow > 254)
    {
        glow = 254;
    }

    const bool shader = useShader();

    iScreenPassState saved;
    iScreenPassBegin(&saved);
    RwCameraEndUpdate(cam);

    RwRaster* picture = NULL;
    if (sView == VIEW_MASK ? !iScreenPassCapture(&sScreen) :
                             !iScreenPassReadFrame(&sScreen, &picture))
    {
        glowFail("the frame could not be copied into a texture");
        RwCameraBeginUpdate(cam);
        iScreenPassEnd(&saved);
        return;
    }
    if (picture == NULL)
    {
        picture = sScreen;
    }

    F32 fw = 0.0f;
    F32 fh = 0.0f;
    if (!makeTargets(picture->width, picture->height, &fw, &fh))
    {
        glowFail("its render targets could not be made");
        RwCameraBeginUpdate(cam);
        iScreenPassEnd(&saved);
        return;
    }

    if (!sReported)
    {
        sReported = 1;
        printf("tssm: Xbox glow: %dx%d", (int)picture->width, (int)picture->height);
        for (S32 i = 0; i < sHalvings; i++)
        {
            printf(" -> %dx%d", (int)sHalf[i].width, (int)sHalf[i].height);
        }
        printf(" -> 2 x %dx%d, taps %s\n", (int)sTarget[0].width, (int)sTarget[0].height,
               shader ? "in one shader" : "as four draws");
        fflush(stdout);
    }

    // 0. Halve a large frame first.
    RwRaster* from = picture;
    for (S32 i = 0; i < sHalvings; i++)
    {
        if (iGpuOptOn(IGPUOPT_INVALIDATE))
        {
            iScreenPassClear(&sHalf[i]);
        }
        RwCameraBeginUpdate(sHalf[i].camera);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iScreenPassQuad(from, 0.0f, 0.0f, (F32)sHalf[i].width, (F32)sHalf[i].height, 0.0f, 0.0f,
                        1.0f, 1.0f, 0xFF, 0xFF, 0xFF, 0xFF);
        RwCameraEndUpdate(sHalf[i].camera);
        from = sHalf[i].raster;
    }

    // 1. Both targets to (0, 0, 0, glow + 1) (0x1aec20).
    clearTarget(&sTarget[0], (U8)(glow + 1));
    clearTarget(&sTarget[1], (U8)(glow + 1));

    // 2. The copy: alpha > glow, ONE/ZERO. librw's tests are >= and <, so
    //    > glow is >= glow + 1.
    RwCameraBeginUpdate(sTarget[0].camera);
    rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
    rw::SetRenderState(rw::ALPHATESTREF, (rw::uint32)(glow + 1));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    iScreenPassQuad(from, 0.0f, 0.0f, (F32)sTarget[0].width, (F32)sTarget[0].height, 0.0f, 0.0f,
                    1.0f, 1.0f, 0xFF, 0xFF, 0xFF, 0xFF);
    rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
    rw::SetRenderState(rw::ALPHATESTREF, 0);
    RwCameraEndUpdate(sTarget[0].camera);

    // 3. The blur, in the Xbox's float arithmetic: the rectangle read shrinks
    //    by shrink x i of itself, the one drawn by shrink x (i + 1).
    F32 sw = fw;
    F32 sh = fh;
    F32 dw = fw;
    F32 dh = fh;
    S32 src = 0;
    for (S32 i = 0; i < chain.passes; i++)
    {
        const F32 step = chain.shrink * (F32)i;
        sw = sw - step * sw;
        sh = sh - step * sh;
        dw = dw - (F32)(i + 1) * chain.shrink * dw;
        dh = dh - (F32)(i + 1) * chain.shrink * dh;

        const U8 weight = i > chain.doubleAfter ? (U8)(kTapWeight * 2) : kTapWeight;
        blurPass(&sTarget[1 - src], &sTarget[src], sw, sh, dw, dh, kTapStep * (F32)i, weight,
                 shader);
        src = 1 - src;
    }
    iScreenPassTarget* last = &sTarget[src];

    RwCameraBeginUpdate(cam);

    RwRaster* frame = RwCameraGetRaster(cam);
    F32 w = frame != NULL ? (F32)frame->width : (F32)picture->width;
    F32 h = frame != NULL ? (F32)frame->height : (F32)picture->height;

    if (sView == VIEW_MASK)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iScreenPassQuad(NULL, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFF, 0xFF, 0xFF, 0xFF);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCALPHA);
        iScreenPassQuad(sScreen, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFF, 0xFF, 0xFF, 0xFF);
        iScreenPassEnd(&saved);
        return;
    }
    if (sView == VIEW_BASE)
    {
        iScreenPassEnd(&saved);
        return;
    }
    if (sView == VIEW_GLOW)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iScreenPassQuad(NULL, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 0xFF);
    }

    // 4. The last rectangle over the frame, ONE/ONE, times the strength;
    //    colour writes as the game left them, alpha not written.
    iScreenPassSetWriteMask(saved.writeMask & ISCREENPASS_WRITE_RGB);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    const F32 u1 = dw / (F32)last->width;
    const F32 v1 = dh / (F32)last->height;
    F32 left = strength;
    while (left > 0.5f / 255.0f)
    {
        const F32 part = left > 1.0f ? 1.0f : left;
        const U8 c = (U8)(part * 255.0f + 0.5f);
        iScreenPassQuad(last->raster, 0.0f, 0.0f, w, h, 0.0f, 0.0f, u1, v1, c, c, c, 0xFF);
        left -= part;
    }

    iScreenPassEnd(&saved);
}

#else

// LIBRW_PLATFORM=NULL: no device, no glow.
void iXboxGlowRender(RwCamera*, S32, F32, const iXboxGlowChain&)
{
}

#endif
