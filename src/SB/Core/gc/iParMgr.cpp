#include "iParMgr.h"
#include "zParSys.h"

static S32 gColorTableInit;
static F32 gColorTable[256];

static void iRenderPushFlat(xPar* p, xParCmdTex* tex);
void iRenderPushQuadStreak(xPar* p, xParCmdTex* tex);

void iParMgrInit()
{
    S32 i;

    if (!gColorTableInit)
    {
        for (i = 0; i < 256; i++)
        {
            gColorTable[i] = i / 255.0f;
        }

        gColorTableInit = 1;
    }
}

void iParMgrUpdate(float)
{
}

void iParMgrRender()
{
}

inline void iRenderSetCameraViewMatrix(xMat4x3* m)
{
    if (m == NULL && globals.screen->icam != NULL)
    {
        gRenderBuffer.m_camViewMatrix =
            *(xMat4x3*)&RwCameraGetFrame((RwCamera*)RWSRCGLOBAL(curCamera))->modelling;
    }
    else
    {
        gRenderBuffer.m_camViewMatrix = *m;
    }

    xVec3Inv((xVec3*)&gRenderBuffer.m_camViewR, &gRenderBuffer.m_camViewMatrix.right);
    xVec3Inv((xVec3*)&gRenderBuffer.m_camViewU, &gRenderBuffer.m_camViewMatrix.up);
}

inline void iRenderTrianglesImmediate(S32 vertType, S32 vertTypeSize, void* data, S32 dataSize,
                                      U16* index, S32 indexSize)
{
    if (RwIm3DTransform((RwIm3DVertex*)data, dataSize, NULL, rwIM3D_VERTEXUV) != NULL)
    {
        if (indexSize != 0)
        {
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, index, indexSize);
        }
        else
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
        }
        RwIm3DEnd();
    }
}

inline void iRenderFlush()
{
    if (gRenderBuffer.m_vertexCount > 0)
    {
        iRenderTrianglesImmediate(gRenderBuffer.m_vertexType, gRenderBuffer.m_vertexTypeSize,
                                  gRenderBuffer.m_vertex, gRenderBuffer.m_vertexCount,
                                  gRenderBuffer.m_index, gRenderBuffer.m_indexCount);
    }

    gRenderBuffer.m_vertexCount = 0;
    gRenderBuffer.m_indexCount = 0;
}

void iParMgrRenderParSys_Sprite(void* data, xParGroup* ps)
{
    xPar* idx = ps->m_root;
    zParSys* s;
    RwTexture* texture;
    RwRaster* raster;
    S32 indexCount;
    S32 vertexCount;
    U16* i3d;
    RxObjSpace3DVertex* v3d;
    xParCmdTex* tex;

    iRenderSetCameraViewMatrix(NULL);

    s = (zParSys*)data;

    texture = (RwTexture*)xSTFindAsset(s->tasset->textureID, NULL);
    if (texture != NULL)
    {
        raster = texture->raster;
        if (raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }
    }

    tex = ps->m_cmdTex;

    indexCount = 0;
    vertexCount = 0;
    i3d = gRenderBuffer.m_index;
    v3d = gRenderBuffer.m_vertex;

    while (idx != NULL)
    {
        xPar* p;
        F32 size;
        xVec3 h;
        xVec3 v;
        U8 r;
        U8 g;
        U8 b;
        U8 a;
        xVec3 vert[4];

        p = idx;
        idx = idx->m_next;

        size = 0.5f * p->m_size;

        h.x = size * gRenderBuffer.m_camViewR.x;
        h.y = size * gRenderBuffer.m_camViewR.y;
        h.z = size * gRenderBuffer.m_camViewR.z;
        v.x = size * gRenderBuffer.m_camViewU.x;
        v.y = size * gRenderBuffer.m_camViewU.y;
        v.z = size * gRenderBuffer.m_camViewU.z;

        r = p->m_c[0];
        g = p->m_c[1];
        b = p->m_c[2];
        a = p->m_c[3];

        vert[0].x = p->m_pos.x - h.x - v.x;
        vert[0].y = p->m_pos.y - h.y - v.y;
        vert[0].z = p->m_pos.z - h.z - v.z;
        vert[1].x = p->m_pos.x + h.x + v.x;
        vert[1].y = p->m_pos.y + h.y + v.y;
        vert[1].z = p->m_pos.z + h.z + v.z;
        vert[2].x = p->m_pos.x + h.x - v.x;
        vert[2].y = p->m_pos.y + h.y - v.y;
        vert[2].z = p->m_pos.z + h.z - v.z;
        vert[3].x = p->m_pos.x - h.x + v.x;
        vert[3].y = p->m_pos.y - h.y + v.y;
        vert[3].z = p->m_pos.z - h.z + v.z;

        RwIm3DVertexSetRGBA(&v3d[0], r, g, b, a);
        RwIm3DVertexSetRGBA(&v3d[1], r, g, b, a);
        RwIm3DVertexSetRGBA(&v3d[2], r, g, b, a);
        RwIm3DVertexSetRGBA(&v3d[3], r, g, b, a);

        RwIm3DVertexSetPos(&v3d[0], vert[0].x, vert[0].y, vert[0].z);
        RwIm3DVertexSetPos(&v3d[1], vert[1].x, vert[1].y, vert[1].z);
        RwIm3DVertexSetPos(&v3d[2], vert[2].x, vert[2].y, vert[2].z);
        RwIm3DVertexSetPos(&v3d[3], vert[3].x, vert[3].y, vert[3].z);

        if (tex != NULL)
        {
            F32 u1;
            F32 u2;
            F32 v1;
            F32 v2;

            u1 = tex->x1 + p->m_texIdx[0] * tex->unit_width;
            v1 = tex->y1 + p->m_texIdx[1] * tex->unit_height;
            u2 = tex->x1 + (p->m_texIdx[0] + 1) * tex->unit_width;
            v2 = tex->y1 + (p->m_texIdx[1] + 1) * tex->unit_height;

            v3d[0].u = u1;
            v3d[0].v = v1;
            v3d[1].u = u2;
            v3d[1].v = v2;
            v3d[2].u = u2;
            v3d[2].v = v1;
            v3d[3].u = u1;
            v3d[3].v = v2;
        }
        else
        {
            v3d[0].u = 0.0f;
            v3d[0].v = 0.0f;
            v3d[1].u = 1.0f;
            v3d[1].v = 1.0f;
            v3d[2].u = 1.0f;
            v3d[2].v = 0.0f;
            v3d[3].u = 0.0f;
            v3d[3].v = 1.0f;
        }

        i3d[0] = vertexCount;
        i3d[1] = vertexCount + 1;
        i3d[2] = vertexCount + 2;
        i3d[3] = vertexCount + 3;
        i3d[4] = vertexCount;
        i3d[5] = vertexCount + 1;

        i3d += 6;
        v3d += 4;
        indexCount += 6;
        vertexCount += 4;

        if (indexCount > 960 - 6 || vertexCount > 480 - 4)
        {
            gRenderBuffer.m_indexCount = indexCount;
            gRenderBuffer.m_vertexCount = vertexCount;
            v3d = gRenderBuffer.m_vertex;
            i3d = gRenderBuffer.m_index;
            iRenderFlush();
            indexCount = 0;
            vertexCount = 0;
        }
    }

    gRenderBuffer.m_indexCount = indexCount;
    gRenderBuffer.m_vertexCount = vertexCount;
    iRenderFlush();
}

void iRenderPushQuadStreak(xPar* p, xParCmdTex* tex)
{
    void* vertices;
    U16* indices;
    xVec3 h;
    static RxObjSpace3DVertex v3d[4];
    static U16 i3d[6] = { 0, 1, 2, 0, 2, 3 };

    vertices = v3d;
    indices = i3d;

    if (gRenderBuffer.m_indexCount + 6 > 960)
    {
        iRenderFlush();
    }

    if (gRenderBuffer.m_vertexCount + 4 > 480)
    {
        iRenderFlush();
    }

    U8 r = p->m_c[0];
    U8 g = p->m_c[1];
    U8 b = p->m_c[2];
    U8 a = p->m_c[3];
    F32 size = p->m_size;
    xVec3 vert[4];

    h.x = size * gRenderBuffer.m_camViewR.x;
    h.y = size * gRenderBuffer.m_camViewR.y;
    h.z = size * gRenderBuffer.m_camViewR.z;

    vert[0].x = p->m_pos.x;
    vert[0].y = p->m_pos.y;
    vert[0].z = p->m_pos.z;

    vert[3].x = p->m_pos.x;
    vert[3].y = p->m_pos.y;
    vert[3].z = p->m_pos.z;

    vert[1].x = vert[0].x - 5.0f * p->m_vel.x;
    vert[1].y = vert[0].y - 5.0f * p->m_vel.y;
    vert[1].z = vert[0].z - 5.0f * p->m_vel.z;

    vert[2].x = vert[1].x;
    vert[2].y = vert[1].y;
    vert[2].z = vert[1].z;

    vert[0].x -= h.x;
    vert[0].y -= h.y;
    vert[0].z -= h.z;

    vert[1].x -= h.x;
    vert[1].y -= h.y;
    vert[1].z -= h.z;

    vert[2].x += h.x;
    vert[2].y += h.y;
    vert[2].z += h.z;

    vert[3].x += h.x;
    vert[3].y += h.y;
    vert[3].z += h.z;

    RwIm3DVertexSetRGBA(&v3d[0], r, g, b, a);
    RwIm3DVertexSetRGBA(&v3d[1], r, g, b, a);
    RwIm3DVertexSetRGBA(&v3d[2], r, g, b, a);
    RwIm3DVertexSetRGBA(&v3d[3], r, g, b, a);

    RwIm3DVertexSetPos(&v3d[0], vert[0].x, vert[0].y, vert[0].z);
    RwIm3DVertexSetPos(&v3d[1], vert[1].x, vert[1].y, vert[1].z);
    RwIm3DVertexSetPos(&v3d[2], vert[2].x, vert[2].y, vert[2].z);
    RwIm3DVertexSetPos(&v3d[3], vert[3].x, vert[3].y, vert[3].z);

    if (tex != NULL)
    {
        F32 u1;
        F32 u2;
        F32 v1;
        F32 v2;

        u1 = tex->x1 + p->m_texIdx[0] * tex->unit_width;
        v1 = tex->y1 + p->m_texIdx[1] * tex->unit_height;
        u2 = tex->x1 + (p->m_texIdx[0] + 1) * tex->unit_width;
        v2 = tex->y1 + (p->m_texIdx[1] + 1) * tex->unit_height;

        v3d[0].u = u1;
        v3d[0].v = v2;
        v3d[1].u = u1;
        v3d[1].v = v1;
        v3d[2].u = u2;
        v3d[2].v = v1;
        v3d[3].u = u2;
        v3d[3].v = v2;
    }
    else
    {
        v3d[0].u = 0.0f;
        v3d[0].v = 1.0f;
        v3d[1].u = 0.0f;
        v3d[1].v = 0.0f;
        v3d[2].u = 1.0f;
        v3d[2].v = 0.0f;
        // Retail bug: writes v3d[2].v instead of v3d[3].v (and v3d[3].u = 0.0f),
        // so v3d[3].v keeps whatever the previous particle left in the static.
        v3d[3].u = 0.0f;
        v3d[2].v = 1.0f;
    }

    U16* dst = &gRenderBuffer.m_index[gRenderBuffer.m_indexCount];
    dst[0] = gRenderBuffer.m_vertexCount + indices[0];
    dst[1] = gRenderBuffer.m_vertexCount + indices[1];
    dst[2] = gRenderBuffer.m_vertexCount + indices[2];
    dst[3] = gRenderBuffer.m_vertexCount + indices[3];
    dst[4] = gRenderBuffer.m_vertexCount + indices[4];
    dst[5] = gRenderBuffer.m_vertexCount + indices[5];

    memcpy((U8*)gRenderBuffer.m_vertex +
               gRenderBuffer.m_vertexTypeSize * gRenderBuffer.m_vertexCount,
           vertices, gRenderBuffer.m_vertexTypeSize * 4);

    gRenderBuffer.m_indexCount += 6;
    gRenderBuffer.m_vertexCount += 4;
}

static void iRenderPushFlat(xPar* p, xParCmdTex* tex)
{
    void* vertices;
    U16* indices;
    static RxObjSpace3DVertex v3d[4];
    static U16 i3d[6] = { 0, 1, 2, 0, 2, 3 };

    vertices = v3d;
    indices = i3d;

    if (gRenderBuffer.m_indexCount + 6 > 960)
    {
        iRenderFlush();
    }

    if (gRenderBuffer.m_vertexCount + 4 > 480)
    {
        iRenderFlush();
    }

    U8 r = p->m_c[0];
    U8 g = p->m_c[1];
    U8 b = p->m_c[2];
    U8 a = p->m_c[3];
    F32 size = 0.5f * p->m_size;
    xVec3 vert[4];
    xMat3x3 groundmat;
    F32 yaw = 0.0f;

    if (p->m_rotdeg[0])
    {
        yaw = 6.2831855f * (p->m_rotdeg[0] / 255.0f);
    }

    xMat3x3Euler(&groundmat, yaw, 0.0f, 0.0f);

    xVec3 zdir;
    zdir.x = groundmat.at.x * size;
    zdir.z = groundmat.at.z * size;

    xVec3 xdir;
    xdir.x = groundmat.right.x * size;
    xdir.z = groundmat.right.z * size;

    xVec3 centre;
    centre.x = p->m_pos.x;
    centre.y = p->m_pos.y;
    centre.z = p->m_pos.z;

    vert[0].x = centre.x - xdir.x - zdir.x;
    vert[0].y = centre.y;
    vert[0].z = centre.z - xdir.z - zdir.z;
    vert[1].x = centre.x + xdir.x - zdir.x;
    vert[1].y = centre.y;
    vert[1].z = centre.z + xdir.z - zdir.z;
    vert[2].x = centre.x + xdir.x + zdir.x;
    vert[2].y = centre.y;
    vert[2].z = centre.z + xdir.z + zdir.z;
    vert[3].x = centre.x - xdir.x + zdir.x;
    vert[3].y = centre.y;
    vert[3].z = centre.z - xdir.z + zdir.z;

    RwIm3DVertexSetRGBA(&v3d[0], r, g, b, a);
    RwIm3DVertexSetRGBA(&v3d[1], r, g, b, a);
    RwIm3DVertexSetRGBA(&v3d[2], r, g, b, a);
    RwIm3DVertexSetRGBA(&v3d[3], r, g, b, a);

    RwIm3DVertexSetPos(&v3d[0], vert[0].x, vert[0].y, vert[0].z);
    RwIm3DVertexSetPos(&v3d[1], vert[1].x, vert[1].y, vert[1].z);
    RwIm3DVertexSetPos(&v3d[2], vert[2].x, vert[2].y, vert[2].z);
    RwIm3DVertexSetPos(&v3d[3], vert[3].x, vert[3].y, vert[3].z);

    if (tex != NULL)
    {
        F32 u1;
        F32 u2;
        F32 v1;
        F32 v2;

        u1 = tex->x1 + p->m_texIdx[0] * tex->unit_width;
        v1 = tex->y1 + p->m_texIdx[1] * tex->unit_height;
        u2 = tex->x1 + (p->m_texIdx[0] + 1) * tex->unit_width;
        v2 = tex->y1 + (p->m_texIdx[1] + 1) * tex->unit_height;

        v3d[0].u = u1;
        v3d[0].v = v1;
        v3d[1].u = u2;
        v3d[1].v = v1;
        v3d[2].u = u2;
        v3d[2].v = v2;
        v3d[3].u = u1;
        v3d[3].v = v2;
    }
    else
    {
        v3d[0].u = 0.0f;
        v3d[0].v = 0.0f;
        v3d[1].u = 1.0f;
        v3d[1].v = 0.0f;
        v3d[2].u = 1.0f;
        v3d[2].v = 1.0f;
        v3d[3].u = 0.0f;
        v3d[3].v = 1.0f;
    }

    U16* dst = &gRenderBuffer.m_index[gRenderBuffer.m_indexCount];
    dst[0] = gRenderBuffer.m_vertexCount + indices[0];
    dst[1] = gRenderBuffer.m_vertexCount + indices[1];
    dst[2] = gRenderBuffer.m_vertexCount + indices[2];
    dst[3] = gRenderBuffer.m_vertexCount + indices[3];
    dst[4] = gRenderBuffer.m_vertexCount + indices[4];
    dst[5] = gRenderBuffer.m_vertexCount + indices[5];

    memcpy((U8*)gRenderBuffer.m_vertex +
               gRenderBuffer.m_vertexTypeSize * gRenderBuffer.m_vertexCount,
           vertices, gRenderBuffer.m_vertexTypeSize * 4);

    gRenderBuffer.m_indexCount += 6;
    gRenderBuffer.m_vertexCount += 4;
}

void iParMgrRenderParSys_Streak(void* data, xParGroup* ps)
{
    xPar* idx = ps->m_root;
    zParSys* s;
    RwTexture* texture;
    RwRaster* raster;
    RxObjSpace3DVertex* v3d;

    iRenderSetCameraViewMatrix(NULL);

    s = (zParSys*)data;

    texture = (RwTexture*)xSTFindAsset(s->tasset->textureID, NULL);
    if (texture != NULL)
    {
        raster = texture->raster;
        if (raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    }

    v3d = gRenderBuffer.m_vertex;

    for (; idx != NULL; idx = idx->m_next)
    {
        F32 dx, dy, dz;
        F32 tx, ty, tz;
        F32 ax, ay, az;
        F32 px, py, pz;
        F32 size;

        px = idx->m_pos.x;
        py = idx->m_pos.y;
        pz = idx->m_pos.z;
        size = idx->m_size;
        dx = size * gRenderBuffer.m_camViewR.x;
        dy = size * gRenderBuffer.m_camViewR.y;
        dz = size * gRenderBuffer.m_camViewR.z;
        tx = px - 5.0f * idx->m_vel.x;
        ty = py - 5.0f * idx->m_vel.y;
        tz = pz - 5.0f * idx->m_vel.z;
        ax = tx;
        ay = ty;
        az = tz;
        tx -= dx;
        ty -= dy;
        tz -= dz;
        ax += dx;
        ay += dy;
        az += dz;

        v3d[0].x = ax;
        v3d[0].y = ay;
        v3d[0].z = az;

        v3d[1].x = tx;
        v3d[1].y = ty;
        v3d[1].z = tz;

        v3d[2].x = px;
        v3d[2].y = py;
        v3d[2].z = pz;

        U8 r = idx->m_c[0];
        U8 g = idx->m_c[1];
        U8 b = idx->m_c[2];
        U8 a = idx->m_c[3];

        v3d[0].r = r;
        v3d[0].g = g;
        v3d[0].b = b;
        v3d[0].a = a;
        v3d[1].r = r;
        v3d[1].g = g;
        v3d[1].b = b;
        v3d[1].a = a;
        v3d[2].r = r;
        v3d[2].g = g;
        v3d[2].b = b;
        v3d[2].a = a;

        v3d[2].u = 0.5f;
        v3d[2].v = 1.0f;
        v3d[1].u = 0.0f;
        v3d[1].v = 0.0f;
        v3d[0].u = 1.0f;
        v3d[0].v = 0.0f;

        v3d += 3;

        gRenderBuffer.m_vertexCount += 3;

        if (gRenderBuffer.m_vertexCount + 3 > 477)
        {
            iRenderFlush();
            v3d = gRenderBuffer.m_vertex;
        }
    }

    iRenderFlush();
}

void iParMgrRenderParSys_InvStreak(void* data, xParGroup* ps)
{
    xPar* idx = ps->m_root;
    zParSys* s;
    RwTexture* texture;
    RwRaster* raster;
    RxObjSpace3DVertex* v3d;

    iRenderSetCameraViewMatrix(NULL);

    s = (zParSys*)data;

    texture = (RwTexture*)xSTFindAsset(s->tasset->textureID, NULL);
    if (texture != NULL)
    {
        raster = texture->raster;
        if (raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    }

    v3d = gRenderBuffer.m_vertex;

    for (; idx != NULL; idx = idx->m_next)
    {
        F32 dx, dy, dz;
        F32 tx, ax, ty, ay, tz, az;
        F32 px, py, pz;
        F32 size;

        px = idx->m_pos.x;
        py = idx->m_pos.y;
        pz = idx->m_pos.z;
        size = idx->m_size;
        dx = size * gRenderBuffer.m_camViewR.x;
        dy = size * gRenderBuffer.m_camViewR.y;
        dz = size * gRenderBuffer.m_camViewR.z;
        tx = px - 5.0f * idx->m_vel.x;
        ax = tx;
        ty = py - 5.0f * idx->m_vel.y;
        ay = ty;
        tz = pz - 5.0f * idx->m_vel.z;
        az = tz;
        tx -= dx;
        ty -= dy;
        tz -= dz;
        ax += dx;
        ay += dy;
        az += dz;

        v3d[0].x = ax;
        v3d[0].y = ay;
        v3d[0].z = az;

        v3d[1].x = tx;
        v3d[1].y = ty;
        v3d[1].z = tz;

        v3d[2].x = px;
        v3d[2].y = py;
        v3d[2].z = pz;

        U8 r = idx->m_c[0];
        U8 g = idx->m_c[1];
        U8 b = idx->m_c[2];
        U8 a = idx->m_c[3];

        v3d[0].r = r;
        v3d[0].g = g;
        v3d[0].b = b;
        v3d[0].a = a;
        v3d[1].r = r;
        v3d[1].g = g;
        v3d[1].b = b;
        v3d[1].a = a;
        v3d[2].r = r;
        v3d[2].g = g;
        v3d[2].b = b;
        v3d[2].a = a;

        v3d[2].u = 0.5f;
        v3d[2].v = 1.0f;
        v3d[1].u = 0.0f;
        v3d[1].v = 0.0f;
        v3d[0].u = 1.0f;
        v3d[0].v = 0.0f;

        v3d += 3;

        gRenderBuffer.m_vertexCount += 3;

        if (gRenderBuffer.m_vertexCount + 3 > 477)
        {
            iRenderFlush();
            v3d = gRenderBuffer.m_vertex;
        }
    }

    iRenderFlush();
}

void iParMgrRenderParSys_QuadStreak(void* data, xParGroup* ps)
{
    xPar* idx = ps->m_root;
    RwTexture* texture;
    RwRaster* raster;

    iRenderSetCameraViewMatrix(NULL);

    texture = (RwTexture*)xSTFindAsset(((zParSys*)data)->tasset->textureID, NULL);
    if (texture != NULL)
    {
        raster = texture->raster;
        if (raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    }

    for (; idx != NULL; idx = idx->m_next)
    {
        iRenderPushQuadStreak(idx, ps->m_cmdTex);
    }

    iRenderFlush();
}

void iParMgrRenderParSys_Static(void*, xParGroup*)
{
}

void iParMgrRenderParSys_Ground(void* data, xParGroup* ps)
{
    xPar* idx = ps->m_root;
    zParSys* s;
    RwTexture* texture;
    RwRaster* raster;
    xParCmdTex* tex;
    static RxObjSpace3DVertex v3d[4];
    static U16 i3d[6] = { 0, 1, 2, 3, 0, 1 };

    iRenderSetCameraViewMatrix(NULL);

    s = (zParSys*)data;

    texture = (RwTexture*)xSTFindAsset(s->tasset->textureID, NULL);
    if (texture != NULL)
    {
        raster = texture->raster;
        if (raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }
    }

    for (; idx != NULL; idx = idx->m_next)
    {
        tex = ps->m_cmdTex;

        if (gRenderBuffer.m_indexCount + 6 > 960 || gRenderBuffer.m_vertexCount + 4 > 480)
        {
            iRenderFlush();
        }

        F32 size;
        xVec3 vert[4];
        U8 r;
        U8 g;
        U8 b;
        U8 a;
        xMat3x3 groundmat;
        F32 angx;
        F32 angy;
        F32 angz;
        xVec3 zdir;
        xVec3 xdir;
        xVec3 centre;

        size = 0.5f * idx->m_size;
        r = idx->m_c[0];
        g = idx->m_c[1];
        b = idx->m_c[2];
        a = idx->m_c[3];
        angz = 0.0f;
        angy = 0.0f;
        angx = 0.0f;

        if (idx->m_rotdeg[0])
        {
            angx = 6.2831855f * (idx->m_rotdeg[0] / 255.0f);
        }

        if (idx->m_rotdeg[1])
        {
            angy = 6.2831855f * (idx->m_rotdeg[1] / 255.0f);
        }

        if (idx->m_rotdeg[2])
        {
            angz = 6.2831855f * (idx->m_rotdeg[2] / 255.0f);
        }

        xMat3x3Euler(&groundmat, angx, angy, angz);

        zdir.x = groundmat.at.x * size;
        zdir.y = groundmat.at.y * size;
        zdir.z = groundmat.at.z * size;
        xdir.x = groundmat.right.x * size;
        xdir.y = groundmat.right.y * size;
        xdir.z = groundmat.right.z * size;
        centre.x = idx->m_pos.x;
        centre.y = idx->m_pos.y;
        centre.z = idx->m_pos.z;

        vert[0].x = centre.x - xdir.x - zdir.x;
        vert[0].y = centre.y - xdir.y - zdir.y;
        vert[0].z = centre.z - xdir.z - zdir.z;
        vert[1].x = centre.x + xdir.x + zdir.x;
        vert[1].y = centre.y + xdir.y + zdir.y;
        vert[1].z = centre.z + xdir.z + zdir.z;
        vert[2].x = centre.x + xdir.x - zdir.x;
        vert[2].y = centre.y + xdir.y - zdir.y;
        vert[2].z = centre.z + xdir.z - zdir.z;
        vert[3].x = centre.x - xdir.x + zdir.x;
        vert[3].y = centre.y - xdir.y + zdir.y;
        vert[3].z = centre.z - xdir.z + zdir.z;

        RwIm3DVertexSetRGBA(&v3d[0], r, g, b, a);
        RwIm3DVertexSetRGBA(&v3d[1], r, g, b, a);
        RwIm3DVertexSetRGBA(&v3d[2], r, g, b, a);
        RwIm3DVertexSetRGBA(&v3d[3], r, g, b, a);

        RwIm3DVertexSetPos(&v3d[0], vert[0].x, vert[0].y, vert[0].z);
        RwIm3DVertexSetPos(&v3d[1], vert[1].x, vert[1].y, vert[1].z);
        RwIm3DVertexSetPos(&v3d[2], vert[2].x, vert[2].y, vert[2].z);
        RwIm3DVertexSetPos(&v3d[3], vert[3].x, vert[3].y, vert[3].z);

        if (tex != NULL)
        {
            F32 u1;
            F32 u2;
            F32 v1;
            F32 v2;

            u1 = tex->x1 + idx->m_texIdx[0] * tex->unit_width;
            v1 = tex->y1 + idx->m_texIdx[1] * tex->unit_height;
            u2 = tex->x1 + (idx->m_texIdx[0] + 1) * tex->unit_width;
            v2 = tex->y1 + (idx->m_texIdx[1] + 1) * tex->unit_height;

            v3d[0].u = u1;
            v3d[0].v = v1;
            v3d[1].u = u2;
            v3d[1].v = v2;
            v3d[2].u = u2;
            v3d[2].v = v1;
            v3d[3].u = u1;
            v3d[3].v = v2;
        }
        else
        {
            v3d[0].u = 0.0f;
            v3d[0].v = 0.0f;
            v3d[1].u = 1.0f;
            v3d[1].v = 1.0f;
            v3d[2].u = 1.0f;
            v3d[2].v = 0.0f;
            v3d[3].u = 0.0f;
            v3d[3].v = 1.0f;
        }

        U16* src = i3d;
        U16* dst = &gRenderBuffer.m_index[gRenderBuffer.m_indexCount];
        for (S32 i = 0; i < 6; i++)
        {
            dst[i] = gRenderBuffer.m_vertexCount + src[i];
        }

        memcpy((U8*)gRenderBuffer.m_vertex +
                   gRenderBuffer.m_vertexTypeSize * gRenderBuffer.m_vertexCount,
               v3d, gRenderBuffer.m_vertexTypeSize * 4);

        gRenderBuffer.m_indexCount += 6;
        gRenderBuffer.m_vertexCount += 4;
    }

    iRenderFlush();
}

void iParMgrRenderParSys_Flat(void* data, xParGroup* ps)
{
    xPar* idx = ps->m_root;
    RwTexture* texture;
    RwRaster* raster;

    iRenderSetCameraViewMatrix(NULL);

    texture = (RwTexture*)xSTFindAsset(((zParSys*)data)->tasset->textureID, NULL);
    if (texture != NULL)
    {
        raster = texture->raster;
        if (raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        }
    }

    for (; idx != NULL; idx = idx->m_next)
    {
        iRenderPushFlat(idx, ps->m_cmdTex);
    }

    iRenderFlush();
}
