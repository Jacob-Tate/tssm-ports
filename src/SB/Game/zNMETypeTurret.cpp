#include "zNMETypeTurret.h"

U32 g_hash_turranim[TURR_ANIM_NOMORE];
char* g_strz_turranim[TURR_ANIM_NOMORE] = {
    "Unknown",    "Idle",  "Idle_Alert", "Shoot",  "Shoot_Prep", "Shoot_Loop",
    "Shoot_Done", "Taunt", "Reload",     "Damage", "Pop_Up",     "Pop_Down",
};

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_Turret_Startup()
{
    for (S32 i = 0; i < TURR_ANIM_NOMORE; i++)
    {
        g_hash_turranim[i] = xStrHash(g_strz_turranim[i]);
    }
}

void zNMETurret::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);
}

U32 zNMETurret::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    U32 da_anim = g_hash_turranim[TURR_ANIM_INACTIVE];
    return da_anim;
}

xAnimTable* zNMETurret::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMETurret", 0);
    }

    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_INACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}

zMovePoint* zNMETurret::MvptFirstTarget()
{
    zMovePoint* nav;

    if (((zNMEAssetTurret*)nmeass)->aid_mvptTarget == 0)
    {
        return NULL;
    }

    nav = zMovePoint_From_xAssetID(((zNMEAssetTurret*)nmeass)->aid_mvptTarget);
    return nav;
}

S32 zNMEGoalTurrHurt::GoalHandleMail(NMEMsg* msg)
{
    S32 handled = 0;

    switch (msg->msgid)
    {
    case NME_MID_DAMAGE:
    case NME_MID_STUN:
        handled = 1;
        break;
    }

    return handled;
}

S32 zNMEGoalTurrDead::Enter(F32 dt, void* updCtxt)
{
    ((zNMECommon*)psyche->clt_owner)->DieTheGoodDeath();

    return zNMEGoalDead::Enter(dt, updCtxt);
}

S32 zNMEGoalTurrDead::GoalHandleMail(NMEMsg* msg)
{
    S32 handled = 0;

    switch (msg->msgid)
    {
    case NME_MID_DAMAGE:
    case NME_MID_STUN:
        handled = 1;
        break;
    }

    return handled;
}
