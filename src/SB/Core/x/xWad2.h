#ifndef XWAD2_H
#define XWAD2_H

#include "xDebug.h"
#include "zGlobals.h"
// xCam.h before xFont.h (pulled in by xHudText.h below): retail .rodata has xCam.h's templates
// before screen_bounds. It needs xCamera.h from zGlobals.h.
#include "xCam.h"
#include <types.h>
#include <rpworld.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>
#include "xString.h"
#include "string.h"
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cmath>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cstring>
#include "xIni.h"
#include "xHudText.h"
#include "xTextAsset.h"
#include "xstransvc.h"
#include "zScene.h"
#include "zTextBox.h"
#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include "xHudModel.h"
#include "xHudMeter.h"
#include "xHudImage.h"
#include "zHud.h"
#include "iMath.h"
#include "xEvent.h"
#include "xhipio.h"
#include "xGroup.h"
#include "xGrid.h"
#include "zGrid.h"
#include "zFX.h"
#include "rpworld.h"
#include "zSurface.h"
#include "rpmatfx.h"
#include "xFont.h"
#include "xFog.h"
#include "xFFX.h"
#include "xFactory.h"
#include "xEntMotion.h"
#include "xEntBoulder.h"
#include "xRumbleEmitter.h"
#include "zEntPlayer.h"
#include "xShadow.h"
#include "xSnd.h"
#include "xMath.h"
#include "iCollide.h"
#include "zEntDestructObj.h"
#include "zDestructible.h"
#include "iModel.h"
#include "xDebug.h"
#include "zParEmitter.h"
#include <rpptank.h>
#include "xDecal.h"
#include "xClumpColl.h"
#include "iParMgr.h"

static st_HIPLOADDATA* HIPLCreate(const char* filename, char* dblbuf, S32 bufsize, S32 fileflags);
static void HIPLDestroy(st_HIPLOADDATA* lddata);
static U32 HIPLBaseSector(st_HIPLOADDATA* lddata);
static S32 HIPLSetBypass(st_HIPLOADDATA* lddata, S32 enable, S32 use_async);
static void HIPLSetSpot(st_HIPLOADDATA* lddata, S32 spot);
static U32 HIPLBlockEnter(st_HIPLOADDATA* lddata);
static void HIPLBlockExit(st_HIPLOADDATA* lddata);
static S32 HIPLBlockRead(st_HIPLOADDATA* lddata, void* data, S32 cnt, S32 size);
static S32 HIPLBypassRead(st_HIPLOADDATA* lddata, void* data, S32 cnt, S32 size);
static S32 HIPLReadAsync(st_HIPLOADDATA* lddata, S32 pos, char* data, S32 cnt, S32 size);
static en_READ_ASYNC_STATUS HIPLPollRead(st_HIPLOADDATA* lddata);
static S32 HIPLReadBytes(st_HIPLOADDATA* lddata, char* data, S32 cnt);
static S32 HIPLReadShorts(st_HIPLOADDATA* lddata, S16* data, S32 cnt);
static S32 HIPLReadLongs(st_HIPLOADDATA* lddata, S32* data, S32 cnt);
static S32 HIPLReadFloats(st_HIPLOADDATA* lddata, F32* data, S32 cnt);
static S32 HIPLReadString(st_HIPLOADDATA* lddata, char* buf);

#endif
