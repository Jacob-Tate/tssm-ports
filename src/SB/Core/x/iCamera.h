#ifndef ICAMERA_H
#define ICAMERA_H

#include <rwcore.h>
#include <types.h>

#include "xMath3.h"
#include "iEnv.h"
#include "iTime.h"
#include "iColor.h"

struct iFogParams
{
    RwFogType type;
    F32 start;
    F32 stop;
    F32 density;
    RwRGBA fogcolor;
    RwRGBA bgcolor;
    U8* table;
};

extern RwCamera* globalCamera;

void iCameraPerformRenderWareHack(RwCamera* camera);
RwCamera* RwCameraBeginUpdateWrapper(RwCamera* camera);

// No symbol and original name unknown: every GC RwCameraCreate call site (xModel, xShadow,
// zHud, xFXCameraTexture) applies the RenderWare hack to the new camera before using it,
// with the result kept in a register across the call, so this is an inline helper.
inline RwCamera* iCameraRwCreate()
{
    RwCamera* camera = RwCameraCreate();
    if (camera != NULL)
    {
        iCameraPerformRenderWareHack(camera);
    }
    return camera;
}
RwCamera* iCameraCreate(S32 width, S32 height, S32 mainGameCamera);
void iCameraDestroy(RwCamera* camera);
void iCameraBegin(RwCamera* cam, S32 clear);
void iCameraEnd(RwCamera* cam);
void iCameraShowRaster(RwCamera* cam);
void iCameraFrustumPlanes(RwCamera* cam, xVec4* frustplane);
void iCameraUpdatePos(RwCamera* cam, xMat4x3* pos);
void iCameraSetFOV(RwCamera* cam, F32 fov);
F32 iCameraGetFOV(RwCamera* cam);
void iCameraAssignEnv(RwCamera* camera, iEnv* env_geom);
void iCamGetViewMatrix(RwCamera* camera, xMat4x3* view_matrix);
void iCameraSetNearFarClip(F32 nearPlane, F32 farPlane);
void iCameraSetFogParams(iFogParams* fp, F32 time);
void iCameraUpdateFog(RwCamera* cam, iTime t);
void iCameraSetFogRenderStates();
void iCameraSetFog(bool enable);
void iCameraGetNearFarClip(F32& nearPlane, F32& farPlane);
void iCameraClear(RwCamera* camera, xColor_tag color);

#endif
