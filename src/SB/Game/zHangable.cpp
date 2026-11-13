#include "zHangable.h"

// No functions of this file survive in the retail DOL (zHangable_EventCB and the rest were
// dead-stripped). The Incredibles PS2 DWARF names the file and its statics; retail keeps playerDPos
// in zWad2's .bss between zFX's poppers and zLasso's sGuideList.
static zHangableObject* sPlayerHangObj;
static xVec3 playerDPos;
static zHangableObject* sHangableObjects;
static S32 sNumHangableObjects;
