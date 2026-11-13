// The game's linker-COMMON globals, defined once for a host link.
//
// Each of these is declared `extern` in the game's headers and defined in no
// source file. On the console they are .bss objects that the GameCube build
// gets from its symbol map (config/GGVE78/symbols.txt), where retail's objects
// carried them as COMMON; a host linker needs one definition, and this is it.
// Like the console's .bss, every one starts zeroed; the game initialises them
// itself (xMath3Init fills g_I3, xCutscene_Init clears sActiveCutscene, ...).
//
// zcam_backupconvers is not in retail's symbol map at all: its only users,
// zCameraSetConvers and zCameraDoTrans (zCamera.cpp), are dead-stripped there.
// A host link keeps every function, so the object they name must exist.
//
// The opposite case -- an object defined in a header and so in several wads,
// which CodeWarrior's -common on merges -- is handled in the shared headers
// (types.h's WAD_COMMON).

#include "xCutscene.h"
#include "xMath3.h"
#include "xModel.h"
#include "xPad.h"
#include "xQuickCull.h"
#include "xSnd.h"
#include "xTransient.h"
#include "zCamera.h"
#include "zScene.h"

xSndGlobals gSnd;
_tagxPad mPad[4];
xMat4x3 g_I3;
xCutscene sActiveCutscene;
xQCControl xqc_def_ctrl;
RpMorphTarget anim_coll_old_mt;
_tagClimate gClimate;
xCamera zcam_backupcam;
xCamera zcam_backupconvers;
xQuat zcam_quat;

namespace xTransient
{
    xSpacePartitionTree2D space_tree;
}
