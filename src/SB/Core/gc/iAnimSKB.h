#ifndef IANIMSKB_H
#define IANIMSKB_H

#include "xMath3.h"

// copy-at-load: ExtractedData is a 4-byte pointer slot in the animation data on disk; xAnim and
// zEntPlayer store pointers in it and bit 31 is used as a flag. A 64-bit host must keep the slot
// 4 bytes or widen the header at load (which misses cutscene animations, evaluated in their stream
// buffer).
struct iAnimSKBHeader
{
    U32 Magic;
    void* ExtractedData;
    U16 BoneCount;
    U16 TimeCount;
    U32 KeyCount;
    F32 Scale[3];
};

struct iAnimSKBKey
{
    U16 TimeIndex;
    S16 Quat[4];
    S16 Tran[3];
};

void iAnimEvalSKB(iAnimSKBHeader* data, F32 time, U32 flags, xVec3* tran, xQuat* quat);
void _iAnimSKBAdjustTranslate(iAnimSKBHeader* data, U32 bone, F32* starttran, F32* endtran);
S32 _iAnimSKBNumExtract(iAnimSKBHeader* data, U32 bone);
S32 _iAnimSKBExtractTranslate(iAnimSKBHeader* data, U32 bone, xVec3* tranArray, S32 tranCount);
S32 _iAnimSKBExtractTranslateAxis(iAnimSKBHeader* data, U32 bone, xVec3* tranArray, S32 tranCount,
                                  U8* axis);
F32 QuatToYaw(xQuat* q);
S32 _iAnimSKBExtractYaw(iAnimSKBHeader* data, U32 bone, F32* yawArray, S32 yawCount);
void _iAnimSKBAdjustYaw(iAnimSKBHeader* data, U32 bone, F32 yawStart, F32 yawEnd);
// No GC symbol (always inlined into iAnimDuration).
inline F32 iAnimDurationSKB(iAnimSKBHeader* data)
{
    return ((F32*)((iAnimSKBKey*)(data + 1) + data->KeyCount))[data->TimeCount - 1];
}

#endif
