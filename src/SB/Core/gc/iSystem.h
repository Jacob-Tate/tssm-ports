#ifndef ISYSTEM_H
#define ISYSTEM_H

#include <types.h>
#include <dolphin.h>

extern "C" {
char* malloc(long msize);
void free(char* p);
}

void iVSync();

// An alarm that resumes a suspended thread when it fires (see iSystemAlarmCallBack).
struct iSystemAlarm
{
    OSAlarm alarm;
    OSThread* thread;
};

void** psGetMemoryFunctions();
void my_dsc(u16 dsc);

void iSystemAlarmCallBack(OSAlarm* alarm, OSContext* context);
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
