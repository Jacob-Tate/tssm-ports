#ifndef ZNMETYPECRITTER_H
#define ZNMETYPECRITTER_H

#include "zNMECentral.h"
#include "zNMECommon.h"
#include "zNMEGoalStd.h"

extern U32 g_hash_critanim[5];
extern char* g_strz_critanim[5];

struct zMODCritter : zModule
{
    void ScenePrepare();
    void ScenePostInit();
};

void ZNME_Critter_Startup();

// The anim states and transitions every critter shares. No symbol (always inlined; the name is
// ours); defined in zNMETypeCritter.cpp, where retail keeps its ourAnims template.
inline void zNMECritter_AnimTableAdd(xAnimTable* table);

// Type-specific asset data that trails the common zNMEAsset (offset 0x60). The PS2 DWARF
// has no type for it, so the name is ours and only the fields read so far are defined;
// the total size (0x68) is the one ValidCritter (zNMEAsset.cpp) checks for.
struct zNMEAssetCritter : zNMEAsset
{
    U32 aid_mvptFollow;
    U32 unk_64;
};

// Member functions follow the original source order (the PS2 DWARF lists them reversed).
// The first non-inline virtual is the key function that emits the class's vtable.
struct zNMECritter : zNMECommon
{
    zNMECritter()
    {
    }
    zNMENavNet privNavNet;

    zNMENavNet* Gimme_NavNet()
    {
        return &privNavNet;
    }
    zMovePoint* MvptFirstFollow();
    void Init(xEntAsset* entass);
    void CritBonePos(xVec3* pos, S32 idx_request) const;
};

// ---- zNMETypeCritter.h / zNMETypeCritJelly.h / zNMETypeCritBasic.h goals ----

struct zNMEGoalCritIdle : zNMEGoalCommon
{
    zNMEGoalCritIdle()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalCritPatrol : zNMEGoalCommon
{
    F32 tmr_delay;
    S32 flg_critpat;

    zNMEGoalCritPatrol()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void CritMoveNormal(F32 dt);
    void CritMoveSpline(F32 dt);
};

struct zNMEGoalCritDying : zNMEGoalCommon
{
    xVec3 pos_bumper;
    xVec3 pos_bumpin;
    xVec3 pos_grindin;
    F32 tmr_dying;
    xFXRibbon* ribbon;
    struct
    {
        S32 cnt_skipRibbonJoint : 8;
        S32 unused : 24;
    };

    zNMEGoalCritDying()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 InputInfo(NMEDamageInfo* dmgmail);
    void RibbonUpdate(zNMECritter* npc, F32 dt);
    F32 CalcEndPoint(zNMECritter* npc, xVec3* pos_end, const xVec3* dir_aim);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalCritDead : zNMEGoalDead
{
    zNMEGoalCritDead()
    {
    }

    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalCritAttack : zNMEGoalPushAnim
{
    zNMEGoalCritAttack()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }
};

#endif
