#ifndef XSND_H
#define XSND_H

#include <types.h>
#include "xVec3.h"
#include "xMath3.h"
#include "iSnd.h"
#include "xEnt.h"
#include "xString.h"

enum sound_category
{
    SND_CAT_INVALID = 0xffffffff,
    SND_CAT_GAME = 0,
    SND_CAT_DIALOG,
    SND_CAT_MUSIC,
    SND_CAT_CUTSCENE,
    SND_CAT_UI,
    SND_CAT_NUM_CATEGORIES
};

struct xSndGroup;
struct iSndFileInfo;

// Size: 0x68 (GC). The PS2 DWARF layout is 0x6C; only iSndInfo differs (see iSnd.h).
struct xSndVoiceInfo
{
    union
    {
        xSndGroup* soundGroup;
        iSndGroupHandle soundGroupHandle;
    };
    U32 assetID;
    iSndHandle sndUniqueID;
    xEnt* pParent;
    xVec3* pPosition;
    U32 flags;
    F32 vol;
    F32 xpitch;
    F32 dopplerFreqCoef;
    S16 category;
    U8 bformerlyAPartOfPriority;
    U8 priority;
    xVec3 actualPos;
    xVec3 playPos;
    xVec3* pVelocity;
    xVec3 actualVelocity;
    F32 distToListener;
    F32 innerRadius;
    F32 outerRadius;
    iSndInfo ps;
    iSndFileInfo* pSndFileInfo;
};

enum xSndMode
{
    xSndMode_Mono,
    xSndMode_Stereo,
    xSndMode_Dolby,
    xSndMode_Count
};

enum exVoiceType
{
    exVoiceType_Streamed,
    exVoiceType_RAM
};

enum xSndListener
{
    xSndListener_CAMERA,
    xSndListener_PLAYER,
    xSndListener_MAX_TYPES
};

enum sound_listener_game_mode
{
    SND_LISTENER_MODE_PLAYER,
    SND_LISTENER_MODE_CAMERA
};

struct xSndGlobals
{
    static const F32 speedOfSound;

    xSndVoiceInfo voice[96];
    xMat4x3 listenerMat[2];
    xVec3 listenerVel[2];
    xVec3 right;
    xVec3 up;
    xVec3 at;
    xVec3 pos;
    xVec3 velocity;
    U32 m_uSndMgrFlags;
    xSndMode sndMode;
    U32 SndCount;
    xSndListener listenerMode;
};

struct _xSndDelayed
{
    U32 id;
    F32 vol;
    F32 pitch;
    U32 priority;
    U32 flags;
    U32 parentID;
    xEnt* parentEnt;
    xVec3* pos;
    F32 innerRadius;
    F32 outerRadius;
    sound_category category;
    F32 delay;
    U32 pad0;
};

struct xSoundFXAsset : xBaseAsset
{
    union
    {
        U32 soundAssetID;
        iSndGroupHandle soundAsset;
    };
    U32 attachID;
    xVec3 pos;
    U32 uFlags;
};

struct xSoundFX : xBase
{
    xSoundFXAsset* asset;
    iSndHandle sndHandle;
    F32 cachedOuterDistSquared;
};

void xSoundFXUpdateEnvironmentalStreamSounds(xSoundFX* pSFXList, U32 numSounds);
void xSoundFXInit(void* t, void* asset);
void xSoundFXInit(xSoundFX* t, xSoundFXAsset* asset);
void xSoundFXEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32);
xSoundFX* xSoundFXGet(U32 soundfxID);

struct xSndGroupHeader : xBaseAsset
{
    U32 uPlayedMask;
    U8 uInfoCount;
    U8 uSetBits;
    S8 nMaxPlays;
    U8 uPriority;
    U8 uFlags;
    U8 eSoundCategory;
    U8 ePlayRule;
    U8 uInfoPad0;
    F32 fInnerRadius;
    F32 fOuterRadius;
    char* pszGroupName;
};

struct xSndGroupInfo
{
    U32 uSoundNameHash;
    F32 fVolume;
    F32 fMinPitchMult;
    F32 fMaxPitchMult;
};

struct xSndGroup
{
    xSndGroupHeader header;
    xSndGroupInfo aSndGroupInfo[0];
};

// sound_queue calls these; declared ahead of the template.
void xSndMgrStop(iSndHandle& hInHandle);
bool xSndMgrIsPlaying(iSndHandle hSound);

template <S32 N> struct sound_queue
{
    iSndHandle _playing[N + 1];
    S32 head;
    S32 tail;

    sound_queue()
    {
        head = 0;
        tail = 0;
    }

    void push(iSndHandle id)
    {
        _playing[tail] = id;

        S32 h = head;
        S32 t = tail + 1;

        if (t <= h)
        {
            t += N + 1;
        }

        if (t - h > N)
        {
            xSndMgrStop(_playing[h]);
            head = (h + 1) % (N + 1);
        }

        tail = t % (N + 1);
    }

    void pop()
    {
        xSndMgrStop(_playing[head]);
        head = (head + 1) % (N + 1);
    }

    S32 size() const
    {
        if (tail < head)
        {
            return tail + (N + 1) - head;
        }
        return tail - head;
    }

    void clear()
    {
        while (size() > 0)
        {
            pop();
        }
    }

    iSndHandle recent(S32 index) const
    {
        S32 i = tail - index - 1;
        if (i < 0)
        {
            i += N + 1;
        }
        return _playing[i];
    }

    bool playing() const
    {
        S32 count = size();

        for (S32 i = 0; i < count; i++)
        {
            if (!xSndMgrIsPlaying(recent(i)))
            {
                return false;
            }
        }

        return true;
    }
};

enum sound_effect
{
    SND_EFFECT_NONE,
    SND_EFFECT_CAVE
};

enum xSndEffect
{
    xSndEffect_NONE,
    xSndEffect_CAVE,
    xSndEffect_MAX_TYPES
};

enum sound_listener_type
{
    SND_LISTENER_CAMERA,
    SND_LISTENER_PLAYER,
    SND_MAX_LISTENER_TYPES
};

extern xSndGlobals gSnd;
extern xSndEffect m_eCurrentEffect;
extern U32 gCurrSoundMemoryUsed;
extern U32 gHighwaterSoundMemoryUsed;

void xSndSceneInit();
void xSndResume();
void xSndUpdate();
void xSndMgrUpdate();
xSndEffect xSndMgrGetEffect();
U8 xSndMgr_SetInaudible(xSndVoiceInfo* pVoice);
void xSndSetEnvironmentalEffect(sound_effect effectType);
void xSndSuspend();
void xSndSetVol(U32 snd, F32 vol);
void xSndSetPitch(U32 snd, F32 pitch);
void xSndStop(U32 snd);
void xSndStopAll(U32 mask);
void xSndPauseAll(U32 pause_effects, U32 pause_streams);
void xSndMgrPauseSounds(S16 eSoundCategory, bool bPaused, bool bPauseFutureSoundsOfThisType);
bool xSndMgrIsPaused(S16 eSoundCategory);
void xSndMgrStopSounds(S16 eSoundCategory, bool bSkipPaused);
void xSndPauseCategory(U32 mask, U32 pause);
void xSndDelayedInit();
void reset_faders();
void xSndParentDied(U32 pid);
void xSndCalculateListenerPosition();
void xSndDelayedUpdate();
void update_faders(F32 timeElapsed);
void xSndProcessSoundPos(const xVec3* pActual, xVec3* pProcessed);
void xSndInternalUpdateVoicePos(xSndVoiceInfo* voiceInfo);

inline void xSndSetListenerData(xSndListener listenerType, const xMat4x3* pMat,
                                const xVec3& velocity)
{
    gSnd.listenerMat[listenerType] = *pMat;

    if (velocity.length2() >= xSndGlobals::speedOfSound * xSndGlobals::speedOfSound)
    {
        gSnd.listenerVel[listenerType] = xVec3::m_Null;
    }
    else
    {
        gSnd.listenerVel[listenerType] = velocity;
    }
}

void xSndSelectListenerMode(sound_listener_game_mode listenerGameMode);
void xSndExit();
void xSndMgrInit();
void xSndMgrSceneInit();
void xSndMgrExit();
void xSndMgrSceneExit();
void xSndMgrStopChildren(xEnt* pParentEnt);
xSndGroup* xSndMgr_AllocGroup(U8 nSoundAssets);
iSndHandle xSndMgrPlay(iSndGroupHandle hSoundToPlay, U32 uFlags, const xVec3* pPos_WS,
                       const xVec3* pVel, xEnt* pParentEnt, F32* pfOverrideVolume,
                       F32* pfOverridePitch);
U32 xSndMgrIsReady(iSndHandle hSound);
bool xSndMgrIsPlaying(iSndGroupHandle hSoundGroup);
void xSndMgrSetPitch(iSndHandle hSound, F32 fPitch);
void xSndMgrSetVolume(iSndHandle hSound, F32 fVolume);
void xSndMgrPause(iSndHandle hSound, bool bPause);
U32 xSndMgrGetSoundId(iSndHandle hSound);
bool xSndMgrVoiceAvailable(exVoiceType voiceType, U32 priority);
U32 xSndMgrGetSoundId(iSndGroupHandle hSound, U32 uIndexOfSound);
U8 xSndMgrGetSoundCount(iSndGroupHandle hSound);
xSndVoiceInfo* xSndMgr_GetFreeVirtualVoiceInfo();
void xSndMgrSetEffect(xSndEffect Effect);
void xSndMgrSelectListenerMode(xSndListener Mode);
void xSndMgrStop(iSndHandle& hInHandle);
void xSndMgrStartStereo(iSndHandle hSnd00, iSndHandle hSnd01);
void xSndMgrSetPitch(iSndHandle hSound, F32 fPitch);
iSndGroupHandle xSndMgrGetSoundGroup(U32 uSoundGroupNameHash);

// Always inlined (no symbol); Ratatouille's GC xSndMgr.h has this overload.
inline iSndGroupHandle xSndMgrGetSoundGroup(char* pszSoundGroupName)
{
    return xSndMgrGetSoundGroup(xStrHash(pszSoundGroupName));
}

// An iSndGroupHandle holds an xSndGroup address in 32 bits (asset fields store them), so a host
// keeps its sound groups below 4 GB. A plain cast here: through UPtr, GC/2.6 numbers xWad5's
// labels differently. Always inlined (no symbol); Ratatouille's xSndMgr.h has this inline.
inline xSndGroup* xSndMgr_GetGroup(iSndGroupHandle hHandle)
{
    return (xSndGroup*)hHandle;
}

// Always inlined (no symbol); the name is a guess (Ratatouille's xSndMgr.h has a similar
// xSndMgrIsStream). Header uFlags bit 1 marks a streamed group.
inline bool xSndMgrIsStream(iSndGroupHandle hSound)
{
    if (hSound == (iSndGroupHandle)-1)
    {
        return false;
    }

    return (((xSndGroup*)(UPtr)hSound)->header.uFlags >> 1) & 1;
}

// Always inlined (no symbol); Ratatouille has a function with this signature. Retail inlines
// this shape in xSndMgrPlay and xSoundFXUpdateEnvironmentalStreamSounds.
inline U8 xSndMgrGetPriority(iSndGroupHandle hSound, bool bStreaming, bool bLooping)
{
    if (hSound == (iSndGroupHandle)-1)
    {
        return 0;
    }

    xSndGroupHeader* pSnd = &((xSndGroup*)(UPtr)hSound)->header;
    U32 uPriority = pSnd->uPriority;

    if (bStreaming)
    {
        uPriority = 0xE0;
    }

    uPriority += pSnd->eSoundCategory;

    if (bLooping)
    {
        uPriority += 10;
    }

    if (uPriority > 0xFF)
    {
        uPriority = 0xFF;
    }

    return uPriority;
}

F32 xSndMgrGetDampedVolume(S16 eSoundCategory);
F32 xSndMgrGetUserVolume(S16 eSoundCategory);
void xSndMgrSetUserVolume(S16 eSoundCategory, F32 fVolume);
U32 xSndPlay(U32 id, F32 vol, F32 pitch, U32 priority, U32 flags, U32 parentID,
             sound_category category, F32 delay);
U32 xSndPlay3D(U32 id, F32 vol, F32 pitch, U32 priority, U32 flags, xEnt* parent, F32 innerRadius,
               F32 outerRadius, sound_category category, F32 delay);
U32 xSndPlay3D(U32 id, F32 vol, F32 pitch, U32 priority, U32 flags, const xVec3* pos,
               F32 innerRadius, F32 outerRadius, sound_category category, F32 delay);
U32 xSndPlayInternal(U32 id, F32 vol, F32 pitch, U32 priority, U32 flags, U32 parentID,
                     xEnt* parentEnt, const xVec3* pos, F32 innerRadius, F32 outerRadius,
                     sound_category category, F32 delay);
void xSndStartStereo(U32 id1, U32 id2, F32 pitch);
U8 xSndIsPlayingByHandle(U32 sndID);
U32 xSndIsPlaying(U32 assetID);
U32 xSndIDIsPlaying(U32 sndID);
void xSndStop(U32 snd);
void xSndParentDied(U32 pid);
void xSndStopChildren(U32 pid);
void xSndSetVol(U32 snd, F32 vol);
void xSndSetPitch(U32 snd, F32 pitch);
void xSndSetCategoryVol(sound_category category, F32 vol);
void xSndSetExternalCallback(void (*callback)(U32));

#endif
