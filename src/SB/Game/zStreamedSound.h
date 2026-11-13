#ifndef ZSTREAMEDSOUND_H
#define ZSTREAMEDSOUND_H

#include "iSnd.h"

struct zStreamedSound
{
    char* mAssetName;
    F32 mCycleTime;
    U32 mFlags;
    F32 mSoonestTimeToPlay;
    iSndGroupHandle mSoundGroupHdl;

    static zStreamedSound* sDelayedSound;
    static iSndHandle sLastPlayedSoundHdl;

    void PlaySoundDelayed(F32 delay);
    void PlaySound();
    static void StopLastPlayedSound();
    // No GC symbol: inlined into zSceneUpdate.
    static void UpdateSounds();

    // Always inlined (no symbols); defined in zStreamedSound.cpp.
    static bool LastPlayedSoundIsPlaying();
    bool IsReadyToPlay();
    void PlaySoundCore();
};

struct zStreamedSoundList
{
    U32 mNumSounds;
    zStreamedSound* mSounds;

    void Reset();
    void InitWithSounds(zStreamedSound* soundList, U32 numSounds);

    void PlaySound(U32 index)
    {
        if (index < mNumSounds)
        {
            mSounds[index].PlaySound();
        }
    }

    void PlaySoundDelayed(U32 index, F32 delay)
    {
        if (index < mNumSounds)
        {
            mSounds[index].PlaySoundDelayed(delay);
        }
    }

    iSndGroupHandle GetSoundGroup(U32 index)
    {
        if (index >= mNumSounds)
        {
            return (iSndGroupHandle)-1;
        }
        zStreamedSound& sound = mSounds[index];
        return sound.mSoundGroupHdl;
    }
};

#endif
