#include "xTextAsset.h"
#include "xString.h"
#include "xstransvc.h"

char* xTextFindString(U32 key, U32* len)
{
    U32 size;
    xTextAsset* asset;

    if (key == 0)
    {
        return NULL;
    }

    asset = (xTextAsset*)xSTFindAsset(key, &size);
    if (asset == NULL)
    {
        return NULL;
    }

    if (len != NULL)
    {
        *len = asset->len;
    }

    return xTextAssetGetText(asset);
}

char* xTextFindString(const char* key, U32* len)
{
    return xTextFindString(xStrHash(key), len);
}
