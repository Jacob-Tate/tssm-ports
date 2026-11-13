#include "zCutsceneMgr.h"
#ifdef PLATFORM_PC
#include "iCoop.h"
#endif
#include "zCineFX.h"
#include "xTextureManager.h"
#include "iTRC.h"
#include "iCutscene.h"
#include "iSystem.h"
#include "iDebug.h"

static zCutsceneHack cutsceneHackTable[17] = {
    { "enemy_popper_intro", "popper_t_bind.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_fogger_intro", "signs.dff", 1, 0, NULL },
    { "enemy_fogger_intro", "lights.dff", 1, 0, NULL },
    { "enemy_spinner_intro", "spatula.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_turret_intro", "spatula.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "thug_tug", "spatula_bind.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "thug_tug", "spatula02_bind.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_01.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_02.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_03.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_04.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_05.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_06.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_critter_intro", "critter_07.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_merv_intro", "mini_merv.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "enemy_merv_intro", "minimerv_v1_bind.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
    { "cliff_monster_cine", "projectile_skel.dff", 0, 1, NULL, { { 0x00401200, 0x18, 0, 0 } } },
};

static S32 gCutsceneSkipOK;
static U8 ents_hidden;

void zCutsceneMgrInit(void* b, void* tasset)
{
    zCutsceneMgrInit((xBase*)b, (xCutsceneMgrAsset*)tasset);
}

inline void zCutsceneMgrInit(xBase* b, xCutsceneMgrAsset* tasset)
{
    zCutsceneMgr* mgr = (zCutsceneMgr*)b;
    xBaseInit(mgr, tasset);
    mgr->eventFunc = zCutsceneMgrEventCB;
    mgr->tasset = tasset;
    mgr->csn = NULL;
    mgr->sendDoneForDisabledCutscene = 0;
    mgr->pausedMusic = 0;
    if (mgr->linkCount != 0)
    {
        mgr->link = (xLinkAsset*)(mgr->tasset + 1);
    }
    else
    {
        mgr->link = NULL;
    }
    ents_hidden = 0;
}

inline void zCutsceneMgrReset(zCutsceneMgr* mgr)
{
    xBaseReset(mgr, mgr->tasset);
    if (mgr->csn != NULL)
    {
        xCutscene_Destroy(mgr->csn);
    }
    mgr->csn = NULL;
    zEntPlayerControlOn(CONTROL_OWNER_CUTSCENE);
    ents_hidden = 0;
}

void zCutsceneMgrSave(zCutsceneMgr*, xSerial* s)
{
}

void zCutsceneMgrLoad(zCutsceneMgr*, xSerial* s)
{
}

// counter and hacked are not in the PS2 DWARF; the names are from the Incredibles and Ratatouille.
// hacked is only ever cleared in TSSM.
static void zCutsceneMgrPlayStart(zCutsceneMgr* t)
{
    U32 i;
    U32 j;
    U32 counter;
    U32 cinid;
    U32 hackid;
    U8 hacked;

    gCutsceneSkipOK = 1;
    counter = sizeof(cutsceneHackTable) / sizeof(cutsceneHackTable[0]);

    for (i = 0; i < counter; i++)
    {
        cinid = xStrHash(cutsceneHackTable[i].cinname);
        if (t->tasset->cutsceneAssetID != cinid)
        {
            continue;
        }

        hackid = xStrHash(cutsceneHackTable[i].modelname);
        hacked = 0;
        for (j = 0; j < t->csn->Info->NumData; j++)
        {
            if (hackid != t->csn->Data[j].AssetID ||
                (t->csn->Data[j].DataType & 0xfffffff) != XCUTSCENEDATA_TYPE_RW_MODEL ||
                t->csn->Data[j].DataPtr == NULL)
            {
                continue;
            }

            if (cutsceneHackTable[i].noshadow)
            {
                t->csn->Data[j].DataType |= 0x10000000;
            }

            if (cutsceneHackTable[i].renderCB)
            {
                ((RpAtomic*)t->csn->Data[j].DataPtr)->renderCallBack =
                    cutsceneHackTable[i].renderCB;
                if (((RpAtomic*)t->csn->Data[j].DataPtr)->renderCallBack == NULL)
                {
                    ((RpAtomic*)t->csn->Data[j].DataPtr)->renderCallBack =
                        AtomicDefaultRenderCallBack;
                }
            }

            if (cutsceneHackTable[i].numPipes)
            {
                t->csn->ModelHack[j].pipe = cutsceneHackTable[i].pipe;
            }
        }
    }
}

void zCutsceneMgrEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32)
{
    zCutsceneMgr* t = (zCutsceneMgr*)to;

    if (globals.NoCutscenes)
    {
        if (toEvent == 0xbe)
        {
            globals.DisabledCutsceneDoneMgr = t;
            zEntEvent(to, to, 0x18);
            zEntEvent(to, to, 0xbf);
        }
        return;
    }

    switch (toEvent)
    {
    case 0x13:
        t->stop = 1;
        break;
    case 0x18:
        break;
    case 0xbe:
        if (toParam && toParam[0] == 0.0f)
        {
            t->pausedMusic = 1;
            zMusicPause();
        }
        if (globals.cmgr == NULL)
        {
            xTextureManager::Get().FreeMemoryBlockingCall();
            t->stop = 0;
            t->csn = xCutscene_Create(t->tasset->cutsceneAssetID, t->tasset->uSubtitlesID);
            if (t->csn != NULL)
            {
                xCutscene_LoadStart(t->csn);
                globals.cmgr = t;
                t->oldfov = globals.screen->fov;
#ifdef PLATFORM_PC
                // Online co-op (docs/MULTIPLAYER.md 7.3): the host's cutscene
                // plays on the guest too. Nothing when co-op is off.
                iCoopShowCutsceneStart(t, toParam);
#endif
                if (toParam == NULL || toParam[1] != 0.0f)
                {
                    zCutsceneMgrFinishLoad(t);
                }
            }
        }
        break;
    case 0xbf:
        break;
    case 0xa:
        zCutsceneMgrReset(t);
        break;
    }
}

// Retail has FinishLoad and FinishExit unoptimized, unlike the rest of this file. The self-call
// under gFrameCount == 0 is HI's trick to keep FinishLoad out of the event callback (Ratatouille
// names that guard thisShouldAlwaysBeFalseItsJustToFoolTheStupidCompiler).
#pragma optimization_level 0
void zCutsceneMgrFinishLoad(xBase* to)
{
    zCutsceneMgr* t = (zCutsceneMgr*)to;

    if (gFrameCount == 0)
    {
        zCutsceneMgrFinishLoad(to);
    }

    do
    {
        iTRCDisk::TRCCheck();
        iFileAsyncService();
        xSndMgrUpdate();
        iCSLoadStep(t->csn);
    } while (!t->csn->Ready);

    zCutsceneMgrPlayStart(t);
    zEntEvent(to, to, 0x18);
    zEntEvent(xglobals->___player_ent_dont_use_directly, 8);
    zEntEvent(xglobals->___player_ent_dont_use_directly, 4);
}

// Compiled without optimization in retail, like FinishLoad above.
void zCutsceneMgrFinishExit(xBase* to)
{
    zCutsceneMgr* t = (zCutsceneMgr*)to;

    while (true)
    {
        // Ratatouille prints this here. iprintf is empty in the final build, but in this
        // unoptimized function the string still lands in the pool, unreferenced, as in retail.
        iprintf("FINISH EXIT...\n");
        iFileAsyncService();
        xSndMgrUpdate();
        if (t->csn->Waiting == 0)
        {
            if (xCutscene_Destroy(t->csn))
            {
                zEntEvent(xglobals->___player_ent_dont_use_directly, 9);
                zEntEvent(xglobals->___player_ent_dont_use_directly, 3);
                zCutsceneMgrKillFX(t);
                t->csn = NULL;
                globals.cmgr = NULL;
            }
            return;
        }
        iVSync();
    }
}
#pragma optimization_level reset

inline void zCutsceneMgrKillFX(zCutsceneMgr* t)
{
    xCutsceneMgrAsset* a = t->tasset;
    for (S32 i = 0; i < 15; i++)
    {
        if (a->emitID[i] == 0)
        {
            continue;
        }
        zParEmitter* e = zParEmitterFind(a->emitID[i]);
        if (e != NULL)
        {
            // U8 mask: in this unoptimized function only that gives retail's one-register form.
            e->emit_flags &= (U8)~0x1;
        }
    }
}

inline void zCutsceneMgrUpdateFX(zCutsceneMgr* t, F32)
{
    xCutsceneMgrAsset* a = t->tasset;
    xCutscene* csn = t->csn;
    for (S32 i = 0; i < 15; i++)
    {
        if (a->emitID[i] == 0)
        {
            continue;
        }
        zParEmitter* e = zParEmitterFind(a->emitID[i]);
        if (e != NULL)
        {
            if (e->emit_flags & 1)
            {
                if (csn->Time > a->endTime[i])
                {
                    e->emit_flags &= 0xfe;
                }
            }
            else if (csn->Time >= a->startTime[i] && csn->Time <= a->endTime[i])
            {
                e->emit_flags |= 1;
            }
        }
    }
}

void zCutsceneMgrUpdate(xBase* to, xScene* sc, F32 dt)
{
    zCutsceneMgr* t;
    S32 csncount;
    S32 cnum;
    U32 skipButtons;
    static S32 doCineFX = 0;

    if (zGameIsPaused())
    {
        return;
    }

    t = (zCutsceneMgr*)to;
    if (t->sendDoneForDisabledCutscene)
    {
        zEntEvent(to, to, 0xbf);
        t->sendDoneForDisabledCutscene = 0;
        globals.cmgr = NULL;
        return;
    }

    if (t->csn->ShutDownWait)
    {
        if (xCutscene_Destroy(t->csn))
        {
            if (doCineFX)
            {
                zCineFXCutsceneDone(sc, dt, t);
            }
            doCineFX = 0;
            zCutsceneMgrKillFX(t);
            t->csn = NULL;
            globals.cmgr = NULL;
            globals.screen->set_fov(t->oldfov);
        }
    }
    else if (t->csn->Ready)
    {
        if (!doCineFX)
        {
            doCineFX = zCineFXCutscenePrep(sc, dt, t);
        }
        if (doCineFX)
        {
            zCineFXCutscene(sc, dt, t);
        }

        zCutsceneMgrUpdateFX(t, dt);

        if (globals.ForceAllCinematics)
        {
            static F32 csnPlayTimeLimit = 2.0f;
            csnPlayTimeLimit -= dt;
            if (csnPlayTimeLimit < 0.0f)
            {
                t->stop = 1;
                csnPlayTimeLimit = 2.0f;
            }
        }

        if (t->stop || !xCutscene_Update(t->csn, dt))
        {
            if (doCineFX)
            {
                zCineFXCutsceneDone(sc, dt, t);
            }
            doCineFX = 0;

            if (xCutscene_Destroy(t->csn))
            {
                zCutsceneMgrKillFX(t);
                t->csn = NULL;
                globals.cmgr = NULL;
                globals.screen->set_fov(t->oldfov);
            }

            if (t->pausedMusic)
            {
                zMusicUnpause();
            }
#ifdef PLATFORM_PC
            // Online co-op (7.3): the host's cutscene ended (or was skipped).
            iCoopShowCutsceneEnd(t);
#endif

            zEntEvent(xglobals->___player_ent_dont_use_directly, 9);
            zEntEvent(xglobals->___player_ent_dont_use_directly, 3);
            zEntEvent(to, to, 0xbf);
            iCameraSetNearFarClip(0.0f, 0.0f);

            if (globals.ForceAllCinematics && globals.ForceCinematic > 0)
            {
                csncount = xSTAssetCountByType('CSNM');
                if (globals.ForceCinematic <= csncount)
                {
                    for (cnum = 0; cnum < csncount; cnum++)
                    {
                        if (cnum == globals.ForceCinematic - 1)
                        {
                            zEntEvent(
                                zSceneFindObject(
                                    ((xBaseAsset*)xSTFindAssetByType('CSNM', cnum, NULL))->id),
                                0xbe);
                        }
                    }
                }
                else
                {
                    globals.ForceCinematic = 0;
                }
                globals.ForceCinematic++;
            }
        }
        else
        {
            skipButtons = 0x50000;
#ifdef PLATFORM_PC
            // Online co-op (7.3): a guest does not skip the host's cutscene
            // (the host does, for both). TRUE at once when co-op is off.
            if (t->csn->Time > 0.5f && globals.pad0->pressed & skipButtons && iCoopMaySkipShow())
#else
            if (t->csn->Time > 0.5f && globals.pad0->pressed & skipButtons)
#endif
            {
                t->stop = 1;
            }
        }
    }
    else
    {
        iCSLoadStep(t->csn);
        if (t->csn->Ready)
        {
            zCutsceneMgrPlayStart(t);
            zEntEvent(to, to, 0x18);
            zEntEvent(xglobals->___player_ent_dont_use_directly, 8);
            zEntEvent(xglobals->___player_ent_dont_use_directly, 4);
            doCineFX = zCineFXCutscenePrep(sc, dt, t);
        }
    }

    check_hide_entities();
}

inline void check_hide_entities()
{
    U8 hide = globals.cmgr != NULL;
    if (hide == ents_hidden)
    {
        return;
    }
    ents_hidden = hide;

    zScene& scene = *globals.sceneCur;
    zEnt** it = (zEnt**)scene.base;
    zEnt** end = it + scene.num_base;
    if (hide)
    {
        for (; it != end; it++)
        {
            zEnt* ent = *it;
            if (!(ent->baseFlags & 0x8) && ent->baseFlags & 0x20)
            {
                ent->flags |= 0x80;
                if (ent->visUpdate)
                {
                    ent->visUpdate(ent);
                }
            }
        }
    }
    else
    {
        for (; it != end; it++)
        {
            zEnt* ent = *it;
            if (ent->baseFlags & 0x20)
            {
                ent->flags &= 0x7f;
                if (ent->visUpdate)
                {
                    ent->visUpdate(ent);
                }
            }
        }
    }
}
