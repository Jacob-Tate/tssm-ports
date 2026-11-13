#ifndef IPERFSAMPLER_H
#define IPERFSAMPLER_H

// A sampling profiler for the game thread, from inside the game: TSSM_PERF_PROFILE
// (iPerf.h). Linux and Android only; elsewhere iPerfSamplerStart says so and
// returns FALSE.
//
// A POSIX timer on the calling thread's own CPU clock sends it SIGPROF every
// 1/hz s of CPU time it uses, so the samples are where that thread spends its
// CPU, never where it sleeps. The handler copies the interrupted PC, the link
// register (arm64) and the frame-pointer chain (arm64 and x86-64 keep frame
// records), checked against the thread's stack bounds, into a buffer made at
// start, with the frame phase iPerf was in (iPerfSamplerSetPhase). Nothing in
// the handler allocates, locks or calls out.
//
// At exit (or iPerfSamplerWrite) the samples are written to a text file:
// identical stacks merged with a count, the process's executable mappings
// (/proc/self/maps) for attributing each address to a library, and dladdr's
// nearest exported symbol for every address outside the game's own library.
// tools/android_bench.py symbolizes the rest from the unstripped libmain.so.

#include <types.h>

S32 iPerfSamplerStart(S32 hz, const char* outPath);
void iPerfSamplerSetPhase(S32 phase);
void iPerfSamplerWrite();

#endif
