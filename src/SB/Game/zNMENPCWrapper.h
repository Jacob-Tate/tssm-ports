#ifndef ZNMENPCWRAPPER_H
#define ZNMENPCWRAPPER_H

#include "zNPC.h"
#include "zEnt.h"
#include "zNPCTypes.h"

void NME_entwrap_setup(xEnt* ent);
void NME_entwrap_reset(xEnt* ent);
void NME_entwrap_update(xEnt* ent, xScene* xscn, F32 dt);
void NME_entwrap_bupdate(xEnt* ent, xVec3* pos);
void NME_entwrap_move(xEnt* ent, xScene* xscn, F32 dt, xEntFrame* frame);
void NME_entwrap_event(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 toParamWidgetID);
void NME_entwrap_render(xEnt* ent);

// Virtual order must match __vt__14zNMENPCWrapper (new virtuals follow the zNPC::base ones).
struct zNMENPCWrapper : zNPC::base
{
    void (*fun_setup)(xEnt*);
    void (*fun_reset)(xEnt*);
    S32 colFreq;
    U8 flg_colCheck;
    U8 flg_penCheck;
    U16 flg_unusedCollFlags;
    struct
    {
        S32 aflg_basenme : 8;
        S32 flg_upward : 8;
        S32 flg_xtrarend : 1;
        S32 flg_inUpdate : 1;
        S32 flg_newtime : 1;
        S32 flg_postproc : 1;
        S32 flg_profProc : 1;
        S32 flg_hudrend : 1;
        S32 flg_moreUnusedSpace : 10;
    };

    // Weak. Returns en_npctyp, not BfBB's S32: the inlined zNMEShrapnelRetrieveShrapnel callers
    // allocate registers like retail only with this return type.
    en_npctyp SelfType() const
    {
        return (en_npctyp)itemType;
    }

    // Inlined everywhere (no symbol); same body as xNPCBasic::RestoreColFlags.
    void RestoreColFlags()
    {
        flg_colCheck = ColChkFlags();
        flg_penCheck = ColPenFlags();
        chkby = ColChkByFlags();
        penby = ColPenByFlags();
        pflags = PhysicsFlags();
        colFreq = -1;
    }

    // Inlined everywhere on GC (no symbol).
    void NMEWrap_Init(xEntAsset* asset)
    {
        xEntInit(this, asset);
        sound_id_offset = 0xFFFF;
        fun_setup = NME_entwrap_setup;
        fun_reset = NME_entwrap_reset;
        eventFunc = NME_entwrap_event;
        update = NME_entwrap_update;
        bupdate = NME_entwrap_bupdate;
        move = NME_entwrap_move;
        render = NME_entwrap_render;
        zEntParseModelInfo(this, asset->modelInfoID);
        flags |= 0x40;
        baseFlags &= (U16)~0x10;
        global_parameters = zEntGetModelParams(asset->modelInfoID, &global_parameters_size);
        npc_asset = NULL;
        local_parameters_size = 0;
        local_parameters = NULL;
    }
    void UpdateWrapper(F32 dt);

    xAnimTable* CreateAnimTable(xAnimTable* table)
    {
        return NULL;
    }
    virtual void InitFromNPCMgr(xEntAsset* asset)
    {
        Init(asset);
    }
    virtual void Init(xEntAsset* asset)
    {
        NMEWrap_Init(asset);
    }
    virtual void PostInit()
    {
    }
    virtual void Setup()
    {
    }
    virtual void PostSetup()
    {
    }
    virtual void Reset()
    {
        xEntReset(this);
    }
    virtual void BUpdate(xVec3* pos)
    {
    }
    virtual S32 SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, U32 toParamWidgetID, S32* handled)
    {
        return 1;
    }
    virtual void Save(xSerial* s) const
    {
    }
    virtual void Load(xSerial* s)
    {
    }
    virtual void Process(F32 dt)
    {
    }
    virtual void NewTime(F32 dt)
    {
    }
    virtual void Render()
    {
    }
    virtual void RenderExtra()
    {
    }
    virtual void RenderHud()
    {
    }
    virtual void Move(F32 dt)
    {
    }
    virtual void Destroy()
    {
    }
    virtual void SelfSetup()
    {
    }
    virtual void SelfDestroy()
    {
    }
    virtual S32 ColFreqReset()
    {
        return -1;
    }
    virtual void CollideReview()
    {
    }
    virtual U8 ColChkFlags() const
    {
        return 0;
    }
    virtual U8 ColPenFlags() const
    {
        return 0;
    }
    virtual U8 ColChkByFlags() const
    {
        return 0;
    }
    virtual U8 ColPenByFlags() const
    {
        return 0;
    }
    virtual U8 PhysicsFlags() const
    {
        return 0;
    }

    virtual void init(xEntAsset* asset)
    {
        Init(asset);
        SelfSetup();
    }
    void setup()
    {
        Setup();
        Reset();
    }
    virtual void reset()
    {
        Reset();
    }
    void update_npc(F32 dt)
    {
        Process(dt);
    }
    virtual void update_bound()
    {
    }
    void change_bounds(F32 scale, xVec3, bool use_bounding_box)
    {
    }
    bool system_event(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                      U32 toParamWidgetID)
    {
        return true;
    }
    virtual void render_npc()
    {
        Render();
    }
    virtual void debug_render()
    {
    }
    void render_extra()
    {
    }
    virtual void damage(xBase*, S32, zHitSource, zHitTarget, const xVec3&)
    {
    }
    void destroy()
    {
        Destroy();
    }
    void scene_setup()
    {
    }
    virtual void add_states(xAnimTable* table)
    {
    }
    virtual void add_transitions(xAnimTable* table)
    {
    }
};

#endif
