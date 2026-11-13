// Memory below 4 GB on the Switch: the iHostReserveLow seam's Switch half
// (iHostSwitch.cpp passes straight through to here). In its own file, with
// libnx and nothing of the game's, so the hardware spike's findings change
// this file and no other.
//
// The game keeps pointers in 32-bit fields, so its arena (iMemMgr.cpp) and
// RenderWare's heap (iLowHeap.cpp) have to sit below 4 GB. Horizon has no mmap
// and puts the heap where it likes; under Atmosphere's default 39-bit layout
// for homebrew, the heap, alias and stack regions are all placed by the kernel
// and nothing puts them low (docs/SWITCH.md, "Memory below 4 GB").
//
// The route that remains is the one hbloader itself uses for every .nro's
// .data and .bss: svcMapProcessCodeMemory moves heap pages to an address in the
// code region, and svcSetProcessMemoryPermission makes them read/write. The
// code region covers the whole map in the 39-bit layout, so the address can be
// a free one below 4 GB that we choose. That choice is the unproven part.
//
// All of it is in SwitchMapLow below, ONE function, so the hardware spike's
// findings change one place. Until then the order is:
//   1. the code-memory route at a free address below 4 GB;
//   2. if the stack region happens to be low (the 36-bit layout, which a user
//      can ask Atmosphere for), virtmemFindStack + svcMapMemory, as libnx maps
//      thread stacks;
//   3. a plain heap block, which iMemInit / iLowHeapInit then refuse because it
//      is above 4 GB -- a clear message instead of corrupt pointers.
//
// Memory here is real from the moment it is mapped (unlike PC and Android,
// where untouched reserved pages cost nothing), so the pools are sized down on
// Switch (iLowHeap.cpp).

#include "iSwitch.h"

#include <switch.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const uint64_t kPage = 0x1000;
    const uint64_t kLow4G = 0x100000000ULL;

    // The lowest address tried. Below it is the null guard and, in the 36-bit
    // layout, the start of the small map region; nothing the game wants.
    const uint64_t kLowStart = 0x08000000ULL;

    enum LowKind
    {
        LOW_NONE,
        LOW_CODE, // svcMapProcessCodeMemory
        LOW_STACK, // svcMapMemory into the stack region
        LOW_HEAP // plain heap; not actually low
    };

    struct LowMapping
    {
        void* addr;
        void* src;
        uint64_t size;
        LowKind kind;
        VirtmemReservation* rv;
    };

    const int kMaxLow = 8;
    LowMapping sLow[kMaxLow];

    uint64_t AlignUp(uint64_t v, uint64_t a)
    {
        return (v + a - 1) & ~(a - 1);
    }

    bool Overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
    {
        return bn != 0 && a < b + bn && b < a + an;
    }

    struct Regions
    {
        uint64_t heap, heapSize;
        uint64_t alias, aliasSize;
        uint64_t stack, stackSize;
        uint64_t aslr, aslrSize;
    };

    void ReadRegions(Regions* r)
    {
        memset(r, 0, sizeof(*r));
        svcGetInfo(&r->heap, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->heapSize, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->alias, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->aliasSize, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->stack, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->stackSize, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->aslr, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&r->aslrSize, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
    }

    // A free range of `size` bytes below 4 GB, inside the code (ASLR) region
    // and outside the heap, alias and stack regions. Walks the address space
    // with svcQueryMemory. 0 if none. Call with virtmemLock held, so libnx does
    // not hand the same range to a thread stack meanwhile.
    uint64_t FindFreeLow(uint64_t size, const Regions& r)
    {
        uint64_t lo = kLowStart;
        if (r.aslrSize != 0 && lo < r.aslr)
        {
            lo = AlignUp(r.aslr, kPage);
        }
        uint64_t hi = kLow4G;
        if (r.aslrSize != 0 && r.aslr + r.aslrSize < hi)
        {
            hi = r.aslr + r.aslrSize;
        }

        // A guard page either side, so a stray write off the end of one pool
        // faults instead of landing in the next mapping.
        const uint64_t guard = kPage;
        uint64_t addr = lo;
        while (addr + size + 2 * guard <= hi)
        {
            MemoryInfo info;
            u32 pageInfo = 0;
            if (R_FAILED(svcQueryMemory(&info, &pageInfo, addr)))
            {
                return 0;
            }
            const uint64_t blockEnd = info.addr + info.size;
            if (blockEnd <= addr)
            {
                return 0; // the top of the address space
            }
            if (info.type != MemType_Unmapped)
            {
                addr = AlignUp(blockEnd, kPage);
                continue;
            }

            // A free block: is there room in it, clear of the regions?
            uint64_t start = AlignUp(addr > info.addr ? addr : info.addr, kPage) + guard;
            uint64_t end = blockEnd < hi ? blockEnd : hi;
            bool moved = true;
            while (moved)
            {
                moved = false;
                const uint64_t bases[3] = { r.heap, r.alias, r.stack };
                const uint64_t sizes[3] = { r.heapSize, r.aliasSize, r.stackSize };
                for (int i = 0; i < 3; i++)
                {
                    if (Overlaps(start - guard, size + 2 * guard, bases[i], sizes[i]))
                    {
                        start = AlignUp(bases[i] + sizes[i], kPage) + guard;
                        moved = true;
                    }
                }
            }
            if (start + size + guard <= end)
            {
                return start;
            }
            addr = AlignUp(blockEnd, kPage);
        }
        return 0;
    }

    // THE function the hardware spike adjusts. Maps `size` bytes (a multiple of
    // the page size) read/write below 4 GB; fills `out` and returns true, or
    // returns false having changed nothing.
    bool SwitchMapLow(uint64_t size, LowMapping* out)
    {
        Regions r;
        ReadRegions(&r);

        void* src = aligned_alloc(kPage, size);
        if (src == NULL)
        {
            printf("tssm: switch: low memory: no %llu MB of heap to move below 4 GB\n",
                   (unsigned long long)(size >> 20));
            return false;
        }

        // 1. Code memory at an address we choose.
        virtmemLock();
        uint64_t dst = FindFreeLow(size, r);
        if (dst != 0)
        {
            const Handle self = envGetOwnProcessHandle();
            Result rc = svcMapProcessCodeMemory(self, dst, (uint64_t)(uintptr_t)src, size);
            if (R_SUCCEEDED(rc))
            {
                rc = svcSetProcessMemoryPermission(self, dst, size, Perm_Rw);
                if (R_SUCCEEDED(rc))
                {
                    out->rv = virtmemAddReservation((void*)(uintptr_t)dst, size);
                    virtmemUnlock();
                    out->addr = (void*)(uintptr_t)dst;
                    out->src = src;
                    out->size = size;
                    out->kind = LOW_CODE;
                    printf("tssm: switch: low memory: %llu MB at %#llx (code memory)\n",
                           (unsigned long long)(size >> 20), (unsigned long long)dst);
                    return true;
                }
                printf("tssm: switch: low memory: svcSetProcessMemoryPermission(%#llx) failed: "
                       "%#x\n",
                       (unsigned long long)dst, rc);
                svcUnmapProcessCodeMemory(self, dst, (uint64_t)(uintptr_t)src, size);
            }
            else
            {
                printf("tssm: switch: low memory: svcMapProcessCodeMemory(%#llx, %llu MB) failed: "
                       "%#x\n",
                       (unsigned long long)dst, (unsigned long long)(size >> 20), rc);
            }
        }
        else
        {
            printf("tssm: switch: low memory: no free %llu MB range below 4 GB in the code "
                   "region\n",
                   (unsigned long long)(size >> 20));
        }

        // 2. The stack region, when it is low (36-bit layout).
        if (r.stackSize != 0 && r.stack + r.stackSize <= kLow4G)
        {
            void* sdst = virtmemFindStack(size, kPage);
            if (sdst != NULL)
            {
                Result rc = svcMapMemory(sdst, src, size);
                if (R_SUCCEEDED(rc))
                {
                    out->rv = virtmemAddReservation(sdst, size);
                    virtmemUnlock();
                    out->addr = sdst;
                    out->src = src;
                    out->size = size;
                    out->kind = LOW_STACK;
                    printf("tssm: switch: low memory: %llu MB at %p (stack region)\n",
                           (unsigned long long)(size >> 20), sdst);
                    return true;
                }
                printf("tssm: switch: low memory: svcMapMemory(%p) failed: %#x\n", sdst, rc);
            }
        }
        virtmemUnlock();

        free(src);
        return false;
    }

    LowMapping* FreeSlot()
    {
        for (int i = 0; i < kMaxLow; i++)
        {
            if (sLow[i].kind == LOW_NONE)
            {
                return &sLow[i];
            }
        }
        return NULL;
    }
} // namespace

void* iSwitchReserveLow(uint64_t size)
{
    const uint64_t want = AlignUp(size, kPage);
    LowMapping* slot = FreeSlot();
    if (slot == NULL)
    {
        return NULL;
    }

    static bool sReported;
    if (!sReported)
    {
        sReported = true;
        Regions r;
        ReadRegions(&r);
        printf("tssm: switch: address space %#llx+%#llx; heap %#llx+%#llx, alias %#llx+%#llx, "
               "stack %#llx+%#llx\n",
               (unsigned long long)r.aslr, (unsigned long long)r.aslrSize,
               (unsigned long long)r.heap, (unsigned long long)r.heapSize,
               (unsigned long long)r.alias, (unsigned long long)r.aliasSize,
               (unsigned long long)r.stack, (unsigned long long)r.stackSize);
    }

    if (SwitchMapLow(want, slot))
    {
        memset(slot->addr, 0, (size_t)want);
        return slot->addr;
    }

    // 3. Whatever the heap gives, which the callers refuse as above 4 GB.
    void* p = aligned_alloc(kPage, (size_t)want);
    if (p == NULL)
    {
        return NULL;
    }
    memset(p, 0, (size_t)want);
    slot->addr = p;
    slot->src = NULL;
    slot->size = want;
    slot->kind = LOW_HEAP;
    slot->rv = NULL;
    return p;
}

void iSwitchReleaseLow(void* p)
{
    if (p == NULL)
    {
        return;
    }

    for (int i = 0; i < kMaxLow; i++)
    {
        LowMapping* m = &sLow[i];
        if (m->kind == LOW_NONE || m->addr != p)
        {
            continue;
        }

        const uint64_t addr = (uint64_t)(uintptr_t)m->addr;
        const uint64_t src = (uint64_t)(uintptr_t)m->src;
        bool undone = true;
        if (m->kind == LOW_CODE)
        {
            // TODO(switch): confirm on hardware that code memory made
            // read/write can be unmapped as it is; if the kernel wants it back
            // at its original permission first, set that here.
            undone = R_SUCCEEDED(
                svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), addr, src, m->size));
        }
        else if (m->kind == LOW_STACK)
        {
            undone = R_SUCCEEDED(svcUnmapMemory(m->addr, m->src, m->size));
        }

        if (m->rv != NULL)
        {
            virtmemLock();
            virtmemRemoveReservation(m->rv);
            virtmemUnlock();
        }

        if (m->kind == LOW_HEAP)
        {
            free(m->addr);
        }
        else if (undone)
        {
            free(m->src);
        }
        // else: the heap pages are still mapped elsewhere; leak them rather
        // than hand the allocator memory it cannot touch.

        memset(m, 0, sizeof(*m));
        return;
    }
}
