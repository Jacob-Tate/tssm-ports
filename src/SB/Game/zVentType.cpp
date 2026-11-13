#include "zVentType.h"

#include <string.h>

#include "zBase.h"
#include "zScene.h"

void zVentType::load(xBase& data, xDynAsset& asset, size_t)
{
    ((zVentType&)data).load((const asset_type&)asset);
}

// No GC symbol: inlined into load(xBase&, xDynAsset&, size_t) (PS2 demo has it out of line).
void zVentType::load(const asset_type& a)
{
    memset(this, 0, sizeof(zVentType));
    xBaseInit(this, &a);

    baseType = eBaseTypeVentType;
    asset = &a;

    if (linkCount)
    {
        link = (xLinkAsset*)(&a + 1);
    }

    if (asset->idleEmitter)
    {
        idleEmitter = (xParEmitter*)zSceneFindObject(asset->idleEmitter);
    }

    if (asset->warnEmitter)
    {
        warnEmitter = (xParEmitter*)zSceneFindObject(asset->warnEmitter);
    }

    if (asset->damageEmitter)
    {
        damageEmitter = (xParEmitter*)zSceneFindObject(asset->damageEmitter);
    }
}
