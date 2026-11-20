// The Xbox release's spotlight light volumes. What the Xbox does and how this
// follows it are in iLightVolume.h.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include "backend.h"
#include "iLightVolume.h"
#include "screenpass.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The GameCube cone (zSpotlight.cpp's create_cone_atomic and set_cone_verts):
// 32 columns of 8 rings, vertex c * 8 + r, ring 0 at the lamp and ring 7 at
// the far end, and 448 side triangles. A closed copy adds a centre at each end
// and a fan of 32 triangles to it.
#define CONE_COLUMNS 32
#define CONE_RINGS 8
#define CONE_VERTS (CONE_COLUMNS * CONE_RINGS)
#define CONE_SIDE_TRIS 448
#define CONE_TRIS (CONE_SIDE_TRIS + 2 * CONE_COLUMNS)

// What pass 1 draws with: the colour each cone shape's material last had (the
// GameCube cone starts white), and the blend constant's alpha the last volume
// left (D3D's default constant is 0). The colours belong to the level's cones,
// which zSpotlight::scene_exit destroys, so they go when the next level makes
// its own (iLightVolumeForgetCones); the constant is the device's and stays.
struct ConeLeftover
{
    const void* atomic;
    RwRGBA color;
};

#define MAX_CONES 32
static ConeLeftover sCones[MAX_CONES];
static S32 sConeCount;
static U8 sConstantAlpha;

// The closed cone, made when the first volume is drawn: the GameCube
// spotlights (the 3DS's only ones) draw none.
static RwIm3DVertex* sVerts;
static RwImVertexIndex* sIndices;

static bool sSaid;

// The render states the passes change, as they were.
struct SavedStates
{
    void* raster;
    S32 ztest;
    S32 zwrite;
    S32 cull;
    S32 vertexAlpha;
    S32 srcBlend;
    S32 destBlend;
    U32 writeMask;
    U32 alphaFunc;
    U32 alphaRef;
    U32 stencil[8];
};
static SavedStates sSaved;

static const rw::RenderState kStencilStates[8] = {
    rw::STENCILENABLE,      rw::STENCILFAIL,         rw::STENCILZFAIL,
    rw::STENCILPASS,        rw::STENCILFUNCTION,     rw::STENCILFUNCTIONREF,
    rw::STENCILFUNCTIONMASK, rw::STENCILFUNCTIONWRITEMASK,
};

static void say(const char* what)
{
    if (!sSaid)
    {
        sSaid = true;
        printf("tssm: spotlight light volumes: %s\n", what);
        fflush(stdout);
    }
}

bool iLightVolumeReady()
{
    if (!iScreenPassReady())
    {
        return false;
    }
#ifdef RW_GL3
    if (iBackendIsGL3() && rw::gl3::virtualScreenUsesMSRTT())
    {
        say("off -- this anti-aliasing path (TSSM_GPU_MSRTT) has no stencil buffer");
        return false;
    }
#endif
    return true;
}

void iLightVolumeBegin()
{
    SavedStates& s = sSaved;
    s.raster = NULL;
    s.ztest = TRUE;
    s.zwrite = TRUE;
    s.cull = rwCULLMODECULLBACK;
    s.vertexAlpha = FALSE;
    s.srcBlend = rwBLENDSRCALPHA;
    s.destBlend = rwBLENDINVSRCALPHA;
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &s.raster);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &s.ztest);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &s.zwrite);
    RwRenderStateGet(rwRENDERSTATECULLMODE, &s.cull);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s.vertexAlpha);
    RwRenderStateGet(rwRENDERSTATESRCBLEND, &s.srcBlend);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, &s.destBlend);
    s.writeMask = rw::GetRenderState(rw::COLORWRITEMASK);
    s.alphaFunc = rw::GetRenderState(rw::ALPHATESTFUNC);
    s.alphaRef = rw::GetRenderState(rw::ALPHATESTREF);
    for (S32 i = 0; i < 8; i++)
    {
        s.stencil[i] = rw::GetRenderState(kStencilStates[i]);
    }
    say("on");
}

void iLightVolumeEnd()
{
    const SavedStates& s = sSaved;
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, s.raster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(UPtr)s.ztest);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)(UPtr)s.zwrite);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)(UPtr)s.cull);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)(UPtr)s.vertexAlpha);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)(UPtr)s.srcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)(UPtr)s.destBlend);
    rw::SetRenderState(rw::COLORWRITEMASK, s.writeMask);
    rw::SetRenderState(rw::ALPHATESTFUNC, s.alphaFunc);
    rw::SetRenderState(rw::ALPHATESTREF, s.alphaRef);
    for (S32 i = 0; i < 8; i++)
    {
        rw::SetRenderState(kStencilStates[i], s.stencil[i]);
    }
}

void iLightVolumeClearStencil()
{
    RwCamera* cam = RwCameraGetCurrentCamera();
    if (cam == NULL)
    {
        return;
    }
    // The Xbox ends the camera's update around the clear; so does this.
    RwCameraEndUpdate(cam);
    RwRGBA black = { 0, 0, 0, 0 };
    RwCameraClear(cam, &black, rwCAMERACLEARSTENCIL);
    RwCameraBeginUpdate(cam);
}

void iLightVolumeForgetCones()
{
    sConeCount = 0;
}

S32 iLightVolumeConeCount()
{
    return sConeCount;
}

// The leftover colour of a cone shape, made on first sight.
static RwRGBA* coneColor(const void* atomic)
{
    for (S32 i = 0; i < sConeCount; i++)
    {
        if (sCones[i].atomic == atomic)
        {
            return &sCones[i].color;
        }
    }
    if (sConeCount == MAX_CONES)
    {
        // A scene with more shapes than this reuses the last slot; only pass
        // 1's colour depends on it.
        sConeCount--;
    }
    ConeLeftover& c = sCones[sConeCount++];
    c.atomic = atomic;
    c.color.red = c.color.green = c.color.blue = c.color.alpha = 0xFF;
    return &c.color;
}

// The cone, closed, in sVerts and sIndices. False when the geometry is not the
// GameCube cone, or there is no memory for it.
static bool buildCone(rw::Geometry* g)
{
    if (g == NULL || g->numVertices != CONE_VERTS || g->numTriangles != CONE_SIDE_TRIS ||
        g->numMorphTargets < 1 || g->morphTargets[0].vertices == NULL || g->triangles == NULL)
    {
        return false;
    }
    if (sVerts == NULL)
    {
        sVerts = (RwIm3DVertex*)malloc((CONE_VERTS + 2) * sizeof(RwIm3DVertex));
    }
    if (sIndices == NULL)
    {
        sIndices = (RwImVertexIndex*)malloc(CONE_TRIS * 3 * sizeof(RwImVertexIndex));
    }
    if (sVerts == NULL || sIndices == NULL)
    {
        return false;
    }

    const rw::V3d* v = g->morphTargets[0].vertices;
    rw::V3d centre[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    for (S32 i = 0; i < CONE_VERTS; i++)
    {
        RwIm3DVertexSetPos(&sVerts[i], v[i].x, v[i].y, v[i].z);
        const S32 ring = i % CONE_RINGS;
        if (ring == 0 || ring == CONE_RINGS - 1)
        {
            rw::V3d& c = centre[ring == 0 ? 0 : 1];
            c.x += v[i].x;
            c.y += v[i].y;
            c.z += v[i].z;
        }
    }
    for (S32 k = 0; k < 2; k++)
    {
        RwIm3DVertexSetPos(&sVerts[CONE_VERTS + k], centre[k].x / CONE_COLUMNS,
                           centre[k].y / CONE_COLUMNS, centre[k].z / CONE_COLUMNS);
    }

    RwImVertexIndex* idx = sIndices;
    for (S32 t = 0; t < CONE_SIDE_TRIS; t++)
    {
        *idx++ = (RwImVertexIndex)g->triangles[t].v[0];
        *idx++ = (RwImVertexIndex)g->triangles[t].v[1];
        *idx++ = (RwImVertexIndex)g->triangles[t].v[2];
    }
    // The side triangles face outward by the right-hand rule, columns going
    // round +z; the ends are wound the same way.
    for (S32 c = 0; c < CONE_COLUMNS; c++)
    {
        const S32 next = (c + 1) % CONE_COLUMNS;
        *idx++ = (RwImVertexIndex)CONE_VERTS;
        *idx++ = (RwImVertexIndex)(next * CONE_RINGS);
        *idx++ = (RwImVertexIndex)(c * CONE_RINGS);

        *idx++ = (RwImVertexIndex)(CONE_VERTS + 1);
        *idx++ = (RwImVertexIndex)(c * CONE_RINGS + CONE_RINGS - 1);
        *idx++ = (RwImVertexIndex)(next * CONE_RINGS + CONE_RINGS - 1);
    }
    return true;
}

static void setColor(RwRGBA c)
{
    for (S32 i = 0; i < CONE_VERTS + 2; i++)
    {
        RwIm3DVertexSetRGBA(&sVerts[i], c.red, c.green, c.blue, c.alpha);
    }
}

// One pass of the cone, transformed by `ltm`.
static void drawCone(RwMatrix* ltm)
{
    if (RwIm3DTransform(sVerts, CONE_VERTS + 2, ltm, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA) != NULL)
    {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, sIndices, CONE_TRIS * 3);
        RwIm3DEnd();
    }
}

void iLightVolumeRender(RpAtomic* atomic, RwRGBA color, bool additive)
{
    rw::Atomic* a = reinterpret_cast<rw::Atomic*>(atomic);
    if (a == NULL || a->getFrame() == NULL || !buildCone(a->geometry))
    {
        return;
    }
    RwMatrix* ltm = reinterpret_cast<RwMatrix*>(a->getFrame()->getLTM());
    RwRGBA* leftover = coneColor(atomic);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
    rw::SetRenderState(rw::ALPHATESTREF, 0);
    if (additive)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDDESTCOLOR);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCALPHA);
    }
    rw::SetRenderState(rw::STENCILENABLE, 1);
    rw::SetRenderState(rw::STENCILFUNCTIONREF, 1);
    rw::SetRenderState(rw::STENCILFUNCTIONMASK, 0xFF);
    rw::SetRenderState(rw::STENCILFUNCTIONWRITEMASK, 0xFF);

    // 1. The inner faces, depth tested; hidden ones count up. Colour written,
    //    with what the last pass 3s left.
    RwRGBA c1 = *leftover;
    if (!additive)
    {
        c1.alpha = sConstantAlpha;
    }
    setColor(c1);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLFRONT);
    rw::SetRenderState(rw::COLORWRITEMASK, ISCREENPASS_WRITE_ALL);
    rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILALWAYS);
    rw::SetRenderState(rw::STENCILFAIL, rw::STENCILKEEP);
    rw::SetRenderState(rw::STENCILPASS, rw::STENCILKEEP);
    rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILINC);
    drawCone(ltm);

    // 2. The outer faces, no colour; hidden ones count down.
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    rw::SetRenderState(rw::COLORWRITEMASK, 0);
    rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILDEC);
    drawCone(ltm);

    // 3. The inner faces, no depth test, where the count is 1: the scene
    //    inside the beam, in the light's colour (black is white).
    RwRGBA c3 = color;
    if (c3.red == 0 && c3.green == 0 && c3.blue == 0)
    {
        c3.red = c3.green = c3.blue = 0xFF;
    }
    setColor(c3);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLFRONT);
    rw::SetRenderState(rw::COLORWRITEMASK, additive ? ISCREENPASS_WRITE_RGB : ISCREENPASS_WRITE_ALL);
    rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILEQUAL);
    rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILKEEP);
    drawCone(ltm);

    *leftover = c3;
    sConstantAlpha = c3.alpha;
}
