#include "zAsync.h"

#include <dolphin/gx/GXManage.h>
#include <dolphin/vi.h>

#include "iAsync.h"
#include "iCamera.h"
#include "iTime.h"
#include "iTRC.h"
#include "xDebug.h"
#include "xMemMgr.h"
#include "zGlobals.h"

extern U32 FB_XRES;
extern U32 FB_YRES;

namespace zAsync
{
    RwCamera* cam;
    RpLight* light;
    RwFrame* frame;
    RpWorld* world;
    Screen* screen;
} // namespace zAsync

void zAsync::Init(Screen* scr)
{
    screen = scr;
}

void zAsync::Allocate()
{
    cam = iCameraCreate(FB_XRES, FB_YRES, 0);
    light = RpLightCreate(rpLIGHTPOINT);
    frame = RwFrameCreate();
    RwBBox tmpbbox = { { 10.0f, 10.0f, 10.0f }, { -10.0f, -10.0f, -10.0f } };
    world = RpWorldCreate(&tmpbbox);

    RwV3d lightpos = { 5.0f, 5.0f, -5.0f };
    RwRGBAReal color = { 1.0f, 0.0f, 0.0f, 1.0f };

    RpLightSetFrame(light, frame);
    RpLightSetColor(light, &color);
    RwFrameTranslate(frame, &lightpos, rwCOMBINEREPLACE);
    RpWorldAddLight(world, light);

    RpWorldAddCamera(world, cam);

    if (screen)
    {
        screen->Init();
    }
}

void zAsync::Free()
{
    if (screen)
    {
        screen->Exit();
    }

    GXDrawDone();

    RpWorldRemoveLight(world, light);
    // Retail reads frame before the sync call.
    RwFrame* tframe = frame;
    _rwFrameSyncDirty();
    RwFrameDestroy(tframe);
    RpLightDestroy(light);

    RpWorldRemoveCamera(world, cam);
    RpWorldDestroy(world);

    iCameraDestroy(cam);
    cam = NULL;
    light = NULL;
    frame = NULL;
    world = NULL;
}

void* zAsync::RenderThread(void*)
{
    iTime lastTime;
    iTime curTime;
    F32 dt;
    static F32 rot = 0.0f;

    lastTime = iTimeGet();
    globals.inLoadingScreen = TRUE;

    while (!iAsyncIsDone())
    {
        iAsyncBeginUpdate();

        curTime = iTimeGet();
        dt = iTimeDiffSec(lastTime, curTime);
        if (dt == 0.0f)
        {
            dt = 0.0001f;
        }
        lastTime = curTime;

        RwRGBA bg = { 0x12, 0x24, 0x4B, 0x00 };

        RwCamera* currentCamera = RwCameraGetCurrentCamera();
        U8 diskError = iTRCDisk::CheckDiskErrorInHigherPriorityThread();

        if (currentCamera || scratch_mem_used)
        {
            iAsyncEndUpdate();
            if (diskError)
            {
                iAsyncSuspend();
            }
            VIWaitForRetrace();
            continue;
        }

        RwCameraClear(cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
        RwCameraBeginUpdateWrapper(cam);

        if (screen)
        {
            screen->Update(dt);
            screen->Render();
        }

        RwCameraEndUpdate(cam);
        RwCameraShowRaster(cam, NULL, 1);

        if (currentCamera)
        {
            RwCameraBeginUpdateWrapper(currentCamera);
        }

        gFrameCount++;
        iAsyncEndUpdate();
    }

    globals.inLoadingScreen = FALSE;
    return NULL;
}
