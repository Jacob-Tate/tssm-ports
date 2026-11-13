#ifndef IMEMMGR_H
#define IMEMMGR_H

#include <types.h>

void iMemInit();
void* iMemMgrMalloc(size_t size, unsigned int hint);
void iMemMgrFree(void* mem);
void* iMemMgrRealloc(void* mem, size_t newSize, unsigned int hint);
void* iMemMgrCalloc(size_t numObj, size_t sizeObj, unsigned int hint);
void iMemExit();
void* iMemPushTemp(U32 size);
void iMemPopTemp(void* memory);
void iMemMgrEnableRenderWareMemoryHack(bool hackEnabled);
bool iMemMgrCheckCollisionWithMallocHeap(void* mem, U32 size);
S32 iMemMgrGetMinFreeSpace();

#endif