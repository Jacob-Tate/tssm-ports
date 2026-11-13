#include "xSnd.h"
#include "xDebug.h"
#include "xstransvc.h"

#include <fmod/fmod.h>

const F32 xSndGlobals::speedOfSound = 200.0f;
xSndEffect m_eCurrentEffect;
U32 gCurrSoundMemoryUsed;
U32 gHighwaterSoundMemoryUsed;
static F32 s_fCategoryVolume[6];
static F32 s_fCategoryVolumeFade[6];
static xSoundFX* s_managedEnvSoundFX[1] = { NULL };

U8 xSndMgr_UpdateVoicePosition(xSndVoiceInfo* pVoice);
U8 xSndMgr_SetAudible(xSndVoiceInfo* pVoiceStorage);

static inline void xSndMgr_Stop(xSndVoiceInfo* pVoice)
{
    S32 voiceId = pVoice - gSnd.voice;

    if (voiceId < 48)
    {
        iSndStop(pVoice);
        pVoice->flags &= ~0x4000;
    }
    else if (pVoice->flags & 0x20000)
    {
        pVoice->flags = 0;
        pVoice->assetID = 0;
    }
    else
    {
        pVoice->flags &= ~0x4000;
    }
}

static inline void xSndMgr_Pause(xSndVoiceInfo* pVoice, bool bPaused)
{
    iSndPause(pVoice, bPaused);

    if (bPaused)
    {
        pVoice->flags |= 0x800000;
    }
    else
    {
        pVoice->flags &= ~0x800000;
    }
}

static inline void xSndMgr_SetPaused(S16 whichCat, bool bPause)
{
    switch (whichCat)
    {
    case -3:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x7c;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x7c;
        }
        break;
    case -2:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x5c;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x5c;
        }
        break;
    case 0:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x4;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x4;
        }
        break;
    case 1:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x8;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x8;
        }
        break;
    case 2:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x10;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x10;
        }
        break;
    case 3:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x20;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x20;
        }
        break;
    case 5:
        if (bPause)
        {
            gSnd.m_uSndMgrFlags |= 0x40;
        }
        else
        {
            gSnd.m_uSndMgrFlags &= ~0x40;
        }
        break;
    }
}

static inline void xSndMgr_UpdateListenerPosition()
{
    switch (gSnd.listenerMode)
    {
    case xSndListener_PLAYER:
    {
        xMat4x3* pMat = &gSnd.listenerMat[0];

        gSnd.right = pMat->right;
        gSnd.up = pMat->up;
        gSnd.at = pMat->at;
        gSnd.pos = gSnd.listenerMat[1].pos;
        gSnd.velocity = gSnd.listenerVel[1];
        break;
    }
    case xSndListener_CAMERA:
    {
        xMat4x3* pMat = &gSnd.listenerMat[0];

        gSnd.right = pMat->right;
        gSnd.up = pMat->up;
        gSnd.at = pMat->at;
        gSnd.pos = pMat->pos;
        gSnd.velocity = gSnd.listenerVel[1];
        break;
    }
    }
}

xSndGroup* xSndMgr_AllocGroup(U8 nSoundAssets)
{
    U32 datasize;
    void* pAllocated;
    xSndGroup* pRetGroup;
    U32 i;

    datasize = sizeof(xSndGroupHeader);
    datasize += nSoundAssets * sizeof(xSndGroupInfo);
    pAllocated = xMemAlloc(gActiveHeap, datasize, 0);
    pRetGroup = (xSndGroup*)pAllocated;

    pRetGroup->header.uPlayedMask = 0;
    pRetGroup->header.uInfoPad0 = 'B';
    pRetGroup->header.uSetBits = 0;
    pRetGroup->header.nMaxPlays = 10;
    pRetGroup->header.eSoundCategory = 0;
    pRetGroup->header.uPriority = 0x80;
    pRetGroup->header.uFlags = 0;
    pRetGroup->header.fInnerRadius = 25.0f;
    pRetGroup->header.fOuterRadius = 100.0f;
    pRetGroup->header.pszGroupName = "";
    pRetGroup->header.uInfoCount = nSoundAssets;

    for (i = 0; i < nSoundAssets; i++)
    {
        pRetGroup->aSndGroupInfo[i].fMaxPitchMult = 0.0f;
        pRetGroup->aSndGroupInfo[i].fMinPitchMult = 0.0f;
        pRetGroup->aSndGroupInfo[i].fVolume = 1.0f;
        pRetGroup->aSndGroupInfo[i].uSoundNameHash = 0;
    }

    return pRetGroup;
}

void xSndMgrInit()
{
    xSndVoiceInfo* voice;

    memset(&gSnd, 0, sizeof(gSnd));
    iSndInit();

    for (voice = gSnd.voice; voice < gSnd.voice + 96; voice++)
    {
        voice->flags = 0;
        voice->soundGroupHandle = (iSndGroupHandle)-1;
        voice->sndUniqueID = (iSndHandle)-1;
    }

    s_fCategoryVolume[3] = 0.8f;
    s_fCategoryVolume[0] = 1.0f;
    s_fCategoryVolume[1] = 1.0f;
    s_fCategoryVolume[2] = 1.0f;
    s_fCategoryVolume[4] = 1.0f;
    s_fCategoryVolume[5] = 1.0f;

    gSnd.listenerMode = xSndListener_PLAYER;
    m_eCurrentEffect = xSndEffect_NONE;
    iSndSetEnvironmentalEffect(xSndEffect_NONE);
    iSndSuspendCD(1);
}

void xSndMgrSelectListenerMode(xSndListener Mode)
{
    gSnd.listenerMode = Mode;
}

void xSndMgrSetEffect(xSndEffect Effect)
{
    if (m_eCurrentEffect != Effect)
    {
        m_eCurrentEffect = Effect;
        iSndSetEnvironmentalEffect(Effect);
    }
}

xSndEffect xSndMgrGetEffect()
{
    return m_eCurrentEffect;
}

// The deltas, distances, inwardShift and factor are declared together, as in Ratatouille.
// 'inwardShift *= 0.5f' keeps inwardShift a variable, which retail's register allocation needs.
void xSndMgrProcessSoundPosition(const xVec3* pActual, xVec3* pProcessed)
{
    switch (gSnd.listenerMode)
    {
    case xSndListener_PLAYER:
    {
        xVec3 playerDelta;
        xVec3 cameraDelta;
        F32 playerDist;
        F32 cameraDist;
        F32 inwardShift;
        F32 factor;

        playerDelta = *pActual - gSnd.listenerMat[1].pos;
        cameraDelta = *pActual - gSnd.listenerMat[0].pos;
        playerDist = xVec3Length(&playerDelta);
        cameraDist = xVec3Length(&cameraDelta);

        if (cameraDist < playerDist)
        {
            inwardShift = playerDist - cameraDist;
            inwardShift *= 0.5f;

            factor = (playerDist - inwardShift) / playerDist;
            playerDelta *= factor;
            *pProcessed = playerDelta + gSnd.listenerMat[1].pos;
        }
        else
        {
            *pProcessed = *pActual;
        }
        break;
    }
    case xSndListener_CAMERA:
        *pProcessed = *pActual;
        break;
    }
}

U8 xSndMgr_UpdateVoicePosition(xSndVoiceInfo* pVoice)
{
    if (!(pVoice->flags & 0x4000))
    {
        return 1;
    }

    if (pVoice->flags & 0x8000)
    {
        if (pVoice->flags & 0x2)
        {
            if (pVoice->pPosition != NULL)
            {
                pVoice->actualPos = *pVoice->pPosition;

                if (pVoice->pVelocity != NULL)
                {
                    pVoice->actualVelocity = *pVoice->pVelocity;
                }
            }
            else if (pVoice->pParent != NULL)
            {
                // not in the PS2 DWARF: retail keeps the parent in a register across the inlined
                // xVec3Copy.
                xEnt* ent = pVoice->pParent;
                if (xBaseIsEnabled(ent) && !xSndMgrIsPaused(pVoice->category))
                {
                    xVec3Copy(&pVoice->actualPos, xEntGetPos(ent));

                    if ((pVoice->flags & 0x1000) && !xfeq0(xglobals->update_dt))
                    {
                        xVec3 vel = (ent->frame->mat.pos - ent->frame->oldmat.pos) *
                                    (1.0f / xglobals->update_dt);
                        xVec3Copy(&pVoice->actualVelocity, &vel);
                    }
                }
                else if (!xBaseIsEnabled(ent))
                {
                    pVoice->flags &= ~0x2;
                }
            }
        }

        xSndMgrProcessSoundPosition(&pVoice->actualPos, &pVoice->playPos);

        if (pVoice->flags & 0x1000)
        {
            xVec3 soundDir = gSnd.pos - pVoice->actualPos;

            if (!xfeq0(soundDir.length2()))
            {
                soundDir.normalize();

                F32 velListener = gSnd.velocity.dot(soundDir);
                F32 velSource = pVoice->actualVelocity.dot(soundDir);
                F32 newDoppler = (xSndGlobals::speedOfSound - velListener) /
                                 (xSndGlobals::speedOfSound - velSource);

                if (newDoppler > 0.0f)
                {
                    pVoice->dopplerFreqCoef = newDoppler;
                }
                else
                {
                    pVoice->dopplerFreqCoef = 1.0f;
                }
            }
        }

        xVec3 soundDirectionXYZ;
        xVec3Sub(&soundDirectionXYZ, &pVoice->playPos, &gSnd.pos);
        pVoice->distToListener = soundDirectionXYZ.length();

        if (!(pVoice->flags & 0x20000))
        {
            if (pVoice->distToListener > pVoice->outerRadius)
            {
                if (!pVoice->pSndFileInfo->isLooping() &&
                    pVoice->distToListener > 1.2f * pVoice->outerRadius)
                {
                    return 0;
                }

                return xSndMgr_SetInaudible(pVoice);
            }
        }
        else if (pVoice->distToListener <= pVoice->outerRadius)
        {
            return xSndMgr_SetAudible(pVoice);
        }
    }

    return 1;
}

xSndVoiceInfo* xSndMgr_GetFreeVirtualVoiceInfo()
{
    S32 i;
    xSndVoiceInfo* pVoice = &gSnd.voice[48];

    for (i = 48; i < 96; i++)
    {
        if (pVoice->flags == 0)
        {
            return pVoice;
        }
        pVoice++;
    }

    return NULL;
}

U8 xSndMgr_SetInaudible(xSndVoiceInfo* pVoice)
{
    xSndVoiceInfo* pVoiceStorage;

    // Streamed sounds can't be virtualized (PS2: ID < 0x1000).
    if (pVoice->pSndFileInfo->uFlags & 0x2)
    {
        return 1;
    }

    // Only looped sounds are kept around (PS2: is_looped).
    if (!(pVoice->pSndFileInfo->uFlags & 0x1))
    {
        return 1;
    }

    pVoiceStorage = xSndMgr_GetFreeVirtualVoiceInfo();
    if (pVoiceStorage == NULL)
    {
        return 1;
    }

    *pVoiceStorage = *pVoice;
    xSndMgr_Stop(pVoice);

    pVoice->flags = 0;
    pVoice->ps.channel = -1;
    pVoiceStorage->flags |= 0x20000;

    return 0;
}

// Ratatouille's assert (xSnd_CALC3D's TSSM value is not established). Compiled out, it still
// counts towards GC/2.6's inline-size estimate and keeps this out of line, as retail calls it
// from xSndMgr_UpdateVoicePosition.
U8 xSndMgr_SetAudible(xSndVoiceInfo* pVoiceStorage)
{
    xASSERT(pVoiceStorage->flags & xSnd_CALC3D);

    xSndVoiceInfo* vp = iSndGetFreeVoiceInfo(pVoiceStorage->priority, pVoiceStorage->flags);

    if (vp == NULL)
    {
        return 0;
    }

    *vp = *pVoiceStorage;
    vp->flags &= ~0x20000;

    U8 bOk = iSndPlay(vp);
    if (bOk)
    {
        pVoiceStorage->flags = 0;

        if (xSndMgrIsPaused(vp->category))
        {
            xSndMgrPause(vp->sndUniqueID, true);
        }
    }

    return 0;
}

void xSndMgrSceneInit()
{
    F32 musicFader;
    S32 i;

    gSnd.m_uSndMgrFlags |= 0x1;
    xSndMgrSetEffect(xSndEffect_NONE);

    if (globals.sceneCur->sceneID == 'B101' || globals.sceneCur->sceneID == 'B201' ||
        globals.sceneCur->sceneID == 'B401' || globals.sceneCur->sceneID == 'DE02' ||
        globals.sceneCur->sceneID == 'TR01' || globals.sceneCur->sceneID == 'SC02')
    {
        musicFader = 0.0f;
    }
    else
    {
        musicFader = 0.23f;
    }

    s_fCategoryVolumeFade[0] = 0.23f;
    s_fCategoryVolumeFade[1] = 0.23f;
    s_fCategoryVolumeFade[2] = 0.23f;
    s_fCategoryVolumeFade[3] = musicFader;
    s_fCategoryVolumeFade[4] = 0.0f;
    s_fCategoryVolumeFade[5] = 0.435f;

    gSnd.listenerMode = xSndListener_PLAYER;

    for (i = 0; i < 2; i++)
    {
        gSnd.listenerMat[i].at.assign(1.0f, 0.0f, 0.0f);
        gSnd.listenerMat[i].right.assign(0.0f, 1.0f, 0.0f);
        gSnd.listenerMat[i].up.assign(0.0f, 0.0f, 1.0f);
        gSnd.listenerMat[i].pos.assign(999999.0f, 999999.0f, 999999.0f);
    }

    gSnd.at.assign(1.0f, 0.0f, 0.0f);
    gSnd.right.assign(0.0f, 1.0f, 0.0f);
    gSnd.up.assign(0.0f, 0.0f, 1.0f);
    gSnd.pos.assign(999999.0f, 999999.0f, 999999.0f);

    iSndUpdate();
}

void xSndMgrUpdate()
{
    U32 i;

    xSndMgr_UpdateListenerPosition();

    for (i = 0; i < 96; i++)
    {
        xSndMgr_UpdateVoicePosition(&gSnd.voice[i]);
    }

    iSndUpdate();
}

void xSndMgrSceneExit()
{
    xSndMgrStopSounds(-3, false);
    xSndMgrSetEffect(xSndEffect_NONE);
    xSndMgrUpdate();
    iSndSceneExit();
    memset(s_managedEnvSoundFX, 0, sizeof(s_managedEnvSoundFX));
    gSnd.m_uSndMgrFlags &= ~0x7c;
    gSnd.m_uSndMgrFlags &= ~0x1;
}

void xSndMgrExit()
{
    iSndExit();
}

void xSndMgrStopSounds(S16 eSoundCategory, bool bSkipPaused)
{
    if (eSoundCategory == -3)
    {
        for (S32 i = 0; i < 96; i++)
        {
            if ((gSnd.voice[i].flags & 0x4000) &&
                (!bSkipPaused || !(gSnd.voice[i].flags & 0x800000)))
            {
                xSndMgr_Stop(&gSnd.voice[i]);
            }
        }

        // GC only: also shut the FMOD reverb down.
        FSOUND_AUXFX_ShutdownEffect(0);
    }
    else if (eSoundCategory == -2)
    {
        for (S32 i = 0; i < 96; i++)
        {
            if ((gSnd.voice[i].flags & 0x4000) &&
                (!bSkipPaused || !(gSnd.voice[i].flags & 0x800000)) && gSnd.voice[i].category != 3)
            {
                xSndMgr_Stop(&gSnd.voice[i]);
            }
        }
    }
    else
    {
        for (S32 i = 0; i < 96; i++)
        {
            if ((gSnd.voice[i].flags & 0x4000) &&
                (!bSkipPaused || !(gSnd.voice[i].flags & 0x800000)) &&
                gSnd.voice[i].category == eSoundCategory)
            {
                xSndMgr_Stop(&gSnd.voice[i]);
            }
        }
    }
}

void xSndMgrPauseSounds(S16 eSoundCategory, bool bPaused, bool bPauseFutureSoundsOfThisType)
{
    if (eSoundCategory == -3)
    {
        for (S32 i = 0; i < 48; i++)
        {
            if (gSnd.voice[i].flags & 0x4000)
            {
                xSndMgr_Pause(&gSnd.voice[i], bPaused);
            }
        }
    }
    else if (eSoundCategory == -2)
    {
        for (S32 i = 0; i < 48; i++)
        {
            if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].category != 3)
            {
                xSndMgr_Pause(&gSnd.voice[i], bPaused);
            }
        }
    }
    else
    {
        for (S32 i = 0; i < 48; i++)
        {
            if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].category == eSoundCategory)
            {
                xSndMgr_Pause(&gSnd.voice[i], bPaused);
            }
        }
    }

    xSndMgr_SetPaused(eSoundCategory, bPaused && bPauseFutureSoundsOfThisType);
}

void xSndMgrSetUserVolume(S16 eSoundCategory, F32 fVolume)
{
    switch (eSoundCategory)
    {
    case -3:
        s_fCategoryVolume[3] = fVolume;
    case -2:
        s_fCategoryVolume[0] = fVolume;
        s_fCategoryVolume[1] = fVolume;
        s_fCategoryVolume[2] = fVolume;
        s_fCategoryVolume[5] = fVolume;
        s_fCategoryVolume[4] = fVolume;
        break;
    default:
        s_fCategoryVolume[eSoundCategory] = fVolume;
        break;
    }
}

F32 xSndMgrGetUserVolume(S16 eSoundCategory)
{
    return MAX(s_fCategoryVolume[eSoundCategory], 0.0f);
}

F32 xSndMgrGetDampedVolume(S16 eSoundCategory)
{
    F32 fRealVolume = s_fCategoryVolume[eSoundCategory] - s_fCategoryVolumeFade[eSoundCategory];
    return MAX(0.0f, MIN(fRealVolume, 1.0f));
}

U8 xSndMgrGetSoundCount(iSndGroupHandle hSound)
{
    if (hSound == -1)
    {
        return 0;
    }

    return ((xSndGroup*)(UPtr)hSound)->header.uInfoCount;
}

U32 xSndMgrGetSoundId(iSndGroupHandle hSound, U32 uIndexOfSound)
{
    xSndGroup* pSndGroup;

    if (hSound == -1)
    {
        return 0;
    }

    pSndGroup = (xSndGroup*)(UPtr)hSound;
    if (uIndexOfSound < pSndGroup->header.uInfoCount)
    {
        return pSndGroup->aSndGroupInfo[uIndexOfSound].uSoundNameHash;
    }

    return 0;
}

iSndGroupHandle xSndMgrGetSoundGroup(U32 uSoundGroupNameHash)
{
    iSndGroupHandle retHandle;
    xSndGroup* pSndGroup;

    if (uSoundGroupNameHash == 0)
    {
        return (iSndGroupHandle)-1;
    }

    retHandle = (iSndGroupHandle)(UPtr)xSTFindAsset(uSoundGroupNameHash, NULL);
    if (retHandle == 0)
    {
        return (iSndGroupHandle)-1;
    }

    pSndGroup = (xSndGroup*)(UPtr)retHandle;
    pSndGroup->header.pszGroupName = xSTAssetName(uSoundGroupNameHash);

    if (pSndGroup->header.uInfoPad0 != 'B')
    {
        return (iSndGroupHandle)-1;
    }

    return retHandle;
}

bool xSndMgrIsPlaying(iSndGroupHandle hSoundGroup)
{
    S32 i;

    if (hSoundGroup == -1)
    {
        return false;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].soundGroupHandle == hSoundGroup)
        {
            return true;
        }
    }

    return false;
}

bool xSndMgrVoiceAvailable(exVoiceType voiceType, U32 priority)
{
    return iSndGetFreeVoiceInfo(priority, voiceType == exVoiceType_Streamed ? 0x100000 : 0) != NULL;
}

// Always inlined (no symbol); Ratatouille has a function with this signature. Sounds of these
// categories get the environmental effect (voice flag 0x80000).
static inline bool xSndMgrDoesEffects(S16 eSoundCategory)
{
    return eSoundCategory == 0 || eSoundCategory == 1 || eSoundCategory == 2;
}

// Always inlined (no symbol); the name is a guess. True when a voice of this group is already
// playing for the given parent entity.
static inline bool xSndMgrIsPlaying(iSndGroupHandle hSoundGroup, xEnt* pParentEnt)
{
    S32 i;

    if (hSoundGroup == -1)
    {
        return false;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].soundGroupHandle == hSoundGroup &&
            gSnd.voice[i].pParent == pParentEnt)
        {
            return true;
        }
    }

    return false;
}

// Always inlined (no symbol); the name is a guess. True when nMaxPlays voices of the group with
// this asset ID are already playing.
static inline bool xSndMgrIsMaxPlaying(U32 uGroupID, U8 nMaxPlays)
{
    U8 uPlayCount = 0;
    S32 i;

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].soundGroupHandle != 0 &&
            uGroupID == gSnd.voice[i].soundGroup->header.id)
        {
            uPlayCount++;
            if (uPlayCount == nMaxPlays)
            {
                return true;
            }
        }
    }

    return false;
}

// bStreaming/bLooping are not in the DWARF: the PS2 demo computes the two xSndMgrGetPriority
// arguments on lines of their own before the call, and retail's registers need them as
// separate statements.
iSndHandle xSndMgrPlay(iSndGroupHandle hSoundToPlay, U32 uFlags, const xVec3* pPos_WS,
                       const xVec3* pVel, xEnt* pParentEnt, F32* pfOverrideVolume,
                       F32* pfOverridePitch)
{
    U32 uInfoIndex;
    xSndGroupInfo* pSndInfo;
    U32 iFlags;
    iSndFileInfo* ip;
    xSndVoiceInfo* vp;
    U8 bOKtoPlay;
    bool bPlaying;

    if (hSoundToPlay == -1)
    {
        return (iSndHandle)-1;
    }

    if (uFlags & 0x2000)
    {
        if (xSndMgrIsPlaying(hSoundToPlay, pParentEnt))
        {
            return (iSndHandle)-1;
        }
    }

    uInfoIndex =
        xrand_RandomChoice(xSndMgr_GetGroup(hSoundToPlay)->header.uPlayedMask,
                           xSndMgr_GetGroup(hSoundToPlay)->header.uSetBits,
                           xSndMgr_GetGroup(hSoundToPlay)->header.uInfoCount,
                           (xRandomChoiceRule)xSndMgr_GetGroup(hSoundToPlay)->header.ePlayRule);
    pSndInfo = &xSndMgr_GetGroup(hSoundToPlay)->aSndGroupInfo[uInfoIndex];
    iFlags = uFlags & ~0x7FC000;
    ip = NULL;

    if (uFlags & 0x1)
    {
        iFlags |= 0x100000;
    }
    else
    {
        ip = iSndLookup(pSndInfo->uSoundNameHash);
        if (ip == NULL)
        {
            return (iSndHandle)-1;
        }

        if (ip->uFlags & 0x2)
        {
            iFlags |= 0x100000;
        }
        else
        {
            iFlags |= 0x40000;
        }

        if (xSndMgrDoesEffects(xSndMgr_GetGroup(hSoundToPlay)->header.eSoundCategory) &&
            !(uFlags & 0x8))
        {
            iFlags |= 0x80000;
        }
    }

    if (iFlags & 0x100000)
    {
        if ((xSndMgr_GetGroup(hSoundToPlay)->header.uFlags & 0x1) || (uFlags & 0x800))
        {
            iFlags |= 0x800;
        }

        if (globals.cmgr != NULL && xSndMgr_GetGroup(hSoundToPlay)->header.eSoundCategory != 4)
        {
            return (iSndHandle)-1;
        }
    }

    if (xSndMgr_GetGroup(hSoundToPlay)->header.nMaxPlays > 0 &&
        xSndMgr_GetGroup(hSoundToPlay)->header.nMaxPlays < 96)
    {
        if (xSndMgrIsMaxPlaying(xSndMgr_GetGroup(hSoundToPlay)->header.id,
                                xSndMgr_GetGroup(hSoundToPlay)->header.nMaxPlays))
        {
            return (iSndHandle)-1;
        }
    }

    if (xSndMgr_GetGroup(hSoundToPlay)->header.eSoundCategory == 4)
    {
        xSndMgrStopSounds(2, false);
    }
    else if (xSndMgr_GetGroup(hSoundToPlay)->header.eSoundCategory == 2 &&
             (gSnd.m_uSndMgrFlags & 0x2))
    {
        return (iSndHandle)-1;
    }

    vp = iSndGetFreeVoiceInfo(xSndMgr_GetGroup(hSoundToPlay)->header.uPriority, iFlags);
    if (vp == NULL)
    {
        return (iSndHandle)-1;
    }

    if (pSndInfo->fMinPitchMult == 100.0f || pSndInfo->fMaxPitchMult == 100.0f)
    {
        pSndInfo->fMinPitchMult = 0.0f;
        pSndInfo->fMaxPitchMult = 0.0f;
    }

    bool bStreaming = ip ? ip->isStreamed() : false;
    bool bLooping = ip ? ip->isLooping() : false;
    vp->priority = xSndMgrGetPriority(hSoundToPlay, bStreaming, bLooping);
    vp->xpitch = pfOverridePitch ?
                     *pfOverridePitch :
                     xrand_RandomFloatRange(pSndInfo->fMinPitchMult, pSndInfo->fMaxPitchMult);
    vp->vol = pfOverrideVolume ? *pfOverrideVolume : pSndInfo->fVolume;
    vp->assetID = pSndInfo->uSoundNameHash;
    vp->pSndFileInfo = ip;
    vp->dopplerFreqCoef = 1.0f;
    vp->flags = iFlags | 0x4000;
    vp->category = xSndMgr_GetGroup(hSoundToPlay)->header.eSoundCategory;
    vp->sndUniqueID = (iSndHandle)++gSnd.SndCount;
    vp->soundGroupHandle = hSoundToPlay;

    if (vp->category == 0)
    {
        if (pParentEnt != NULL)
        {
            vp->flags |= 0x8000;
            vp->pParent = pParentEnt;
            vp->pPosition = NULL;
            vp->actualPos = *xEntGetPos(pParentEnt);
            vp->pVelocity = NULL;

            if (vp->flags & 0x1000)
            {
                vp->actualVelocity =
                    xglobals->update_dt > 0.01f ?
                        (pParentEnt->frame->mat.pos - pParentEnt->frame->oldmat.pos) *
                            (1.0f / xglobals->update_dt) :
                        xVec3::m_Null;
            }
            else
            {
                vp->actualVelocity.assign(0.0f, 0.0f, 0.0f);
            }

            vp->innerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fInnerRadius;
            vp->outerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fOuterRadius <= 0.0f ?
                                  100000.0f :
                                  xSndMgr_GetGroup(hSoundToPlay)->header.fOuterRadius;
        }
        else if (pPos_WS != NULL)
        {
            vp->flags |= 0x8000;
            vp->pParent = NULL;
            vp->pPosition = (xVec3*)pPos_WS;
            vp->actualPos = *pPos_WS;
            vp->innerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fInnerRadius;
            vp->outerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fOuterRadius <= 0.0f ?
                                  100000.0f :
                                  xSndMgr_GetGroup(hSoundToPlay)->header.fOuterRadius;

            if (pVel != NULL)
            {
                vp->pVelocity = (xVec3*)pVel;
                vp->actualVelocity = *pVel;
            }
            else
            {
                vp->pVelocity = NULL;
                vp->actualVelocity.assign(0.0f, 0.0f, 0.0f);
            }
        }
        else
        {
            vp->pParent = NULL;
            vp->pPosition = NULL;
            vp->actualPos = gSnd.pos;
            vp->actualVelocity.assign(0.0f, 0.0f, 0.0f);
            vp->pVelocity = NULL;
            vp->innerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fInnerRadius;
            vp->outerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fOuterRadius;
        }
    }
    else
    {
        vp->pParent = NULL;
        vp->pPosition = NULL;
        vp->actualPos = gSnd.pos;
        vp->actualVelocity.assign(0.0f, 0.0f, 0.0f);
        vp->pVelocity = NULL;
        vp->innerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fInnerRadius;
        vp->outerRadius = xSndMgr_GetGroup(hSoundToPlay)->header.fOuterRadius;
    }

    bOKtoPlay = TRUE;
    if (vp->flags & 0x8000)
    {
        bOKtoPlay = xSndMgr_UpdateVoicePosition(vp);
    }

    bPlaying = vp->pSndFileInfo ? vp->pSndFileInfo->isLooping() : false;

    if (bOKtoPlay)
    {
        bPlaying = iSndPlay(vp);

        if (xSndMgrIsPaused(vp->category))
        {
            xSndMgrPause(vp->sndUniqueID, true);
        }
    }

    if (!bPlaying)
    {
        vp->flags = 0;
        return (iSndHandle)-1;
    }

    return vp->sndUniqueID;
}

U32 xSndMgrGetSoundId(iSndHandle hSound)
{
    S32 i;

    if (hSound == -1)
    {
        return 0;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].sndUniqueID == hSound)
        {
            return gSnd.voice[i].assetID;
        }
    }

    return 0;
}

bool xSndMgrIsPlaying(iSndHandle hSound)
{
    S32 i;

    if (hSound == -1)
    {
        return false;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].sndUniqueID == hSound)
        {
            return true;
        }
    }

    return false;
}

U32 xSndMgrIsReady(iSndHandle hSound)
{
    if (hSound == -1)
    {
        return 0;
    }

    return iSndIsReady(hSound);
}

void xSndMgrPause(iSndHandle hSound, bool bPause)
{
    S32 i;

    if (hSound == -1)
    {
        return;
    }

    for (i = 0; i < 48; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].sndUniqueID == hSound)
        {
            iSndPause(&gSnd.voice[i], bPause);
            if (bPause)
            {
                gSnd.voice[i].flags |= 0x800000;
            }
            else
            {
                gSnd.voice[i].flags &= ~0x800000;
            }
        }
    }

    // Retail keeps an empty loop over the virtual voices here (its body was compiled out).
    if (i == 48)
    {
        for (; i < 96; i++)
        {
        }
    }
}

void xSndMgrStop(iSndHandle& hInHandle)
{
    S32 i;

    if (hInHandle == -1)
    {
        return;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].sndUniqueID == hInHandle)
        {
            xSndMgr_Stop(&gSnd.voice[i]);
            break;
        }
    }

    hInHandle = (iSndHandle)-1;
}

void xSndMgrSetVolume(iSndHandle hSound, F32 fVolume)
{
    S32 i;

    if (hSound == -1)
    {
        return;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].sndUniqueID == hSound)
        {
            if (i < 48)
            {
                iSndSetVol(&gSnd.voice[i], fVolume);
            }
            gSnd.voice[i].vol = fVolume;
        }
    }
}

void xSndMgrSetPitch(iSndHandle hSound, F32 fPitch)
{
    S32 i;

    if (hSound == -1)
    {
        return;
    }

    for (i = 0; i < 96; i++)
    {
        if ((gSnd.voice[i].flags & 0x4000) && gSnd.voice[i].sndUniqueID == hSound)
        {
            if (i < 48)
            {
                iSndSetPitch(&gSnd.voice[i], fPitch);
            }
            gSnd.voice[i].xpitch = fPitch;
        }
    }
}

void xSndMgrStartStereo(iSndHandle hSnd00, iSndHandle hSnd01)
{
    iSndStartStereo(hSnd00, hSnd01);
}

void xSndMgrStopChildren(xEnt* pParentEnt)
{
    U32 i;
    xSndVoiceInfo* vp;

    vp = gSnd.voice;

    for (i = 0; i < 96; i++)
    {
        if ((vp->flags & 0x4000) && vp->pParent == pParentEnt)
        {
            xSndMgr_Stop(vp);
        }
        vp++;
    }
}

// The cases return the masked flags and let the return convert them to bool. Standalone that
// compiles the same as '!= 0', but inlined into xSndMgrPlay it gives retail's registers
// (Ratatouille's debug build has this function out of line in the same form).
bool xSndMgrIsPaused(S16 eSoundCategory)
{
    switch (eSoundCategory)
    {
    case -3:
        return gSnd.m_uSndMgrFlags & 0x7c;
    case -2:
        return gSnd.m_uSndMgrFlags & 0x5c;
    case 0:
        return gSnd.m_uSndMgrFlags & 0x4;
    case 1:
        return gSnd.m_uSndMgrFlags & 0x8;
    case 2:
        return gSnd.m_uSndMgrFlags & 0x10;
    case 3:
        return gSnd.m_uSndMgrFlags & 0x20;
    case 5:
        return gSnd.m_uSndMgrFlags & 0x40;
    }
    return false;
}
