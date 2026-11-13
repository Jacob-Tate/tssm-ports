#include "zMovePoint.h"
#include "zNMESupplement.h"

#include "xEvent.h"

#include <string.h>

zMovePoint* g_mvpt_list;
S32 g_mvpt_cnt;

zMovePoint* zMovePoint_GetMemPool(S32 cnt)
{
    g_mvpt_list = cnt ? (zMovePoint*)xMemAllocSize(cnt * sizeof(zMovePoint)) : NULL;
    g_mvpt_cnt = cnt;
    return g_mvpt_list;
}

void zMovePointInit(zMovePoint* m, xMovePointAsset* asset)
{
    xMovePointInit((xMovePoint*)m, asset);
    m->eventFunc = zMovePointEventCB;
    if (m->linkCount)
    {
        // Links follow the asset header and its numPoints point IDs.
        m->link =
            (xLinkAsset*)((U8*)asset + sizeof(xMovePointAsset) + asset->numPoints * sizeof(U32));
    }
    else
    {
        m->link = NULL;
    }
}

zMovePoint* zMovePoint_GetInst(S32 n)
{
    return &g_mvpt_list[n];
}

void zMovePointSetup(zMovePoint* mvpt, zScene* scn)
{
    xMovePointSetup((xMovePoint*)mvpt, (xScene*)scn);
}

zMovePoint* zMovePoint_From_xAssetID(U32 aid)
{
    zMovePoint* da_mvpt = NULL;
    S32 i;

    for (i = 0; i < g_mvpt_cnt; i++)
    {
        zMovePoint* mvpt = &g_mvpt_list[i];
        if (mvpt->asset->id == aid)
        {
            da_mvpt = mvpt;
            break;
        }
    }

    return da_mvpt;
}

void zMovePointSave(zMovePoint* ent, xSerial* s)
{
    xMovePointSave((xMovePoint*)ent, s);
}

void zMovePointLoad(zMovePoint* ent, xSerial* s)
{
    xMovePointLoad((xMovePoint*)ent, s);
}

void zMovePointEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32)
{
    zMovePoint* m = (zMovePoint*)to;

    switch (toEvent)
    {
    case eEventOn:
        m->on = TRUE;
        break;
    case eEventOff:
        m->on = FALSE;
        break;
    case eEventArrive:
        break;
    case eEventReset:
        xMovePointReset(m);
        break;
    case eEventMakeASplash:
        NMECSplashData splash;
        memset(&splash, 0, sizeof(NMECSplashData));
        splash.rad_splash = -1.0f;
        splash.tym_duration = -1.0f;
        splash.typ_splash = NMEC_SPLASH_WATER;

        if (toParamWidget)
        {
            // The widget is the splash location's pointer asset.
            splash.ass_ptr = (pointer_asset*)toParamWidget;
        }
        else if (m->pos)
        {
            splash.pos_splash = *m->pos;
        }
        else
        {
            break;
        }

        if (toParam)
        {
            if (toParam[0] > 1e-5f)
            {
                splash.rad_splash = toParam[0];
            }
            if (toParam[1] > 1e-5f)
            {
                splash.tym_duration = toParam[1];
            }
            if ((S32)toParam[3] > 0)
            {
                splash.typ_splash = (en_typeSplash)(U32)toParam[3];
            }
        }

        NMEC_MakeASplash(&splash);
        break;
    }
}

F32 zMovePointGetNext(const zMovePoint* current, const zMovePoint* prev, zMovePoint** next,
                      xVec3* hdng)
{
    F32 dist = xMovePointGetNext(current, prev, (xMovePoint**)next, hdng);
    return dist;
}

xVec3* zMovePointGetPos(const zMovePoint* m)
{
    return xMovePointGetPos(m);
}

F32 zMovePointGetDelay(const zMovePoint* m)
{
    return m->delay;
}

// Dead-stripped placeholders: retail's .rodata keeps four unreferenced all-zero templates after the
// last zMovePoint function. The first is a zero xVec3 of a stripped function, likely a
// zMovePointFindNearest overload.
static void __deadstripped_zMovePoint()
{
    xVec3 delta = {};
}

// The other three are the locals of the stripped MVPT_dflt_chklos, zMovePointFindNearest's default
// line-of-sight callback (names from Scooby's PS2 DWARF, order from retail's templates).
static void __deadstripped2_zMovePoint()
{
    xRay3 ray_los = {};
    xVec3 vec_tgt = {};
    xCollis colrec = {};
}
