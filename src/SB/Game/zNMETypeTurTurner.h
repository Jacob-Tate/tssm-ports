#ifndef ZNMETYPETURTURNER_H
#define ZNMETYPETURTURNER_H

#include "zNMETypeTurret.h"

struct zNMETurTurner : zNMETurret
{
    zNMETurTurner()
    {
    }
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Reset();
    void Process(F32 dt);
    S32 TypeHandleMail(NMEMsg* mail);
};

#endif
