#include "zLight.h"
#include "zLightEffect.h"
#include "zScene.h"
#include "xPartition.h"
#include "xShadow.h"
#include "xString.h"

#include <string.h>

static _zLight* sLight[32];
static S32 sLightTotal;
static _tagPartition sLightPart;
static zVolume* sPartitionVolume;
static S32 gNumTemporaryLights;
static _zLight* gTemporaryLights[32];
void (*sEffectFuncs[18])(_zLight*, F32) = {
    NULL,
    NULL,
    zLightEffectFlickerSlow,
    zLightEffectFlicker,
    zLightEffectFlickerErratic,
    zLightEffectStrobeSlow,
    zLightEffectStrobe,
    zLightEffectStrobeFast,
    zLightEffectDimSlow,
    zLightEffectDim,
    zLightEffectDimFast,
    zLightEffectHalfDimSlow,
    zLightEffectHalfDim,
    zLightEffectHalfDimFast,
    zLightEffectRandomColSlow,
    zLightEffectRandomCol,
    zLightEffectRandomColFast,
    zLightEffectCauldron,
};
lightInitFunc sEffectInitFuncs[18] = {
    NULL,
    NULL,
    zLightEffectInitFlicker,
    zLightEffectInitFlicker,
    zLightEffectInitFlicker,
    zLightEffectInitStrobe,
    zLightEffectInitStrobe,
    zLightEffectInitStrobe,
    zLightEffectInitDim,
    zLightEffectInitDim,
    zLightEffectInitDim,
    zLightEffectInitHalfDim,
    zLightEffectInitHalfDim,
    zLightEffectInitHalfDim,
    zLightEffectInitRandomCol,
    zLightEffectInitRandomCol,
    zLightEffectInitRandomCol,
    zLightEffectInitCauldron,
};
xVec3 sDefaultShadowVec = { 0.0f, 1.0f, 0.0f };

static inline void zLightDestroy(_zLight* t);
static inline void zLightReset(_zLight* t);
static inline void zLightEffectSet(_zLight* zlight, S32 idx);

void zLightResetAll(xEnv* env)
{
    memset(sLight, 0, sizeof(sLight));
    sLightTotal = 0;

    if (sPartitionVolume)
    {
        xPartitionVolume(&sLightPart, sPartitionVolume, 10, 1, 10);
    }
    else
    {
        xPartitionWorld(&sLightPart, env, 10, 1, 10);
    }
}

void zLightInit(void* b, void* tasset)
{
    zLightInit((xBase*)b, (zLightAsset*)tasset);
}

void zLightInit(xBase* b, zLightAsset* tasset)
{
    _zLight* t = (_zLight*)b;
    U32 itype;

    xBaseInit(b, tasset);
    t->eventFunc = zLightEventCB;
    t->tasset = tasset;

    if (b->linkCount)
    {
        b->link = (xLinkAsset*)(t->tasset + 1);
    }
    else
    {
        b->link = NULL;
    }

    itype = 1;
    switch (tasset->lightType)
    {
    case 0:
        itype = 1;
        break;
    case 1:
        itype = 2;
        break;
    case 2:
        itype = 1;
        break;
    case 3:
        itype = 1;
        break;
    }

    if (iLightCreate(&t->light, itype))
    {
        sLight[sLightTotal++] = t;

        t->light.sph.center.x = tasset->lightSphere.center.x;
        t->light.sph.center.y = tasset->lightSphere.center.y;
        t->light.sph.center.z = tasset->lightSphere.center.z;
        t->light.sph.r = tasset->lightSphere.r;
        t->light.color.r = tasset->lightColor[0];
        t->light.color.g = tasset->lightColor[1];
        t->light.color.b = tasset->lightColor[2];
        t->light.color.a = tasset->lightColor[3];
        t->light.coneangle = tasset->lightConeAngle;
        t->light.radius_sq = t->light.sph.r * t->light.sph.r;
        iLightModify(&t->light, 31);

        t->flags = 0;
        if (t->tasset->lightFlags & 0x20)
        {
            t->flags |= 1;
        }

        if (t->tasset->lightEffect)
        {
            t->reg = (F32*)xMemAlloc(gActiveHeap, 8 * sizeof(F32), 0);
            zLightEffectSet(t, t->tasset->lightEffect);
        }
        else
        {
            t->reg = NULL;
        }

        t->true_idx = xPartitionInsert(&sLightPart, t, &t->light.sph.center);
    }
}

void zLightResolveLinks()
{
    S32 i;
    _zLight* zl;

    for (i = 0; i < sLightTotal; i++)
    {
        zl = sLight[i];
        if (zl->tasset->attachID)
        {
            zl->attached_to = zSceneFindObject(zl->tasset->attachID);
        }
        else
        {
            zl->attached_to = NULL;
        }
    }
}

void zLightDestroyAll()
{
    S32 total = sLightTotal;
    S32 i;

    for (i = 0; i < total; i++)
    {
        zLightDestroy(sLight[i]);
        sLightTotal--;
    }
    sLightTotal = 0;
}

void zLightSave(_zLight* ent, xSerial* s)
{
    xBaseSave(ent, s);
}

void zLightLoad(_zLight* ent, xSerial* s)
{
    xBaseLoad(ent, s);
}

void zLightEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
{
    _zLight* t = (_zLight*)to;

    switch (toEvent)
    {
    case 38:
        t->flags |= 1;
        break;
    case 39:
        t->flags &= ~1;
        break;
    case 10:
        zLightReset(t);
        break;
    }
}

void zLightUpdate(xBase* to, xScene*, F32 dt)
{
    _zLight* t = (_zLight*)to;

    if ((t->flags & 1) && t->attached_to)
    {
        xVec3 pos = *xEntGetPos((xEnt*)t->attached_to);
        pos.y += 1.0f;
        iLightSetPos(&t->light, &pos);
        t->light.sph.center = pos;
        t->true_idx = xPartitionUpdate(&sLightPart, t, t->true_idx, &pos);
    }

    if (sEffectFuncs[t->effect_idx])
    {
        sEffectFuncs[t->effect_idx](t, dt);
    }
}

void zLightAddLocalEnv()
{
    S32 i;
    _zLight* zlight;
    iLight* light;

    for (i = 0; i < sLightTotal; i++)
    {
        zlight = sLight[i];
        if ((zlight->flags & 1) && (zlight->tasset->lightFlags & 0x8))
        {
            light = &zlight->light;
            iLightEnv(light, 1);

            light->hw->inWorld.prev = gLightWorld->directionalLightList.link.prev;
            light->hw->inWorld.next = &gLightWorld->directionalLightList.link;
            gLightWorld->directionalLightList.link.prev->next = &light->hw->inWorld;
            gLightWorld->directionalLightList.link.prev = &light->hw->inWorld;

            gTemporaryLights[gNumTemporaryLights] = zlight;
            gNumTemporaryLights++;
        }
    }
}

void zLightAddLocal(xEnt* ent)
{
    xVec3 default_light_pos = *xEntGetPos(ent);
    default_light_pos.y += 1.0f;

    if (!ent->entShadow)
    {
        ent->entShadow = (xEntShadow*)xMemAlloc(gActiveHeap, sizeof(xEntShadow), 0);
        ent->entShadow->pos = default_light_pos;
        ent->entShadow->vec = sDefaultShadowVec;
    }

    xShadowSetLight(&ent->entShadow->pos, &ent->entShadow->vec, 1.0f);
}

void zLightRemoveLocalEnv()
{
    int i;
    const RwLLLink* link;

    for (i = 0; i < gNumTemporaryLights; i++)
    {
        link = gLightWorld->directionalLightList.link.prev;
        link->prev->next = link->next;
        link->next->prev = link->prev;
    }
    gNumTemporaryLights = 0;
}

void zLightSetVolume(zVolume* vol)
{
    if (!vol)
    {
        sPartitionVolume = NULL;
    }
    else
    {
        U32 lp_id = xStrHash("LIGHT_PARTITION");
        if (vol->id == lp_id)
        {
            sPartitionVolume = vol;
        }
    }
}

static inline void zLightDestroy(_zLight* t)
{
    iLightDestroy(&t->light);
}

static inline void zLightReset(_zLight* t)
{
    xBaseReset(t, t->tasset);
    t->flags = 0;
    if (t->tasset->lightFlags & 0x20)
    {
        t->flags |= 1;
    }
    zLightEffectSet(t, t->tasset->lightEffect);
}

static inline void zLightEffectSet(_zLight* zlight, S32 idx)
{
    if (zlight->reg)
    {
        zlight->effect_idx = idx;
        if (sEffectInitFuncs[zlight->effect_idx])
        {
            sEffectInitFuncs[zlight->effect_idx](zlight);
        }
    }
}
