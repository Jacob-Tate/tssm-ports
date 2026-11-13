#include "xShadow.h"

#include "iParMgr.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>

struct _ProjectionParam
{
    RwV3d at;
    RwMatrixTag invMatrix;
    U8 shadowValue;
    S32 fade;
    U32 numIm3DBatch;
    U32 shadowWord;
};

struct ShadowCacheContext
{
    xShadowCache* cache;
    F32 minNormY;
};

struct ShadowCBParam
{
    xShadowCache* cache;
    RpIntersection* isx;
    xVec3 capsuleStart;
    xVec3 capsuleEnd;
    F32 capsuleRadius;
    xEnt* ent;
    RwLine localLine;
    RwV3d localDelta;
    F32 localRadius;
    RwMatrixTag* modelMat;
    RpGeometry* geom;
    U32 polyFound;
    xEnt* rayCloser[5];
};

RpLight* ShadowLight;
RwRGBAReal ShadowLightColor = { 1.0f, 1.0f, 1.0f, 1.0f };
RwV3d gCamPos = { 0.0f, 0.0f, 0.0f };
F32 rscale = 1.0f;
static F32 SHADOW_BOTH;
static RxObjSpace3DVertex* Im3DBuffer;
static U32 Im3DBufferPos;
RwCamera* ShadowCamera;
RwRaster* ShadowCameraRaster;
static RwRaster* ShadowRenderRaster;
F32 gShadowObjectRadius;
static U32 shadow_ent_count;
static xEnt* shadow_ent_list[16];
static S32 sShadowCollJSP;
static U8 sCastEnvironment = 1;
static RwRaster* gc_saveraster;
static xEnt* sEntSelf;
static xShadowMgr* sMgrList;
static S32 sMgrCount;
static S32 sMgrTotal;
static xShadowCache sCacheList[3];

static inline void ShadowCameraDestroy(RwCamera* shadowCamera)
{
    RwRaster* raster;
    RwFrame* frame;

    if (shadowCamera == NULL)
    {
        return;
    }

    _rwFrameSyncDirty();

    frame = RwCameraGetFrame(shadowCamera);
    if (frame != NULL)
    {
        RwCameraSetFrame(shadowCamera, NULL);
        _rwFrameSyncDirty();
        RwFrameDestroy(frame);
    }

    raster = RwCameraGetZRaster(shadowCamera);
    if (raster != NULL)
    {
        RwCameraSetZRaster(shadowCamera, NULL);
        RwRasterDestroy(raster);
    }

    raster = RwCameraGetRaster(shadowCamera);
    if (raster != NULL)
    {
        RwCameraSetRaster(shadowCamera, NULL);
        RwRasterDestroy(raster);
    }

    RwCameraDestroy(shadowCamera);
}

static void GCSaveFrameBuffer();
static void GCRestoreFrameBuffer();
static RwCamera* ShadowCameraCreatePersp(S32 res);
static RwCamera* ShadowCameraSetSpherePersp(RwCamera* camera, RwV3d* center, F32 radius);

static inline RwRaster* ShadowRasterCreate(S32 res)
{
    return RwRasterCreate(res, res, 0, rwRASTERTYPECAMERATEXTURE);
}

static inline S32 SetupShadow()
{
    S32 res = 128;

    while (res > (S32)FB_XRES || res > (S32)FB_YRES)
    {
        res >>= 1;
    }

    ShadowCamera = ShadowCameraCreatePersp(res);
    if (ShadowCamera == NULL)
    {
        return 0;
    }

    ShadowCameraRaster = ShadowRasterCreate(res);
    if (ShadowCameraRaster == NULL)
    {
        return 0;
    }

    RwCameraSetRaster(ShadowCamera, ShadowCameraRaster);
    return 1;
}

static inline U32 xShadowCameraCreate()
{
    return SetupShadow() != 0;
}

static void InvertRaster(RwCamera* cam);

static inline RwCamera* ShadowCameraUpdate(RwCamera* shadowCamera, void* model,
                                           void (*renderCB)(void*), S32 shadowMode)
{
    RwRGBA bgColor = { 255, 255, 255, 0 };
    RwCamera* camera = RwCameraGetCurrentCamera();
    S32 fogstate;

    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fogstate);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

    if (camera != NULL)
    {
        RwCameraEndUpdate(camera);
    }

    GCSaveFrameBuffer();

    shadowCamera->frameBuffer->width--;
    shadowCamera->frameBuffer->height--;
    RwCameraClear(shadowCamera, &bgColor, rwCAMERACLEARIMAGE);
    shadowCamera->frameBuffer->width++;
    shadowCamera->frameBuffer->height++;

    RwFrameOrthoNormalize(RwCameraGetFrame(shadowCamera));

    if (RwCameraBeginUpdateWrapper(shadowCamera) != NULL)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

        renderCB(model);

        if (shadowMode == 0)
        {
            InvertRaster(shadowCamera);
        }

        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);

        RwCameraEndUpdate(shadowCamera);
        RwGameCubeCameraTextureFlush(shadowCamera->frameBuffer, 0);
    }

    if (camera != NULL)
    {
        RwCameraBeginUpdateWrapper(camera);
    }

    GCRestoreFrameBuffer();
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)fogstate);

    return shadowCamera;
}

void xShadowInit()
{
    RwFrame* frame;

    xShadowCameraCreate();

    gc_saveraster = RwRasterCreate(128, 128, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    shadow_ent_count = 0;

    ShadowLight = RpLightCreate(rpLIGHTDIRECTIONAL);
    RpLightSetColor(ShadowLight, &ShadowLightColor);

    frame = RwFrameCreate();
    RpLightSetFrame(ShadowLight, frame);
}

void xShadowSetWorld(RpWorld* world)
{
    RpWorldAddCamera(world, ShadowCamera);
    SHADOW_BOTH = 2.0f;
}

void xShadowSetLight(xVec3* target_pos, xVec3* in_vec, F32)
{
    xVec3 zvec;
    xMat4x3 matrix;
    RwFrame* camFrame;
    RwMatrixTag* camMatrix;

    xVec3Normalize(&zvec, in_vec);
    xMat3x3LookVec(&matrix, &zvec);
    matrix.pos = *target_pos;

    camFrame = RwCameraGetFrame(ShadowCamera);
    camMatrix = RwFrameGetMatrix(camFrame);

    xMat4x3Copy((xMat4x3*)camMatrix, &matrix);
    RwFrameOrthoNormalize(camFrame);
    RwMatrixUpdate(camMatrix);
    RwFrameUpdateObjects(camFrame);
}

void xShadowCameraUpdate(void* model, void (*renderCB)(void*), xVec3* center, F32 radius,
                         S32 shadowMode)
{
    ShadowCameraSetSpherePersp(ShadowCamera, (RwV3d*)center, radius);
    ShadowCameraUpdate(ShadowCamera, model, renderCB, shadowMode);
    ShadowRenderRaster = ShadowCameraRaster;
}

U32 xShadowReceiveShadowSetup(xEnt* ent)
{
    if (ent->model != NULL && xEntIsVisible(ent) && (ent->baseFlags & 0x10) &&
        !xModelCullGroup(ent->model))
    {
        return 1;
    }

    return 0;
}

void xShadowReceiveShadow(xEnt* ent, F32 shadowFactor, S32 shadowMode, RwMatrixTag* shadowMat,
                          RwRaster* shadowRast)
{
    if (!xModelIsPreinstanced(ent->model))
    {
        RwMatrixTag oldroot = { 0 };
        RwMatrixTag invMatrix;
        // vShad/vIn: the output and input of RwV3dTransformPoints (not in the PS2 DWARF).
        RwV3d vShad[3];
        RwV3d vIn[3];
        RwV3d at;
        RwV3d scl;
        RwV3d tr;
        S32 fogstate;

        if (ent->model->Scale.x)
        {
            oldroot = *ent->model->Mat;

            ent->model->Mat->right.x *= ent->model->Scale.x;
            ent->model->Mat->right.y *= ent->model->Scale.x;
            ent->model->Mat->right.z *= ent->model->Scale.x;
            ent->model->Mat->up.x *= ent->model->Scale.y;
            ent->model->Mat->up.y *= ent->model->Scale.y;
            ent->model->Mat->up.z *= ent->model->Scale.y;
            ent->model->Mat->at.x *= ent->model->Scale.z;
            ent->model->Mat->at.y *= ent->model->Scale.z;
            ent->model->Mat->at.z *= ent->model->Scale.z;
        }

        RwCamera* shadowCamera = ShadowCamera;

        if (shadowRast != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, shadowRast);
        }
        else
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwCameraGetRaster(shadowCamera));
        }

        RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fogstate);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

        switch (shadowMode)
        {
        case 1:
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
            break;
        case 0:
        default:
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
            break;
        }

        if (shadowFactor < 0.0f)
        {
            shadowFactor = -shadowFactor;

            switch (shadowMode)
            {
            case 1:
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVSRCALPHA);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCALPHA);
                break;
            case 0:
            default:
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);
                break;
            }
        }
        else
        {
            switch (shadowMode)
            {
            case 1:
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
                break;
            case 0:
            default:
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCCOLOR);
                break;
            }
        }

        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

        RwMatrixTag* shadowMatrix;
        if (shadowMat != NULL)
        {
            shadowMatrix = shadowMat;
        }
        else
        {
            shadowMatrix = RwFrameGetMatrix(RwCameraGetFrame(shadowCamera));
        }

        at = shadowMatrix->at;

        F32 radius = gShadowObjectRadius;
        F32 fadeDist = 0.0f;

        RwMatrixInvert(&invMatrix, shadowMatrix);

        scl.x = scl.y = -0.5f / radius;
        scl.z = 1.0f / (fadeDist + radius);
        RwMatrixScale(&invMatrix, &scl, rwCOMBINEPOSTCONCAT);

        tr.x = tr.y = 0.5f;
        tr.z = 0.0f;
        RwMatrixTranslate(&invMatrix, &tr, rwCOMBINEPOSTCONCAT);

        xModelInstance* model;
        U32 i;
        xVec3* xvert;
        RpTriangle* tri;
        RpGeometry* geom;
        U8 val;
        U32 max_verts = 0;

        for (model = ent->model; model != NULL; model = model->Next)
        {
            if (model->Flags & 0x1)
            {
                U32 num_verts = model->Data->geometry->numVertices;
                if (num_verts > max_verts)
                {
                    max_verts = num_verts;
                }
            }

            if (ent->pflags & 0x40)
            {
                break;
            }
        }

        if (max_verts != 0)
        {
            xvert = (xVec3*)xMemPushTemp(max_verts * sizeof(xVec3));
            if (xvert != NULL)
            {
                Im3DBuffer = gRenderBuffer.m_vertex;

                for (model = ent->model; model != NULL; model = model->Next)
                {
                    if (model->Flags & 0x1)
                    {
                        geom = model->Data->geometry;

                        iModelVertEval(model->Data, 0, geom->numVertices, model->Mat, NULL, xvert);

                        val = 255.0f * shadowFactor;
                        tri = geom->triangles;

                        for (i = 0; i < geom->numTriangles; i++, tri++)
                        {
                            if (Im3DBufferPos > 477)
                            {
                                if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL,
                                                    rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ |
                                                        rwIM3D_VERTEXRGBA))
                                {
                                    RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
                                    RwIm3DEnd();
                                }

                                Im3DBufferPos = 0;
                            }

                            RxObjSpace3DVertex* imv = &Im3DBuffer[Im3DBufferPos];
                            xVec3* v0 = &xvert[tri->vertIndex[0]];
                            xVec3* v1 = &xvert[tri->vertIndex[1]];
                            xVec3* v2 = &xvert[tri->vertIndex[2]];

                            vIn[0] = *(RwV3d*)v0;
                            vIn[1] = *(RwV3d*)v1;
                            vIn[2] = *(RwV3d*)v2;

                            RwV3dTransformPoints(vShad, vIn, 3, &invMatrix);

                            if (vShad[0].z < 0.0f && vShad[1].z < 0.0f && vShad[2].z < 0.0f)
                            {
                                continue;
                            }

                            if (vShad[0].x < 0.0f && vShad[1].x < 0.0f && vShad[2].x < 0.0f)
                            {
                                continue;
                            }

                            if (vShad[0].x > 1.0f && vShad[1].x > 1.0f && vShad[2].x > 1.0f)
                            {
                                continue;
                            }

                            if (vShad[0].y < 0.0f && vShad[1].y < 0.0f && vShad[2].y < 0.0f)
                            {
                                continue;
                            }

                            if (vShad[0].y > 1.0f && vShad[1].y > 1.0f && vShad[2].y > 1.0f)
                            {
                                continue;
                            }

                            // a, b: the edge vectors; c: their cross product.
                            RwV3d a;
                            RwV3d b;
                            RwV3d c;

                            a.x = v1->x - v0->x;
                            a.y = v1->y - v0->y;
                            a.z = v1->z - v0->z;
                            b.x = v2->x - v0->x;
                            b.y = v2->y - v0->y;
                            b.z = v2->z - v0->z;

                            c.x = a.y * b.z - a.z * b.y;
                            c.y = a.z * b.x - a.x * b.z;
                            c.z = a.x * b.y - a.y * b.x;

                            F32 len = RwV3dLength(&c);
                            if (xabs(len) < 1e-05f)
                            {
                                if (len < 0.0f)
                                {
                                    len = -1e-05f;
                                }
                                else
                                {
                                    len = 1e-05f;
                                }
                            }

                            RwV3dScaleMacro(&c, &c, 0.008f / len);

                            if (c.x * at.x + c.y * at.y + c.z * at.z > -0.00069724565f)
                            {
                                continue;
                            }

                            RwIm3DVertexSetPos(&imv[0], v0->x + c.x, v0->y + c.y, v0->z + c.z);
                            RwIm3DVertexSetPos(&imv[1], v1->x + c.x, v1->y + c.y, v1->z + c.z);
                            RwIm3DVertexSetPos(&imv[2], v2->x + c.x, v2->y + c.y, v2->z + c.z);

                            imv[0].u = vShad[0].x;
                            imv[1].u = vShad[1].x;
                            imv[2].u = vShad[2].x;
                            imv[0].v = vShad[0].y;
                            imv[1].v = vShad[1].y;
                            imv[2].v = vShad[2].y;

                            RwIm3DVertexSetRGBA(&imv[0], val, val, val, val);
                            RwIm3DVertexSetRGBA(&imv[1], val, val, val, val);
                            RwIm3DVertexSetRGBA(&imv[2], val, val, val, val);

                            Im3DBufferPos += 3;
                        }

                        if (Im3DBufferPos != 0)
                        {
                            if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL,
                                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ |
                                                    rwIM3D_VERTEXRGBA))
                            {
                                RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
                                RwIm3DEnd();
                            }

                            Im3DBufferPos = 0;
                        }
                    }

                    if (ent->pflags & 0x40)
                    {
                        break;
                    }
                }

                xMemPopTemp(xvert);

                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
                RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)fogstate);
                RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
            }
        }

        if (ent->model->Scale.x)
        {
            *ent->model->Mat = oldroot;
        }
    }
}

void xShadow_ListAdd(xEnt* ent)
{
    xShadowManager_Add(ent);
}

S32 Im2DRenderQuad(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, F32 recipCamZ, F32 uvOffset)
{
    RwIm2DVertex vx[4];

    RwIm2DVertexSetScreenX(&vx[0], x1);
    RwIm2DVertexSetScreenY(&vx[0], y1);
    RwIm2DVertexSetScreenZ(&vx[0], z);
    RwIm2DVertexSetIntRGBA(&vx[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[0], uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[0], uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&vx[1], x1);
    RwIm2DVertexSetScreenY(&vx[1], y2);
    RwIm2DVertexSetScreenZ(&vx[1], z);
    RwIm2DVertexSetIntRGBA(&vx[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[1], uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[1], 1.0f + uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&vx[2], x2);
    RwIm2DVertexSetScreenY(&vx[2], y1);
    RwIm2DVertexSetScreenZ(&vx[2], z);
    RwIm2DVertexSetIntRGBA(&vx[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[2], 1.0f + uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[2], uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&vx[3], x2);
    RwIm2DVertexSetScreenY(&vx[3], y2);
    RwIm2DVertexSetScreenZ(&vx[3], z);
    RwIm2DVertexSetIntRGBA(&vx[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[3], 1.0f + uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[3], 1.0f + uvOffset, recipCamZ);

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vx, 4);

    return 1;
}

static void InvertRaster(RwCamera* cam)
{
    RwIm2DVertex vx[4];
    F32 cam_width = cam->frameBuffer->width;
    F32 cam_height = cam->frameBuffer->height;

    RwIm2DVertexSetScreenX(&vx[0], 0.0f);
    RwIm2DVertexSetScreenY(&vx[0], 0.0f);
    RwIm2DVertexSetScreenZ(&vx[0], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[0], 0.0f, 1.0f);
    RwIm2DVertexSetV(&vx[0], 0.0f, 1.0f);

    RwIm2DVertexSetScreenX(&vx[1], 0.0f);
    RwIm2DVertexSetScreenY(&vx[1], cam_height);
    RwIm2DVertexSetScreenZ(&vx[1], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[1], 0.0f, 1.0f);
    RwIm2DVertexSetV(&vx[1], 1.0f, 1.0f);

    RwIm2DVertexSetScreenX(&vx[2], cam_width);
    RwIm2DVertexSetScreenY(&vx[2], 0.0f);
    RwIm2DVertexSetScreenZ(&vx[2], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[2], 1.0f, 1.0f);
    RwIm2DVertexSetV(&vx[2], 0.0f, 1.0f);

    RwIm2DVertexSetScreenX(&vx[3], cam_width);
    RwIm2DVertexSetScreenY(&vx[3], cam_height);
    RwIm2DVertexSetScreenZ(&vx[3], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[3], 1.0f, 1.0f);
    RwIm2DVertexSetV(&vx[3], 1.0f, 1.0f);

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVDESTCOLOR);
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vx, 4);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
}

static void ShadowRenderCachePolys(_ProjectionParam* param, xShadowPoly* poly, S32 count)
{
    S32 i;
    RxObjSpace3DVertex* imv;
    // xVec4[3] in the PS2 DWARF; here the RwV3d output array of RwV3dTransformPoints (retail's
    // 12-byte stride).
    RwV3d vShad[3];
    RwV3d vIn[3];
    F32 cx;
    F32 cy;
    F32 cz;
    U8 val;

    for (i = 0; i < count; i++, poly++)
    {
        vIn[0].x = poly->vert[0].x;
        vIn[0].y = poly->vert[0].y;
        vIn[0].z = poly->vert[0].z;
        vIn[1].x = poly->vert[1].x;
        vIn[1].y = poly->vert[1].y;
        vIn[1].z = poly->vert[1].z;
        vIn[2].x = poly->vert[2].x;
        vIn[2].y = poly->vert[2].y;
        vIn[2].z = poly->vert[2].z;

        RwV3dTransformPoints(vShad, vIn, 3, &param->invMatrix);

        if (vShad[0].z < 0.0f && vShad[1].z < 0.0f && vShad[2].z < 0.0f)
        {
            continue;
        }

        if (vShad[0].x < 0.0f && vShad[1].x < 0.0f && vShad[2].x < 0.0f)
        {
            continue;
        }

        if (vShad[0].x > 1.0f && vShad[1].x > 1.0f && vShad[2].x > 1.0f)
        {
            continue;
        }

        if (vShad[0].y < 0.0f && vShad[1].y < 0.0f && vShad[2].y < 0.0f)
        {
            continue;
        }

        if (vShad[0].y > 1.0f && vShad[1].y > 1.0f && vShad[2].y > 1.0f)
        {
            continue;
        }

        if (Im3DBufferPos > 477)
        {
            if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
                RwIm3DEnd();
            }

            param->numIm3DBatch++;
            Im3DBufferPos = 0;
        }

        imv = &Im3DBuffer[Im3DBufferPos];

        // The w components of the three verts hold the poly normal
        cx = 0.008f * poly->vert[0].w;
        cy = 0.008f * poly->vert[1].w;
        cz = 0.008f * poly->vert[2].w;

        RwIm3DVertexSetPos(&imv[0], poly->vert[0].x + cx, poly->vert[0].y + cy,
                           poly->vert[0].z + cz);
        RwIm3DVertexSetPos(&imv[1], poly->vert[1].x + cx, poly->vert[1].y + cy,
                           poly->vert[1].z + cz);
        RwIm3DVertexSetPos(&imv[2], poly->vert[2].x + cx, poly->vert[2].y + cy,
                           poly->vert[2].z + cz);

        imv[0].u = vShad[0].x;
        imv[1].u = vShad[1].x;
        imv[2].u = vShad[2].x;
        imv[0].v = vShad[0].y;
        imv[1].v = vShad[1].y;
        imv[2].v = vShad[2].y;

        val = param->shadowValue;
        RwIm3DVertexSetRGBA(&imv[0], val, val, val, val);
        RwIm3DVertexSetRGBA(&imv[1], val, val, val, val);
        RwIm3DVertexSetRGBA(&imv[2], val, val, val, val);

        Im3DBufferPos += 3;
    }
}

// Plain (not inline) functions at their PS2 position (BfBB keeps the same order): auto-inlined
// and stripped at link, but their copies here intern 572.95807f, 0.001745331f, 128.0f and
// 0.00390625f where retail's .sdata2 has them.
static void GCSaveFrameBuffer()
{
    RwGameCubeCameraTextureFlush(gc_saveraster, 0);
}

static void GCRestoreFrameBuffer()
{
    F32 size = 128.0f;
    F32 uvOffset = 0.00390625f;
    F32 recipCamZ = 1.0f / RwCameraGetCurrentCamera()->farPlane;
    S32 oldAlphaFunction;
    S32 oldTextureFilter;

    RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &oldAlphaFunction);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &oldTextureFilter);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gc_saveraster);

    Im2DRenderQuad(0.0f, 0.0f, size, size, RwIm2DGetFarScreenZ(), recipCamZ, uvOffset);

    if (oldAlphaFunction >= rwALPHATESTFUNCTIONNEVER)
    {
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)oldAlphaFunction);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
    }

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    if (oldTextureFilter >= rwFILTERNEAREST)
    {
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)oldTextureFilter);
    }
}

static RwCamera* ShadowCameraCreatePersp(S32 res)
{
    RwCamera* camera = RwCameraCreate();

    if (camera != NULL)
    {
        iCameraPerformRenderWareHack(camera);
    }

    if (camera != NULL)
    {
        RwV2d vw;

        RwCameraSetFrame(camera, RwFrameCreate());

        vw.x = 0.001745331f;
        vw.y = 0.001745331f;
        RwCameraSetViewWindow(camera, &vw);

        if (RwCameraGetFrame(camera) != NULL)
        {
            RwRaster* zRaster = RwRasterCreate(res, res, 0, rwRASTERTYPEZBUFFER);
            if (zRaster != NULL)
            {
                RwCameraSetZRaster(camera, zRaster);
                return camera;
            }
        }
    }

    ShadowCameraDestroy(camera);
    return NULL;
}

static RwCamera* ShadowCameraSetSpherePersp(RwCamera* camera, RwV3d* center, F32 radius)
{
    RwFrame* camFrame = RwCameraGetFrame(camera);
    RwMatrixTag* camMatrix = RwFrameGetMatrix(camFrame);
    RwV3d* camPos = RwMatrixGetPos(camMatrix);
    F32 objDepth = 572.95807f * radius;
    F32 nearZ = objDepth - rscale * radius;
    F32 farZ = objDepth + rscale * radius;

    camera->nearPlane = nearZ;
    camera->farPlane = farZ;
    RwCameraSetNearClipPlane(camera, nearZ);
    RwCameraSetFarClipPlane(camera, farZ);

    *camPos = *center;
    RwV3dIncrementScaledMacro(camPos, RwMatrixGetAt(camMatrix), -objDepth);
    gCamPos = *camPos;

    RwMatrixUpdate(camMatrix);
    RwFrameUpdateObjects(camFrame);

    gShadowObjectRadius = radius;

    return camera;
}

static RpCollisionTriangle* shadowCacheEnvCB(RpIntersection* intersection, RpWorldSector* sector,
                                             RpCollisionTriangle* tri, F32 distance, void* data)
{
    ShadowCacheContext* context = (ShadowCacheContext*)data;
    xShadowCache* cache = context->cache;
    xShadowPoly* poly;
    F32 dydx;
    F32 dydz;
    F32 depth0;
    F32 n0x;
    F32 n0z;
    F32 n0d;
    F32 n1x;
    F32 n1z;
    F32 n1d;
    F32 n2x;
    F32 n2z;
    F32 n2d;

    if (cache->polyCount >= 256)
    {
        return NULL;
    }

    if (tri->normal.y < context->minNormY)
    {
        return tri;
    }

    if (sShadowCollJSP && !(((xClumpCollBSPTriangle*)tri->index)->flags & 0x8))
    {
        return tri;
    }

    poly = &cache->poly[cache->polyCount];
    cache->polyCount++;

    *(xVec3*)&poly->vert[0] = *(xVec3*)tri->vertices[0];
    *(xVec3*)&poly->vert[1] = *(xVec3*)tri->vertices[1];
    *(xVec3*)&poly->vert[2] = *(xVec3*)tri->vertices[2];

    // The w components of the three verts hold the poly normal
    poly->vert[0].w = tri->normal.x;
    poly->vert[1].w = tri->normal.y;
    poly->vert[2].w = tri->normal.z;

    dydx = -tri->normal.x / tri->normal.y;
    dydz = -tri->normal.z / tri->normal.y;
    depth0 = poly->vert[0].y +
             (dydx * (cache->pos.x - poly->vert[0].x) + dydz * (cache->pos.z - poly->vert[0].z));
    n0x = poly->vert[0].z - poly->vert[1].z;
    n0z = poly->vert[1].x - poly->vert[0].x;
    n0d = n0x * (cache->pos.x - poly->vert[0].x) + n0z * (cache->pos.z - poly->vert[0].z);
    n1x = poly->vert[1].z - poly->vert[2].z;
    n1z = poly->vert[2].x - poly->vert[1].x;
    n1d = n1x * (cache->pos.x - poly->vert[1].x) + n1z * (cache->pos.z - poly->vert[1].z);
    n2x = poly->vert[2].z - poly->vert[0].z;
    n2z = poly->vert[0].x - poly->vert[2].x;
    n2d = n2x * (cache->pos.x - poly->vert[2].x) + n2z * (cache->pos.z - poly->vert[2].z);

    if (n0d <= 1e-5f && n1d <= 1e-5f && n2d <= 1e-5f)
    {
        cache->polyRayDepth[0] = MAX(depth0, cache->polyRayDepth[0]);
    }

    if (0.5f * n0x * cache->radius + n0d <= 1e-5f && 0.5f * n1x * cache->radius + n1d <= 1e-5f &&
        0.5f * n2x * cache->radius + n2d <= 1e-5f)
    {
        cache->polyRayDepth[1] = MAX(0.5f * dydx * cache->radius + depth0, cache->polyRayDepth[1]);
    }

    if (n0d - 0.5f * n0x * cache->radius <= 1e-5f && n1d - 0.5f * n1x * cache->radius <= 1e-5f &&
        n2d - 0.5f * n2x * cache->radius <= 1e-5f)
    {
        cache->polyRayDepth[2] = MAX(depth0 - 0.5f * dydx * cache->radius, cache->polyRayDepth[2]);
    }

    if (0.5f * n0z * cache->radius + n0d <= 1e-5f && 0.5f * n1z * cache->radius + n1d <= 1e-5f &&
        0.5f * n2z * cache->radius + n2d <= 1e-5f)
    {
        cache->polyRayDepth[3] = MAX(0.5f * dydz * cache->radius + depth0, cache->polyRayDepth[3]);
    }

    if (n0d - 0.5f * n0z * cache->radius <= 1e-5f && n1d - 0.5f * n1z * cache->radius <= 1e-5f &&
        n2d - 0.5f * n2z * cache->radius <= 1e-5f)
    {
        cache->polyRayDepth[4] = MAX(depth0 - 0.5f * dydz * cache->radius, cache->polyRayDepth[4]);
    }

    return tri;
}

static S32 shadowCacheLeafCB(S32 numTriangles, S32 triOffset, void* data)
{
    ShadowCBParam* cbparam = (ShadowCBParam*)data;
    xShadowCache* cache = cbparam->cache;
    RpGeometry* geometry = cbparam->geom;
    RwV3d* vertices = geometry->morphTarget->verts;
    RpTriangle* triangles = geometry->triangles;
    S32 triSlot;
    U16* triIndex = RpCollTreeGetEntryMap(RpCollisionGeometryGetCollTree(geometry));

    if (triIndex)
    {
        triIndex += triOffset;
        triSlot = 0;
    }
    else
    {
        triSlot = triOffset;
    }

    while (numTriangles--)
    {
        S32 i;
        RpTriangle* tri;
        S32 vertIndex0;
        S32 vertIndex1;
        S32 vertIndex2;
        RwV3d* v0;
        RwV3d* v1;
        RwV3d* v2;
        xVec3 worldV[3];
        F32 posX;
        F32 posZ;

        if (triIndex)
        {
            triSlot = *triIndex++;
        }
        else
        {
            triSlot++;
        }

        tri = &triangles[triSlot];
        vertIndex0 = tri->vertIndex[0];
        vertIndex1 = tri->vertIndex[1];
        vertIndex2 = tri->vertIndex[2];
        v0 = &vertices[vertIndex0];
        v1 = &vertices[vertIndex1];
        v2 = &vertices[vertIndex2];

        xMat4x3Toworld(&worldV[0], (xMat4x3*)cbparam->modelMat, (xVec3*)v0);
        xMat4x3Toworld(&worldV[1], (xMat4x3*)cbparam->modelMat, (xVec3*)v1);
        xMat4x3Toworld(&worldV[2], (xMat4x3*)cbparam->modelMat, (xVec3*)v2);

        posX = cbparam->capsuleStart.x;
        posZ = cbparam->capsuleStart.z;

        // Reject the triangle if the capsule lies entirely outside one of its edges (XZ plane).
        // goto (both loops): retail jumps straight to the next triangle.
        for (i = 0; i < 3; i++)
        {
            xVec3* vert0 = &worldV[i];
            xVec3* vert1 = &worldV[(i == 2) ? 0 : i + 1];
            F32 nx = vert0->z - vert1->z;
            F32 nz = vert1->x - vert0->x;
            F32 nmag2 = nx * nx + nz * nz;
            F32 pdot = nx * (posX - vert0->x) + nz * (posZ - vert0->z);

            if (pdot > 0.0f && pdot * pdot >= nmag2 * SQR(cbparam->capsuleRadius))
            {
                goto nextTri;
            }
        }

        // Reject the triangle if the capsule lies outside the radius of one of its corners
        for (i = 0; i < 3; i++)
        {
            xVec3* vert0 = &worldV[(i + 1) % 3];
            xVec3* vert1 = &worldV[(i + 2) % 3];
            F32 dotA = (vert0->x - worldV[i].x) * (posX - worldV[i].x) +
                       (vert0->z - worldV[i].z) * (posZ - worldV[i].z);
            F32 dotB = (vert1->x - worldV[i].x) * (posX - worldV[i].x) +
                       (vert1->z - worldV[i].z) * (posZ - worldV[i].z);

            if (dotA < 0.0f && dotB < 0.0f &&
                SQR(posX - worldV[i].x) + SQR(posZ - worldV[i].z) > SQR(cbparam->capsuleRadius))
            {
                goto nextTri;
            }
        }

        cbparam->polyFound++;

        {
            xVec3* vert0 = (xVec3*)v0;
            xVec3* vert1 = (xVec3*)v1;
            xVec3* vert2 = (xVec3*)v2;
            xVec3 aa;
            xVec3 bb;
            xVec3 trinorm;
            F32 depthtest;
            F32 dydx;
            F32 dydz;
            F32 depth0;
            F32 n0x;
            F32 n0z;
            F32 n0d;
            F32 n1x;
            F32 n1z;
            F32 n1d;
            F32 n2x;
            F32 n2z;
            F32 n2d;
            F32 denom;

            xVec3Sub(&aa, vert1, vert0);
            xVec3Sub(&bb, vert2, vert0);
            xVec3Cross(&trinorm, &aa, &bb);
            xVec3Normalize(&trinorm, &trinorm);

            denom = (xabs(trinorm.y) > 1e-5f) ? trinorm.y : 1e-5f;

            dydx = -trinorm.x / denom;
            dydz = -trinorm.z / denom;
            depth0 = worldV[0].y +
                     (dydx * (cache->pos.x - worldV[0].x) + dydz * (cache->pos.z - worldV[0].z));
            n0x = worldV[0].z - worldV[1].z;
            n0z = worldV[1].x - worldV[0].x;
            n0d = n0x * (cache->pos.x - worldV[0].x) + n0z * (cache->pos.z - worldV[0].z);
            n1x = worldV[1].z - worldV[2].z;
            n1z = worldV[2].x - worldV[1].x;
            n1d = n1x * (cache->pos.x - worldV[1].x) + n1z * (cache->pos.z - worldV[1].z);
            n2x = worldV[2].z - worldV[0].z;
            n2z = worldV[0].x - worldV[2].x;
            n2d = n2x * (cache->pos.x - worldV[2].x) + n2z * (cache->pos.z - worldV[2].z);

            // Center ray
            if (n0d <= 1e-5f && n1d <= 1e-5f && n2d <= 1e-5f && depth0 > cache->polyRayDepth[0])
            {
                cbparam->rayCloser[0] = cbparam->ent;
                cache->polyRayDepth[0] = depth0;
            }

            // +X ray
            if (0.5f * n0x * cache->radius + n0d <= 1e-5f &&
                0.5f * n1x * cache->radius + n1d <= 1e-5f &&
                0.5f * n2x * cache->radius + n2d <= 1e-5f)
            {
                depthtest = 0.5f * dydx * cache->radius + depth0;
                if (depthtest > cache->polyRayDepth[1])
                {
                    cbparam->rayCloser[1] = cbparam->ent;
                    cache->polyRayDepth[1] = depthtest;
                }
            }

            // -X ray
            if (n0d - 0.5f * n0x * cache->radius <= 1e-5f &&
                n1d - 0.5f * n1x * cache->radius <= 1e-5f &&
                n2d - 0.5f * n2x * cache->radius <= 1e-5f)
            {
                depthtest = depth0 - 0.5f * dydx * cache->radius;
                if (depthtest > cache->polyRayDepth[2])
                {
                    cbparam->rayCloser[2] = cbparam->ent;
                    cache->polyRayDepth[2] = depthtest;
                }
            }

            // +Z ray
            if (0.5f * n0z * cache->radius + n0d <= 1e-5f &&
                0.5f * n1z * cache->radius + n1d <= 1e-5f &&
                0.5f * n2z * cache->radius + n2d <= 1e-5f)
            {
                depthtest = 0.5f * dydz * cache->radius + depth0;
                if (depthtest > cache->polyRayDepth[3])
                {
                    cbparam->rayCloser[3] = cbparam->ent;
                    cache->polyRayDepth[3] = depthtest;
                }
            }

            // -Z ray
            if (n0d - 0.5f * n0z * cache->radius <= 1e-5f &&
                n1d - 0.5f * n1z * cache->radius <= 1e-5f &&
                n2d - 0.5f * n2z * cache->radius <= 1e-5f)
            {
                depthtest = depth0 - 0.5f * dydz * cache->radius;
                if (depthtest > cache->polyRayDepth[4])
                {
                    cbparam->rayCloser[4] = cbparam->ent;
                    cache->polyRayDepth[4] = depthtest;
                }
            }
        }

    nextTri:;
    }

    return 1;
}

static S32 shadowCacheEntityCB(xEnt* ent, void* cbdata)
{
    ShadowCBParam* cbparam = (ShadowCBParam*)cbdata;
    xCollis coll;
    RpCollisionData* colldata;
    RwMatrixTag inverseLTM;

    if (!(ent->baseFlags & 0x10))
    {
        return 1;
    }

    if (cbparam->cache->entCount >= 16)
    {
        return 0;
    }

    if (ent == sEntSelf)
    {
        return 1;
    }

    coll.flags = 0;

    if (ent->bound.type == XBOUND_TYPE_SPHERE)
    {
        xBoxHitsSphere((xBox*)&cbparam->isx->t.box, &ent->bound.sph, &coll);
    }
    else if (ent->bound.type == XBOUND_TYPE_OBB)
    {
        xBoxHitsObb((xBox*)&cbparam->isx->t.box, &ent->bound.box.box, ent->bound.mat, &coll);
    }
    else if (ent->bound.type == XBOUND_TYPE_BOX &&
             cbparam->isx->t.box.sup.x > ent->bound.box.box.lower.x &&
             cbparam->isx->t.box.sup.y > ent->bound.box.box.lower.y &&
             cbparam->isx->t.box.sup.z > ent->bound.box.box.lower.z &&
             cbparam->isx->t.box.inf.x < ent->bound.box.box.upper.x &&
             cbparam->isx->t.box.inf.y < ent->bound.box.box.upper.y &&
             cbparam->isx->t.box.inf.z < ent->bound.box.box.upper.z)
    {
        coll.flags |= 0x1;
    }

    if (coll.flags & 0x1)
    {
        xModelInstance* model = ent->collModel ? ent->collModel : ent->model;

        colldata = _rpCollisionGeometryGetDataMacro(model->Data->geometry);

        if (model->Data->boundingSphere.radius > 2.0f && colldata != NULL && colldata->tree != NULL)
        {
            RwMatrixInvert(&inverseLTM, model->Mat);
            RwV3dTransformPoints((RwV3d*)&cbparam->localLine, (RwV3d*)&cbparam->capsuleStart, 2,
                                 &inverseLTM);

            cbparam->localDelta.x = cbparam->localLine.end.x - cbparam->localLine.start.x;
            cbparam->localDelta.y = cbparam->localLine.end.y - cbparam->localLine.start.y;
            cbparam->localDelta.z = cbparam->localLine.end.z - cbparam->localLine.start.z;
            cbparam->localRadius = cbparam->capsuleRadius * xVec3Length((xVec3*)&inverseLTM.right);
            cbparam->geom = model->Data->geometry;
            cbparam->modelMat = model->Mat;
            cbparam->polyFound = 0;
            cbparam->ent = ent;

            memset(cbparam->rayCloser, 0, sizeof(cbparam->rayCloser));

            RpCollTreeForAllCapsuleIntsec(colldata->tree, &cbparam->localLine, cbparam->localRadius,
                                          shadowCacheLeafCB, cbparam);

            if (cbparam->polyFound != 0)
            {
                cbparam->cache->ent[cbparam->cache->entCount++] = ent;
            }
        }
        else
        {
            cbparam->cache->ent[cbparam->cache->entCount++] = ent;
        }
    }

    return 1;
}

void xShadowVertical_FillCache(xShadowCache* cache, xVec3* pos, F32 r, F32 depth, F32 minNormY)
{
    S32 i;
    S32 j;
    xEnv* env;
    RpIntersection isx;
    ShadowCacheContext context;
    F32 sortRayDepth[5];
    F32 objMaxDepth;
    xQCData qcd;
    ShadowCBParam cbparam;

    cache->pos = *pos;
    cache->radius = r;
    cache->entCount = 0;
    cache->polyCount = 0;
    cache->polyRayDepth[0] = -1e38f;
    cache->polyRayDepth[1] = -1e38f;
    cache->polyRayDepth[2] = -1e38f;
    cache->polyRayDepth[3] = -1e38f;
    cache->polyRayDepth[4] = -1e38f;

    env = xglobals->sceneCur->env;

    isx.type = rpINTERSECTBOX;
    isx.t.box.sup.x = pos->x + r;
    isx.t.box.sup.y = pos->y + r;
    isx.t.box.sup.z = pos->z + r;
    isx.t.box.inf.x = pos->x - r;
    isx.t.box.inf.y = pos->y - r - depth;
    isx.t.box.inf.z = pos->z - r;

    context.cache = cache;
    context.minNormY = minNormY;

    if (sCastEnvironment)
    {
        S32 i;

        sShadowCollJSP = 1;

        for (i = 0; i < env->geom->jsp_count; i++)
        {
            if (env->geom->jsp_visibilityCount[i] > 0)
            {
                xClumpColl_ForAllIntersections(env->geom->jsp_list[i]->colltree, &isx,
                                               shadowCacheEnvCB, &context);
            }
        }

        sShadowCollJSP = 0;
    }

    memcpy(sortRayDepth, cache->polyRayDepth, sizeof(sortRayDepth));

    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (sortRayDepth[j] > sortRayDepth[j + 1])
            {
                F32 tmp = sortRayDepth[j];
                sortRayDepth[j] = sortRayDepth[j + 1];
                sortRayDepth[j + 1] = tmp;
            }
        }
    }

    objMaxDepth = sortRayDepth[2];
    if (objMaxDepth == -1e38f)
    {
        objMaxDepth = pos->y - depth;
    }

    if (objMaxDepth > pos->y - 0.001f)
    {
        objMaxDepth = pos->y - 0.001f;
    }

    if (objMaxDepth - 1.0f > isx.t.box.inf.y)
    {
        isx.t.box.inf.y = objMaxDepth - 1.0f;
    }

    cbparam.cache = cache;
    cbparam.isx = &isx;
    cbparam.capsuleStart.x = pos->x;
    cbparam.capsuleStart.y = pos->y;
    cbparam.capsuleStart.z = pos->z;
    cbparam.capsuleEnd.x = cbparam.capsuleStart.x;
    cbparam.capsuleEnd.y = objMaxDepth;
    cbparam.capsuleEnd.z = cbparam.capsuleStart.z;
    cbparam.capsuleRadius = r;

    xQuickCullForBox(&qcd, (xBox*)&isx.t.box);

    xGridCheckPosition(&colls_grid, (xVec3*)&isx.t.box.sup, &qcd, shadowCacheEntityCB, &cbparam);
    xGridCheckPosition(&colls_oso_grid, (xVec3*)&isx.t.box.sup, &qcd, shadowCacheEntityCB,
                       &cbparam);
    xGridCheckPosition(&npcs_grid, (xVec3*)&isx.t.box.sup, &qcd, shadowCacheEntityCB, &cbparam);

    cache->castOnEnt = cache->entCount != 0;
    cache->castOnPoly = cache->polyCount != 0;
}

void xShadowVertical_DrawCache(xShadowCache* cache, F32 shadowFactor, F32 fadeDist, S32 shadowMode,
                               RwMatrixTag* shadowMat, RwRaster* shadowRast)
{
    _ProjectionParam param;
    RwMatrixTag* shadowMatrix;
    F32 radius;
    RwV3d scl;
    RwV3d tr;
    xVec3 A;
    xVec3 B;
    S32 fogstate;

    if (shadowRast != NULL)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, shadowRast);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwCameraGetRaster(ShadowCamera));
    }

    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

    switch (shadowMode)
    {
    case 1:
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        break;
    case 0:
    default:
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
        break;
    }

    if (shadowFactor < 0.0f)
    {
        shadowFactor = -shadowFactor;

        switch (shadowMode)
        {
        case 1:
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVSRCALPHA);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCALPHA);
            break;
        case 0:
        default:
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);
            break;
        }
    }
    else
    {
        switch (shadowMode)
        {
        case 1:
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
            break;
        case 0:
        default:
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCCOLOR);
            break;
        }
    }

    if (shadowMat != NULL)
    {
        shadowMatrix = shadowMat;
    }
    else
    {
        shadowMatrix = RwFrameGetMatrix(RwCameraGetFrame(ShadowCamera));
    }

    param.at = shadowMatrix->at;

    radius = gShadowObjectRadius;

    RwMatrixInvert(&param.invMatrix, shadowMatrix);

    scl.x = scl.y = -0.5f / radius;
    scl.z = 1.0f / (fadeDist + radius);
    RwMatrixScale(&param.invMatrix, &scl, rwCOMBINEPOSTCONCAT);

    param.fade = (fadeDist > 0.0f) ? 1 : 0;

    param.shadowValue = 255.0f * shadowFactor;
    param.shadowWord = (param.shadowValue << 24) | (param.shadowValue << 16) |
                       (param.shadowValue << 8) | param.shadowValue;

    param.numIm3DBatch = 0;

    Im3DBuffer = gRenderBuffer.m_vertex;
    Im3DBufferPos = 0;

    xVec3Add(&A, (xVec3*)&shadowMatrix->pos, (xVec3*)&shadowMatrix->at);
    RwV3dTransformPoints((RwV3d*)&B, (RwV3d*)&A, 1, &param.invMatrix);

    tr.x = tr.y = 0.5f;
    tr.z = 0.0f;
    RwMatrixTranslate(&param.invMatrix, &tr, rwCOMBINEPOSTCONCAT);

    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fogstate);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

    ShadowRenderCachePolys(&param, cache->poly, cache->polyCount);

    if (Im3DBufferPos != 0)
    {
        if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL,
                            rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
            RwIm3DEnd();
        }

        Im3DBufferPos = 0;
    }

    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)fogstate);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
}

void xShadowManager_Init(S32 numEnts, S32)
{
    sMgrList = (xShadowMgr*)xMemAlloc(gActiveHeap, numEnts * sizeof(xShadowMgr), 0);
    sMgrTotal = numEnts;
    sMgrCount = 0;
}

void xShadowManager_Add(xEnt* ent)
{
    S32 i;

    for (i = 0; i < sMgrCount; i++)
    {
        if (sMgrList[i].ent == ent)
        {
            return;
        }
    }

    if (sMgrCount < sMgrTotal)
    {
        sMgrList[sMgrCount].ent = ent;
        sMgrList[sMgrCount].cache = NULL;
        sMgrList[sMgrCount].priority = 1000;
        sMgrList[sMgrCount].cacheReady = 0;
        sMgrCount++;
    }
}

void xShadowManager_Remove(xEnt* ent)
{
    S32 i;

    for (i = 3; i < sMgrCount; i++)
    {
        sMgrList[i].cache = NULL;
    }

    i = 0;
    while (i < sMgrCount)
    {
        if (ent == sMgrList[i].ent)
        {
            sMgrList[i] = sMgrList[sMgrCount - 1];
            sMgrCount--;
        }
        else
        {
            i++;
        }
    }
}

static S32 CmpShadowMgr(const void* a, const void* b)
{
    xEnt* entA = ((const xShadowMgr*)a)->ent;
    xEnt* entB = ((const xShadowMgr*)b)->ent;
    S32 isPlayerA = (entA->baseType == eBaseTypePlayer || entA->baseType == eBaseTypeBoulder);
    S32 isPlayerB = (entB->baseType == eBaseTypePlayer || entB->baseType == eBaseTypeBoulder);
    F32 distA;
    F32 distB;
    xVec3* campos;

    if (isPlayerA && !isPlayerB)
    {
        return -1;
    }

    if (isPlayerB && !isPlayerA)
    {
        return 1;
    }

    campos = &xglobals->cam->mat.pos;

    {
        F32 dx__ = campos->x - entA->model->Mat->pos.x;
        F32 dy__ = campos->y - entA->model->Mat->pos.y;
        F32 dz__ = campos->z - entA->model->Mat->pos.z;
        distA = SQR(dx__) + SQR(dy__) + SQR(dz__);
    }

    {
        F32 dx__ = campos->x - entB->model->Mat->pos.x;
        F32 dy__ = campos->y - entB->model->Mat->pos.y;
        F32 dz__ = campos->z - entB->model->Mat->pos.z;
        distB = SQR(dx__) + SQR(dy__) + SQR(dz__);
    }

    if (distA < distB)
    {
        return -1;
    }

    return distA > distB;
}

static void xShadowModelRender(xModelInstance* minst)
{
    xMat3x3 tmpmat;
    S32 i;
    RpMaterialList* matList;
    RwTexture* oldtex[8];

    while (minst != NULL)
    {
        if ((minst->Flags & 0x401) == 0x1)
        {
            if (minst->Scale.x)
            {
                tmpmat = *(xMat3x3*)minst->Mat;

                minst->Mat->right.x *= minst->Scale.x;
                minst->Mat->right.y *= minst->Scale.x;
                minst->Mat->right.z *= minst->Scale.x;
                minst->Mat->up.x *= minst->Scale.y;
                minst->Mat->up.y *= minst->Scale.y;
                minst->Mat->up.z *= minst->Scale.y;
                minst->Mat->at.x *= minst->Scale.z;
                minst->Mat->at.y *= minst->Scale.z;
                minst->Mat->at.z *= minst->Scale.z;
            }

            matList = &minst->Data->geometry->matList;

            if (matList->numMaterials <= 8)
            {
                for (i = 0; i < matList->numMaterials; i++)
                {
                    oldtex[i] = matList->materials[i]->texture;
                    matList->materials[i]->texture = NULL;
                }
            }

            if (minst->BoneCount)
            {
                iModelSetWorldBoundFromBox(minst->Data, &minst->animBound);
            }
            else
            {
                iModelWorldBoundSphere(minst->Data, minst->Mat, NULL);
            }

            iModelRender(minst->Data, minst->Mat);

            if (matList->numMaterials <= 8)
            {
                for (i = 0; i < matList->numMaterials; i++)
                {
                    matList->materials[i]->texture = oldtex[i];
                }
            }

            if (minst->Scale.x)
            {
                *(xMat3x3*)minst->Mat = tmpmat;
            }
        }

        minst = minst->Next;
    }
}

static void xShadow_PickByRayCast(xShadowMgr* mgr);
static void xShadow_PickEntForNPC(xShadowMgr* mgr);

void xShadowManager_Render()
{
    S32 i;
    // [6] in the PS2 DWARF: the GC build has 3 shadow caches (the loops below and the frame size).
    S32 cacheUsed[3];
    S32 j;
    F32 shadow_strength;
    U8 old_xClumpColl_FilterFlags;

    old_xClumpColl_FilterFlags = xClumpColl_FilterFlags;
    xClumpColl_FilterFlags |= 0x20;

    for (i = 3; i < sMgrCount; i++)
    {
        sMgrList[i].cache = NULL;
    }

    i = 0;
    while (i < sMgrCount)
    {
        xEnt* ent_ref = sMgrList[i].ent;
        if (!xEntIsVisible(ent_ref) || ent_ref->entShadow == NULL ||
            (ent_ref->model->Flags & 0x400))
        {
            sMgrList[i] = sMgrList[sMgrCount - 1];
            sMgrCount--;
        }
        else
        {
            i++;
        }
    }

    qsort(sMgrList, sMgrCount, sizeof(xShadowMgr), CmpShadowMgr);

    memset(cacheUsed, 0, sizeof(cacheUsed));

    for (i = 0; i < 3 && i < sMgrCount; i++)
    {
        if (sMgrList[i].cache != NULL)
        {
            cacheUsed[sMgrList[i].cache - sCacheList] = 1;
        }
    }

    for (i = 0; i < 3 && i < sMgrCount; i++)
    {
        if (sMgrList[i].cache == NULL)
        {
            for (j = 0; j < 3; j++)
            {
                if (cacheUsed[j] == 0)
                {
                    sMgrList[i].cache = &sCacheList[j];
                    sMgrList[i].cacheReady = 0;
                    sMgrList[i].priority = 1000;
                    sCacheList[j].entCount = 0;
                    sCacheList[j].polyCount = 0;
                    cacheUsed[j] = 1;
                    break;
                }
            }
        }
    }

    for (i = 3; i < sMgrCount; i++)
    {
        sMgrList[i].cache = NULL;
    }

    {
        S32 bestIndex = -1;
        S32 foundPriority = -1;

        for (i = 0; i < 3 && i < sMgrCount; i++)
        {
            sMgrList[i].priority++;
            if (sMgrList[i].priority > foundPriority)
            {
                bestIndex = i;
                foundPriority = sMgrList[i].priority;
            }
        }

        if (bestIndex != -1)
        {
            xVec3 center;
            F32 radius;
            xShadowMgr* mgr_best;
            F32 dst_depth;
            xEnt* ep;

            zEntGetShadowParams(sMgrList[bestIndex].ent, &center, &radius, &shadow_strength,
                                xEntShadow::RADIUS_CACHE);

            mgr_best = &sMgrList[bestIndex];
            ep = mgr_best->ent;

            dst_depth = 10.0f;
            if (ep->entShadow->dst_cast > 0.0f)
            {
                dst_depth = ep->entShadow->dst_cast;
            }

            if (ep->baseType == eBaseTypeNPC && ep->entShadow->flg_disableEnvCast)
            {
                sCastEnvironment = 0;
            }

            sEntSelf = ep;
            xShadowVertical_FillCache(mgr_best->cache, &center, radius, dst_depth, 0.0871557f);
            sEntSelf = NULL;
            sCastEnvironment = 1;

            sMgrList[bestIndex].priority = 0;
            sMgrList[bestIndex].cacheReady = 1;

            xShadow_PickEntForNPC(mgr_best);
        }
    }

    for (i = 0; i < sMgrCount; i++)
    {
        xEnt* ent = sMgrList[i].ent;
        F32 dst_depth;
        S32 shadowOutside;

        if (ent->flags & 0x40)
        {
            continue;
        }

        dst_depth = 10.0f;
        if (ent->entShadow->dst_cast > 0.0f)
        {
            dst_depth = ent->entShadow->dst_cast;
        }

        xModelCullGroupPlusShadow(ent->model, dst_depth, &shadowOutside);

        if (shadowOutside != 0)
        {
            continue;
        }

        if (sMgrList[i].cache != NULL && sMgrList[i].cacheReady != 0)
        {
            xVec3 center;
            F32 radius;

            ent->entShadow->pos.x = ent->model->Mat->pos.x;
            ent->entShadow->pos.y = 1.0f + ent->model->Mat->pos.y;
            ent->entShadow->pos.z = ent->model->Mat->pos.z;
            ent->entShadow->vec.x = 0.0f;
            ent->entShadow->vec.y = -1.0f;
            ent->entShadow->vec.z = 0.0f;

            xShadowSetLight(&ent->entShadow->pos, &ent->entShadow->vec, 1.0f);

            zEntGetShadowParams(ent, &center, &radius, &shadow_strength, xEntShadow::RADIUS_RASTER);

            if (radius <= 0.0f)
            {
                continue;
            }

            RpAtomic* old_model = NULL;
            xModelInstance* old_mnext = ent->model->Next;
            RpAtomic* shadModel = ent->entShadow->shadowModel;

            if (shadModel != NULL)
            {
                old_model = ent->model->Data;
                ent->model->Data = shadModel;
                ent->model->Next = NULL;
            }

            xLightKit_Enable(NULL, xglobals->currWorld);
            xShadowCameraUpdate(ent->model, (void (*)(void*))xShadowModelRender, &center, radius,
                                0);

            if (old_model != NULL)
            {
                ent->model->Data = old_model;
                ent->model->Next = old_mnext;
            }

            xShadowVertical_DrawCache(sMgrList[i].cache, shadow_strength, 0.0f, 0, NULL, NULL);

            if (i == 0 || sMgrList[i].ent->baseType == eBaseTypeBoulder)
            {
                for (j = 0; j < sMgrList[i].cache->entCount; j++)
                {
                    xEnt* ep = sMgrList[i].cache->ent[j];
                    if (xShadowReceiveShadowSetup(ep))
                    {
                        xShadowReceiveShadow(ep, 0.3f, 0, NULL, NULL);
                    }
                }
            }
            else
            {
                xShadowMgr* mgr = &sMgrList[i];
                xEntShadow* npcshad;
                S32 num;

                if (mgr->ent->baseType == eBaseTypeNPC && mgr->cache->entCount >= 1 &&
                    (npcshad = mgr->ent->entShadow) != NULL)
                {
                    if (npcshad->flg_castOnAllDFF)
                    {
                        num = mgr->cache->entCount;
                    }
                    else if (npcshad->flg_castOnOneDFF)
                    {
                        num = 1;
                    }
                    else
                    {
                        continue;
                    }

                    for (S32 a = 0; a < num; a++)
                    {
                        xEnt* ep = mgr->cache->ent[a];
                        if (xShadowReceiveShadowSetup(ep))
                        {
                            xShadowReceiveShadow(ep, 0.3f, 0, NULL, NULL);
                        }
                    }
                }
            }
        }
        else if (ent != NULL && ent->simpShadow != NULL)
        {
            F32 rad;

            if (ent->baseType == eBaseTypeBoulder)
            {
                rad = ent->model->Data->boundingSphere.radius;
                if (rad > 0.75f)
                {
                    rad = 0.75f;
                }
                rad *= 2.0f;
            }
            else if (ent->simpShadow->radiusOptional > 0.0f)
            {
                rad = ent->simpShadow->radiusOptional;
            }
            else if (ent->bound.type == XBOUND_TYPE_SPHERE)
            {
                rad = ent->bound.sph.r;
            }
            else
            {
                xVec3* lower = &ent->bound.box.box.lower;
                xVec3* upper = &ent->bound.box.box.upper;
                rad = 0.167f * (upper->x + upper->y + upper->z - lower->x - lower->y - lower->z);
            }

            xShadowSimple_Add_Expert(ent->simpShadow, (xMat4x3*)ent->model->Mat,
                                     ent->simpShadow->alpha, rad, 10.0f, 1.0f);
        }
    }

    xClumpColl_FilterFlags = old_xClumpColl_FilterFlags;
}

// Dead-stripped in retail; body from Ratatouille's GC builds, where it is live (defined there
// next to xShadow_PickByBounds). Its later eBaseTypeNPCEntity case and its asserts are left out.
static void xShadow_PickByBaseType(xShadowMgr* mgr)
{
    xEnt* best_dyn = NULL;
    S32 idx_dyn = -1;
    xEnt* best_stat = NULL;
    S32 idx_stat = -1;
    xEnt* best_other = NULL;
    S32 idx_other = -1;
    S32 num;
    S32 i;
    xEnt* ep;

    num = mgr->cache->entCount;
    for (i = 0; i < num; i++)
    {
        ep = mgr->cache->ent[i];

        switch (ep->baseType)
        {
        case eBaseTypePlatform:
        case eBaseTypeDynamic:
            if (!best_dyn)
            {
                best_dyn = ep;
                idx_dyn = i;
            }
            break;
        case eBaseTypeStatic:
            if (!best_dyn && !best_stat)
            {
                best_stat = ep;
                idx_stat = i;
            }
            break;
        case eBaseTypeNPC:
            break;
        default:
            if (!best_dyn && !best_stat && !best_other)
            {
                best_other = ep;
                idx_other = i;
            }
            break;
        }
    }

    if (idx_dyn != 0)
    {
        if (idx_dyn > 0)
        {
            mgr->cache->ent[idx_dyn] = mgr->cache->ent[0];
            mgr->cache->ent[0] = best_dyn;
        }
        else if (idx_stat != 0)
        {
            if (idx_stat > 0)
            {
                mgr->cache->ent[idx_stat] = mgr->cache->ent[0];
                mgr->cache->ent[0] = best_stat;
            }
            else if (idx_other > 0)
            {
                mgr->cache->ent[idx_other] = mgr->cache->ent[0];
                mgr->cache->ent[0] = best_other;
            }
        }
    }
}

// Dead-stripped in retail; body from Ratatouille's GC builds, where it is live. Retail's .sdata2
// still interns its PI here, although no live xShadow function uses PI. vol_best is never raised
// (there too), so the last entity with a positive volume wins.
static void xShadow_PickByBounds(xShadowMgr* mgr)
{
    S32 num;
    F32 vol_best = -1.0f;
    xEnt* ent_best = NULL;
    S32 idx_best = -1;
    S32 i;
    xEnt* ep;
    F32 vol;
    xVec3 vec_dim;

    num = mgr->cache->entCount;
    for (i = 0; i < num; i++)
    {
        ep = mgr->cache->ent[i];

        if (ep->bound.type == XBOUND_TYPE_SPHERE)
        {
            vol = PI * (ep->bound.sph.r * ep->bound.sph.r);
        }
        else
        {
            xVec3Sub(&vec_dim, &ep->bound.box.box.upper, &ep->bound.box.box.lower);
            vol = vec_dim.x * vec_dim.y * vec_dim.z;
        }

        if (vol > vol_best)
        {
            ent_best = ep;
            idx_best = i;
        }
    }

    if (idx_best > 0)
    {
        mgr->cache->ent[idx_best] = mgr->cache->ent[0];
        mgr->cache->ent[0] = ent_best;
    }
}

static void xShadow_PickByRayCast(xShadowMgr* mgr)
{
    xEnt* ent_best = NULL;
    S32 idx_best = -1;
    xCollis colrec;
    xRay3 ray;
    S32 num;
    S32 i;
    xEnt* ep;

    memset(&colrec, 0, sizeof(xCollis));

    ray.dir = g_NY3;
    ray.min_t = 0.0f;
    ray.max_t = 10.5f;
    ray.flags = 0xc00;

    num = mgr->cache->entCount;
    for (i = 0; i < num; i++)
    {
        ep = mgr->cache->ent[i];

        colrec.flags = 0;
        colrec.flags |= 0x1f00;

        ray.origin.x = ep->model->Mat->pos.x;
        ray.origin.y = ep->model->Mat->pos.y;
        ray.origin.z = ep->model->Mat->pos.z;

        xRayHitsModel(&ray, ep->model, &colrec);

        if (!(colrec.flags & 1))
        {
            continue;
        }
        else if (colrec.dist > 21.7f)
        {
            continue;
        }
        else
        {
            ent_best = ep;
            idx_best = i;
        }
    }

    if (idx_best > 0)
    {
        mgr->cache->ent[idx_best] = mgr->cache->ent[0];
        mgr->cache->ent[0] = ent_best;
    }
}

static void xShadow_PickEntForNPC(xShadowMgr* mgr)
{
    if (mgr->ent->baseType == eBaseTypeNPC)
    {
        if (mgr->cache->entCount < 2)
        {
            return;
        }

        xShadow_PickByRayCast(mgr);
    }
}
