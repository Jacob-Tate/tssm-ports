#include "iMemMgr.h"
#include "iD0Stats.h"
#include "iException.h"
#include "iHost.h"
#include "xMemMgr.h"
#include "xMemoryManager.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>

// PC iMemMgr: gc/iMemMgr.cpp's allocator on treedome's low arena.
//
// TSSM's console allocator is MemoryManagerMulti: four fixed-size pools (4, 8,
// 32 and 48 bytes), a resource arena RenderWare asked for by exact size, and a
// general heap that grows DOWN from the top of the game's DRAM while xMem's
// heap 0 grows UP from the bottom of the same range -- the "heap overlaps
// malloc pool" crash is the two meeting. All of that is kept as retail has it.
//
// What changes is where the memory comes from. The console takes it from the
// OS arena (OSGetArenaLo/Hi) after carving an OS heap for MSL's malloc; the
// port reserves one block below 4 GB with iHostReserveLow, because xMem does
// its arithmetic on gMemInfo.DRAM.addr as a U32 (treedome's design; see
// docs/PCPORT.md there, "Word size and the low-4 GB arena"). There is no OS
// heap: the host CRT's malloc is the C library's own, and iSystem.cpp does not
// replace it the way gc/iSystem.cpp replaces MSL's.
//
// Two host-only differences, each marked at the site:
//   * the 4-byte pool's elements are pointer-sized on a 64-bit host, because a
//     free element holds a FixedHeader*;
//   * the arena is twice the DRAM size: xMemInit puts gxHeap[1] and gxHeap[2]
//     at DRAM.addr + DRAM.size, past the block, which on the console is
//     unclaimed OS arena and here would be a wild write (treedome, same fix).
//
// Not yet looked at: xMemoryManagerGeneral::FreeNode holds two pointers, so it
// is 24 bytes rather than 16 on a 64-bit host, while the size arithmetic below
// (`(size + 0x2f) & ~0x1f`, then `- 0x10`) is written for a 16-byte header.
// A 32-bit build is unaffected; a 64-bit one has to be checked before it runs.

extern xMemInfo gMemInfo;
extern xMemHeap gxHeap[3];

#define ALIGN_UP(x, a) (((UPtr)(x) + ((a) - 1)) & ~(UPtr)((a) - 1))

// The size of the game's DRAM. The console has 24 MB of main RAM and the game
// gets what the OS arena leaves of it (about 20 MB). 64 MB, as treedome's:
// a 64-bit build makes every object with a pointer in it larger, and the heap
// sizes are load-bearing, so the port is not the place to find out by running
// out. The arena is reserved low either way.
#ifdef __3DS__
// The 3DS commits memory when it is allocated, and a New 3DS .3dsx has about
// 83 MB of heap for everything (docs/3DS.md), so the arena is sized like the
// console's rather than treedome's: 28 MB of "DRAM" (the GameCube gave the
// game about 20 MB, and a 32-bit build's objects are the console's size).
// gxHeap[1] and [2] are based at DRAM's end but grow down (xMemInitHeap, flag
// 0x100), so the pad past it is a margin, not where they live. Estimates, until
// a soak run measures the high-water marks; TSSM_ARENA_MB overrides the DRAM size (16 to 64).
#define IMEM_DRAM_SIZE_DEFAULT 0x1C00000
#define IMEM_ARENA_PAD 0x200000
#define IMEM_DRAM_SIZE sDramSize
#define IMEM_ARENA_SIZE (IMEM_DRAM_SIZE + IMEM_ARENA_PAD)
static U32 sDramSize = IMEM_DRAM_SIZE_DEFAULT;
#else
#define IMEM_DRAM_SIZE 0x4000000
#define IMEM_ARENA_SIZE (IMEM_DRAM_SIZE * 2)
#endif

// Element size of the smallest pool: 4 on the console, a pointer on a host.
#define IMEM_FIXED4_SIZE (sizeof(void*) > 4 ? (U32)sizeof(void*) : 4u)

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
            fixed4.Init((void*)ALIGN_UP(startByte, 8), byte4Count, debugDataSize + IMEM_FIXED4_SIZE);
            fixed8.Init((void*)ALIGN_UP(fixed4.arenaEnd, 8), byte8Count, debugDataSize + 8);
            fixed32.Init((void*)ALIGN_UP(fixed8.arenaEnd, 32), byte32Count, debugDataSize + 32);
            // 48 is not a power of two; retail's ALIGN_UP with it is a no-op
            // mask on an already 32-aligned address, and the same here.
            fixed48.Init((void*)ALIGN_UP(fixed32.arenaEnd, 48), byte48Count, debugDataSize + 48);

            startByte = (U8*)ALIGN_UP(fixed48.arenaEnd, 32);
            heap.Init(startByte, (U32)(size - (startByte - (U8*)start) - 16),
                      xMemoryManagerGeneral::Down, xMemoryManagerGeneral::FirstFit, debugging);
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

    inline UPtr GetMallocTop()
    {
        xMemHeap* heap = &gxHeap[gActiveHeap];
        xHeapState* sp = &heap->state[heap->state_idx];
        if (tempCurrent == 0)
        {
            return sp->curr;
        }
        return (UPtr)tempStack[tempCurrent - 1].data + tempStack[tempCurrent - 1].size;
    }

    inline bool IsInArena(const xMemoryManager& manager, void* pointer)
    {
        return pointer >= manager.arenaStart && pointer < manager.arenaEnd;
    }
} // namespace

static bool memInited;
static void* sArena;
static U32 sArenaSize;
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
    if ((UPtr)raw < GetMallocTop())
    {
        char buffer[128];
        // Retail passes the two values in the opposite order to its "### Over by"
        // format string; that only printed right because PPC varargs keep ints
        // and floats apart. Written the right way round here.
        snprintf(buffer, sizeof(buffer),
                 "Heap overlaps malloc pool\nTop: %08X; Ptr: %08X\nOptions: %08X\nOver by %u bytes",
                 (U32)GetMallocTop(), (U32)(UPtr)raw, options, (U32)(GetMallocTop() - (UPtr)raw));
        iExceptionMemCrash("MemoryManagerMulti", size, buffer);
    }

    iD0MemSample();
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
    snprintf(optional, sizeof(optional), "options=%08X", options);
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

#ifdef __3DS__
    const char* arenaEnv = getenv("TSSM_ARENA_MB");
    if (arenaEnv != NULL && arenaEnv[0] != '\0')
    {
        const long mb = strtol(arenaEnv, NULL, 10);
        if (mb >= 16 && mb <= 64)
        {
            sDramSize = (U32)mb << 20;
        }
        else
        {
            printf("tssm: TSSM_ARENA_MB '%s' is not 16 to 64; using %u\n", arenaEnv,
                   (unsigned)(IMEM_DRAM_SIZE_DEFAULT >> 20));
        }
    }
    printf("tssm: game arena: %u MB of DRAM + %u KB pad\n", (unsigned)(IMEM_DRAM_SIZE >> 20),
           (unsigned)(IMEM_ARENA_PAD >> 10));
#endif
    sArenaSize = IMEM_ARENA_SIZE;
    sArena = iHostReserveLow(sArenaSize);
    if (sArena == NULL)
    {
        fprintf(stderr, "iMemInit: could not reserve a %u byte arena\n", sArenaSize);
        exit(-5);
    }

    // Not optional: past 4 GB the U32 arithmetic in xMem silently produces a
    // heap base that points at the wrong memory.
    unsigned long long base64 = (unsigned long long)(UPtr)sArena;
    if (base64 + sArenaSize > 0x100000000ULL)
    {
        fprintf(stderr,
                "iMemInit: arena at %#llx is outside the low 4 GB; the game allocator addresses "
                "memory with U32 and cannot use it\n",
                base64);
        exit(-5);
    }
#ifdef __3DS__
    // For reading a fault address in Luma3DS's dump.
    printf("tssm: game arena at %#llx-%#llx\n", base64, base64 + sArenaSize);
#endif

    hiheap_start = (U32)ALIGN_UP(base64, 32);
    hiheap_end = (U32)((base64 + IMEM_DRAM_SIZE) & ~31ULL);
    hiheap_size = hiheap_end - hiheap_start;
    memoryManager.Init((void*)(UPtr)hiheap_start, hiheap_size, 0xAE000, 768, 768, 512, 850, false);

    // gc fills system and stack from the console's memory map; a host has no
    // OS region at 0 and the stack is not the game's to describe.
    gMemInfo.system.addr = 0;
    gMemInfo.system.size = 0;
    gMemInfo.system.flags = 0x20;
    gMemInfo.stack.addr = 0;
    gMemInfo.stack.size = 0;
    gMemInfo.stack.flags = 0x820;
    gMemInfo.DRAM.addr = (U32)(UPtr)memoryManager.heap.arenaStart;
    gMemInfo.DRAM.size = (U32)((U8*)memoryManager.heap.arenaEnd - (U8*)memoryManager.heap.arenaStart);
    gMemInfo.DRAM.flags = 0x820;
    memInited = true;
}

// Retail frees DRAM.addr through MSL's free, which on the console is the OS
// heap's; the arena here came from iHostReserveLow and goes back there.
void iMemExit()
{
    if (sArena != NULL)
    {
        iHostRelease(sArena, sArenaSize);
        sArena = NULL;
    }
    gMemInfo.DRAM.addr = 0;
    memInited = false;
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

    return memoryManager.Allocate((U32)size, 1);
}

void* iMemMgrRealloc(void* mem, size_t newSize, unsigned int hint)
{
    if (newSize > 48)
    {
        newSize = (newSize + 0x2f) & ~0x1f;
        newSize -= 0x10;
    }

    return memoryManager.Reallocate(mem, (U32)newSize, 0);
}

void iMemMgrFree(void* mem)
{
    memoryManager.Free(mem);
}

void* iMemMgrCalloc(size_t numObj, size_t sizeObj, unsigned int hint)
{
    return memoryManager.Allocate((U32)(numObj * sizeObj), 1);
}

void iMemMgrD0Usage(U32* prefix, U32* mallocExtent)
{
    *prefix = (U32)((U8*)memoryManager.heap.arenaStart - (U8*)(UPtr)hiheap_start);
    *mallocExtent =
        (U32)((U8*)memoryManager.heap.arenaEnd - (U8*)memoryManager.heap.GetCurrentEnd());
}

bool iMemMgrCheckCollisionWithMallocHeap(void* mem, U32 size)
{
    // Every xMemAlloc passes through here, so it is where the arena's
    // high-water mark is cheapest to take.
    iD0MemSample();

    U8 collision =
        (U8*)mem + size >= memoryManager.heap.GetCurrentEnd() && mem < memoryManager.arenaEnd;
    return collision;
}

void* iMemPushTemp(U32 size)
{
    xMemHeap* heap;
    void* end;

    size = (U32)ALIGN_UP(size, 16);

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
    for (S32 i = 0; i < (S32)tempCurrent; i++)
    {
        if (tempStack[i].data == pointer)
        {
            tempStack[i].data = NULL;
            tempStack[i].size = 0;

            if (i + 1 == (S32)tempCurrent)
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
    return (S32)((UPtr)memoryManager.heap.GetCurrentEnd() - sp->curr);
}
