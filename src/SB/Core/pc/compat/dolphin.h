// PC shadow of the Dolphin SDK's <dolphin.h>.
//
// The port has no Dolphin SDK: src/SB/Core/pc replaces the console's platform
// layer, and librw replaces GX for everything RenderWare draws. A few shared
// units still call the SDK directly, though, and those calls are what this
// directory declares -- only those, each with the SDK's own signature:
//
//   GX     zAsync.cpp (GXDrawDone), xHudImage.cpp (constants),
//          xFXHighDynamicRange.cpp and xScreenWarp.cpp
//          (types only; their GX calls are GameCube-only)    dolphin/gx.h
//   MTX    xFXHighDynamicRange.cpp, xScreenWarp.cpp (types)  dolphin/mtx.h
//   VI     zAsync.cpp                                        dolphin/vi.h
//   OS     zGame.cpp, zMain.cpp                              dolphin/os.h
//   CARD   zMain.cpp, zSaveLoad.cpp                          dolphin/card.h
//
// The definitions are in src/SB/Core/pc/stubs/, one file per SDK module, and
// stubs/STUBS.md (written by tools/pcstubs.py) says which of them are stubs.
// The lasting fix for each call is an i* function (gc keeps the call, pc gets a
// host version), through the GameCube gate; as each call moves, its
// declaration here goes. Declaring another SDK function here to make a shared
// file compile is the wrong direction.
//
// The console's platform headers (gc/iSystem.h, gc/isavegame.h, gc/iFile.h)
// include <dolphin.h>, which is how zMain.cpp and zSaveLoad.cpp see the CARD
// calls; pc/iSystem.h and pc/isavegame.h include this file the same way.
#pragma once

#include <dolphin/types.h>
#include <dolphin/os.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/vi.h>
#include <dolphin/card.h>
