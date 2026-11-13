#include "xMemoryManager.h"

void xMemoryManagerFixed::Init(void* start, U32 elements, U32 elementSize)
{
    DoInit(start, elements * elementSize, false);
    this->elements = elements;
    this->elementSize = elementSize;
    InitMemory();
}

void* xMemoryManagerFixed::DoAllocate(U32 size, U32 options)
{
    FixedHeader* ret = freeList;
    if (ret == NULL)
    {
        return NULL;
    }

    freeList = ret->next;
    return ret;
}

void xMemoryManagerFixed::DoFree(void* pointer)
{
    FixedHeader* header = (FixedHeader*)pointer;
    header->next = freeList;
    freeList = header;
}

void* xMemoryManagerFixed::DoReallocate(void* pointer, U32 size, U32 options)
{
    return pointer;
}

U32 xMemoryManagerFixed::DoGetBlockSize(void* pointer) const
{
    return elementSize;
}

// No retail symbol: a plain member auto-inlined into its only caller Init and dead-stripped
// (name from the Ratatouille GC DWARF).
void xMemoryManagerFixed::InitMemory()
{
    freeList = (FixedHeader*)arenaStart;
    FixedHeader* cur = freeList;
    for (U32 i = 0; i < elements - 1; i++)
    {
        cur->next = (FixedHeader*)((U8*)cur + elementSize);
        cur = cur->next;
    }
    cur->next = NULL;
}
