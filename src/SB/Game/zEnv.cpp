#include "zEnv.h"

#include "zGlobals.h"
#include "zLight.h"
#include "xstransvc.h"
#include "xGroup.h"
#include "zSurface.h"
#include "xClimate.h"
#include "xEnt.h"
#include "xDebug.h"

void zEnvInit(void* env, void* easset)
{
    zEnvInit((_zEnv*)env, (xEnvAsset*)easset);
}

// Retail calls this from zEnvInit(void*, void*); GC/2.6 never auto-inlines 30 or more statements.
// The compiled-out xFAIL in the else (Ratatouille fails there when the scene environment has no
// geometry) is what keeps it out of line.
void zEnvInit(_zEnv* env, xEnvAsset* easset)
{
    xBaseInit(env, easset);

    env->easset = easset;
    env->eventFunc = zEnvEventCB;

    if (env->linkCount)
    {
        env->link = (xLinkAsset*)(env->easset + 1);
    }

    globals.sceneCur->zen = env;
    globals.sceneCur->zen = env;

    xEnvLoadJSPList(globals.sceneCur->env, 0);

    S32 mapper_count = xSTAssetCountByType('MAPR');
    for (S32 i = 0; i < mapper_count; i++)
    {
        U32 size;
        xBase* material_map = (xBase*)xSTFindAssetByType('MAPR', i, &size);
        zSurfaceRegisterMapper(material_map->id);
    }

    if (globals.sceneCur->env)
    {
        if (globals.sceneCur->env->geom)
        {
            zLightResetAll(globals.sceneCur->env);
            iLightInit(globals.sceneCur->env->geom->world);
        }
        else
        {
            xFAIL();
        }
    }

    xEntGrowGlobalBoundingBox(&easset->minBounds);
    xEntGrowGlobalBoundingBox(&easset->maxBounds);

    if (easset->flags & 0x1)
    {
        for (S32 i = 0; i < globals.sceneCur->env->geom->jsp_count; i++)
        {
            xEntGrowGlobalBoundingBox(&globals.sceneCur->env->geom->jsp_bound[i].upper);
            xEntGrowGlobalBoundingBox(&globals.sceneCur->env->geom->jsp_bound[i].lower);
        }
    }
}

void zEnvSetup(_zEnv* env)
{
    xBaseSetup(env);
    iEnvLightingBasics(globals.sceneCur->env->geom, env->easset);
}

void zEnvStartingCamera(_zEnv*)
{
    return;
}

void zEnvRender(xEnv* env)
{
    RpWorld* world = env->geom->world;
    S32 num = RpWorldGetNumMaterials(world);

    for (S32 i = 0; i < num; i++)
    {
        xSurface* sp = zSurfaceGetSurface(i);
        zSurfaceProps* pp = (zSurfaceProps*)sp->moprops;

        if (pp && pp->asset && pp->texanim_flags & 0x1)
        {
            RpMaterial* mp = RpWorldGetMaterial(world, i);

            if (mp)
            {
                xGroup* g = (xGroup*)pp->texanim[0].group_ptr;

                if (g)
                {
                    U32 texid = xGroupGetItem(g, pp->texanim[0].group_idx);
                    RwTexture* texptr = (RwTexture*)xSTFindAsset(texid, NULL);

                    if (texptr)
                    {
                        RpMaterialSetTexture(mp, texptr);
                    }
                }
            }
        }
    }

    xEnvRender(env, FALSE);
}

void zEnvSave(_zEnv* ent, xSerial* s)
{
    xBaseSave(ent, s);
}

void zEnvLoad(_zEnv* ent, xSerial* s)
{
    xBaseLoad(ent, s);
}

void zEnvEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget, U32)
{
    switch (toEvent)
    {
    case eEventReset:
        xBaseReset(to, ((_zEnv*)to)->easset);
        break;
    case eEventSetSnow:
        xClimateSetSnow(toParam[0]);
        break;
    case eEventSetRain:
        xClimateSetRain(toParam[0]);
        break;
    case eEventOn:
        iEnvJSPOn(globals.sceneCur->env->geom, (xJSPHeader*)toParamWidget);
        break;
    case eEventOff:
        iEnvJSPOff(globals.sceneCur->env->geom, (xJSPHeader*)toParamWidget);
        break;
    case eEventJSPVisibilityIncrement:
        iEnvJSPVisibilityInc(globals.sceneCur->env->geom, (xJSPHeader*)toParamWidget);
        break;
    case eEventJSPVisibilityDecrement:
        iEnvJSPVisibilityDec(globals.sceneCur->env->geom, (xJSPHeader*)toParamWidget);
        break;
    case eEventSetLightKit:
        gCurXEnv->lightKit = (xLightKit*)toParamWidget;
        break;
    }
}
