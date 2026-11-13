#ifndef ZNPCMGR_H
#define ZNPCMGR_H

#include "xBase.h"
#include "xFactory.h"
#include "zNPCTypes.h"
#include "zNPC.h"

#include <types.h>
#include <string.h>

struct xEnt;
struct xEntAsset;
struct xDynAsset;

namespace zNPC
{
    struct base;
}

struct NPCBrainTableEntry
{
    char* name;
    en_npctyp type;
    U32 id;
};

extern NPCBrainTableEntry g_brainTable[67];

// No symbol (always inlined): used by zNPC.cpp's create_anim_table and CreateDynNME.
inline en_npctyp zNPCMgr_TypeForBrain(U32 brainID)
{
    if (brainID != 0)
    {
        for (S32 i = 0; i < (S32)(sizeof(g_brainTable) / sizeof(g_brainTable[0])); i++)
        {
            if (brainID == g_brainTable[i].id)
            {
                return g_brainTable[i].type;
            }
        }
    }

    return NPC_TYPE_COMMON;
}

// TSSM layout (PS2 DWARF, zNPCMgrpv.h): size 0x1C8.
struct zNPCMgr : RyzMemData
{
    st_XORDEREDARRAY npclist;
    xFactory* npcFactory;
    xBase selfbase;
    S32 flg_createForAnim;
    zNPC::base* firstAllocList[104];

    xEnt* CreateDynNME(const xEntAsset* ent_asset, const xDynAsset* dyn_asset, S32 siz_dynass);
    zNPC::base* FindNPC(U32 aid);
    void Startup();
    void Shutdown();

    // Always inlined in TSSM (out of line in the Incredibles prototype DWARF, which gives the
    // name and locals).
    xEnt* EarlyNPCInst(en_npctyp ntyp)
    {
        zNPC::base* npc_inst = firstAllocList[ntyp];
        if (npc_inst != NULL)
        {
            return npc_inst;
        }

        xFactoryInst* fac_inst = npcFactory->CreateItem(ntyp, NULL, NULL);
        npc_inst = (zNPC::base*)fac_inst;
        if (firstAllocList[ntyp] == NULL)
        {
            firstAllocList[ntyp] = npc_inst;
        }

        return npc_inst;
    }

    void ScenePrepare(S32 npccnt)
    {
        XOrdInit(&npclist, npccnt, 0);
        zNPCTypes_ForEachType(&zNPCTypeInfo::scene_enter);
    }

    void SceneFinish()
    {
        memset(firstAllocList, 0, sizeof(firstAllocList));
        for (S32 i = 0; i < npclist.cnt; i++)
        {
            ((zNPC::base*)npclist.list[i])->destroy();
        }
        XOrdDone(&npclist, 0);
        npcFactory->DestroyAll();
    }

    ~zNPCMgr()
    {
    }
};

zNPCMgr* zNPCMgrSelf();
st_XORDEREDARRAY* zNPCMgr_GetNPCList();
S32 zNPCMgr_OrdTest_npcid(const void* vkey, void* vitem);
void zNPCMgr_Startup();
void zNPCMgr_Shutdown();
void zNPCMgr_scenePrepare(S32 npccnt);
void zNPCMgr_sceneInit();
void zNPCMgr_sceneFinish();
void zNPCMgr_sceneReset();
void zNPCMgr_scenePostInit();
xEnt* zNPCMgr_createDynNMEInst(const xEntAsset* assdat, const xDynAsset* dynass, S32 siz_dynass);

#endif
