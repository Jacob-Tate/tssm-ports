#include "zAnimCache.h"

// The functions of this file are dead-stripped from retail; only the .bss arrays survive.
namespace
{
    U32 cachedAnimations;
    zAnimCacheEntry cachedEntry[32];
    U32 currentCacheIndex;
    xVec3 cache[2560]; // 0x7800 on GC (the PS2 demo DWARF has 5120)
} // namespace
