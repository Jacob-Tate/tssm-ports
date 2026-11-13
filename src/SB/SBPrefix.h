#ifndef SBPREFIX_H
#define SBPREFIX_H

// Precompiled prefix header for all game code (configure.py builds SBPrefix.mch and passes it
// with -prefix; the name is ours). Retail shows these headers were precompiled: their weak inlines
// sit in the using file's .text section and their labels have the same numbers in every wad.
// xRand.h, xString.h and xDebug.h stay out, as retail keeps their inlines or templates per unit.
#include <types.h>
#include "xMath.h"
#include "xMathInlines.h"
#include "xVec3.h"
#include "xVec3Inlines.h"
#include "xMath3.h"
#include "xMemMgr.h"

#endif
