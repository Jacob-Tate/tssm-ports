#ifndef TSSM_PC_COMPAT_TYPES_H
#define TSSM_PC_COMPAT_TYPES_H

// The PC build's types.h: include/types.h, plus the host-only helpers the port
// layer uses.
//
// Game code includes "types.h" unqualified and this directory comes first on
// the PC include path, so this file is what every PC translation unit gets; it
// takes the shared include/types.h with include_next and adds to it.
//
// include/types.h has host branches (fixed-width
// types from <stdint.h>, the host size_t, no __declspec stub, a real WEAK,
// WAD_STATIC, nullptr and UINT32_MAX left to the host). Before that this file
// replaced it outright. What is left here is what only the port needs, after
// treedome's include/types.h:
//
//   UPtr / SPtr     (now in include/types.h) an integer wide enough for a
//                   pointer. The game keeps pointers in U32s; a round trip
//                   through UPtr keeps the value, and game-allocator pointers
//                   still fit a U32 because iMemInit reserves the arena below
//                   4 GB.
//   TSSM_PTR64      set on 64-bit hosts, for code that must walk an asset
//                   laid out with 4-byte pointer slots differently.
//   TSSM_STATIC_ASSERT
//   SHARED_INLINE   treedome's spelling for a function declared in a header
//                   and defined `inline` in one .cpp that others call.
//
// Still to do in the shared header, not here: include/types.h types s32/u32
// as long on host builds too, which is 64-bit on LP64 (Linux, Android, macOS).
// tools/pcimport.py --check names this file when include/types.h changes.

#include_next <types.h>

#include <stdint.h>

// UPtr and SPtr themselves are in include/types.h
// (uintptr_t/intptr_t on hosts, U32/S32 on the console).

#if defined(_WIN64) || defined(__LP64__) || defined(_LP64)
#define TSSM_PTR64 1
#endif

#define TSSM_STATIC_ASSERT(cond, msg) static_assert(cond, msg)

#if defined(__MINGW32__)
#define SHARED_INLINE WEAK
#else
#define SHARED_INLINE WEAK inline
#endif

#ifdef __cplusplus
// iPtr32<T>: a pointer kept in a 4-byte slot, for a structure whose layout is
// fixed by data the game reads in place and which the game also uses to hold
// a pointer: iAnimSKBHeader::ExtractedData (animations, also inside cutscene
// time chunks) and xCutsceneData::DataPtr (cutscene headers and time chunks).
// It reads and writes like a T*. On a 32-bit host it is the pointer itself.
//
// On a 64-bit host a pointer below 4 GB is stored as it is: everything the
// game arena (iMemMgr.cpp) and RenderWare's heap (iLowHeap.h) hand out. One
// above 4 GB -- a static of the executable, such as the player's ledge-grab
// table zEntPlayer.cpp keeps in an animation -- is entered in a small table
// (iAssetHooks.cpp) and the slot holds its handle, a value in the first 64 KB
// of the address space, which no host maps. The game's flag bit 31 (set and
// cleared on the raw value by zEntPlayer and zNMETypeBossNeptune) survives
// either way; `(UPtr)slot` gives that raw value.
#ifdef TSSM_PTR64
#define IPTR32_FAR_BASE 0x1000u
#define IPTR32_FAR_STEP 16u
#define IPTR32_FAR_MAX 256u
U32 iPtr32Far(const void* p);
void* iPtr32FarGet(U32 handle);
#endif

inline U32 iPtr32Pack(const void* p)
{
#ifdef TSSM_PTR64
    if ((UPtr)p >> 32)
    {
        return iPtr32Far(p);
    }
#endif
    return (U32)(UPtr)p;
}

inline void* iPtr32Unpack(U32 bits)
{
#ifdef TSSM_PTR64
    if (bits - IPTR32_FAR_BASE < IPTR32_FAR_STEP * IPTR32_FAR_MAX)
    {
        return iPtr32FarGet(bits);
    }
#endif
    return (void*)(UPtr)bits;
}

template <class T>
struct iPtr32
{
    U32 bits;

    iPtr32& operator=(T* p)
    {
        bits = iPtr32Pack(p);
        return *this;
    }

    operator T*() const
    {
        return (T*)iPtr32Unpack(bits);
    }

    // (RpAtomic*)data->DataPtr
    template <class U>
    explicit operator U*() const
    {
        return (U*)iPtr32Unpack(bits);
    }

    // (UPtr)skb->ExtractedData: the raw slot, flag bits and all
    explicit operator UPtr() const
    {
        return bits;
    }
};
#endif

#endif // !TSSM_PC_COMPAT_TYPES_H
