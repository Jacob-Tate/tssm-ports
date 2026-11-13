#ifndef XFMV_H
#define XFMV_H

#include <types.h>
#include <rwcore.h>

struct MOVIE;

S32 xFMVGetBinkCompliantVolume();
void xFMVPauseSound(bool doPause);
void (*xFMVSetPauseSoundCB(void (*pauseSoundCB)(bool)))(bool);
RwTexture* xFMVGetTexture(MOVIE* pMovie, U16 looping, U16 freezeframed, U32 startframe,
                          U32 endframe, U32);

#endif
