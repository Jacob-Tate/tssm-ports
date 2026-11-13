#ifndef ZNMETYPEBOSSFROGFISH_H
#define ZNMETYPEBOSSFROGFISH_H

#include "zWadNME.h"
#include "xCollide.h"
#include "iSnd.h"
#include "zStreamedSound.h"

struct zShrapnelAsset;
struct zNMETongueSpin;

namespace xRumble
{
    struct effect;
} // namespace xRumble

enum eFFBossStage
{
    ezFFBOSS_STAGE_A,
    ezFFBOSS_STAGE_B,
    ezFFBOSS_STAGE_C,
    ezFFBOSS_STAGE_INTRO,
    ezFFBOSS_STAGE_OUTRO,
    exFFBOSS_STAGE_NOMORE
};

enum eFFBossLocation
{
    ezFFBOSS_LOC_OPPOSITE_PLAYER,
    ezFFBOSS_LOC_SAME_SIDE_PLAYER,
    ezFFBOSS_LOC_TONGUE,
    ezFFBOSS_LOC_TONGUE_TRENCH,
    exFFBOSS_LOC_NOMORE
};

enum eFFBossCameraState
{
    ezFFBOSS_CAMERA_STATE_REGULAR,
    ezFFBOSS_CAMERA_STATE_PREPJUMP,
    ezFFBOSS_CAMERA_STATE_AFTERJUMP,
    ezFFBOSS_CAMERA_STATE_AFTERTONGUE,
    ezFFBOSS_CAMERA_STATE_PREPGENERATION,
    ezFFBOSS_CAMERA_STATE_AFTERGENERATION,
    ezFFBOSS_CAMERA_STATE_TRANSTOREGULAR,
    ezFFBOSS_CAMERA_STATE_NOMORE
};

enum eFFBossState
{
    ezFFBOSS_STATE_IDLE,
    ezFFBOSS_STATE_CHARGE,
    ezFFBOSS_STATE_STOPPING,
    ezFFBOSS_STATE_TURNING,
    ezFFBOSS_STATE_TURNING2,
    ezFFBOSS_STATE_TEETER,
    ezFFBOSS_STATE_LUNGE,
    ezFFBOSS_STATE_TAUNT,
    ezFFBOSS_STATE_QUICKTAUNT,
    ezFFBOSS_STATE_TAUNT_HIDE,
    ezFFBOSS_STATE_WAIT_IN_PIT,
    ezFFBOSS_STATE_DAMAGE,
    ezFFBOSS_STATE_TONGUESPIN,
    ezFFBOSS_STATE_TONGUESTART,
    ezFFBOSS_STATE_TONGUEDAMAGE,
    ezFFBOSS_STATE_TONGUESTUN,
    ezFFBOSS_STATE_TONGUESTUNDONE,
    ezFFBOSS_STATE_ATTACKAPREP,
    ezFFBOSS_STATE_DYING,
    ezFFBOSS_STATE_DEAD
};

enum eFFOneLiner
{
    eFFOneLinerFFHit,
    eFFOneLinerPatHit,
    eFFOneLinerSBHintsAttack0,
    eFFOneLinerSBHintsAttack1,
    eFFOneLinerSBHintsAttack2,
    eFFOneLinerSBHintsNow,
    eFFOneLinerSBComment,
    eFFOneLinerPatComment,
    eFFOneLinerSBIntro,
    eFFOneLinerPatIntro,
    eFFOneLinerPatPhlegm,
    eFFOneLinerNumSounds
};

// Layout from the PS2 DWARF; the GC code uses the same offsets.
struct zNMECfgFrogfish
{
    struct
    {
        S32 maxStage1;
        S32 maxStage2;
        S32 maxStage3;
        F32 radBound;
        xVec3 vecBoundShift;
    } health;
    struct
    {
        F32 chargeSpeed;
        F32 range;
        F32 stoppingDecel;
        F32 stoppingDecelFast;
        F32 knockbackDist;
    } attackA;
    struct
    {
        en_haztyp haztype;
        F32 spdProj;
        F32 dstMinRange;
        F32 tymLeadTarget;
        xVec3 vecOffsetLaunch;
        F32 tymReload;
        F32 radMin;
        F32 radMax;
    } attackC;
    struct
    {
        F32 centerX;
        F32 centerZ;
        F32 radius;
        F32 stoppingRadius;
        F32 innerRadius;
    } arena;
    struct
    {
        F32 duration;
        F32 gravity;
    } fall;
    struct
    {
        F32 duration;
        F32 gravity;
    } jumpToStand;
    struct
    {
        F32 turnRate;
    } turn;
    struct
    {
        F32 time;
        F32 mag;
        F32 cycle;
        F32 rot_mag;
        F32 radius;
    } shake;
};

// Attack sequence per stage (global .data, 0x90 bytes); read by the inline SetTheStage.
extern S32 gFFBossAttackTypes[3][12];

// Type-specific asset data of "Enemy:SB:FrogFish" DYNAs. The PS2 DWARF has no type for it, so
// the name is ours; nothing reads the trailing field yet, the total size (0x64) is the one
// ValidFrogFish (zNMEAsset.cpp) checks for.
struct zNMEAssetFrogFish : zNMEAsset
{
    U32 unk_60;
};

// Member functions follow the original source order (the PS2 DWARF lists them reversed);
// CreateConfig is the key function that emits __vt__12zNMEFrogfish.
struct zNMEFrogfish : zNMEBoss
{
    zNMEFrogfish()
    {
    }
    // GC-only (zNMEFrogfish::eSound in the SoundPlayEffect symbol); the PS2 DWARF predates it.
    // Enumerator names follow the sound group names Setup stores in sSoundAsset[].
    enum eSound
    {
        eSoundStomp,
        eSoundChargePrep,
        eSoundCharge,
        eSoundHit,
        eSoundSmash,
        eSoundSpit,
        eSoundTaunt,
        eSoundYelp,
        eSoundLand,
        eSoundTeeter,
        eSoundRecover,
        eSoundMad,
        eSoundRoar,
        eSoundTongueBite,
        eSoundDead,
        eSoundFootstepWalk,
        eSoundFootstepRun,
        eSoundSkid,
        eSoundTongueTaunt,
        eSoundTongueDown,
        eSoundNoMore
    };

    S32 stageCurr; // 0x1C0
    S32 attackCurr;
    S32 attackIndex;
    S32 damageTakenAttackCurr;
    S32 ptsHealth;
    U32 anid_played;
    zNMECfgFrogfish* cfg; // 0x1D8
    eFFBossState state; // 0x1DC
    xVec3 chargeVelDir;
    F32 chargeVelMagnitude;
    F32 timer;
    xParabola jumpParabola;
    F32 timeLeaping;
    xVec3 dirDest;
    U8 roarPlayed;
    U8 landFXPlayed;
    U8 meleeSuccessfull;
    U8 attackChangePending;
    xRumble::effect* rumbleMelee;
    zShrapnelAsset* fragSkullStompL;
    zShrapnelAsset* fragSkullStompR;
    zShrapnelAsset* fragSlam;
    zShrapnelAsset* fragSkid;
    zNMETongueSpin* npc_tongue;
    xVec3 camTarget1;
    xVec3 camTarget2;
    xVec3 camTargetStart;
    xVec3 camTargetEnd;
    eFFBossCameraState camState;
    F32 camTimer;
    F32 camInterpTime;
    F32 camInterpDuration;
    U8 firstTimeTongue;
    iSndHandle teeterSndHandle;
    iSndHandle deadSndHandle;
    F32 fxTimer;
    F32 sbHintTimer;
    F32 sbCommentTimer;
    F32 patCommentTimer;
    F32 sbIntroTimer;
    F32 patIntroTimer;
    U8 sbIntroPlayed;
    U8 patIntroPlayed;
    U8 tongueFXPlayed;
    U8 roarInTrenchPlayed;
    U8 cameraSetup;
    iSndGroupHandle sSoundAsset[20];
    zStreamedSoundList FFOneLiners;

    static zStreamedSound FFOneLinersData[12];

    void CreateConfig(const char* name);
    virtual S32 DfltVulnFlags();
    void Init(xEntAsset* asset);
    void Setup();
    void Reset();
    void Destroy();
    void SetCamState(eFFBossCameraState newState);
    void UpdateCam(F32 dt);
    void SoundInitAssetTable();
    void RumbleAndShake(F32 intensity);
    void FeetFXUpdate(F32 dt);
    void Process(F32 dt);
    void DoDamageAndKnockback();
    void BUpdate(xVec3* pos);
    bool IsTeeterOk();
    void EnterState(eFFBossState newState);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    U32 PlayExplicitAnim(U32 anid, S32 forceRestart);
    void CalcPos(xVec3* posDest, eFFBossLocation loc);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 FrogfishMailDamage(NMEDamageInfo* dmgmail);
    S32 FrogfishMailSys(NMESysEvent* sysmail);
    // No GC symbol (always inlined into zNMETongueSpin::TongueSpinMailDamage); the name is ours.
    void TakeDamage(S32 amt_dmg)
    {
        ptsHealth -= amt_dmg;
        damageTakenAttackCurr += amt_dmg;
        if (ptsHealth < 0)
        {
            ptsHealth = 0;
        }
        if (ptsHealth < 1 && stageCurr == ezFFBOSS_STAGE_C)
        {
            EnterState(ezFFBOSS_STATE_DYING);
        }
    }
    void StageIncrement();

    // Always inlined (no symbol); the name comes from the PS2 build's analysis.
    void SetTheStage(eFFBossStage stageToBe)
    {
        stageCurr = stageToBe;
        attackIndex = 0;
        damageTakenAttackCurr = 0;
        attackCurr = 0;
        ptsHealth = 1;

        if (stageCurr == ezFFBOSS_STAGE_A)
        {
            ptsHealth = cfg->health.maxStage1;
            attackCurr = gFFBossAttackTypes[stageCurr][attackIndex];
        }
        else if (stageCurr == ezFFBOSS_STAGE_B)
        {
            ptsHealth = cfg->health.maxStage2;
            attackCurr = gFFBossAttackTypes[stageCurr][attackIndex];
        }
        else if (stageCurr == ezFFBOSS_STAGE_C)
        {
            ptsHealth = cfg->health.maxStage3;
            attackCurr = gFFBossAttackTypes[stageCurr][attackIndex];
        }
    }

    // Always inlined (no symbol; the name is ours): true once the boss has lost health in the
    // current stage.
    bool IsStageHealthDown()
    {
        if (stageCurr == ezFFBOSS_STAGE_A)
        {
            if (ptsHealth < cfg->health.maxStage1)
            {
                return true;
            }
        }
        else if (stageCurr == ezFFBOSS_STAGE_B)
        {
            if (ptsHealth < cfg->health.maxStage2)
            {
                return true;
            }
        }
        else if (stageCurr == ezFFBOSS_STAGE_C)
        {
            if (ptsHealth < cfg->health.maxStage3)
            {
                return true;
            }
        }

        return false;
    }

    // Always inlined (no symbol; the name is ours). Pat's phlegm line is only picked while the
    // tongue spins after a hit this stage.
    bool IsPhlegmOk()
    {
        if (state != ezFFBOSS_STATE_TONGUESPIN)
        {
            return false;
        }
        if (!IsStageHealthDown())
        {
            return false;
        }
        return true;
    }
    void AttackIncrement();
    bool IsTongueAttackNext();
    S32 ReadyAimFire();
    void CalcJump(eFFBossLocation loc);
    S32 EvalJump(F32 dt, F32 duration, bool setOrient);
    S32 IsAlive();
    void OneLinersReset();
    void OneLinersUpdate(F32 dt);
    void RenderHud();
    void MeterUpdate();
    void MeterRender();
    bool LungeHit();
    bool AttackHit();
    // Returns the handle (Process keeps the teeter sound's handle in teeterSndHandle).
    iSndHandle SoundPlayEffect(eSound sound, F32 volume);

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
