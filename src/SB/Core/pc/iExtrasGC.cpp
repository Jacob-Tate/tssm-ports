// The Xbox extras in a level whose package is GameCube files; see iExtrasGC.h.

#include "iExtrasGC.h"

#include "iExtras.h"

#include "xBase.h"
#include "xCutsceneMgr.h"
#include "xEnt.h"
#include "xLinkAsset.h"
#include "xMemMgr.h"
#include "xpkrsvc.h"
#include "xstransvc.h"
#include "zBase.h"
#include "zDispatcher.h"
#include "zScene.h"

#include <stdio.h>
#include <string.h>

namespace
{
    // An object whose links change, its asset checked in iExtrasGCScenePrepare.
    struct Pending
    {
        U32 id;
        U32 type;
    };

    const S32 kMaxPending = 8;
    Pending sPending[kMaxPending];
    S32 sPendingCount;

    // An asset of the level by ID: its data when the packer has it as `type`, else NULL. A mod's
    // file may give an ID to anything.
    void* FindAsset(U32 id, U32 type, U32* size)
    {
        PKRAssetTOCInfo info;
        U32 n = 0;
        if (id == 0 || !xSTGetAssetInfo(id, &info) || info.typeref == NULL ||
            info.typeref->typetag != type)
        {
            return NULL;
        }
        void* a = xSTFindAsset(id, &n);
        if (a == NULL || n == 0)
        {
            return NULL;
        }
        if (size != NULL)
        {
            *size = n;
        }
        return a;
    }

    // A base asset (an object's) by ID: as FindAsset, and starting with its own ID.
    xBaseAsset* FindBase(U32 id, U32 type, U32* size)
    {
        xBaseAsset* a = (xBaseAsset*)FindAsset(id, type, size);
        return a != NULL && *size >= sizeof(xBaseAsset) && a->id == id ? a : NULL;
    }

    // Where an object's links are in its asset: right after the asset's own fields, as the
    // dispatcher's and the cutscene manager's init find them.
    U32 LinkOffset(U32 type)
    {
        return type == 'CSNM' ? (U32)sizeof(xCutsceneMgrAsset) : (U32)sizeof(xBaseAsset);
    }

    // BB02's crowd: each simple object with one of the rigid crowd models and no animation list
    // gets the skinned model and its list, when the level has both.
    void PointCrowd()
    {
        S32 rigid = 0;
        S32 pointed = 0;
        const S32 count = xSTAssetCountByType('SIMP');
        for (S32 i = 0; i < count; i++)
        {
            U32 size = 0;
            xEntAsset* a = (xEntAsset*)xSTFindAssetByType('SIMP', i, &size);
            U32 model;
            U32 list;
            if (a == NULL || size < sizeof(xEntAsset) || a->baseType != eBaseTypeStatic ||
                a->animListID != 0 || !iExtrasCrowdSkinned(a->modelInfoID, &model, &list))
            {
                continue;
            }
            rigid++;
            if (FindAsset(model, 'MODL', NULL) == NULL || FindAsset(list, 'ALST', NULL) == NULL)
            {
                continue;
            }
            a->modelInfoID = model;
            a->animListID = list;
            pointed++;
        }

        if (rigid > 0 && pointed == 0)
        {
            iExtrasFallBack(iEXTRAS_CROWDS,
                            "This level's GameCube files have no animation for their crowd, so it "
                            "stands still.");
        }
        else if (pointed > 0)
        {
            iExtrasNote(iEXTRAS_CROWDS, "(%d given the skinned crowd%s)", (int)pointed,
                        pointed < rigid ? ", some without its animation" : "");
        }
    }

    const char* const kSpotlightsDiffer =
        "This level's GameCube files are not the GameCube disc's where they switch the "
        "spotlights off, so they keep the GameCube look.";

    // The assets of the objects whose links change: each there, of its type, and with the links
    // the GameCube disc has. Any that is not leaves the level's spotlights the GameCube's.
    void CheckSpotlightLinks(U32 sceneID)
    {
        U32 ids[kMaxPending];
        U32 types[kMaxPending];
        const S32 n = iExtrasSpotlightLinkObjects(sceneID, ids, types, kMaxPending);
        static xLinkAsset scratch[256];
        for (S32 i = 0; i < n; i++)
        {
            U32 size = 0;
            const U8* a = (const U8*)FindBase(ids[i], types[i], &size);
            const U32 off = LinkOffset(types[i]);
            const S32 links = a != NULL ? ((const xBaseAsset*)a)->linkCount : -1;
            if (links < 0 || size < off || (size - off) / sizeof(xLinkAsset) < (U32)links ||
                iExtrasSpotlightLinks(sceneID, ids[i], (const xLinkAsset*)(a + off), links,
                                      scratch, 255) < 0)
            {
                sPendingCount = 0;
                iExtrasFallBack(iEXTRAS_SPOTLIGHTS, kSpotlightsDiffer);
                return;
            }
            sPending[sPendingCount].id = ids[i];
            sPending[sPendingCount].type = types[i];
            sPendingCount++;
        }
    }
} // namespace

void iExtrasGCScenePrepare(U32 sceneID)
{
    sPendingCount = 0;
    if (iExtrasLevelFiles() != iEXTRAS_FILES_GAMECUBE)
    {
        return;
    }
    if (iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_XBOX)
    {
        PointCrowd();
    }
    if (iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_XBOX)
    {
        CheckSpotlightLinks(sceneID);
    }
}

void iExtrasGCSceneObjects(U32 sceneID)
{
    if (sPendingCount == 0)
    {
        return;
    }

    // The objects first, so that one missing changes none.
    xBase* objects[kMaxPending];
    for (S32 i = 0; i < sPendingCount; i++)
    {
        const U8 want = sPending[i].type == 'CSNM' ? eBaseTypeCutsceneMgr : eBaseTypeDispatcher;
        objects[i] = zSceneFindObject(sPending[i].id);
        if (objects[i] == NULL || objects[i]->id != sPending[i].id ||
            objects[i]->baseType != want || (objects[i]->linkCount != 0 && objects[i]->link == NULL))
        {
            sPendingCount = 0;
            iExtrasFallBack(iEXTRAS_SPOTLIGHTS, kSpotlightsDiffer);
            return;
        }
    }

    for (S32 i = 0; i < sPendingCount; i++)
    {
        xBase* b = objects[i];
        static xLinkAsset links[256];
        const S32 n = iExtrasSpotlightLinks(sceneID, b->id, b->link, b->linkCount, links, 255);
        if (n < 0)
        {
            continue; // checked on the asset already; the object reads the same links
        }

        // In the scene's memory: kept as long as the objects, released with them.
        if (sPending[i].type == 'DPAT')
        {
            // A dispatcher's reset reads its links from its asset again (ZDSP_instReset), so it
            // gets a copy of its asset with the new links.
            st_ZDISPATCH_DATA* d = (st_ZDISPATCH_DATA*)b;
            const U32 bytes = (U32)(sizeof(xBaseAsset) + n * sizeof(xLinkAsset));
            xBaseAsset* copy = (xBaseAsset*)xMemAllocSize(bytes);
            *copy = *d->rawass;
            copy->linkCount = (U8)n;
            memcpy(copy + 1, links, n * sizeof(xLinkAsset));
            d->rawass = copy;
            d->linkCount = (U8)n;
            d->link = n != 0 ? (xLinkAsset*)(copy + 1) : NULL;
        }
        else
        {
            // The cutscene manager reads them once, at its init.
            xLinkAsset* copy = n != 0 ? (xLinkAsset*)xMemAllocSize(n * sizeof(xLinkAsset)) : NULL;
            if (copy != NULL)
            {
                memcpy(copy, links, n * sizeof(xLinkAsset));
            }
            b->linkCount = (U8)n;
            b->link = copy;
        }
    }

    iExtrasNote(iEXTRAS_SPOTLIGHTS, "(the GameCube files' links of %d object%s as the Xbox's)",
                (int)sPendingCount, sPendingCount == 1 ? "" : "s");
    sPendingCount = 0;
}
