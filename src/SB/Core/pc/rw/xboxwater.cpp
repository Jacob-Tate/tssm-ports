// The Xbox release's water surface: the renderer's half (iXboxWaterDraw.h).
//
// Two arms, the way distort.cpp and glow.cpp have them: rw::d3d for Direct3D 9's
// shader path and Direct3D 11 (one HLSL source, two blobs), and rw::gl3 for
// OpenGL 3 and OpenGL ES 3. Each keeps one vertex buffer of (i, j) and one strip
// index buffer per grid size and draws a tile with its corner in a constant
// (water_VS.hlsl says why).

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

#include "backend.h"
#include "iGpuOpt.h"
#include "iXboxWaterDraw.h"
#include "iXboxWaterGrid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    bool ForcedFailure()
    {
        const char* v = getenv("TSSM_WATER_FAIL");
        return v != NULL && strstr(v, "shaders") != NULL;
    }
} // namespace

#if defined(RW_D3D_ANY) || defined(RW_GL3)

namespace
{
    // The two grid sizes, in the order iXboxWaterDrawHasGrid looks them up.
    const U32 kGridVerts[iXBOXWATERDRAW_GRIDS] = { 17, 33 };

    S32 GridIndex(U32 vertsPerSide)
    {
        for (U32 g = 0; g < iXBOXWATERDRAW_GRIDS; g++)
        {
            if (kGridVerts[g] == vertsPerSide)
            {
                return (S32)g;
            }
        }

        return -1;
    }

    // The vertices of a grid, (i, j) at i * nv + j: the order the Xbox fills
    // them in (0x1C2310) and the order iXboxWaterStripIndices draws.
    F32* GridVertices(U32 nv)
    {
        F32* v = (F32*)malloc(nv * nv * 2 * sizeof(F32));

        if (v == NULL)
        {
            return NULL;
        }

        for (U32 i = 0; i < nv; i++)
        {
            for (U32 j = 0; j < nv; j++)
            {
                v[2 * (i * nv + j) + 0] = (F32)i;
                v[2 * (i * nv + j) + 1] = (F32)j;
            }
        }

        return v;
    }

    // What open said, kept: it is asked once a scene.
    bool sOpened;
    const char* sOpenWhy;

    S32 sGrid = -1; // the grid of the draw in progress
    iRenderStateSnap sSaved;
} // namespace

#if defined(RW_D3D9) || defined(RW_D3D11)
#ifdef RW_D3D9
namespace sm2
{
#include "shaders/water_PS.h"
#include "shaders/water_VS.h"
} // namespace sm2
#endif
#ifdef RW_D3D11
namespace sm4
{
#include "shaders11/water_PS.h"
#include "shaders11/water_VS.h"
} // namespace sm4
#endif

namespace d3dwater
{
    void* sVS;
    void* sPS;
    void* sDecl;
    void* sVB[iXBOXWATERDRAW_GRIDS];
    void* sIB[iXBOXWATERDRAW_GRIDS];
    U32 sIndices[iXBOXWATERDRAW_GRIDS];

    void* Blob(bool vertex)
    {
#ifdef RW_D3D11
        if (iBackendIsD3D11())
        {
            return vertex ? (void*)sm4::water_VS : (void*)sm4::water_PS;
        }
#endif
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            return vertex ? (void*)sm2::water_VS : (void*)sm2::water_PS;
        }
#endif
        return NULL;
    }

    bool MakeGrid(U32 g)
    {
        const U32 nv = kGridVerts[g];
        const U32 count = iXboxWaterStripIndices(nv, NULL);
        U16* indices = (U16*)malloc(count * sizeof(U16));
        F32* verts = GridVertices(nv);

        if (indices == NULL || verts == NULL)
        {
            free(indices);
            free(verts);
            return false;
        }

        iXboxWaterStripIndices(nv, indices);

        // The managed pool on Direct3D 9: kept through a device reset.
        sVB[g] = rw::d3d::createVertexBuffer(nv * nv * 2 * sizeof(F32), 0, false);
        sIB[g] = rw::d3d::createIndexBuffer(count * sizeof(U16), false);
        bool ok = sVB[g] != NULL && sIB[g] != NULL;

        if (ok)
        {
            rw::uint8* v = rw::d3d::lockVertices(sVB[g], 0, nv * nv * 2 * sizeof(F32), 0);
            rw::uint16* x = rw::d3d::lockIndices(sIB[g], 0, count * sizeof(U16), 0);
            ok = v != NULL && x != NULL;

            if (v != NULL)
            {
                memcpy(v, verts, nv * nv * 2 * sizeof(F32));
                rw::d3d::unlockVertices(sVB[g]);
            }

            if (x != NULL)
            {
                memcpy(x, indices, count * sizeof(U16));
                rw::d3d::unlockIndices(sIB[g]);
            }
        }

        sIndices[g] = count;
        free(indices);
        free(verts);
        return ok;
    }

    const char* Open()
    {
        if (iBackendIsVulkan())
        {
            return "the Vulkan renderer has no water shaders";
        }

#ifdef RW_D3D9
        if (iBackendIsD3D9() && rw::d3d::getFixedFunction())
        {
            return "Direct3D 9's fixed path has no water shaders";
        }
#endif

        if (Blob(true) == NULL)
        {
            return "this renderer has no water shaders";
        }

        sVS = rw::d3d::createVertexShader(Blob(true));
        sPS = rw::d3d::createPixelShader(Blob(false));

        if (sVS == NULL || sPS == NULL)
        {
            return "the water shaders could not be made";
        }

        // One stream: (i, j) as two floats.
        rw::d3d9::VertexElement elements[2] = {
            { 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 },
        };
        sDecl = rw::d3d9::createVertexDeclaration(elements);

        if (sDecl == NULL)
        {
            return "the water's vertex declaration could not be made";
        }

#ifdef RW_D3D11
        // Direct3D 11 makes the input layout at the first draw and skips the
        // draw, silently, when it cannot; asked here so that a failure is a
        // GameCube water with a reason rather than no water at all.
        if (iBackendIsD3D11() && rw::d3d::impl11::inputLayoutFor(sDecl, sVS) == NULL)
        {
            return "Direct3D 11 would not take the water's vertex layout";
        }
#endif

        for (U32 g = 0; g < iXBOXWATERDRAW_GRIDS; g++)
        {
            if (!MakeGrid(g))
            {
                return "the water's vertex and index buffers could not be made";
            }
        }

        return NULL;
    }

    void Begin(const iXboxWaterSurface& s, S32 g, rw::Matrix* world)
    {
        rw::d3d::setVertexShader(sVS);
        rw::d3d::setPixelShader(sPS);
        rw::d3d::setVertexDeclaration(sDecl);
        rw::d3d::setStreamSource(0, sVB[g], 0, 2 * sizeof(F32));
        rw::d3d::setIndices(sIB[g]);

        // c0..c3 = world * view * projection (VSLOC_combined).
        rw::d3d::uploadMatrices(world);

        F32 grid[4] = { 1.0f / (F32)kGridVerts[g], 0.0f, 0.0f, 0.0f };
        rw::d3d::setVertexShaderConstantF(42, grid, 1);
        rw::d3d::setPixelShaderConstantF(1, s.tint, 1);

        rw::d3d::setTexture(0, reinterpret_cast<rw::Texture*>(s.bump));
        rw::d3d::setTexture(1, reinterpret_cast<rw::Texture*>(s.refraction));
        rw::d3d::setTexture(2, reinterpret_cast<rw::Texture*>(s.reflection));
    }

    void Tile(S32 g, const F32 tile[4], const F32 refract[4], const F32 reflect[4])
    {
        F32 bump[8];
        memcpy(bump, refract, 4 * sizeof(F32));
        memcpy(bump + 4, reflect, 4 * sizeof(F32));

        rw::d3d::setVertexShaderConstantF(41, tile, 1);
        rw::d3d::setPixelShaderConstantF(2, bump, 2);
        rw::d3d::flushCache();

        const U32 nv = kGridVerts[g];
        rw::d3d::drawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, nv * nv, 0, sIndices[g] - 2);
    }

    void End()
    {
        rw::d3d::setTexture(1, NULL);
        rw::d3d::setTexture(2, NULL);
    }
} // namespace d3dwater
#endif

#ifdef RW_GL3
namespace
{
#include "shadersgl/water_frag_gl.inc"
#include "shadersgl/water_vert_gl.inc"
} // namespace

namespace gl3water
{
    rw::int32 sUniform = -1;
    rw::gl3::Shader* sShader;
    GLuint sVbo[iXBOXWATERDRAW_GRIDS];
    GLuint sIbo[iXBOXWATERDRAW_GRIDS];
    GLuint sVao[iXBOXWATERDRAW_GRIDS];
    U32 sIndices[iXBOXWATERDRAW_GRIDS];

    // The reflection's texture unit. 2 and 3 hold the shadow map and the toon
    // ramp for the whole frame (header.frag), so the third texture goes past
    // them.
    const rw::int32 kReflectUnit = 4;

    rw::gl3::AttribDesc sAttrib = { rw::gl3::ATTRIB_POS, GL_FLOAT, GL_FALSE, 2,
                                    2 * sizeof(F32), 0 };

    // u_water: [0] the tile, [1] the grid, [2] the tint, [3] and [4] the bump
    // matrices.
    F32 sValues[5][4];

    bool MakeGrid(U32 g)
    {
        const U32 nv = kGridVerts[g];
        const U32 count = iXboxWaterStripIndices(nv, NULL);
        U16* indices = (U16*)malloc(count * sizeof(U16));
        F32* verts = GridVertices(nv);

        if (indices == NULL || verts == NULL)
        {
            free(indices);
            free(verts);
            return false;
        }

        iXboxWaterStripIndices(nv, indices);

        // The element buffer belongs to whichever vertex array is bound when
        // it is bound: the grid's own where librw uses them, else librw's
        // default one, which every draw rebinds its buffers into.
        if (rw::gl3::useVaos)
        {
            glGenVertexArrays(1, &sVao[g]);
            rw::gl3::bindVertexArray(sVao[g]);
        }
        else
        {
            rw::gl3::bindDefaultVertexArray();
        }

        glGenBuffers(1, &sVbo[g]);
        glGenBuffers(1, &sIbo[g]);
        glBindBuffer(GL_ARRAY_BUFFER, sVbo[g]);
        glBufferData(GL_ARRAY_BUFFER, nv * nv * 2 * sizeof(F32), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIbo[g]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(U16), indices, GL_STATIC_DRAW);

        if (rw::gl3::useVaos)
        {
            rw::gl3::setAttribPointers(&sAttrib, 1);
        }

        rw::gl3::bindDefaultVertexArray();

        sIndices[g] = count;
        free(indices);
        free(verts);
        return sVbo[g] != 0 && sIbo[g] != 0 && (!rw::gl3::useVaos || sVao[g] != 0);
    }

    const char* Open()
    {
        if (sUniform < 0)
        {
            // iXboxWaterRegisterShaderUniforms was never called.
            return "the water's GL uniform was not registered";
        }

        const char* vs[] = { rw::gl3::shaderDecl, rw::gl3::header_vert_src, water_vert_src,
                             NULL };
        const char* fs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src, water_frag_src,
                             NULL };
        sShader = rw::gl3::Shader::create(vs, fs);

        if (sShader == NULL)
        {
            return "the water shaders could not be made";
        }

        // Shader::create binds tex0..tex3 by name; the reflection's sampler is
        // pointed at its unit here, once.
        sShader->use();
        GLint loc = glGetUniformLocation(sShader->program, "u_waterReflect");

        if (loc < 0)
        {
            return "the water shader has no reflection sampler";
        }

        glUniform1i(loc, kReflectUnit);

        for (U32 g = 0; g < iXBOXWATERDRAW_GRIDS; g++)
        {
            if (!MakeGrid(g))
            {
                return "the water's vertex and index buffers could not be made";
            }
        }

        return NULL;
    }

    void Bind(S32 g)
    {
        if (rw::gl3::useVaos)
        {
            rw::gl3::bindVertexArray(sVao[g]);
            return;
        }

        rw::gl3::bindDefaultVertexArray();
        glBindBuffer(GL_ARRAY_BUFFER, sVbo[g]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIbo[g]);
        rw::gl3::setAttribPointers(&sAttrib, 1);
    }

    void Begin(const iXboxWaterSurface& s, S32 g, rw::Matrix* world)
    {
        sShader->use();
        rw::gl3::setWorldMatrix(world);

        memset(sValues, 0, sizeof(sValues));
        sValues[1][0] = 1.0f / (F32)kGridVerts[g];
        memcpy(sValues[2], s.tint, sizeof(sValues[2]));

        rw::gl3::setTexture(0, reinterpret_cast<rw::Texture*>(s.bump));
        rw::gl3::setTexture(1, reinterpret_cast<rw::Texture*>(s.refraction));
        rw::gl3::setTexture(kReflectUnit, reinterpret_cast<rw::Texture*>(s.reflection));

        Bind(g);
    }

    void Tile(S32 g, const F32 tile[4], const F32 refract[4], const F32 reflect[4])
    {
        memcpy(sValues[0], tile, sizeof(sValues[0]));
        memcpy(sValues[3], refract, sizeof(sValues[3]));
        memcpy(sValues[4], reflect, sizeof(sValues[4]));
        rw::gl3::setUniform(sUniform, sValues);

        // Something between two tiles (nothing, today) could have bound
        // another vertex array; bindVertexArray is a compare when it has not.
        if (rw::gl3::useVaos)
        {
            rw::gl3::bindVertexArray(sVao[g]);
        }

        rw::gl3::flushCache();
        glDrawElements(GL_TRIANGLE_STRIP, (GLsizei)sIndices[g], GL_UNSIGNED_SHORT, NULL);
    }

    void End()
    {
        if (!rw::gl3::useVaos)
        {
            rw::gl3::disableAttribPointers(&sAttrib, 1);
        }

        // Back to librw's default vertex array, so that a buffer bound next
        // without one of its own does not land in the grid's.
        rw::gl3::bindDefaultVertexArray();
        rw::gl3::setTexture(1, NULL);
        rw::gl3::setTexture(kReflectUnit, NULL);
    }
} // namespace gl3water
#endif

const char* iXboxWaterDrawOpen()
{
    if (sOpened)
    {
        return sOpenWhy;
    }

    if (ForcedFailure())
    {
        // Not kept: the test asks for it in one run and not the next.
        return "the water shaders could not be made (TSSM_WATER_FAIL)";
    }

    const char* why = "no renderer";

#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D() || iBackendIsVulkan())
    {
        why = d3dwater::Open();
    }
#elif defined(RW_VULKAN)
    if (iBackendIsVulkan())
    {
        why = "the Vulkan renderer has no water shaders";
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        why = gl3water::Open();
    }
#endif

    // A failure is final for the run, as the shaders and the device are.
    sOpened = true;
    sOpenWhy = why;
    return why;
}

bool iXboxWaterDrawHasGrid(U32 vertsPerSide)
{
    return sOpened && sOpenWhy == NULL && GridIndex(vertsPerSide) >= 0;
}

bool iXboxWaterDrawBegin(const iXboxWaterSurface& s)
{
    S32 g = GridIndex(s.vertsPerSide);

    if (!sOpened || sOpenWhy != NULL || g < 0 || s.bump == NULL || s.refraction == NULL ||
        s.reflection == NULL)
    {
        return false;
    }

    // iWaterPushRenderStates (0x1C2430), as far as the picture goes. The
    // texture raster goes to nothing first so that the bump frame bound in
    // its place is the only texture the alpha states are worked out from.
    iRenderStateSave(&sSaved);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);

    rw::Matrix identity;
    identity.setIdentity();
    rw::Matrix world = s.world != NULL ? *reinterpret_cast<const rw::Matrix*>(s.world) : identity;

    sGrid = g;

#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dwater::Begin(s, g, &world);
        return true;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3water::Begin(s, g, &world);
        return true;
    }
#endif

    iRenderStateRestore(&sSaved, FALSE);
    sGrid = -1;
    return false;
}

void iXboxWaterDrawTile(const F32 tile[4], const F32 refract[4], const F32 reflect[4])
{
    if (sGrid < 0)
    {
        return;
    }

#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dwater::Tile(sGrid, tile, refract, reflect);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3water::Tile(sGrid, tile, refract, reflect);
        return;
    }
#endif
}

void iXboxWaterDrawEnd()
{
    if (sGrid < 0)
    {
        return;
    }

#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dwater::End();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3water::End();
    }
#endif

    // The stage-0 texture, its filter and addressing, and every state set in
    // Begin, as they were.
    iRenderStateRestore(&sSaved, FALSE);
    sGrid = -1;
}

#else

// No renderer with shaders (LIBRW_PLATFORM=NULL, or Vulkan alone).
const char* iXboxWaterDrawOpen()
{
    if (ForcedFailure())
    {
        return "the water shaders could not be made (TSSM_WATER_FAIL)";
    }

    return "this renderer has no water shaders";
}

bool iXboxWaterDrawHasGrid(U32)
{
    return false;
}

bool iXboxWaterDrawBegin(const iXboxWaterSurface&)
{
    return false;
}

void iXboxWaterDrawTile(const F32*, const F32*, const F32*)
{
}

void iXboxWaterDrawEnd()
{
}

#endif

namespace
{
    char sStatus[200];
} // namespace

void iXboxWaterSetStatus(const char* status)
{
    if (status == NULL)
    {
        sStatus[0] = '\0';
        return;
    }

    snprintf(sStatus, sizeof(sStatus), "%s", status);
}

const char* iXboxWaterStatus()
{
    return sStatus[0] != '\0' ? sStatus : NULL;
}

void iXboxWaterRegisterShaderUniforms(void)
{
#if defined(RW_GL3)
    gl3water::sUniform = rw::gl3::registerUniform("u_water", rw::gl3::UNIFORM_VEC4, 5);
#endif
}
