#ifndef IMODEL_H
#define IMODEL_H

#include "xMath3.h"
#include "xModel.h"

#include <rwcore.h>
#include <rpworld.h>
#include <rphanim.h>

void iModelInit();
void iModelDeInit();
RpHAnimHierarchy* iModelGetHierarchy(RpAtomic* imodel);
U32 iModelNumBones(RpAtomic* model);
S32 iModelCull(RpAtomic* model, RwMatrixTag* mat);
S32 iModelSphereCull(xSphere* sphere);
xSphere* iModelWorldBoundSphere(RpAtomic* model, RwMatrixTag* mat, xSphere* sphere);
void iModelSetWorldBoundFromBox(RpAtomic* model, xBox* box);
RpAtomic* iModelFile_RWMultiAtomic(RpAtomic* model);
void iModelSetMaterialTexture(RpAtomic* model, void* texture);
void iModelResetMaterial(RpAtomic* model);
S32 iModelCullPlusShadow(RpAtomic* model, RwMatrix* mat, xVec3* shadowVec, S32* shadowOutside);
void iModelTagEval(RpAtomic* model, const xModelTag* tag, RwMatrixTag* mat, xVec3* dest);
void iModelTagEval(RpAtomic* model, const xModelTagWithNormal* tag, RwMatrixTag* mat, xVec3* dest,
                   xVec3* normal);
U32 iModelTagSetup(xModelTag* tag, const RpAtomic* model, F32 x, F32 y, F32 z);
U32 iModelTagSetup(xModelTagWithNormal* tag, const RpAtomic* model, F32 x, F32 y, F32 z);
void iModelSetMaterialAlpha(RpAtomic* model, U8 alpha);
U32 iModelVertCount(RpAtomic* model);
U32 iModelVertEval(RpAtomic* model, U32 index, U32 count, RwMatrixTag* mat, xVec3* vert,
                   xVec3* dest);
U32 iModelNormalEval(xVec3* out, const RpAtomic& m, const RwMatrixTag* mat, size_t index, S32 size,
                     const xVec3* in);
xSphere* iModelWorldBoundSphere(RpAtomic* model, RwMatrixTag* mat, xSphere* sphere);
void iModelMaterialMul(RpAtomic* model, F32 rm, F32 gm, F32 bm);
RpAtomic* iModelFileNew(void* buffer, U32 size);
// PC: ends the run with a message for a model asset that does not read (iModel.cpp).
void iModelReportUnreadable(U32 assetid);
void iModelRender(RpAtomic* model, RwMatrix* mat);
void iModelUnload(RpAtomic* userdata);
void iModelUnlock(RpAtomic* pModel);
RwTexCoords* iModelLockAndGetTextureCoords(RpAtomic* pModel, S32* numVertices);
void iModelQuatToMat(xQuat* quat, xVec3* tran, RwMatrixTag* mat);
void iModelAnimMatrices(RpAtomic* model, xQuat* quat, xVec3* tran, RwMatrixTag* mat);
void iModelAnimBoundPrepare(RpAtomic* model);
void iModelAnimBoundWorld(xBox* box, xBox* combinedBox, RpAtomic* model, RwMatrixTag* mat,
                          RwMatrixTag* bonemat);

#endif
