#ifndef IHANGDUMP_H
#define IHANGDUMP_H

#include <types.h>

// PC-only test aid for unattended runs: the hang dump.
//
//   TSSM_TEST_HANG_DUMP=<seconds>   when the game thread has made no progress
//                                   for that many seconds (5 when the value is
//                                   not a number), every thread's stack is
//                                   written to the log, named and symbolised,
//                                   and the process ends with exit code 3.
//                                   While it is set, an SDL assertion is
//                                   written to the log the same way (the
//                                   asserting thread first, then every other
//                                   thread, for the first three) and then
//                                   ignored, as a release build of SDL does,
//                                   instead of SDL's message box waiting for a
//                                   click that never comes.
//
// "Progress" is a heartbeat the game thread gives on every window pump (each
// presented frame, each pass of a movie) and every file read (so a long load
// is not a hang). Windows only: the dump is dbghelp's StackWalk64 with the
// symbols of the PDBs beside the executable (tssm_main.cpp). Nothing happens
// unless the variable is set.

// The game thread made progress.
void iHangDumpBeat();

// Heartbeats so far.
U32 iHangDumpBeats();

// Writes every thread's stack to the log under a title line, if this build has
// a dumper (Windows, with TSSM_TEST_HANG_DUMP set). Any thread.
void iHangDumpAll(const char* title);

typedef void (*iHangDumpFn)(const char* title);
void iHangDumpSetDumper(iHangDumpFn fn);

#endif
