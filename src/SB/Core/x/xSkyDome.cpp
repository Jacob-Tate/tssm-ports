#include "xSkyDome.h"

struct SkyDomeInfo
{
    xEnt* ent;
    S32 sortorder;
    S32 lockXZ;
    S32 lockY;
};

static SkyDomeInfo sSkyList[8];
static S32 sSkyCount;

void xSkyDome_EmptyRender(xEnt*)
{
}

void xSkyDome_Setup()
{
    sSkyCount = 0;
}

void xSkyDome_AddEntity(xEnt* ent, S32 sortorder, S32 lockY, S32 lockXZ)
{
    S32 i, j;

    if (ent->render == xSkyDome_EmptyRender)
    {
        return;
    }

    for (i = 0; i < sSkyCount; i++)
    {
        if (sSkyList[i].ent == ent)
        {
            return;
        }
    }

    for (i = 0; i < sSkyCount; i++)
    {
        if (sortorder < sSkyList[i].sortorder)
        {
            break;
        }
    }

    for (j = sSkyCount - 1; j >= i; j--)
    {
        sSkyList[j + 1] = sSkyList[j];
        sSkyList[j + 1].ent->user_data = (void*)(j + 1);
    }

    sSkyList[i].ent = ent;
    sSkyList[i].sortorder = sortorder;
    sSkyList[i].lockY = lockY;
    sSkyList[i].lockXZ = lockXZ;

    sSkyCount++;

    ent->user_data = (void*)i;
    ent->render = xSkyDome_EmptyRender;
    ent->model->Flags &= (U16)~0x1;
    ent->baseFlags &= (U16)~0x10;

    zEntEvent(ent, eEventCollisionOff);
    zEntEvent(ent, eEventCameraCollideOff);
}

void xSkyDome_Render()
{
    RwCamera* cam = RwCameraGetCurrentCamera();
    RwMatrix* cammat = RwFrameGetMatrix(RwCameraGetFrame(cam));
    F32 far_clip = RwCameraGetFarClipPlane(cam);

    RwCameraEndUpdate(cam);
    RwCameraSetFarClipPlane(cam, 10000.0f);

    RwRGBA color = { 0, 0, 0, 0 };
    RwCameraClear(cam, &color, rwCAMERACLEARZ | rwCAMERACLEARSTENCIL);
    RwCameraBeginUpdateWrapper(cam);

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

    S32 i;
    xEnt* ent;

    for (i = 0; i < sSkyCount; i++)
    {
        ent = sSkyList[i].ent;

        ent->render = xSkyDome_EmptyRender;
        ent->model->Flags &= (U16)~0x1;

        if (ent->model && xEntIsVisible(ent))
        {
            RwV3d pos;

            pos = ent->model->Mat->pos;

            if (sSkyList[i].lockXZ)
            {
                ent->model->Mat->pos.x = cammat->pos.x;
                ent->model->Mat->pos.z = cammat->pos.z;
            }

            if (sSkyList[i].lockY)
            {
                ent->model->Mat->pos.y = cammat->pos.y;
            }

            if (!xModelCullSingle(ent->model))
            {
                iModelRender(ent->model->Data, ent->model->Mat);
            }

            ent->model->Mat->pos = pos;
        }
    }

    RwCameraEndUpdate(cam);
    RwCameraSetFarClipPlane(cam, far_clip);
    RwCameraBeginUpdateWrapper(cam);
}

#ifdef PLATFORM_PC
// PC: xSkyDome_BeginRenderSingle, xSkyDome_RenderSingle and
// xSkyDome_EndRenderSingle (PS2 DWARF), dead-stripped from the GameCube build,
// written from the Xbox executable's (0x19BDB0, 0x19BE20, 0x19BDF0).
static F32 sSingleFarClip;

void xSkyDome_BeginRenderSingle()
{
    // The far plane out to 10000 for the domes, as in xSkyDome_Render, but
    // nothing is cleared.
    RwCamera* cam = RwCameraGetCurrentCamera();

    sSingleFarClip = RwCameraGetFarClipPlane(cam);
    RwCameraEndUpdate(cam);
    RwCameraSetFarClipPlane(cam, 10000.0f);
    RwCameraBeginUpdateWrapper(cam);
}

void xSkyDome_RenderSingle(xEnt* ent)
{
    // Held over the camera as in xSkyDome_Render, and drawn with no visibility
    // test: the domes the water draws are hidden from the scene.
    RwCamera* cam = RwCameraGetCurrentCamera();
    RwMatrix* cammat = RwFrameGetMatrix(RwCameraGetFrame(cam));
    RwMatrix* mat = ent->model->Mat;
    RwV3d oldpos = mat->pos;

    // The Xbox takes user_data as the dome's index unchecked; a stale one here
    // only loses the hold.
    S32 index = (S32)(intptr_t)ent->user_data;

    if (index >= 0 && index < sSkyCount && sSkyList[index].ent == ent)
    {
        const SkyDomeInfo& info = sSkyList[index];

        if (info.lockXZ)
        {
            mat->pos.x = cammat->pos.x;
            mat->pos.z = cammat->pos.z;
        }

        if (info.lockY)
        {
            mat->pos.y = cammat->pos.y;
        }
    }

    if (!xModelCullSingle(ent->model))
    {
        iModelRender(ent->model->Data, ent->model->Mat);
    }

    mat->pos = oldpos;
}

void xSkyDome_EndRenderSingle()
{
    RwCamera* cam = RwCameraGetCurrentCamera();

    RwCameraEndUpdate(cam);
    RwCameraSetFarClipPlane(cam, sSingleFarClip);
    RwCameraBeginUpdateWrapper(cam);
}
#endif
