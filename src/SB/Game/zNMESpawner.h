#ifndef ZNMESPAWNER_H
#define ZNMESPAWNER_H

#include "zNMECentral.h"
#include "zNMECommon.h"
#include "xordarray.h"
#include "xutil.h"

enum en_wavemode
{
    SM_WAVE_CONTINUOUS,
    SM_WAVE_DISCREET,
    SM_WAVE_AMBUSHWAVE,
    SM_WAVE_AMBUSHCONT,
    SM_WAVE_NOMORE,
    SM_WAVE_FORCE = 0x7fffffff
};

enum en_wavestat
{
    SM_STAT_BEGIN,
    SM_STAT_INPROG,
    SM_STAT_ABORT,
    SM_STAT_MARKTIME,
    SM_STAT_DONE,
    SM_STAT_NOMORE,
    SM_STAT_FORCE = 0x7fffffff
};

enum en_smnpcstat
{
    SM_NPC_DEAD,
    SM_NPC_READY,
    SM_NPC_PENDING,
    SM_NPC_SPAWNED,
    SM_NPC_ACTIVE,
    SM_NPC_NOMORE,
    SM_NPC_FORCE = 0x7fffffff
};

struct SMSPStatus
{
    zMovePoint* sp;
    S32 flg_stat;
    zNMECommon* npc_prefer;
};

struct SMNPCStatus
{
    zNMECommon* npc;
    en_smnpcstat status;
    zMovePoint* sp_prefer;
};

struct zNMESpawner : RyzMemData
{
    S32 flg_spawner;
    zNMECommon* npc_owner;
    F32 tym_delay;
    S32 max_spawn;
    en_wavemode wavemode;
    en_wavestat wavestat;
    F32 tmr_wave;
    S32 cnt_spawn;
    S32 cnt_contAmbushRemain;
    SMSPStatus sppool[32];
    SMNPCStatus npcpool[32];
    SMNPCStatus* sp_inUseLock[32];
    st_XORDEREDARRAY pendlist;
    st_XORDEREDARRAY actvlist;
    S32 cnt_cleanup;

    // Inlined (no GC symbol); BfBB's zNPCSpawner::Subscribe.
    void Subscribe(zNMECommon* owner)
    {
        npc_owner = owner;
        tym_delay = 5.0f;
        max_spawn = -1;
        wavestat = SM_STAT_BEGIN;
        XOrdInit(&pendlist, 32, 0);
        XOrdInit(&actvlist, 32, 0);
    }
    // Inlined (no GC symbol); BfBB's zNPCSpawner::SetWaveMode. A negative delay selects the
    // default.
    void SetWaveMode(en_wavemode mode, F32 delay, S32 lifemax)
    {
        wavemode = mode;
        max_spawn = lifemax;
        cnt_contAmbushRemain = 0;
        if (delay < 0.0f)
        {
            tym_delay = 5.0f;
        }
        else
        {
            tym_delay = delay;
        }
    }
    // Inlined (no GC symbol); BfBB's zNPCSpawner::Reset, plus TSSM's ambush counter and spawn
    // point locks.
    void Reset()
    {
        cnt_spawn = 0;
        wavestat = SM_STAT_BEGIN;
        cnt_cleanup = 0;
        cnt_contAmbushRemain = 0;
        flg_spawner &= 0x1;
        flg_spawner |= 0x8;
        ClearPending();
        ClearActive();
        for (S32 i = 0; i < 32; i++)
        {
            sp_inUseLock[i] = NULL;
        }
        MapPreferred();
    }
    S32 AddSpawnPoint(zMovePoint* sp);
    S32 AddSpawnNPC(zNMECommon* npc);
    void MapPreferred();
    void Timestep(F32 dt);
    void UpdateWaveMode(F32 dt);
    void UpdateAmbushWave(F32 dt);
    void UpdateContinuous(F32 dt);
    void Notify(en_smnote note, void* data);
    // Inlined (no GC symbol); BfBB's zNPCSpawner::ClearPending.
    void ClearPending()
    {
        for (S32 i = 0; i < pendlist.cnt; i++)
        {
            ((SMNPCStatus*)pendlist.list[i])->status = SM_NPC_READY;
        }

        XOrdReset(&pendlist);
    }
    // Inlined (no GC symbol); BfBB's zNPCSpawner::ClearActive. Both games walk actvlist.cnt
    // entries of pendlist.list here (sic).
    void ClearActive()
    {
        for (S32 i = 0; i < actvlist.cnt; i++)
        {
            if (pendlist.list[i] != NULL)
            {
                ((SMNPCStatus*)pendlist.list[i])->status = SM_NPC_READY;
            }
        }

        XOrdReset(&actvlist);
    }
    // Inlined (no GC symbol); BfBB's zNPCSpawner::ReFillPending.
    void ReFillPending()
    {
        SMNPCStatus* npc_stat;
        S32 i;

        for (i = 0; i < 32; i++)
        {
            npc_stat = &npcpool[i];
            if (npc_stat->npc != NULL && npc_stat->status == SM_NPC_READY)
            {
                XOrdAppend(&pendlist, npc_stat);
                npc_stat->status = SM_NPC_PENDING;
            }
        }
    }
    // Inlined (no GC symbol); BfBB's zNPCSpawner::FillPending.
    void FillPending()
    {
        ClearPending();
        ReFillPending();
    }
    // Inlined (no GC symbol); BfBB's zNPCSpawner::NextPendingNPC.
    SMNPCStatus* NextPendingNPC()
    {
        S32 cnt = pendlist.cnt;

        if (cnt < 1)
        {
            return NULL;
        }

        return xUtil_select((SMNPCStatus**)pendlist.list, cnt, NULL);
    }
    // Preferred spawn point (SPSelect_Prefer) first, else SPSelect_Avail.
    SMSPStatus* SelectSP(const SMNPCStatus* npcstat);
    SMSPStatus* SPSelect_Avail(const SMNPCStatus* npcstat);
    SMSPStatus* SPSelect_Prefer(const SMNPCStatus* npcstat);
    // Inlined (no GC symbol); BfBB's zNPCSpawner::StatForSP.
    SMSPStatus* StatForSP(const zMovePoint* sp)
    {
        SMSPStatus* sp_stat = NULL;
        S32 i;
        SMSPStatus* tmp_stat;

        for (i = 0; i < 32; i++)
        {
            tmp_stat = &sppool[i];
            if (tmp_stat->sp != NULL && tmp_stat->sp == sp)
            {
                sp_stat = tmp_stat;
                break;
            }
        }

        return sp_stat;
    }
    // No GC symbol (always inlined into SelectSP and SpawnBeastie; the name is ours): index of
    // spstat in sppool (and so in sp_inUseLock), -1 if it is not one of ours.
    S32 SPIndex(const SMSPStatus* spstat)
    {
        S32 idx = -1;
        S32 i;

        for (i = 0; i < 32; i++)
        {
            if (&sppool[i] == spstat)
            {
                idx = i;
                break;
            }
        }

        return idx;
    }
    S32 IsSPLZClear(zMovePoint* sp);
    // Not in the PS2 demo; BfBB's zNPCSpawner::IsNearbyMover.
    S32 IsNearbyMover(xBound* bnd, S32 usecyl, xCollis* caller_colrec);
    SMNPCStatus* StatForNPC(zNMECommon* npc);
    S32 SpawnBeastie(SMNPCStatus* npcstat, SMSPStatus* spstat);
    void ChildCleanup(F32 dt);
    void SPUnlock(const SMNPCStatus* npcstat, S32);
};

struct SMDepot
{
    st_XORDEREDARRAY spawners;
};

struct zMODSpawner : zModule
{
    void ScenePrepare();
    void SceneFinish();
};

void zNMESpawner_Startup();
zNMESpawner* zNMESpawner_GetInstance();

#endif
