#ifndef ZNMETYPETURBARREL_H
#define ZNMETYPETURBARREL_H

#include "zNMETypeTurret.h"

struct LobParm
{
    en_haztyp haztype;
    F32 spd_proj;
    F32 rad_lob;
};

// Layout from the PS2 DWARF.
struct NMECfgTBarrel
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Ranges
    {
        F32 rad_plyrSeen;
        F32 rad_plyrGrey;
        F32 rad_shootBeg;
        F32 rad_shootEnd;
    } ranges;
    struct Movement
    {
        F32 spd_rotate;
        F32 spd_retarget;
    } move;
    LobParm lobparm;
    struct Attack
    {
        F32 tym_reload;
        S32 num_salvo;
    } attack;
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMETurBarrel : zNMETurret
{
    zNMETurBarrel()
    {
    }
    S32 pts_health; // 0x1C0
    struct
    {
        S32 flg_reacquire : 1;
        S32 flg_unused : 31;
    };
    xQuat quat_left;
    xQuat quat_right;
    const NMECfgTBarrel* cfg_barrel; // 0x1E8

    void CreateConfig(const char* name);
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    virtual S32 DfltVulnFlags();
    void Init(xEntAsset* asset);
    void Reset();
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 TurBarMailDamage(NMEDamageInfo* dmgmail);
    void CalcEndPoints(const xMat3x3* mat_ref);
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;

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
};

#endif
