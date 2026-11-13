#include "zMusic.h"
#include "iTime.h"
#include "xMath.h"
#include "xSnd.h"
#include "zCutsceneMgr.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zScene.h"

// File statics (PS2 DWARF of zWad2: static; retail: local .bss/.sbss). zMenuSB.cpp, earlier in
// the wad, reaches sMusicTrack/sMusic through 'extern' declarations, which CodeWarrior accepts.
WAD_STATIC zMusicTrackInfo sMusicTrack[2];
static xSndGroup* spMusicGroup;
WAD_STATIC zMusicState sMusic;
F32 gDialogueMusicLevel = 0.45f;

static U32 _zMusicCrossFade(F32 fFadeVolume, F32 fFadeVolTime, F32 fNewVolume, F32 fNewVolTime,
                            U32 newTrack, eTrackReason eReason);

// No symbol in TSSM: inlined at every call site (name from Ratatouille's debug build).
inline void _zMusicStop(U32 uWhichSlot)
{
    if (sMusicTrack[uWhichSlot].sndHandle != -1)
    {
        xSndMgrStop(sMusicTrack[uWhichSlot].sndHandle);
    }
    sMusicTrack[uWhichSlot].sndAssetID = 0;
}

// No symbol in TSSM: auto-inlined into zMenuInit and zGameInit, and the out-of-line copy was
// dead-stripped by the linker. It was a plain function: its "music" string sits at this file's
// position in the string pool, i.e. the out-of-line copy was compiled here.
void zMusicInit()
{
    iSndGroupHandle hMusicDefGroup;
    U8 uSoundCount;

    sMusic.bPaused = FALSE;
    sMusic.uMusicDefault = 0;
    sMusic.uMusicRestart = 0;
    sMusicTrack[0].sndHandle = (iSndHandle)-1;
    sMusicTrack[1].sndHandle = (iSndHandle)-1;

    spMusicGroup = xSndMgr_AllocGroup(1);
    spMusicGroup->header.eSoundCategory = 3;

    if (xglobals->sceneCur->parameters->uDefaultMusicHash != 0)
    {
        sMusic.uMusicDefault = xglobals->sceneCur->parameters->uDefaultMusicHash;
    }
    else
    {
        hMusicDefGroup = xSndMgrGetSoundGroup("music");
        if (hMusicDefGroup != (iSndGroupHandle)-1)
        {
            uSoundCount = xSndMgrGetSoundCount(hMusicDefGroup);
            uSoundCount = xrand_RandomChoice(uSoundCount);
            sMusic.uMusicDefault = xSndMgrGetSoundId(hMusicDefGroup, uSoundCount);
        }
    }
}

void zMusicUpdate(F32 dt)
{
    F32 fNowTime;
    U32 i;
    F32 fDeltaTime;
    F32 fHowLongPlaying;

    if (xglobals->NoMusic)
    {
        return;
    }

    if ((gGameMode == eGameMode_Game || gGameMode == eGameMode_Title) &&
        sMusicTrack[0].sndHandle == -1)
    {
        if (globals.cmgr == NULL || globals.cmgr->csn == NULL)
        {
            if (sMusicTrack[1].sndHandle == -1)
            {
                sMusicTrack[0].volume = 1.0f;
                sMusicTrack[0].targetVolume = 1.0f;
                sMusicTrack[0].targetVolumeTime = 0.0f;
                sMusicTrack[0].crossFadeStartTime = iTimeGetGame();
                sMusicTrack[0].trackDesc = eMusic;
                sMusicTrack[0].sndAssetID = spMusicGroup->aSndGroupInfo[0].uSoundNameHash =
                    sMusic.uMusicRestart ? sMusic.uMusicRestart : sMusic.uMusicDefault;
                sMusicTrack[0].sndHandle =
                    xSndMgrPlay((iSndGroupHandle)(UPtr)spMusicGroup, 0xc00, NULL, NULL, NULL,
                                &sMusicTrack[0].volume, NULL);
                sMusic.uMusicRestart = 0;
            }
            else if (sMusicTrack[1].trackDesc == eNewMusic)
            {
                sMusicTrack[0] = sMusicTrack[1];
                sMusicTrack[0].trackDesc = eMusic;
                sMusicTrack[1].sndHandle = (iSndHandle)-1;
                sMusicTrack[1].sndAssetID = 0;
            }
        }
    }
    else if (!xSndMgrIsPlaying(sMusicTrack[0].sndHandle))
    {
        sMusicTrack[0].sndHandle = (iSndHandle)-1;
    }

    fNowTime = iTimeGetGame();

    for (i = 0; i < 2; i++)
    {
        if (sMusicTrack[i].sndHandle == -1)
        {
            continue;
        }

        if (xeq(sMusicTrack[i].volume, sMusicTrack[i].targetVolume, 1e-5f) == FALSE)
        {
            fDeltaTime = fNowTime - sMusicTrack[i].crossFadeStartTime;
            fDeltaTime = sMusicTrack[i].targetVolumeTime ?
                             fDeltaTime / sMusicTrack[i].targetVolumeTime :
                             1.0f;

            if (fDeltaTime >= 1.0f)
            {
                sMusicTrack[i].volume = sMusicTrack[i].targetVolume;

                if (sMusicTrack[i].volume >= -1e-5f && sMusicTrack[i].volume <= 1e-5f)
                {
                    if ((sMusicTrack[i].trackDesc == eMusic ||
                         sMusicTrack[i].trackDesc == eNewMusic) &&
                        sMusicTrack[1].trackDesc == eTempMusic)
                    {
                        xSndMgrSetVolume(sMusicTrack[i].sndHandle, 0.0f);
                        if (sMusicTrack[i].sndHandle != -1)
                        {
                            xSndMgrPause(sMusicTrack[i].sndHandle, true);
                        }
                    }
                    else
                    {
                        if (sMusicTrack[i].sndHandle != -1)
                        {
                            xSndMgrStop(sMusicTrack[i].sndHandle);
                        }
                        sMusicTrack[i].sndAssetID = 0;
                    }
                }
                else
                {
                    xSndMgrSetVolume(sMusicTrack[i].sndHandle, sMusicTrack[i].volume);
                }
            }
            else
            {
                sMusicTrack[i].volume = fDeltaTime * sMusicTrack[i].targetVolume +
                                        (1.0f - fDeltaTime) * sMusicTrack[i].startVolume;
                xSndMgrSetVolume(sMusicTrack[i].sndHandle, sMusicTrack[i].volume);
            }
        }

        if (sMusicTrack[i].trackDesc == eTempMusic)
        {
            fHowLongPlaying = fNowTime - sMusicTrack[i].crossFadeStartTime;

            if (sMusicTrack[i].playTime && fHowLongPlaying > sMusicTrack[i].playTime)
            {
                sMusicTrack[i].startVolume = sMusicTrack[i].volume;
                sMusicTrack[i].targetVolume = 0.0f;
                sMusicTrack[i].crossFadeStartTime = iTimeGetGame();

                sMusicTrack[!i].targetVolume = 1.0f;
                sMusicTrack[!i].crossFadeStartTime = iTimeGetGame();
                sMusicTrack[!i].startVolume = 0.0f;
                if (sMusicTrack[!i].sndHandle != -1)
                {
                    xSndMgrPause(sMusicTrack[!i].sndHandle, false);
                }

                sMusicTrack[i].playTime = 0.0f;
            }
        }
    }
}

void zMusicKill()
{
    U32 i;

    for (i = 0; i < 2; i++)
    {
        if (sMusicTrack[i].sndHandle != -1)
        {
            xSndMgrStop(sMusicTrack[i].sndHandle);
        }
        sMusicTrack[i].sndAssetID = 0;
    }
}

void zMusicPause()
{
    U32 i;

    if (!sMusic.bPaused)
    {
        for (i = 0; i < 2; i++)
        {
            if (sMusicTrack[i].sndHandle != -1)
            {
                xSndMgrPause(sMusicTrack[i].sndHandle, true);
            }
        }

        sMusic.bPaused = TRUE;
    }
}

void zMusicUnpause()
{
    U32 i;

    if (sMusic.bPaused)
    {
        for (i = 0; i < 2; i++)
        {
            if (sMusicTrack[i].sndHandle != -1)
            {
                xSndMgrPause(sMusicTrack[i].sndHandle, false);
            }
        }

        sMusic.bPaused = FALSE;
    }
}

void zMusicSetVolume(F32 vol, F32 delay)
{
    sMusicTrack[0].targetVolume = vol;
    sMusicTrack[0].targetVolumeTime = delay;
    sMusicTrack[0].crossFadeStartTime = iTimeGetGame();
    sMusicTrack[0].startVolume = sMusicTrack[0].volume;
}

static U32 _zMusicCrossFade(F32 fFadeVolume, F32 fFadeVolTime, F32 fNewVolume, F32 fNewVolTime,
                            U32 newTrack, eTrackReason eReason)
{
    U32 i;
    U32 nFadeInSlot;
    U32 j;

    for (i = 0; i < 2; i++)
    {
        if (sMusicTrack[i].sndHandle != -1 &&
            xSndMgrGetSoundId(sMusicTrack[i].sndHandle) == newTrack)
        {
            sMusicTrack[i].crossFadeStartTime = iTimeGetGame();
            sMusicTrack[i].startVolume = sMusicTrack[i].volume;
            sMusicTrack[i].targetVolume = fNewVolume;
            sMusicTrack[i].targetVolumeTime = fNewVolTime;
            return i;
        }
    }

    nFadeInSlot = 0;

    if (eReason == eNewMusic)
    {
        for (j = 0; j < 2; j++)
        {
            if (sMusicTrack[j].sndHandle == -1)
            {
                nFadeInSlot = j;
                break;
            }

            if (sMusicTrack[j].volume < sMusicTrack[nFadeInSlot].targetVolume)
            {
                nFadeInSlot = j;
            }
        }

        if (sMusicTrack[nFadeInSlot].sndHandle != -1)
        {
            xSndMgrStop(sMusicTrack[nFadeInSlot].sndHandle);
            sMusicTrack[nFadeInSlot].sndAssetID = 0;
        }
    }
    else if (eReason == eTempMusic)
    {
        nFadeInSlot = 1;

        if (sMusicTrack[1].sndHandle != -1)
        {
            if (sMusicTrack[1].trackDesc == eNewMusic)
            {
                if (sMusicTrack[0].sndHandle != -1)
                {
                    xSndMgrStop(sMusicTrack[0].sndHandle);
                }

                sMusicTrack[0] = sMusicTrack[1];
                xSndMgrStop(sMusicTrack[1].sndHandle);
                sMusicTrack[1].sndAssetID = 0;
            }
            else
            {
                xSndMgrStop(sMusicTrack[1].sndHandle);
                sMusicTrack[1].sndAssetID = 0;
            }
        }
    }

    sMusicTrack[nFadeInSlot].volume = 0.0f;
    sMusicTrack[nFadeInSlot].startVolume = 0.0f;
    sMusicTrack[nFadeInSlot].crossFadeStartTime = iTimeGetGame();
    sMusicTrack[nFadeInSlot].targetVolume = fNewVolume;
    sMusicTrack[nFadeInSlot].targetVolumeTime = fNewVolTime;
    sMusicTrack[nFadeInSlot].trackDesc = eReason;
    spMusicGroup->aSndGroupInfo[0].uSoundNameHash = newTrack;
    sMusicTrack[nFadeInSlot].sndAssetID = newTrack;
    sMusicTrack[nFadeInSlot].sndHandle =
        xSndMgrPlay((iSndGroupHandle)(UPtr)spMusicGroup, 0xc00, NULL, NULL, NULL,
                    &sMusicTrack[nFadeInSlot].volume, NULL);

    return nFadeInSlot;
}

void zMusicNewSongEvent(const F32* toParam, U32 toParamWidgetID)
{
    F32 fFadeVolTime;
    F32 fNewVolTime;
    F32 fFadeVolume;
    F32 fNewVolume;
    U32 i;

    fFadeVolTime = std::fabsf(toParam[0]);
    fNewVolTime = std::fabsf(toParam[1]);
    fFadeVolume = std::fabsf(toParam[2]);
    fNewVolume = std::fabsf(toParam[3]);

    if (fNewVolume == 99.0f)
    {
        fNewVolume = gDialogueMusicLevel;
    }

    sMusic.uMusicRestart = 0;

    for (i = 0; i < 2; i++)
    {
        if (sMusicTrack[i].sndHandle != -1)
        {
            sMusicTrack[i].startVolume = sMusicTrack[i].volume;
            sMusicTrack[i].targetVolume = fFadeVolume;
            sMusicTrack[i].targetVolumeTime = fFadeVolTime;
            sMusicTrack[i].crossFadeStartTime = iTimeGetGame();
        }
    }

    if (toParamWidgetID != 0)
    {
        if (!sMusic.bPaused)
        {
            _zMusicCrossFade(fFadeVolume, fFadeVolTime, fNewVolume, fNewVolTime, toParamWidgetID,
                             eNewMusic);
        }
    }
    else if (xglobals->sceneCur->parameters->uDefaultMusicHash != 0)
    {
        if (!sMusic.bPaused)
        {
            _zMusicCrossFade(fFadeVolume, fFadeVolTime, fNewVolume, fNewVolTime,
                             xglobals->sceneCur->parameters->uDefaultMusicHash, eNewMusic);
        }
    }
}

void zMusicTempSongEvent(const F32* toParam, U32 toParamWidgetID)
{
    F32 fFadeVolTime;
    F32 fNewVolTime;
    F32 fPlayTime;
    F32 fNewVolume;
    U32 i;
    U32 nSlot;

    fFadeVolTime = std::fabsf(toParam[0]);
    fNewVolTime = std::fabsf(toParam[1]);
    fPlayTime = std::fabsf(toParam[2]);
    fNewVolume = std::fabsf(toParam[3]);

    for (i = 0; i < 2; i++)
    {
        if (sMusicTrack[i].sndHandle != -1)
        {
            sMusicTrack[i].startVolume = sMusicTrack[i].volume;
            sMusicTrack[i].targetVolume = 0.0f;
            sMusicTrack[i].targetVolumeTime = fFadeVolTime;
            sMusicTrack[i].crossFadeStartTime = iTimeGetGame();
        }
    }

    if (toParamWidgetID != 0 && !sMusic.bPaused)
    {
        nSlot = _zMusicCrossFade(0.0f, fFadeVolTime, fNewVolume, fNewVolTime, toParamWidgetID,
                                 eTempMusic);
        sMusicTrack[nSlot].playTime = fPlayTime;
    }
}

void zMusicTempSongStopEvent()
{
    U32 i;

    for (i = 0; i < 2; i++)
    {
        if (sMusicTrack[i].sndHandle != -1 && sMusicTrack[i].trackDesc == eTempMusic)
        {
            sMusicTrack[i].playTime = -1.0f;
        }
    }
}

F32 zMusicGetCurrentVolume(U32)
{
    return sMusicTrack[0].volume;
}
