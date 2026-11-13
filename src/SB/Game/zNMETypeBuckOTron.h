#ifndef ZNMETYPEBUCKOTRON_H
#define ZNMETYPEBUCKOTRON_H

#include "zNMECommon.h"

struct zNMESpawner;
struct NMESysEvent;

// Anim names, indexed as g_strz_buckanim: Unknown, Idle, Attack, Attack_Left, Attack_Right, Defeat
extern U32 g_hash_buckanim[6];
extern char* g_strz_buckanim[6];

enum en_spawnmode
{
    NME_SPAWNMODE_CONTINUOUS,
    NME_SPAWNMODE_WAVES,
    NME_SPAWNMODE_AMBUSHWAVE,
    NME_SPAWNMODE_AMBUSHCONT,
    NME_SPAWNMODE_NOMORE,
    NME_SPAWNMODE_FORCE = 0x7fffffff
};

struct zNMEAssetBucketOTron : zNMEAsset
{
    U32 aid_spawnGroup;
    en_spawnmode spawnMode;
    F32 spawnDelay;
    S32 flg_buckass;
    S32 maxSpawn;
};

struct zNMEBuckotron : zNMECommon
{
    zNMEBuckotron()
    {
    }
    zNMESpawner* spawner;
    F32 tmr_lamps;
    S16 cnt_lampsync;
    S8 idx_currLamp;
    S8 unused;

    // Source order (the PS2 DWARF lists them reversed); AnimPick is the key function.
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void SelfSetup();
    virtual S32 DfltVulnFlags();
    void Init(xEntAsset* asset);
    void Setup();
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void SetupSpawning();
    void ParseSpawnItem(xBase* baseitem);
    void Reset();
    void DuploNotice(en_smnote note, void* data);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 Buck_SysMail(NMESysEvent* sysmail);

    U8 ColChkFlags() const
    {
        return 0;
    }
    U8 ColPenFlags() const
    {
        return 0;
    }
    U8 ColChkByFlags() const
    {
        return XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
    }
    U8 ColPenByFlags() const
    {
        return XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
    }
    U8 PhysicsFlags() const
    {
        return 0;
    }
};

void ZNME_BuckOTron_Startup();

#endif
