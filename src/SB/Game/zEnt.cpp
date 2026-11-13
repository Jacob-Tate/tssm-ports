// xDebugTweak.h first: retail .rodata of this unit opens with its seven tweak_callback templates.
#include "xDebugTweak.h"
#include "zEnt.h"

#include <types.h>

#include "zGlobals.h"
#include "zBase.h"
#include "xstransvc.h"
#include "xString.h"
#include "xRand.h"
#include "xutil.h"
#include "iModel.h"
#include "xMathInlines.h"
#include "zNPC.h"
#include "zAnimList.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

U32 g_hash_xentanim[5] = { 0 };
char* g_strz_xentanim[5] = { "Idle01", "Anim02", "Anim03", "Anim04", "Anim05" };
_ShadowParams gShadowParams[1] = { { 0, 0.0f, 0.0f } };

inline void zEntEventAllOfType(xBase* from, U32 fromEvent, U32 toEvent, F32* toParam, U32 type)
{
    zScene* s = globals.sceneCur;
    if (s == NULL)
    {
        return;
    }

    for (U16 i = 0; i < s->num_base; i++)
    {
        xBase* base = s->base[i];
        if (base != NULL && type == base->baseType)
        {
            zEntEvent(from, fromEvent, base, toEvent, toParam, NULL, 0, FE_NO);
        }
    }
}

void zEntInit(zEnt* ent, xEntAsset* asset, U32 type)
{
    S32 num_used;
    xAnimState* ast;
    xAnimSingle* single;

    xEntInit(ent, asset);
    ent->update = (xEntUpdateCallback)zEntUpdate;

    if (type == 'PLYR')
    {
        ent->collType = 0x10;
        ent->collLev = 0x4;
        ent->bound.type = 0x1;
        zEntParseModelInfo(ent, asset->modelInfoID);
    }
    else if (type == 'VIL ')
    {
        ent->collType = 0x8;
        ent->collLev = 0x4;
        ent->bound.type = 0x1;
        ent->moreFlags |= 0x10;
        zEntParseModelInfo(ent, asset->modelInfoID);
    }
    else if (type == 'ITEM')
    {
        ent->collType = 0x2;
        ent->collLev = 0x4;
        ent->bound.type = 0x1;
        ent->eventFunc = NULL;
        zEntParseModelInfo(ent, asset->modelInfoID);
    }
    else if (type == 'PKUP')
    {
        ent->collType = 0;
        ent->bound.type = 0;
    }
    else if (type == 'ZLIN')
    {
        ent->collType = 0x4;
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.type = 0x4;
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'PLAT')
    {
        ent->collType = 0x4;
        if (asset->moreFlags & 2)
        {
            ent->collLev = 0x5;
        }
        else
        {
            ent->collLev = 0x4;
        }
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.type = 0x4;
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'PEND')
    {
        ent->collType = 0x4;
        if (asset->moreFlags & 2)
        {
            ent->collLev = 0x5;
        }
        else
        {
            ent->collLev = 0x4;
        }
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.type = 0x4;
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'TRIG')
    {
        ent->collType = 0x1;
        ent->bound.type = 0;
    }
    else if (type == 'HANG')
    {
        ent->collType = 0x4;
        ent->collLev = 0x4;
        ent->bound.type = 0x1;
        zEntParseModelInfo(ent, asset->modelInfoID);
    }
    else if (type == 'SIMP' || type == 'TRCK')
    {
        ent->collType = 0x2;
        if (asset->moreFlags & 2)
        {
            ent->collLev = 0x5;
        }
        else
        {
            ent->collLev = 0x4;
        }
        ent->bound.type = 0x4;
        zEntParseModelInfo(ent, asset->modelInfoID);
        if (ent->model == NULL)
        {
            return;
        }
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'UI  ')
    {
    }
    else if (type == 'BUTN')
    {
        ent->collType = 0x4;
        if (asset->moreFlags & 2)
        {
            ent->collLev = 0x5;
        }
        else
        {
            ent->collLev = 0x4;
        }
        ent->bound.type = 0x4;
        ent->moreFlags |= 0x10;
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'DSTR')
    {
        ent->collType = 0x4;
        if (asset->moreFlags & 2)
        {
            ent->collLev = 0x5;
        }
        else
        {
            ent->collLev = 0x4;
        }
        ent->bound.type = 0x4;
        ent->moreFlags |= 0x10;
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'EGEN')
    {
        ent->collType = 0x4;
        if (asset->moreFlags & 2)
        {
            ent->collLev = 0x5;
        }
        else
        {
            ent->collLev = 0x4;
        }
        ent->bound.type = 0x4;
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }
    else if (type == 'TBOX')
    {
        ent->collType = 0x2;
        ent->collLev = 0x5;
        ent->bound.type = 0x4;
        zEntParseModelInfo(ent, asset->modelInfoID);
        ent->bound.mat = (xMat4x3*)ent->model->Mat;
    }

    if (asset->animListID != 0)
    {
        num_used = zAnimListGetNumUsed(asset->animListID);
        if (num_used > 0)
        {
            ent->atbl = zAnimListGetTable(asset->animListID);
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, ent->atbl, ent->model);

            ast = xAnimTableGetState(ent->atbl, "idle");
            if (ast != NULL)
            {
                single = ent->model->Anim->Single;
                xAnimPlaySetState(single, ast, 0.0f);
                single->CurrentSpeed = 1.0f;
                xModelEval(ent->model);
            }
        }
        else
        {
            ent->atbl = NULL;
        }
    }
    else
    {
        ent->atbl = NULL;
    }

    xEntInitForType(ent);
}

inline void checkpoint_collision_hack(zEnt* ent)
{
    static U32 model_id = 0x8d398d0c; // xStrHash("checkpoint_bind")

    if (ent->asset->modelInfoID == model_id)
    {
        ent->bound.type = XBOUND_TYPE_BOX;
        xVec3* lower = &ent->bound.box.box.lower;
        xVec3* upper = &ent->bound.box.box.upper;
        *lower = *upper = xEntGetFrame(ent)->pos;

        lower->x += -0.5f;
        lower->z += -0.5f;
        upper->x += 0.5f;
        upper->y += 5.0f;
        upper->z += 0.5f;

        xEntDefaultBoundUpdate(ent, &xEntGetFrame(ent)->pos);

        ent->miscflags |= 0x8;
        ent->baseFlags &= (U16)~0x10;
        ent->asset->baseFlags &= (U16)~0x10;
    }
}

void zEntSetup(zEnt* ent)
{
    xEntSetup(ent);
    checkpoint_collision_hack(ent);
}

void zEntSave(zEnt* ent, xSerial* s)
{
    xEntSave(ent, s);
}

void zEntLoad(zEnt* ent, xSerial* s)
{
    xEntLoad(ent, s);
}

void zEntReset(zEnt* ent)
{
    xAnimState* ast;
    xAnimSingle* single;

    xEntReset(ent);

    if (ent->asset->animListID != 0 && ent->atbl != NULL)
    {
        ast = xAnimTableGetState(ent->atbl, "idle");
        if (ast != NULL && ent->model->Anim != NULL)
        {
            single = ent->model->Anim->Single;
            single->State = ast;
            single->Time = 0.0f;
            single->CurrentSpeed = 1.0f;
            xModelEval(ent->model);
        }
    }

    if (!(ent->miscflags & 0x1) && ent->asset->modelInfoID != 0 && ent->model != NULL &&
        ent->model->Anim != NULL && ent->model->Anim->Table != NULL &&
        strcmp("xEntAutoEventSimple", ent->model->Anim->Table->Name) == 0)
    {
        xAnimPlaySetState(ent->model->Anim->Single, ent->model->Anim->Table->StateList, 0.0f);
        ent->miscflags |= 0x1;
    }

    checkpoint_collision_hack(ent);
}

void zEntUpdate(zEnt* ent, zScene* scene, F32 elapsedSec)
{
    xEntUpdate(ent, scene, elapsedSec);
}

void zEntEventAll(xBase* from, U32 fromEvent, U32 toEvent, F32* toParam)
{
    zScene* s = globals.sceneCur;

    for (U16 i = 0; i < s->num_base; i++)
    {
        if (s->base[i] != NULL && s->base[i]->baseType != eBaseTypeGroup)
        {
            zEntEvent(from, fromEvent, s->base[i], toEvent, toParam, NULL, 0, FE_NO);
        }
    }
}

void zEntEventAllOfType(U32 toEvent, U32 type)
{
    zEntEventAllOfType(NULL, 0, toEvent, NULL, type);
}

xModelInstance* zEntRecurseModelInfo(void* info, xEnt* ent)
{
    U32 i;
    U32 bufsize;
    RpAtomic* imodel;
    xModelInstance* tempInst[64];
    xModelAssetInfo* zinfo = (xModelAssetInfo*)info;
    xModelAssetInst* zinst = (xModelAssetInst*)(zinfo + 1);
    xAnimTable* table;

    memset(tempInst, 0, sizeof(tempInst));

    for (i = 0; i < zinfo->NumModelInst; i++)
    {
        imodel = (RpAtomic*)xSTFindAsset(zinst[i].ModelID, &bufsize);

        if (*(U32*)imodel == 0x464e494d)
        {
            tempInst[i] = zEntRecurseModelInfo(imodel, ent);
            if (i != 0)
            {
                tempInst[i]->Flags |= zinst[i].Flags;
                tempInst[i]->BoneIndex = zinst[i].Bone;
                xModelInstanceAttach(tempInst[i], tempInst[zinst[i].Parent]);
            }
        }
        else if (i == 0)
        {
            tempInst[i] = xModelInstanceAlloc(imodel, ent, 0, 0, NULL);
            tempInst[i]->modelID = zinst[i].ModelID;

            while ((imodel = iModelFile_RWMultiAtomic(imodel)) != NULL)
            {
                xModelInstanceAttach(xModelInstanceAlloc(imodel, ent, 0x2000, 0, NULL),
                                     tempInst[i]);
            }
        }
        else
        {
            tempInst[i] = xModelInstanceAlloc(imodel, ent, zinst[i].Flags, zinst[i].Bone, NULL);
            xModelInstanceAttach(tempInst[i], tempInst[zinst[i].Parent]);

            while ((imodel = iModelFile_RWMultiAtomic(imodel)) != NULL)
            {
                xModelInstanceAttach(xModelInstanceAlloc(imodel, ent, 0x2000, 0, NULL),
                                     tempInst[i]);
            }
        }
    }

    if (zinfo->AnimTableID != 0)
    {
        table = (xAnimTable*)xSTFindAsset(zinfo->AnimTableID, &bufsize);
        tempInst[0]->Anim = xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, tempInst[0]);
    }

    return tempInst[0];
}

void zEntParseModelInfo(xEnt* ent, U32 assetID)
{
    U32 bufsize;
    void* info = xSTFindAsset(assetID, &bufsize);

    if (info != NULL)
    {
        if (*(U32*)info == 0x464e494d)
        {
            ent->model = zEntRecurseModelInfo(info, ent);
        }
        else
        {
            xEntLoadModel(ent, (RpAtomic*)info, NULL);
            ent->model->modelID = assetID;
        }
    }
}

void zEntAnimEventCore(xModelInstance* model, xAnimTable* atbl, U32 animEvent, const F32* animParam,
                       xEnt* ent)
{
    xAnimPlay* play = model->Anim;
    if (play == NULL)
    {
        return;
    }

    xAnimSingle* single = play->Single;
    if (single == NULL)
    {
        return;
    }

    switch (animEvent)
    {
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    {
        if (animParam == NULL)
        {
            break;
        }

        S32 anum = (S32)animParam[0] - 1;
        if (anum < 0 || anum >= 10 || atbl == NULL)
        {
            break;
        }

        char name[12];
        if (animEvent == eEventAnimPlayLoop)
        {
            sprintf(name, "loop%d", anum);
        }
        else
        {
            sprintf(name, "stop%d", anum);
        }

        xAnimState* ast = xAnimTableGetState(atbl, name);
        if (ast == NULL)
        {
            break;
        }

        xAnimPlaySetState(single, ast, 0.0f);
        if (animParam[2] == 0.0f)
        {
            ast->Speed = single->CurrentSpeed = 1.0f;
        }
        else
        {
            ast->Speed = single->CurrentSpeed = animParam[2];
        }
        xAnimPlayUpdate(play, 0.0f);
        xAnimPlayEval(play);
        break;
    }
    case eEventAnimStop:
    {
        char name[12];
        strcpy(name, single->State->Name);
        name[0] = 's';
        name[1] = 't';
        xAnimState* ast = xAnimTableGetState(atbl, name);
        single->State = ast;
        break;
    }
    case eEventAnimPause:
        single->CurrentSpeed = 0.0f;
        break;
    case eEventAnimResume:
        single->CurrentSpeed = 1.0f;
        break;
    case eEventAnimTogglePause:
        if (single->CurrentSpeed)
        {
            single->CurrentSpeed = 0.0f;
        }
        else
        {
            single->CurrentSpeed = 1.0f;
        }
        break;
    case eEventAnimPlayRandom:
    {
        if (animParam == NULL)
        {
            break;
        }

        S32 anum1 = (S32)animParam[0] - 1;
        S32 anum2 = (S32)animParam[1] - 1;
        if (anum1 < 0 || anum1 > anum2 || anum2 >= 10 || atbl == NULL)
        {
            break;
        }

        char name[12];
        sprintf(name, "stop%d", anum1 + (S32)xrand_RandomInt32() % (anum2 - anum1 + 1));

        xAnimState* ast = xAnimTableGetState(atbl, name);
        if (ast == NULL)
        {
            break;
        }

        xAnimPlaySetState(single, ast, 0.0f);
        single->CurrentSpeed = 1.0f;
        xAnimPlayUpdate(play, 0.0f);
        xAnimPlayEval(play);
        break;
    }
    case eEventAnimPlayMaybe:
    {
        if (animParam == NULL)
        {
            break;
        }

        S32 anum = (S32)animParam[0] - 1;
        F32 prob = 0.01f * animParam[1];
        if (anum >= 0 && anum < 10 && xurand() < prob && atbl != NULL)
        {
            char name[12];
            sprintf(name, "stop%d", anum);

            xAnimState* ast = xAnimTableGetState(atbl, name);
            if (ast != NULL)
            {
                single->CurrentSpeed = 1.0f;
                xAnimPlaySetState(single, ast, 0.0f);
                xAnimPlayUpdate(play, 0.0f);
                xAnimPlayEval(play);
            }
        }
        break;
    }
    }
}

void zEntAnimEvent(zEnt* ent, U32 animEvent, const F32* animParam)
{
    if (ent->miscflags & 0x1)
    {
        zEntAnimEvent_AutoAnim(ent, animEvent, animParam);
        return;
    }

    zEntAnimEventCore(ent->model, ent->atbl, animEvent, animParam, ent);
}

void zEntAnimEvent_AutoAnim(zEnt* ent, U32 animEvent, const F32* animParam)
{
    xAnimPlay* play = ent->model->Anim;
    xAnimSingle* single = play->Single;

    switch (animEvent)
    {
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    {
        if (animParam == NULL)
        {
            break;
        }

        S32 anum = (S32)animParam[0] - 1;
        if (anum < 0 || anum >= 5)
        {
            break;
        }

        xAnimTable* tab = ent->model->Anim->Table;
        if (tab == NULL)
        {
            break;
        }

        xAnimState* ast = xAnimTableGetStateID(tab, g_hash_xentanim[anum]);
        if (ast == NULL)
        {
            break;
        }

        if (anum != 0)
        {
            if (animEvent == eEventAnimPlayLoop)
            {
                ast->Flags &= ~0x20;
                ast->Flags |= 0x10;
            }
            else
            {
                ast->Flags |= 0x20;
                ast->Flags &= ~0x10;
            }
        }

        xAnimPlaySetState(single, ast, 0.0f);
        single->CurrentSpeed = 1.0f;
        xAnimPlayUpdate(play, 0.0f);
        xAnimPlayEval(play);
        break;
    }
    case eEventAnimStop:
    {
        xAnimTable* tab = ent->model->Anim->Table;
        if (tab == NULL)
        {
            break;
        }

        xAnimState* ast = xAnimTableGetStateID(tab, g_hash_xentanim[0]);
        if (ast != NULL)
        {
            xAnimPlaySetState(single, ast, 0.0f);
            single->CurrentSpeed = 0.0f;
            xAnimPlayUpdate(play, 0.0f);
            xAnimPlayEval(play);
        }
        break;
    }
    case eEventAnimPause:
        single->CurrentSpeed = 0.0f;
        break;
    case eEventAnimResume:
        single->CurrentSpeed = 1.0f;
        break;
    case eEventAnimTogglePause:
        if (single->CurrentSpeed > 0.0f)
        {
            single->CurrentSpeed = 0.0f;
        }
        else
        {
            single->CurrentSpeed = 1.0f;
        }
        break;
    case eEventAnimPlayRandom:
    {
        if (animParam == NULL)
        {
            break;
        }

        S32 anum1 = (S32)animParam[0] - 1;
        S32 anum2 = (S32)animParam[1] - 1;
        if (anum1 < 0 || anum2 < 0 || anum1 > anum2 || anum2 >= 5)
        {
            break;
        }

        S32 anum = xrand_RandomInt32();
        anum2 = anum2 - anum1 + 1;
        anum %= anum2;
        anum1 += anum;

        xAnimTable* tab = ent->model->Anim->Table;
        if (tab == NULL)
        {
            break;
        }

        xAnimState* ast = xAnimTableGetStateID(tab, g_hash_xentanim[anum1]);
        if (ast == NULL)
        {
            break;
        }

        if (anum1 != 0)
        {
            ast->Flags |= 0x20;
            ast->Flags &= ~0x10;
        }

        xAnimPlaySetState(single, ast, 0.0f);
        single->CurrentSpeed = 1.0f;
        xAnimPlayUpdate(play, 0.0f);
        xAnimPlayEval(play);
        break;
    }
    case eEventAnimPlayMaybe:
        if (xUtil_yesno(0.01f * animParam[1]))
        {
            zEntAnimEvent_AutoAnim(ent, eEventAnimPlay, animParam);
        }
        break;
    }
}

xModelAssetParam* zEntGetModelParams(U32 assetID, U16* size)
{
    U32 bufsize;
    U32 tempsize;
    void* info = xSTFindAsset(assetID, &bufsize);
    xModelAssetInfo* minf = (xModelAssetInfo*)info;

    if (minf->Magic == 0x464e494d)
    {
        tempsize =
            bufsize - (minf->NumModelInst * sizeof(xModelAssetInst) + sizeof(xModelAssetInfo));
        if (tempsize != 0)
        {
            *size = tempsize;
            return (xModelAssetParam*)((U8*)minf + minf->NumModelInst * sizeof(xModelAssetInst) +
                                       sizeof(xModelAssetInfo));
        }
    }
    else
    {
        *size = 0;
    }

    return NULL;
}

// Not in the GC symbol table: inlined into the const overload below and deadstripped.
char* zParamGetString(xModelAssetParam* param, U32 size, char* tok, char* def)
{
    U32 testhash = xStrHash(tok);

    while (param != NULL && size != 0)
    {
        if (param->HashID == testhash)
        {
            return (char*)param->String;
        }

        size -= param->WordLength * 4 + 8;
        param = (xModelAssetParam*)((U8*)param + (param->WordLength * 4 + 8));
    }

    return def;
}

const char* zParamGetString(xModelAssetParam* param, U32 size, const char* tok, const char* def)
{
    return zParamGetString(param, size, (char*)tok, (char*)def);
}

// zParamGetInt/Float/Vector have no GC symbol: auto-inlined into the auto_tweak::load_param
// instances and dead-stripped. TSSM's int reader also accepts "TRUE".
S32 zParamGetInt(xModelAssetParam* param, U32 size, const char* tok, S32 def)
{
    return zParamGetInt(param, size, (char*)tok, def);
}

S32 zParamGetInt(xModelAssetParam* param, U32 size, char* tok, S32 def)
{
    char* str = zParamGetString(param, size, tok, NULL);
    if (str == NULL)
    {
        return def;
    }

    if (strcmp(xStrupr(str), "TRUE") == 0)
    {
        return 1;
    }
    return atoi(str);
}

F32 zParamGetFloat(xModelAssetParam* param, U32 size, const char* tok, F32 def)
{
    return zParamGetFloat(param, size, (char*)tok, def);
}

F32 zParamGetFloat(xModelAssetParam* param, U32 size, char* tok, F32 def)
{
    char* str = zParamGetString(param, size, tok, NULL);
    if (str != NULL)
    {
        return xatof(str);
    }
    return def;
}

S32 zParamGetFloatList(xModelAssetParam* param, U32 size, const char* tok, S32 count, F32* def,
                       F32* result)
{
    return zParamGetFloatList(param, size, (char*)tok, count, def, result);
}

S32 zParamGetFloatList(xModelAssetParam* param, U32 size, char* tok, S32 count, F32* def,
                       F32* result)
{
    char* str = zParamGetString(param, size, tok, NULL);
    S32 act = 0;

    if (def != NULL)
    {
        for (S32 i = 0; i < count; i++)
        {
            result[i] = def[i];
        }
    }

    if (str != NULL)
    {
        act = xStrParseFloatList(result, str, count);
    }
    return act;
}

S32 zParamGetVector(xModelAssetParam* param, U32 size, const char* tok, xVec3 def, xVec3* result)
{
    return zParamGetVector(param, size, (char*)tok, def, result);
}

S32 zParamGetVector(xModelAssetParam* param, U32 size, char* tok, xVec3 def, xVec3* result)
{
    char* str = zParamGetString(param, size, tok, NULL);
    S32 list = 0;
    xVec3 vec = { 0 };

    xVec3Copy(result, &def);
    if (str != NULL)
    {
        list = xStrParseFloatList((F32*)&vec, str, 3);
        if (list > 0)
        {
            result->x = vec.x;
        }

        if (list > 1)
        {
            result->y = vec.y;
        }

        if (list > 2)
        {
            result->z = vec.z;
        }
    }
    return list;
}

void zEntGetShadowParams(xEnt* ent, xVec3* center, F32* radius, F32* strength,
                         xEntShadow::radius_enum rtype)
{
    xBox* bbox;
    F32 cacheFudgeFactor;
    F32 r;
    xVec3* lower;
    xVec3* upper;

    if (ent->baseType == eBaseTypeNPC)
    {
        *strength = ((zNPC::base*)ent)->shadow_strength;
    }
    else
    {
        *strength = 0.3f;
    }

    if (ent->model->BoneCount != 0)
    {
        lower = &ent->model->combinedAnimBound.lower;
        upper = &ent->model->combinedAnimBound.upper;

        *radius = MAX(0.5f * (upper->y - lower->y),
                      0.5f * xsqrt(SQR(upper->x - lower->x) + SQR(upper->z - lower->z)));

        cacheFudgeFactor = 1.5f;
        center->x = 0.5f * (upper->x + lower->x);
        center->y = 0.5f * (upper->y + lower->y);
        center->z = 0.5f * (upper->z + lower->z);

        if (ent->baseType == eBaseTypeNPC)
        {
            cacheFudgeFactor = ((zNPC::base*)ent)->shadow_cache_fudge_factor;
        }

        if (ent->model->modelID == 0x9c407ff9)
        {
            *radius *= 1.9f;
        }

        if (ent->model->modelID == 0xcd4fb987)
        {
            *radius *= 1.5f;
            *strength *= 0.5f;
        }

        if (rtype == xEntShadow::RADIUS_CACHE)
        {
            *radius *= cacheFudgeFactor;
        }
        else
        {
            *radius *= 1.016f;
        }
    }
    else
    {
        *center = ent->bound.sph.center;

        if (ent->entShadow != NULL && ent->entShadow->radius[rtype] > 0.0f)
        {
            *radius = ent->entShadow->radius[rtype];
        }
        else
        {
            if (ent->bound.type == XBOUND_TYPE_SPHERE)
            {
                r = ent->bound.sph.r;
            }
            else
            {
                bbox = &ent->bound.box.box;
                r = 0.167f * (bbox->upper.x + bbox->upper.y + bbox->upper.z - bbox->lower.x -
                              bbox->lower.y - bbox->lower.z);
            }

            *radius = (r < 0.01f) ? 2.0f : 2.4f * r;

            if (ent == xglobals->___player_ent_dont_use_directly)
            {
                *radius *= 2.0f;
            }
        }
    }
}
