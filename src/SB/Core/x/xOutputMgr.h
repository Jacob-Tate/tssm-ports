#ifndef XOUTPUTMGR_H
#define XOUTPUTMGR_H

#include <types.h>

// Heavy Iron's debug output manager. Retail has no xOut* code (the final build compiles the
// calls out); like xASSERT (xDebug.h), each call is assumed to expand to ((void)0).

// From the Ratatouille GC DWARF.
enum xOutSeverity
{
    xOutSevIgnore = -1,
    xOutSevCrit = 1,
    xOutSevErr = 2,
    xOutSevWarn = 3,
    xOutSevInfo = 4,
    xOutSevBabble = 5,
    xOutSevForce = 0x7fffffff
};

#define xOutSetSev(sev) ((void)0)
#define xOutEnable(name, trumpsev) ((void)0)

#endif
