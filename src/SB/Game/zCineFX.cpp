#include "zCineFX.h"
#include "zCineFXShared.h"

#include <string.h>

#include "xString.h"
#include "zCutsceneMgr.h"

static NCINMap* g_cutmap[64] = {};
static NCINBeNosey* g_noz_ncin;

static void AddTables();

void NCINBeNosey::CanRenderNow()
{
    zCutsceneMgr* csnmgr = this->use_csnmgr;
    NCINEntry* fxtab = this->use_fxtab;
    NCINEntry* nextrec;
    NCINEntry* fxrec;

    if (csnmgr == NULL)
    {
        return;
    }

    if (fxtab == NULL)
    {
        return;
    }

    nextrec = fxtab;

    while (nextrec->info.fxTypeID != 0)
    {
        fxrec = nextrec++;

        if (fxrec->cb.fxrend != NULL && (fxrec->flg_stat & NCIN_STAT_ACTIVE))
        {
            fxrec->cb.fxrend(csnmgr, fxrec);
        }
    }
}

void NCINBeNosey::UpdatedAnimated(RpAtomic* model, RwMatrixTag* animMat, U32 animIndex,
                                  U32 dataIndex, U32&)
{
    zCutsceneMgr* csnmgr = this->use_csnmgr;
    NCINEntry* fxtab = this->use_fxtab;
    NCINEntry* nextrec;
    NCINEntry* fxrec;

    if (csnmgr == NULL)
    {
        return;
    }

    if (fxtab == NULL)
    {
        return;
    }

    nextrec = fxtab;

    while (nextrec->info.fxTypeID != 0)
    {
        fxrec = nextrec++;

        if (fxrec->cb.fxanim != NULL && (fxrec->flg_stat & NCIN_STAT_ACTIVE))
        {
            fxrec->cb.fxanim(csnmgr, fxrec, model, animMat, animIndex, dataIndex);
        }
    }
}

void zCineFXStartup()
{
    static NCINBeNosey nozey_npc_cinematics;

    g_noz_ncin = &nozey_npc_cinematics;
    AddTables();
}

void zCineFXShutdown()
{
}

void zCineFXSceneSetup()
{
    zCineFX_ZapperIntro_PostInit();
}

void zCineFXRegisterTable(NCINMap* maplist)
{
    NCINMap* maprec;
    S32 i;

    for (maprec = maplist; maprec->cinname != NULL; maprec++)
    {
        for (i = 0; i < 64; i++)
        {
            if (g_cutmap[i] == NULL)
            {
                g_cutmap[i] = maprec;
                g_cutmap[i]->cinhash = xStrHash(g_cutmap[i]->cinname);
                break;
            }
        }
    }
}

static inline NCINEntry* zCineFXCutscenePickTable(const zCutsceneMgr* csnmgr)
{
    NCINEntry* da_fxtab = NULL;

    for (S32 i = 0; i < 64; i++)
    {
        NCINMap* cutrec = g_cutmap[i];
        if (cutrec == NULL)
        {
            continue;
        }

        if (cutrec->cinhash == csnmgr->csn->Info->AssetID)
        {
            da_fxtab = cutrec->cinfxtab;
            break;
        }
    }

    return da_fxtab;
}

S32 zCineFXCutscenePrep(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    NCINEntry* fxtab = zCineFXCutscenePickTable(csnmgr);
    NCINEntry* nextrec;
    NCINEntry* fxrec;

    if (fxtab == NULL)
    {
        return 0;
    }

    if (csnmgr != NULL && csnmgr->csn != NULL)
    {
        csnmgr->csn->cb_nosey = NULL;
    }

    g_noz_ncin->Init(csnmgr, fxtab, 3);

    nextrec = fxtab;

    while (nextrec->info.fxTypeID != 0)
    {
        fxrec = nextrec++;

        fxrec->flg_stat = 0;
        fxrec->flg_stat = NCIN_STAT_FIRST | NCIN_STAT_8;
        memset(&fxrec->fxdata, 0, sizeof(NCINData));
    }

    return 1;
}

void zCineFXCutsceneDone(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    NCINEntry* fxtab = zCineFXCutscenePickTable(csnmgr);
    NCINEntry* nextrec;
    NCINEntry* fxrec;

    if (fxtab == NULL)
    {
        return;
    }

    if (csnmgr != NULL && csnmgr->csn != NULL)
    {
        csnmgr->csn->cb_nosey = NULL;
    }

    g_noz_ncin->Done();

    nextrec = fxtab;

    while (nextrec->info.fxTypeID != 0)
    {
        fxrec = nextrec++;

        if (fxrec->flg_stat & NCIN_STAT_DEAD)
        {
            continue;
        }

        if (!(fxrec->flg_stat & NCIN_STAT_ACTIVE))
        {
            continue;
        }

        fxrec->cb.fxupd((zCutsceneMgr*)csnmgr, fxrec, 1);
        fxrec->flg_stat &= ~NCIN_STAT_ACTIVE;
    }
}

void zCineFXCutscene(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    xCutscene* csn = csnmgr->csn;
    NCINEntry* fxtab = zCineFXCutscenePickTable(csnmgr);
    S32 need_animated;
    S32 need_render;
    NCINEntry* nextrec;
    NCINEntry* fxrec;
    S32 flags;

    if (fxtab == NULL)
    {
        return;
    }

    nextrec = fxtab;
    need_animated = 0;
    need_render = 0;

    while (nextrec->info.fxTypeID != 0)
    {
        fxrec = nextrec++;

        if (csn->Time < fxrec->info.tym_beg)
        {
            continue;
        }

        if (fxrec->flg_stat & NCIN_STAT_DEAD)
        {
            continue;
        }

        if (csn->Time > fxrec->info.tym_end && (fxrec->flg_stat & NCIN_STAT_ACTIVE))
        {
            fxrec->cb.fxupd((zCutsceneMgr*)csnmgr, fxrec, 1);
            fxrec->flg_stat &= ~NCIN_STAT_ACTIVE;
            continue;
        }

        if (csn->Time > fxrec->info.tym_end && !(fxrec->flg_stat & NCIN_STAT_DEAD))
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
            continue;
        }

        fxrec->flg_stat |= NCIN_STAT_ACTIVE;
        fxrec->cb.fxupd((zCutsceneMgr*)csnmgr, fxrec, 0);

        if (!(fxrec->flg_stat & NCIN_STAT_FIRST))
        {
            fxrec->flg_stat &= ~NCIN_STAT_8;
        }

        fxrec->flg_stat &= ~NCIN_STAT_FIRST;

        if (fxrec->flg_stat & NCIN_STAT_DEAD)
        {
            fxrec->flg_stat &= ~NCIN_STAT_ACTIVE;
        }

        if (fxrec->flg_stat & NCIN_STAT_ACTIVE)
        {
            if (fxrec->cb.fxanim != NULL)
            {
                need_animated++;
            }

            if (fxrec->cb.fxrend != NULL)
            {
                need_render++;
            }
        }
    }

    flags = 0;

    if (need_animated != 0)
    {
        flags |= 2;
    }

    if (need_render != 0)
    {
        flags |= 1;
    }

    if (flags != 0)
    {
        g_noz_ncin->flg_nosey = flags;
        csn->cb_nosey = g_noz_ncin;
    }
    else
    {
        csn->cb_nosey = NULL;
    }
}

static void AddTables()
{
    zCineFX_Register_TestTable();
    zCineFX_Register_General();
    zCineFX_Register_IntroJelly();
    zCineFX_Register_IntroTurret();
    zCineFX_Register_IntroFogger();
    zCineFX_Register_IntroSlammer();
    zCineFX_Register_IntroBuckOTron();
    zCineFX_Register_IntroFlinger();
    zCineFX_Register_IntroSpinner();
    zCineFX_Register_IntroPopper();
    zCineFX_Register_IntroZapper();
    zCineFX_Register_BossFrogFish();
    zCineFX_Register_BossDenOne();
    zCineFX_Register_BossDenTwo();
    zCineFX_Register_BossNeptune();
    zCineFX_Register_SceneBB02();
    zCineFX_Register_SceneDE01();
    zCineFX_Register_SceneTR02();
    zCineFX_Register_SceneJK01();
    zCineFX_Register_ScenePT01();
    zCineFX_Register_ScenePT02();
}
