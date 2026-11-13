#include "iCamera.h"
#include "iMemMgr.h"
#include "iSystem.h"

#include <dolphin/os.h>

RwCamera* globalCamera;
static RwCamera* sMainGameCamera;

F32 sCameraNearClip = 0.1f;
F32 sCameraFarClip = 400.0f;

// RenderWare world plugin data attached to every camera (baworobj.c, rpWorldCameraExt).
struct RpWorldCameraExt
{
    RpWorldSector** frustumSectors;
    S32 space;
    S32 position;
    RpWorld* world;
    RwCamera* (*oldBeginUpdate)(RwCamera*);
    RwCamera* (*oldEndUpdate)(RwCamera*);
    RwObjectHasFrame* (*oldSync)(RwObjectHasFrame*);
};

void iCameraClear(RwCamera* camera, xColor_tag color)
{
    RwCameraClear(camera, &color.rgba, rwCAMERACLEARIMAGE | rwCAMERACLEARZ | rwCAMERACLEARSTENCIL);
}

RwCamera* iCameraCreate(S32 width, S32 height, S32 mainGameCamera)
{
    RwV2d vw;
    RwCamera* camera;
    RwFrame* cameraFrame;
    RwRaster* cameraRaster;
    RwRaster* zbuffer;

    camera = iCameraRwCreate();

    cameraFrame = RwFrameCreate();
    RwCameraSetFrame(camera, cameraFrame);

    cameraRaster = RwRasterCreate(width, height, 0, rwRASTERTYPECAMERA);
    RwCameraSetRaster(camera, cameraRaster);

    zbuffer = RwRasterCreate(width, height, 0, rwRASTERTYPEZBUFFER);
    RwCameraSetZRaster(camera, zbuffer);

    RwCameraSetFarClipPlane(camera, sCameraFarClip);
    RwCameraSetNearClipPlane(camera, sCameraNearClip);

    vw.x = 1.0f;
    vw.y = 1.0f;

    RwCameraSetViewWindow(camera, &vw);

    if (mainGameCamera)
    {
        iScrFxCameraCreated(camera);
        sMainGameCamera = camera;
    }

    return camera;
}

void iCameraDestroy(RwCamera* camera)
{
    RpWorld* pWorld;
    RwRaster* raster;
    RwFrame* frame;

    iMemMgrEnableRenderWareMemoryHack(true);
    _rwFrameSyncDirty();
    iMemMgrEnableRenderWareMemoryHack(false);

    pWorld = RwCameraGetWorld(camera);

    if (pWorld)
    {
        RpWorldRemoveCamera(pWorld, camera);
    }

    if (camera == sMainGameCamera)
    {
        iScrFxCameraDestroyed(camera);
        sMainGameCamera = NULL;
    }

    if (camera)
    {
        frame = RwCameraGetFrame(camera);

        if (frame)
        {
            RwCameraSetFrame(camera, NULL);
            _rwFrameSyncDirty();
            RwFrameDestroy(frame);
        }

        raster = RwCameraGetRaster(camera);

        if (raster)
        {
            RwRasterDestroy(raster);
            RwCameraSetRaster(camera, NULL);
        }

        raster = RwCameraGetZRaster(camera);

        if (raster)
        {
            RwRasterDestroy(raster);
            RwCameraSetZRaster(camera, NULL);
        }

        RwCameraDestroy(camera);
    }
}

void iCameraShowRaster(RwCamera* cam)
{
    RwCameraShowRaster(cam, NULL, 0);
}

void iCameraFrustumPlanes(RwCamera* cam, xVec4* frustplane)
{
    RwFrustumPlane* rwPlane;

    rwPlane = &cam->frustumPlanes[2];
    frustplane[0].x = rwPlane->plane.normal.x;
    frustplane[1].x = rwPlane->plane.normal.y;
    frustplane[2].x = rwPlane->plane.normal.z;
    frustplane[3].x = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[4];
    frustplane[0].y = rwPlane->plane.normal.x;
    frustplane[1].y = rwPlane->plane.normal.y;
    frustplane[2].y = rwPlane->plane.normal.z;
    frustplane[3].y = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[5];
    frustplane[0].z = rwPlane->plane.normal.x;
    frustplane[1].z = rwPlane->plane.normal.y;
    frustplane[2].z = rwPlane->plane.normal.z;
    frustplane[3].z = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[3];
    frustplane[0].w = rwPlane->plane.normal.x;
    frustplane[1].w = rwPlane->plane.normal.y;
    frustplane[2].w = rwPlane->plane.normal.z;
    frustplane[3].w = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[1];
    frustplane[4].x = rwPlane->plane.normal.x;
    frustplane[5].x = rwPlane->plane.normal.y;
    frustplane[6].x = rwPlane->plane.normal.z;
    frustplane[7].x = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[0];
    frustplane[4].y = rwPlane->plane.normal.x;
    frustplane[5].y = rwPlane->plane.normal.y;
    frustplane[6].y = rwPlane->plane.normal.z;
    frustplane[7].y = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[2];
    frustplane[4].z = rwPlane->plane.normal.x;
    frustplane[5].z = rwPlane->plane.normal.y;
    frustplane[6].z = rwPlane->plane.normal.z;
    frustplane[7].z = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[4];
    frustplane[4].w = rwPlane->plane.normal.x;
    frustplane[5].w = rwPlane->plane.normal.y;
    frustplane[6].w = rwPlane->plane.normal.z;
    frustplane[7].w = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[2];
    frustplane[8].x = rwPlane->plane.normal.x;
    frustplane[9].x = rwPlane->plane.normal.y;
    frustplane[10].x = rwPlane->plane.normal.z;
    frustplane[11].x = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[4];
    frustplane[8].y = rwPlane->plane.normal.x;
    frustplane[9].y = rwPlane->plane.normal.y;
    frustplane[10].y = rwPlane->plane.normal.z;
    frustplane[11].y = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[5];
    frustplane[8].z = rwPlane->plane.normal.x;
    frustplane[9].z = rwPlane->plane.normal.y;
    frustplane[10].z = rwPlane->plane.normal.z;
    frustplane[11].z = rwPlane->plane.distance;

    rwPlane = &cam->frustumPlanes[3];
    frustplane[8].w = rwPlane->plane.normal.x;
    frustplane[9].w = rwPlane->plane.normal.y;
    frustplane[10].w = rwPlane->plane.normal.z;
    frustplane[11].w = rwPlane->plane.distance;
}

void iCameraUpdatePos(RwCamera* cam, xMat4x3* pos)
{
    RwFrame* f;
    RwMatrix* m;

    f = RwCameraGetFrame(cam);
    m = RwFrameGetMatrix(f);

    xMat4x3Copy((xMat4x3*)m, pos);

    m = RwFrameGetLTM(f);

    xMat4x3Copy((xMat4x3*)m, pos);

    RwFrameOrthoNormalize(f);
    RwFrameUpdateObjects(f);
}

F32 iCameraGetFOV(RwCamera* cam)
{
    return 2.0f * (180.0f / PI) * atanf(cam->viewWindow.x);
}

void iCameraAssignEnv(RwCamera* camera, iEnv* env_geom)
{
    globalCamera = camera;

    RpWorldAddCamera(env_geom->world, camera);
    xShadowSetWorld(env_geom->world);
}

void iCamGetViewMatrix(RwCamera* camera, xMat4x3* view_matrix)
{
    RwMatrix* rw_view;

    memset(view_matrix, 0, sizeof(xMat4x3));

    rw_view = RwCameraGetViewMatrix(camera);

    view_matrix->right.x = rw_view->right.x;
    view_matrix->right.y = rw_view->right.y;
    view_matrix->right.z = rw_view->right.z;
    view_matrix->up.x = rw_view->up.x;
    view_matrix->up.y = rw_view->up.y;
    view_matrix->up.z = rw_view->up.z;
    view_matrix->at.x = rw_view->at.x;
    view_matrix->at.y = rw_view->at.y;
    view_matrix->at.z = rw_view->at.z;
    view_matrix->pos.x = rw_view->pos.x;
    view_matrix->pos.y = rw_view->pos.y;
    view_matrix->pos.z = rw_view->pos.z;
}

void iCameraSetNearFarClip(F32 nearPlane, F32 farPlane)
{
    if (nearPlane <= 0.0f)
    {
        nearPlane = 0.1f;
    }

    sCameraNearClip = nearPlane;

    if (farPlane <= 0.0f)
    {
        farPlane = 400.0f;
    }

    sCameraFarClip = farPlane;
}

void iCameraGetNearFarClip(F32& nearPlane, F32& farPlane)
{
    nearPlane = sCameraNearClip;
    farPlane = sCameraFarClip;
}

void iCameraSetFogParams(iFogParams* fp, F32 time)
{
    if (!fp || fp->type == rwFOGTYPENAFOGTYPE)
    {
        xglobals->fog.type = rwFOGTYPENAFOGTYPE;
        xglobals->fogA.type = rwFOGTYPENAFOGTYPE;
    }
    else if (0.0f == time || fp->type != xglobals->fogA.type)
    {
        xglobals->fog = *fp;
        xglobals->fogA = *fp;
        xglobals->fog_t0 = 0;
    }
    else
    {
        xglobals->fogA = xglobals->fog;
        xglobals->fogB = *fp;

        xglobals->fog_t0 = iTimeGet();
        xglobals->fog_t1 = xglobals->fog_t0 + (iTime)(time * OS_TIMER_CLOCK);
    }
}

void iCameraUpdateFog(RwCamera* cam, iTime t)
{
    iFogParams* fp = &xglobals->fog;
    iFogParams* fa = &xglobals->fogA;
    iFogParams* fb = &xglobals->fogB;
    F32 a;
    RwRGBA ca;
    RwRGBA cb;
    RwRGBA color;

    if (xglobals->fog.type == rwFOGTYPENAFOGTYPE)
    {
        return;
    }

    if (xglobals->fog_t0 == 0)
    {
        return;
    }

    t = iTimeGet();

    a = iTimeDiffSec(xglobals->fog_t0, t) / iTimeDiffSec(xglobals->fog_t0, xglobals->fog_t1);
    a = CLAMP(a, 0.0f, 1.0f);

    fp->type = xglobals->fogB.type;
    fp->table = xglobals->fogB.table;

    fp->start = fa->start + a * (fb->start - fa->start);
    fp->stop = fa->stop + a * (fb->stop - fa->stop);
    fp->density = fa->density + a * (fb->density - fa->density);

    ca = fa->fogcolor;
    cb = fb->fogcolor;
    color.red = ca.red + a * (cb.red - ca.red);
    color.green = ca.green + a * (cb.green - ca.green);
    color.blue = ca.blue + a * (cb.blue - ca.blue);
    color.alpha = ca.alpha + a * (cb.alpha - ca.alpha);
    fp->fogcolor = color;

    ca = fa->bgcolor;
    cb = fb->bgcolor;
    color.red = ca.red + a * (cb.red - ca.red);
    color.green = ca.green + a * (cb.green - ca.green);
    color.blue = ca.blue + a * (cb.blue - ca.blue);
    color.alpha = ca.alpha + a * (cb.alpha - ca.alpha);
    fp->bgcolor = color;

    if (1.0f == a)
    {
        xglobals->fog_t0 = 0;
        xglobals->fogA = xglobals->fogB;
    }
}

void iCameraSetFogRenderStates()
{
    RwCamera* pCamera;
    iFogParams* pFogParams;
    U32 bite_me;

    pCamera = RwCameraGetCurrentCamera();

    if (pCamera == NULL)
    {
        return;
    }

    pFogParams = &xglobals->fog;

    if (pFogParams->type == rwFOGTYPENAFOGTYPE)
    {
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

        RwCameraSetFarClipPlane(pCamera, sCameraFarClip);
    }
    else if (pFogParams->start >= pFogParams->stop)
    {
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

        RwCameraSetFarClipPlane(pCamera, pFogParams->stop);
    }
    else
    {
        bite_me = (pFogParams->fogcolor.alpha << 24) | (pFogParams->fogcolor.red << 16) |
                  (pFogParams->fogcolor.green << 8) | pFogParams->fogcolor.blue;

        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEFOGTYPE, (void*)pFogParams->type);
        RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)bite_me);
        RwRenderStateSet(rwRENDERSTATEFOGDENSITY, (void*)&pFogParams->density);

        RwCameraSetFogDistance(pCamera, pFogParams->start);
        RwCameraSetFarClipPlane(pCamera, pFogParams->stop);
    }
}

void iCameraSetFog(bool enable)
{
    iFogParams* pFogParams = &xglobals->fog;

    if (pFogParams->type == rwFOGTYPENAFOGTYPE || pFogParams->start >= pFogParams->stop)
    {
        return;
    }

    if (enable)
    {
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    }
}

RwCamera* RwCameraBeginUpdateWrapper(RwCamera* camera)
{
    RwCamera* retval;

    retval = RwCameraBeginUpdate(camera);

    return retval;
}

void iCameraPerformRenderWareHack(RwCamera* camera)
{
    RpWorldSector** newFrustumSectors;
    S32 cameraExtOffset;
    RpWorldCameraExt* cameraExt;

    newFrustumSectors = (RpWorldSector**)RwMalloc(50 * sizeof(RpWorldSector*), 0);
    cameraExtOffset = RwCameraGetPluginOffset(rwID_WORLDOBJMODULE);
    cameraExt = RWPLUGINOFFSET(RpWorldCameraExt, camera, cameraExtOffset);
    cameraExt->frustumSectors = newFrustumSectors;
    cameraExt->space = 50;
}

// No symbol (inlined into xCamScreen::set_fov).
inline void iCameraSetFOV(RwCamera* cam, F32 fov)
{
    F32 view_window;
    F32 fAspectRatio;
    RwV2d vw;

    view_window = itan((PI / 180.0f) * (0.5f * fov));
    fAspectRatio = 0.75f;
    vw.x = view_window;
    vw.y = view_window * fAspectRatio;

    RwCameraSetViewWindow(cam, &vw);
}

// No symbol (inlined into xCamScreen::begin_render).
inline void iCameraBegin(RwCamera* cam, S32 clear)
{
    if (clear)
    {
        if (xglobals->fog.type != rwFOGTYPENAFOGTYPE)
        {
            RwCameraClear(cam, &xglobals->fog.bgcolor,
                          rwCAMERACLEARIMAGE | rwCAMERACLEARZ | rwCAMERACLEARSTENCIL);
        }
        else
        {
            RwCameraClear(cam, NULL, rwCAMERACLEARZ);
        }
    }

    RwCameraSetNearClipPlane(cam, sCameraNearClip);
    RwCameraBeginUpdate(cam);
}

// No symbol (inlined into xCamScreen::end_render); unlike BfBB, no iScrFxPostCameraEnd call.
inline void iCameraEnd(RwCamera* cam)
{
    iScrFxCameraEndScene(cam);
    RwCameraEndUpdate(cam);
}
