#include "zNMETypeTurSpiral.h"
#include "zNMEParFX.h"
#include "zNMESupport.h"

void zNMETurSpiral::Init(xEntAsset* asset)
{
    zNMETurret::Init(asset);
    zNMEParFX_PartyInvite(NPAR_TYP_FIRESPIRAL, NULL, 0);
}

void zNMETurSpiral::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;
    xGoal* goal;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    goal = psy->AddGoal(NME_GOAL_TURR_IDLE, NULL);
    goal->fun_process = Spiral_SeePlyr;
    goal->cbdata = NULL;

    psy->AddGoal(NME_GOAL_TURR_HURT, NULL);
    psy->AddGoal(NME_GOAL_TURR_DEAD, NULL);
    psy->AddGoal(NME_GOAL_TURR_SPIRAL, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_TURR_IDLE);
}

U32 zNMETurSpiral::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    en_turranim idx;

    switch (gid)
    {
    case NME_GOAL_TURR_IDLE:
        idx = TURR_ANIM_ACTIVE;
        break;
    case NME_GOAL_TURR_HURT:
        idx = TURR_ANIM_DAMAGE;
        break;
    case NME_GOAL_TURR_SPIRAL:
        if (gspot == NME_GSPOT_START)
        {
            idx = TURR_ANIM_SHOOT_PREP;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = TURR_ANIM_SHOOT_LOOP;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = TURR_ANIM_SHOOT_DONE;
        }
        else
        {
            idx = TURR_ANIM_SHOOT_PREP;
        }
        break;
    default:
        idx = TURR_ANIM_ACTIVE;
        break;
    }

    return g_hash_turranim[idx];
}

xAnimTable* zNMETurSpiral::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMETurSpiral", 0);
    }

    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_INACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_ACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_DAMAGE], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[7] = {
        TURR_ANIM_INACTIVE,   TURR_ANIM_ACTIVE,     TURR_ANIM_DAMAGE,  TURR_ANIM_SHOOT_PREP,
        TURR_ANIM_SHOOT_LOOP, TURR_ANIM_SHOOT_DONE, TURR_ANIM_UNKNOWN,
    };
    NMEC_BuildStandardAnimTran(table, g_strz_turranim, ourAnims, 2, 0.2f);

    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_SHOOT_PREP],
                            g_strz_turranim[TURR_ANIM_SHOOT_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);

    return table;
}

void zNMETurSpiral::Reset()
{
    zNMETurret::Reset();

    psy_self->GoalSet(NME_GOAL_TURR_IDLE, 0);
}

void zNMETurSpiral::Process(F32 dt)
{
    psy_self->Timestep(dt, NULL);

    xModelSetScale(model, 0.35f);

    zNMETurret::Process(dt);
}

S32 zNMETurSpiral::Spiral_SeePlyr(xGoal* rawgoal, void* data, en_trantype* trantype, F32 dt,
                                  void* ctxt)
{
    zNMETurSpiral* npc = (zNMETurSpiral*)rawgoal->psyche->clt_owner;

    if (globals.player.s->Health < 1)
    {
        return 0;
    }

    if (NME::GlobalData::flg_wonder)
    {
        return 0;
    }

    if (npc->XZDstSqToPlayer(NULL, NULL) > 625.0f)
    {
        return 0;
    }

    *trantype = GOAL_TRAN_SET;
    return NME_GOAL_TURR_SPIRAL;
}

S32 zNMEGoalTurrSpiral::Enter(F32 dt, void* updCtxt)
{
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

static void TurrSpiral_Spin(zNMETurSpiral* npc, F32 dt);

S32 zNMEGoalTurrSpiral::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurSpiral* npc = (zNMETurSpiral*)psyche->clt_owner;

    if (npc->XZDstSqToPlayer(NULL, NULL) > 900.0f)
    {
        *trantype = GOAL_TRAN_SET;
        return NME_GOAL_TURR_IDLE;
    }

    TurrSpiral_Spin(npc, dt);

    EmitSteam(dt);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No symbol: a plain helper that Process auto-inlines (the unused copy is dead-stripped); the name
// is ours. Placed between Process and EmitSteam, which gives retail's literal order.
static void TurrSpiral_Spin(zNMETurSpiral* npc, F32 dt)
{
    xMat3x3 mat_spin;

    xMat3x3Euler(&mat_spin, 4.712389f * dt, 0.0f, 0.0f);
    xMat3x3Mul((xMat3x3*)npc->model->Mat, &mat_spin, (xMat3x3*)npc->model->Mat);
}

// The PS2 DWARF also lists a 'delta' local (center - pos); it is folded into the expression here,
// as retail has no stack copies for it.
void zNMEGoalTurrSpiral::EmitSteam(F32 dt)
{
    zNMETurSpiral* npc = (zNMETurSpiral*)psyche->clt_owner;

    xVec3 pos_src = *(xVec3*)&npc->model->Mat->pos +
                    (npc->bound.sph.center - *(xVec3*)&npc->model->Mat->pos) * 2.0f;

    xVec3 pos_tgt = *(xVec3*)&npc->model->Mat->at * 15.0f;
    pos_tgt += pos_src;

    xVec3 dir_laser = pos_tgt - pos_src;
    dir_laser.normalize();

    if (--cnt_nextlos < 0)
    {
        xCollis colrec_data;
        xCollis* colrec = &colrec_data;
        memset(colrec, 0, sizeof(xCollis));

        S32 rc = npc->HaveLOSToPos(&pos_tgt, 15.0f, xglobals->sceneCur, NULL, colrec);

        if (!rc && (colrec->flags & 1))
        {
            len_laser = colrec->dist;
        }
        else
        {
            len_laser = 15.0f;
        }

        if (NMEC_LineHitsBound(&pos_src, &pos_tgt,
                               &xglobals->___player_ent_dont_use_directly->bound, colrec) &&
            colrec->dist < 15.0f)
        {
            zEntPlayer_DamageNPCKnockBack(npc, 1, (xVec3*)&npc->model->Mat->pos,
                                          ePlayerNMEDamageDefault);
            len_laser = colrec->dist + xglobals->___player_ent_dont_use_directly->bound.sph.r;
        }

        rc = xrand_GenRandInt32();
        cnt_nextlos = (rc & 1) + 5;

        len_laser = MAX(0.25f, MIN(len_laser, 15.0f));
    }

    pos_tgt = dir_laser * len_laser;
    pos_tgt += pos_src;
}
