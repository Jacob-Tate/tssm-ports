#include "xMemoryManager.h"
#include "xMemory.h"

#include <stdio.h>

namespace
{
    S32 zeroMemoryAddress = 0;

    U32 MEM_CLEAR_VALUE = 0xD00D00D5;
    U32 MEM_DEALLOC_VALUE = 0x1A13D00D;
} // namespace

void* xMemoryManager::Allocate(U32 size, U32 options)
{
    if (size == 0)
    {
        return &zeroMemoryAddress;
    }

    size = xALIGN(size, 4);

    void* memory = DoAllocate(size + debugDataSize, options);
    if (!memory)
    {
        HandleOutOfMemory(size, options);
        return NULL;
    }

    if (IsDebugging())
    {
        memory = SetupDebugBlock(memory, size, NULL, NULL, 0);
        if (!(options & 0x1))
        {
            xMemorySetV32A32(memory, MEM_CLEAR_VALUE, size);
        }
    }

    if (options & 0x1)
    {
        xMemorySetV32A32(memory, 0, size);
    }

    return memory;
}

void xMemoryManager::Free(void* pointer)
{
    if (!pointer || pointer == &zeroMemoryAddress)
    {
        return;
    }

    if (IsDebugging())
    {
        U32 size;
        pointer = RemoveDebugBlock(pointer, &size);
        xMemorySetV32A32(pointer, MEM_DEALLOC_VALUE, size);
    }

    DoFree(pointer);
}

void* xMemoryManager::Reallocate(void* pointer, U32 size, U32 options)
{
    if (!pointer)
    {
        return Allocate(size, options);
    }

    if (size == 0)
    {
        if (pointer != &zeroMemoryAddress)
        {
            Free(pointer);
        }
        return &zeroMemoryAddress;
    }

    if (IsDebugging())
    {
        pointer = RemoveDebugBlock(pointer, NULL);
    }

    size = xALIGN(size, 4);

    void* newBlock = DoReallocate(pointer, size + debugDataSize, options);
    if (!newBlock)
    {
        HandleOutOfMemory(size, options);
        return NULL;
    }

    if (IsDebugging())
    {
        newBlock = SetupDebugBlock(newBlock, size, NULL, NULL, 0);
    }

    return newBlock;
}

U32 xMemoryManager::GetBlockSize(void* pointer) const
{
    if (pointer == &zeroMemoryAddress)
    {
        return 0;
    }

    return DoGetBlockSize(pointer) - debugDataSize;
}

// Dead-stripped in retail (its two formats remain, unreferenced, in @stringBase0); body from
// Ratatouille, whose GC debug build keeps it.
void xMemoryManager::DumpActiveList() const
{
    DebugAllocationHeader* list = activeList;
    S32 count = 0;

    while (list)
    {
        printf("%4d: %6d bytes at %08X %s#%d (%s)\n", count, list->size, (U32)(UPtr)(list + 1),
               list->file, list->line, list->function);
        list = list->next;
        count++;
    }

    printf("total: %d\n", count);
}

void xMemoryManager::DoInit(void* start, U32 size, bool debugging)
{
    arenaStart = start;
    arenaEnd = (U8*)start + size;
    this->size = size;
    debugDataSize = debugging ? sizeof(DebugAllocationHeader) + sizeof(DebugAllocationTrailer) : 0;
    activeList = NULL;
}

void xMemoryManager::HandleOutOfMemory(U32 size, U32 options)
{
}

void* xMemoryManager::DoReallocate(void* pointer, U32 size, U32 options)
{
    U32 copySize = DoGetBlockSize(pointer);
    if (copySize > size)
    {
        copySize = size;
    }

    void* newBlock = DoAllocate(size, options);
    if (!newBlock)
    {
        return NULL;
    }

    xMemoryCopyUpA32(newBlock, pointer, copySize);
    DoFree(pointer);

    return newBlock;
}
