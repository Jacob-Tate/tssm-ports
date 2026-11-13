#include "xLightKit.h"

xLightKit* gLastLightKit;

// Dead-stripped placeholder, not recovered code: retail's @stringBase0 keeps these unreferenced
// strings of a stripped light-kit parser (Ratatouille's xLightKit_Parse), and the zero xVec3
// stands for an unreferenced 12-byte .rodata template next to them. The body is unknown.
static const char* __deadstripped_xLightKit()
{
    xVec3 vec = { 0.0f, 0.0f, 0.0f };

    return "\n\r\0"
           "intensity=\0"
           "color=\0"
           "%f\0"
           "%f,%f,%f\0"
           "matrix=\0"
           "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\0"
           "radius=\0"
           "angle=";
}

xLightKit* xLightKit_Prepare(void* data)
{
    xLightKit* lkit = (xLightKit*)data;
    lkit->lightList = (xLightKitLight*)(lkit + 1);
    xLightKitLight* currlight = (xLightKitLight*)(lkit + 1);

    for (U32 i = 0; i < lkit->lightCount; currlight++, i++)
    {
        if (currlight->platLight != NULL)
        {
            return lkit;
        }

        // If any of the colors is greater than 1.0, normalize back to 0-1
        if (currlight->color.red > 1.0f || currlight->color.green > 1.0f ||
            currlight->color.blue > 1.0f)
        {
            F32 s;
            s = MAX(MAX(currlight->color.red, currlight->color.green), currlight->color.blue);
            s = MAX(s, 0.00001f);
            s = 1.0f / s;
            currlight->color.red *= s;
            currlight->color.green *= s;
            currlight->color.blue *= s;
        }

        switch (currlight->type)
        {
        case 1:
            currlight->platLight = RpLightCreate(2);
            break;
        case 2:
            currlight->platLight = RpLightCreate(1);
            break;
        case 3:
            currlight->platLight = RpLightCreate(128);
            break;
        case 4:
            currlight->platLight = RpLightCreate(130);
            break;
        default:
            break;
        }
        RpLightSetColor(currlight->platLight, &currlight->color);
        if (currlight->type >= 2)
        {
            RwFrame* frame = RwFrameCreate();
            RwMatrixTag tmpmat;

            memset(&tmpmat, 0, 64);
            tmpmat.right.x = -currlight->matrix[0];
            tmpmat.right.y = -currlight->matrix[1];
            tmpmat.right.z = -currlight->matrix[2];
            tmpmat.up.x = currlight->matrix[4];
            tmpmat.up.y = currlight->matrix[5];
            tmpmat.up.z = currlight->matrix[6];
            tmpmat.at.x = -currlight->matrix[8];
            tmpmat.at.y = -currlight->matrix[9];
            tmpmat.at.z = -currlight->matrix[10];
            tmpmat.pos.x = currlight->matrix[12];
            tmpmat.pos.y = currlight->matrix[13];
            tmpmat.pos.z = currlight->matrix[14];
            RwV3dNormalize(&tmpmat.right, &tmpmat.right);
            RwV3dNormalize(&tmpmat.up, &tmpmat.up);
            RwV3dNormalize(&tmpmat.at, &tmpmat.at);
            RwFrameTransform(frame, &tmpmat, rwCOMBINEREPLACE);
            _rwObjectHasFrameSetFrame(currlight->platLight, frame);
        }
        if (currlight->type >= 3)
        {
            RpLightSetRadius(currlight->platLight, currlight->radius);
        }
        if (currlight->type >= 4)
        {
            RpLightSetConeAngle(currlight->platLight, currlight->angle);
        }
    }

    return (xLightKit*)data;
}

void xLightKit_Enable(xLightKit* lkit, RpWorld* world)
{
    if (lkit != gLastLightKit)
    {
        U32 i;
        if (gLastLightKit != NULL)
        {
            for (i = 0; i < gLastLightKit->lightCount; i++)
            {
                RpWorldRemoveLight(world, gLastLightKit->lightList[i].platLight);
            }
        }
        gLastLightKit = lkit;
        if (lkit != NULL)
        {
            for (i = 0; i < lkit->lightCount; i++)
            {
                RpWorldAddLight(world, lkit->lightList[i].platLight);
            }
        }
    }
}

xLightKit* xLightKit_GetCurrent(RpWorld* world)
{
    return gLastLightKit;
}

void xLightKit_Destroy(xLightKit* lkit)
{
    if (lkit == NULL)
    {
        return;
    }

    U32 i;
    xLightKitLight* currLight = lkit->lightList;

    for (i = 0; i < lkit->lightCount; currLight++, i++)
    {
        if (currLight->platLight != NULL)
        {
            _rwFrameSyncDirty();
            RwFrame* tframe = RpLightGetFrame(currLight->platLight);
            if (tframe != NULL)
            {
                RpLightSetFrame(currLight->platLight, NULL);
                _rwFrameSyncDirty();
                RwFrameDestroy(tframe);
            }
            RpLightDestroy(currLight->platLight);
            currLight->platLight = NULL;
        }
    }
}
