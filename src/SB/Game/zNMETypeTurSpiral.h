#ifndef ZNMETYPETURSPIRAL_H
#define ZNMETYPETURSPIRAL_H

#include "zNMETypeTurret.h"

// Members are in the original source order; Init is the key function that emits the vtable.
struct zNMETurSpiral : zNMETurret
{
    zNMETurSpiral()
    {
    }
    void Init(xEntAsset* asset);
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Reset();
    void Process(F32 dt);
    static S32 Spiral_SeePlyr(xGoal* goal, void* data, en_trantype* trantype, F32 dt, void* ctxt);

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
