#include "xWad3.h"
#include "iModel.h"
#include "iMath3.h"
#include "xIsect.h"
#include "xstransvc.h"
#include "xString.h"
#include "xJSP.h"
#include "zGlobals.h"
#include <stdio.h>

#include "xJaw.cpp"
#include "xJSP.cpp"
#include "xLightKit.cpp"
#include "xLightVolume.cpp"
#include "xMath.cpp"
#include "xMath2.cpp"
#include "xMath3.cpp"
#include "xMemMgr.cpp"
#include "xMemoryManager.cpp"
#include "xMemoryManagerFixed.cpp"
#include "xMemoryManagerGeneral.cpp"
#include "xModel.cpp"
#include "xModelBucket.cpp"
#include "xMorph.cpp"
#include "xMovePoint.cpp"

// Placeholders for a dead-stripped file of unknown name (xNPCPred statics in the PS2 DWARF) between
// xMovePoint.cpp and xordarray.cpp: they restore the float literals it left in retail .sdata2, in
// order. Two functions because the conversion double has to come before 0.5f.
static void __deadstripped_xNPCPred2(F32* out)
{
    out[0] = 0.5f;
}

static void __deadstripped_xNPCPred(F32* out, U32 n)
{
    out[0] = 0.0f;
    out[1] = 10.0f;
    out[2] = 1.0f;
    out[3] = n;
}

#include "xordarray.cpp"

// The last .text section of the target (0x801F02E8-0x801F06D0) holds only the weak
// quicksort<CmpAlphaBucket> instantiation used by xModelBucket.cpp.
