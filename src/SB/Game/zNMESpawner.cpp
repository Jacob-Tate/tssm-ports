#include "zNMESpawner.h"
#include "zNMESupport.h"
#include "zMovePoint.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "xGrid.h"
#include "xQuickCull.h"
#include "xutil.h"
#include "xEvent.h"
#include "xDebug.h"

#include <string.h>

static SMDepot g_smdepot = {};

static zMODSpawner* g_mod_spawner;

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void zNMESpawner_Startup()
{
    static zMODSpawner mod_spawner;

    g_mod_spawner = &mod_spawner;
    zNMECentral_RegisterModule(&mod_spawner, 'SPWN');
}

void zMODSpawner::ScenePrepare()
{
    SMDepot* depot = &g_smdepot;
    zNMESpawner* sm;
    S32 i;

    XOrdInit(&depot->spawners, 16, 0);
    for (i = 0; i < 16; i++)
    {
        sm = new ('SPWN', NULL) zNMESpawner;
        XOrdAppend(&depot->spawners, sm);
    }
}

void zMODSpawner::SceneFinish()
{
    SMDepot* depot = &g_smdepot;
    S32 i;

    for (i = 0; i < depot->spawners.cnt; i++)
    {
        delete (zNMESpawner*)depot->spawners.list[i];
    }

    XOrdDone(&depot->spawners, 0);
}

// No GC symbol (always inlined into zNMEBuckotron::SetupSpawning).
inline zNMESpawner* zNMESpawner_GetInstance()
{
    zNMESpawner* sm = NULL;
    zNMESpawner* sm_tmp;
    SMDepot* depot = &g_smdepot;
    S32 i;

    for (i = 0; i < depot->spawners.cnt; i++)
    {
        sm_tmp = (zNMESpawner*)depot->spawners.list[i];
        if (!(sm_tmp->flg_spawner & 0x1))
        {
            sm_tmp->flg_spawner |= 0x1;
            sm = sm_tmp;
            break;
        }
    }

    return sm;
}

S32 zNMESpawner::AddSpawnPoint(zMovePoint* sp)
{
    SMSPStatus* sp_stat;
    S32 ack = 0;
    S32 i;

    for (i = 0; i < 32; i++)
    {
        sp_stat = &sppool[i];
        if (sp_stat->sp == NULL)
        {
            sp_stat->sp = sp;
            sp_stat->npc_prefer = NULL;
            ack = 1;
            break;
        }
    }

    return ack;
}

S32 zNMESpawner::AddSpawnNPC(zNMECommon* npc)
{
    S32 ack = 0;
    S32 i;
    SMNPCStatus* npc_stat;

    for (i = 0; i < 32; i++)
    {
        npc_stat = &npcpool[i];
        if (npc_stat->npc == NULL)
        {
            npc_stat->npc = npc;
            ack = 1;
            npc_stat->status = SM_NPC_DEAD;
            npc_stat->sp_prefer = NULL;
            break;
        }
    }

    npc->DuploOwner(npc_owner);

    return ack;
}

// No GC symbol: a plain member here in the PS2 demo, auto-inlined into zNMEBuckotron::Reset.
void zNMESpawner::MapPreferred()
{
    S32 i;
    SMNPCStatus* npc_stat;
    zMovePoint* sp;

    for (i = 0; i < 32; i++)
    {
        npc_stat = &npcpool[i];
        if (npc_stat->npc != NULL)
        {
            sp = npc_stat->npc->MvptFirstFollow();
            if (sp != NULL)
            {
                SMSPStatus* sp_stat = StatForSP(sp);
                if (sp_stat != NULL)
                {
                    npc_stat->sp_prefer = sp;
                    sp_stat->npc_prefer = npc_stat->npc;
                }
            }
        }
    }
}

// No GC symbol: a plain member here in the PS2 demo, auto-inlined into zNMEBuckotron::Process.
void zNMESpawner::Timestep(F32 dt)
{
    if (flg_spawner & 0x10)
    {
        ChildCleanup(dt);
    }

    if (!(flg_spawner & 0x4))
    {
        tmr_wave = MAX(-1.0f, tmr_wave - dt);

        switch (wavemode)
        {
        case SM_WAVE_DISCREET:
            UpdateWaveMode(dt);
            break;
        case SM_WAVE_CONTINUOUS:
            UpdateContinuous(dt);
            break;
        case SM_WAVE_AMBUSHWAVE:
            UpdateAmbushWave(dt);
            break;
        case SM_WAVE_AMBUSHCONT:
            UpdateContinuous(dt);
            break;
        }
    }
}

void zNMESpawner::UpdateWaveMode(F32 dt)
{
    SMSPStatus* spstat = NULL;

    switch (wavestat)
    {
    case SM_STAT_BEGIN:
        FillPending();
        if (pendlist.cnt > 0)
        {
            wavestat = SM_STAT_INPROG;
            zEntEvent(npc_owner, eEventDuploWaveBegin);
            flg_spawner &= ~0x20;
        }
        tmr_wave = -1.0f;
        break;
    case SM_STAT_INPROG:
        if (pendlist.cnt < 1)
        {
            wavestat = SM_STAT_MARKTIME;
        }
        else if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (tmr_wave < 0.0f)
        {
            SMNPCStatus* npcstat = NextPendingNPC();
            if (npcstat != NULL)
            {
                spstat = SelectSP(npcstat);
            }

            if (spstat != NULL)
            {
                SpawnBeastie(npcstat, spstat);
            }

            tmr_wave = tym_delay * (0.25f * (xurand() - 0.5f)) + tym_delay;
        }
        break;
    case SM_STAT_MARKTIME:
        if (actvlist.cnt < 1)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            wavestat = SM_STAT_ABORT;
        }
        break;
    case SM_STAT_ABORT:
        if (actvlist.cnt < 1)
        {
            wavestat = SM_STAT_DONE;
        }
        ClearPending();
        break;
    case SM_STAT_DONE:
        if (!(flg_spawner & 0x20))
        {
            zEntEvent(npc_owner, eEventDuploWaveComplete);
        }

        if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            if (!(flg_spawner & 0x20))
            {
                flg_spawner |= 0x20;
                zEntEvent(npc_owner, eEventDuploExpiredMaxNPC);
                zEntEvent(npc_owner, eEventDuploSuperDuperDone);
                if (flg_spawner & 0x2)
                {
                    zEntEvent(npc_owner, eEventDuploDuperIsDoner);
                }
            }
        }
        else if (flg_spawner & 0x2)
        {
            if (!(flg_spawner & 0x20))
            {
                flg_spawner |= 0x20;
                zEntEvent(npc_owner, eEventDuploSuperDuperDone);
                zEntEvent(npc_owner, eEventDuploDuperIsDoner);
            }
        }
        else
        {
            wavestat = SM_STAT_BEGIN;
        }
        break;
    }
}

void zNMESpawner::UpdateAmbushWave(F32 dt)
{
    SMSPStatus* spstat = NULL;

    switch (wavestat)
    {
    case SM_STAT_BEGIN:
        FillPending();
        if (pendlist.cnt > 0)
        {
            wavestat = SM_STAT_INPROG;
            zEntEvent(npc_owner, eEventDuploWaveBegin);
            flg_spawner &= ~0x20;
        }
        tmr_wave = 0.05f;
        break;
    case SM_STAT_INPROG:
        if (pendlist.cnt < 1)
        {
            wavestat = SM_STAT_MARKTIME;
        }
        else if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (tmr_wave < 0.0f)
        {
            SMNPCStatus* npcstat = NextPendingNPC();
            if (npcstat != NULL)
            {
                spstat = SelectSP(npcstat);
            }

            if (spstat != NULL)
            {
                SpawnBeastie(npcstat, spstat);
            }

            tmr_wave = 0.05f;
        }
        break;
    case SM_STAT_MARKTIME:
        if (actvlist.cnt < 1)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            wavestat = SM_STAT_ABORT;
        }
        break;
    case SM_STAT_ABORT:
        if (actvlist.cnt < 1)
        {
            wavestat = SM_STAT_DONE;
        }
        ClearPending();
        break;
    case SM_STAT_DONE:
        if (!(flg_spawner & 0x20))
        {
            zEntEvent(npc_owner, eEventDuploWaveComplete);
        }

        if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            if (!(flg_spawner & 0x20))
            {
                flg_spawner |= 0x20;
                zEntEvent(npc_owner, eEventDuploExpiredMaxNPC);
                zEntEvent(npc_owner, eEventDuploSuperDuperDone);
                if (flg_spawner & 0x2)
                {
                    zEntEvent(npc_owner, eEventDuploDuperIsDoner);
                }
            }
        }
        else if (flg_spawner & 0x2)
        {
            if (!(flg_spawner & 0x20))
            {
                flg_spawner |= 0x20;
                zEntEvent(npc_owner, eEventDuploSuperDuperDone);
                zEntEvent(npc_owner, eEventDuploDuperIsDoner);
            }
        }
        else
        {
            wavestat = SM_STAT_BEGIN;
        }
        break;
    }
}

void zNMESpawner::UpdateContinuous(F32 dt)
{
    SMSPStatus* spstat = NULL;

    switch (wavestat)
    {
    case SM_STAT_BEGIN:
        FillPending();
        if (pendlist.cnt > 0)
        {
            wavestat = SM_STAT_INPROG;
            zEntEvent(npc_owner, eEventDuploWaveBegin);
            flg_spawner &= ~0x20;
            if (wavemode == SM_WAVE_AMBUSHCONT)
            {
                cnt_contAmbushRemain = pendlist.cnt;
            }
            else
            {
                cnt_contAmbushRemain = 0;
            }
        }
        tmr_wave = -1.0f;
        break;
    case SM_STAT_INPROG:
        if (pendlist.cnt < 1)
        {
            wavestat = SM_STAT_MARKTIME;
        }
        else if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (cnt_contAmbushRemain > 0)
        {
            SMNPCStatus* npcstat = NextPendingNPC();
            if (npcstat != NULL)
            {
                spstat = SelectSP(npcstat);
            }

            if (spstat != NULL)
            {
                SpawnBeastie(npcstat, spstat);
            }

            tmr_wave = tym_delay * (0.25f * (xurand() - 0.5f)) + tym_delay;
            cnt_contAmbushRemain = MAX(0, cnt_contAmbushRemain - 1);
        }
        else if (tmr_wave < 0.0f)
        {
            SMNPCStatus* npcstat = NextPendingNPC();
            if (npcstat != NULL)
            {
                spstat = SelectSP(npcstat);
            }

            if (spstat != NULL)
            {
                SpawnBeastie(npcstat, spstat);
            }

            tmr_wave = tym_delay * (0.25f * (xurand() - 0.5f)) + tym_delay;
        }
        break;
    case SM_STAT_MARKTIME:
        if (flg_spawner & 0x2)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            wavestat = SM_STAT_ABORT;
        }
        else if (tmr_wave < 0.0f)
        {
            tmr_wave = tym_delay * (0.25f * (xurand() - 0.5f)) + tym_delay;
            ReFillPending();
            if (pendlist.cnt > 0)
            {
                wavestat = SM_STAT_INPROG;
            }
        }
        else if (pendlist.cnt > 0)
        {
            wavestat = SM_STAT_INPROG;
        }
        break;
    case SM_STAT_ABORT:
        if (actvlist.cnt < 1)
        {
            wavestat = SM_STAT_DONE;
        }
        ClearPending();
        break;
    case SM_STAT_DONE:
        if (!(flg_spawner & 0x20))
        {
            zEntEvent(npc_owner, eEventDuploWaveComplete);
        }

        if (max_spawn > 0 && cnt_spawn >= max_spawn)
        {
            if (!(flg_spawner & 0x20))
            {
                flg_spawner |= 0x20;
                zEntEvent(npc_owner, eEventDuploExpiredMaxNPC);
                zEntEvent(npc_owner, eEventDuploSuperDuperDone);
                if (flg_spawner & 0x2)
                {
                    zEntEvent(npc_owner, eEventDuploDuperIsDoner);
                }
            }
        }
        else if (flg_spawner & 0x2)
        {
            if (!(flg_spawner & 0x20))
            {
                flg_spawner |= 0x20;
                zEntEvent(npc_owner, eEventDuploSuperDuperDone);
                zEntEvent(npc_owner, eEventDuploDuperIsDoner);
            }
        }
        break;
    }
}

void zNMESpawner::Notify(en_smnote note, void* data)
{
    SMNPCStatus* npcstat;

    switch (note)
    {
    case SM_NOTE_NMEDIED:
        npcstat = StatForNPC((zNMECommon*)data);
        XOrdRemove(&actvlist, npcstat, -1);
        zEntEvent(npc_owner, eEventDuploNPCKilled);
        npcstat->status = SM_NPC_DEAD;
        SPUnlock(npcstat, 1);
        break;
    case SM_NOTE_NMESTANDBY:
        npcstat = StatForNPC((zNMECommon*)data);
        npcstat->status = SM_NPC_READY;
        SPUnlock(npcstat, 1);
        break;
    case SM_NOTE_NMEALIVE:
        npcstat = StatForNPC((zNMECommon*)data);
        npcstat->status = SM_NPC_ACTIVE;
        SPUnlock(npcstat, 0);
        break;
    case SM_NOTE_NMELIVINGINSIN:
    {
        SMNPCStatus* npcstat = StatForNPC((zNMECommon*)data);
        if (npcstat != NULL)
        {
            XOrdFind(&actvlist, npcstat, NULL);
            npcstat->status = SM_NPC_SPAWNED;
            XOrdRemove(&pendlist, npcstat, -1);
            XOrdAppend(&actvlist, npcstat);
        }
        break;
    }
    case SM_NOTE_DUPPAUSE:
        flg_spawner |= 0x4;
        break;
    case SM_NOTE_DUPRESUME:
        if (!(flg_spawner & 0x2))
        {
            flg_spawner &= ~0x4;
        }
        break;
    case SM_NOTE_DUPSETDELAY:
        tym_delay = *(F32*)data;
        tym_delay = MAX(tym_delay, 1.0f);
        break;
    case SM_NOTE_DUPDEAD:
        if (!(flg_spawner & 0x2))
        {
            ClearPending();
            flg_spawner |= 0x2;
            if (wavestat == SM_STAT_DONE && !(flg_spawner & 0x20))
            {
                zEntEvent(npc_owner, eEventDuploDuperIsDoner);
            }
        }
        break;
    case SM_NOTE_KILLKIDS:
        ClearPending();
        flg_spawner |= 0x2;
        flg_spawner |= 0x10;
        break;
    }
}

// The compiled-out asserts (conditions unknown) keep this from being inlined into its three
// callers, which call it as in retail.
SMSPStatus* zNMESpawner::SelectSP(const SMNPCStatus* npcstat)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    SMSPStatus* sp_stat;

    if (npcstat->sp_prefer != NULL)
    {
        sp_stat = SPSelect_Prefer(npcstat);
        if (sp_stat != NULL)
        {
            return sp_stat;
        }
    }

    return SPSelect_Avail(npcstat);
}

// No GC symbol (the name is ours, after SPSelect_Avail): inlined into SelectSP. The body is the
// sp_prefer branch of BfBB's zNPCSpawner::SelectSP; retail's register use needs it as a function.
SMSPStatus* zNMESpawner::SPSelect_Prefer(const SMNPCStatus* npcstat)
{
    SMSPStatus* sp_stat = StatForSP(npcstat->sp_prefer);

    if (!sp_stat->sp->IsOn())
    {
        return NULL;
    }

    if (!IsSPLZClear(sp_stat->sp))
    {
        return NULL;
    }

    return sp_stat;
}

// No GC symbol: auto-inlined into its only caller SelectSP (the out-of-line copy is dead-stripped).
SMSPStatus* zNMESpawner::SPSelect_Avail(const SMNPCStatus* npcstat)
{
    SMSPStatus* splist[32] = {};
    S32 cnt;
    S32 i;
    SMSPStatus* tmp_stat;
    S32 rc;
    SMSPStatus* sp_stat;

    cnt = 0;
    for (i = 0; i < 32; i++)
    {
        tmp_stat = &sppool[i];
        if (tmp_stat->sp == NULL)
        {
            continue;
        }
        if (!tmp_stat->sp->on)
        {
            continue;
        }
        if (tmp_stat->npc_prefer != NULL)
        {
            continue;
        }
        if (sp_inUseLock[SPIndex(tmp_stat)] != NULL)
        {
            continue;
        }

        rc = IsSPLZClear(tmp_stat->sp);
        if (!rc)
        {
            continue;
        }

        splist[cnt++] = tmp_stat;
    }

    sp_stat = NULL;
    if (cnt != 0)
    {
        sp_stat = xUtil_select(splist, cnt, NULL);
    }

    return sp_stat;
}

// xGridCheckBound callback of IsSPLZClear: counts the NPCs standing within the test site's
// radius.
struct SpawnChkCollData
{
    S32 hitSomething;
    xBound bnd_testSite;
    S32 useCylTest;
    F32 radiusSquare;

    U8 operator()(xEnt& ent);
};

U8 SpawnChkCollData::operator()(xEnt& ent)
{
    en_npctyp ntyp;
    xVec3 vec_cent;

    if (!(ent.chkby & XENT_COLLTYPE_NPC))
    {
        return 1;
    }

    if (ent.baseType == eBaseTypeNPC)
    {
        ntyp = (en_npctyp)((zNMECommon&)ent).itemType;
        if (ntyp >= NME_TYPE_BUCKOTRON_V1 && ntyp <= NME_TYPE_BUCKOTRON_V7)
        {
            return 1;
        }
        if (ntyp >= NME_TYPE_TIKI && ntyp <= NME_TYPE_TIKI_STONE)
        {
            return 1;
        }
    }
    else
    {
        return 1;
    }

    if (useCylTest)
    {
        NMEC_pos_ofBase(&ent, &vec_cent);
        xVec3SubFrom(&vec_cent, &bnd_testSite.sph.center);
        if (SQ(vec_cent.x) + SQ(vec_cent.z) > radiusSquare)
        {
            return 1;
        }

        hitSomething++;
        return 0;
    }

    return 1;
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined, as retail calls
// it. BfBB's debug draw (g_drawSpawnBounds) is not in TSSM.
S32 zNMESpawner::IsSPLZClear(zMovePoint* sp)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xVec3 pos_sp = { 0.0f, 0.0f, 0.0f };
    xBound bnd;
    SpawnChkCollData cbdata;
    xBound bnd_bigger;

    memset(&bnd, 0, sizeof(xBound));
    bnd.type = XBOUND_TYPE_NA;
    xVec3Copy(&pos_sp, zMovePointGetPos(sp));
    bnd.type = XBOUND_TYPE_SPHERE;
    bnd.sph.r = 3.5f;
    xVec3Copy(&bnd.sph.center, &pos_sp);
    xQuickCullForBound(&bnd.qcd, &bnd);

    if (NMEC_chk_hitPlyr(&bnd, NULL))
    {
        return 0;
    }

    xVec3 delt = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - pos_sp;
    if (SQ(delt.x) + SQ(delt.z) < SQ(3.5f))
    {
        return 0;
    }

    memset(&cbdata, 0, sizeof(SpawnChkCollData));
    cbdata.bnd_testSite = bnd;
    cbdata.useCylTest = 1;
    cbdata.radiusSquare = SQ(3.5f);

    bnd_bigger = bnd;
    bnd_bigger.sph.r *= 7.0f;
    // Retail recomputes the culling data of bnd, not of bnd_bigger.
    xQuickCullForBound(&bnd.qcd, &bnd);

    xGridCheckBound(npcs_grid, bnd_bigger, bnd_bigger.qcd, cbdata);

    return cbdata.hitSomething ? 0 : 1;
}

// No GC symbol: auto-inlined into Notify (out of line in the PS2 demo).
SMNPCStatus* zNMESpawner::StatForNPC(zNMECommon* npc)
{
    SMNPCStatus* tmp_stat;
    SMNPCStatus* npc_stat = NULL;
    S32 i;

    for (i = 0; i < 32; i++)
    {
        tmp_stat = &npcpool[i];
        if (tmp_stat->npc != NULL && tmp_stat->npc == npc)
        {
            npc_stat = tmp_stat;
            break;
        }
    }

    return npc_stat;
}

S32 zNMESpawner::IsNearbyMover(xBound* bnd, S32 usecyl, xCollis* caller_colrec)
{
    S32 hitthing = 0;
    xCollis* colrec;
    S32 i;
    zNMECommon* npc;
    xCollis local_colrec;
    en_npctyp ntyp;

    if (caller_colrec != NULL)
    {
        colrec = caller_colrec;
    }
    else
    {
        colrec = &local_colrec;
        memset(colrec, 0, sizeof(xCollis));
    }

    for (i = 0; i < xglobals->sceneCur->num_npcs; i++)
    {
        npc = (zNMECommon*)xglobals->sceneCur->npcs[i];
        if (!(npc->chkby & XENT_COLLTYPE_NPC))
        {
            continue;
        }

        ntyp = (en_npctyp)npc->itemType;
        if (ntyp >= NME_TYPE_BUCKOTRON_V1 && ntyp <= NME_TYPE_BUCKOTRON_V7)
        {
            continue;
        }
        if (ntyp >= NME_TYPE_TIKI && ntyp <= NME_TYPE_TIKI_STONE)
        {
            continue;
        }

        xBoundHitsBound(bnd, &npc->bound, colrec);
        if (colrec->flags & 0x1)
        {
            hitthing++;
        }
        else if (usecyl)
        {
            xVec3 delt = { 0.0f, 0.0f, 0.0f };

            NMEC_pos_ofBase(npc, &delt);
            xVec3SubFrom(&delt, &bnd->sph.center);
            if (SQ(delt.x) + SQ(delt.z) < SQ(bnd->cyl.r))
            {
                hitthing++;
            }
        }

        if (hitthing)
        {
            break;
        }
    }

    return hitthing;
}

S32 zNMESpawner::SpawnBeastie(SMNPCStatus* npcstat, SMSPStatus* spstat)
{
    zNMECommon* npc = npcstat->npc;
    zMovePoint* sp = spstat->sp;
    zMovePoint* nav_dest = NULL;
    xVec3 pos_sp;

    zMovePointGetNext(sp, sp, &nav_dest, NULL);
    if (nav_dest == NULL)
    {
        nav_dest = sp;
    }

    pos_sp = *sp->PosGet();

    npcstat->status = SM_NPC_SPAWNED;
    XOrdRemove(&pendlist, npcstat, -1);
    XOrdAppend(&actvlist, npcstat);

    sp_inUseLock[SPIndex(spstat)] = npcstat;

    npc->Respawn(&pos_sp, nav_dest, sp);
    cnt_spawn++;

    zEntEvent(npc_owner, eEventDuploNPCBorn);

    return 1;
}

// Placeholder for dead-stripped code, not recovered code: retail's .rodata keeps an unreferenced
// all-zero 0xC template from between SpawnBeastie and ChildCleanup. Its owner and type are unknown.
static void __deadstripped_zNMESpawner()
{
    xVec3 unknown = {};
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined into
// zNMEBuckotron::Process, as retail calls it.
void zNMESpawner::ChildCleanup(F32 dt)
{
    S32 i;
    SMNPCStatus* npc_stat;
    S32 cnt_know;
    S32 cnt_dead;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (wavestat == SM_STAT_DONE || wavestat == SM_STAT_ABORT)
    {
        for (i = actvlist.cnt - 1; i >= 0; i--)
        {
            npc_stat = (SMNPCStatus*)actvlist.list[i];
            xASSERT_PLACEHOLDER();
            xASSERT_PLACEHOLDER();
            npc_stat->npc->Damage(DMGTYP_INSTAKILL, NULL, NULL, -1, ePowerupLevelNormal);
        }

        if (actvlist.cnt == 0 && pendlist.cnt == 0)
        {
            cnt_know = 0;
            cnt_dead = 0;
            for (i = 0; i < 32; i++)
            {
                npc_stat = &npcpool[i];
                if (npc_stat->npc == NULL)
                {
                    continue;
                }

                cnt_know++;
                if (npc_stat->status == SM_NPC_READY)
                {
                    cnt_dead++;
                }
                break;
            }

            if (cnt_dead == cnt_know)
            {
                flg_spawner &= ~0x10;
            }
        }

        cnt_cleanup++;
    }
}

// No GC symbol: inlined into Notify. The unused second parameter is from the PS2 symbol (Notify
// passes 1 for died or standby, 0 for alive).
void zNMESpawner::SPUnlock(const SMNPCStatus* npcstat, S32)
{
    S32 i;

    for (i = 0; i < 32; i++)
    {
        if (sp_inUseLock[i] == npcstat)
        {
            sp_inUseLock[i] = NULL;
            break;
        }
    }
}
