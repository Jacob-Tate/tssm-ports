#include "zEntSimpleObj.h"

#include <types.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>

#include "zCam.h"
#include "zDestructible.h"
#include "zReactiveAnimation.h"
#include "xModelBucket.h"
#include "xLightKit.h"
#include "xRand.h"
#include "xVec3Inlines.h"
#include "xMathInlines.h"
#include "iCamera.h"
#include "iModel.h"
#include "iAnimSKB.h"
#include "zMain.h"

#ifdef PLATFORM_PC
#include "iExtras.h"
#endif

static U32 sMgrCount;
static zSimpleMgr* sMgrList;
static U32 sSimpleCustomCount;
static xEnt** sSimpleCustomList;

static void zEntSimple_Orientation(const xMat4x3& mat, RwMatrixTag* modelmat, bool use_world_up);
static void zEntSimpleObj_Move(xEnt*, xScene*, F32, xEntFrame*);
static void zEntSimpleObj_Render(xEnt* ent);
static void VisUpdateSimpleMgr(xEnt* ent);
static S32 CmpSimpleMgr(const void* a, const void* b);

static S32 CmpSimpleMgr(const void* a, const void* b)
{
    zSimpleMgr* simpA = (zSimpleMgr*)a;
    zSimpleMgr* simpB = (zSimpleMgr*)b;

#ifdef PLATFORM_PC
    // PLATFORM_PC: the console's address order of the atomics is their creation order, latest
    // first (iModelOrder.h), as in xModelBucket.cpp; the host's heap has no such order.
    S32 order = iModelCreationOrderCompare(simpA->lodAtomic[0], simpB->lodAtomic[0]);
    if (order != 0)
    {
        return order;
    }
#else
    if (simpA->lodAtomic[0] > simpB->lodAtomic[0])
    {
        return 1;
    }
    if (simpA->lodAtomic[0] < simpB->lodAtomic[0])
    {
        return -1;
    }
#endif
    if (simpA->lkit > simpB->lkit)
    {
        return 1;
    }
    if (simpA->lkit < simpB->lkit)
    {
        return -1;
    }
    return 0;
}

static void VisUpdateSimpleMgr(xEnt* ent)
{
    if (((zEntSimpleObj*)ent)->smgr == NULL)
    {
        return;
    }

    if (xEntIsVisible(ent) && (ent->model->Flags & 0x1))
    {
        ((zEntSimpleObj*)ent)->smgr->flags |= 0x1;
    }
    else
    {
        ((zEntSimpleObj*)ent)->smgr->flags &= ~0x1;
    }
}

void zEntSimpleObj_MgrInit(zEntSimpleObj** entList, U32 entCount)
{
    U32 i;
    U32 reactiveExtras;
    U32 modelsRequired;
    zEntSimpleObj** tempEntList;
    U32 tempEntCount;
    U32 custEntCount;
    zEntSimpleObj* ent;

    sMgrCount = 0;
    sMgrList = NULL;
    sSimpleCustomCount = 0;
    sSimpleCustomList = NULL;

    if (entCount == 0)
    {
        return;
    }

    reactiveExtras = zReactiveCountExtraModels(entList, entCount);
    modelsRequired = entCount + reactiveExtras;
    tempEntList = (zEntSimpleObj**)RwMalloc(modelsRequired * sizeof(zEntSimpleObj*), 0);
    tempEntCount = 0;
    custEntCount = 0;

    for (i = 0; i < entCount; i++)
    {
        ent = entList[i];
        if (ent->destructible != NULL && ent->model != NULL)
        {
            zDestructibleAsset_ReReset(ent->destructible, ent);
        }

        if (!(entList[i]->sflags & 0x10))
        {
            if ((entList[i]->destructible != NULL || entList[i]->move != NULL ||
                 (entList[i]->moreFlags & 0x8) || (entList[i]->moreFlags & 0x20) ||
                 (entList[i]->moreFlags & 0x80) || (entList[i]->miscflags & 0x1) ||
                 entList[i]->atbl != NULL || (entList[i]->sflags & 0x4) ||
                 (entList[i]->sflags & 0x8) || entList[i]->model == NULL ||
                 entList[i]->model->Next != NULL || entList[i]->model->Anim != NULL ||
                 entList[i]->reactiveAnimationData != NULL ||
                 entList[i]->baseType == eBaseTypeTrackPhysics || entList[i]->driver != NULL) &&
                !(entList[i]->sflags & 0x200))
            {
                tempEntList[modelsRequired - 1 - custEntCount++] = entList[i];

                if ((entList[i]->driver != NULL && entList[i]->move == NULL) ||
                    (entList[i]->moreFlags & 0x8))
                {
                    entList[i]->move = zEntSimpleObj_Move;
                    entList[i]->pflags |= 0x1;
                    entList[i]->frame = (xEntFrame*)xMemAlloc(gActiveHeap, sizeof(xEntFrame), 0);
                    entList[i]->frame->mat = *(xMat4x3*)entList[i]->model->Mat;
                }
            }
            else
            {
                entList[i]->baseFlags |= 0x80;
                tempEntList[tempEntCount++] = entList[i];

                if ((entList[i]->sflags & 0x200) && zReactiveNeedExtraModel(*entList[i]))
                {
                    tempEntList[tempEntCount++] = entList[i];
                }
            }
        }

        if (!(entList[i]->sflags & 0x200))
        {
            ent = entList[i];
            if (ent->sasset->flags & 0x1)
            {
                F32 dt = 1.0f / VBLANKS_PER_SEC;
                F32 duration = iAnimDuration(ent->anim);
                S32 frameCount = duration / dt;
                S32 boneCount = ((iAnimSKBHeader*)ent->anim)->BoneCount;

                ent->fastMatList = (RwMatrixTag*)xMemAlloc(
                    gActiveHeap, boneCount * frameCount * sizeof(RwMatrixTag), 16);

                xQuat* q0 = (xQuat*)g_iAnim_workBuffer;
                xVec3* t0 = (xVec3*)(q0 + 65);
                F32 t = 0.0f;
                RwMatrixTag* fastMatList = ent->fastMatList;

                for (S32 i = 0; i < frameCount; i++)
                {
                    iAnimEval(ent->anim, t, 0, t0, q0);
                    iModelAnimMatrices(ent->model->Data, q0, t0, fastMatList);
                    t += dt;
                    fastMatList += boneCount;
                }
            }
            else
            {
                ent->fastMatList = NULL;
            }
        }
    }

    if (custEntCount != 0)
    {
        sSimpleCustomCount = custEntCount;
        sSimpleCustomList = (xEnt**)xMemAlloc(gActiveHeap, custEntCount * sizeof(xEnt*), 0);

        for (i = 0; i < custEntCount; i++)
        {
            sSimpleCustomList[i] = tempEntList[modelsRequired - 1 - i];
        }
    }
    else
    {
        sSimpleCustomCount = 0;
        sSimpleCustomList = NULL;
    }

    if (tempEntCount == 0)
    {
        RwFree(tempEntList);
        return;
    }

    sMgrCount = tempEntCount;
    sMgrList = (zSimpleMgr*)xMemAlloc(gActiveHeap, tempEntCount * sizeof(zSimpleMgr), 64);

    zSimpleMgr* smgr = sMgrList;
    for (i = 0; i < tempEntCount; i++)
    {
        if (tempEntList[i]->sflags & 0x200)
        {
            zReactivePreBucket(*tempEntList[i]);
        }

        RpAtomic* model = tempEntList[i]->model->Data;
        RwSphere oldbound = model->boundingSphere;
        model->boundingSphere.radius *= 1.1f;
        iModelWorldBoundSphere(model, tempEntList[i]->model->Mat, &smgr->worldBound);
        model->boundingSphere = oldbound;

        zLODTable* lod = zLOD_Get(tempEntList[i]);
        if (lod != NULL)
        {
            F32 distscale = SQR(tempEntList[i]->model->Mat->right.x) +
                            SQR(tempEntList[i]->model->Mat->right.y) +
                            SQR(tempEntList[i]->model->Mat->right.z);
            if (distscale < 0.0001f)
            {
                distscale = 1.0f;
            }

            smgr->lodDist0 = (lod->lodDist[0]) ? lod->lodDist[0] * distscale : 1e38f;
            smgr->noRenderDist = (lod->noRenderDist) ? lod->noRenderDist * distscale : 1e38f;
            smgr->lodBucket[0] = lod->baseBucket;
            smgr->lodBucket[1] = lod->lodBucket[0];

            if (smgr->lodBucket[1] == NULL)
            {
                smgr->lodDist0 = 1e38f;
            }
        }
        else
        {
            smgr->noRenderDist = smgr->lodDist0 = 1e38f;
            smgr->lodBucket[0] = tempEntList[i]->model->Bucket;
            smgr->lodBucket[1] = NULL;
        }

        smgr->flags = (*smgr->lodBucket[0])->Pipe.Flags & ~0x7;

        bool uvAnimated = ((*smgr->lodBucket[0])->Pipe.Flags & 0x1800000) ? TRUE : FALSE;
        // not in the PS2 DWARF: retail masks the bucket pipe flags right after the load and keeps
        // only the & 0x7 value.
        U32 modelFlags = (*smgr->lodBucket[0])->Pipe.Flags & 0x7;
        if (smgr->lodBucket[1] != NULL)
        {
            uvAnimated =
                uvAnimated || (((*smgr->lodBucket[1])->Pipe.Flags & 0x1800000) ? TRUE : FALSE);
        }

        if ((smgr->flags & 0xFF00) || uvAnimated)
        {
            smgr->flags |= 0x2;
        }
        else if (modelFlags == 0x4 &&
                 (smgr->lodBucket[1] == NULL || ((*smgr->lodBucket[1])->Pipe.Flags & 0x7) == 0x4))
        {
            smgr->flags |= 0x4;
        }

        if (tempEntList[i]->model->FadeStart < 1e19f)
        {
            smgr->fadeDist = tempEntList[i]->model->FadeStart * tempEntList[i]->model->FadeStart;
        }
        else
        {
            smgr->fadeDist = 1e38f;
        }

        smgr->lodAtomic[0] = (*smgr->lodBucket[0])->OriginalData;
        smgr->lodAtomic[1] =
            (smgr->lodBucket[1] != NULL) ? (*smgr->lodBucket[1])->OriginalData : NULL;
        smgr->model = tempEntList[i]->model;
        smgr->mat = tempEntList[i]->model->Mat;
        smgr->ent = tempEntList[i];
        smgr->lkit = tempEntList[i]->model->LightKit;

        RwMatrixUpdate(smgr->mat);
        xEntUpdate(tempEntList[i], globals.sceneCur, 0.0f);
        smgr++;
    }

    qsort(sMgrList, sMgrCount, sizeof(zSimpleMgr), CmpSimpleMgr);

    for (i = 0; i < sMgrCount; i++)
    {
        if (sMgrList[i].ent->smgr != NULL && (sMgrList[i].ent->sflags & 0x200))
        {
            zReactiveAddExtraSimpleMgr(*sMgrList[i].ent, &sMgrList[i]);
        }
        else
        {
            sMgrList[i].ent->smgr = &sMgrList[i];

            if (sMgrList[i].ent->visUpdate == NULL)
            {
                sMgrList[i].ent->visUpdate = VisUpdateSimpleMgr;
            }

            VisUpdateSimpleMgr(sMgrList[i].ent);
        }
    }

    RwFree(tempEntList);
}

void zEntSimpleObj_MgrUpdateRender(RpWorld* world, F32 dt)
{
    U32 i;
    U32 currPipeFlags = 0;
    U32 pipeMask = 0x38;

    if (xglobals->fog.type == rwFOGTYPENAFOGTYPE)
    {
        pipeMask &= ~0x8;
    }

    F32 camX = globals.cam->mat.pos.x;
    F32 camY = globals.cam->mat.pos.y;
    F32 camZ = globals.cam->mat.pos.z;
    zSimpleMgr* smgr = sMgrList;

    for (i = sMgrCount; i != 0; i--, smgr++)
    {
        U32 smgrFlags = smgr->flags;

        if (!(smgrFlags & 0x1))
        {
            if (smgr->ent->smgr == smgr && (smgr->ent->sflags & 0x200))
            {
                zReactiveDeactivate(*smgr->ent, true);
            }
            continue;
        }

        F32 camdist2 = (camX - smgr->worldBound.center.x) * (camX - smgr->worldBound.center.x) +
                       (camY - smgr->worldBound.center.y) * (camY - smgr->worldBound.center.y) +
                       (camZ - smgr->worldBound.center.z) * (camZ - smgr->worldBound.center.z);

        if (camdist2 > smgr->noRenderDist)
        {
            if (smgr->ent->sflags & 0x200)
            {
                zReactiveDeactivate(*smgr->ent, true);
            }
            continue;
        }

        if (xModelSphereCull(&smgr->worldBound))
        {
            if (smgr->ent->sflags & 0x200)
            {
                zReactiveDeactivate(*smgr->ent, true);
            }
            continue;
        }

        zEntSimpleObj* ent = smgr->ent;

        if ((ent->sflags & 0x200) && zReactiveSelectLOD(*ent, camdist2))
        {
            continue;
        }

        U32 testPipeFlags = smgrFlags & pipeMask;
        U32 xorFlags = testPipeFlags ^ currPipeFlags;

        if (xorFlags)
        {
            currPipeFlags = testPipeFlags;

            if (xorFlags & 0x8)
            {
                if (testPipeFlags & 0x8)
                {
                    iCameraSetFog(false);
                }
                else
                {
                    iCameraSetFog(true);
                }
            }

            if (xorFlags & 0x30)
            {
                if ((testPipeFlags & 0x30) == 0x20)
                {
                    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
                }
                else if ((testPipeFlags & 0x30) <= 0x10)
                {
                    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
                }
            }
        }

        S32 picklod = camdist2 > smgr->lodDist0;

        if ((smgrFlags & 0x2) || camdist2 >= smgr->fadeDist)
        {
            xModelInstance* minst = smgr->model;
            minst->Bucket = smgr->lodBucket[picklod];
            minst->Data = smgr->lodAtomic[picklod];
            xModelBucket_Add(minst);
        }
        else
        {
            if (ent->model->Surf != NULL)
            {
                xEntSetupPipeline(ent->model->Surf, ent->model->Data);
            }

            RpAtomic* thisAtomic = smgr->lodAtomic[picklod];
            *(xSphere*)&thisAtomic->worldBoundingSphere = smgr->worldBound;

            xLightKit_Enable(smgr->lkit, xglobals->currWorld);

            if (smgr->model->Flags & 0x4000)
            {
                iModelMaterialMul(smgr->model->Data, smgr->model->RedMultiplier,
                                  smgr->model->GreenMultiplier, smgr->model->BlueMultiplier);
                iModelRender(thisAtomic, smgr->mat);
                iModelResetMaterial(smgr->model->Data);
            }
            else
            {
                iModelRender(thisAtomic, smgr->mat);
            }
        }

        if (picklod == 0 && (xrand_RandomInt32() & 0xFFFF) < 0x55)
        {
            xVec3 pos;
            xVec3Copy(&pos, (xVec3*)&ent->model->Mat->pos);

            pos.y += 0.25f * xurand() + 0.25f;

            xVec3 blob_posrnd = { 0.25f, 1.0f, 0.25f };

            zFX_SpawnBubbleTrail(&pos, (xrand_RandomInt32() & 0x7) + 1, &blob_posrnd, NULL);
        }
    }

    if (currPipeFlags & 0x8)
    {
        iCameraSetFog(true);
    }

    if ((currPipeFlags & 0x30) > 0x10)
    {
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    }
}

void zEntSimpleObj_MgrCustomUpdate(zScene* s, F32 dt)
{
    U32 i;

    for (i = 0; i < sSimpleCustomCount; i++)
    {
        if (!(sSimpleCustomList[i]->baseFlags & 0x40))
        {
            sSimpleCustomList[i]->update(sSimpleCustomList[i], s, dt);
        }
    }
}

void zEntSimpleObj_MgrCustomRender()
{
    U32 i;

    for (i = 0; i < sSimpleCustomCount; i++)
    {
        sSimpleCustomList[i]->render(sSimpleCustomList[i]);
    }
}

static void zEntSimpleObj_Render(xEnt* ent)
{
    if (ent->model == NULL || !xEntIsVisible(ent))
    {
        return;
    }

    xModelRender(ent->model);
}

#ifdef PLATFORM_PC
// PLATFORM_PC: [extras] crowds = gamecube (iExtras.h). The Xbox files give the fish crowds of
// BB02 and BB01 skinned models and an animation list; the GameCube files give them rigid models
// and none. zEntReset has just started the list's idle cycle at its start; at speed 0 the crowd
// keeps that first pose. Dropping the list instead leaves BB01's skinned crowd in a heap.
static void zEntSimpleObj_HoldCrowd(zEntSimpleObj* ent)
{
    if (ent->atbl != NULL && ent->model != NULL && ent->model->Anim != NULL &&
        iExtrasCrowdHeld(ent->asset->modelInfoID))
    {
        ent->model->Anim->Single->CurrentSpeed = 0.0f;
    }
}
#endif

void zEntTrackPhysics_Init(void* ent, void* asset)
{
    zEntSimpleObj_Init((zEntSimpleObj*)ent, (xEntAsset*)asset, true, true);
}

void zEntSimpleObj_Init(void* ent, void* asset)
{
    zEntSimpleObj_Init((zEntSimpleObj*)ent, (xEntAsset*)asset, false, true);
}

void zEntSimpleObj_Init(zEntSimpleObj* ent, xEntAsset* asset, bool physparams, bool candestroy)
{
    xSimpleObjAsset* sasset;
    U32 tmpsize;
    RpAtomic* modelData;
    void* animData;
    U32 animBoneCount;
    U32 size;
    U32 newid;
    xDestructibleAsset* destruct;

    zEntInit(ent, asset, 'SIMP');

    if (physparams)
    {
        ent->baseType = eBaseTypeTrackPhysics;
    }

    // The simple object asset directly follows the entity asset in both layouts
    // (retail still tests physparams here).
    if (physparams)
    {
        sasset = (xSimpleObjAsset*)(asset + 1);
    }
    else
    {
        sasset = (xSimpleObjAsset*)(asset + 1);
    }

    ent->sasset = sasset;
    ent->sflags = 0;
    ent->smgr = NULL;

    if (ent->sasset->flags & 0x8)
    {
        ent->sflags |= 0x80;
    }

    if (ent->sasset->flags & 0x2)
    {
        ent->sflags |= 0x20;
    }

    if (ent->sasset->flags & 0x4)
    {
        ent->sflags |= 0x40;
    }

    if (ent->sasset->flags & 0x10)
    {
        ent->sflags |= 0x100;
    }

    ent->pflags = 0;
    ent->penby |= XENT_COLLTYPE_PLYR;
    ent->collType = ent->sasset->collType;
    ent->chkby = ent->collType ? (XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR) : 0;
    ent->move = NULL;
    ent->update = (xEntUpdateCallback)zEntSimpleObj_Update;
    ent->eventFunc = zEntSimpleObjEventCB;
    ent->render = zEntSimpleObj_Render;

    if (ent->linkCount)
    {
        if (physparams)
        {
            ent->link = (xLinkAsset*)((U8*)ent->asset + sizeof(xEntAsset) +
                                      sizeof(xSimpleObjAsset) + sizeof(xEntMotionAsset));
        }
        else
        {
            ent->link =
                (xLinkAsset*)((U8*)ent->asset + sizeof(xEntAsset) + sizeof(xSimpleObjAsset));
        }
    }
    else
    {
        ent->link = NULL;
    }

    ent->eventFunc = zEntSimpleObjEventCB;

    modelData = (RpAtomic*)xSTFindAsset(asset->modelInfoID, &tmpsize);
    animData = NULL;

    if (!(ent->miscflags & 0x1) && ent->asset->modelInfoID != 0 && ent->model != NULL &&
        ent->model->Anim != NULL && ent->model->Anim->Table != NULL &&
        strcmp("xEntAutoEventSimple", ent->model->Anim->Table->Name) == 0)
    {
        xAnimPlaySetState(ent->model->Anim->Single, ent->model->Anim->Table->StateList, 0.0f);
        ent->miscflags |= 0x1;
    }
    else if (asset->animListID != 0 && ent->atbl == NULL)
    {
        animData = xSTFindAsset(asset->animListID, &tmpsize);
        if (animData != NULL)
        {
            animBoneCount = iAnimBoneCount(animData);
            if (animBoneCount == 0 || animBoneCount != iModelNumBones(modelData))
            {
                animData = NULL;
            }
        }
    }

    ent->anim = animData;
    ent->animTime = 0.0f;

    zEntReset(ent);
#ifdef PLATFORM_PC
    zEntSimpleObj_HoldCrowd(ent);
#endif
    xEntOpacityInit(&ent->opacity, ent);

    if (candestroy)
    {
        ent->destructible = NULL;
        size = 0;

        if (ent->model != NULL)
        {
            newid = xStrHashCat(ent->model->modelID, "_destruct");
            destruct = (xDestructibleAsset*)xSTFindAsset(newid, &size);
            if (destruct != NULL)
            {
                zDestructibleAsset_Init(newid, ent->destructible, ent, ent->asset->modelInfoID);
                ent->moreFlags |= 0x10;
            }

            if (ent->destructible != NULL)
            {
                ent->destructible->base_model = ent->model;
                ent->destructible->base_collision = ent->collModel;
                ent->destructible->has_animation = TRUE;

                if (ent->destructible->dstates->animList != NULL)
                {
                    ent->anim = *ent->destructible->dstates->animList->animData;
                }
            }
        }
    }
}

static void zEntSimpleObj_Move(xEnt*, xScene*, F32, xEntFrame*)
{
}

static void zEntSimple_Orientation(const xMat4x3& mat, RwMatrixTag* modelmat, bool use_world_up)
{
    F32 xscale = xVec3Length(modelmat->right.x, modelmat->right.y, modelmat->right.z);
    F32 yscale = xVec3Length(modelmat->up.x, modelmat->up.y, modelmat->up.z);
    F32 zscale = xVec3Length(modelmat->at.x, modelmat->at.y, modelmat->at.z);

    modelmat->at.x = mat.pos.x - modelmat->pos.x;
    modelmat->at.y = mat.pos.y - modelmat->pos.y;
    modelmat->at.z = mat.pos.z - modelmat->pos.z;

    xVec3 _up_ = { 0.0f, 1.0f, 0.0f };
    xVec3 _at_ = { modelmat->at.x, modelmat->at.y, modelmat->at.z };
    xVec3 _right_;

    xVec3Cross(&_right_, &_up_, &_at_);

    if (use_world_up)
    {
        xVec3Cross(&_at_, &_right_, &_up_);
    }
    else
    {
        xVec3Cross(&_up_, &_at_, &_right_);
    }

    xVec3Normalize(&_at_, &_at_);
    xVec3Normalize(&_up_, &_up_);
    xVec3Normalize(&_right_, &_right_);

    modelmat->at.x = _at_.x * zscale;
    modelmat->at.y = _at_.y * zscale;
    modelmat->at.z = _at_.z * zscale;

    modelmat->up.x = _up_.x * yscale;
    modelmat->up.y = _up_.y * yscale;
    modelmat->up.z = _up_.z * yscale;

    modelmat->right.x = _right_.x * xscale;
    modelmat->right.y = _right_.y * xscale;
    modelmat->right.z = _right_.z * xscale;
}

void zEntSimpleObj_Update(zEntSimpleObj* ent, xScene* sc, F32 dt)
{
    bool use_world_up;
    F32 duration;
    xQuat* q0;
    xVec3* t0;

    xEntUpdate(ent, sc, dt);

    use_world_up = (ent->sflags & 0x100) != 0;

    if (ent->sflags & 0x20)
    {
        zEntSimple_Orientation(zCamGetMatrix(), ent->model->Mat, use_world_up);
    }

    if (ent->sflags & 0x40)
    {
        zEntSimple_Orientation(xglobals->___player_ent_dont_use_directly->frame->mat,
                               ent->model->Mat, use_world_up);
    }

    if (ent->destructible != NULL)
    {
        zDestructibleAsset_Update(ent, ent->destructible, sc, dt);
    }

    if (ent->anim != NULL && !(ent->sflags & 0x200) && ent->model != NULL &&
        !(ent->model->Flags & 0x400))
    {
        duration = iAnimDuration(ent->anim);

        ent->animTime += dt;
        if (ent->animTime >= duration)
        {
            ent->animTime -= duration;
        }

        q0 = (xQuat*)g_iAnim_workBuffer;
        t0 = (xVec3*)(q0 + 65);
        iAnimEval(ent->anim, ent->animTime, 0, t0, q0);

        iModelAnimMatrices(ent->model->Data, q0, t0, ent->model->Mat + 1);
    }

    if (ent->model != NULL && !(ent->model->Pipe.Flags & 0x300000))
    {
        xEntOpacityUpdate(&ent->opacity, ent, dt);
    }
}

void zEntSimpleObj_Setup(zEntSimpleObj* ent)
{
    if (ent->model != NULL)
    {
        zEntSetup(ent);
    }

    if (ent->destructible != NULL && ent->destructible->base_collision == NULL &&
        ent->collModel != NULL)
    {
        ent->destructible->base_collision = ent->collModel;
    }
}

void zEntSimpleObj_Save(zEntSimpleObj* ent, xSerial* s)
{
    zEntSave(ent, s);
}

void zEntSimpleObj_Load(zEntSimpleObj* ent, xSerial* s)
{
    zEntLoad(ent, s);
}

void zEntSimpleObj_Reset(zEntSimpleObj* ent, xScene* scene)
{
    RpAtomic* imodel;
    xLightKit* pLightKit;
    xSurface* surface;
    S32 i;
    xLinkAsset* la;
    xEnt* dent;

    if (ent->destructible != NULL && ent->model != NULL)
    {
        imodel = (RpAtomic*)xSTFindAsset(ent->destructible->base_modelID, NULL);
        if (imodel != NULL)
        {
            pLightKit = ent->model->LightKit;
            if (ent->model != NULL)
            {
                surface = ent->model->Surf;
                xModelInstanceFree(ent->model);
                ent->model = NULL;
            }

            ent->model = xEntLoadModel(NULL, imodel, ent->destructible->base_model);
            ent->model->Surf = surface;
            xModelSetLightKit(ent->model, pLightKit);
            ent->model->Flags &= (U16)~0x400;
            ent->model->modelID = ent->destructible->base_modelID;

            xBoxForModelLocal(ent->bound.box.box, ent->model, false);
            ent->bound.mat = (xMat4x3*)ent->model->Mat;
            xQuickCullForBound(&ent->bound.qcd, &ent->bound);
            xBoundUpdate(&ent->bound);
            zGridUpdateEnt(ent);
        }

        if (ent->destructible != NULL)
        {
            zDestructibleAsset_Reset(ent->destructible, ent);
            ent->destructible->base_model = ent->model;
            ent->destructible->base_collision = ent->collModel;
            ent->destructible->has_animation = TRUE;

            if (ent->destructible->dstates->animList != NULL)
            {
                ent->anim = *ent->destructible->dstates->animList->animData;
            }

            for (i = 0; i < ent->linkCount; i++)
            {
                la = &ent->link[i];

                if (la->dstEvent == eEventDrivenby)
                {
                    dent = (xEnt*)xSceneResolvID(g_xSceneCur, la->dstAssetID);

                    if (dent != NULL)
                    {
                        if (dent->frame == NULL)
                        {
                            dent->collType = XENT_COLLTYPE_DYN;
                            xEntInitForType(dent);
                        }

                        ent->driver = dent;
                        ent->driveMode = (S32)la->param[0];
                        dent->driving_count++;
                    }
                }
            }
        }
    }

    if (ent->model != NULL)
    {
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else
    {
        return;
    }

    ent->pflags = 0;
    zEntReset(ent);
#ifdef PLATFORM_PC
    zEntSimpleObj_HoldCrowd(ent);
#endif

    if (ent->sasset->flags & 0x8)
    {
        ent->sflags |= 0x80;
    }

    if (ent->sasset->flags & 0x2)
    {
        ent->sflags |= 0x20;
    }

    if (ent->sasset->flags & 0x4)
    {
        ent->sflags |= 0x40;
    }

    if (ent->sasset->flags & 0x10)
    {
        ent->sflags |= 0x100;
    }

    ent->penby |= XENT_COLLTYPE_PLYR;
    ent->collType = ent->sasset->collType;
    ent->chkby = ent->collType ? (XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR) : 0;
    ent->move = NULL;
    ent->update = (xEntUpdateCallback)zEntSimpleObj_Update;
    ent->eventFunc = zEntSimpleObjEventCB;
    ent->render = zEntSimpleObj_Render;

    if (!(ent->sflags & 0x200))
    {
        ent->animTime = 0.0f;
    }

    xGridRemove(&ent->gridb);

    if (ent->bupdate != NULL)
    {
        ent->bupdate(ent, (xVec3*)&ent->model->Mat->pos);
    }
    else
    {
        xEntDefaultBoundUpdate(ent, (xVec3*)&ent->model->Mat->pos);
    }

    xEntOpacityInit(&ent->opacity, ent);

    if ((ent->driver != NULL && ent->move == NULL) || (ent->moreFlags & 0x8))
    {
        ent->move = zEntSimpleObj_Move;
        ent->pflags |= 0x1;
        xMat4x3Copy(&ent->frame->mat, xModelGetFrame(ent->model));
    }

    if (ent->smgr != NULL)
    {
        iModelWorldBoundSphere(ent->model->Data, ent->model->Mat, &ent->smgr->worldBound);
    }

    if (ent->destructible != NULL)
    {
        ent->moreFlags |= 0x10;
    }
}

void zEntSimpleObjEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                          xBase* toParamWidget, U32 toParamWidgetID)
{
    zEntSimpleObj* s = (zEntSimpleObj*)to;
    zShrapnelAsset* shrap;
    FloatAndVoid dist;

    if (s->destructible != NULL && from != to)
    {
        zDestructibleAsset_EventCB(s, from, s->destructible, toEvent, toParam, toParamWidget,
                                   toParamWidgetID);
    }

    switch (toEvent)
    {
    case eEventFastVisible:
    case eEventVisible:
        xEntShow((xEnt*)to);
        if (toParam != NULL && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOn((xEnt&)*to, toParam[1], toParam[2]);
        }
        break;
    case eEventFastInvisible:
    case eEventInvisible:
        xEntHide((xEnt*)to);
        if (toParam != NULL && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOff((xEnt&)*to, toParam[1], toParam[2]);
        }
        break;
    case eEventCollision_Visible_On:
        s->collType = XENT_COLLTYPE_STAT;
        s->chkby |= XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;

        xEntShow((xEnt*)to);

        xGridRemove(&s->gridb);
        if (s->bupdate != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        else
        {
            xEntDefaultBoundUpdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }

        if (toParam != NULL && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOn((xEnt&)*to, toParam[1], toParam[2]);
        }
        break;
    case eEventSpawnBubblesOn:
        zFXBubbleSpawnStart(*s, toParam[0]);
        break;
    case eEventSpawnBubblesOff:
        zFXBubbleSpawnStop(*s);
        break;
    case eEventCollisionOn:
        s->collType = XENT_COLLTYPE_STAT;
        s->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        if (s->bupdate != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        else
        {
            xEntDefaultBoundUpdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        break;
    case eEventCollisionReset:
        s->collType = s->sasset->collType;
        s->chkby = s->collType ? (XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR) : 0;
        if (s->bupdate != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        else
        {
            xEntDefaultBoundUpdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        break;
    case eEventCollision_Visible_Off:
        s->collType = XENT_COLLTYPE_NONE;
        s->chkby &= (U8) ~(XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR);

        xEntHide((xEnt*)to);

        if (toParam != NULL && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOff((xEnt&)*to, toParam[1], toParam[2]);
        }
        break;
    case eEventCollisionOff:
        s->collType = XENT_COLLTYPE_NONE;
        s->chkby = 0;
        if (s->bupdate != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        else
        {
            xEntDefaultBoundUpdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        break;
    case eEventCameraCollidePartial:
        zCollGeom_CamEnable((xEnt*)to);
        s->sflags |= 0x400;
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable((xEnt*)to);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable((xEnt*)to);
        break;
    case eEventReset:
        zEntSimpleObj_Reset((zEntSimpleObj*)to, globals.sceneCur);
        break;
    case eEventSceneBegin:
        if (s->model != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        break;
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    case eEventAnimStop:
    case eEventAnimPause:
    case eEventAnimResume:
    case eEventAnimTogglePause:
    case eEventAnimPlayRandom:
    case eEventAnimPlayMaybe:
        zEntAnimEvent((zEnt*)to, toEvent, toParam);
        break;
    case eEventSetSkyDome:
        xSkyDome_AddEntity((xEnt*)to, toParam[0], toParam[1], toParam[2] == 0.0f);
        break;
    case eEventSetGoo:
        zGooAdd((xEnt*)to, toParam[0], toParam[1]);
        break;
    case eEventGooSetWarb:
        zFXGooEventSetWarb((xEnt*)to, toParam);
        break;
    case eEventGooSetFreezeDuration:
        zFXGooEventSetFreezeDuration((xEnt*)to, toParam[0]);
        break;
    case eEventGooMelt:
        zFXGooEventMelt((xEnt*)to);
        break;
    case eEventSetAsBounceBack:
        s->miscflags |= 0x20;
        break;
    case eEventSetAsBounceBack_Cancel:
        s->miscflags &= (U8)~0x20;
        break;
    case eEventLaunchShrapnel:
        shrap = (zShrapnelAsset*)toParamWidget;
        if (shrap != NULL && shrap->initCB != NULL)
        {
            shrap->initCB(shrap, s->model, NULL, NULL);
        }
        break;
    case eEventDestroy:
        if (s->moreFlags & 0x8)
        {
            zThrown_KillFruit(s);
        }
        if (s->driver != NULL)
        {
            s->driver->driving_count--;
            s->driver = NULL;
        }
        break;
    case eEventSetOpacity:
        if (toParam[0] >= 0.0f && s->model != NULL)
        {
            s->model->Alpha = toParam[0];
        }
        s->opacity.delta_opacity = toParam[1];
        s->opacity.delta_opacity_2 = toParam[2];
        break;
    case eEventSetLightKit:
        if (s->smgr != NULL)
        {
            s->smgr->lkit = (xLightKit*)toParamWidget;
        }
        xModelSetLightKit(s->model, (xLightKit*)toParamWidget);
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr)
        {
            if (toParam[0] <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, s, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                dist.f = SQR(toParam[0]);
                xUpdateCull_SetCB(globals.updateMgr, s, xUpdateCull_DistanceSquaredCB, dist.v);
            }
        }
        break;
    case eEventConnectToChild:
        break;
    case eEventHit:
        break;
    case eEventLaunchFireWorks:
        if (toParam != NULL)
        {
            U32 numFireworks = (toParam[3] == 0.0f) ? 10.0f : toParam[3];
            for (U32 i = 0; i < numFireworks; i++)
            {
                xVec3 position;
                if (s != NULL)
                {
                    position = *xEntGetCenter(s);
                    position -= *(xVec3*)&s->model->Mat->pos;
                    xMat3x3Tolocal(&position, (xMat3x3*)s->model->Mat, &position);
                    position.x += toParam[0];
                    position.y += toParam[1];
                    position.z += toParam[2];
                    xMat4x3Toworld(&position, (xMat4x3*)s->model->Mat, &position);
                }
                else
                {
                    position.x = toParam[0];
                    position.y = toParam[1];
                    position.z = toParam[2];
                }
                xFXFireworksLaunch(0.5f * xurand(), &position, 0.5f * xurand() + 0.1f);
            }
        }
        else
        {
            xEnt* ent = s;
            for (U32 i = 0; i < 10; i++)
            {
                xVec3 position;
                if (ent != NULL)
                {
                    position = *xEntGetCenter(ent);
                }
                else
                {
                    position.assign(0.0f, 0.0f, 0.0f);
                }
                xFXFireworksLaunch(0.5f * xurand(), &position, 0.5f * xurand() + 0.1f);
            }
        }
        break;
    }
}

U8 zEntSimpleObj_UsePartialCameraCollision(zEntSimpleObj* ent)
{
    return (ent->sflags & 0x400) != 0;
}
