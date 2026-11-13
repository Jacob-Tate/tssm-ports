#ifndef ZNMETYPETONGUESPIN_H
#define ZNMETYPETONGUESPIN_H

#include "zNMECommon.h"
#include "iSnd.h"
#include "zNMETypeBossFrogfish.h"
#include "zNMESupport.h"
#include "zFXRibbonPool.h"

struct zNMEFrogfish;
struct xFXRibbon;

enum eTongueSpinState
{
    ezTONGUESPIN_STATE_HIDDEN,
    ezTONGUESPIN_STATE_BURROW,
    ezTONGUESPIN_STATE_POPUP,
    ezTONGUESPIN_STATE_ATTACK,
    ezTONGUESPIN_STATE_ATTACK_WAIT,
    ezTONGUESPIN_STATE_POPDOWN,
    ezTONGUESPIN_STATE_TAUNT,
    ezTONGUESPIN_STATE_DAMAGE,
    ezTONGUESPIN_STATE_STUN,
    ezTONGUESPIN_STATE_DEAD
};

extern char* gStrTongueSpinAnim[12];
extern NMEHelpRib::RibData ribCone;

// Members are in the original source order; CreateAnimTable is the key function that emits the
// vtable.
struct zNMETongueSpin : zNMECommon
{
    zNMETongueSpin()
    {
    }
    zNMEFrogfish* npc_bossman; // 0x1C0
    U32 anid_played;
    eTongueSpinState state;
    F32 tmr_stunned;
    F32 attackTimer;
    F32 popupTimer;
    U8 flgHitPlayer;
    U8 attackSoundPlayed;
    U8 locationChosen;
    S8 cnt_skipPlyrChecks;
    S8 cntBubemit;
    S8 cntSkipRibbonJoint;
    xFXRibbon* ribbon; // 0x1E0
    xVec3 vel;
    F32 knockbackTimer;
    iSndHandle burrowSndHandle;
    iSndHandle spinSndHandle;

    static iSndGroupHandle sSoundAsset[6];

    xAnimTable* CreateAnimTable(xAnimTable* table);
    void SelfSetup();
    void Init(xEntAsset* asset);
    void Setup();
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void Reset();
    virtual S32 DfltVulnFlags();
    // GC only (not in the PS2 DWARF); between DfltVulnFlags and EnterState in the GC source.
    void ChooseLocation();
    void EnterState(eTongueSpinState newState);
    // fNearPlayer is uint8 in the PS2 DWARF but bool in the GC symbol.
    U8 PickRandomPointInArena(xVec3* pos, bool fNearPlayer);
    void KnockMeBack();
    S32 ColChk_PlayerJump();
    S32 ColChk_Player();
    S32 TypeHandleMail(NMEMsg* mail);
    // No GC symbol (always inlined into TypeHandleMail); the name is ours.
    void TongueStun(NMEStunInfo* stundata)
    {
        bool wasStunned = tmr_stunned > 0.0f;

        tmr_stunned = stundata->tym_stuntime * (0.25f * (xurand() - 0.5f)) + stundata->tym_stuntime;
        if (!wasStunned)
        {
            EnterState(ezTONGUESPIN_STATE_STUN);
            npc_bossman->EnterState(ezFFBOSS_STATE_TONGUESTUN);
        }
    }
    S32 TongueSpinMailDamage(NMEDamageInfo* dmgmail);
    // No GC symbol and no PS2 DWARF entry (inlined into EnterState); the names are ours
    // (PlayExplicitAnim as zNMEFrogfish's, RibbonStart as zNMEGoalCritDying::Enter's ribbon code).
    U32 PlayExplicitAnim(U32 anid, S32 forceRestart)
    {
        if (AnimStart(anid, forceRestart))
        {
            anid_played = anid;
        }
        else
        {
            anid_played = 0;
        }

        return anid_played;
    }
    void RibbonStart()
    {
        NMEHelpRib::RibData* ribhelp = &ribCone;

        ribbon = zFXRibbonPoolNew();
        if (ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribbon);
            cntSkipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
        }
    }
    void DoCapsuleTests();
    // No GC symbols (inlined into Setup); see zNMETypeTongueSpin.cpp.
    void GopherDecalInit();
    void SoundInitAssetTable();
    void RibbonLoop();
    void FXUpdate(F32 dt, F32 tym_animPlusTime, F32 tym_animDur, F32 tym_animCurr);
    S32 MakeIceCreamSplat(xVec3* splatPos);

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
