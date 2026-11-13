#include "iMemMgr.h"
#include "iException.h"
#include "xMemMgr.h"
#include "xMemoryManager.h"
#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>

extern xMemInfo gMemInfo;
extern xMemHeap gxHeap[3];
extern "C" {
extern U8 _stack_addr[];
extern U8 _stack_end[];
}

#define ALIGN_UP(x, a) (((U32)(x) + ((a) - 1)) & ~((a) - 1))

namespace
{
    struct MemoryManagerMulti : xMemoryManager
    {
        void* resourceArena;
        bool resourceArenaUsed;
        U32 resourceArenaSize;
        xMemoryManagerFixed fixed4;
        xMemoryManagerFixed fixed8;
        xMemoryManagerFixed fixed32;
        xMemoryManagerFixed fixed48;
        xMemoryManagerGeneral heap;

        void* DoAllocate(U32 size, U32 options);
        void DoFree(void* pointer);
        void* DoReallocate(void* pointer, U32 size, U32 options);
        U32 DoGetBlockSize(void* pointer) const;
        void HandleOutOfMemory(U32 size, U32 options);

        void Init(void* start, U32 size, U32 resourceArenaSize, U32 byte4Count, U32 byte8Count,
                  U32 byte32Count, U32 byte48Count, bool debugging)
        {
            DoInit(start, size, debugging);

            resourceArena = (void*)ALIGN_UP(start, 32);
            resourceArenaUsed = false;
            this->resourceArenaSize = resourceArenaSize + debugDataSize;

            U8* startByte = (U8*)start + this->resourceArenaSize;
            fixed4.Init((void*)ALIGN_UP(startByte, 4), byte4Count, debugDataSize + 4);
            fixed8.Init((void*)ALIGN_UP(fixed4.arenaEnd, 8), byte8Count, debugDataSize + 8);
            fixed32.Init((void*)ALIGN_UP(fixed8.arenaEnd, 32), byte32Count, debugDataSize + 32);
            fixed48.Init((void*)ALIGN_UP(fixed32.arenaEnd, 48), byte48Count, debugDataSize + 48);

            startByte = (U8*)ALIGN_UP(fixed48.arenaEnd, 32);
            heap.Init(startByte, size - (startByte - (U8*)start) - 16, xMemoryManagerGeneral::Down,
                      xMemoryManagerGeneral::FirstFit, debugging);
        }
    };

    struct TempBlock
    {
        void* data;
        U32 size;
    };

    TempBlock tempStack[32];
    U32 tempCurrent;
    MemoryManagerMulti memoryManager;

    // Top of the malloc pool: the end of the last temp block, or the current
    // top of the active xMem heap when no temp block is pushed.
    inline U32 GetMallocTop()
    {
        xMemHeap* heap = &gxHeap[gActiveHeap];
        xHeapState* sp = &heap->state[heap->state_idx];
        if (tempCurrent == 0)
        {
            return sp->curr;
        }
        return (U32)tempStack[tempCurrent - 1].data + tempStack[tempCurrent - 1].size;
    }

    inline bool IsInArena(const xMemoryManager& manager, void* pointer)
    {
        return pointer >= manager.arenaStart && pointer < manager.arenaEnd;
    }
} // namespace

static bool memInited;
OSHeapHandle the_heap;
U32 hiheap_start;
U32 hiheap_end;
U32 hiheap_size;
static bool sMemMgrEnableRenderWareMemoryHack;

void* MemoryManagerMulti::DoAllocate(U32 size, U32 options)
{
    if (size == resourceArenaSize && !resourceArenaUsed)
    {
        resourceArenaUsed = true;
        return resourceArena;
    }

    if (size <= fixed4.elementSize)
    {
        void* test = fixed4.Allocate(size, options);
        if (test != NULL)
        {
            return test;
        }
    }

    if (size <= fixed8.elementSize)
    {
        void* test = fixed8.Allocate(size, options);
        if (test != NULL)
        {
            return test;
        }
    }

    if (size <= fixed32.elementSize)
    {
        void* test = fixed32.Allocate(size, options);
        if (test != NULL)
        {
            return test;
        }
    }

    if (size <= fixed48.elementSize)
    {
        void* test = fixed48.Allocate(size, options);
        if (test != NULL)
        {
            return test;
        }
    }

    if (size <= 48)
    {
        size = (size + 0x2f) & ~0x1f;
        size -= 0x10;
    }

    void* raw = heap.Allocate(size, options);
    if ((U32)raw < GetMallocTop())
    {
        char buffer[128];
        sprintf(buffer, "Heap overlaps malloc pool\nTop: %08X; Ptr: %08X\nOptions: %08X",
                GetMallocTop(), raw, options);
        iExceptionMemCrash("MemoryManagerMulti", size, buffer);
        // The arguments are in the opposite order to the format string. This only
        // prints correctly because PPC varargs pass ints and floats in separate registers.
        OSReport("### Over by %d bytes (%6.2f KB)\n", (F32)((U32)raw - GetMallocTop()) / 1024.0f,
                 (U32)raw - GetMallocTop());
    }

    return raw;
}

void* MemoryManagerMulti::DoReallocate(void* pointer, U32 size, U32 options)
{
    if (IsInArena(fixed4, pointer) && size <= fixed4.elementSize)
    {
        return pointer;
    }
    if (IsInArena(fixed8, pointer) && size > fixed4.elementSize && size <= fixed8.elementSize)
    {
        return pointer;
    }
    if (IsInArena(fixed32, pointer) && size > fixed8.elementSize && size <= fixed32.elementSize)
    {
        return pointer;
    }
    if (IsInArena(fixed48, pointer) && size > fixed32.elementSize && size <= fixed48.elementSize)
    {
        return pointer;
    }
    if (IsInArena(heap, pointer))
    {
        return heap.Reallocate(pointer, size, options);
    }
    return xMemoryManager::DoReallocate(pointer, size, options);
}

void MemoryManagerMulti::DoFree(void* pointer)
{
    if (pointer < fixed4.arenaStart)
    {
        resourceArenaUsed = false;
    }
    else if (pointer < fixed4.arenaEnd)
    {
        fixed4.Free(pointer);
    }
    else if (pointer < fixed8.arenaEnd)
    {
        fixed8.Free(pointer);
    }
    else if (pointer < fixed32.arenaEnd)
    {
        fixed32.Free(pointer);
    }
    else if (pointer < fixed48.arenaEnd)
    {
        fixed48.Free(pointer);
    }
    else
    {
        heap.Free(pointer);
    }
}

U32 MemoryManagerMulti::DoGetBlockSize(void* pointer) const
{
    if (pointer < fixed4.arenaStart)
    {
        return resourceArenaSize;
    }
    else if (pointer < fixed4.arenaEnd)
    {
        return fixed4.GetBlockSize(pointer);
    }
    else if (pointer < fixed8.arenaEnd)
    {
        return fixed8.GetBlockSize(pointer);
    }
    else if (pointer < fixed32.arenaEnd)
    {
        return fixed32.GetBlockSize(pointer);
    }
    else if (pointer < fixed48.arenaEnd)
    {
        return fixed48.GetBlockSize(pointer);
    }
    else
    {
        return heap.GetBlockSize(pointer);
    }
}

void MemoryManagerMulti::HandleOutOfMemory(U32 size, U32 options)
{
    char optional[256];
    sprintf(optional, "options=%08X", options);
    iExceptionMemCrash("xMemoryManager::Allocate", size, optional);
}

void iMemInit()
{
    if (memInited)
    {
        return;
    }

    new (&memoryManager) MemoryManagerMulti;
    tempCurrent = 0;

    hiheap_start = ALIGN_UP(OSGetArenaLo(), 32);
    hiheap_end = (U32)OSGetArenaHi() & ~31;

    the_heap = OSCreateHeap(OSInitAlloc((void*)hiheap_start, (void*)(hiheap_start + 0xD7800), 1),
                            (void*)(hiheap_start + 0xD7800));
    if (the_heap >= 0)
    {
        OSSetCurrentHeap(the_heap);
    }
    else
    {
        exit(-5);
    }

    hiheap_start += 0xD7800;
    hiheap_size = hiheap_end - hiheap_start;
    memoryManager.Init((void*)hiheap_start, hiheap_size, 0xAE000, 768, 768, 512, 850, false);

    gMemInfo.system.addr = 0;
    gMemInfo.system.size = 0x100000;
    gMemInfo.system.flags = 0x20;
    gMemInfo.stack.addr = (U32)_stack_addr;
    gMemInfo.stack.size = _stack_end - _stack_addr;
    gMemInfo.stack.flags = 0x820;
    gMemInfo.DRAM.addr = (U32)memoryManager.heap.arenaStart;
    gMemInfo.DRAM.size = (U8*)memoryManager.heap.arenaEnd - (U8*)memoryManager.heap.arenaStart;
    gMemInfo.DRAM.flags = 0x820;
    memInited = true;
}

// This call resolves to MSL's std::free(void*) (stdlib.h), not to iSystem.cpp's free(char*), so
// it is not inlined (as in retail).
void iMemExit()
{
    free((void*)gMemInfo.DRAM.addr);
    gMemInfo.DRAM.addr = 0;
}

void iMemMgrEnableRenderWareMemoryHack(bool hackEnabled)
{
    sMemMgrEnableRenderWareMemoryHack = hackEnabled;
}

void* iMemMgrMalloc(size_t size, unsigned int hint)
{
    if (size == 200 && sMemMgrEnableRenderWareMemoryHack)
    {
        return NULL;
    }

    if (size > 48)
    {
        size = (size + 0x2f) & ~0x1f;
        size -= 0x10;
    }

    return memoryManager.Allocate(size, 1);
}

void* iMemMgrRealloc(void* mem, size_t newSize, unsigned int hint)
{
    if (newSize > 48)
    {
        newSize = (newSize + 0x2f) & ~0x1f;
        newSize -= 0x10;
    }

    return memoryManager.Reallocate(mem, newSize, 0);
}

void iMemMgrFree(void* mem)
{
    memoryManager.Free(mem);
}

void* iMemMgrCalloc(size_t numObj, size_t sizeObj, unsigned int hint)
{
    return memoryManager.Allocate(numObj * sizeObj, 1);
}

bool iMemMgrCheckCollisionWithMallocHeap(void* mem, U32 size)
{
    U8 collision =
        (U8*)mem + size >= memoryManager.heap.GetCurrentEnd() && mem < memoryManager.arenaEnd;
    return collision;
}

// end is computed through xMEMADVANCE (as in Ratatouille's debug build); open-coded pointer math
// allocates the registers differently.
void* iMemPushTemp(U32 size)
{
    xMemHeap* heap;
    void* end;

    size = ALIGN_UP(size, 16);

    if (tempCurrent != 0)
    {
        if (tempCurrent >= 32)
        {
            return NULL;
        }
        tempStack[tempCurrent].data =
            xMEMADVANCE(tempStack[tempCurrent - 1].data, tempStack[tempCurrent - 1].size);
    }
    else
    {
        xHeapState* sp;
        heap = &gxHeap[gActiveHeap];
        sp = &heap->state[heap->state_idx];
        tempStack[0].data = (void*)ALIGN_UP(sp->curr, 16);
    }

    tempStack[tempCurrent].size = size;
    end = xMEMADVANCE(tempStack[tempCurrent].data, tempStack[tempCurrent].size);
    if (end > memoryManager.heap.GetCurrentEnd())
    {
        tempStack[tempCurrent].size = 0;
        return NULL;
    }

    return tempStack[tempCurrent++].data;
}

void iMemPopTemp(void* pointer)
{
    for (S32 i = 0; i < tempCurrent; i++)
    {
        if (tempStack[i].data == pointer)
        {
            tempStack[i].data = NULL;
            tempStack[i].size = 0;

            if (i + 1 == tempCurrent)
            {
                tempCurrent--;
                while (tempCurrent != 0 && tempStack[tempCurrent - 1].data == NULL)
                {
                    tempCurrent--;
                }
            }
        }
    }
}

S32 iMemMgrGetMinFreeSpace()
{
    xHeapState* sp;
    xMemHeap* heap = &gxHeap[gActiveHeap];
    sp = &heap->state[heap->state_idx];
    return (U32)memoryManager.heap.GetCurrentEnd() - sp->curr;
}
