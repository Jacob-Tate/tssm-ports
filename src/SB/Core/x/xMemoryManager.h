#ifndef XMEMORYMANAGER_H
#define XMEMORYMANAGER_H

#include <types.h>
#include "xMemory.h"

struct xMemoryManager;

// GC layout (target asm + Ratatouille GC DWARF): one header magic word and eight trailer
// magic words. The PS2 DWARF has magic[5] / magic[4]; the total size (0x40) is the same.
struct DebugAllocationHeader
{
    static const U32 MAGIC = 0xDEADBEEF;
    static const U32 NUM_MAGIC = 1;

    char* file;
    S32 line;
    char* function;
    U32 size;
    DebugAllocationHeader* prev;
    DebugAllocationHeader* next;
    xMemoryManager* manager;
    U32 magic[NUM_MAGIC];
};

struct DebugAllocationTrailer
{
    static const U32 MAGIC = 0x31173D0D;
    static const U32 NUM_MAGIC = 8;

    U32 magic[NUM_MAGIC];
};

// The vtable pointer is at offset 0 (the virtuals are declared before the data).
// Virtual order must match __vt__14xMemoryManager.
struct xMemoryManager
{
    virtual void* DoAllocate(U32 size, U32 options) = 0;
    virtual void DoFree(void* pointer) = 0;
    virtual void* DoReallocate(void* pointer, U32 size, U32 options);
    virtual U32 DoGetBlockSize(void* pointer) const = 0;
    virtual void HandleOutOfMemory(U32 size, U32 options);

    void* arenaStart;
    void* arenaEnd;
    U32 size;
    char* lastFile;
    char* lastFunction;
    S32 lastLine;
    U32 debugDataSize;
    DebugAllocationHeader* activeList;

    bool IsDebugging() const
    {
        return debugDataSize != 0;
    }

    void* Allocate(U32 size, U32 options);
    void Free(void* pointer);
    void* Reallocate(void* pointer, U32 size, U32 options);
    U32 GetBlockSize(void* pointer) const;
    void DumpActiveList() const; // dead-stripped in retail (Ratatouille DWARF)
    void DoInit(void* start, U32 size, bool debugging);

    // No retail symbols: inline, expanded into Allocate, Free and Reallocate.
    void* SetupDebugBlock(void* memory, U32 size, char* file, char* function, S32 line)
    {
        DebugAllocationHeader* header = (DebugAllocationHeader*)memory;
        DebugAllocationTrailer* trailer = xMEMADVANCE((DebugAllocationTrailer*)(header + 1), size);

        header->file = file;
        header->function = function;
        header->line = line;
        header->size = size;
        header->next = activeList;
        header->prev = NULL;
        header->manager = this;

        if (activeList)
        {
            activeList->prev = header;
        }

        activeList = header;

        for (S32 i = 0; i < DebugAllocationHeader::NUM_MAGIC; i++)
        {
            header->magic[i] = DebugAllocationHeader::MAGIC;
        }

        for (S32 i = 0; i < DebugAllocationTrailer::NUM_MAGIC; i++)
        {
            trailer->magic[i] = DebugAllocationTrailer::MAGIC;
        }

        return header + 1;
    }

    void* RemoveDebugBlock(void* memory, U32* size)
    {
        DebugAllocationHeader* header = (DebugAllocationHeader*)memory - 1;

        if (header->prev)
        {
            header->prev->next = header->next;
        }
        else
        {
            activeList = header->next;
        }

        if (header->next)
        {
            header->next->prev = header->prev;
        }

        if (size)
        {
            *size = header->size + debugDataSize;
        }

        return header;
    }
};

struct xMemoryManagerFixed : xMemoryManager
{
    struct FixedHeader
    {
        FixedHeader* next;
    };

    FixedHeader* freeList;
    U32 elements;
    U32 elementSize;

    void* DoAllocate(U32 size, U32 options);
    void DoFree(void* pointer);
    void* DoReallocate(void* pointer, U32 size, U32 options);
    U32 DoGetBlockSize(void* pointer) const;

    void Init(void* start, U32 elements, U32 elementSize);
    void InitMemory();
};

struct xMemoryManagerGeneral : xMemoryManager
{
    enum Dir
    {
        Up,
        Down
    };

    enum Strategy
    {
        FirstFit,
        BestFit
    };

    struct FreeNode
    {
        FreeNode* next;
        FreeNode* prev;
        U32 size;
        U32 pad[1];
    };

    struct AllocatedNode
    {
        U32 size;
        U32 pad[3];
    };

    FreeNode* freeStart;
    FreeNode* freeEnd;
    Dir dir;
    Strategy strategy;

    void* DoAllocate(U32 size, U32 options);
    void DoFree(void* pointer);
    void* DoReallocate(void* pointer, U32 size, U32 options);
    U32 DoGetBlockSize(void* pointer) const;

    AllocatedNode* ShortenNode(AllocatedNode* allocatedNode, U32 size);
    void* GetCurrentEnd() const;
    void Init(void* start, U32 size, Dir dir, Strategy strategy, bool debugging);

    FreeNode* NextBlockOfMemory(FreeNode* node) const
    {
        return (FreeNode*)(dir == Up ? (U8*)(node + 1) + node->size : (U8*)(node - 1) - node->size);
    }

    // Out of line in the PS2 DWARF; no GC retail symbol: inline, expanded into ShortenNode and DoFree.
    void MergeNodes(FreeNode* node)
    {
        if (node->prev && NextBlockOfMemory(node->prev) == node)
        {
            node->prev->size += node->size + sizeof(FreeNode);
            node->prev->next = node->next;
            if (node->next)
            {
                node->next->prev = node->prev;
            }
            else
            {
                freeEnd = node->prev;
            }
            node = node->prev;
        }

        if (node->next && NextBlockOfMemory(node) == node->next)
        {
            node->size += node->next->size + sizeof(FreeNode);
            node->next = node->next->next;
            if (node->next)
            {
                node->next->prev = node;
            }
            else
            {
                freeEnd = node;
            }
        }
    }
};

#endif
