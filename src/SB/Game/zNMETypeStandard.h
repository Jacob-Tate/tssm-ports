#ifndef ZNMETYPESTANDARD_H
#define ZNMETYPESTANDARD_H

#include "zNMECentral.h"
#include "zNMECommon.h"
#include "zNMEGoalStd.h"
#include "xModelBlur.h"
#include "zFXRibbonPool.h"
#include "zNMESupport.h"
#include "xScreenWarp.h"
#include "xBehaveMgr.h"
#include "zGlobals.h"
#include "xRand.h"

struct zNMEHazard;
struct zNMEStandard;

// Layout from the PS2 DWARF.
struct NMECfgStandard
{
    struct Taunt
    {
        F32 tym_keeplaffin;
    } taunt;
    struct Detect
    {
        F32 rad_outerLimit;
    } detect;
    struct Terminate
    {
        F32 hyt_fallToDeath;
    } terminate;
};

enum en_stananim
{
    STAN_ANIM_UNKNOWN,
    STAN_ANIM_IDLE,
    STAN_ANIM_MOVE,
    STAN_ANIM_ALERT,
    STAN_ANIM_CHASE,
    STAN_ANIM_FLEE,
    STAN_ANIM_PANIC,
    STAN_ANIM_SCARE_LOOP,
    STAN_ANIM_SCARE_DONE,
    STAN_ANIM_SPOOK_LOOP,
    STAN_ANIM_NOTICE,
    STAN_ANIM_TAUNT,
    STAN_ANIM_VICTORY,
    STAN_ANIM_DAMAGE,
    STAN_ANIM_DEFEAT,
    STAN_ANIM_PRAY,
    STAN_ANIM_ATTACK,
    STAN_ANIM_ATTACK_PREP,
    STAN_ANIM_ATTACK_LOOP,
    STAN_ANIM_ATTACK_DONE,
    STAN_ANIM_STUN_PREP,
    STAN_ANIM_STUN_LOOP,
    STAN_ANIM_STUN_PICKUP,
    STAN_ANIM_STUN_CARRY,
    STAN_ANIM_TOSS_PREP,
    STAN_ANIM_TOSS_LOOP,
    STAN_ANIM_SPIN_PREP,
    STAN_ANIM_SPIN_LOOP,
    STAN_ANIM_SPIN_THROW,
    STAN_ANIM_POP_UP,
    STAN_ANIM_POP_DOWN,
    STAN_ANIM_DEFLECT_PREP,
    STAN_ANIM_DEFLECT_LOOP,
    STAN_ANIM_DEFLECT_DONE,
    STAN_ANIM_BOMBLEFT_PREP,
    STAN_ANIM_BOMBLEFT_LOOP,
    STAN_ANIM_BOMBLEFT_DONE,
    STAN_ANIM_BOMBRYHT_PREP,
    STAN_ANIM_BOMBRYHT_LOOP,
    STAN_ANIM_BOMBRYHT_DONE,
    STAN_ANIM_NOMORE,
    STAN_ANIM_FORCE = 0x7fffffff
};

extern U32 g_hash_stananim[STAN_ANIM_NOMORE];
extern char* g_strz_stananim[STAN_ANIM_NOMORE];

// Type-specific asset data after the common zNMEAsset (offset 0x60). The PS2 DWARF has no type
// for it, so the names are ours and only the fields read so far are defined.
struct zNMEAssetStandard : zNMEAsset
{
    U32 aid_mvptFollow;
    U32 aid_mvptGroup;
    // 0x1: start in NME_GOAL_SCAREWAIT ("Prepare for Scare"); 0x2: allocate a private driver
    // (zNMEStandard::Init); 0x4/0x8: walk on PLATs/SIMPs.
    S32 flg_stdass; // 0x68
    U32 unk_6C[4]; // total size 0x7C, as checked by ValidStandard (zNMEAsset.cpp)
};

// Standard data of zNMETypeStandard.cpp.
namespace Standard
{
    namespace GlobalData
    {
        // Defined static in zNMETypeStandard.cpp; the in-class zNMEStandard::Init needs it here.
        // Redeclaring an extern as static is an extension CW and MSVC accept.
        extern S32 cnt_numStdNME;
    } // namespace GlobalData
    namespace Config
    {
        // Defined static const in zNMETypeStandard.cpp; same extension as cnt_numStdNME.
        extern const NMECfgStandard cfg_standard;
    } // namespace Config
} // namespace Standard

struct WarpData
{
    S32 doWarpFX;
    union
    {
        S32 warpType;
        xScreenWarp::type_enum typ_warp;
    };
    F32 rad_size;
    F32 fac_intensity;
    F32 frq_freq;
    F32 tym_duration;
};

struct BlurData
{
    struct Flags
    {
        S8 enable;
        S8 dev_enable;
        S8 unused1;
        S8 unused2;
    } flags;
    struct Parms
    {
        F32 tym_lifespan;
        F32 alf_master;
        F32 tym_fade;
    } parms;
    xModelBlur::config_data cfg_blur;
};

// zNMETypeStandard.cpp data.
namespace Spawn
{
    extern WarpData warp_spawn;

    namespace Ribbon
    {
        extern NMEHelpRib::RibData rib_emerge;
    } // namespace Ribbon
} // namespace Spawn

namespace Wound
{
    namespace Ribbon
    {
        namespace Normal
        {
            extern NMEHelpRib::RibData rib_norm;
        } // namespace Normal

        namespace Glory
        {
            extern NMEHelpRib::RibData rib_glory;
        } // namespace Glory
    } // namespace Ribbon

    namespace PushBlur
    {
        extern BlurData blur_push;
    } // namespace PushBlur
} // namespace Wound

// No symbol (always inlined); the name is ours. Scans a zero-terminated goal id list.
inline S32 NMEStd_GoalInList(S32 gid, const S32* list)
{
    S32 found = 0;
    for (S32 i = 0; list[i] != 0; i++)
    {
        if (gid == list[i])
        {
            found = 1;
            break;
        }
    }
    return found;
}

// The same scan for the en_npcgol lists (Standard::noStunGList, alreadyStunnedList). Returns
// bool: the PS2 build converts this one's result, where the S32 overload is tested directly.
inline bool NMEStd_GoalInList(en_npcgol gid, const en_npcgol* list)
{
    S32 found = 0;
    for (S32 i = 0; list[i] != NME_GOAL_UNKNOWN; i++)
    {
        if (gid == list[i])
        {
            found = 1;
            break;
        }
    }
    return found;
}

// Goals used by the zNMEStandard enemies.

enum en_sentell
{
    SENTRY_TELL_ARRIVED,
    SENTRY_TELL_UNPAUSE,
    SENTRY_TELL_NOMORE
};

struct zNMEGoalSentry : zNMEGoalCommon
{
    F32 tmr_delay;
    xVec3 pos_from;
    F32 dst_interpNorm;
    struct
    {
        S32 flg_sentry : 16;
        S32 flg_doRot : 1;
        S32 flg_dfltAccel : 1;
        S32 flg_ignoreDelay : 1;
        S32 flg_unused : 13;
    };
    void (*fun_tellbody)(zNMEGoalSentry*, en_sentell, zNMECommon*);

    void Clear()
    {
        flg_sentry = 0;
        tmr_delay = -1.0f;
        dst_interpNorm = 0.0f;
    }
    void Sentry_Update(zNMEStandard* npc, F32 dt);
    // No symbol (inlined into the Zap/Merv/FlingMove Enters); the name is ours.
    void Sentry_Enter(zNMECommon* npc)
    {
        zNMENavNet* navnet = npc->navnet;

        tmr_delay = -1.0f;
        flg_sentry = 0;
        if (navnet->nav_dest == NULL)
        {
            navnet->MvptReset(navnet->nav_curr);
        }
        pos_from = *xEntGetPos(npc);
    }
    // No symbol (always inlined); the name is ours. Callers set the bit on a FindGoal result
    // through it, so the store comes after the call as in retail.
    void Sentry_DoRot(S32 doRot)
    {
        flg_doRot = doRot;
    }
    void DoOnArriveStuff(zNMECommon* npc);
    void SentryMoveNormal(zNMECommon* npc, F32 dt);
    void SentryMoveSpline(zNMECommon* npc, F32 dt);
    virtual void TellMyNukeFam(en_sentell note, zNMECommon* npc)
    {
    }
};

struct Input
{
    en_plyrpup pup_plyr;
    xVec3 dir_pushed;
    F32 mag_pushed;
};

struct zNMEGoalWound : zNMEGoalCommon
{
    S8 flg_wound;
    S8 cnt_skipRibbonJoint;
    S16 flg_other;
    xVec3 vel_fling;
    F32 tmr_knocked;
    xFXRibbon* ribbon;
    xModelBlur pushBlur;
    Input input;

    zNMEGoalWound()
    {
        flg_npcgable |= 0x1;
    }

    void Clear()
    {
        if (ribbon)
        {
            zFXRibbonPoolDelete(ribbon);
            ribbon = NULL;
        }
        // Retail makes a virtual call to this same Clear() here, which would recurse forever if
        // it were ever reached. Kept as shipped.
        Clear();
    }
    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 CollReview(void* ctxt);
    S32 InputInfoDflt();
    S32 InputInfo(NMEDamageInfo* dmgmail);
    S32 GoalHandleMail(NMEMsg* mail);
    void TakeItLikeAMan(zNMEStandard* npc);
    void RibbonPrep(zNMEStandard* npc);
    void RibbonUpdate(zNMEStandard* npc, F32 dt);
};

enum en_resp
{
    SPAWN_STAT_PATIENCE,
    SPAWN_STAT_EMERGING,
    SPAWN_STAT_FINALACT,
    SPAWN_STAT_FORCE = 0x7fffffff
};

enum en_dupspit
{
    DUP_SPIT_WAIT,
    DUP_SPIT_NOW,
    DUP_SPIT_ABORT
};

struct Storage
{
    xVec3 pos_poofHere;
    zMovePoint* nav_spawnReference;
    zMovePoint* nav_firstMovepoint;
};

struct zNMEGoalSpawning : zNMEGoalCommon
{
    en_resp stat_spawning;
    struct
    {
        S32 flg_doSpawnball : 1;
        S32 flg_doScreenWarp : 1;
        S32 flg_spawningUnused : 22;
        S32 cnt_skipRibInsert : 8;
    };
    xFXRibbon* ribbon;
    union
    {
        F32 tmr_robobits;
        F32 tmr_respawn;
    };
    F32 tmr_duperTakesTooLong;
    zNMEHazard* haz_spawn;
    Storage storage;

    zNMEGoalSpawning()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    void Clear()
    {
        if (ribbon)
        {
            zFXRibbonPoolDelete(ribbon);
            ribbon = NULL;
        }
        zNMEGoalCommon::Clear();
    }
    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* ctxt);
    S32 PatienceMonitor(zNMECommon* npc, F32 dt);
    en_dupspit ExamineDuperAnim(zNMECommon* npc);
    void CalcLaunchPoint(zNMECommon* npc, xVec3* pos_launch, S32 useDefault);
    S32 EmergeYeBeastie(zNMECommon* npc, F32 dt);
    F32 LaunchSpawnBall(zNMECommon* npc, xVec3* pos_launch);
    void DoAppearFX(zNMECommon* npc, F32, F32);
    void CowCatcher(zNMECommon* npc, F32);
    void RibbonUpdate(xVec3* pos_home, F32 ratio);
    void KickFromTheNest(zNMECommon* npc);
};

struct zNMEGoalAfterlife : zNMEGoalDead
{
    zNMEGoalAfterlife()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalSpooked : zNMEGoalCommon
{
    F32 tmr_nextturn;
    F32 tmr_spooked;
    xVec3 dir_desire;

    zNMEGoalSpooked()
    {
        flg_npcgauto |= 0x6;
    }

    // No symbol (inlined into Enter and Process); the name is ours. Picks a random XZ heading.
    void PickHeading()
    {
        F32 ang = 2.0f * PI * xurand() - PI;

        dir_desire.x = isin(ang);
        dir_desire.y = 0.0f;
        dir_desire.z = icos(ang);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalNotice : zNMEGoalCommon
{
    zNMEGoalNotice()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

// Not in the PS2 DWARF; reconstructed from its vtable and Process.
struct zNMEGoalAware : zNMEGoalCommon
{
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalScareWait : zNMEGoalDead
{
    zNMEGoalScareWait()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* msg);
    void AbortAbort();
};

struct zNMEGoalScare : zNMEGoalLoopAnim
{
    xParabola* parab;

    zNMEGoalScare()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 UpdateAnim(F32 dt);
    S32 PathUpdate(F32 dt);
    S32 PathParabola(F32 dt);
    S32 PathDirect(F32 dt);
    void ParabSetup();
};

struct zNMEGoalTaunt : zNMEGoalLoopAnim
{
    zNMEGoalTaunt()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalEvilPat : zNMEGoalCommon
{
    zNMEGoalEvilPat()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* msg);
    void GlyphStart();
    void GlyphStop();
};

struct zNMEGoalStunned : zNMEGoalCommon
{
    zNMEGoalStunned()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* ctxt);
};

struct zNMEGoalPatCarry : zNMEGoalCommon
{
    zNMEGoalPatCarry()
    {
        SetFlags(0x2);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void TowerOfPisa(zNMEStandard* npc, F32 dt);
    F32 CalcPlayerLean(zNMEStandard* npc, F32 dt);
};

struct zNMEGoalPatTwirl : zNMEGoalCommon
{
    zNMEGoalPatTwirl()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Process(en_trantype* trantype, F32 dt, void* ctxt);
};

struct zNMEGoalPatThrow : zNMEGoalCommon
{
    zNMEGoalPatThrow()
    {
        flg_npcgable = 0x1;
        flg_npcgauto |= 0x6;
    }

    S32 CollReview(void* ctxt)
    {
        return 0;
    }
    S32 Enter(F32 dt, void* updCtxt);
};

// Overrides are declared in vtable order; the first non-inline one (CreateAnimTable) is the key
// function that emits the vtable.
struct zNMEStandard : zNMECommon
{
    F32 tmr_stunned; // 0x1C0
    S32 pts_health; // 0x1C4
    const NMECfgStandard* cfg_std; // 0x1C8
    zNMEHazard* haz_dazed;
    bool bucketHead; // 0x1D0; uint8 in the PS2 DWARF, bool* in the get_parameter call
    zNMENavNet privNavNet; // 0x1D4
    zNMEDriver* privDriveAlloc; // 0x1F4

    // No symbol (inlined into every create_npc<> of a standard enemy).
    zNMEStandard()
    {
        privNavNet.nme_owner = this;
    }

    xAnimTable* CreateAnimTable(xAnimTable* table);

    // The standard enemies reach the player through the non-const XYZDstSqToPos, one inline
    // level deeper than zNMECommon's const path (the turrets use that one). XYZDstSqToEnt has
    // no symbol (always inlined); the name is ours.
    using zNMECommon::XYZDstSqToPlayer;
    F32 XYZDstSqToEnt(const xEnt* ent, xVec3* vec)
    {
        return XYZDstSqToPos(xEntGetPos(ent), vec);
    }
    F32 XYZDstSqToPlayer(xVec3* vec)
    {
        return XYZDstSqToEnt(xglobals->___player_ent_dont_use_directly, vec);
    }

    // Init, PostInit, Setup, Reset, SelfSetup and CreateConfig are inline (weak) in retail.
    void Init(xEntAsset* asset)
    {
        Standard::GlobalData::cnt_numStdNME++;
        if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x2)
        {
            // Not a DWARF local (probably an unknown inline helper's temporary): retail stores
            // the xMemAlloc result after the memset.
            zNMEDriver* drv = (zNMEDriver*)xMemAlloc(gActiveHeap, sizeof(zNMEDriver), 0);
            memset(drv, 0, sizeof(zNMEDriver));
            privDriveAlloc = drv;
        }
        zNMECommon::Init(asset);
        RuntimeDefaults();
        flags &= (U8)~0x40;
    }
    void PostInit()
    {
        zNMEGoalWound* goal = (zNMEGoalWound*)psy_self->HasGoal(NME_GOAL_WOUND);
        if (goal)
        {
            goal->pushBlur.create();
        }
    }
    void Setup()
    {
        zNMECommon::Setup();
        get_parameter("IsBucketHead", &bucketHead, true);
    }
    void Reset()
    {
        zNMECommon::Reset();
        flg_vuln = DfltVulnFlags();
        tmr_stunned = 0.0f;
        haz_dazed = NULL;
        pts_health = 1;
    }
    void SelfSetup()
    {
        xBehaveMgr* bmgr;
        xPsyche* psy;
        xGoal* goal;

        bmgr = xBehaveMgr_GetSelf();
        psy_self = bmgr->Subscribe(this, 0);
        psy = psy_self;
        psy->BrainBegin();

        goal = psy->AddGoal(NME_GOAL_IDLE, NULL);
        goal->fun_process = NULL;
        goal->cbdata = NULL;

        goal = psy->AddGoal(NME_GOAL_WANDER, NULL);
        goal->fun_process = NULL;
        goal->cbdata = NULL;

        goal = psy->AddGoal(NME_GOAL_PATROL, NULL);
        goal->fun_process = NULL;
        goal->cbdata = NULL;

        psy->BrainEnd();
        psy->SetSafety(NME_GOAL_IDLE);
    }
    void CreateConfig(const char* name)
    {
        zNMECommon::CreateConfig(name);
        cfg_std = &Standard::Config::cfg_standard;
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
        return 0;
    }
    U8 ColPenByFlags() const
    {
        return 0;
    }
    U8 PhysicsFlags() const
    {
        return 0;
    }
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    S32 IsAlive()
    {
        return pts_health > 0;
    }
    S32 IsHealthy()
    {
        return IsAlive();
    }
    // Declared after IsHealthy: retail emits the weak copies in the order PostInit, IsHealthy,
    // CollideReview, Gimme_DriveData, and CreateConfig right after SelfSetup.
    void CollideReview()
    {
    }
    void Boo(const zNMECommon* npc);
    void DuploOwner(zNMECommon* duper);
    void DieTheGoodDeath();
    S32 SetCarryState(en_carystat state);
    zMovePoint* MvptFirstFollow();
    zNMEDriver* Gimme_DriveData()
    {
        return privDriveAlloc;
    }
    zNMENavNet* Gimme_NavNet()
    {
        return &privNavNet;
    }
    S32 WannaPoolShadow()
    {
        return 2;
    }

    // new virtuals
    virtual void WoundedInput(en_npcgol gid, NMEDamageInfo* dmgmail);
    virtual void Type_AddYourMove(F32 dt)
    {
    }
    virtual S32 DfltVulnFlags()
    {
        return 0;
    }

    void DazedFXUpdate(F32 dt);
    S32 IsVulnerable(en_npcdmg dmgtype);
    void DfltCelebrateMail();
    void StdTyp_Process(F32 dt);
    void StdTyp_CollideReview()
    {
        Common_Collide();
    }

    // No GC symbol: auto-inlined into the Process of the std types and dead-stripped.
    void StdNME_SuicideIsPainless();
};

struct zMODStanEnemy : zModule
{
    void ScenePrepare();
    void ScenePostInit();
};

void ZNME_Standard_Startup();

#endif
