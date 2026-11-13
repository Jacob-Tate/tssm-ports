#ifndef ZREFERENCE_H
#define ZREFERENCE_H

#include "xBase.h"
#include "xDynAsset.h"

struct zReference : xBase
{
    xBase* current;
};

struct zReferenceAsset : xDynAsset
{
    U32 initial;
};

void zReferenceInit(xBase& data, xDynAsset& asset, size_t);

#endif
