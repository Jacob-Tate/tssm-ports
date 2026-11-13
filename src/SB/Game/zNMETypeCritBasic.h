#ifndef ZNMETYPECRITBASIC_H
#define ZNMETYPECRITBASIC_H

#include "zNMETypeCritter.h"

struct zNMECritBasic : zNMECritter
{
    zNMECritBasic()
    {
    }
    void SelfSetup();
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Reset();
    void Process(F32 dt);
    S32 TypeHandleMail(NMEMsg* mail);
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);

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
