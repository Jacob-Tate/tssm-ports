#ifndef IANIM_H
#define IANIM_H

#include "xMath3.h"

// Opaque raw animation data (an incomplete type in the PS2 DWARF too).
struct iAnimFile;

// Bone limit of an animation pose: a pose in the work buffer is IANIM_MAXBONES quats followed by
// IANIM_MAXBONES translations (xAnimFileEval: translations at quats + 0x410 = 65 * sizeof(xQuat);
// xCutscene allocates 65 bone matrices per fake model).
#define IANIM_MAXBONES 65

extern U8* giAnimScratch;
extern U8* g_iAnim_workBuffer;

void iAnimInit();
F32 iAnimDuration(void* RawData);
U32 iAnimBoneCount(void* RawData);
void iAnimBlend(F32 BlendFactor, F32 BlendRecip, U16* BlendTimeOffset, F32* BoneTable,
                U32 BoneCount, xVec3* Tran1, xQuat* Quat1, xVec3* Tran2, xQuat* Quat2,
                xVec3* TranDest, xQuat* QuatDest);
void iAnimEval(void* RawData, float time, unsigned int flags, class xVec3* tran, class xQuat* quat);

#endif
