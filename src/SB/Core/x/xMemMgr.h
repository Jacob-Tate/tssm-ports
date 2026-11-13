#ifndef XMEMMGR_H
#define XMEMMGR_H

#include <types.h>

#include "iMemMgr.h"

struct xMemBlock;

struct xMemBlkInfo
{
    xMemBlock* header;
    U32 pre;
    U32 block;
    U32 post;
    U32 curr;
    U32 waste;
    U32 total;
};

struct xMemFreeLink
{
    xMemFreeLink* next;
    void (*func)(void*);
    void* param;
};

struct xHeapState
{
    U32 curr;
    U16 blk_ct;
    U16 pad;
    U32 used;
    U32 wasted;
    xMemFreeLink* free_funcs;
};

struct xMemBlock
{
    U32 addr;
    U32 size;
    S32 align;
};

struct xMemHeap
{
    U32 flags;
    U32 hard_base;
    U32 size;
    S16 opp_heap[2];
    xHeapState state[12];
    U16 state_idx;
    U16 max_blks;
    xMemBlock* blk;
    xMemBlock* lastblk;
};

struct xMemArea
{
    U32 addr;
    U32 size;
    U32 flags;
};

struct xMemInfo
{
    xMemArea system;

    // Offset: 0xC
    xMemArea stack;

    // Offset: 0x18
    xMemArea DRAM;

    // No SRAM area in TSSM: gMemInfo is 0x24 bytes in the target.
};

struct xMemPool;
typedef void (*xMemPoolInitCB)(xMemPool*, void*);
struct xMemPool
{
    void* FreeList;
    U16 NextOffset;
    U16 Flags;
    void* UsedList;
    xMemPoolInitCB InitCB;
    void* Buffer;
    U16 Size;
    U16 NumRealloc;
    U32 Total;
};

extern U32 gActiveHeap;
extern U32 scratch_mem_used;

void xMemDebug_SoakLog(const char* id);
void xMemInit();
void xMemExit();
void xMemInitHeap(xMemHeap* heap, U32 base, U32 size, U32 flags);
void* xMemGrowAlloc(U32 heapID, U32 size);
void* xMemAlloc(U32 heapID, U32 size, S32 align);

inline void* xMemPushTemp(U32 size)
{
    return iMemPushTemp(size);
}

// A macro, not an inline function: retail has no xMemPopTemp symbol and calls iMemPopTemp
// directly (Ratatouille's xMemMgr.h has the same macro).
#define xMemPopTemp(pointer) iMemPopTemp((pointer))

S32 xMemPushBase();
S32 xMemPopBase(S32 depth);
void xMemRegisterBaseNotifyFunc(void (*func)());
S32 xMemGetBase();
void xMemPoolSetup(xMemPool* pool, void* buffer, U32 nextOffset, U32 flags, xMemPoolInitCB initCB,
                   U32 size, U32 count, U32 numRealloc);
void* xMemPoolAlloc(xMemPool* pool);
void xMemPoolFree(xMemPool* pool, void* data);
void* xMemScratchPush(U32 size, U32 align);
void xMemScratchPop(void* mem);

// No symbol (always inlined); implied by zExplosion's and zParticleLocator's allocations
// (xMemAlloc of n * sizeof(T) + 16, then __construct_new_array). The heap-ID parameter is
// assumed (Ratatouille's version takes a tag).
inline void* operator new[](size_t size, U32 heapID)
{
    return xMemAlloc(heapID, size, 0);
}

// No symbol (always inlined); the scalar companion of operator new[] above, with the same
// assumed heap-ID parameter. zFXRibbonPoolInit's register use needs the inlined call.
inline void* operator new(size_t size, U32 heapID)
{
    return xMemAlloc(heapID, size, 0);
}

#define xMemGrowAllocSize(size) xMemGrowAlloc(gActiveHeap, size)
#define xMemAllocSize(size) xMemAlloc(gActiveHeap, size, 0)
#define xMemAllocSizeAlign(size, align) xMemAlloc(gActiveHeap, size, align)

#endif
