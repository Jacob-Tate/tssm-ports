#include "iWad.h"
#include "iException.h"
#include "xMemMgr.h"
#include "xMemoryManager.h"
#include <new>
#include <stdlib.h>
#include "xRenderState.h"
#include "zGame.h"
#include "zParSys.h"

// The PC unity build of the platform modules that were copied from
// src/SB/Core/gc, in gc/iWad.cpp's order. On the console every i-file is
// compiled here; the copies still rely on that (they include little beyond
// their own header), so they are compiled the same way. Each name resolves to
// src/SB/Core/pc/<name>, this file's own directory.
//
// The modules written for the PC (iDraw, iFile, iFMV, iFX, iMemMgr, iPad,
// isavegame, iSnd, iSystem, iTime) are not here: they are whole translation
// units of their own, as treedome's are, and CMakeLists.txt lists them.
#include "iAnim.cpp"
#include "iAnimSKB.cpp"
#include "iAsync.cpp"
#include "iCutscene.cpp"
#include "iEnv.cpp"
#include "iLight.cpp"
#include "iMath.cpp"
#include "iMath3.cpp"
#include "iModel.cpp"
#include "iMorph.cpp"
#include "iParMgr.cpp"
