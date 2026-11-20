// The Xbox files' water bodies drawn the GameCube way when the Xbox water does not take them
// (iWaterBody.h). Port-only; compiled with the game units because it calls game code.

#include "iWaterBody.h"

#include "iXboxWater.h"

#include "xEnt.h"
#include "xEvent.h"
#include "xGroup.h"
#include "xModel.h"
#include "xString.h"
#include "xstransvc.h"
#include "zBase.h"
#include "zScene.h"

#include <stdio.h>
#include <string.h>

namespace
{
    // The asset (zWater.cpp's asset_type in the PS2 symbols, iXboxWater.h): the water model (a
    // SIMP), and the ents or groups drawn into its refraction and reflection.
    typedef zWaterBody::asset_type WaterBodyAsset;

    // What the last iWaterBodyScenePrepare did (iWaterBodyPrepared).
    char sPrepared[160];

    // What the GameCube file has in a water body's place. The IDs are the level files' own.
    struct GameCubeWater
    {
        U32 waterBody;
        // Shown at SceneBegin; the sky dome is made one (sort order 0, not locked) after that.
        U32 show[2];
        U32 skyDome;
        // The water model's pipe settings are copied from this model's (0: kept), its SIMP gets
        // this surface (0: kept), and at SceneBegin it becomes goo with this wobble (GooSetWarb).
        U32 pipeFrom;
        U32 surface;
        bool goo;
        F32 warb[4];
    };

    const GameCubeWater kGameCube[] = {
        // B301 WATER_BODY_XBOX_02. The GameCube file's WATER_NGC_DISP shows WATER_BODY_NGC_SIMP
        // (the sea, already goo through its own links) and REFLECTION_SKY_PS2_SIMP, and makes the
        // latter a sky dome (0, 0, 1, 0).
        { 0xCF2104D9, { 0x8A806F6B, 0x4C7C006F }, 0x4C7C006F, 0, 0, false, {} },
        // SC02 WATER_BODY_01 and _02. The GameCube file's water, WATER_NGC_SIMP_01 and _02, is goo
        // with a wobble of 0.25, 10, 0.25, 9 and the surface WATER_NGC_SURF (1F1A78EF, in the Xbox
        // file too), which scrolls the texture. Its pipe settings (alpha blended, no light kit, the
        // UV transform the scroll needs) are in the Xbox package as those of sc02_water_body_gc_01
        // and _02 (0A0D58FC, 0A0D58FD), meshes that nothing places (fainter than the GameCube's:
        // another texture at a quarter alpha). The Xbox water models have the GameCube's texture,
        // sc02_caustics.
        { 0x5F43C5DA, { 0, 0 }, 0, 0x0A0D58FC, 0x1F1A78EF, true, { 0.25f, 10.0f, 0.25f, 9.0f } },
        { 0x5F43C5DB, { 0, 0 }, 0, 0x0A0D58FD, 0x1F1A78EF, true, { 0.25f, 10.0f, 0.25f, 9.0f } },
    };

    const GameCubeWater* FindGameCube(U32 id)
    {
        for (size_t i = 0; i < sizeof(kGameCube) / sizeof(kGameCube[0]); i++)
        {
            if (kGameCube[i].waterBody == id)
            {
                return &kGameCube[i];
            }
        }
        return NULL;
    }

    // A model's entry in the scene's pipe tables ('PIPT': the count, then the entries), before
    // zSceneInit reads them. Entries past the asset's end are not looked at.
    xModelPipeTable* FindPipe(U32 model)
    {
        const S32 tables = xSTAssetCountByType('PIPT');
        for (S32 t = 0; t < tables; t++)
        {
            U32 size = 0;
            S32* count = (S32*)xSTFindAssetByType('PIPT', t, &size);
            if (count == NULL || size < sizeof(S32))
            {
                continue;
            }

            xModelPipeTable* e = (xModelPipeTable*)(count + 1);
            const U32 room = (size - sizeof(S32)) / sizeof(xModelPipeTable);
            for (S32 i = 0; i < *count && (U32)i < room; i++)
            {
                if (e[i].ModelHashID == model)
                {
                    return &e[i];
                }
            }
        }
        return NULL;
    }

    // A modded file's IDs may name any asset (a model, a texture), and its group counts may run
    // past the group. So an asset is used only when it is a base asset that starts with its own
    // ID, with its type in the file and its size, and the walks below stop after this many
    // assets (a group may hold itself) or this deep.
    const S32 kMaxVisits = 1024;
    const S32 kMaxDepth = 4;

    xBaseAsset* FindBase(U32 id, U32* tag, U32* size)
    {
        PKRAssetTOCInfo info;
        U32 n = 0;
        xBaseAsset* a = id != 0 && xSTGetAssetInfo(id, &info) && info.typeref != NULL ?
                            (xBaseAsset*)xSTFindAsset(id, &n) :
                            NULL;
        if (a == NULL || n < sizeof(xBaseAsset) || a->id != id)
        {
            return NULL;
        }

        *tag = info.typeref->typetag;
        *size = n;
        return a;
    }

    // The Xbox files' helpers are simple objects and platforms; the water models are simple
    // objects.
    xEntAsset* AsEnt(xBaseAsset* a, U32 tag, U32 size)
    {
        const bool ent = (tag == 'SIMP' && a->baseType == eBaseTypeStatic) ||
                         (tag == 'PLAT' && a->baseType == eBaseTypePlatform);
        return ent && size >= sizeof(xEntAsset) ? (xEntAsset*)a : NULL;
    }

    // Hides an ent, or every ent of a group (groups of groups too), found through their assets.
    // `now` false, before the objects are made: clears the visible flag in the ent's asset, so
    // the ent starts hidden and every reset (xEntReset reads the asset) keeps it hidden. `now`
    // true, at SceneBegin: sends the ent's object Invisible, as a link would. `budget` is how many
    // more assets the walk may visit. Returns how many ents.
    S32 Hide(U32 id, bool now, S32 depth, S32* budget)
    {
        if (depth > kMaxDepth || *budget <= 0)
        {
            return 0;
        }
        (*budget)--;

        U32 tag = 0;
        U32 size = 0;
        xBaseAsset* a = FindBase(id, &tag, &size);
        if (a == NULL)
        {
            return 0;
        }

        if (tag == 'GRUP' && a->baseType == eBaseTypeGroup)
        {
            // Items past the asset's end are not read.
            const xGroupAsset* g = (const xGroupAsset*)a;
            if (size < sizeof(xGroupAsset))
            {
                return 0;
            }

            const U32 room = (size - sizeof(xGroupAsset)) / sizeof(U32);
            const U32 count = g->itemCount < room ? g->itemCount : room;
            const U32* items = (const U32*)(g + 1);
            S32 n = 0;
            for (U32 i = 0; i < count && *budget > 0; i++)
            {
                n += Hide(items[i], now, depth + 1, budget);
            }
            return n;
        }

        xEntAsset* ent = AsEnt(a, tag, size);
        if (ent == NULL)
        {
            return 0;
        }

        if (!now)
        {
            ent->flags &= (U8)~0x1;
            return 1;
        }

        xBase* b = zSceneFindObject(id);
        if (b == NULL || b->baseType != a->baseType)
        {
            return 0;
        }

        zEntEvent(b, eEventInvisible);
        return 1;
    }

    S32 HideAll(U32 id, bool now)
    {
        S32 budget = kMaxVisits;
        return Hide(id, now, 0, &budget);
    }
} // namespace

void iWaterBodyScenePrepare()
{
    const U32 type = xStrHash("effect:water_body");
    const S32 count = xSTAssetCountByType('DYNA');
    S32 bodies = 0;
    S32 hidden = 0;
    S32 pipes = 0;

    for (S32 i = 0; i < count; i++)
    {
        U32 size = 0;
        WaterBodyAsset* a = (WaterBodyAsset*)xSTFindAssetByType('DYNA', i, &size);
        if (a == NULL || size < sizeof(WaterBodyAsset) || a->type != type)
        {
            continue;
        }

        bodies++;
        hidden += HideAll(a->facade_refract, false) + HideAll(a->facade_reflect, false);

        // Only while the body is still the Xbox file's SIMP and the entries exist (a mod may
        // have changed any of them).
        const GameCubeWater* gc = FindGameCube(a->id);
        U32 tag = 0;
        U32 bodySize = 0;
        xBaseAsset* base = gc != NULL ? FindBase(a->body, &tag, &bodySize) : NULL;
        xEntAsset* body = base != NULL && tag == 'SIMP' ? AsEnt(base, tag, bodySize) : NULL;
        if (body != NULL)
        {
            xModelPipeTable* to = FindPipe(body->modelInfoID);
            const xModelPipeTable* from = gc->pipeFrom != 0 ? FindPipe(gc->pipeFrom) : NULL;
            if (to != NULL && from != NULL)
            {
                to->Pipe = from->Pipe;
                pipes++;
            }
            if (gc->surface != 0 && xSTFindAsset(gc->surface, NULL) != NULL)
            {
                body->surfaceID = gc->surface;
            }
        }
    }

    int len = snprintf(sPrepared, sizeof(sPrepared),
                       "%d water bod%s drawn the GameCube way: %d reflection/refraction model%s "
                       "hidden",
                       (int)bodies, bodies == 1 ? "y" : "ies", (int)hidden, hidden == 1 ? "" : "s");
    if (pipes > 0 && len > 0 && (size_t)len < sizeof(sPrepared))
    {
        snprintf(sPrepared + len, sizeof(sPrepared) - len,
                 ", %d given the GameCube water's pipe settings", (int)pipes);
    }
}

const char* iWaterBodyPrepared()
{
    return sPrepared;
}

void iWaterBodySceneBegin(const zWaterBody& w)
{
    // A short (modded) asset: zWaterBody::load keeps the object, and it does nothing.
    if (w.asset == NULL)
    {
        return;
    }
    const WaterBodyAsset& a = *w.asset;

    // Done already by iWaterBodyScenePrepare, unless the Xbox water was to run and this body's
    // object could not be used: from here on every reset keeps the helpers hidden too.
    HideAll(a.facade_refract, false);
    HideAll(a.facade_reflect, false);

    // B301's WATER_XBOX_DISP shows the Xbox sky at SceneBegin, before this.
    HideAll(a.facade_refract, true);
    HideAll(a.facade_reflect, true);

    const GameCubeWater* gc = FindGameCube(a.id);
    if (gc == NULL)
    {
        return;
    }

    for (S32 i = 0; i < 2; i++)
    {
        xBase* b = gc->show[i] != 0 ? zSceneFindObject(gc->show[i]) : NULL;
        if (b != NULL)
        {
            zEntEvent(b, eEventVisible);
        }
    }

    xBase* sky = gc->skyDome != 0 ? zSceneFindObject(gc->skyDome) : NULL;
    if (sky != NULL)
    {
        zEntEvent(sky, eEventSetSkyDome, 0.0f, 0.0f, 1.0f, 0.0f);
    }

    xBase* body = gc->goo ? zSceneFindObject(a.body) : NULL;
    if (body != NULL)
    {
        zEntEvent(body, eEventSetGoo, 0.0f, 0.0f, 0.0f, 0.0f);
        zEntEvent(body, eEventGooSetWarb, gc->warb);
    }
}
