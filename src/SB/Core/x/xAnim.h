#ifndef XANIM_H
#define XANIM_H

#include <types.h>

#include "xMath3.h"
#include "xMemMgr.h"
#include "iSnd.h"

typedef struct xAnimState;
typedef struct xAnimTransition;
typedef struct xAnimTransitionList;
typedef struct xAnimEffect;
typedef struct xAnimActiveEffect;
typedef struct xAnimSingle;
typedef struct xAnimPlay;

extern U32 gxAnimUseGrowAlloc;

struct xAnimPhysicsData
{
    xVec3* tranTable;
    F32* yawTable;
    S32 tranCount;
};

struct xAnimFile
{
    xAnimFile* Next;
    const char* Name;
    U32 ID;
    U32 FileFlags;

    // 0x10
    F32 Duration;
    F32 TimeOffset;
    U16 BoneCount;
    U8 NumAnims[2];
    void** RawData; // 0x1c
    xAnimPhysicsData* PhysicsData;
};

struct xAnimMultiFileEntry
{
    U32 ID;
    xAnimFile* File;
};

struct xAnimMultiFileBase
{
    U32 Count;
};

struct xAnimMultiFile : xAnimMultiFileBase
{
    xAnimMultiFileEntry Files[1];
};

typedef void (*xAnimStateBeforeEnterCallback)(xAnimPlay*, xAnimState*, void*);
typedef void (*xAnimStateCallback)(xAnimState*, xAnimSingle*, void*);
typedef void (*xAnimStateBeforeAnimMatricesCallback)(xAnimPlay*, xQuat*, xVec3*, S32);

struct xAnimState
{
    xAnimState* Next;
    const char* Name;
    U32 ID;
    U32 Flags;

    // 0x10
    U32 UserFlags;
    F32 Speed;
    xAnimFile* Data;
    xAnimEffect* Effects;

    // 0x20
    xAnimTransitionList* Default;
    xAnimTransitionList* List;
    F32* BoneBlend;
    F32* TimeSnap;

    // 0x30
    F32 FadeRecip;
    U16* FadeOffset;
    void* CallbackData;
    xAnimMultiFile* MultiFile;

    // 0x40
    xAnimStateBeforeEnterCallback BeforeEnter;
    xAnimStateCallback StateCallback;
    xAnimStateBeforeAnimMatricesCallback BeforeAnimMatrices;
};

typedef U32 (*xAnimTransitionConditionalCallback)(xAnimTransition*, xAnimSingle*, void*);
typedef U32 (*xAnimTransitionCallback)(xAnimTransition*, xAnimSingle*, void*);

struct xAnimTransition
{
    xAnimTransition* Next;
    xAnimState* Dest;
    xAnimTransitionConditionalCallback Conditional;
    xAnimTransitionCallback Callback;
    U32 Flags;
    U32 UserFlags;
    F32 SrcTime; // 0x18
    F32 DestTime; // 0x1C
    U16 Priority;
    U16 QueuePriority;
    F32 BlendRecip; // 0x24
    U16* BlendOffset;
};

struct xAnimTransitionList
{
    xAnimTransitionList* Next;
    xAnimTransition* T;
};

struct xAnimTable
{
    const char* Name; // 0x0
    xAnimTransition* TransitionList; // 0x4
    xAnimState* StateList; // 0x8
    U32 AnimIndex;
    U32 MorphIndex;
    U32 UserFlags;
};

typedef U32 (*xAnimEffectCallback)(U32, xAnimActiveEffect*, xAnimSingle*, void*);

struct xAnimEffect
{
    xAnimEffect* Next;
    U16 Flags;
    U16 Probability;
    F32 StartTime;
    F32 EndTime;
    xAnimEffectCallback Callback;
};

struct xAnimActiveEffect
{
    xAnimEffect* Effect;
    union
    {
        U32 Handle;
        iSndHandle SndHandle;
    };
};

struct xAnimSingle
{
    U32 SingleFlags;
    xAnimState* State;
    F32 Time;
    F32 CurrentSpeed;

    // Offset: 0x10
    F32 BilinearLerp[2];
    xAnimEffect* Effect;
    U32 ActiveCount;

    // Offset: 0x20
    F32 LastTime;
    xAnimActiveEffect* ActiveList;
    xAnimPlay* Play;
    xAnimTransition* Sync;

    // Offset: 0x30
    xAnimTransition* Tran;
    xAnimSingle* Blend;
    F32 BlendFactor;
    xVec3 PhysDisp;
    F32 YawDisp;
    U32 pad[1];
};

typedef struct xModelInstance;

struct xAnimPlay
{
    xAnimPlay* Next;
    U16 NumSingle; // 0x4
    U16 BoneCount; // 0x6
    xAnimSingle* Single; // 0x8
    void* Object; // 0xC
    xAnimTable* Table; // 0x10
    xMemPool* Pool;
    xModelInstance* ModelInst;
    void (*BeforeAnimMatrices)(xAnimPlay*, xQuat*, xVec3*, S32);
};

class AnimTableList
{ // size: 0xC
public:
    char* name; // offset 0x0, size 0x4
    struct xAnimTable* (*constructor)(); // offset 0x4, size 0x4
    unsigned int id; // offset 0x8, size 0x4
};

void xAnimInit();
void xAnimTempStateInit(U32 count);
xAnimTransition* xAnimTempTransitionAlloc(const xAnimTransition* source);
void xAnimTempTransitionInit(U32 count);
void xAnimPhysicsEval(xAnimPhysicsData* phys, F32 time, xVec3* disp, F32* yaw);
xAnimFile* xAnimFileNew(void* rawData, const char* name, U32 flags, xAnimFile** linkedList);
void xAnimFileEval(xAnimFile* data, F32 time, F32* bilinear, U32 flags, xVec3* tran, xQuat* quat,
                   F32* arg6);
xAnimTable* xAnimTableNew(const char* name, U32 userFlags);
xAnimState* xAnimTableNewState(xAnimTable* table, const char* name, U32 flags, U32 userFlags,
                               F32 speed, F32* boneBlend, F32* timeSnap, F32 fadeRecip,
                               U16* fadeOffset, void* callbackData,
                               xAnimStateBeforeEnterCallback beforeEnter,
                               xAnimStateCallback stateCallback,
                               xAnimStateBeforeAnimMatricesCallback beforeAnimMatrices);
xAnimTransition* xAnimTableNewTransition(xAnimTable* table, const char* source, const char* dest,
                                         xAnimTransitionConditionalCallback conditional,
                                         xAnimTransitionCallback callback, U32 flags, U32 userFlags,
                                         F32 srcTime, F32 destTime, U16 priority, U16 queuePriority,
                                         F32 blendRecip, U16* blendOffset);
void xAnimDefaultBeforeEnter(xAnimPlay* play, xAnimState* state, void* object);
void xAnimPoolInit(xMemPool* pool, U32 count, U32 singles, U32 blendFlags, U32 effectMax);
xAnimPlay* xAnimPoolAlloc(xMemPool* pool, void* object, xAnimTable* table,
                          xModelInstance* modelInst);
void xAnimPoolFree(xAnimPlay*);
xAnimState* xAnimTableGetState(xAnimTable* table, const char* name);
void xAnimTableAddTransition(xAnimTable* table, xAnimTransition* tran, const char* source);
void xAnimTableAddFile(xAnimTable* table, xAnimFile* file, const char* states);
xAnimState* xAnimTableAddFileID(xAnimTable* table, xAnimFile* file, U32 stateID, U32 subStateID,
                                U32 subStateCount);
xAnimState* xAnimTableGetStateID(xAnimTable* table, U32 ID);
void xAnimPlaySetup(xAnimPlay* play, void* object, xAnimTable* table, xModelInstance* modelInst);
void xAnimPlaySetState(xAnimSingle* single, xAnimState* state, F32 startTime);
void xAnimPlayChooseTransition(xAnimPlay* play);
void xAnimPlayStartTransition(xAnimPlay* play, xAnimTransition* transition);
void xAnimPlayUpdate(xAnimPlay* play, F32 timeDelta);
void xAnimPlayEval(xAnimPlay* play);
xAnimEffect* xAnimStateNewEffect(xAnimState* state, U16 flags, U16 probability, F32 startTime,
                                 F32 endTime, xAnimEffectCallback callback, U32 userDataSize);
void xAnimFileSetTime(xAnimFile* data, F32 duration, F32 timeOffset);
xAnimFile* xAnimFileNewBilinear(void** rawData, const char* name, U32 hashed_name_id, U32 flags,
                                xAnimFile** linkedList, U32 numX, U32 numY);
void xAnimCalcPhysicsData(xAnimFile* afile, xAnimFile* endPose);

inline F32 xAnimFileRawTime(xAnimFile* data, float time)
{
    if (data->FileFlags & 0x1000 || (data->FileFlags & 0x2000 && time > data->Duration * 0.5f))
    {
        return data->TimeOffset + data->Duration - time;
    }
    return data->TimeOffset + time;
}

#endif
