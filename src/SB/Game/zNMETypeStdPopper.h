#ifndef ZNMETYPESTDPOPPER_H
#define ZNMETYPESTDPOPPER_H

#include "zNMETypeStandard.h"
#include "zScene.h"

// Layout from the PS2 DWARF.
struct NMECfgPopper
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Ranges
    {
        F32 rad_tooclose;
        F32 rad_lob;
        F32 rad_lobgrey;
        F32 rad_aware;
        F32 rad_awaregrey;
    } ranges;
    struct Attack
    {
        en_haztyp haztyp;
        F32 spd_proj;
        S32 num_salvo;
    } attack;
    struct SimpShot
    {
        F32 spd_incr;
        F32 dst_minRange;
        F32 tym_leadTarget;
        F32 hyt_offset;
        F32 hyt_incr;
    } simpshot;
    struct ToeToe
    {
        F32 dst_short;
        F32 dst_long;
    } toetoe;
    struct ShotGun
    {
        F32 dst_farout;
        F32 rad_ud;
        F32 rad_lr;
    } shotgun;
    struct PopGun
    {
        F32 dst_farout;
        F32 rad_ud;
        F32 rad_lr;
    } popgun;
    struct Battle
    {
        F32 tym_reload;
    } battle;
    struct Evade
    {
        F32 spd_evade;
    } evade;
};

struct PopTop
{
    xEnt* ent_lid[2];
    F32 tmr_live[2];

    void LidUpdate(F32 dt);
    // No GC symbol (inlined); out of line in the PS2 demo. The first static entity a
    // ConnectToChild link of the move point points to.
    xEnt* LidLocate(const zMovePoint* mvpt)
    {
        xEnt* da_lid;
        S32 i;
        xLinkAsset* link;
        xBase* mychild;

        if (mvpt == NULL)
        {
            return NULL;
        }

        da_lid = NULL;
        for (i = 0; i < mvpt->linkCount; i++)
        {
            link = &mvpt->link[i];
            if (link->dstEvent == eEventConnectToChild)
            {
                mychild = zSceneFindObject(link->dstAssetID);
                if (mychild != NULL && mychild->baseType == eBaseTypeStatic)
                {
                    da_lid = (xEnt*)mychild;
                    break;
                }
            }
        }

        return da_lid;
    }

    // No GC or PS2 symbol (always inlined); the name is ours. In all three PS2 callers the lid
    // lookup and the timer store share one source line.
    void LidStart(S32 idx, const zMovePoint* mvpt)
    {
        ent_lid[idx] = LidLocate(mvpt);
        tmr_live[idx] = 1.0f;
    }
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMEPopper : zNMEStandard
{
    zNMEPopper()
    {
    }
    zMovePoint* nav_evade; // 0x1F8
    zMovePoint* nav_evadePrev;
    const NMECfgPopper* cfg_popper; // 0x200
    PopTop lid;
    xGroup* grp_mvpt;

    void CreateConfig(const char* name);
    S32 DfltVulnFlags();
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Init(xEntAsset* asset);
    void Setup();
    void Reset();
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void ProcessInvis(F32 dt);
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    S32 TypeHandleMail(NMEMsg* mail);
    S32 PopMailDamage(NMEDamageInfo* dmgmail);
    S32 PopMailSys(NMESysEvent* sysmail);
    zMovePoint* PickEvadePoint(zMovePoint* nav_from);

    virtual S32 DfltMiscFlags() const
    {
        return 0;
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

#endif
