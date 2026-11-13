#include "zStreamedSound.h"

#include "iTime.h"
#include "xRand.h"
#include "xSnd.h"
#include "xString.h"
#include "zEntPlayer.h"
#include "zTalkBox.h"

zStreamedSound* zStreamedSound::sDelayedSound;
iSndHandle zStreamedSound::sLastPlayedSoundHdl = (iSndHandle)-1;

inline bool zStreamedSound::LastPlayedSoundIsPlaying()
{
    if (sLastPlayedSoundHdl == (iSndHandle)-1)
    {
        return false;
    }

    return xSndMgrIsPlaying(sLastPlayedSoundHdl);
}

inline void zStreamedSound::UpdateSounds()
{
    if (sLastPlayedSoundHdl != (iSndHandle)-1 && !xSndMgrIsPlaying(sLastPlayedSoundHdl))
    {
        sLastPlayedSoundHdl = (iSndHandle)-1;
    }

    if (sDelayedSound && iTimeGetGame() >= sDelayedSound->mSoonestTimeToPlay)
    {
        zStreamedSound* sound = sDelayedSound;
        sDelayedSound = NULL;

        sound->PlaySoundCore();
    }
}

void zStreamedSound::StopLastPlayedSound()
{
    if (LastPlayedSoundIsPlaying())
    {
        xSndMgrStop(sLastPlayedSoundHdl);
    }

    sLastPlayedSoundHdl = (iSndHandle)-1;
}

inline bool zStreamedSound::IsReadyToPlay()
{
    bool ready = false;
    bool allowed = false;
    if (mSoundGroupHdl != (iSndGroupHandle)-1 && !(mFlags & 0x4000) &&
        zEntPlayer_SoundsCurrentlyAllowed() && !(gSnd.m_uSndMgrFlags & 0x2) &&
        !ztalkbox::get_active())
    {
        allowed = true;
    }
    if (allowed && !LastPlayedSoundIsPlaying())
    {
        ready = true;
    }
    return ready;
}

// No GC or PS2 symbol (inlined into PlaySound and UpdateSounds); name from Ratatouille. The
// probability check is written out: as a separate inline it gets a different register.
inline void zStreamedSound::PlaySoundCore()
{
    if (IsReadyToPlay())
    {
        F32 timeNow = iTimeGetGame();
        if (timeNow >= mSoonestTimeToPlay)
        {
            if (!sDelayedSound)
            {
                U32 chance = mFlags & 0x3F80;
                if (!chance ? true : 0.01f * (chance >> 7) > xrand_GenRandFloat())
                {
                    sLastPlayedSoundHdl =
                        xSndMgrPlay(mSoundGroupHdl, 0, NULL, NULL, NULL, NULL, NULL);
                    mSoonestTimeToPlay = timeNow + mCycleTime;
                    if (mFlags & 0x10)
                    {
                        mFlags |= 0x4000;
                    }
                }
            }
        }
        else if (mFlags & 0x20)
        {
            mSoonestTimeToPlay = timeNow + mCycleTime;
        }
    }
}

void zStreamedSound::PlaySound()
{
    S32 delayBits = mFlags & 0xF;
    if (delayBits)
    {
        PlaySoundDelayed(delayBits * 0.1f);
    }
    else
    {
        PlaySoundCore();
    }
}

void zStreamedSound::PlaySoundDelayed(F32 delay)
{
    if (!sDelayedSound)
    {
        F32 timeNow = iTimeGetGame();
        F32 futureTime = timeNow + delay;
        if (futureTime >= mSoonestTimeToPlay)
        {
            mSoonestTimeToPlay = futureTime;
            sDelayedSound = this;
        }
    }
}

void zStreamedSoundList::InitWithSounds(zStreamedSound* soundList, U32 numSounds)
{
    iTimeGetGame();
    mNumSounds = 0;
    mSounds = soundList;
    while (soundList->mAssetName)
    {
        soundList->mSoonestTimeToPlay = 0.3f * soundList->mCycleTime + iTimeGetGame();
        soundList->mSoundGroupHdl = soundList->mAssetName[0] ?
                                        xSndMgrGetSoundGroup(xStrHash(soundList->mAssetName)) :
                                        (iSndGroupHandle)-1;
        soundList++;
        mNumSounds++;
    }
}

void zStreamedSoundList::Reset()
{
    zStreamedSound* sp = mSounds;
    for (U32 i = 0; i < mNumSounds; i++)
    {
        sp->mFlags &= ~0x4000;
        sp++;
    }
}
