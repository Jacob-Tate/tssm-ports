#include "zNPCMgr.h"

#include "xString.h"
#include "xstransvc.h"
#include "zNMECommon.h"

S32 g_modinit;
zNPCMgr* g_npcmgr;
NPCBrainTableEntry g_brainTable[67] = {
    { "NME_TYPE_COMMON", NME_TYPE_COMMON, 0 },
    { "NME_TYPE_TIKI", NME_TYPE_TIKI, 0 },
    { "NME_TYPE_TIKI_WOOD", NME_TYPE_TIKI_WOOD, 0 },
    { "NME_TYPE_TIKI_LOVEY", NME_TYPE_TIKI_LOVEY, 0 },
    { "NME_TYPE_TIKI_QUIET", NME_TYPE_TIKI_QUIET, 0 },
    { "NME_TYPE_TIKI_THUNDER", NME_TYPE_TIKI_THUNDER, 0 },
    { "NME_TYPE_TIKI_STONE", NME_TYPE_TIKI_STONE, 0 },
    { "NME_TYPE_CRITTER", NME_TYPE_CRITTER, 0 },
    { "NME_TYPE_CRITBASIC", NME_TYPE_CRITBASIC, 0 },
    { "NME_TYPE_CRITJELLY", NME_TYPE_CRITJELLY, 0 },
    { "NME_TYPE_BUCKETJELLY", NME_TYPE_BUCKETJELLY, 0 },
    { "NME_TYPE_TURRET", NME_TYPE_TURRET, 0 },
    { "NME_TYPE_TURBARREL_V1", NME_TYPE_TURBARREL_V1, 0 },
    { "NME_TYPE_TURBARREL_V2", NME_TYPE_TURBARREL_V2, 0 },
    { "NME_TYPE_TURBARREL_V3", NME_TYPE_TURBARREL_V3, 0 },
    { "NME_TYPE_TURSPIRAL", NME_TYPE_TURSPIRAL, 0 },
    { "NME_TYPE_TURPOPUP", NME_TYPE_TURPOPUP, 0 },
    { "NME_TYPE_TURTURNER", NME_TYPE_TURTURNER, 0 },
    { "NME_TYPE_TURARTY", NME_TYPE_TURARTY, 0 },
    { "NME_TYPE_TURTRACE", NME_TYPE_TURTRACE, 0 },
    { "NME_TYPE_STANDARD", NME_TYPE_STANDARD, 0 },
    { "NME_TYPE_TURBARREL", NME_TYPE_TURBARREL_V1, 0 },
    { "NME_TYPE_FOGGER", NME_TYPE_FOGGER_V1, 0 },
    { "NME_TYPE_SLAMMER", NME_TYPE_SLAMMER_V1, 0 },
    { "NME_TYPE_FLINGER", NME_TYPE_FLINGER_V1, 0 },
    { "NME_TYPE_SPINNER", NME_TYPE_SPINNER_V1, 0 },
    { "NME_TYPE_POPPER", NME_TYPE_POPPER_V1, 0 },
    { "NME_TYPE_MERVYN", NME_TYPE_MERVYN_V1, 0 },
    { "NME_TYPE_DENNIS", NME_TYPE_DENNIS_V1, 0 },
    { "NME_TYPE_FOGGER_V1", NME_TYPE_FOGGER_V1, 0 },
    { "NME_TYPE_SLAMMER_V1", NME_TYPE_SLAMMER_V1, 0 },
    { "NME_TYPE_FLINGER_V1", NME_TYPE_FLINGER_V1, 0 },
    { "NME_TYPE_SPINNER_V1", NME_TYPE_SPINNER_V1, 0 },
    { "NME_TYPE_POPPER_V1", NME_TYPE_POPPER_V1, 0 },
    { "NME_TYPE_ZAPPER_V1", NME_TYPE_ZAPPER_V1, 0 },
    { "NME_TYPE_MERVYN_V1", NME_TYPE_MERVYN_V1, 0 },
    { "NME_TYPE_FOGGER_V2", NME_TYPE_FOGGER_V2, 0 },
    { "NME_TYPE_SLAMMER_V2", NME_TYPE_SLAMMER_V2, 0 },
    { "NME_TYPE_FLINGER_V2", NME_TYPE_FLINGER_V2, 0 },
    { "NME_TYPE_SPINNER_V2", NME_TYPE_SPINNER_V2, 0 },
    { "NME_TYPE_POPPER_V2", NME_TYPE_POPPER_V2, 0 },
    { "NME_TYPE_ZAPPER_V2", NME_TYPE_ZAPPER_V2, 0 },
    { "NME_TYPE_MERVYN_V2", NME_TYPE_MERVYN_V2, 0 },
    { "NME_TYPE_FOGGER_V3", NME_TYPE_FOGGER_V3, 0 },
    { "NME_TYPE_SLAMMER_V3", NME_TYPE_SLAMMER_V3, 0 },
    { "NME_TYPE_FLINGER_V3", NME_TYPE_FLINGER_V3, 0 },
    { "NME_TYPE_SPINNER_V3", NME_TYPE_SPINNER_V3, 0 },
    { "NME_TYPE_POPPER_V3", NME_TYPE_POPPER_V3, 0 },
    { "NME_TYPE_ZAPPER_V3", NME_TYPE_ZAPPER_V3, 0 },
    { "NME_TYPE_MERVYN_V3", NME_TYPE_MERVYN_V3, 0 },
    { "NME_TYPE_BUCKOTRON", NME_TYPE_BUCKOTRON_V1, 0 },
    { "NME_TYPE_BUCKOTRON_V1", NME_TYPE_BUCKOTRON_V1, 0 },
    { "NME_TYPE_BUCKOTRON_V2", NME_TYPE_BUCKOTRON_V2, 0 },
    { "NME_TYPE_BUCKOTRON_V3", NME_TYPE_BUCKOTRON_V3, 0 },
    { "NME_TYPE_BUCKOTRON_V4", NME_TYPE_BUCKOTRON_V4, 0 },
    { "NME_TYPE_BUCKOTRON_V5", NME_TYPE_BUCKOTRON_V5, 0 },
    { "NME_TYPE_BUCKOTRON_V6", NME_TYPE_BUCKOTRON_V6, 0 },
    { "NME_TYPE_BUCKOTRON_V7", NME_TYPE_BUCKOTRON_V7, 0 },
    { "NME_TYPE_DENNIS_V1", NME_TYPE_DENNIS_V1, 0 },
    { "NME_TYPE_DENNIS_V2", NME_TYPE_DENNIS_V2, 0 },
    { "NME_TYPE_NEPTUNE", NME_TYPE_NEPTUNE, 0 },
    { "NME_TYPE_FROGFISH", NME_TYPE_FROGFISH, 0 },
    { "NME_TYPE_SBBAT", NME_TYPE_SBBAT, 0 },
    { "NME_TYPE_TONGUESPIN", NME_TYPE_TONGUESPIN, 0 },
    { "NME_TYPE_MINDY", NME_TYPE_MINDY, 0 },
    { "NME_TYPE_NPC_PAT", NME_TYPE_PATRICK, 0 },
    { "NME_TYPE_NPC_BOB", NME_TYPE_SPONGEBOB, 0 },
};

S32 zNPCMgr_OrdComp_npcid(void* vkey, void* vitem);
S32 zNPCMgr_OrdTest_npcid(const void* vkey, void* vitem);

inline xEnt* zNPCMgr::CreateDynNME(const xEntAsset* ent_asset, const xDynAsset* dyn_asset,
                                   S32 siz_dynass)
{
    U32 size;
    U32 aid_minf;
    void* thing;
    xModelAssetInfo* modelInfoAsset;
    en_npctyp nt;
    xFactoryInst* npc_inst;
    zNPC::base* npc;
    zNMECommon* nme;

    aid_minf = xStrHashCat(ent_asset->modelInfoID, ".MINF");
    thing = xSTFindAsset(aid_minf, &size);
    modelInfoAsset = (xModelAssetInfo*)thing;
    ((xEntAsset*)ent_asset)->modelInfoID = aid_minf;

    nt = zNPCMgr_TypeForBrain(modelInfoAsset->BrainID);

    if (!flg_createForAnim)
    {
        npc_inst = firstAllocList[nt];
        if (npc_inst == NULL)
        {
            npc_inst = npcFactory->CreateItem(nt, NULL, NULL);
        }
        else
        {
            firstAllocList[nt] = NULL;
        }
    }
    else
    {
        npc_inst = npcFactory->CreateItem(nt, NULL, NULL);
    }

    npc = (zNPC::base*)npc_inst;
    if (flg_createForAnim && firstAllocList[nt] == NULL)
    {
        firstAllocList[nt] = npc;
    }

    nme = (zNMECommon*)npc;
    nme->model_asset = modelInfoAsset;
    nme->asset = (xEntAsset*)ent_asset;
    nme->nmeass = (zNMEAsset*)dyn_asset;
    nme->siz_nmeass = siz_dynass;
    nme->InitFromNPCMgr((xEntAsset*)ent_asset);

    XOrdAppend(&npclist, npc);
    if (npclist.cnt == npclist.max)
    {
        XOrdSort(&npclist, zNPCMgr_OrdComp_npcid);
    }

    return npc;
}

inline void zNPCMgr::Startup()
{
    for (S32 i = 0; i < (S32)(sizeof(g_brainTable) / sizeof(g_brainTable[0])); i++)
    {
        g_brainTable[i].id = xStrHash(g_brainTable[i].name);
    }

    selfbase.id = 'NPCM';
    selfbase.baseType = 0xAB;
    npcFactory = new ('NPCM', NULL) xFactory(64);

    zNPCTypes_RegisterTypes(npcFactory);
}

zNPCMgr* zNPCMgrSelf()
{
    return g_npcmgr;
}

st_XORDEREDARRAY* zNPCMgr_GetNPCList()
{
    return &g_npcmgr->npclist;
}

void zNPCMgr_Startup()
{
    if (g_modinit++ == 0)
    {
        g_npcmgr = new ('NPCM', NULL) zNPCMgr;
        g_npcmgr->Startup();
    }
}

void zNPCMgr_Shutdown()
{
    g_modinit--;
    if (g_modinit == 0)
    {
        zNPCMgr* mgr = zNPCMgrSelf();
        if (mgr != NULL)
        {
            mgr->Shutdown();
            delete mgr;
            g_npcmgr = NULL;
        }

        // Retail bug (also in BfBB): calls itself instead of a submodule shutdown. The nested
        // call only decrements g_modinit again (leaving it at -1), so a later zNPCMgr_Startup()
        // would not re-create the manager.
        zNPCMgr_Shutdown();
    }
}

void zNPCMgr_scenePrepare(S32 npccnt)
{
    zNPCMgrSelf()->ScenePrepare(npccnt);
}

void zNPCMgr_sceneInit()
{
    zNPCTypes_ForEachType(&zNPCTypeInfo::scene_init);
}

void zNPCMgr_sceneFinish()
{
    zNPCMgr* mgr = zNPCMgrSelf();
    mgr->SceneFinish();
}

void zNPCMgr_sceneReset()
{
    zNPCTypes_ForEachType(&zNPCTypeInfo::reset);
}

void zNPCMgr_scenePostInit()
{
    zNPCTypes_ForEachType(&zNPCTypeInfo::setup);
}

// Placeholder for dead-stripped code: retail's string pool keeps "<unknown>" here, but nothing
// references it. Probably a name lookup's fallback; the body is unknown.
static const char* __deadstripped_zNPCMgr()
{
    return "<unknown>";
}

xEnt* zNPCMgr_createDynNMEInst(const xEntAsset* assdat, const xDynAsset* dynass, S32 siz_dynass)
{
    return zNPCMgrSelf()->CreateDynNME(assdat, dynass, siz_dynass);
}

void zNPCMgr::Shutdown()
{
    if (npcFactory != NULL)
    {
        delete npcFactory;
    }
    npcFactory = NULL;
    zNPCTypes_ShutdownTypes();
}

zNPC::base* zNPCMgr::FindNPC(U32 aid)
{
    S32 idx = XOrdLookup(&npclist, (void*)aid, zNPCMgr_OrdTest_npcid);
    return (idx < 0) ? NULL : (zNPC::base*)npclist.list[idx];
}

S32 zNPCMgr_OrdTest_npcid(const void* vkey, void* vitem)
{
    S32 rc;
    U32 key = *(U32*)vitem;

    if ((U32)(UPtr)vkey < key)
    {
        rc = -1;
    }
    else if ((U32)(UPtr)vkey > key)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    return rc;
}

S32 zNPCMgr_OrdComp_npcid(void* vkey, void* vitem)
{
    S32 rc;
    U32 item;
    U32 key;

    key = *(U32*)vkey;
    item = *(U32*)vitem;
    if (key < item)
    {
        rc = -1;
    }
    else if (key > item)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    return rc;
}
