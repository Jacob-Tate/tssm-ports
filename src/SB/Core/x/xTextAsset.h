#ifndef XTEXTASSET_H
#define XTEXTASSET_H

#include <types.h>

struct xTextAsset
{
    U32 len;
};

#define xTextAssetGetText(t) ((char*)((xTextAsset*)(t) + 1))

char* xTextFindString(const char* key, U32* len);
char* xTextFindString(U32 key, U32* len);

#endif
