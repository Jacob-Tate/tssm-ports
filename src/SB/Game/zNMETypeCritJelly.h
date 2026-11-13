#ifndef ZNMETYPECRITJELLY_H
#define ZNMETYPECRITJELLY_H

#include "zWadNME.h"

// Member functions follow the original source order (the PS2 DWARF lists them reversed);
// CreateConfig is the key function that emits __vt__13zNMECritJelly.
struct zNMECritJelly : zNMECritter
{
    zNMECritJelly()
    {
    }
    F32 tmr_warn;
    S32 idx_leg;

    void CreateConfig(const char* name);
    virtual S32 DfltVulnFlags();
    void Setup();
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Reset();
    S32 IsAlive();
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 JellyMailDamage(NMEDamageInfo* dmgmail);
    void DangerSigns();

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
    S32 WannaPoolShadow()
    {
        return 2;
    }
};

#endif
