#ifndef ZNMETYPEBOSSDENNIS_H
#define ZNMETYPEBOSSDENNIS_H

#include "zNMECommon.h"
#include "zNMEGoalStd.h"
#include "zNMETypeSBBat.h"
#include "xModelBlur.h"
#include "zStreamedSound.h"
#include "zBossCam.h"

// Type-specific asset data that trails the common zNMEAsset (offset 0x60). The PS2 DWARF
// has no type for it, so the name is ours and only the fields read so far are defined;
// the total size (0x70) is the one ValidDennis (zNMEAsset.cpp) checks for.
struct zNMEAssetDennis : zNMEAsset
{
    U32 aid_mvptFollow;
    U32 aid_mvptGroup; // 0x64, read by Reset (named as in zNMEAssetStandard)
    U32 unk_68[2];
};

enum en_denstage
{
    DEN_STAGE_UNKNOWN,
    DEN_STAGE_INTRO,
    DEN_STAGE_A,
    DEN_STAGE_B,
    DEN_STAGE_C,
    DEN_STAGE_OUTRO,
    DEN_STAGE_NOMORE
};

enum en_dbcmode
{
    DEN_CAMMOD_UNKNOWN,
    DEN_CAMMOD_NORMAL,
    DEN_CAMMOD_BINARY,
    DEN_CAMMOD_NOMORE
};

extern U32 g_hash_bossanim[14];
extern char* g_strz_bossanim[14];
extern U32 g_hash_dennanim[31];
extern char* g_strz_dennanim[31];
extern U32 g_hash_dentanim[31];
extern char* g_strz_dentanim[31];

struct zNMEBoss : zNMECommon
{
};

struct NMECfgDennis
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Ranges
    {
        F32 rad_tooclose[2];
        F32 rad_lob;
        F32 rad_lobgrey;
        F32 rad_aware;
        F32 rad_awaregrey;
    } ranges;
    struct Attack
    {
        F32 spd_proj;
        S32 num_salvo;
        F32 tym_shoot;
        F32 dst_spreadHorz;
    } attack;
    struct Battle
    {
        F32 tym_reload;
        F32 tym_leapDelay;
        S32 num_atakPerLeap;
    } battle;
};

struct DennisLetMeKnow : xPSYNote
{
    void Notice(en_psynote note, xGoal* goal, void* data);
};

struct zNMEDennis;

struct zNMEDennis : zNMEBoss
{
    zNMEDennis()
    {
    }
    S32 pts_health; // 0x1c0
    NMECfgDennis* cfg_dennis;
    F32 tmr_nextLeap; // 0x1c8
    S32 cnt_attacksTilEvade;
    zMovePoint* nav_evade;
    zNMESBBat* npc_sbbat;
    union
    {
        en_denstage stageCurr;
        S32 stageCurrNum;
    };
    union
    {
        xModelBlur moveBlur;
        xModelBlur moveBlurShort;
    };
    xModelBlur moveBlurLong;
    zStreamedSoundList oneLiners;
    DennisLetMeKnow tellMeWhen;
    S32 flg_firstTimestep;
    zBossCam_Binary cam_boss;
    en_dbcmode mod_camCurr;
    en_dbcmode mod_camWant;
    xGroup* grp_mvpt;

    // Member functions follow the original source order (the PS2 DWARF lists them reversed);
    // CreateConfig is the key function that emits __vt__10zNMEDennis.
    void CreateConfig(const char* name);
    virtual S32 DfltVulnFlags();
    void SelfSetup();
    zMovePoint* MvptFirstFollow();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Init(xEntAsset* asset);
    void Setup();
    void Reset();
    void Destroy();
    S32 IsAlive();
    void Process(F32 dt);
    void BUpdate(xVec3* pos);
    void NewTime(F32 dt);
    void PostProcess(F32 dt);
    void RenderHud();
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    void BossCamSet(en_dbcmode mod_desired, bool forceCut);
    void MeterRender();
    S32 TypeHandleMail(NMEMsg* mail);
    S32 DenMailDamage(NMEDamageInfo* dmgmail);
    S32 DenMailSys(NMESysEvent* sysmail);
    zMovePoint* PickEvade(zMovePoint* nav_from);

    // No GC symbol (always inlined); the name follows zNMEDenToo::PickEvadePoint, which has
    // the same structure.
    zMovePoint* PickEvadePoint(zMovePoint* nav_from)
    {
        zMovePoint* nav_there;

        if (grp_mvpt == NULL)
        {
            if (nav_from != NULL)
            {
                xMovePointGetNext(nav_from, NULL, (xMovePoint**)&nav_there, NULL);
                if (nav_there == NULL)
                {
                    return nav_from;
                }
                return nav_there;
            }
            return MvptFirstFollow();
        }
        return PickEvade(NULL);
    }

    // No GC symbol (always inlined into Reset); the name is ours.
    void MvptGroupFind()
    {
        xGroup* grp = NULL;

        if (((zNMEAssetDennis*)nmeass)->aid_mvptGroup != 0)
        {
            grp = (xGroup*)zSceneFindObject(((zNMEAssetDennis*)nmeass)->aid_mvptGroup);
        }
        grp_mvpt = grp;
        if (grp_mvpt == NULL)
        {
            // The result is not used (also in the PS2 demo).
            MvptFirstFollow();
        }
    }

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

struct zNMEGoalDenNormal : zNMEGoalCommon
{
    zNMEGoalDenNormal()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalDenEvade : zNMEGoalCommon
{
    xParabola* parab;
    xFXRibbon* ribbon;
    F32 tmr_leaping;
    S8 cnt_skipRibbonJoint;
    S8 status;
    S8 padded[2];

    zNMEGoalDenEvade()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 UpdateAnim(zNMEDennis* npc, F32 dt);
    S32 PathUpdate(zNMEDennis* npc, F32 dt);
    S32 PathParabola(zNMEDennis* npc, F32 dt);
    S32 PathDirect(zNMEDennis* npc, F32 dt);
    void RibbonLoop(zNMEDennis* npc, F32 dt);
    S32 GoalHandleMail(NMEMsg* mail);
    void ParabSetup(zNMEDennis* npc);
};

struct zNMEGoalDenBattle : zNMEGoalCommon
{
    F32 tmr_tossDelay;

    zNMEGoalDenBattle()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkExitCases(zNMEDennis* npc, S32& nextgoal, en_trantype& trantype);
};

struct zNMEGoalDenAttack : zNMEGoalCommon
{
    S32 idx_shot;
    xVec3 pos_tgtBasis;

    zNMEGoalDenAttack()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void ThrowUpdate(zNMEDennis* npc);
    S32 GolfBomb(zNMEDennis* npc, S32 idx);
};

struct zNMEGoalDenTaunt : zNMEGoalCommon
{
    zNMEGoalDenTaunt()
    {
        SetFlags(0x2);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalDenDamage : zNMEGoalCommon
{
    zNMEGoalDenDamage()
    {
        SetFlags(0x8);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 ChkExistence(zNMEDennis* npc, F32 dt, S32& nextgoal, en_trantype& trantype);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalDenDead : zNMEGoalDead
{
    zNMEGoalDenDead()
    {
        SetFlags(0x8);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

inline void zNMESBBat::FollowTheBoss()
{
    if (npc_bossman != NULL)
    {
        zNMEDennis* boss = npc_bossman;

        *model->Mat = *boss->model->Mat;
        AnimCurSingle()->Time = boss->AnimTimeCurrent();
    }
}

void ZNME_Boss_Startup();

#endif
