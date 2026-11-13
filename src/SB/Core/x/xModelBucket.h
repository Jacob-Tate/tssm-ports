#ifndef XMODELBUCKET_H
#define XMODELBUCKET_H

#include "xModel.h"

struct xModelBucket
{
    RpAtomic* Data;
    RpAtomic* OriginalData;
    union
    {
        xModelInstance* List;
        xModelBucket** BackRef;
    };
    S32 ClipFlags;
    xModelPipe Pipe;
};

struct xModelAlphaBucket
{
    RpAtomic* Data;
    xModelInstance* MInst;
    F32 AlphaFade;
    F32 SortValue;
    U32 Layer;
};

extern S32 xModelBucketEnabled;

void xModelBucket_RenderAlpha();

void xModelBucket_PreCountReset();
void xModelBucket_PreCountBucket(RpAtomic* data, xModelPipe& pipe, U32 subObjects);
void xModelBucket_PreCountAlloc(S32 maxAlphaModels);
void FullAtomicDupe(RpAtomic* atomic, S32 count, RpAtomic** output);
void xModelBucket_InsertBucket(RpAtomic* data, xModelPipe& pipe, U32 subObjects);
void xModelBucket_Init();
void xModelBucket_Deinit();
xModelBucket** xModelBucket_GetBuckets(RpAtomic* data);
void xModelBucket_Begin();
void xModelBucket_Add(xModelInstance* minst);
void xModelBucket_AddAlphaFade(xModelInstance* minst, F32 alphaFade);
void xModelBucket_RenderOpaqueBegin();
void xModelBucket_RenderOpaque(S32 maxLayer);
void xModelBucket_RenderAlphaBegin();
void xModelBucket_RenderAlphaLayer(S32 maxLayer);
bool xModelBucket_RenderAny(S32 maxLayer);
void xModelBucket_RenderAlphaEnd();

#endif
