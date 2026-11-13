#ifndef IARAMTMP_H
#define IARAMTMP_H

#include <types.h>

// Returned by iARAMTmpMalloc when no ARAM-temp block has room.
#define IARAMTMP_NO_MEMORY ((void*)0xBEEEEEEF)

void iARAMTmpEnd(bool wait);
S32 iARAMTmpInARAMTemp(void* ptr);
void iARAMTmpFree(void* ptr);
void* iARAMTmpMalloc(U32 size);
U32 iARAMTmpBegin(U32 type, U32 maxSize);

#endif
