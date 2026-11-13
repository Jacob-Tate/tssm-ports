// ported from bfbb repo
#ifndef ISCRFX_H
#define ISCRFX_H

#include <rwcore.h>
#include <rwplcore.h>
#include <types.h>
#include "xMath3.h"

struct iScrFxLensFlare
{
    xVec3 pos;
    F32 dist2;
    xVec3 at;
    void* source;
    F32 intensity;
    F32 attenuatedIntensity;
    U32 flags;
    U32 padding;
};

struct _iMotionBlurData
{
    S32 motionBlurAlpha;
    RwRaster* motionBlurFrontBuffer;
    RwIm2DVertex vertex[4];
    U16 index[6];
    U32 w;
    U32 h;
};

void iScrFxLensFlare_Render(RwCamera* camera, iScrFxLensFlare* flare,
                            void (*renderCB)(RwRaster*, RwFrame*, U8, void*));
void iScrFxLensFlare_Reset();
void iScrFxInit();
void iScrFxBegin();
void iScrFxEnd();
void iScrFxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, U8 red, U8 green, U8 blue, U8 alpha,
                   bool clear_z_to_far);
void iCameraMotionBlurActivate(U32 activate);
void iCameraSetBlurriness(F32 amount);
void iScrFxCameraCreated(RwCamera* pCamera);
void iScrFxCameraEndScene(RwCamera* pCamera);
void iScrFxPostCameraEnd(RwCamera* pCamera);
S32 iScrFxCameraDestroyed(RwCamera* pCamera);

#endif
