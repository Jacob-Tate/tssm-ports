#include "xMemMgr.h"
#include "xDebug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

xMemInfo gMemInfo;
xMemHeap gxHeap[3];
U32 gActiveHeap;
void (*sMemBaseNotifyFunc)();

U32 scratch_mem_used;

namespace
{
    U16 scratch_stack[128];
    // Aligned buffer: retail .bss has 8 bytes of padding after scratch_stack (a 16-byte boundary).
    U8 scratch_buffer[0x4000] ATTRIBUTE_ALIGN(16);
    S32 scratch_allocations;
} // namespace

void xMemDebug_SoakLog(const char*)
{
    printf("MEM FREE = %d K\n", iMemMgrGetMinFreeSpace() / 1024 - 24 * 1024);
}

// The heaps keep addresses in 32-bit fields (gMemInfo, xMemHeap::hard_base, xHeapState::curr,
// xMemBlock::addr), as retail does. A 64-bit host has to place the arena below 4 GB; conversions
// between those fields and pointers go through UPtr. Pool links and scratch rounding are pointer
// arithmetic at full width (the scratch buffer is static data, not arena memory).
void iMemInit();

void xMemInit()
{
    iMemInit();
    xMemInitHeap(&gxHeap[0], gMemInfo.DRAM.addr, gMemInfo.DRAM.size, 0x8826);
    xMemInitHeap(&gxHeap[1], gMemInfo.DRAM.addr + gMemInfo.DRAM.size, gMemInfo.DRAM.size, 0x8925);
    xMemInitHeap(&gxHeap[2], gMemInfo.DRAM.addr + gMemInfo.DRAM.size, gMemInfo.DRAM.size, 0x892A);
    gxHeap[0].opp_heap[0] = 1;
    gxHeap[0].opp_heap[1] = 2;
    gxHeap[1].opp_heap[0] = 0;
    gxHeap[1].opp_heap[1] = 2;
    gxHeap[2].opp_heap[0] = 0;
    gxHeap[2].opp_heap[1] = 1;
    gActiveHeap = 0;
}

void iMemExit();

void xMemExit()
{
    iMemExit();
}

// The compiled-out asserts are Ratatouille's; they keep this from being auto-inlined, as retail
// calls it from xMemInit.
void xMemInitHeap(xMemHeap* heap, U32 base, U32 size, U32 flags)
{
    U32 old_base = base;

    xASSERT(heap);
    xASSERT(base);

    S32 align = 1 << ((flags >> 9) & 0x1F);
    if (flags & 0x100)
    {
        base = base & -align;
        size -= (old_base - base);
    }
    else
    {
        base = (old_base + align - 1) & -align;
        size -= (base - old_base);
    }

    xASSERT((base & (align - 1)) == 0);
    xASSERT(size >= (U32)align);

    heap->state_idx = 0;
    heap->hard_base = base;
    heap->size = size;
    heap->flags = flags;
    heap->lastblk = NULL;
    xHeapState* sp = &heap->state[heap->state_idx];
    sp->curr = base;
    sp->blk_ct = 0;
    sp->free_funcs = NULL;

    if (flags & 0x10000)
    {
        heap->max_blks = (1 << ((flags >> 14) & 0x1));
        heap->blk = (xMemBlock*)malloc(heap->max_blks * sizeof(xMemBlock));
        memset(heap->blk, 0, heap->max_blks * sizeof(xMemBlock));
    }
    else
    {
        heap->max_blks = -1;
        heap->blk = 0;
    }
    heap->opp_heap[0] = -1;
    heap->opp_heap[1] = -1;
}

static U32 xMemGetBlockInfo(xMemHeap* heap, U32 size, S32 align, xMemBlkInfo* info)
{
    S32 total;
    S32 hdr;
    S32 pre;
    S32 block;
    S32 post;
    S32 dir;
    xHeapState* sp = &heap->state[heap->state_idx];
    S32 header_size;

    dir = (heap->flags & 0x100) ? -1 : 1;
    header_size = (heap->flags & 0x8000) ? (S32)sizeof(xMemBlock) : 0;

    if (heap->flags & 0x100)
    {
        hdr = -header_size;

        S32 remainder = (align - 1) & (sp->curr - header_size - size);

        block = -(S32)(header_size + size + remainder);
        pre = block;
        post = block + size;
        total = -pre;
    }
    else
    {
        hdr = 0;

        S32 remainder = (align - 1) & (sp->curr + header_size);
        if (!remainder)
        {
            remainder = align;
        }

        block = align + header_size - remainder;
        pre = block;
        post = block + size;
        total = post;
    }

    total = (total + 3) & ~3;

    if (heap->flags & 0x10000)
    {
        info->header = &heap->blk[sp->blk_ct];
    }
    else
    {
        info->header = (xMemBlock*)(UPtr)(sp->curr + hdr);
    }

    info->pre = sp->curr + pre;
    info->block = sp->curr + block;
    info->post = sp->curr + post;
    info->curr = sp->curr + dir * total;
    info->waste = total - (header_size + size);
    info->total = total;

    return total;
}

void* xMemGrowAlloc(U32 heapID, U32 size)
{
    xMemHeap* heap;
    xMemBlock* hdr;
    xHeapState* sp;
    void* memptr;
    // not in the PS2 DWARF: the old block size is used four times.
    U32 oldalignsize;
    U32 newalignsize;

    size = (size + 3) & ~3;

    heap = &gxHeap[heapID];
    hdr = heap->lastblk;
    sp = &heap->state[heap->state_idx];

    oldalignsize = hdr->size;
    newalignsize = hdr->size + size;
    if (sp->used + (newalignsize - oldalignsize) > heap->size)
    {
        return NULL;
    }

    if (heap->flags & 0x100)
    {
        memptr = (void*)(UPtr)(hdr->addr - size);
        hdr->addr = (U32)(UPtr)memptr;
        sp->curr -= newalignsize - oldalignsize;
    }
    else
    {
        memptr = (void*)(UPtr)(hdr->addr + hdr->size);
        sp->curr += newalignsize - oldalignsize;
    }

    sp->used += newalignsize - oldalignsize;
    hdr->size += size;

    memset(memptr, 0, size);

    return memptr;
}

static U32 xMemGetBlockInfo(xMemHeap* heap, U32 size, S32 align, xMemBlkInfo* info);

// The compiled-out asserts are Ratatouille's xMemAlloc checks; they keep this from being
// auto-inlined, as retail calls it. XMEM_MAX_HEAPS and eMemActive are not defined yet: define
// them before enabling asserts.
void* xMemAlloc(U32 heapID, U32 size, S32 align)
{
    xMemHeap* heap;
    xMemBlock* hdr;
    xHeapState* sp;
    xMemBlkInfo info;
    U32 total;

    // Not in the PS2 DWARF.
    S32 true_align;

    xASSERT(heapID >= 0 && heapID <= XMEM_MAX_HEAPS);
    heap = &gxHeap[heapID];
    sp = &heap->state[heap->state_idx];

    xASSERT(heap->flags & eMemActive);
    true_align = 1 << (heap->flags >> 9 & 0x1f);
    if (align > true_align)
    {
        true_align = align;
    }
    xASSERT((true_align & (true_align - 1)) == 0);

    if (size == 0)
    {
        return (void*)0xDEADBEEF;
    }

    total = xMemGetBlockInfo(heap, size, true_align, &info);
    hdr = info.header;

    if (sp->used + total > heap->size)
    {
        xASSERT(sp->used + total <= heap->size);
        return NULL;
    }

    sp->curr = info.curr;
    sp->blk_ct += 1;
    if (heap->flags & 0x10000)
    {
        xASSERT(sp->blk_ct <= heap->max_blks);
        return NULL;
    }

    hdr->addr = info.block;
    hdr->size = size;
    hdr->align = true_align;

    if (iMemMgrCheckCollisionWithMallocHeap((void*)(UPtr)hdr->addr, hdr->size))
    {
        printf("Out of memory crash, Loc=xMemAlloc Size=%d, heap overlaps malloc pool\n",
               hdr->size);
    }

    sp->used += total;
    sp->wasted += info.waste;

    xASSERT(sp->used == (U32)abs((S32)sp->curr - (S32)heap->hard_base));

    memset((void*)(UPtr)hdr->addr, 0, size);
    heap->lastblk = hdr;
    return (void*)(UPtr)hdr->addr;
}

static inline S32 xMemPushBase(U32 heapID)
{
    xMemHeap* heap = &gxHeap[heapID];

    heap->state_idx++;
    xHeapState* sp = &heap->state[heap->state_idx];
    *sp = *(sp - 1);
    sp->free_funcs = NULL;

    if (sMemBaseNotifyFunc != NULL)
    {
        sMemBaseNotifyFunc();
    }
    return heap->state_idx - 1;
}

S32 xMemPushBase()
{
    return xMemPushBase(gActiveHeap);
}

static inline S32 xMemPopBase(U32 heapID, S32 depth)
{
    xMemHeap* heap = &gxHeap[heapID];
    S32 new_idx;
    S32 popped;
    xMemFreeLink* list;

    if (depth < 0)
    {
        new_idx = heap->state_idx + depth;
    }
    else
    {
        new_idx = depth;
    }

    for (popped = heap->state_idx; popped > new_idx; popped--)
    {
        for (list = heap->state[popped].free_funcs; list != NULL; list = list->next)
        {
            list->func(list->param);
        }
    }

    heap->state_idx = new_idx;
    if (sMemBaseNotifyFunc != NULL)
    {
        sMemBaseNotifyFunc();
    }

    return heap->state_idx;
}

S32 xMemPopBase(S32 depth)
{
    return xMemPopBase(gActiveHeap, depth);
}

void xMemRegisterBaseNotifyFunc(void (*func)())
{
    sMemBaseNotifyFunc = func;
}

static inline S32 xMemGetBase(U32 heapID)
{
    return gxHeap[heapID].state_idx;
}

S32 xMemGetBase()
{
    return xMemGetBase(gActiveHeap);
}

static inline void xMemPoolAddElements(xMemPool* pool, void* buffer, U32 count)
{
    S32 i;
    void* curr;
    void (*initCB)(xMemPool*, void*);
    U32 next;
    U32 size;

    initCB = pool->InitCB;
    next = pool->NextOffset;
    size = pool->Size;

    curr = buffer;
    for (i = 0; i < (S32)count - 1; i++)
    {
        *(void**)((UPtr)curr + next) = (void*)((UPtr)curr + size);
        if (initCB != NULL)
        {
            initCB(pool, curr);
        }
        curr = (void*)((UPtr)curr + size);
    }

    *(void**)((UPtr)curr + next) = pool->FreeList;
    if (initCB != NULL)
    {
        initCB(pool, curr);
    }
    pool->FreeList = buffer;
    pool->Total += count;
}

void xMemPoolSetup(xMemPool* pool, void* buffer, U32 nextOffset, U32 flags, xMemPoolInitCB initCB,
                   U32 size, U32 count, U32 numRealloc)
{
    pool->FreeList = NULL;
    pool->NextOffset = nextOffset;
    pool->Flags = flags;
    pool->UsedList = NULL;
    pool->InitCB = initCB;
    pool->Buffer = buffer;
    pool->Size = size;
    pool->NumRealloc = numRealloc;
    pool->Total = 0;
    xMemPoolAddElements(pool, buffer, count);
}

void* xMemPoolAlloc(xMemPool* pool)
{
    void* retval = pool->FreeList;
    U32 next = pool->NextOffset;
    U32 flags = pool->Flags;

    if (retval == NULL)
    {
        xMemPoolAddElements(pool, xMemAlloc(gActiveHeap, pool->NumRealloc * pool->Size, 0),
                            pool->NumRealloc);
        retval = pool->FreeList;
    }

    pool->FreeList = *(void**)((UPtr)retval + next);
    if (flags & 1)
    {
        *(void**)((UPtr)retval + next) = pool->UsedList;
        pool->UsedList = retval;
    }

    return retval;
}

void xMemPoolFree(xMemPool* pool, void* data)
{
    // Not in the DWARF order (freeList, next, prev, curr): declaring prev after next changes the
    // generated code.
    void** prev;
    void* freeList;
    U32 next;
    void* curr;

    if (data == NULL)
    {
        return;
    }

    freeList = pool->FreeList;
    next = pool->NextOffset;
    if (pool->Flags & 1)
    {
        prev = &pool->UsedList;
        curr = pool->UsedList;

        while (curr != NULL && curr != data)
        {
            prev = (void**)((UPtr)curr + next);
            curr = *prev;
        }

        if (curr != NULL)
        {
            *prev = *(void**)((UPtr)curr + next);
        }
    }

    *(void**)((UPtr)data + next) = freeList;
    pool->FreeList = data;
}

void* xMemScratchPush(U32 size, U32 align)
{
    // Not in the PS2 DWARF (the PS2 build uses a different scratch memory path).
    U8* scratch = scratch_buffer;
    U32 align_mask;
    U8* mem;
    U32 used;

    if (align < 4)
    {
        align = 4;
    }

    align_mask = align - 1;
    mem = (U8*)((UPtr)(scratch + scratch_mem_used + align_mask) & ~(UPtr)align_mask);
    used = size + (mem - scratch);
    if (used > sizeof(scratch_buffer))
    {
        return NULL;
    }

    scratch_stack[scratch_allocations] = used - scratch_mem_used;
    scratch_mem_used = used;
    scratch_allocations++;
    return mem;
}

void xMemScratchPop(void* mem)
{
    scratch_allocations--;
    scratch_mem_used -= scratch_stack[scratch_allocations];
}
