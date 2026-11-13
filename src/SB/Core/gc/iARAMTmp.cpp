#include "iARAMTmp.h"

#include "iDebug.h"
#include "iSystem.h"
#include "iTime.h"
#include "iTRC.h"
#include "xpkrsvc.h"
#include "xstransvc.h"

#include <dolphin/ar.h>
#include <dolphin/os/OSCache.h>
#include <string.h>

// ARAM temp memory: the main-RAM copies of every asset of one type are saved to ARAM,
// which frees their main-RAM range for temporary allocations (iARAMTmpMalloc) until
// iARAMTmpEnd copies the assets back.

// Struct and member names from the Ratatouille GC DWARF.
struct memBlock
{
    U32 base; // main-RAM address of the block (32-byte aligned)
    U32 size;
    // Temp-alloc cursor; nonzero while the block's data lives in ARAM. Volatile: transferDoneCB
    // writes it from the ARQ interrupt while Begin/End poll it.
    volatile U32 addr;
    void* aram; // ARAM copy
    ARQRequest xfer;
};

static void transferDoneCB(u32 pointerToARQRequest);

static memBlock blockList[32];
static U32 numBlocks;

static void transferDoneCB(u32 pointerToARQRequest)
{
    memBlock* blk = (memBlock*)((ARQRequest*)pointerToARQRequest)->owner;

    if (blk->addr == 0)
    {
        // Saved to ARAM: the main-RAM range is now free for temp allocations.
        blk->addr = (blk->base + 31) & ~31;
    }
    else
    {
        // Restored from ARAM.
        blk->addr = 0;
    }
}

U32 iARAMTmpBegin(U32 type, U32 max)
{
    iTimeGet();
    ARQInit();

    memset(blockList, 0, sizeof(blockList));
    numBlocks = 0;

    S32 num = xSTAssetCountByType(type);
    S32 total = 0;
    S32 i;
    S32 j;
    S32 totalSize = 0;

    for (i = 0; i < num; i++)
    {
        PKRAssetTOCInfo ainfo;
        xSTGetAssetInfoByType(type, i, &ainfo);

        U32 base = (U32)ainfo.mempos;
        U32 alignedBase = base & ~31;
        U32 size = ainfo.size + (base - alignedBase);
        size = (size + 31) & ~31;
        if (totalSize + size > max)
        {
            break;
        }

        totalSize += size;

        // Merge with an existing block that touches or overlaps this range (newest first).
        memBlock* bp = &blockList[numBlocks];
        j = numBlocks;
        while (bp--, j--)
        {
            if (alignedBase > bp->base && alignedBase - (bp->base + bp->size) < 16)
            {
                bp->size = alignedBase + size - bp->base;
                break;
            }

            if (alignedBase > bp->base && alignedBase < bp->base + bp->size)
            {
                bp->size = alignedBase + size - bp->base;
                break;
            }

            if (bp->base > alignedBase && bp->base - (alignedBase + size) < 16)
            {
                bp->size = bp->base + bp->size - alignedBase;
                bp->base = alignedBase;
                break;
            }

            if (bp->base > alignedBase && bp->base < alignedBase + size)
            {
                bp->size = alignedBase + size - bp->base;
                break;
            }
        }

        if (j < 0)
        {
            blockList[numBlocks].base = alignedBase;
            blockList[numBlocks].size = size;
            numBlocks++;
        }
    }

    memBlock* bp = blockList;
    for (i = 0; i < numBlocks; i++, bp++)
    {
        total += bp->size;

        S32 size = (bp->size + 31) & ~31;
        bp->aram = iSystemARAMAlloc(size);
        DCFlushRange((void*)bp->base, size);
        ARQPostRequest(&bp->xfer, (u32)bp, ARQ_TYPE_MRAM_TO_ARAM, ARQ_PRIORITY_HIGH, bp->base,
                       (u32)bp->aram, size, transferDoneCB);

        while (bp->addr == 0)
        {
            iTRCDisk::TRCCheck();
        }
    }

    iTimeGet();
    return total;
}

void* iARAMTmpMalloc(U32 size)
{
    size = (size + 31) & ~31;

    memBlock* blk = blockList;
    for (U32 i = 0; i < numBlocks; i++, blk++)
    {
        if (blk->addr + size <= blk->base + blk->size)
        {
            // Retail reads the volatile blk->addr once more here and discards it: a compiled-out debug
            // statement that still evaluated its argument.
            iDEBUG_EVAL_PLACEHOLDER(blk->addr);

            void* mem = (void*)blk->addr;
            blk->addr += size;
            return mem;
        }
    }

    return IARAMTMP_NO_MEMORY;
}

void iARAMTmpFree(void* ptr)
{
}

S32 iARAMTmpInARAMTemp(void* ptr)
{
    memBlock* blk = blockList;
    for (U32 i = 0; i < numBlocks; i++, blk++)
    {
        if ((U32)ptr >= blk->base && (U32)ptr < blk->base + blk->size)
        {
            return 1;
        }
    }

    return 0;
}

void iARAMTmpEnd(bool wait)
{
    iTimeGet();

    memBlock* blk = blockList;
    for (U32 i = 0; i < numBlocks; i++, blk++)
    {
        DCInvalidateRange((void*)blk->base, blk->size);
        ARQPostRequest(&blk->xfer, (u32)blk, ARQ_TYPE_ARAM_TO_MRAM, ARQ_PRIORITY_HIGH,
                       (u32)blk->aram, blk->base, blk->size, transferDoneCB);

        while (blk->addr != 0)
        {
            if (wait)
            {
                iTRCDisk::TRCCheck();
            }
        }

        iSystemARAMFree(blk->aram);
    }

    iTimeGet();
}
