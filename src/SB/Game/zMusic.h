#ifndef ZMUSIC_H
#define ZMUSIC_H

#include <types.h>
#include "xBase.h"
#include "iSnd.h"

struct zMusicSituation
{
    S32 track;
    U32 priority;
    U32 countMax;
    F32 delay;
    F32 punchDelay;
    F32 elapsedTime;
    U32 count;
    U32 sndid;
    S32 situation;
    S32 music_enum;
    S32 game_state;
};

struct zVolumeInfo
{
    F32 cur;
    F32 end;
    F32 inc;
    U32 adjusted[2];
};

enum eTrackReason
{
    eMusic,
    eNewMusic,
    eTempMusic
};

struct zMusicTrackInfo
{
    iSndHandle sndHandle;
    U32 sndAssetID;
    F32 volume;
    F32 startVolume;
    F32 targetVolume;
    F32 targetVolumeTime;
    F32 crossFadeStartTime;
    eTrackReason trackDesc;
    F32 playTime;
};

extern F32 gDialogueMusicLevel;

void zMusicRefreshVolume();
void zMusicNotify(S32 situation);
void zMusicNotifyEvent(const F32* toParam, xBase* base);
F32 zMusicGetCurrentVolume(U32);
void zMusicTempSongStopEvent();
void zMusicTempSongEvent(const F32* toParam, U32 toParamWidgetID);
void zMusicNewSongEvent(const F32* toParam, U32 toParamWidgetID);
void zMusicSetVolume(F32 vol, F32 delay);
void zMusicKill();
void zMusicPause();
void zMusicUnpause();
void zMusicReset();
void zMusicInit();
void zMusicUpdate(F32 dt);

// Defined inline in zMusic.cpp (no symbol in TSSM; Ratatouille's debug build has it as a static).
void _zMusicStop(U32 uWhichSlot);

#endif
