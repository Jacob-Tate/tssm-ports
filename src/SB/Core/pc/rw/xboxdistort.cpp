// The Sonic Wave Guitar's screen distortion as a PC pass. What the Xbox does
// and how this follows it are in iXboxDistort.h; the steps it shares with the
// other passes are in screenpass.h.

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
#include "iXboxDistort.h"
#include "screenpass.h"

#include <chrono>
#include <math.h>
#include <stdio.h>

#if defined(RW_D3D_ANY) || defined(RW_GL3)

#ifdef RW_D3D_ANY
#ifdef RW_D3D9
namespace sm2
{
#include "shaders/xboxdistort_PS.h"
} // namespace sm2
#endif
#ifdef RW_D3D11
namespace sm4
{
#include "shaders11/xboxdistort_PS.h"
} // namespace sm4
#endif
#endif

#ifdef RW_GL3
namespace
{
#include "shadersgl/xboxdistort_gl.inc"
} // namespace
#endif

// The offset at an amount of 1, in pixels of the Xbox's 640x480 frame
// (0x28d428, times the amount, in 0x1afb60).
static const F32 kDisplacePixels = 15.0f;
static const F32 kReferenceWidth = 640.0f;
static const F32 kReferenceHeight = 480.0f;

// Radians per millisecond of KeTickCount (0x2a765c).
static const double kRadiansPerMs = 0.002;

static RwRaster* sScreen;
static void* sShader;
static bool sShaderTried;
static bool sFailed;
static bool sReported;

#ifdef RW_GL3
static rw::int32 sDisplaceUniform = -1;
#endif

static void distortFail(const char* what)
{
    sFailed = true;
    printf("tssm: the guitar's distortion is off -- %s\n", what);
    fflush(stdout);
}

// Whether the renderer has a pixel shader for the pass. Not Direct3D 9's fixed
// path, and not Vulkan, which has no SPIR-V blob for it yet
// (shaders/make_shaders.cmd); with one, Vulkan takes the Direct3D branches
// below, as it does in xboxglow.cpp.
static bool rendererHasShader()
{
#ifdef RW_D3D9
    if (iBackendIsD3D9() && rw::d3d::getFixedFunction())
    {
        return false;
    }
#endif
    return iBackendIsD3D() || iBackendIsGL3();
}

static bool makeShader()
{
    if (!sShaderTried)
    {
        sShaderTried = true;
#ifdef RW_D3D_ANY
        if (iBackendIsD3D())
        {
            sShader = rw::d3d::createPixelShader(RWD3D_SHADER(xboxdistort_PS));
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            const char* vs[] = { rw::gl3::shaderDecl, rw::gl3::header_vert_src,
                                 rw::gl3::im2d_vert_src, NULL };
            const char* fs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src,
                                 xboxdistort_frag_src, NULL };
            sShader = (void*)rw::gl3::Shader::create(vs, fs);
        }
#endif
    }
    return sShader != NULL;
}

static void bindShader(void* shader)
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D())
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

// The swirl map on stage 1. Stage 0 is the render state's; nothing in the 2D
// path touches stage 1.
static void setMap(RwTexture* map)
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D())
    {
        rw::d3d::setTexture(1, reinterpret_cast<rw::Texture*>(map));
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        rw::gl3::setTexture(1, reinterpret_cast<rw::Texture*>(map));
    }
#endif
}

static void uploadDisplace(F32* displace)
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D())
    {
        // c1: librw keeps c0 for the fog colour.
        rw::d3d::setPixelShaderConstantF(1, displace, 1);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3() && sDisplaceUniform >= 0)
    {
        rw::gl3::setUniform(sDisplaceUniform, displace);
    }
#endif
}

bool iXboxDistortDraw(RwCamera* cam, RwTexture* map, F32 amount, F32 ms)
{
    if (sFailed || cam == NULL || map == NULL || map->raster == NULL || amount <= 0.0f)
    {
        return false;
    }
    if (!iScreenPassReady() || !rendererHasShader())
    {
        return false;
    }
    if (!makeShader())
    {
        distortFail("its pixel shader would not compile");
        return false;
    }

    iScreenPassState saved;
    iScreenPassBegin(&saved);

    // The copy reads the finished surface, so the scene is closed round it.
    RwCameraEndUpdate(cam);
    const bool captured = iScreenPassCapture(&sScreen);
    RwCameraBeginUpdate(cam);
    if (!captured)
    {
        distortFail("the frame could not be copied into a texture");
        iScreenPassEnd(&saved);
        return false;
    }

    if (!sReported)
    {
        sReported = true;
        printf("tssm: guitar distortion: %dx%d frame, swirl map %dx%d\n", (int)sScreen->width,
               (int)sScreen->height, (int)map->raster->width, (int)map->raster->height);
        fflush(stdout);
    }

    // The angle, wrapped to one turn before it becomes a float: a millisecond
    // count is past a float's whole numbers within hours.
    double now = (double)ms;
    if (ms < 0.0f)
    {
        using namespace std::chrono;
        now = (double)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
    const F32 t = (F32)fmod(now * kRadiansPerMs, 2.0 * 3.14159265358979323846);
    const F32 s = sinf(t) * kDisplacePixels * amount;
    const F32 c = cosf(t) * kDisplacePixels * amount;

    // u = x + r (A sin t) + g (A cos t), v = y + r (A cos t) - g (A sin t), as
    // shares of the picture: of the Xbox's 640x480, whatever the size here.
    F32 displace[4] = { s / kReferenceWidth, c / kReferenceHeight, c / kReferenceWidth,
                        -s / kReferenceHeight };
    uploadDisplace(displace);

    RwRaster* frame = RwCameraGetRaster(cam);
    const F32 w = frame != NULL ? (F32)frame->width : (F32)sScreen->width;
    const F32 h = frame != NULL ? (F32)frame->height : (F32)sScreen->height;

    // ONE/ZERO, linear, clamped (iScreenPassBegin), every channel written: the
    // pass replaces the picture.
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    setMap(map);
    bindShader(sShader);
    iScreenPassQuad(sScreen, 0.0f, 0.0f, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFF, 0xFF, 0xFF, 0xFF);
    bindShader(NULL);
    setMap(NULL);

    iScreenPassEnd(&saved);
    return true;
}

const char* iXboxDistortRendererStatus()
{
#ifdef RW_D3D9
    if (iBackendIsD3D9() && rw::d3d::getFixedFunction())
    {
        // iExtrasResolve's own answer; here for a caller that asks anyway.
        return "Direct3D 9's fixed path has no pixel shader for the swirl: none is drawn.";
    }
#endif
    if (iBackendIsVulkan())
    {
        return "The Vulkan renderer has no shader for the swirl yet: none is drawn.";
    }
    if (!rendererHasShader())
    {
        return "This renderer cannot draw the swirl: none is drawn.";
    }
    if (sFailed)
    {
        return "The swirl could not be drawn (the log says why): none is drawn.";
    }
    return NULL;
}

#else

// LIBRW_PLATFORM=NULL: no device, no pass.
bool iXboxDistortDraw(RwCamera*, RwTexture*, F32, F32)
{
    return false;
}

const char* iXboxDistortRendererStatus()
{
    return "This renderer cannot draw the swirl: none is drawn.";
}

#endif

void iXboxDistortRegisterShaderUniforms(void)
{
#if defined(RW_GL3)
    sDisplaceUniform = rw::gl3::registerUniform("u_xboxDistort", rw::gl3::UNIFORM_VEC4);
#endif
}
