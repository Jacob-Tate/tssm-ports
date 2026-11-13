#ifndef ZMAIN_H
#define ZMAIN_H

#include <types.h>
#include "xIni.h"

extern F32 VBLANKS_PER_SEC;
extern F32 SECS_PER_VBLANK;
extern U32 FB_XRES;
extern U32 FB_YRES;
extern U32 FB_DEPTH;
extern U32 SCENE_ID_MNU_INGAME;
extern U32 SCENE_ID_MNU_START;
extern F32 gSkipTimeCutscene;
extern F32 gSkipTimeFlythrough;

#ifdef __MWERKS__
void main(S32 argc, char** argv);
#else
int main(S32 argc, char** argv);
#endif
void zMainShowProgressBar(S32 percentage);
void zMainMemCardSpaceQuery();
void zMainFirstScreen(S32 mode);

#endif
