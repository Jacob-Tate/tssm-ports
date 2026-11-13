#include "zAssetTypes.h"

// First, so that xCam.h is parsed before xGrid.h (which arrives with xEnt.h): that puts the
// template instances in retail's .text order.
#include "zEntCruiseBubble.h"
#include "xAnim.h"
#include "xMorph.h"
#include "xRand.h"
#include "xSnd.h"
#include "xEnv.h"
#include "xJSP.h"
#include "xLightKit.h"
#include "xMovePoint.h"
#include "xpkrsvc.h"
#include "xstransvc.h"
#include "xString.h"
#include "xSubTitles.h"
#include "xCurveAsset.h"
#include "xHudModel.h"
#include "iModel.h"
#include "zGlobals.h"
#include "zCar.h"
#include "zEntPlayer.h"
#include "zNPC.h"
// Unused here, but retail's includes bring in xFont.h: its screen_bounds and default_adjust are in
// retail .rodata after the zGlobals.h templates.
#include "xFont.h"

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>
#include <stdio.h>
#include <string.h>

static void* Navigation_Mesh_Read(void* userdata, U32 assetid, void* indata, U32 insize,
                                  U32* outsize)
{
    return NULL;
}

inline void* Dash_Track_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    return NULL;
}

static xAnimTable* Anim_ATBL_getTable(xAnimTable* (*func)());
static void* ATBL_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
void* RWTX_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
static void* BSP_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
static void* JSP_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
static void* Model_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
static void* Curve_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
static void Model_Unload(void* userdata, U32 assetid);
static void BSP_Unload(void* userdata, U32 assetid);
static void JSP_Unload(void* userdata, U32 assetid);
static void Anim_Unload(void* userdata, U32 assetid);
static void LightKit_Unload(void* userdata, U32 assetid);
static void MovePoint_Unload(void* userdata, U32 assetid);

static xJSPHeader sDummyEmptyJSP;
static xJSPHeader* sTempJSP;

static PKRAssetType assetTypeHandlers[] = {
    { 'BSP ', 0, 0, BSP_Read, NULL, NULL, NULL, NULL, BSP_Unload },
    { 'JSP ', 0, 0, JSP_Read, NULL, NULL, NULL, NULL, JSP_Unload },
    { 'TXD ' },
    { 'MODL', 0, 0, Model_Read, NULL, NULL, NULL, NULL, Model_Unload },
    { 'ANIM', 0, 0, NULL, NULL, NULL, NULL, NULL, Anim_Unload },
    { 'RWTX', 0, 0, RWTX_Read, NULL, NULL, NULL, NULL, TextureRW3_Unload },
    { 'LKIT', 0, 0, NULL, NULL, NULL, NULL, NULL, LightKit_Unload },
    { 'CAM ' },
    { 'PLYR' },
    { 'NPC ' },
    { 'ITEM' },
    { 'PKUP' },
    { 'TRIG' },
    { 'SDF ' },
    { 'TEX ' },
    { 'TXT ' },
    { 'ENV ' },
    { 'ATBL', 0, 0, ATBL_Read },
    { 'MINF' },
    { 'PICK' },
    { 'PLAT' },
    { 'PEND' },
    { 'MRKR' },
    { 'MVPT', 0, 0, NULL, NULL, NULL, NULL, NULL, MovePoint_Unload },
    { 'TIMR' },
    { 'CNTR' },
    { 'PORT' },
    { 'SND ' },
    { 'SNDS' },
    { 'GRUP' },
    { 'MPHT' },
    { 'SFX ' },
    { 'SNDI' },
    { 'HANG' },
    { 'SIMP' },
    { 'BUTN' },
    { 'SURF' },
    { 'DSTR' },
    { 'BOUL' },
    { 'MAPR' },
    { 'GUST' },
    { 'VOLU' },
    { 'UI  ' },
    { 'UIFT' },
    { 'TEXT' },
    { 'COND' },
    { 'DPAT' },
    { 'PRJT' },
    { 'LOBM' },
    { 'FOG ' },
    { 'LITE' },
    { 'PARE' },
    { 'PARS' },
    { 'CSN ' },
    { 'CTOC' },
    { 'CSNM' },
    { 'EGEN' },
    { 'ALST' },
    { 'RAW ' },
    { 'LODT' },
    { 'SHDW' },
    { 'DYNA' },
    { 'VIL ' },
    { 'VILP' },
    { 'COLL' },
    { 'PARP' },
    { 'PIPT' },
    { 'DSCO' },
    { 'JAW ' },
    { 'SHRP' },
    { 'FLY ' },
    { 'TRCK' },
    { 'CRV ', 0, 0, Curve_Read },
    { 'ZLIN' },
    { 'DUPC' },
    { 'SLID' },
    { 'NGMS', 0, 0, Navigation_Mesh_Read },
    { 'DTRK', 0, 0, Dash_Track_Read },
    { 'ARNA' },
    { 'CRDT' },
    { 'ATKT' },
    { 'ONEL' },
    { 'SGRP' },
    { 'SPLN', 0, 0, x::Spline_Read },
    { 'NPCS' },
    { 'SDFX' },
    { 'UIM ' },
    { 'DEST' },
    { 'SSET' },
    { 'SCRP' },
    { 'PGRS' },
    { 'TEXS' },
    { 'CSSS' },
    { 'SPLP', 0, 0, zAsset::spline_path::init },
    { 'SUBT', 0, 0, xSubTitlesRead, NULL, NULL, NULL, NULL, xSubTitlesUnloaded },
    { 'GRSM' },
    { 'TRWT' },
    { 'RANM' },
    { 'TPIK' },
    { 'WIRE' },
    { 'BINK' },
    { 0 },
};

AnimTableList animTable[8] = {
    { "PlayerSB", zEntPlayer_AnimTable, 0 },
    { "PlayerSBTongue", zSpongeBobTongue_AnimTable, 0 },
    { "PlayerPat", zPatrick_AnimTable, 0 },
    { "PlayerCar", zCar::CreateAnimTable, 0 },
    { "SpongeBall", zEntPlayer_BoulderVehicleAnimTable, 0 },
    { "CruiseBubble", cruise_bubble::anim_table, 0 },
    { "PlayerSlide", zSpongePat_AnimTable, 0 },
    { "xHUD", xHUDCreateAnimTable, 0 },
};

static xAnimTable* (*tableFuncList[])() = { NULL };


inline void ATBL_Init()
{
    for (S32 i = 0; i < 8; i++)
    {
        animTable[i].id = xStrHash(animTable[i].name);
    }
}

void zAssetStartup()
{
    xSTStartup(assetTypeHandlers);
    ATBL_Init();
}

void zAssetShutdown()
{
    xSTShutdown();
}

static void* Model_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    if (indata == NULL)
    {
        return NULL;
    }

    RpAtomic* model = iModelFileNew(indata, insize);
    *outsize = sizeof(RpAtomic);

    RwFrameSetIdentity(RpClumpGetFrame(RpAtomicGetClump(model)));

    return model;
}

static void* Curve_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    xCurveAsset* curve_asset = (xCurveAsset*)indata;
    *outsize = insize;
    curve_asset->points = (F32*)(curve_asset + 1);
    return curve_asset;
}

void* x::Spline_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    NURBS* nurbs = (NURBS*)indata;
    *outsize = insize;
    nurbs->control = (xVec3*)(nurbs + 1);
    nurbs->knot = (F32*)(nurbs->control + nurbs->n + 1);
    return nurbs;
}

void* zAsset::spline_path::init(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    spline_path* path = (spline_path*)indata;
    *outsize = insize;
    path->spline = (x::NURBS*)xSTFindAsset(path->spline_id, NULL);
    path->used = 0;

    U32* forward_ids = (U32*)(path + 1);
    U32* backward_ids = forward_ids + path->forwards_count;
    path->forward_paths = (spline_path**)forward_ids;
    path->backward_paths = path->forward_paths + path->forwards_count;

    for (S32 i = 0; i < path->forwards_count; i++)
    {
        path->forward_paths[i] = (spline_path*)xSTFindAsset(forward_ids[i], NULL);
    }

    for (S32 i = 0; i < path->backwards_count; i++)
    {
        path->backward_paths[i] = (spline_path*)xSTFindAsset(backward_ids[i], NULL);
    }

    return path;
}

static void Model_Unload(void* userdata, U32 assetid)
{
    if (userdata != NULL)
    {
        iModelUnload((RpAtomic*)userdata);
    }
}

static void* BSP_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    RwStream* stream;

    printf("BSP_Read BEGIN\n");

    RwMemory rwmem = { (U8*)indata, insize };
    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
    if (stream == NULL)
    {
        printf("BSP_Read RwStreamOpen failed\n");
    }

    if (!RwStreamFindChunk(stream, rwID_WORLD, NULL, NULL))
    {
        printf("BSP_Read RwStreamFindChunk failed\n");
        RwChunkHeaderInfo chunkHeaderInfo;
        RwStreamReadChunkHeaderInfo(stream, &chunkHeaderInfo);
        *outsize = 0;
        return NULL;
    }

    RpWorld* bsp = RpWorldStreamRead(stream);
    if (bsp == NULL)
    {
        printf("BSP_Read RpWorldStreamRead failed\n");
    }

    RwStreamClose(stream, NULL);
    *outsize = sizeof(RpWorld*);
    printf("BSP_Read END %d\n", *outsize);

    return bsp;
}

static void BSP_Unload(void* userdata, U32 assetid)
{
    xEnvFree(globals.sceneCur->env);
}

static void* JSP_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    xJSPHeader* retjsp = &sDummyEmptyJSP;
    *outsize = sizeof(xJSPHeader);

    xJSP_MultiStreamRead(indata, insize, &sTempJSP, assetid);
    if (sTempJSP->jspNodeList != NULL)
    {
        retjsp = sTempJSP;
        sTempJSP = NULL;
        *outsize = sizeof(xJSPHeader*);
    }

    return retjsp;
}

static void JSP_Unload(void* userdata, U32 assetid)
{
    if ((xJSPHeader*)userdata != &sDummyEmptyJSP)
    {
        xJSP_Destroy((xJSPHeader*)userdata);
    }
}

static RwTexture* TexCB(RwTexture* texture, void* data)
{
    RwTexture** texFound = (RwTexture**)data;
    if (*texFound == NULL)
    {
        *texFound = texture;
    }
    return texture;
}

void* RWTX_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    RwTexDictionary* txd = NULL;
    RwMemory rwmem;
    RwStream* stream = NULL;
    RwTexture* tex = NULL;
    RwError error;

    if (insize != 0)
    {
        rwmem.start = (U8*)indata;
        rwmem.length = insize;
        stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
        if (stream != NULL)
        {
            if (!RwStreamFindChunk(stream, rwID_TEXDICTIONARY, NULL, NULL))
            {
                RwErrorGet(&error);
                RwStreamFindChunk(stream, rwID_TEXDICTIONARY, NULL, NULL);
                RwStreamClose(stream, NULL);
            }
            else
            {
                txd = RwTexDictionaryStreamRead(stream);
                RwStreamClose(stream, NULL);
                if (txd != NULL)
                {
                    RwTexDictionaryForAllTextures(txd, TexCB, &tex);
                    if (tex == NULL)
                    {
                        RwTexDictionaryDestroy(txd);
                    }
                    else
                    {
                        RwTexDictionaryRemoveTexture(tex);
                        RwTexDictionaryDestroy(txd);
                        RwTextureAddRef(tex);
                        RwTextureSetFilterMode(tex, rwFILTERLINEARMIPLINEAR);
                        *outsize = sizeof(RwTexture);
                        return tex;
                    }
                }
            }
        }
    }

    *outsize = insize;
    return NULL;
}

void TextureRW3_Unload(void* userdata, U32 assetid)
{
    RwTexture* tex = (RwTexture*)userdata;
    if (tex != NULL)
    {
        tex->refCount = 1;
        RwTextureDestroy(tex);
    }
}

static U32 dummyEffectCB(U32, xAnimActiveEffect*, xAnimSingle*, void*)
{
    return 0;
}

static U32 soundEffectCB(U32 cbenum, xAnimActiveEffect* acteffect, xAnimSingle* single,
                         void* object)
{
    U32 sndhandle = 0;
    xEnt* ent_tmp = (xEnt*)object;
    zAnimFxSoundGroup* snd;

    if (cbenum == 1)
    {
        if (xrand_RandomChoice(100) < acteffect->Effect->Probability)
        {
            snd = (zAnimFxSoundGroup*)(acteffect->Effect + 1);

            if (ent_tmp != NULL && ent_tmp->baseType == eBaseTypeNPC)
            {
                sndhandle = ((zNPC::base*)ent_tmp)->play_sound(snd);
            }
            else
            {
                sndhandle =
                    xSndMgrPlay(xSndMgrGetSoundGroup(snd->ID), 0, NULL, NULL, ent_tmp, NULL, NULL);
            }
        }
    }

    if (cbenum == 3)
    {
        xSndMgrStop(acteffect->SndHandle);
    }

    return sndhandle;
}

static U32 (*effectFuncList[])(U32, xAnimActiveEffect*, xAnimSingle*, void*) = {
    dummyEffectCB,
    soundEffectCB,
    soundEffectCB,
};

static void* FindAssetCB(U32 ID, char*)
{
    U32 size;
    return xSTFindAsset(ID, &size);
}

static void* ATBL_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    U32 i;
    U32 j;
    U32 debugNum = 0;
    U32 tmpsize;
    xAnimTable* table;
    xAnimState* astate;

    // copy-at-load: ATBL keeps pointers in 4-byte slots (zaRaw[], xAnimAssetFile::RawData and the
    // index array it points to) and they are resolved in place below. A host with 8-byte pointers
    // must widen the asset when it is loaded, before this runs.
    U8* zaBytes = (U8*)indata;
    xAnimAssetTable* zaTbl = (xAnimAssetTable*)zaBytes;
    void** zaRaw = (void**)(zaTbl + 1);
    xAnimAssetFile* zaFile = (xAnimAssetFile*)(zaRaw + zaTbl->NumRaw);
    xAnimAssetState* zaState = (xAnimAssetState*)(zaFile + zaTbl->NumFiles);

    for (i = 0; i < zaTbl->NumRaw; i++)
    {
        zaRaw[i] = xSTFindAsset(*(U32*)&zaRaw[i], &tmpsize);
    }

    for (i = 0; i < zaTbl->NumRaw; i++)
    {
        if (zaRaw[i] == NULL)
        {
            for (j = 0; j < zaTbl->NumRaw; j++)
            {
                if (zaRaw[j] != NULL)
                {
                    zaRaw[i] = zaRaw[j];
                    break;
                }
            }
        }
    }

    for (i = 0; i < zaTbl->NumRaw; i++)
    {
        if (*(U32*)zaRaw[i] == 'QSPM')
        {
            xMorphSeqSetup(zaRaw[i], FindAssetCB);
        }
    }

    for (i = 0; i < zaTbl->NumFiles; i++)
    {
        zaFile[i].RawData = (void**)(zaBytes + (U32)(UPtr)zaFile[i].RawData);

        S32 k;
        S32 num_raw_data = zaFile[i].NumAnims[0] * zaFile[i].NumAnims[1];
        for (k = 0; k < num_raw_data; k++)
        {
            zaFile[i].RawData[k] = zaRaw[(U32)(UPtr)zaFile[i].RawData[k]];
        }
    }

    xAnimFile** fList = (xAnimFile**)zaFile;
    const char* animation_name = "";
    for (i = 0; i < zaTbl->NumFiles; i++)
    {
        fList[i] = xAnimFileNewBilinear(zaFile[i].RawData, animation_name, 0, zaFile[i].FileFlags,
                                        NULL, zaFile[i].NumAnims[0], zaFile[i].NumAnims[1]);
        if (zaFile[i].TimeOffset >= 0.0f)
        {
            xAnimFileSetTime(fList[i], zaFile[i].Duration, zaFile[i].TimeOffset);
        }
    }

    xAnimTable* (*constructor)() = NULL;
    if (zaTbl->ConstructFunc < sizeof(tableFuncList) / sizeof(tableFuncList[0]))
    {
        constructor = tableFuncList[zaTbl->ConstructFunc];
    }
    else
    {
        for (S32 i = 0; i < sizeof(animTable) / sizeof(animTable[0]); i++)
        {
            if (zaTbl->ConstructFunc == animTable[i].id)
            {
                constructor = animTable[i].constructor;
                break;
            }
        }
    }

    gxAnimUseGrowAlloc = true;

    if (constructor == NULL)
    {
        table = zNPC::create_anim_table(zaTbl->ConstructFunc);
    }
    else
    {
        table = Anim_ATBL_getTable(constructor);
    }

    for (i = 0; i < zaTbl->NumStates; i++)
    {
        astate = xAnimTableAddFileID(table, fList[zaState[i].FileIndex], zaState[i].StateID,
                                     zaState[i].SubStateID, zaState[i].SubStateCount);
        if (astate == NULL)
        {
            xAnimTransition* atran;
            char* tmpstr = (char*)xMemAllocSize(8);
            sprintf(tmpstr, "Debug%02d", debugNum++);
            astate = xAnimTableNewState(table, tmpstr, 0x20, 0x80000000, 1.0f, NULL, NULL, 0.0f,
                                        NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
            atran = xAnimTableNewTransition(table, tmpstr, NULL, NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                                            0, 0.2f, NULL);
            atran->Dest = table->StateList;
            xAnimTableAddFileID(table, fList[zaState[i].FileIndex], astate->ID, 0, 0);
        }
        astate->Speed = zaState[i].Speed;
    }

    xAnimFile* foundFile = NULL;
    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (foundFile == NULL && astate->Data != NULL)
        {
            foundFile = astate->Data;
        }
    }

    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (astate->Data == NULL)
        {
            astate->Data = foundFile;
            astate->UserFlags |= 0x40000000;
        }
    }

    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (astate->MultiFile != NULL)
        {
            xAnimState* endState = NULL;
            xAnimFile* endPose;

            if (astate->Default != NULL)
            {
                endState = astate->Default->T->Dest;
            }

            for (U32 i = 0; i < astate->MultiFile->Count; i++)
            {
                xAnimFile* file = astate->MultiFile->Files[i].File;
                endPose = NULL;

                if (endState != NULL)
                {
                    if (endState->MultiFile != NULL)
                    {
                        for (U32 j = 0; j < endState->MultiFile->Count; j++)
                        {
                            if (astate->MultiFile->Files[i].ID == endState->MultiFile->Files[j].ID)
                            {
                                endPose = endState->MultiFile->Files[j].File;
                                break;
                            }
                        }
                    }
                    else
                    {
                        endPose = endState->Data;
                    }
                }

                if (file->FileFlags & 0xF0000)
                {
                    xAnimCalcPhysicsData(file, endPose);
                }
            }
        }
        else if (astate->Data->FileFlags & 0xF0000)
        {
            xAnimFile* endPose = NULL;

            if (astate->Default != NULL)
            {
                endPose = astate->Default->T->Dest->Data;
            }

            xAnimCalcPhysicsData(astate->Data, endPose);
        }
    }

    for (i = 0; i < zaTbl->NumStates; i++)
    {
        if (zaState[i].EffectCount != 0)
        {
            xAnimState* state = xAnimTableGetStateID(table, zaState[i].StateID);
            xAnimAssetEffect* zaEffect = (xAnimAssetEffect*)(zaBytes + zaState[i].EffectOffset);

            if (state != NULL)
            {
                for (j = 0; j < zaState[i].EffectCount; j++)
                {
                    xAnimEffect* effect =
                        xAnimStateNewEffect(state, zaEffect->Flags, zaEffect->Probability,
                                            zaEffect->StartTime, zaEffect->EndTime,
                                            effectFuncList[zaEffect->EffectType],
                                            zaEffect->UserDataSize);
                    memcpy(effect + 1, zaEffect + 1, zaEffect->UserDataSize);
                    zaEffect = (xAnimAssetEffect*)((UPtr)zaEffect + zaEffect->UserDataSize +
                                                   sizeof(xAnimAssetEffect));
                }
            }
        }
    }

    gxAnimUseGrowAlloc = false;
    *outsize = sizeof(xAnimTable);
    return table;
}

static void Anim_Unload(void* userdata, U32 assetid)
{
}

static void LightKit_Unload(void* userdata, U32 assetid)
{
    xLightKit_Destroy((xLightKit*)userdata);
}

static xAnimTable* Anim_ATBL_getTable(xAnimTable* (*func)())
{
    return func();
}

static void MovePoint_Unload(void* userdata, U32 assetid)
{
    xMovePointSplineDestroy((xMovePoint*)userdata);
}
