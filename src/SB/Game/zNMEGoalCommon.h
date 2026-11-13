#ifndef ZNMEGOALCOMMON_H
#define ZNMEGOALCOMMON_H

#include "zNMECommon.h"

struct zNMEGoalCommon : xGoal
{
    // Inlined into every zNME::GoalCreate<> instance (no symbol).
    zNMEGoalCommon()
    {
        bul_entered = bul_resumed = 0;
        flg_npcgauto = 0;
        flg_npcgable = 0;
    }

    U32 anid_played;
    struct
    {
        S32 flg_npcgauto : 8;
        S32 flg_npcgable : 16;
        S32 bul_entered : 1;
        S32 bul_resumed : 1;
        S32 bul_unused : 6;
    };
    struct
    {
        S32 flg_info : 16;
        S32 flg_user : 16;
    };

    // Inlined (no symbol; the name is not known): is the goal's automatic animation for gspot
    // the one playing?
    S32 AutoAnimIsCurrent(en_npcgspot gspot)
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
        U32 anid = npc->AnimPick(GetID(), gspot, this);
        U32 curid = npc->AnimCurStateID(); // as BfBB's zNPCGoalCommon::PreCalc

        return curid == anid;
    }

    // Inlined (no symbol); picks and starts the goal's automatic animation. As in BfBB, anid is a
    // nested inline's parameter (DoAutoAnim passes it on); as a local, GC/2.6 swaps the npc and
    // anid registers.
    U32 DoAutoAnim(en_npcgspot gspot, S32 forceRestart)
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

        return DoPickedAnim(npc, npc->AnimPick(GetID(), gspot, this), forceRestart);
    }

    // Inlined (no symbol; the name is ours): starts anid (if any) on npc, the goal's owner, and
    // records it as played.
    U32 DoPickedAnim(zNMECommon* npc, U32 anid, S32 forceRestart)
    {
        if (anid)
        {
            if (npc->AnimStart(anid, forceRestart))
            {
                anid_played = anid;
            }
            else
            {
                anid_played = 0;
            }
        }
        return anid;
    }

    // Inlined (no symbol; named after BfBB's zNPCGoalCommon::DoExplicitAnim): starts anid on the
    // owner and records it as played.
    U32 DoExplicitAnim(U32 anid, S32 forceRestart)
    {
        if (((zNMECommon*)psyche->clt_owner)->AnimStart(anid, forceRestart))
        {
            anid_played = anid;
        }
        else
        {
            anid_played = 0;
        }
        return anid_played;
    }

    // All are weak inlines (GoalHandleMail/CollReview are new virtuals at the end of
    // __vt__14zNMEGoalCommon). Enter is declared before Resume: retail emits the weak copies
    // Clear, Enter, Resume, ..., Process (the PS2 DWARF lists them in the same order).
    S32 Process(en_trantype* trantyp, F32 dt, void* ctxt)
    {
        S32 rc = xGoal::Process(trantyp, dt, ctxt);
        bul_entered = bul_resumed = 0;
        return rc;
    }
    S32 Enter(F32 dt, void* updCtxt)
    {
        bul_entered = 1;
        if (flg_npcgauto & 0x2)
        {
            DoAutoAnim(NME_GSPOT_START, 0);
        }
        return 0;
    }
    S32 Resume(F32 dt, void* updCtxt)
    {
        bul_resumed = 1;
        if (flg_npcgauto & 0x4)
        {
            DoAutoAnim(NME_GSPOT_RESUME, 0);
        }
        return 0;
    }
    void Clear()
    {
        flg_info = 0;
        xGoal::Clear();
    }
    virtual S32 GoalHandleMail(NMEMsg* mail)
    {
        return 0;
    }
    virtual S32 CollReview(void*)
    {
        return 0;
    }
};

// Goal factory hooks registered by zNMEGoals_RegisterTypes.
// Every goal class created here declares a default constructor (empty where there is nothing to
// set): CW would otherwise emit some GoalCreate<T> instances out of retail's registration order.
namespace zNME
{
    template <class T> xFactoryInst* GoalCreate(S32 who, RyzMemGrow* grow, void*)
    {
        return new (who, grow) T;
    }

    inline void GoalDestroy(xFactoryInst* inst)
    {
        delete inst;
    }
} // namespace zNME

#endif
