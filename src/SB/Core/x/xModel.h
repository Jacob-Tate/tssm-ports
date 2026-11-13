#ifndef XMODEL_H
#define XMODEL_H

#include <rwcore.h>
#include <rpworld.h>

#include "xAnim.h"
#include "xLightKit.h"
#include "xSurface.h"
#include "xMath3.h"
#include "xMath2.h"

struct xModelBucket;

struct xModelPool
{
    xModelPool* Next;
    U32 NumMatrices;
    xModelInstance* List;
};

struct xModelPipe
{
    U32 Flags;
    U8 Layer;
    U8 AlphaDiscard;
    U16 PipePad;
};

#include "xModelBucket.h"

struct xModelInstance
{
    struct _class_6
    {
        xVec3* verts;
    };

    xModelInstance* Next;
    xModelInstance* Parent;
    xModelPool* Pool;
    xAnimPlay* Anim;
    RpAtomic* Data;
    xModelPipe Pipe;
    U8 InFrustum;
    U8 TrueClip;
    S8 sortBias;
    U8 modelpad;
    F32 RedMultiplier;
    F32 GreenMultiplier;
    F32 BlueMultiplier;
    F32 Alpha;
    F32 FadeStart;
    F32 FadeEnd;
    xSurface* Surf;
    xModelBucket** Bucket;
    xModelInstance* BucketNext;
    xLightKit* LightKit;
    void* Object;
    U16 Flags;
    U8 BoneCount;
    U8 BoneIndex;
    U8* BoneRemap;
    RwMatrixTag* Mat;
    xVec3 Scale;
    xBox animBound;
    xBox combinedAnimBound;
    U32 modelID;
    U32 shadowID; // 0x98
    // No shadowmapAtomic on GC (PS2-only): xModelAnimCollStart uses anim_coll at 0x9C.
    _class_6 anim_coll;
};

// NOTE (Square): Theses are based on the access pattern found in zUI_Render.
// I doubt they wrote this out by hand every time, but I'm just guessing on the macro here.
#define XMODELINSTANCE_GET_SRCBLEND(inst) (((inst)->PipeFlags >> 0x8) & 0xf)
#define XMODELINSTANCE_GET_DSTBLEND(inst) (((inst)->PipeFlags >> 0xc) & 0xf)

struct xModelTag
{
    xVec3 v;
    U32 matidx;
    F32 wt[4];
};

struct xModelTagWithNormal : xModelTag
{
    xVec3 normal;
};

struct xModelAssetParam
{
    U32 HashID;
    U8 WordLength;
    U8 String[3];
};

struct xModelAssetInfo
{
    U32 Magic;
    U32 NumModelInst;
    U32 AnimTableID;
    U32 CombatID;
    U32 BrainID;
};

struct xModelAssetInst
{
    U32 ModelID;
    U16 Flags;
    U8 Parent;
    U8 Bone;
    F32 MatRight[3];
    F32 MatUp[3];
    F32 MatAt[3];
    F32 MatPos[3];
};

struct xModelPipeLookup
{
    RpAtomic* model;
    xModelPipe Pipe;
};

struct xModelPipeTable
{
    U32 ModelHashID;
    U32 SubObjectBits;
    xModelPipe Pipe;
};

extern S32 xModelPipeNumTables;
extern S32 xModelPipeCount[16];
extern xModelPipeTable* xModelPipeData[16];
extern S32 xModelLookupCount;
extern xModelPipeLookup* xModelLookupList;
extern S32 xModelInstStaticAlloc;

U32 xModelGetPipeFlags(RpAtomic* model);
xModelPipe xModelGetPipe(RpAtomic* model);
RpAtomic* xModelFindAtomic(U32 id, U32* actual_id);
void xModelInit();
void xModelPoolInit(U32 count, U32 numMatrices);
void xModelAnimCollStart(xModelInstance& m);
void xModelAnimCollRefresh(const xModelInstance& cm);
S32 xModelSphereCull(xSphere* sphere);
S32 xModelBoxCull(xBox* box);
S32 xModelCullGroup(xModelInstance* minst);
S32 xModelCullGroupPlusShadow(xModelInstance* minst, F32 shadowDepth, S32* shadowOutside);
void xModelSetFrame(xModelInstance* modelInst, const xMat4x3* frame);
xModelInstance* xModelInstanceAllocReuse(RpAtomic* data, void* object, U16 flags, U8 boneIndex,
                                         U8* boneRemap, xModelInstance* reuse_instance);

inline bool xModelAnimCollDirty(const xModelInstance& cm)
{
    return (cm.Flags & 0x1800) == 0x800;
}

extern RpMorphTarget anim_coll_old_mt;

// xModelAnimCollDirty/Apply/Restore are xModel.h inlines in the Ratatouille GameCube DWARF;
// no symbols in TSSM.
inline void xModelAnimCollApply(const xModelInstance& cm)
{
    if (xModelAnimCollDirty(cm))
    {
        xModelAnimCollRefresh(cm);
    }

    RpMorphTarget* mt = cm.Data->geometry->morphTarget;
    anim_coll_old_mt.verts = mt->verts;
    mt->verts = (RwV3d*)cm.anim_coll.verts;
}

inline void xModelAnimCollRestore(const xModelInstance& cm)
{
    cm.Data->geometry->morphTarget->verts = anim_coll_old_mt.verts;
}

// No symbol in TSSM: the target calls xModelInstanceAllocReuse with a NULL instance.
inline xModelInstance* xModelInstanceAlloc(RpAtomic* data, void* object, U16 flags, U8 boneIndex,
                                           U8* boneRemap)
{
    return xModelInstanceAllocReuse(data, object, flags, boneIndex, boneRemap, NULL);
}
void xModelInstanceFree(xModelInstance* modelInst);
void xModelInstanceAttach(xModelInstance* inst, xModelInstance* parent);
S32 xModelCullSingle(xModelInstance* minst);
void xModelRender(xModelInstance* modelInst);
S32 xModelSphereCullPlusShadow(xSphere* sphere, xVec3* shadowVec, S32* shadowOutside);
void xModelRenderSingle(xModelInstance* modelInst);
void xModelRender2D(const xModelInstance& model, const basic_rect<F32>& r, const xVec3& from,
                    const xVec3& to);
void xModelUpdate(xModelInstance* modelInst, F32 timeDelta);
xMat4x3* xModelGetFrame(xModelInstance* modelInst);
void xModelEvalSingle(xModelInstance* modelInst, xBox* combinedAnimBound);
void xModelEval(xModelInstance* modelInst);
void xModel_SceneEnter(RpWorld* world);
void xModel_SceneExit(RpWorld* world);
void xModelGetBoneMat(xMat4x3& mat, const xModelInstance& model, size_t index);
void xModelGetBoneLocation(xVec3& loc, const xModelInstance& model, size_t index);
void xModelGetBoneLocationScaled(xVec3& loc, const xModelInstance& model, size_t index);
void xModelInstanceUpgradeBrotherShared(xModelInstance* inst, U32 flags);
void xModelSetLightKit(xModelInstance* minst, xLightKit* lightKit);
bool xModelIsPreinstanced(xModelInstance* model);

inline void xModelSetFrame(xModelInstance* modelInst, const xMat4x3* frame)
{
    xMat4x3Copy((xMat4x3*)modelInst->Mat, frame);
}

inline xMat4x3* xModelGetFrame(xModelInstance* modelInst)
{
    return (xMat4x3*)modelInst->Mat;
}

// No symbol in TSSM (inlined into parse_tag_model); Ratatouille's GC DWARF lists it in xModel.h.
inline xSphere* xModelGetLocalSBound(xModelInstance* model)
{
    return (xSphere*)RpAtomicGetBoundingSphere(model->Data);
}

// No symbol in TSSM (always inlined); BfBB has it as the weak
// xModelSetScale__FP14xModelInstanceRC5xVec3.
inline void xModelSetScale(xModelInstance* model, const xVec3& scale)
{
    for (; model != NULL; model = model->Next)
    {
        model->Scale = scale;
    }
}

// No symbol in TSSM (inlined into zNMETurSpiral::Process); Ratatouille's GC DWARF lists this
// uniform-scale overload in xModel.h.
inline void xModelSetScale(xModelInstance* model, F32 scale)
{
    for (; model != NULL; model = model->Next)
    {
        model->Scale.x = scale;
        model->Scale.y = scale;
        model->Scale.z = scale;
    }
}

void iModelSetMaterialAlpha(RpAtomic* model, U8 alpha);

// No symbol in TSSM (inlined into xhud::render_model); Ratatouille's GC DWARF also lists it in
// xModel.h.
inline void xModelSetMaterialAlpha(xModelInstance* modelInst, U8 alpha)
{
    iModelSetMaterialAlpha(modelInst->Data, alpha);
}

#endif
