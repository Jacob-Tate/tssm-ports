#include "zSurface.h"

#include "xDebug.h"
#include "xEvent.h"
#include "xGroup.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xRand.h"
#include "xstransvc.h"
#include "zMain.h"

#include <string.h>

#define MAX_MAPPER 20

static zSurfaceProps* zsps;
static S32 sMapperCount;
static zMaterialMapAsset* sMapper[MAX_MAPPER] = {};
static xSurface* pDefaultSurface;
static xSurface sDefaultSurface;
static zSurfaceProps sDefaultSurfaceProps;
static zSurfAssetIN sDefaultSurfaceAsset;

static inline void zSurfaceInitDefaultSurface()
{
    memset(&sDefaultSurfaceAsset, 0, sizeof(sDefaultSurfaceAsset));
    memset(&sDefaultSurfaceProps, 0, sizeof(sDefaultSurfaceProps));

    sDefaultSurface.friction = 1.0f;
    sDefaultSurface.state = 0;
    sDefaultSurface.moprops = &sDefaultSurfaceProps;
    sDefaultSurfaceProps.asset = &sDefaultSurfaceAsset;

    sDefaultSurfaceAsset.game_damage_type = 0;
    sDefaultSurfaceAsset.game_sticky = 0;
    sDefaultSurfaceAsset.game_damage_flags = 0;
    sDefaultSurfaceAsset.sld_start = 20;
    sDefaultSurfaceAsset.sld_stop = 10;
    sDefaultSurfaceAsset.phys_flags = 0;
    sDefaultSurfaceAsset.friction = 1.0f;
    sDefaultSurfaceAsset.oob_delay = -1.0f;

    pDefaultSurface = &sDefaultSurface;
}

static inline void zSurfaceResetSurface(xSurface* surf)
{
    xSurfaceReset(surf);
    surf->friction = ((zSurfaceProps*)surf->moprops)->asset->friction;
}

static void zSurfaceEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam,
                            xBase* toParamWidget, U32);

void zSurfaceInit()
{
    U32 size;
    U16 nsurfs;
    zSurfAssetIN* defaultAsset;

    nsurfs = xSTAssetCountByType('SURF');

    xSurfaceInit(nsurfs);

    // xStrHash("DEFAULT SURFACE")
    defaultAsset = (zSurfAssetIN*)xSTFindAsset(0x58DA240E, NULL);

    if (nsurfs)
    {
        zsps = (zSurfaceProps*)xMemAllocSize(nsurfs * sizeof(zSurfaceProps));

        for (U16 i = 0; i < nsurfs; i++)
        {
            zSurfAssetIN* asset;
            xSurface* surf;
            zSurfaceProps* moprops;

            surf = xSurfaceGetByIdx(i);
            moprops = &zsps[i];
            surf->moprops = moprops;

            asset = (zSurfAssetIN*)xSTFindAssetByType('SURF', i, &size);
            if (asset == defaultAsset)
            {
                pDefaultSurface = surf;
            }
            moprops->asset = asset;

            xBaseInit(surf, asset);

            if (surf->linkCount)
            {
                surf->link = (xLinkAsset*)(asset + 1);
            }
            else
            {
                surf->link = NULL;
            }

            surf->eventFunc = zSurfaceEventCB;
            surf->friction = asset->friction;
            surf->state = !asset->on;
            moprops->uvfx_flags = asset->uvfx_flags;
            moprops->texanim_flags = asset->texture_anim_flags;

            for (S32 j = 0; j < 2; j++)
            {
                moprops->texanim[j].mode = asset->texture_anim[j].mode;
                moprops->texanim[j].speed = asset->texture_anim[j].speed;
                moprops->texanim[j].group = asset->texture_anim[j].group;
                moprops->texanim[j].group_idx = 0;
                moprops->uvfx[j].mode = asset->uvfx[j].mode;
                // Retail clamps the not-yet-initialized destination (inverted bounds),
                // then overwrites it with the asset value below.
                if (moprops->uvfx[j].rot <= 360.0f)
                {
                    moprops->uvfx[j].rot = 360.0f;
                }
                if (moprops->uvfx[j].rot >= 0.0f)
                {
                    moprops->uvfx[j].rot = 0.0f;
                }
                moprops->uvfx[j].rot = asset->uvfx[j].rot;
                moprops->uvfx[j].rot_spd = asset->uvfx[j].rot_spd;
                moprops->uvfx[j].trans = asset->uvfx[j].trans;
                moprops->uvfx[j].trans_spd = asset->uvfx[j].trans_spd;
                moprops->uvfx[j].scale = asset->uvfx[j].scale;
                moprops->uvfx[j].scale_spd = asset->uvfx[j].scale_spd;
                moprops->uvfx[j].min = asset->uvfx[j].min;
                moprops->uvfx[j].max = asset->uvfx[j].max;
                moprops->uvfx[j].minmax_spd = asset->uvfx[j].minmax_spd;
                moprops->uvfx[j].minmax_timer[0] = 0.0f;
                moprops->uvfx[j].minmax_timer[1] = 0.0f;
            }
        }
    }
    else
    {
        zsps = NULL;
    }

    if (!pDefaultSurface)
    {
        zSurfaceInitDefaultSurface();
    }
}

// No GC symbol: inlined into zSceneExit.
inline void zSurfaceExit()
{
    xSurfaceExit();
    sMapperCount = 0;
}

void zSurfaceRegisterMapper(U32 assetId)
{
    if (sMapperCount >= MAX_MAPPER)
    {
        return;
    }
    if (!assetId)
    {
        return;
    }

    sMapper[sMapperCount] = (zMaterialMapAsset*)xSTFindAsset(assetId, NULL);
    if (sMapper[sMapperCount])
    {
        sMapperCount++;
    }
}

xSurface* zSurfaceGetSurface(U32 mat_id)
{
    for (S32 map = 0; map < sMapperCount; map++)
    {
        zMaterialMapAsset* mapper = sMapper[map];
        if (mapper)
        {
            for (U16 i = 0; i < mapper->count; i++)
            {
                zMaterialMapEntry* entry = (zMaterialMapEntry*)(mapper + 1) + i;
                if (entry->materialIndex == mat_id)
                {
                    U16 nsurfs = xSurfaceGetNumSurfaces();
                    for (U16 j = 0; j < nsurfs; j++)
                    {
                        xSurface* surf = xSurfaceGetByIdx(j);
                        if (surf->id == entry->surfaceAssetID)
                        {
                            return surf;
                        }
                    }
                }
            }
        }
    }
    return pDefaultSurface;
}

xSurface* zSurfaceGetSurface(const xCollis* coll)
{
    xSurface* surf = NULL;

    if (coll->flags & 0x1)
    {
        if (coll->optr)
        {
            if (coll->mptr)
            {
                surf = coll->mptr->Surf;
            }
        }
        else
        {
            surf = zSurfaceGetSurface(coll->oid);
        }
    }
    if (surf != NULL)
    {
        return surf;
    }
    return pDefaultSurface;
}

U32 zSurfaceGetSlide(const xSurface* surf)
{
    if (surf->moprops)
    {
        return ((zSurfaceProps*)surf->moprops)->asset->phys_flags & 1;
    }
    return 0;
}

U32 zSurfaceGetStep(const xSurface* surf)
{
    if (surf->moprops)
    {
        return ((zSurfaceProps*)surf->moprops)->asset->phys_flags & 4;
    }
    return 0;
}

U8 zSurfaceOutOfBounds(const xSurface& s)
{
    if (s.moprops)
    {
        return ((zSurfaceProps*)s.moprops)->asset->phys_flags >> 4 & 1;
    }
    return 0;
}

F32 zSurfaceGetSlideStartAngle(const xSurface* surf)
{
    if (surf->moprops)
    {
        return (PI / 180.0f) * ((zSurfaceProps*)surf->moprops)->asset->sld_start;
    }
    return 0.34906584f;
}

F32 zSurfaceGetSlideStopAngle(const xSurface* surf)
{
    if (surf->moprops)
    {
        return (PI / 180.0f) * ((zSurfaceProps*)surf->moprops)->asset->sld_stop;
    }
    return 0.17453292f;
}

U32 zSurfaceGetMatchOrient(const xSurface* surf)
{
    if (surf->moprops)
    {
        return ((zSurfaceProps*)surf->moprops)->asset->phys_flags & 2;
    }
    return 0;
}

S32 zSurfaceGetDamageType(const xSurface* surf)
{
    if (surf->moprops)
    {
        return ((zSurfaceProps*)surf->moprops)->asset->game_damage_type;
    }
    return 0;
}

U32 zSurfaceGetDamagePassthrough(const xSurface* surf)
{
    if (surf->moprops)
    {
        return ((zSurfaceProps*)surf->moprops)->asset->game_damage_flags & 1;
    }
    return 0;
}

U32 zSurfaceGetSticky(const xSurface* surf)
{
    if (surf->moprops)
    {
        return ((zSurfaceProps*)surf->moprops)->asset->game_sticky;
    }
    return 0;
}

U32 zSurfaceGetStandOn(const xSurface* surf)
{
    if (surf->moprops)
    {
        return !(((zSurfaceProps*)surf->moprops)->asset->phys_flags & 8);
    }
    return 1;
}

F32 zSurfaceGetFriction(const xSurface* surf)
{
    return surf->friction;
}

F32 zSurfaceGetOutOfBoundsDelay(const xSurface& s)
{
    if (s.moprops)
    {
        return ((zSurfaceProps*)s.moprops)->asset->oob_delay;
    }
    return -1.0f;
}

S32 zSurfaceGetSlickness(const xSurface* surf)
{
    return (S32)(1.0f / surf->friction);
}

F32 zSurfaceGetDamping(const xSurface* surf, F32 min_vel)
{
    return xpow(min_vel, surf->friction);
}

// No GC or PS2 symbol (inlined into zSurfaceSetup); name from Ratatouille. num_items is unsigned
// but i is int: retail tests the count unsigned and runs the loop signed.
inline void CacheTextureRefs(zSurfacePropTexAnim* texanim)
{
    xGroup* grp = (xGroup*)texanim->group_ptr;

    if (grp)
    {
        U32 num_items = xGroupGetCount(grp);

        if (num_items >= 1)
        {
            U32 amt = num_items * sizeof(RwTexture*);
            void* mem = xMemAlloc(gActiveHeap, amt, 0);

            memset(mem, 0, amt);
            texanim->txtr_animList = (RwTexture**)mem;

            for (S32 i = 0; i < (S32)num_items; i++)
            {
                U32 aid_item = xGroupGetItem(grp, i);

                if (aid_item)
                {
                    RwTexture* txtr = (RwTexture*)xSTFindAsset(aid_item, NULL);

                    if (txtr)
                    {
                        texanim->txtr_animList[i] = txtr;
                    }
                }
            }
        }
    }
}

// No GC symbol (inlined into zSceneSetup and dead-stripped); a real function in the PS2 build.
void zSurfaceSetup(xSurface* s)
{
    zSurfaceProps* pp = (zSurfaceProps*)s->moprops;
    S32 i;

    if (pp)
    {
        for (i = 0; i < 2; i++)
        {
            pp->texanim[i].group_ptr = NULL;

            if (pp->texanim[i].group)
            {
                pp->texanim[i].group_ptr = zSceneFindObject(pp->texanim[i].group);

                if (pp->texanim[i].group_ptr)
                {
                    pp->texanim[i].txtr_animList = NULL;
                    CacheTextureRefs(&pp->texanim[i]);
                }
            }
        }

        if (pp->asset->matfx.dualmapID)
        {
            pp->txtr_matFXDualMap = (RwTexture*)xSTFindAsset(pp->asset->matfx.dualmapID, NULL);
        }
    }
}

// Weak in retail (emitted right after its only caller, zSurfaceUpdate).
inline void zSurfaceUpdateTextureAnim(F32 dt, xSurface* s, zSurfaceProps* moprops)
{
    F32 timestep_forUV = dt;
    S32 j;

    for (j = 0; j < 2; j++)
    {
        if (j == 0 && !(moprops->uvfx_flags & UVANIM_FLAG_ON))
        {
            continue;
        }
        if (j == 1 && !(moprops->uvfx_flags & UVANIM_FLAG_ON2))
        {
            continue;
        }

        switch (moprops->uvfx[j].mode)
        {
        case 0:
        {
            xVec3 d;

            moprops->uvfx[j].rot += moprops->uvfx[j].rot_spd * dt;
            while (moprops->uvfx[j].rot >= 360.0f)
                moprops->uvfx[j].rot -= 360.0f;
            while (moprops->uvfx[j].rot < 0.0f)
                moprops->uvfx[j].rot += 360.0f;

            xVec3SMul(&d, &moprops->uvfx[j].trans_spd, timestep_forUV);
            xVec3Add(&moprops->uvfx[j].trans, &d, &moprops->uvfx[j].trans);
            while (moprops->uvfx[j].trans.x >= 1.0f)
                moprops->uvfx[j].trans.x -= 1.0f;
            while (moprops->uvfx[j].trans.x < 0.0f)
                moprops->uvfx[j].trans.x += 1.0f;
            while (moprops->uvfx[j].trans.y >= 1.0f)
                moprops->uvfx[j].trans.y -= 1.0f;
            while (moprops->uvfx[j].trans.y < 0.0f)
                moprops->uvfx[j].trans.y += 1.0f;

            xVec3SMul(&d, &moprops->uvfx[j].scale_spd, dt);
            xVec3Add(&moprops->uvfx[j].scale, &d, &moprops->uvfx[j].scale);
            while (moprops->uvfx[j].scale.x >= 8.0f)
                moprops->uvfx[j].scale.x -= 8.0f;
            while (moprops->uvfx[j].scale.x < 0.0f)
                moprops->uvfx[j].scale.x += 8.0f;
            while (moprops->uvfx[j].scale.y >= 8.0f)
                moprops->uvfx[j].scale.y -= 8.0f;
            while (moprops->uvfx[j].scale.y < 0.0f)
                moprops->uvfx[j].scale.y += 8.0f;
            break;
        }
        case 1:
            moprops->uvfx[j].trans.x = isin(2.0f * gFrameCount * SECS_PER_VBLANK);
            moprops->uvfx[j].trans.y = isin(2.0f * gFrameCount * SECS_PER_VBLANK);
            moprops->uvfx[j].scale.x = isin(2.0f * gFrameCount * SECS_PER_VBLANK);
            moprops->uvfx[j].scale.y = isin(2.0f * gFrameCount * SECS_PER_VBLANK);
            break;
        case 2:
        {
            zSurfacePropUVFX& sfx = moprops->uvfx[j];

            if (sfx.minmax_spd.x != 0.0f)
            {
                sfx.minmax_timer[0] += dt;

                F32 uTime = sfx.minmax_timer[0] * sfx.minmax_spd.x;
                S32 uTimeInt = (S32)uTime;
                if (uTimeInt > 1)
                {
                    sfx.minmax_timer[0] -= 2.0f / sfx.minmax_spd.x;
                }
                uTime -= uTimeInt & ~1;

                sfx.trans.x = sfx.min.x + (sfx.max.x - sfx.min.x) * isin(HALF_PI * uTime);
            }
            else
            {
                sfx.trans.x = sfx.min.x;
            }

            if (sfx.minmax_spd.y != 0.0f)
            {
                sfx.minmax_timer[1] += dt;

                F32 vTime = sfx.minmax_timer[1] * sfx.minmax_spd.y;
                S32 vTimeInt = (S32)vTime;
                if (vTimeInt > 1)
                {
                    sfx.minmax_timer[1] -= 2.0f / sfx.minmax_spd.y;
                }
                vTime -= vTimeInt & ~1;

                sfx.trans.y = sfx.min.y + (sfx.max.y - sfx.min.y) * isin(HALF_PI * vTime);
            }
            else
            {
                sfx.trans.y = sfx.min.y;
            }

            sfx.scale.x = 1.0f;
            sfx.scale.y = 1.0f;
            break;
        }
        }
    }

    for (j = 0; j < 2; j++)
    {
        if (j == 0 && !(moprops->texanim_flags & SURF_TEXANIM_ON))
        {
            continue;
        }
        if (j == 1 && !(moprops->texanim_flags & SURF_TEXANIM_ON2))
        {
            continue;
        }

        xGroup* g = (xGroup*)moprops->texanim[j].group_ptr;
        if (g)
        {
            U32 max = xGroupGetCount(g);
            if (max >= 1)
            {
                if (moprops->texanim[j].mode == 0)
                {
                    moprops->texanim[j].frame += moprops->texanim[j].speed * dt;
                    if (moprops->texanim[j].frame >= 1.0f)
                    {
                        moprops->texanim[j].group_idx += (U32)moprops->texanim[j].frame;
                        moprops->texanim[j].frame -= (S32)moprops->texanim[j].frame;
                    }
                }
                else if (moprops->texanim[j].mode == 1)
                {
                    moprops->texanim[j].frame += moprops->texanim[j].speed * dt;
                    if (moprops->texanim[j].frame >= 1.0f)
                    {
                        moprops->texanim[j].group_idx -= (U32)moprops->texanim[j].frame;
                        moprops->texanim[j].frame -= (S32)moprops->texanim[j].frame;
                    }
                }
                else if (moprops->texanim[j].mode == 2)
                {
                    moprops->texanim[j].frame += moprops->texanim[j].speed * dt;
                    if (moprops->texanim[j].frame >= 1.0f)
                    {
                        moprops->texanim[j].group_idx = xrand_RandomChoice(max);
                        moprops->texanim[j].frame -= (S32)moprops->texanim[j].frame;
                    }
                }

                if (moprops->texanim[j].group_idx >= max)
                {
                    moprops->texanim[j].group_idx = 0;
                }
            }
        }
    }
}

void zSurfaceUpdate(xBase* to, xScene*, F32 dt)
{
    xSurface* t = (xSurface*)to;
    zSurfaceUpdateTextureAnim(dt, t, (zSurfaceProps*)t->moprops);
}

static void zSurfaceEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam,
                            xBase* toParamWidget, U32)
{
    xSurface* t = (xSurface*)to;

    switch (toEvent)
    {
    case eEventOn:
        t->state = 0;
        break;
    case eEventOff:
        t->state = 1;
        break;
    case eEventToggle:
        if (t->state == 0)
        {
            t->state = 1;
        }
        else
        {
            t->state = 0;
        }
        break;
    case eEventReset:
        zSurfaceResetSurface(t);
        t->state = 0;
        break;
    case eEventTextureAnimateOn:
    {
        zSurfaceProps* p = (zSurfaceProps*)t->moprops;
        if (p)
        {
            if (toParam[3] == 0.0f)
            {
                p->texanim_flags |= SURF_TEXANIM_ON;
            }
            else
            {
                p->texanim_flags |= SURF_TEXANIM_ON2;
            }
        }
        break;
    }
    case eEventTextureAnimateStep:
    {
        zSurfaceProps* p = (zSurfaceProps*)t->moprops;
        if (p && (p->texanim_flags & SURF_TEXANIM_ON))
        {
            if (toParam[3] == 0.0f)
            {
                p->texanim[0].frame += toParam[0];
            }
            else
            {
                p->texanim[1].frame += toParam[0];
            }
        }
        break;
    }
    case eEventTextureAnimateOff:
    {
        zSurfaceProps* p = (zSurfaceProps*)t->moprops;
        if (p)
        {
            if (toParam[3] == 0.0f)
            {
                p->texanim_flags &= ~SURF_TEXANIM_ON;
            }
            else
            {
                p->texanim_flags &= ~SURF_TEXANIM_ON2;
            }
        }
        break;
    }
    case eEventTextureAnimateToggle:
    {
        zSurfaceProps* p = (zSurfaceProps*)t->moprops;
        if (p)
        {
            if (toParam[3] == 0.0f)
            {
                // Not in the DWARF; retail reads the flags once (probably an inlined helper).
                U32 flags = p->texanim_flags;
                if (flags & SURF_TEXANIM_ON)
                {
                    p->texanim_flags = flags & ~SURF_TEXANIM_ON;
                }
                else
                {
                    p->texanim_flags = flags | SURF_TEXANIM_ON;
                }
            }
            else
            {
                U32 flags = p->texanim_flags;
                if (flags & SURF_TEXANIM_ON2)
                {
                    p->texanim_flags = flags & ~SURF_TEXANIM_ON2;
                }
                else
                {
                    p->texanim_flags = flags | SURF_TEXANIM_ON2;
                }
            }
        }
        break;
    }
    case eEventSetTextureAnimGroup:
    {
        zSurfaceProps* p = (zSurfaceProps*)t->moprops;
        if (p)
        {
            if (toParamWidget)
            {
                S32 idx = (toParam[3] == 0.0f) ? 0 : 1;
                p->texanim[idx].group = toParamWidget->id;
                p->texanim[idx].group_ptr = toParamWidget;
            }
            else
            {
                if (toParam[3] == 0.0f)
                {
                    p->texanim[0].group = 0;
                    p->texanim[0].group_ptr = NULL;
                }
                else
                {
                    p->texanim[1].group = 0;
                    p->texanim[1].group_ptr = NULL;
                }
            }
        }
        break;
    }
    case eEventSetTextureAnimSpeed:
    {
        zSurfaceProps* p = (zSurfaceProps*)t->moprops;
        if (p)
        {
            if (toParam[3] == 0.0f)
            {
                p->texanim[0].speed = toParam[0];
            }
            else
            {
                p->texanim[1].speed = toParam[0];
            }
        }
        break;
    }
    }
}

void zSurfaceGetName(S32 type, char* buffer)
{
    *buffer = '\0';
    switch (type)
    {
    case 0:
        strcpy(buffer, "DEFAULT");
        break;
    case 1:
        strcpy(buffer, "TILE");
        break;
    case 2:
        strcpy(buffer, "CARPET");
        break;
    case 3:
        strcpy(buffer, "SHORTGRASS");
        break;
    case 4:
        strcpy(buffer, "LONGGRASS");
        break;
    case 5:
        strcpy(buffer, "GRAVEL");
        break;
    case 6:
        strcpy(buffer, "DIRT");
        break;
    case 7:
        strcpy(buffer, "MUD");
        break;
    case 8:
        strcpy(buffer, "THORNS");
        break;
    case 9:
        strcpy(buffer, "METAL");
        break;
    case 10:
        strcpy(buffer, "SHEETMETAL");
        break;
    case 11:
        strcpy(buffer, "CONCRETE");
        break;
    case 12:
        strcpy(buffer, "MARBLE");
        break;
    case 13:
        strcpy(buffer, "STONE");
        break;
    case 14:
        strcpy(buffer, "WOOD");
        break;
    case 15:
        strcpy(buffer, "SNOW");
        break;
    case 16:
        strcpy(buffer, "ICE");
        break;
    case 17:
        strcpy(buffer, "SHALLOWWATER");
        break;
    case 18:
        strcpy(buffer, "DEEPWATER");
        break;
    case 19:
        strcpy(buffer, "TAR");
        break;
    case 20:
        strcpy(buffer, "SPECIAL1");
        break;
    case 21:
        strcpy(buffer, "DeepAcid");
        break;
    case 22:
        strcpy(buffer, "NONE");
    }
}

xSurface& zSurfaceGetDefault()
{
    return *pDefaultSurface;
}
