#include "xFog.h"

#include "xEvent.h"

void xFogClearFog()
{
    iCameraSetFogParams(NULL, 0.0f);
}

inline void xFogInit(xBase* ent, xFogAsset* tasset)
{
    xBaseInit(ent, tasset);

    ent->eventFunc = xFogEventCB;
    ((_xFog*)ent)->tasset = tasset;

    if (ent->linkCount != 0)
    {
        ent->link = (xLinkAsset*)(((_xFog*)ent)->tasset + 1);
    }
    else
    {
        ent->link = NULL;
    }
}

void xFogInit(void* b, void* tasset)
{
    xFogInit((xBase*)b, (xFogAsset*)tasset);
}

void xFogSave(_xFog* ent, xSerial* s)
{
    xBaseSave((xBase*)ent, s);
}

void xFogLoad(_xFog* ent, xSerial* s)
{
    xBaseLoad((xBase*)ent, s);
}

void xFogEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
{
    _xFog* t = (_xFog*)to;

    switch (toEvent)
    {
    case eEventOn:
        iFogParams fog;
        fog.type = rwFOGTYPELINEAR;
        fog.start = t->tasset->fogStart;
        fog.stop = t->tasset->fogStop;
        fog.density = t->tasset->fogDensity;
        fog.fogcolor.red = t->tasset->fogColor[0];
        fog.fogcolor.green = t->tasset->fogColor[1];
        fog.fogcolor.blue = t->tasset->fogColor[2];
        fog.fogcolor.alpha = t->tasset->fogColor[3];
        fog.bgcolor.red = t->tasset->bkgndColor[0];
        fog.bgcolor.green = t->tasset->bkgndColor[1];
        fog.bgcolor.blue = t->tasset->bkgndColor[2];
        fog.bgcolor.alpha = t->tasset->bkgndColor[3];
        fog.table = NULL;
        iCameraSetFogParams(&fog, t->tasset->transitionTime);
        break;
    case eEventOff:
        iCameraSetFogParams(NULL, 0.0f);
        break;
    case eEventReset:
        xFogReset(t);
        break;
    }
}

void xFogUpdate(xBase* ent, xScene* sc, F32 dt)
{
}
