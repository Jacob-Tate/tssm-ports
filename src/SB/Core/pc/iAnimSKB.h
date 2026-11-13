#ifndef IANIMSKB_H
#define IANIMSKB_H

#include "xMath3.h"

// copy-at-load: ExtractedData is a 4-byte pointer slot in the animation data on disk. xAnim and
// zEntPlayer store data pointers in it, and zEntPlayer and zNMETypeBossNeptune use its bit 31 as a
// flag. A host with 8-byte pointers must keep the slot 4 bytes or widen the header when the asset
// is loaded; widening at load misses cutscene time chunks, whose animations are evaluated in their
// stream buffer.
//
// PORT (adapted from gc/iAnimSKB.h): this is that file. ExtractedData is an iPtr32
// (compat/types.h), 4 bytes on every host. Animations are not only ANIM assets: the cutscene time
// chunks carry SKB1 animations that xCutscene evaluates in the stream buffer, where nothing can be
// widened. So the header keeps the disk layout, ANIM assets are read as they are, and a pointer
// above 4 GB stored here (zEntPlayer's ledge table, a static) goes through iPtr32's handle table.
struct iAnimSKBHeader
{
    U32 Magic;
    iPtr32<void> ExtractedData;
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
