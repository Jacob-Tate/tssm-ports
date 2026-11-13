#include "iARAMTmp.h"

#include "xpkrsvc.h"
#include "xstransvc.h"

#include <stdlib.h>
#include <string.h>

// PC port of gc/iARAMTmp.cpp: "ARAM temp memory".
//
// On the console, iARAMTmpBegin copies the main-RAM bytes of every asset of one
// type out to ARAM (the GameCube's 16 MB of audio RAM, which the CPU cannot
// address), which frees that main-RAM range for temporary allocations
// (iARAMTmpMalloc) until iARAMTmpEnd copies the assets back over it. iFMV uses
// it for the movie player's buffers.
//
// The algorithm is retail's, unchanged: the same block list, the same
// merging of neighbouring assets into one block, the same bump allocation.
// What changes is the far side. ARAM is a host buffer from malloc, and the
// ARQ DMA (asynchronous, finished in transferDoneCB) is a memcpy that has
// finished when it returns, so the polling loops around it are gone.
// Addresses are UPtr rather than U32.

struct memBlock
{
    UPtr base; // main-RAM address of the block (32-byte aligned)
    U32 size;
    UPtr addr; // temp-alloc cursor; nonzero while the block's data is saved away
    void* aram; // the saved copy
};

static memBlock blockList[32];
static U32 numBlocks;

U32 iARAMTmpBegin(U32 type, U32 max)
{
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

        UPtr base = (UPtr)ainfo.mempos;
        UPtr alignedBase = base & ~(UPtr)31;
        U32 size = ainfo.size + (U32)(base - alignedBase);
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
                bp->size = (U32)(alignedBase + size - bp->base);
                break;
            }

            if (alignedBase > bp->base && alignedBase < bp->base + bp->size)
            {
                bp->size = (U32)(alignedBase + size - bp->base);
                break;
            }

            if (bp->base > alignedBase && bp->base - (alignedBase + size) < 16)
            {
                bp->size = (U32)(bp->base + bp->size - alignedBase);
                bp->base = alignedBase;
                break;
            }

            if (bp->base > alignedBase && bp->base < alignedBase + size)
            {
                bp->size = (U32)(alignedBase + size - bp->base);
                break;
            }
        }

        if (j < 0 && numBlocks < sizeof(blockList) / sizeof(blockList[0]))
        {
            blockList[numBlocks].base = alignedBase;
            blockList[numBlocks].size = size;
            numBlocks++;
        }
    }

    memBlock* bp = blockList;
    for (i = 0; i < (S32)numBlocks; i++, bp++)
    {
        total += bp->size;

        U32 size = (bp->size + 31) & ~31;
        bp->aram = malloc(size);
        if (bp->aram == NULL)
        {
            // Nothing saved, so nothing may be handed out over it.
            bp->size = 0;
            continue;
        }
        memcpy(bp->aram, (void*)bp->base, size);
        bp->addr = (bp->base + 31) & ~(UPtr)31;
    }

    return total;
}

void* iARAMTmpMalloc(U32 size)
{
    size = (size + 31) & ~31;

    memBlock* blk = blockList;
    for (U32 i = 0; i < numBlocks; i++, blk++)
    {
        if (blk->addr != 0 && blk->addr + size <= blk->base + blk->size)
        {
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
        if ((UPtr)ptr >= blk->base && (UPtr)ptr < blk->base + blk->size)
        {
            return 1;
        }
    }

    return 0;
}

void iARAMTmpEnd(bool wait)
{
    memBlock* blk = blockList;
    for (U32 i = 0; i < numBlocks; i++, blk++)
    {
        if (blk->aram != NULL)
        {
            memcpy((void*)blk->base, blk->aram, blk->size);
            free(blk->aram);
            blk->aram = NULL;
        }
        blk->addr = 0;
    }

    (void)wait;
}
