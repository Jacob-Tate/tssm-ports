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

// PC-only. A 64-bit host keeps the game's pointers valid in its 32-bit fields
// (U32 asset slots, (U32) round trips) only because every allocator the game
// sees hands out memory below 4 GB: the arena iMemInit reserves (xMem's heaps
// and iMemMgr's pools are carved from it) and iLowHeap's RenderWare heap. Both
// blocks are checked whole when they are reserved. iMemMgr's and iLowHeap's
// allocation calls also pass what they return through this -- including
// iLowHeap's hand-off to the C library's realloc for a block it does not own --
// which stops the game with `who`, the address and the callers if
// [p, p + size) reaches 4 GB, rather than letting a U32 field truncate the
// pointer silently. Nothing on a 32-bit host; one comparison otherwise.
void iMemCheckLow(const void* p, size_t size, const char* who);

#endif