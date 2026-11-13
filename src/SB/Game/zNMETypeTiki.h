#ifndef ZNMETYPETIKI_H
#define ZNMETYPETIKI_H

#include "zNMECommon.h"
#include "zWadNME.h"
#include "zNPCTypes.h"
#include "xString.h"

struct zScene;

struct hoverData
{
    F32 tmr_hovBubDelay;
    F32 tmr_hovBubEmit;
};

struct explodeData
{
    S32 placeholder;
};

// Type-specific asset data that trails the common zNMEAsset (offset 0x60). The PS2 DWARF
// has no type for it, so the name is ours and only the fields read so far are defined.
struct zNMEAssetTiki : zNMEAsset
{
    U32 aid_mvptFollow;
};

struct zNMETiki : zNMECommon
{
    zNMETiki()
    {
    }
    U32 tikiFlag; // 0x1C0
    xBound origLocalBound;
    xVec3 deltaPos;
    F32 timeToLive;
    F32 t1;
    F32 t2;
    F32 t3;
    xVec3 v1;
    xMat4x3* rigidAnim;
    union
    {
        hoverData hovdat;
        explodeData xplod;
    };
    F32 vel;
    F32 landHt;
    xEnt* nonTikiParent;
    xVec3 nonTikiParentDisp;
    U8 numParents;
    zNMETiki* parents[4];
    U8 numChildren;
    zNMETiki* children[4];
    zNMETiki* nextOrphan;
    U32 updatedParents;
    S32 contactParent;
    void* tikiAnim;
    F32 tikiAnimTime;
    zShrapnelAsset* death_explosion;

    // Member order follows the PS2 DWARF. DfltVulnFlags is a new virtual; the rest override
    // zNMECommon virtuals or are non-virtual.
    virtual S32 DfltVulnFlags() const
    {
        en_npctyp ntyp = (en_npctyp)itemType;
        if (ntyp == NME_TYPE_TIKI_STONE)
        {
            return 0x3;
        }
        return 0xFFFF0003;
    }
    void HoverBubCone(xVec3* pos_emitBase, xVec3* dir_emit, S32 num_bub, F32 rad_cone,
                      F32 rad_emitJitter, F32 spd_emitBase, F32 spd_emitRand, F32 scl_bub);
    void HoverBubStream(F32 dt);
    void HoverBubbles(F32 dt);

    // No GC symbols (always inlined). AddChild, RemoveChild and RemoveParent are not in the PS2
    // DWARF; names and bodies as BfBB's zNPCTiki.
    void AddChild(zNMETiki* child)
    {
        U8 i = 0;

        while (children[i] != NULL)
        {
            i++;
        }

        children[i] = child;
        numChildren++;
    }
    void RemoveChild(zNMETiki* child)
    {
        U8 i = 0;

        if (numChildren == 0)
        {
            return;
        }

        while (children[i] != child && i < 4)
        {
            i++;
        }

        if (children[i] != child)
        {
            return;
        }

        children[i] = NULL;
        numChildren--;
    }
    void RemoveParent(zNMETiki* parent)
    {
        U8 i = 0;

        if (numParents == 0)
        {
            return;
        }

        while (parents[i] != parent && i < 4)
        {
            i++;
        }

        if (parents[i] == parent)
        {
            parents[i] = NULL;
            numParents--;
        }

        if (i == contactParent)
        {
            contactParent = -1;
            tikiFlag |= 0x1;
        }

        tikiFlag |= 0x4;
    }
    // No GC symbol and not in the PS2 DWARF (always inlined into ProcSubContract); name and body
    // as BfBB's zNPCTiki.
    void ParentUpdated(zNMETiki* parent)
    {
        for (S32 i = 0; i < 4; i++)
        {
            if (parent == parents[i])
            {
                updatedParents |= (1 << i);
                return;
            }
        }
    }
    void RemoveFromFamily()
    {
        U8 i;

        for (i = 0; i < 4; i++)
        {
            if (parents[i] != NULL)
            {
                parents[i]->RemoveChild(this);
                parents[i] = NULL;
            }
        }
        numParents = 0;
        contactParent = -1;

        for (i = 0; i < 4; i++)
        {
            if (children[i] != NULL)
            {
                children[i]->RemoveParent(this);
                children[i] = NULL;
            }
        }
        numChildren = 0;

        nonTikiParent = NULL;
        // One source line in the PS2 build: a z = y = x chain, as in zGame.cpp.
        nonTikiParentDisp.z = nonTikiParentDisp.y = nonTikiParentDisp.x = 0.0f;
    }

    void FindParents(zScene* zsc);
    void BUpdate(xVec3* pos);
    void RenderExtra();
    void Render();
    S32 SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                 U32 toParamWidgetID, S32* handled);
    void ProcSubContract(F32 dt);
    void UpdateAnimBound();
    void Process(F32 dt);
    void SelfSetup();
    S32 SetCarryState(en_carystat cs);
    S32 TypeHandleMail(NMEMsg* mail);
    void Damage(en_npcdmg damtype, const xBase* who, const xVec3* vec_hit, S32 amt_dmgCaller,
                en_plyrpup pup_fromCaller);
    zMovePoint* MvptFirstFollow();
    zNMENavNet* Gimme_NavNet();
    void InitTikiBoundInfo();
    void Init(xEntAsset* entass);
    void Setup();
    void Reset();
    xAnimTable* CreateAnimTable(xAnimTable* table);
    S32 CanRope()
    {
        return 1;
    }
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
    {
        return xStrHash("Idle");
    }
    void Move(F32 dt)
    {
    }
    S32 IsHealthy()
    {
        return flg_vuln != 0;
    }
    S32 IsAlive()
    {
        return (tikiFlag & 0x600) != 0x400;
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
        return 0x18;
    }
    U8 ColPenByFlags() const
    {
        return 0x18;
    }
    U8 PhysicsFlags() const
    {
        return 0;
    }
};

// Goal member order follows the PS2 DWARF (listed there in reverse declaration order).
struct zNMEGoalTikiIdle : zNMEGoalCommon
{
    F32 tmr_wait;

    zNMEGoalTikiIdle()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
};

struct zNMEGoalTikiPatrol : zNMEGoalCommon
{
    xVec3 dest_pos;
    xVec3 vel;

    zNMEGoalTikiPatrol()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
};

struct zNMEGoalTikiHide : zNMEGoalCommon
{
    zNMEGoalTikiHide()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

struct zNMEGoalTikiCount : zNMEGoalCommon
{
    F32 tmr_count;
    S32 beingCarried;

    zNMEGoalTikiCount()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
};

struct zNMEGoalTikiDying : zNMEGoalCommon
{
    F32 tmr_dying;

    zNMEGoalTikiDying()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

struct zNMEGoalTikiDead : zNMEGoalCommon
{
    F32 tmr_resurrect;

    zNMEGoalTikiDead()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

extern U32 g_hash_tikianim[2];
extern char* g_strz_tikianim[2];

void ZNME_Tiki_Startup();
void zNMETiki_InitFX(zScene* zsc);
void zNMETiki_ExplodeFX(zNMETiki* tiki);
void zNMETiki_InitStacking(zScene* zsc);
void zNMETiki_Timestep(F32 dt);

#endif
