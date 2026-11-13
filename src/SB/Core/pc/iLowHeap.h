#ifndef ILOWHEAP_H
#define ILOWHEAP_H

#include <types.h>

#include <stddef.h>

// PC-only: RenderWare's heap, below 4 GB on 64-bit hosts.
//
// On the console RenderWare allocates from the game's own memory manager
// (gc/iSystem.cpp passes iMemMgrMalloc and friends to RwEngineInit), so every
// RenderWare pointer is a 32-bit arena address, and game code is written
// knowing that: it keeps RwMalloc results, RpAtomic and RwTexture pointers in
// U32 asset fields and rounds them through (U32) casts. PKR_getmem
// (xpkrsvc.cpp) is the first on the boot path; the RW-allocated read buffer for
// font.HIP was its first casualty on x64.
//
// The port gives RenderWare and librw the C library's heap instead, because
// librw allocates far more than the console's RenderWare did (images while a
// texture is converted, geometry and instance copies), and on a 64-bit host
// that heap hands out addresses above 4 GB. So on 64-bit hosts this is the heap
// RenderWare is started with: one block reserved below 4 GB with
// iHostReserveLow, next to the game arena, managed as a two-level segregated
// fit (TLSF) heap -- O(1) allocate and free, immediate coalescing, 16-byte
// alignment like the C library's. On 32-bit hosts every address fits already
// and RenderWare keeps the C library's heap.
//
// The size is 256 MB unless TSSM_RWHEAP_MB says otherwise. Measured: booting
// to the first level's gameplay peaks at 12 MB (x86, TSSM_RWHEAP=1). Committed
// up front as the game arena is; untouched pages cost no physical memory. Running out is
// reported once and answered NULL, which librw's must-allocate paths turn into
// an abort with the reason printed -- a fallback to the C library would bring
// back exactly the high pointers this exists to prevent.
//
// Thread-safe: one lock round every call.

// TRUE when RenderWare should use this heap: 64-bit hosts, or any host with
// TSSM_RWHEAP=1 (to exercise it in a 32-bit build).
S32 iLowHeapWanted();

// Reserves the heap. FALSE (with the reason printed) when no low block could
// be reserved; the caller decides whether that is fatal.
S32 iLowHeapInit();

void* iLowHeapMalloc(size_t size);
void* iLowHeapCalloc(size_t count, size_t size);
void* iLowHeapRealloc(void* mem, size_t size);
void iLowHeapFree(void* mem);

// Bytes in live blocks, headers included, now and at the most.
size_t iLowHeapUsed();
size_t iLowHeapPeak();

// One line to stdout: size, used, peak, the base address.
void iLowHeapReport(const char* when);

#endif
