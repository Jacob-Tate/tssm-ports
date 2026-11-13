#ifndef ZNMETYPEBOSSDENTOO_H
#define ZNMETYPEBOSSDENTOO_H

#include "zNMECentral.h"
#include "zWadNME.h"
#include "xHudMeter.h"
#include "zNMESupport.h"
#include "zNMEHealthMeter.h"

namespace Swimmer
{
    struct zMODSwimmer : zModule
    {
        struct
        {
            S32 flg_swimEnabled : 1;
            S32 flg_unused : 31;
        };
        F32 tmr_delaySeaFog;
        F32 tmr_spraySeaFog;
        F32 tmr_delaySeaMist;
        F32 tmr_spraySeaMist;
        F32 tmr_delayNeck;
        F32 tmr_sprayNeck;
        F32 tmr_delayArm;
        F32 tmr_sprayArm;
        F32 tmr_delayPit;
        F32 tmr_sprayPit;
        F32 tmr_delayHip;
        F32 tmr_sprayHip;
        F32 tmr_delayKnee;
        F32 tmr_sprayKnee;
        F32 tmr_delayFeet;
        F32 tmr_sprayFeet;

        // ScenePrepare is the key function that emits the vtable, so ModulePrepUse (defined
        // inline in zNMETypeBossDenToo.cpp) cannot be declared before it.
        void ScenePrepare();
        void SceneFinish();
        void ScenePostInit();
        void Timestep(F32 dt);
        void ModulePrepUse();
        void WorldFog_Upd(F32 dt);
        void RainyMist_Upd(F32 dt);
        void NeckSpray_Upd(F32);
        void ArmSpray_Upd(F32);
        void PitSpray_Upd(F32);
        void HipSpray_Upd(F32);
        void KneeSpray_Upd(F32);
    };
} // namespace Swimmer

void ZNME_Swimmer_Startup();

struct ThrowSide
{
    F32 spd_proj;
    S32 num_salvo;
    F32 dst_spreadHorz;
};

struct ThrowToes
{
    F32 spd_proj;
    S32 num_salvo;
    F32 dst_nearTgt;
    F32 dst_farTgt;
    F32 hyt_elevate;
};

struct NMECfgDenToo
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Ranges
    {
        F32 rad_tooclose;
        F32 rad_lob;
        F32 rad_lobgrey;
        F32 rad_aware;
        F32 rad_awaregrey;
    } ranges;
    struct Attack
    {
    } attack;
    ThrowSide throwSide;
    ThrowToes throwToes;
    struct Battle
    {
        F32 tym_reload;
        F32 tym_leapDelay;
        S32 num_atakPerLeap;
    } battle;
};

enum en_dentstage
{
    DENT_STAGE_UNKNOWN,
    DENT_STAGE_INTRO,
    DENT_STAGE_A,
    DENT_STAGE_B,
    DENT_STAGE_C,
    DENT_STAGE_OUTRO,
    DENT_STAGE_NOMORE
};

enum en_dtbcmode
{
    DENT_CAMMOD_UNKNOWN,
    DENT_CAMMOD_NORMAL,
    DENT_CAMMOD_BINARY,
    DENT_CAMMOD_NOMORE
};

// Type-specific asset data that trails the common zNMEAsset (offset 0x60). The PS2 DWARF
// has no type for it, so the name is ours and only the fields read so far are defined.
struct zNMEAssetDenToo : zNMEAsset
{
    U32 aid_mvptFollow;
    U32 aid_mvptGroup; // 0x64, read by Reset (named as in zNMEAssetStandard)
};

// setup is the key function that emits __vt__11DenTooMeter.
struct DenTooMeter : zHud::BarMeter<S32>
{
    U8 lastPaused;
    zHud::TextureScroller denTooTexureScroller;

    DenTooMeter();
    void setup();
    void update(F32 dt);
};

// Member functions follow the original source order (the PS2 DWARF lists them reversed), except
// CreateConfig: DfltVulnFlags is the key function, so the vtables land in retail's .data order.
struct zNMEDenToo : zNMEBoss
{
    zNMEDenToo()
    {
    }
    S32 pts_health; // 0x1C0
    DenTooMeter denTooMeter; // 0x1C4
    NMECfgDenToo* cfg_dentoo; // 0x260
    F32 tmr_nextLeap;
    S32 cnt_attacksTilEvade;
    zMovePoint* nav_evade;
    union
    {
        en_dentstage stageCurr;
        S32 stageCurrNum;
    };
    xModelBlur blur_jumpShort;
    xModelBlur blur_jumpLong;
    zStreamedSoundList oneLiners;
    xGroup* grp_mvpt;
    S32 flg_firstTimestep;
    zBossCam_Binary cam_boss;
    en_dtbcmode mod_camCurr;
    en_dtbcmode mod_camWant;
    F32 tmr_bobCycle;

    virtual S32 DfltVulnFlags();
    void CreateConfig(const char* name);
    void SelfSetup();
    zMovePoint* MvptFirstFollow();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Init(xEntAsset* asset);
    void Setup();
    void Reset();
    void Destroy();
    S32 IsAlive();
    void Process(F32 dt);
    void PostProcess(F32 dt);
    void BUpdate(xVec3* pos);
    void RenderHud();
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    void RespondToHittingPlayer();
    void BossCamSet(en_dtbcmode mod_desired, bool forceCut);
    void BossCamRepel(F32 dt);
    static void BossCamOverride(zBossCam_Binary* cam, F32 dt, void* data);
    void MeterRender();
    S32 TypeHandleMail(NMEMsg* mail);
    S32 DenMailDamage(NMEDamageInfo* dmgmail);
    S32 DenMailSys(NMESysEvent* sysmail);
    zMovePoint* PickEvade(zMovePoint* nav_from);
    zMovePoint* PickEvadePoint(zMovePoint* nav_from);

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
        return XENT_COLLTYPE_NPC;
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
