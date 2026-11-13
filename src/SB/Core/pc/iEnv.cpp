#include "iEnv.h"
#include "iCamera.h"
#include "iMath3.h"
#include "xMemMgr.h"
#include "xRenderState.h"
#include "zJSPExtraData.h"
#include "zMain.h"

static S32 sBeginDrawFX;
static RpWorld* sPipeWorld;
static RwCamera* sPipeCamera;
static iEnv* lastEnv;

static RpAtomic* SetPipelineCB(RpAtomic* atomic, void* data)
{
    if (data)
    {
        atomic->pipeline = (RxPipeline*)data;
    }

    return atomic;
}

static inline void iBuildJSPBound(const xClumpCollBSPTree* colltree, xBox& box);

void iEnvLoadBegin(iEnv* env, S32 dataType, S32 count)
{
    if (dataType == 0)
    {
        env->jsp_aid = (U32*)xMemAllocSize(count * sizeof(U32));
        env->jsp_list = (xJSPHeader**)xMemAllocSize(count * sizeof(xJSPHeader*));
        env->jsp_bound = (xBox*)xMemAllocSize(count * sizeof(xBox));
        env->jsp_count = count;
        env->jsp_visibilityCount = (S32*)xMemAllocSize(count * sizeof(S32));

        RwBBox tmpbbox = { 1000.0f, 1000.0f, 1000.0f, -1000.0f, -1000.0f, -1000.0f };

        env->world = RpWorldCreate(&tmpbbox);

        sPipeCamera = iCameraCreate(FB_XRES, FB_YRES, 0);
        sPipeWorld = env->world;

        RpWorldAddCamera(sPipeWorld, sPipeCamera);
    }
}

bool iEnvLoadJSP(iEnv* env, U32 aid, const void* data, U32, S32 dataType, S32 index)
{
    xJSPHeader* jsp = (xJSPHeader*)data;

    if (jsp->idtag[0] == 'J' && jsp->idtag[1] == 'S' && jsp->idtag[2] == 'P' &&
        jsp->idtag[3] == '\0' && dataType == 0)
    {
        env->jsp_aid[index] = aid;
        env->jsp_list[index] = jsp;
        env->jsp_visibilityCount[index] = 0;

        RpClumpForAllAtomics(env->jsp_list[index]->clump, SetPipelineCB, NULL);
        xClumpColl_InstancePointers(env->jsp_list[index]->colltree, env->jsp_list[index]->clump);

        iBuildJSPBound(env->jsp_list[index]->colltree, env->jsp_bound[index]);

        return true;
    }

    return false;
}

void iEnvJSPVisibilityInc(iEnv* env, xJSPHeader* jsp)
{
    for (S32 i = 0; i < env->jsp_count; i++)
    {
        if (env->jsp_list[i] == jsp)
        {
            if (env->jsp_visibilityCount[i] == 0)
            {
                zJSPExtraData::ShowAssociatedGroup(env->jsp_aid[i], true);
            }

            env->jsp_visibilityCount[i]++;
            return;
        }
    }
}

void iEnvJSPVisibilityDec(iEnv* env, xJSPHeader* jsp)
{
    for (S32 i = 0; i < env->jsp_count; i++)
    {
        if (env->jsp_list[i] == jsp)
        {
            if (env->jsp_visibilityCount[i] == 1)
            {
                zJSPExtraData::ShowAssociatedGroup(env->jsp_aid[i], false);
            }

            env->jsp_visibilityCount[i]--;
            return;
        }
    }
}

void iEnvJSPOn(iEnv* env, xJSPHeader* jsp)
{
    for (S32 i = 0; i < env->jsp_count; i++)
    {
        if (env->jsp_list[i] == jsp)
        {
            if (env->jsp_visibilityCount[i] == 0)
            {
                zJSPExtraData::ShowAssociatedGroup(env->jsp_aid[i], true);
            }

            env->jsp_visibilityCount[i] = 1;
            return;
        }
    }
}

void iEnvJSPOff(iEnv* env, xJSPHeader* jsp)
{
    for (S32 i = 0; i < env->jsp_count; i++)
    {
        if (env->jsp_list[i] == jsp)
        {
            if (env->jsp_visibilityCount[i] == 1)
            {
                zJSPExtraData::ShowAssociatedGroup(env->jsp_aid[i], false);
            }

            env->jsp_visibilityCount[i] = 0;
            return;
        }
    }
}

static inline void iBuildJSPBound(const xClumpCollBSPTree* colltree, xBox& box)
{
    for (S32 j = 0; j < colltree->numTriangles; j++)
    {
        for (S32 k = 0; k < 3; k++)
        {
            iBoxBoundVec(&box, &box, (const xVec3*)&colltree->triangles[j].v.p[k]);
        }
    }
}

void iEnvLoadEnd(iEnv* env, S32 dataType)
{
    if (dataType == 0)
    {
        RpWorldRemoveCamera(sPipeWorld, sPipeCamera);
        iCameraDestroy(sPipeCamera);

        sPipeWorld = NULL;
        sPipeCamera = NULL;

        env->memlvl = xMemGetBase();
    }
}

void iEnvFree(iEnv* env)
{
    _rwFrameSyncDirty();

    RpWorldDestroy(env->world);
    env->world = NULL;

    if (env->fx)
    {
        RpWorldDestroy(env->fx);
        env->fx = NULL;
    }

    if (env->collision)
    {
        RpWorldDestroy(env->collision);
        env->collision = NULL;
    }
}

void iEnvDefaultLighting(iEnv*)
{
}

void iEnvLightingBasics(iEnv*, xEnvAsset*)
{
}

// This is named JspPS2_ClumpRender on PS2
static void Jsp_ClumpRender(RpClump* clump, xJSPNodeInfo* nodeInfo, S32)
{
    S32 backcullon = 1;
    S32 zbufferon = 1;
    RwLLLink* cur = rwLinkListGetFirstLLLink(&clump->atomicList);
    RwLLLink* end = rwLinkListGetTerminator(&clump->atomicList);

    while (cur != end)
    {
        RpAtomic* apAtom = rwLLLinkGetData(cur, RpAtomic, inClumpLink);

        if (RpAtomicGetFlags(apAtom) & rpATOMICRENDER)
        {
            RwFrame* frame = RpAtomicGetFrame(apAtom);

            if (!xModelSphereCull(iModelWorldBoundSphere(apAtom, &frame->ltm, NULL)))
            {
                U8 alphaDiscard;

                if (backcullon)
                {
                    if (nodeInfo->nodeFlags & 0x4)
                    {
                        backcullon = 0;
                        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
                    }
                }
                else
                {
                    if (!(nodeInfo->nodeFlags & 0x4))
                    {
                        backcullon = 1;
                        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
                    }
                }

                if (zbufferon)
                {
                    if (nodeInfo->nodeFlags & 0x2)
                    {
                        zbufferon = 0;
                        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
                    }
                }
                else
                {
                    if (!(nodeInfo->nodeFlags & 0x2))
                    {
                        zbufferon = 1;
                        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
                    }
                }

                switch ((nodeInfo->nodeFlags & 0x2) ? 0 : (nodeInfo->nodeFlags >> 14) & 0x3)
                {
                case 0:
                    alphaDiscard = 1;
                    break;
                case 1:
                    alphaDiscard = 100;
                    break;
                case 2:
                    alphaDiscard = 150;
                    break;
                case 3:
                    alphaDiscard = 200;
                    break;
                }

                if (alphaDiscard)
                {
                    xRenderStateSetAlphaDiscard(alphaDiscard);
                }

                RpAtomicRender(apAtom);

                if (alphaDiscard)
                {
                    xRenderStateResetAlphaDiscard();
                }
            }
        }

        cur = rwLLLinkGetNext(cur);
        nodeInfo++;
    }

    if (!backcullon)
    {
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    }

    if (!zbufferon)
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    }
}

void iEnvSetup(iEnv* env)
{
    lastEnv = env;
}

void iEnvRender(iEnv* env, bool alpha)
{
    S32 i;

    if (!alpha)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

        if (env->jsp_count > 0)
        {
            for (i = 0; i < env->jsp_count; i++)
            {
                if (env->jsp_visibilityCount[i] > 0)
                {
                    Jsp_ClumpRender(env->jsp_list[i]->clump, env->jsp_list[i]->jspNodeList, alpha);
                }
            }
        }
        else
        {
            RpWorldRender(env->world);
        }

        lastEnv = env;
    }
}

void iEnvEndRenderFX(iEnv*)
{
    iEnv* env = lastEnv;

    if (lastEnv && env->fx && globalCamera && sBeginDrawFX)
    {
        RpWorldRemoveCamera(env->fx, globalCamera);
        RpWorldAddCamera(env->world, globalCamera);

        sBeginDrawFX = 0;
    }
}
