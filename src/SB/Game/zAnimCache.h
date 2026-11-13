#ifndef ZANIMCACHE_H
#define ZANIMCACHE_H

#include <types.h>

#include "xVec3.h"
#include "xAnim.h"

struct zAnimCacheEntry
{
    xVec3* data;
    xAnimFile* animFile;
    U16 bone;
};

#endif
