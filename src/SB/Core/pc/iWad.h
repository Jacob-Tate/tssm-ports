#ifndef IWAD_H
#define IWAD_H

// The PC counterpart of gc/iWad.h: everything the platform modules copied from
// src/SB/Core/gc expect to have been included before them. On the console
// those files are compiled only inside gc/iWad.cpp's unity build, so most of
// them include just their own header and lean on this one for the rest; the
// copies here are compiled the same way, through pc/iWad.cpp.
//
// Adapted from gc/iWad.h: the MSL, Dolphin, iMix (AX) and "../src/sb/game"
// relative includes are gone, the rest is in gc's order.

#include "zGlobals.h"
#include <stdio.h>
#include <cmath>

#include "iAnim.h"
#include "iAnimSKB.h"
#include "iARAMTmp.h"
#include "iAsync.h"
#include "iCollide.h"
#include "iCollideFast.h"
#include "iColor.h"
#include "iCutscene.h"
#include "iDraw.h"
#include "iEnv.h"
#include "iFile.h"
#include "iFMV.h"
#include "iFX.h"
#include "iLight.h"
#include "iMath.h"
#include "iMath3.h"
#include "iMemMgr.h"
#include "iModel.h"
#include "iMorph.h"
#include "iPad.h"
#include "iParMgr.h"
#include "isavegame.h"
#include "iScrFX.h"
#include "iSnd.h"
#include "iSystem.h"
#include "iTime.h"
#include "iTRC.h"
#include "ngcrad3d.h"
#include "xDebug.h"
#include "xPad.h"
#include "xShadow.h"
#include "xFX.h"
#include "xSnd.h"
#include "xMath.h"
#include "xMath3.h"
#include "iCamera.h"
#include "xTRC.h"
#include <stdlib.h>
#include "xstransvc.h"
#include <limits.h>
#include <rwcore.h>
#include <rtslerp.h>
#include <rpcollis.h>
#include <rphanim.h>
#include <rpmatfx.h>
#include <rpptank.h>
#include <rpskin.h>
#include <rpusrdat.h>

struct ThreadParam
{
    S32 status;
    void (*entry)(void*);
    void* stack;
    S32 stackSize;
    void* gpReg;
    S32 initPriority;
    S32 currentPriority;
    U32 attr;
    U32 option;
    S32 waitType;
    S32 waitId;
    S32 wakeupCount;
};

void iFuncProfileFuncs(S32, S32, F32);

#endif
