// RenderWare's heap below 4 GB; see iLowHeap.h. Port-only.
//
// A two-level segregated fit allocator (TLSF, after Masmano et al.), written
// for this port. Blocks carry a 16-byte header -- the size of the physically
// previous block and this block's size with a "free" flag -- so payloads are
// 16-byte aligned and both neighbours are reachable in O(1) for coalescing.
// Free blocks keep their list links in the payload. Free blocks are indexed by
// size: the first level is the power of two, the second splits each power into
// 32 ranges, and two bitmaps find the smallest non-empty list that is big
// enough without a search.

#include "iLowHeap.h"

#include "iHost.h"

#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Block
    {
        // Total size of the physically previous block, header included; 0 for
        // the first block.
        U64 prevSize;

        // Total size of this block, header included, a multiple of 16; bit 0
        // set while the block is free.
        U64 sizeFlags;

        // In the payload, and only while the block is free.
        Block* nextFree;
        Block* prevFree;
    };

    const size_t kHeader = 16;
    const size_t kAlign = 16;
    const size_t kMinBlock = 32;
    const U64 kFree = 1;

    const int kAlignLog2 = 4;
    const int kSlLog2 = 5;
    const int kSlCount = 1 << kSlLog2;
    const int kFlShift = kSlLog2 + kAlignLog2;
    const size_t kSmallBlock = (size_t)1 << kFlShift;

    // Block sizes stay below 4 GB: first-level indices 0..23.
    const int kFlCount = 32 - kFlShift + 1;

    const U32 kDefaultMB = 256;

    std::mutex sLock;

    U8* sBase;
    U32 sReserved;
    U8* sHeapStart;
    U8* sHeapEnd;

    U32 sFlBitmap;
    U32 sSlBitmap[kFlCount];
    Block* sFree[kFlCount][kSlCount];

    size_t sUsed;
    size_t sPeak;
    bool sReportedFull;
    bool sReportedForeign;

    int Fls(U32 x)
    {
        return x != 0 ? 31 - __builtin_clz(x) : -1;
    }

    int Ffs(U32 x)
    {
        return x != 0 ? __builtin_ctz(x) : -1;
    }

    size_t SizeOf(const Block* b)
    {
        return (size_t)(b->sizeFlags & ~(U64)15);
    }

    bool IsFree(const Block* b)
    {
        return (b->sizeFlags & kFree) != 0;
    }

    Block* Next(Block* b)
    {
        return (Block*)((U8*)b + SizeOf(b));
    }

    Block* Prev(Block* b)
    {
        return b->prevSize != 0 ? (Block*)((U8*)b - b->prevSize) : NULL;
    }

    void* Payload(Block* b)
    {
        return (U8*)b + kHeader;
    }

    Block* FromPayload(void* p)
    {
        return (Block*)((U8*)p - kHeader);
    }

    bool InHeap(const void* p)
    {
        return (const U8*)p >= sHeapStart && (const U8*)p < sHeapEnd;
    }

    void MappingInsert(size_t size, int* fl, int* sl)
    {
        if (size < kSmallBlock)
        {
            *fl = 0;
            *sl = (int)(size >> kAlignLog2);
        }
        else
        {
            const int f = Fls((U32)size);
            *sl = (int)(size >> (f - kSlLog2)) ^ kSlCount;
            *fl = f - (kFlShift - 1);
        }
    }

    // The list to start looking in for `size`: rounded up to the next list
    // boundary, so that any block found there is big enough.
    void MappingSearch(size_t size, int* fl, int* sl)
    {
        if (size >= kSmallBlock)
        {
            size += ((size_t)1 << (Fls((U32)size) - kSlLog2)) - 1;
        }
        MappingInsert(size, fl, sl);
    }

    void Insert(Block* b)
    {
        int fl, sl;
        MappingInsert(SizeOf(b), &fl, &sl);

        Block* head = sFree[fl][sl];
        b->nextFree = head;
        b->prevFree = NULL;
        if (head != NULL)
        {
            head->prevFree = b;
        }
        sFree[fl][sl] = b;

        sFlBitmap |= 1u << fl;
        sSlBitmap[fl] |= 1u << sl;
    }

    void Remove(Block* b)
    {
        int fl, sl;
        MappingInsert(SizeOf(b), &fl, &sl);

        if (b->prevFree != NULL)
        {
            b->prevFree->nextFree = b->nextFree;
        }
        else
        {
            sFree[fl][sl] = b->nextFree;
        }
        if (b->nextFree != NULL)
        {
            b->nextFree->prevFree = b->prevFree;
        }

        if (sFree[fl][sl] == NULL)
        {
            sSlBitmap[fl] &= ~(1u << sl);
            if (sSlBitmap[fl] == 0)
            {
                sFlBitmap &= ~(1u << fl);
            }
        }
    }

    Block* FindFree(size_t size)
    {
        int fl, sl;
        MappingSearch(size, &fl, &sl);
        if (fl >= kFlCount)
        {
            return NULL;
        }

        U32 slMap = sSlBitmap[fl] & (~0u << sl);
        if (slMap == 0)
        {
            if (fl + 1 >= kFlCount)
            {
                return NULL;
            }
            const U32 flMap = sFlBitmap & (~0u << (fl + 1));
            if (flMap == 0)
            {
                return NULL;
            }
            fl = Ffs(flMap);
            slMap = sSlBitmap[fl];
        }
        sl = Ffs(slMap);
        return sFree[fl][sl];
    }

    // A free block that is not on a list yet: merged with whichever neighbours
    // are free, then listed.
    void Release(Block* b)
    {
        b->sizeFlags = SizeOf(b) | kFree;

        Block* next = Next(b);
        if (IsFree(next))
        {
            Remove(next);
            b->sizeFlags = (SizeOf(b) + SizeOf(next)) | kFree;
        }

        Block* prev = Prev(b);
        if (prev != NULL && IsFree(prev))
        {
            Remove(prev);
            prev->sizeFlags = (SizeOf(prev) + SizeOf(b)) | kFree;
            b = prev;
        }

        Next(b)->prevSize = SizeOf(b);
        Insert(b);
    }

    // Cuts a used block down to `size` when the rest is big enough to be a
    // block of its own, and releases the rest.
    void Trim(Block* b, size_t size)
    {
        const size_t have = SizeOf(b);
        if (have < size + kMinBlock)
        {
            return;
        }

        Block* rest = (Block*)((U8*)b + size);
        rest->prevSize = size;
        rest->sizeFlags = have - size;
        b->sizeFlags = size;
        Next(rest)->prevSize = have - size;
        sUsed -= have - size;
        Release(rest);
    }

    // Block size for a request, header included; 0 when it cannot be met.
    size_t BlockSizeFor(size_t request)
    {
        if (request > ((size_t)1 << 31))
        {
            return 0;
        }
        size_t size = (request + kAlign - 1) & ~(kAlign - 1);
        size += kHeader;
        return size < kMinBlock ? kMinBlock : size;
    }

    void ReportFull(size_t request)
    {
        if (!sReportedFull)
        {
            sReportedFull = true;
            printf("tssm: FATAL -- RenderWare's low heap is full: %llu bytes asked for, %llu of %u "
                   "MB in use. Set TSSM_RWHEAP_MB higher.\n",
                   (unsigned long long)request, (unsigned long long)sUsed,
                   (unsigned)(sReserved >> 20));
            fflush(stdout);
        }
    }

    void* MallocLocked(size_t request)
    {
        const size_t size = BlockSizeFor(request == 0 ? 1 : request);
        Block* b = size != 0 ? FindFree(size) : NULL;
        if (b == NULL)
        {
            ReportFull(request);
            return NULL;
        }

        Remove(b);
        b->sizeFlags = SizeOf(b);
        sUsed += SizeOf(b);
        Trim(b, size);

        if (sUsed > sPeak)
        {
            sPeak = sUsed;
        }
        return Payload(b);
    }

    void FreeLocked(void* mem)
    {
        Block* b = FromPayload(mem);
        if (IsFree(b) || ((UPtr)mem & (kAlign - 1)) != 0)
        {
            printf("tssm: low heap: free of %p, which is not a live block; ignored\n", mem);
            fflush(stdout);
            return;
        }

        sUsed -= SizeOf(b);
        Release(b);
    }

    // Pointers the heap did not hand out -- anything allocated before it
    // existed. Passed to the C library, and said once.
    void ReportForeign(const char* what, void* mem)
    {
        if (!sReportedForeign)
        {
            sReportedForeign = true;
            printf("tssm: low heap: %s of %p, which is not in the heap; passed to the C "
                   "library\n",
                   what, mem);
            iHostPrintCallers("low heap", 12);
            fflush(stdout);
        }
    }
} // namespace

S32 iLowHeapWanted()
{
    // TSSM_RWHEAP=1 turns it on in a 32-bit build too, to exercise the heap
    // where the rest of the game already runs.
    const char* env = getenv("TSSM_RWHEAP");
    if (env != NULL && env[0] == '1')
    {
        return TRUE;
    }
    return sizeof(void*) > 4;
}

S32 iLowHeapInit()
{
    if (sBase != NULL)
    {
        return TRUE;
    }

    U32 mb = kDefaultMB;
    const char* env = getenv("TSSM_RWHEAP_MB");
    if (env != NULL && env[0] != '\0')
    {
        const long v = strtol(env, NULL, 10);
        if (v >= 16 && v <= 3072)
        {
            mb = (U32)v;
        }
        else
        {
            printf("tssm: TSSM_RWHEAP_MB '%s' is not 16 to 3072; using %u\n", env, kDefaultMB);
        }
    }

    sReserved = mb << 20;
    sBase = (U8*)iHostReserveLow(sReserved);
    if (sBase == NULL)
    {
        printf("tssm: could not reserve %u MB for RenderWare's heap\n", mb);
        fflush(stdout);
        return FALSE;
    }

    const unsigned long long base64 = (unsigned long long)(UPtr)sBase;
    if (base64 + sReserved > 0x100000000ULL)
    {
        printf("tssm: RenderWare's heap landed at %#llx, outside the low 4 GB; RenderWare "
               "pointers the game keeps in 32 bits would not survive\n",
               base64);
        fflush(stdout);
        iHostRelease(sBase, sReserved);
        sBase = NULL;
        return FALSE;
    }

    sHeapStart = (U8*)(((UPtr)sBase + kAlign - 1) & ~(UPtr)(kAlign - 1));
    sHeapEnd = (U8*)(((UPtr)sBase + sReserved) & ~(UPtr)(kAlign - 1));

    // One free block over everything but a closing header, which is marked in
    // use with size 0 so that no block ever merges past the end.
    Block* first = (Block*)sHeapStart;
    Block* end = (Block*)(sHeapEnd - kHeader);
    first->prevSize = 0;
    first->sizeFlags = (U64)((U8*)end - (U8*)first);
    end->prevSize = SizeOf(first);
    end->sizeFlags = 0;

    sFlBitmap = 0;
    memset(sSlBitmap, 0, sizeof(sSlBitmap));
    memset(sFree, 0, sizeof(sFree));
    sUsed = 0;
    sPeak = 0;
    Release(first);

    printf("tssm: RenderWare heap: %u MB at %p (below 4 GB)\n", mb, (void*)sHeapStart);
    fflush(stdout);
    return TRUE;
}

void* iLowHeapMalloc(size_t size)
{
    std::lock_guard<std::mutex> hold(sLock);
    return MallocLocked(size);
}

void* iLowHeapCalloc(size_t count, size_t size)
{
    if (size != 0 && count > ((size_t)1 << 31) / size)
    {
        return NULL;
    }
    void* mem = iLowHeapMalloc(count * size);
    if (mem != NULL)
    {
        memset(mem, 0, count * size);
    }
    return mem;
}

void iLowHeapFree(void* mem)
{
    if (mem == NULL)
    {
        return;
    }
    if (!InHeap(mem))
    {
        ReportForeign("free", mem);
        free(mem);
        return;
    }

    std::lock_guard<std::mutex> hold(sLock);
    FreeLocked(mem);
}

void* iLowHeapRealloc(void* mem, size_t size)
{
    if (mem == NULL)
    {
        return iLowHeapMalloc(size);
    }
    if (size == 0)
    {
        iLowHeapFree(mem);
        return NULL;
    }
    if (!InHeap(mem))
    {
        ReportForeign("realloc", mem);
        return realloc(mem, size);
    }

    size_t keep;
    {
        std::lock_guard<std::mutex> hold(sLock);

        Block* b = FromPayload(mem);
        const size_t want = BlockSizeFor(size);
        if (want == 0)
        {
            return NULL;
        }

        // In place: already big enough, or big enough with the free block
        // after it.
        if (want <= SizeOf(b))
        {
            Trim(b, want);
            return mem;
        }

        Block* next = Next(b);
        if (IsFree(next) && SizeOf(b) + SizeOf(next) >= want)
        {
            Remove(next);
            sUsed += SizeOf(next);
            b->sizeFlags = SizeOf(b) + SizeOf(next);
            Next(b)->prevSize = SizeOf(b);
            Trim(b, want);
            if (sUsed > sPeak)
            {
                sPeak = sUsed;
            }
            return mem;
        }

        keep = SizeOf(b) - kHeader;
    }

    void* moved = iLowHeapMalloc(size);
    if (moved == NULL)
    {
        return NULL;
    }
    memcpy(moved, mem, keep < size ? keep : size);
    iLowHeapFree(mem);
    return moved;
}

size_t iLowHeapUsed()
{
    std::lock_guard<std::mutex> hold(sLock);
    return sUsed;
}

size_t iLowHeapPeak()
{
    std::lock_guard<std::mutex> hold(sLock);
    return sPeak;
}

void iLowHeapReport(const char* when)
{
    if (sBase == NULL)
    {
        return;
    }
    size_t used, peak;
    {
        std::lock_guard<std::mutex> hold(sLock);
        used = sUsed;
        peak = sPeak;
    }
    printf("tssm: RenderWare heap %s: %.1f MB in use, %.1f MB at most, of %u MB\n", when,
           used / 1048576.0, peak / 1048576.0, (unsigned)(sReserved >> 20));
    fflush(stdout);
}
