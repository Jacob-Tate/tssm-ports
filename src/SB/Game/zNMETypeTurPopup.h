#ifndef ZNMETYPETURPOPUP_H
#define ZNMETYPETURPOPUP_H

#include "zNMETypeTurret.h"

struct zNMETurPopup : zNMETurret
{
    zNMETurPopup()
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
