#include "iWad.h"
#include "iException.h"
#include "xMemMgr.h"
#include "xMemoryManager.h"
#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>
#include "xRenderState.h"
#include "zGame.h"
#include "zParSys.h"
#include <fmod/fmod.h>

// Unity build of the GameCube platform layer. Built with deferred inlining, which emits code in
// the reverse of source order: the files are included in their original (alphabetical) order,
// the reverse of their .text order in the target.
#include "iAnim.cpp"
#include "iAnimSKB.cpp"
#include "iAsync.cpp"
#include "iCutscene.cpp"
#include "iDraw.cpp"
#include "iEnv.cpp"
#include "iFile.cpp"
#include "iFMV.cpp"
#include "iFX.cpp"
#include "iLight.cpp"
#include "iMath.cpp"
#include "iMath3.cpp"
#include "iMemMgr.cpp"
#include "iModel.cpp"
#include "iMorph.cpp"
#include "iPad.cpp"
#include "iParMgr.cpp"
#include "isavegame.cpp"
#include "iSnd.cpp"
#include "iSystem.cpp"
#include "iTime.cpp"
