#include "xSnd.h"
#include "xBase.h"
#include "xEvent.h"
#include "xRand.h"
#include "zScene.h"
#include "zGlobals.h"

// Declared here rather than through zMenuSB.h: that header pulls in zUI.h -> zTextBox.h ->
// xFont.h, whose screen_bounds retail's .rodata places after xstransvc's data (xFont.h was
// first included by xSubTitles.cpp in this wad).
bool zMenuSB_EnsureFreeAudioStream(bool willingToStopMainGameMusic);

static void xSoundFXPlay(xSoundFX* t);

void xSoundFXUpdateEnvironmentalStreamSounds(xSoundFX* pSoundFXList, U32 numSounds)
{
    U32 i;
    static xSoundFX* bestSoundFX[1];
    static F32 bestDist2[1];

    for (i = 0; i < numSounds; i++)
    {
        if ((pSoundFXList[i].asset->uFlags & 0x1) && pSoundFXList[i].sndHandle != (iSndHandle)-1 &&
            !xSndMgrIsPlaying(pSoundFXList[i].sndHandle))
        {
            pSoundFXList[i].sndHandle = (iSndHandle)-1;
            pSoundFXList[i].asset->uFlags &= ~0x2;
            zEntEvent(&pSoundFXList[i], 0, &pSoundFXList[i], eEventDone, NULL, NULL, 0, FE_NO);
        }
    }

    memset(bestSoundFX, 0, sizeof(bestSoundFX));

    for (i = 0; i < numSounds; i++)
    {
        if (xSndMgrIsStream(pSoundFXList[i].asset->soundAsset) &&
            (pSoundFXList[i].asset->uFlags & 0x2))
        {
            xVec3 playPos;

            xSndMgrProcessSoundPosition(&pSoundFXList[i].asset->pos, &playPos);

            xVec3 delta = gSnd.pos - playPos;
            // Retail computes the distance once (the PS2 DWARF lists no local for it).
            F32 dist2 = delta.length2();

            if (dist2 > pSoundFXList[i].cachedOuterDistSquared)
            {
                continue;
            }

            // A candidate stored by one search skips the remaining searches: retail branches
            // straight from each store to the next sound (each search has its own j scope).
            for (S32 j = 0; j < 1; j++)
            {
                if (bestSoundFX[j] == NULL)
                {
                    bestSoundFX[j] = &pSoundFXList[i];
                    bestDist2[j] = dist2;
                    goto nextSound;
                }
            }

            for (S32 j = 0; j < 1; j++)
            {
                if (xSndMgrGetPriority(bestSoundFX[j]->asset->soundAsset, false, false) <
                    xSndMgrGetPriority(pSoundFXList[i].asset->soundAsset, false, false))
                {
                    bestSoundFX[j] = &pSoundFXList[i];
                    bestDist2[j] = dist2;
                    goto nextSound;
                }
            }

            for (S32 j = 0; j < 1; j++)
            {
                if (xSndMgrGetPriority(bestSoundFX[j]->asset->soundAsset, false, false) ==
                        xSndMgrGetPriority(pSoundFXList[i].asset->soundAsset, false, false) &&
                    bestDist2[j] > dist2)
                {
                    bestSoundFX[j] = &pSoundFXList[i];
                    bestDist2[j] = dist2;
                    break;
                }
            }
        }
    nextSound:;
    }

    for (i = 0; i < 1; i++)
    {
        U8 found = FALSE;

        for (S32 j = 0; j < 1; j++)
        {
            if (s_managedEnvSoundFX[i] == bestSoundFX[j])
            {
                found = TRUE;
                bestSoundFX[j] = NULL;
                break;
            }
        }

        if (!found && s_managedEnvSoundFX[i] != NULL)
        {
            xSndMgrStop(s_managedEnvSoundFX[i]->sndHandle);
            s_managedEnvSoundFX[i] = NULL;
        }
    }

    for (i = 0; i < 1; i++)
    {
        if (bestSoundFX[i] != NULL)
        {
            for (S32 j = 0; j < 1; j++)
            {
                if (s_managedEnvSoundFX[j] == NULL)
                {
                    s_managedEnvSoundFX[j] = bestSoundFX[i];
                    break;
                }
            }

            xSoundFXPlay(bestSoundFX[i]);
        }
    }
}

static bool xSoundFXWillSendDone(xSoundFX* pSoundFX)
{
    if (pSoundFX->linkCount > 0)
    {
        const xLinkAsset* idx = pSoundFX->link;

        for (S32 i = 0; i < pSoundFX->linkCount; i++, idx++)
        {
            if (idx->srcEvent == eEventDone)
            {
                return true;
            }
        }
    }

    return false;
}

void xSoundFXInit(void* t, void* asset)
{
    xSoundFXInit((xSoundFX*)t, (xSoundFXAsset*)asset);
}

void xSoundFXInit(xSoundFX* t, xSoundFXAsset* asset)
{
    xBaseInit(t, asset);

    t->eventFunc = xSoundFXEventCB;
    t->asset = asset;

    if (!(asset->uFlags & 0x8))
    {
        t->asset->soundAsset = xSndMgrGetSoundGroup(t->asset->soundAssetID);
        if (asset->soundAsset != (iSndGroupHandle)-1)
        {
            asset->uFlags |= 0x8;
        }
    }

    if (t->linkCount)
    {
        t->link = (xLinkAsset*)(t->asset + 1);
    }
    else
    {
        t->link = NULL;
    }

    if (xSoundFXWillSendDone(t))
    {
        asset->uFlags |= 0x1;
    }

    t->sndHandle = (iSndHandle)-1;
    t->asset->uFlags &= ~0x2;

    // Retail reads the group's uPriority here, not fOuterRadius.
    t->cachedOuterDistSquared = (t->asset->soundAsset == (iSndGroupHandle)-1) ?
                                    0.0f :
                                    ((xSndGroup*)(UPtr)t->asset->soundAsset)->header.uPriority;
    t->cachedOuterDistSquared *= t->cachedOuterDistSquared;
}

static void xSoundFXPlay(xSoundFX* t);
static void xSoundFXStop(xSoundFX* t);

void xSoundFXEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32)
{
    xSoundFX* t = (xSoundFX*)to;

    switch (toEvent)
    {
    case eEventReset:
        xBaseReset(t, t->asset);
        break;
    case eEventPlay:
        t->asset->uFlags |= 0x2;
        if (!xSndMgrIsStream(t->asset->soundAsset))
        {
            if (toParam != NULL && toParam[0] == 1.0f)
            {
                zMenuSB_EnsureFreeAudioStream(true);
            }
            xSoundFXPlay(t);
        }
        break;
    case eEventPlayMaybe:
        if (xrand_RandomChoice(100) >= toParam[0])
        {
            t->asset->uFlags |= 0x2;
            if (!xSndMgrIsStream(t->asset->soundAsset))
            {
                if (toParam != NULL && toParam[0] == 1.0f)
                {
                    zMenuSB_EnsureFreeAudioStream(true);
                }
                xSoundFXPlay(t);
            }
        }
        break;
    case eEventStop:
        if (t->sndHandle != (iSndHandle)-1)
        {
            t->asset->uFlags &= ~0x2;
            if (!xSndMgrIsStream(t->asset->soundAsset))
            {
                xSoundFXStop(t);
            }
        }
        break;
    case eEventPause:
        // Pause is not implemented for SoundFX: retail hangs here (the failure
        // message in the loop is compiled out).
        while (true)
        {
        }
        break;
    }
}

static void xSoundFXPlay(xSoundFX* t)
{
    if (t->sndHandle != (iSndHandle)-1)
    {
        xSndMgrStop(t->sndHandle);
    }

    if (t->asset->uFlags & 0x4)
    {
        xEnt* ent = (xEnt*)zSceneFindObject(t->asset->attachID);
        if (ent != NULL)
        {
            t->sndHandle =
                xSndMgrPlay(t->asset->soundAsset, 0x2, xEntGetPos(ent), NULL, ent, NULL, NULL);
        }
    }
    // Retail special-cases this one SoundFX asset ID and plays it without a position.
    else if (t->asset->id == 0x07A0D579)
    {
        t->sndHandle = xSndMgrPlay(t->asset->soundAsset, 0, NULL, NULL, NULL, NULL, NULL);
    }
    else
    {
        t->sndHandle = xSndMgrPlay(t->asset->soundAsset, 0, &t->asset->pos, NULL, NULL, NULL, NULL);
    }
}

static void xSoundFXStop(xSoundFX* t)
{
    xSndMgrStop(t->sndHandle);
}

xSoundFX* xSoundFXGet(U32 soundfxID)
{
    zScene* s = xglobals->sceneCur;
    S32 count = s->baseCount[eBaseTypeSoundFX];
    xSoundFX* sfx = NULL;

    for (S32 i = 0; i < count; i++)
    {
        sfx = (xSoundFX*)s->baseList[eBaseTypeSoundFX] + i;
        if (sfx->id == soundfxID)
        {
            break;
        }
    }

    return sfx;
}
