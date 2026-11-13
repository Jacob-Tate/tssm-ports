#ifndef ISYSTEM_H
#define ISYSTEM_H

// PC counterpart of gc/iSystem.h.
//
// Gone, because they are the console's and nothing outside src/SB/Core/gc
// reads them: the MSL malloc(long)/free(char*) replacements (a host keeps its
// own C library's), iSystemAlarm (an OSAlarm that resumes an OSThread),
// iSystemAlarmCallBack and psGetMemoryFunctions. The ARAM functions stay with
// host meanings; see iSystem.cpp.

#include <types.h>
#include "iTime.h"
// gc/iSystem.h includes the Dolphin SDK; this is the PC subset (compat/dolphin.h).
#include <dolphin.h>

#include <stdlib.h>

void iVSync();

void my_dsc(u16 dsc);

void iSystemSleep(S32 milliSecondsToSleep);
void iSystemARAMInit();
U32 iSystemARAMBasePtrForFMOD();
U32 iSystemARAMSizeForFMOD();
void iSystemInitRWFailSafe();
void iSystemExitRWFailSafe();
void iSystemARAMFree(void* ptr);
void* iSystemARAMAlloc(U32 numbytes_to_use);
void iSystemInit(U32 options);
void iSystemInitGraphics();
void iSystemExit();
void iSystemGetWidthHeight(U32& uWidth, U32& uHeight, bool bPALMode);

void null_func();

#endif
