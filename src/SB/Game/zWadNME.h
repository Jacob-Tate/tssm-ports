#ifndef ZWADNME_H
#define ZWADNME_H

// zAutoTweak.h first: retail puts its load_param<xColor_tag,S32> template after xDebug.h's
// templates and before xCam.h's.
#include "zAutoTweak.h"
#include "zNMECommon.h"
#include "zNMEGoalCommon.h"
#include "zNMEGoalStd.h"
#include "zNMETypeStandard.h"
#include "zNMETypeCritter.h"
#include "zNMETypeCritBasic.h"
#include "zNMETypeSBBat.h"
#include "zNMETypeBossDennis.h"
#include "xModelBlur.h"
#include "zStreamedSound.h"
#include "zBossCam.h"
#include "zFXRibbonPool.h"
#include "zLightning.h"
#include "zGlobals.h"
#include "iTime.h"

struct zNMEStandard;
struct zNMEHazard;
struct zLightning;

// zNMEGoals.cpp
void zNMEGoals_RegisterTypes(xFactory* fac);

// ---- zNMETypeBossDenToo.h goals ----

struct zNMEDenToo;

struct zNMEGoalDentNormal : zNMEGoalCommon
{
    zNMEGoalDentNormal()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalDentEvade : zNMEGoalCommon
{
    xParabola* parab;
    xFXRibbon* ribbon;
    F32 tmr_leaping;
    S8 cnt_skipRibbonJoint;
    S8 status;
    S8 padded[2];

    zNMEGoalDentEvade()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void SplashLanding(zNMEDenToo* npc);
    S32 UpdateAnim(zNMEDenToo* npc, F32 dt);
    S32 PathUpdate(zNMEDenToo* npc, F32 dt);
    S32 PathParabola(zNMEDenToo* npc, F32 dt);
    S32 PathDirect(zNMEDenToo* npc, F32 dt);
    void RibbonLoop(zNMEDenToo* npc, F32 dt);
    S32 GoalHandleMail(NMEMsg* mail);
    void ParabSetup(zNMEDenToo* npc);
    void RibbonStart();
};

struct zNMEGoalDentBattle : zNMEGoalCommon
{
    F32 tmr_tossDelay;

    zNMEGoalDentBattle()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkExitCases(zNMEDenToo* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalDentAttack : zNMEGoalCommon
{
    S32 idx_shot;
    S32 alt_throw;
    xVec3 pos_tgtBasis;

    zNMEGoalDentAttack()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    inline S32 NumKnivesToThrow(zNMEDenToo* npc);
    inline void ThrowUpdate(zNMEDenToo* npc);
    S32 ThrowKnife_Toes(zNMEDenToo* npc, S32 idx);
};

struct zNMEGoalDentTaunt : zNMEGoalCommon
{
    zNMEGoalDentTaunt()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalDentDamage : zNMEGoalCommon
{
    zNMEGoalDentDamage()
    {
        SetFlags(0x8);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkExistence(zNMEDenToo* npc, F32 dt, S32& nextgoal, en_trantype& trantype);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalDentDead : zNMEGoalDead
{
    zNMEGoalDentDead()
    {
        SetFlags(0x8);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

struct zNMEGoalJellyAttack : zNMEGoalPushAnim
{
    S32 flg_attack;
    zLightning* zap_lytnin[3];

    zNMEGoalJellyAttack()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void ZapperStart();
    void ZapperUpdate();
    // No GC symbol (always inlined into zNMEGoalJellyAttack::Exit, where the PS2 line table has
    // it on one line). The name is ours, after ZapperStart.
    void ZapperStop()
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

        // eNMESound_Special1
        if (npc->snd_table->sndGroupHandle[7] != -1)
        {
            xSndMgrStop(npc->sndID[7]);
            npc->sndID[7] = (iSndHandle)-1;
        }

        for (S32 i = 0; i < 3; i++)
        {
            if (zap_lytnin[i] != NULL)
            {
                zLightningKill(zap_lytnin[i]);
                zap_lytnin[i] = NULL;
            }
        }
    }
};

struct zNMEGoalJellyDying : zNMEGoalCritDying
{
    zNMEGoalJellyDying()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

// ---- zNMETypeTurret.h / zNMETypeTurTurner.h / zNMETypeTurPopup.h / zNMETypeTurBarrel.h /
//      zNMETypeTurSpiral.h goals ----

struct zNMETurBarrel;

struct zNMEGoalTurrIdle : zNMEGoalCommon
{
    zNMEGoalTurrIdle()
    {
        SetFlags(0x4);
        flg_npcgauto |= 0x6;
    }
};

struct zNMEGoalTurrReload : zNMEGoalPushAnim
{
    zNMEGoalTurrReload()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }
};

struct zNMEGoalTurrHurt : zNMEGoalPushAnim
{
    zNMEGoalTurrHurt()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalTurrDead : zNMEGoalDead
{
    zNMEGoalTurrDead()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* msg);
};

struct zNMEGoalTurrTReady : zNMEGoalCommon
{
    zNMEGoalTurrTReady()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalTurrTTurn : zNMEGoalCommon
{
    F32 tmr_shootDelay;

    zNMEGoalTurrTTurn()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalTurrTShoot : zNMEGoalLoopAnim
{
    zNMEGoalTurrTShoot()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }
};

struct zNMEGoalTurrPDormant : zNMEGoalCommon
{
    zNMEGoalTurrPDormant()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalTurrPAlert : zNMEGoalCommon
{
    F32 tmr_shootDelay;

    zNMEGoalTurrPAlert()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalTurrPPatAlpha : zNMEGoalLoopAnim
{
    zNMEGoalTurrPPatAlpha()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }
};

struct zNMEGoalTurrBIdle : zNMEGoalCommon
{
    zNMEGoalTurrBIdle()
    {
        SetFlags(0x4);
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

enum en_rotinf
{
    ROTINF_TURNING,
    ROTINF_ARRIVED,
    ROTINF_ENDPOINT
};

struct zNMEGoalTurrBTurn : zNMEGoalCommon
{
    S32 idx_shot;
    F32 tmr_reloading;
    struct
    {
        S32 turnState : 8;
        S32 flg_goLeft : 1;
        S32 flg_doReload : 1;
        S32 flg_unused : 22;
    };

    zNMEGoalTurrBTurn()
    {
        SetFlags(0x4);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 SubStateUpdate(zNMETurBarrel* npc, F32 dt, S32& nextgoal, en_trantype& trantype);
    void RetargetPlayer(zNMETurBarrel* npc);
    en_rotinf RotateUpdate(F32 dt);
    S32 QRotTowards(xMat3x3* mat, const xQuat* qfrom, const xQuat* qdest, F32 dt, F32 spd);
    S32 GoToStart(zNMETurBarrel* npc, F32 dt);
    S32 NextShot(zNMETurBarrel* npc);
};

struct zNMEGoalTurrBShoot : zNMEGoalPushAnim
{
    S32 idx_shot;

    zNMEGoalTurrBShoot()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 BangBang(zNMETurBarrel* npc);
};

struct zNMEGoalTurrBHurt : zNMEGoalCommon
{
    F32 tmr_hurted;

    zNMEGoalTurrBHurt()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalTurrSpiral : zNMEGoalCommon
{
    S32 cnt_nextlos;
    F32 len_laser;

    zNMEGoalTurrSpiral()
    {
        SetFlags(0x4);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void EmitSteam(F32 dt);
};

// ---- zNMETypeStdFogger.h goals ----

struct zNMEFogger;

enum en_battlefog
{
    FOGGER_BATTLE_ARENA,
    FOGGER_BATTLE_CHASE,
    FOGGER_BATTLE_EVADE,
    FOGGER_BATTLE_NOMORE,
    FOGGER_BATTLE_FORCE = 0x7fffffff
};

struct zNMEGoalFogAware : zNMEGoalCommon
{
    S32 lastFoot;

    zNMEGoalFogAware()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkTranCases(zNMEFogger* npc, S32& nextgoal, en_trantype& trantype);
    void FeetFXUpdate(zNMEFogger* npc, F32 dt);
};

struct zNMEGoalFogBattle : zNMEGoalCommon
{
    en_battlefog battlefog;
    struct
    {
        S32 lastFoot : 8;
        S32 flg_taunting : 1;
        S32 flg_needHopFX : 1;
        S32 flg_unused : 22;
    };
    xVec3 pos_evade;
    F32 tmr_evade;

    zNMEGoalFogBattle()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 CheckSituation(zNMEFogger* npc, S32& nextgoal, en_trantype& trantype);
    S32 SubStateUpdate(zNMEFogger* npc, F32 dt, S32& nextgoal, en_trantype& trantype);
    void ChasePlayer(zNMEFogger* npc, F32 dt, xVec3* pos_tgt);
    F32 PredictPlyrPos(zNMEFogger* npc, xVec3* pos_plyr, xVec3* dir_plyr);
    F32 RevisePrediction(zNMEFogger* npc, xVec3* pos_tgt, xVec3* dir_tgt);
    void GetInArena(zNMEFogger* npc, F32 dt);
    S32 CalcEvadePos(zNMEFogger* npc, xVec3* pos);
    S32 MoveEvadePos(zNMEFogger* npc, const xVec3* pos, F32 dt);
    S32 CheckSpot(zNMEFogger* npc, F32 dt, const xVec3* pos_tgt);
    void BoundingFXUpdate(zNMEFogger* npc, F32 dt);
    void HoppingFXUpdate(zNMEFogger* npc, F32 dt);
};

struct zNMEGoalFogAttack : zNMEGoalLoopAnim
{
    F32 tmr_attack;
    struct
    {
        U32 bul_startedBreath : 1;
        U32 bul_unused : 31;
    };

    zNMEGoalFogAttack()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void BreathAttack();
};

// ---- zNMETypeStdSlammer.h goals ----

struct zNMESlammer;

namespace NMEHelpRib
{
    struct RibData;
}

enum en_alertslam
{
    SLAMMER_BATTLE_NOTICE,
    SLAMMER_BATTLE_BEGIN,
    SLAMMER_BATTLE_CHASE,
    SLAMMER_BATTLE_WHAM,
    SLAMMER_BATTLE_EVADE,
    SLAMMER_BATTLE_NOMORE,
    SLAMMER_BATTLE_FORCE = 0x7fffffff
};

struct zNMEGoalSlamAware : zNMEGoalCommon
{
    zNMEGoalSlamAware()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkTranCases(zNMESlammer* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalSlamBattle : zNMEGoalCommon
{
    en_alertslam alertslam;
    F32 tmr_alertham;

    zNMEGoalSlamBattle()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 Chk_EarlyExit(zNMESlammer* npc, S32& nextgoal, en_trantype& trantype);
    S32 SubStateProcess(zNMESlammer* npc, F32 dt, S32& nextgoal, en_trantype& trantype);
    void MoveEvade(zNMESlammer* npc, F32 dt);
    void PredictPlyrPos(zNMESlammer* npc, xVec3* pos_plyr);
    void MoveChase(zNMESlammer* npc, F32 dt, const xVec3* pos_tgt);
    S32 CanIAttackNow(zNMESlammer* npc, const xVec3* pos_tgt);
    F32 CalcLean(zNMESlammer* npc, F32 dt, const xVec3* dir_dest);
};

struct zNMEGoalSlamAttack : zNMEGoalPushAnim
{
    xVec3 pos_lastVert;
    xVec3 pos_oldVert;
    struct
    {
        S32 flg_hitTarget : 1;
        S32 flg_hitPlayer : 1;
        S32 flg_didEffect : 1;
        S32 flg_needWave : 1;
        S32 flg_unused : 28;
    };
    xFXRibbon* ribhorz;
    xFXRibbon* ribvert;
    S32 cnt_skipRibbonJoint;

    zNMEGoalSlamAttack()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkPrelimTran(zNMESlammer* npc, S32& nextgoal, en_trantype& trantype);
    S32 WaveChecks(zNMESlammer* npc);
    void DoCapsuleTests(zNMESlammer* npc);
    void RibbonPrep(zNMESlammer* npc);
    void RibbonStep(zNMESlammer* npc);
    void RibbonHorz(zNMESlammer* npc, xFXRibbon* rib, NMEHelpRib::RibData* ribhelp);
    void RibbonVert(zNMESlammer* npc, xFXRibbon* rib, NMEHelpRib::RibData* ribhelp);
};

// ---- zNMETypeStdSpinner.h goals ----

struct zNMESpinner;

struct zNMEGoalSpinAware : zNMEGoalCommon
{
    zNMEGoalSpinAware()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkTranCases(zNMESpinner* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalSpinBattle : zNMEGoalCommon
{
    F32 tmr_minAttack;
    S8 cnt_skipBallChecks;
    S8 cnt_skipPlyrChecks;
    S8 cnt_skipNMEChecks;
    S8 cnt_skipRibbonJoint;
    S8 cnt_bubemit;
    S8 unused[3];
    xFXRibbon* ribbon;

    zNMEGoalSpinBattle()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Suspend(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void StepAwayFromTheSponge(zNMESpinner* npc, F32 dt);
    S32 RetreatAndFinish(zNMESpinner* npc, F32 dt);
    S32 PursueAndDestroy(zNMESpinner* npc, F32 dt);
    void WormsAreMean(zNMESpinner* npc);
    S32 ColChk_Player(zNMESpinner* npc);
    S32 ColChk_BBall(zNMESpinner* npc);
    S32 ColChk_NMEDyn(zNMESpinner* npc);
    void RibbonLoop(zNMESpinner* npc);
};

// Grid callback for the spinner's NME collision sweep (zNMEGoalSpinBattle::ColChk_NMEDyn).
struct BattleCollideData
{
    zNMEGoalSpinBattle* goal_battle;
    zNMESpinner* npc;
    xBound bound;

    U8 operator()(xEnt& ent);
};

// ---- zNMETypeStdFlinger.h goals ----

struct zNMEFlinger;

struct zNMEGoalFlingNormal : zNMEGoalCommon
{
    zNMEGoalFlingNormal()
    {
        SetFlags(0xC);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalFlingAware : zNMEGoalCommon
{
    zNMEGoalFlingAware()
    {
        SetFlags(0xC);
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalFlingBattle : zNMEGoalCommon
{
    F32 tmr_tossDelay;

    zNMEGoalFlingBattle()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 SubStateUpdate(zNMEFlinger* npc, F32 dt, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalFlingAttack : zNMEGoalCommon
{
    S32 idx_shot;

    zNMEGoalFlingAttack()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ReadyAimFire(zNMEFlinger* npc);
};

struct zNMEGoalFlingBoing : zNMEGoalCommon
{
    F32 tmr_minBoing;
    struct
    {
        S32 flg_toldMoverIgnore : 1;
        S32 flg_otherstuf : 31;
    };

    zNMEGoalFlingBoing()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalFlingFlee : zNMEGoalCommon
{
    F32 tmr_minFlee;
    struct
    {
        S32 flg_toldMoverIgnore : 1;
        S32 flg_otherstuf : 31;
    };

    zNMEGoalFlingFlee()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalFlingPanic : zNMEGoalCommon
{
    zNMEGoalFlingPanic()
    {
    }
};

struct zNMEGoalFlingMove : zNMEGoalSentry
{
    zNMEGoalFlingMove()
    {
        flg_npcgauto &= ~0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void FiddleThrottle(zNMEFlinger* npc, F32 dt);
    void TellMyNukeFam(en_sentell note, zNMECommon* npc);
};

// ---- zNMETypeStdPopper.h goals ----

struct zNMEPopper;

struct zNMEGoalPopNormal : zNMEGoalCommon
{
    zNMEGoalPopNormal()
    {
        SetFlags(0xC);
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalPopAware : zNMEGoalCommon
{
    zNMEGoalPopAware()
    {
        SetFlags(0xC);
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkAware(zNMEPopper* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalPopWound : zNMEGoalCommon
{
    zNMEGoalPopWound()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalPopEvade : zNMEGoalCommon
{
    F32 tmr_emitTrail;
    F32 tmr_emitGround;
    F32 tmr_emitFount;
    xVec3 pos_lastTime;

    zNMEGoalPopEvade()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void EmergeFount(zNMECommon* npc, F32 dt);
    void EmergeGround(zNMECommon* npc, F32 dt);
    void EvadeTrail(zNMECommon* npc, F32 dt);
};

struct zNMEGoalPopBattle : zNMEGoalCommon
{
    F32 tmr_tossDelay;

    zNMEGoalPopBattle()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkTranCases(zNMEPopper* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalPopAttack : zNMEGoalLoopAnim
{
    S32 idx_shot;

    zNMEGoalPopAttack()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void PopGunUpdate(zNMEPopper* npc, F32 dt);
};

// ---- zNMETypeStdZapper.h goals ----

struct zNMEZapper;

struct zNMEGoalZapNormal : zNMEGoalCommon
{
    zNMEGoalZapNormal()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalZapAware : zNMEGoalCommon
{
    zNMEGoalZapAware()
    {
        SetFlags(0xC);
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkTheSituation(zNMEZapper* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalZapBattle : zNMEGoalCommon
{
    F32 tmr_tossDelay;

    zNMEGoalZapBattle()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkTheSituation(zNMEZapper* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalZapWound : zNMEGoalCommon
{
    F32 tmr_inPain;

    zNMEGoalZapWound()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalZapZap : zNMEGoalLoopAnim
{
    zNMEGoalZapZap()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalZapMove : zNMEGoalSentry
{
    zNMEGoalZapMove()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

// ---- zNMETypeStdMervyn.h goals ----

struct zNMEMervyn;

struct zNMEGoalMervNormal : zNMEGoalCommon
{
    zNMEGoalMervNormal()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalMervAware : zNMEGoalCommon
{
    zNMEGoalMervAware()
    {
        SetFlags(0xC);
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalMervBattle : zNMEGoalCommon
{
    F32 tmr_tossDelay;

    zNMEGoalMervBattle()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 ChkExitCases(zNMEMervyn* npc, S32& nextgoal, en_trantype& trantype);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalMervZap : zNMEGoalLoopAnim
{
    zNMEGoalMervZap()
    {
        SetFlags(0x2);
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalMervBomb : zNMEGoalCommon
{
    struct
    {
        S32 flg_leftside : 1;
        S32 flg_bomb : 31;
    };
    S32 idx_shot;

    zNMEGoalMervBomb()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 BombsAway(zNMEMervyn* npc);
    void PickTarget(zNMEMervyn* npc, xVec3* tgt, const xVec3* src);
};

struct zNMEGoalMervBowl : zNMEGoalCommon
{
    F32 tmr_linger;
    F32 tmr_repelMin;

    zNMEGoalMervBowl()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 RepelBowlBall(F32 dt);
};

struct zNMEGoalMervWound : zNMEGoalCommon
{
    F32 tmr_inPain;

    zNMEGoalMervWound()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalMervMove : zNMEGoalSentry
{
    zNMEGoalMervMove()
    {
        flg_npcgauto &= ~0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

// ---- zNMETypeBuckOTron.h goals ----

struct zNMEBuckotron;

struct zNMEGoalBuckRunning : zNMEGoalCommon
{
    zNMEGoalBuckRunning()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* msg);
};

struct zNMEGoalBuckBirthing : zNMEGoalPushAnim
{
    zNMEGoalBuckBirthing()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void BubsieSudsie(zNMEBuckotron* npc);
};

struct zNMEGoalBuckDying : zNMEGoalCommon
{
    zNMEGoalBuckDying()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
    void SDS_Countdown(zNMEBuckotron* npc, F32 dt);
};

struct zNMEGoalBuckDead : zNMEGoalDead
{
    zNMEGoalBuckDead()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
};

// No GC symbol (always inlined: NME_entwrap_update, zNMEDennis::NewTime); defined here because
// it needs xglobals.
inline void zNMENPCWrapper::UpdateWrapper(F32 dt)
{
    if (flg_profProc)
    {
        iProfileClear(0);
    }

    if (_isCulled)
    {
        model->Flags &= (U16)~0x2;
    }
    else if (!(flg_upward & 0x1) && model != NULL)
    {
        model->Flags |= 0x2;
    }

    xEntBeginUpdate(this, xglobals->sceneCur, dt);
    flg_inUpdate = 1;
    Process(dt);
    flg_inUpdate = 0;
    xEntEndUpdate(this, xglobals->sceneCur, dt);

    if (flg_profProc)
    {
        iFuncProfileFuncs(300, 0, 0.0f);
    }
}

inline iSndHandle zNMECommon::SndPlayFromTable(S32 idx)
{
    U32 flags;
    iSndGroupHandle hdl;

    if (globals.cmgr != NULL)
    {
        return (iSndHandle)-1;
    }

    // An exclusive sound does not restart while it is still playing.
    if ((snd_table->flags[idx] & 0x8000) && sndID[idx] != -1)
    {
        return (iSndHandle)-1;
    }

    if (!snd_table->IsReady(idx))
    {
        return (iSndHandle)-1;
    }

    flags = 0;
    if (!(snd_table->flags[idx] & 0x2000))
    {
        flags |= 0x2;
    }
    if (!(snd_table->flags[idx] & 0x1000))
    {
        flags |= 0x2000;
    }

    // 0x2000: play at the model's position instead of following the entity.
    hdl = snd_table->sndGroupHandle[idx];
    if (!(snd_table->flags[idx] & 0x2000))
    {
        sndID[idx] = xSndMgrPlay(hdl, flags, NULL, NULL, this, NULL, NULL);
    }
    else
    {
        sndID[idx] = xSndMgrPlay(hdl, flags, (xVec3*)&model->Mat->pos, NULL, NULL, NULL, NULL);
    }

    snd_table->timer[idx] = snd_table->time[idx] + iTimeGetGame();
    return sndID[idx];
}

// No GC or PS2 symbol (always inlined, identically, into zNMEGoalDentTaunt::Process and
// zNMEGoalDenTaunt::Process): keep taunting while the player is dead, or is still recovering from
// a hit and is not right next to us. The name is ours.
inline S32 NMEC_TauntStillValid(zNMECommon* npc)
{
    S32 keepTaunting;

    if (globals.player.s->Health < 1)
    {
        keepTaunting = 1;
    }
    else if (globals.player.DamageTimer < 0.25f)
    {
        keepTaunting = 0;
    }
    else if (npc->XZDstSqToPlayer(NULL, NULL) < 100.0f)
    {
        keepTaunting = 0;
    }
    else
    {
        keepTaunting = 1;
    }

    return keepTaunting;
}

inline void zNMENavNet::MvptReset(zMovePoint* nav_goto)
{
    if (nav_goto != NULL)
    {
        nav_dest = nav_goto;
    }
    else
    {
        nav_dest = nme_owner->MvptFirstFollow();
    }
    nav_past = nav_dest;
    nav_curr = nav_dest;
    nav_lead = nav_dest;
    spl_mvptspline = NULL;
    len_mvptspline = 0.0f;
    dst_curspline = 0.0f;
}

inline S32 zNMECommon::NMEMessage(NMEMsg* mail)
{
    S32 handled;
    xPsyche* psy = psy_self;

    if (psy != NULL)
    {
        zNMEGoalCommon* curgoal = (zNMEGoalCommon*)psy->GetCurGoal();

        if (curgoal != NULL)
        {
            handled = curgoal->GoalHandleMail(mail);
            if (handled)
            {
                return handled;
            }
        }
    }

    handled = TypeHandleMail(mail);
    if (!handled)
    {
        handled = DfltHandleMail(mail);
    }

    return handled;
}

inline void zNMECommon::Common_Reset()
{
    zNMEGoalDead* goal;

    if (frame != NULL)
    {
        xVec3Copy(&frame->drot.axis, &g_Y3);
        frame->drot.angle = 0.0f;
        xVec3Copy(&frame->rot.axis, &g_Y3);
        frame->rot.angle = asset->ang.x;
    }

    RestoreColFlags();

    if (model->Anim != NULL)
    {
        xAnimPlaySetState(model->Anim->Single, model->Anim->Table->StateList, 0.0f);
    }

    if (drv_data != NULL)
    {
        drv_data->Reset();
    }

    tmr_common[0] = -1.0f;
    tmr_common[1] = -1.0f;
    tmr_common[2] = -1.0f;

    runtimeData.flg_enableWander = runtimeData.orig.flg_enableWander;
    runtimeData.flg_enablePatrol = runtimeData.orig.flg_enablePatrol;
    runtimeData.flg_enableDetect = runtimeData.orig.flg_enableDetect;
    runtimeData.flg_activeOn = runtimeData.orig.flg_activeOn;
    runtimeData.flg_takeNoDamage = runtimeData.orig.flg_takeNoDamage;
    runtimeData.flg_unused = 0;
    runtimeData.overrideDetect = runtimeData.orig.overrideDetect;
    runtimeData.overrideAttack = runtimeData.orig.overrideAttack;

    if (psy_self != NULL)
    {
        goal = (zNMEGoalDead*)psy_self->HasGoal(NME_GOAL_AFTERLIFE);
        if (goal != NULL)
        {
            goal->stylinDyin = DIE_LOUDLY;
        }
    }

    if (navnet != NULL)
    {
        navnet->MvptReset(NULL);
    }
}

#endif
