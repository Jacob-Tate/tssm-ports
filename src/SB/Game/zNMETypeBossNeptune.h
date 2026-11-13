#ifndef ZNMETYPEBOSSNEPTUNE_H
#define ZNMETYPEBOSSNEPTUNE_H

#include "zWadNME.h"
#include "zGlobals.h"
#include "zLightning.h"
#include "xCamera.h"
#include "xCam.h"
#include "xFX.h"
#include "xModel.h"
#include "xJSP.h"
#include "iSnd.h"
#include "zNMEHealthMeter.h"

#include <rwcore.h>
#include <rpworld.h>

struct _zScript;
struct xRay3;

namespace Neptune
{
    enum eSOUND
    {
        eSOUND_Outrage,
        eSOUND_Damage,
        eSOUND_TailLeft,
        eSOUND_TailRight,
        eSOUND_DefendLoop,
        eSOUND_StunLoop,
        eSOUND_FireClick,
        eSOUND_Recover,
        eSOUND_Taunt,
        eSOUND_FireCharge,
        eSOUND_FireHoseLoop,
        eSOUND_FireLightningLoop,
        eSOUND_FireRingLoop,
        eSOUND_PovCharge,
        eSOUND_PovAttack,
        eSOUND_PovReflect,
        eSOUND_PovStunLoop,
        eSOUND_PovBucketLoop,
        eSOUND_VO_PatHintTable1,
        eSOUND_VO_PatHintTable2,
        eSOUND_VO_PatHintTable3,
        eSOUND_VO_PatHintDodge1,
        eSOUND_VO_PatHintDodge2,
        eSOUND_VO_PatHintWave1,
        eSOUND_VO_PatDamage1,
        eSOUND_VO_PlankDamage1,
        eSOUND_VO_PlankTauntDmg1,
        eSOUND_VO_PlankTauntDmg2,
        eSOUND_VO_PlankTauntDmg3,
        eSOUND_VO_PlankTaunt1,
        eSOUND_VO_NepTauntDmg1,
        eSOUND_VO_NepTaunt1,
        eSOUND_VO_NepTaunt2,
        eSOUND_VO_NepTaunt3,
        eSOUND_MAX
    };
} // namespace Neptune

struct NMECfgNeptuneAttack
{
    F32 time_randomtrack;
    F32 time_finaltrack;
    F32 time_lockon;
    S32 proj_count;
    F32 time_wait;
    F32 time_fire;
    S32 outrage_count;
    S32 feeler_count;
    F32 feeler_length;
    F32 feeler_rotation;
    F32 feeler_thickness;
    S32 ring_count;
    F32 ring_delay;
    F32 ring_minrad;
    F32 ring_maxrad;
    F32 ring_speed;
    F32 ring_thickness;
    F32 ring_lifetime;
};

struct NMENeptuneIndexedTag
{
    xVec3 tagpos;
    S32 modelIndex;
};

struct NMECfgNeptune
{
    S32 stageHealth[3];
    F32 tailattack_rad;
    F32 tailattack_height;
    F32 tailattack_time;
    F32 tailattack_kbvel;
    F32 tailattack_kbtime;
    F32 tailattack_ribstarttime;
    F32 tailattack_ribstoptime;
    F32 tailattack_bubstarttime;
    F32 tailattack_bubstoptime;
    F32 tailattack_bubspeed;
    F32 tailattack_bubdensity;
    F32 tailattack_bubscale;
    F32 player_safe_dist;
    F32 player_safe_vel;
    F32 stun_duration;
    F32 stun_neardist;
    F32 stun_bucketscale;
    F32 defend_cruiseTime;
    F32 defend_cruiseDist;
    F32 defend_min_duration;
    F32 defend_spinCenter;
    F32 defend_scale;
    F32 taunt_blurstart;
    F32 taunt_blurend;
    F32 turn_rate;
    F32 safety_time;
    U8 minion_state;
    F32 poke_dist;
    F32 outrageCam_dist;
    F32 outrageCam_height;
    F32 outrageCam_lerptime;
    F32 fire_angle_dot;
    F32 fire_target_height[3];
    F32 fire_track_factor;
    F32 fire_track_mindist;
    F32 fire_damageRadBeam;
    F32 fire_damageRadLightning;
    xVec3 aim_center;
    xVec3 aim_rest;
    F32 aim_anglerate;
    F32 aim_slerprate;
    U32 aim_slerpfirst;
    F32 aim_tooCloseMin;
    F32 aim_tooCloseMax;
    xVec3 projPOVTag[3];
    NMENeptuneIndexedTag bossTagInit[13];
    xVec3 playerMidSectInit[4];
    xVec3 bossStage2_safeLoc;
    xVec3 scope_camtag;
    xVec3 scope_center;
    xVec3 scope_posadjust;
    F32 scope_scaleadjust;
    U32 scope_drawtrident;
    F32 scope_zoomViewMin;
    F32 scope_zoomViewMax;
    F32 scope_zoomDistMin;
    F32 scope_zoomOutTime;
    F32 tridentblur_alphaStart;
    F32 tridentblur_alphaEnd;
    F32 tridentblur_colorStart;
    F32 tridentblur_colorEnd;
    F32 tridentblur_timeIncrement;
    U32 fire_tipAvgNum;
    F32 fire_colHeight;
    F32 fire_beamEmitRate;
    F32 fire_beamSpeedMin;
    F32 fire_beamSpeedMax;
    F32 fire_beamSpreadDist;
    F32 fire_beamSpreadTime;
    F32 fire_beamFrontDist;
    F32 fire_beamFrontStartDist;
    F32 fire_beamFrontEndDist;
    F32 fire_beamFrontDelay;
    F32 fire_beamFrontTime;
    F32 fire_spiralEmitRate;
    F32 fire_spiralSpeedMin;
    F32 fire_spiralSpeedMax;
    F32 fire_spiralRadiusRate;
    F32 fire_spiralRadiusTarget;
    F32 fire_spiralRotMin;
    F32 fire_spiralRotMax;
    S32 fire_spiralSign;
    F32 fire_ltTimeBeamEnd;
    F32 fire_ltTimeFeelerStart;
    F32 fire_ltTimeFeelerEnd;
    F32 fire_ltTimeRingStart;
    F32 fire_ltRingLerpRate;
    F32 fire_ltDamageDelay;
    F32 fire_ltDamageAfter;
    F32 pov_fadeTime;
    F32 pov_damageDelay;
    F32 pov_damageRad;
    F32 pov_krabsTargetTime;
    F32 pov_krabsTargetRad;
    xVec3 pov_krabsTargetPoint;
    xVec3 pov_reflectorTargetPoint;
    F32 pov_kbmaxvel;
    F32 pov_kbminvel;
    F32 pov_kbmaxtime;
    F32 pov_kbmindist;
    F32 pov_kbmaxdist;
    U32 pov_kbalways;
    F32 pov_pokeDist;
    F32 pov_stunModelSwapTime;
    F32 pov_stunAimLerp; // 0x30c
    F32 pov_stunTridentRot;
    F32 pov_stunAnimTranTime;
    F32 pov_missFlyStopDelay;
    F32 pov_missFlyStopTime[5];
    F32 pov_missFlySoundTime[5];
    F32 pov_glowStart;
    F32 pov_glowMax;
    F32 pov_glowMin;
    F32 pov_glowFadeTime;
    F32 snd_lightningVol;
    F32 snd_lightningFade;
    F32 snd_hoseVol;
    F32 snd_hoseFadeDown;
    F32 snd_hoseFadeUp;
    F32 snd_hoseCenterBias;
    F32 snd_povAttackVol;
    F32 snd_povReflectVol;
    F32 snd_povChargeTime;
    F32 snd_povChargeVol;
    F32 snd_povBeamStunVol;
    F32 snd_povBucketStunVol;
    F32 snd_povBucketStunFadeStart;
    F32 snd_povBucketStunFadeEnd;
    F32 snd_ringVol;
    F32 snd_ringFade;
    F32 snd_ringEarDist;
    F32 snd_ringDopplerPitchMax;
    F32 snd_ringDopplerPitchMin;
    F32 snd_ringDopplerDist;
    NMECfgNeptuneAttack stageAtk[3];
    F32 tailleft_angles[8];
    F32 tailright_angles[8];
};

// Size: 0xC (PS2 DWARF)
struct AnimDefStruct
{
    char* name;
    U32 flags;
    U32 userFlags;
};

struct zScorchDecal
{
    RpAtomic* atomic;
    xMat4x3* driver;
    F32 area;
    F32 timer;
};

struct zNeptuneParTex
{
    F32 tnext;
    F32 tx;
    F32 ty;
    F32 tsize;
};

struct zNeptuneParCol
{
    F32 tnext;
    U8 r;
    U8 g;
    U8 b;
    U8 a;
    F32 tsub;
    F32 tmul;
};

struct zNeptuneParSize
{
    F32 tnext;
    F32 size;
    F32 tsub;
    F32 dsize;
};

struct zNeptuneParType
{
    F32 lifetime;
    U8 updtype;
    U8 texidx;
    U8 colidx;
    U8 sizeidx;
};

struct zNeptuneParticle
{
    xVec3 pos;
    F32 lifetime;
    xVec3 vel;
    F32 liferate;
    U8 updtype;
    U8 texidx;
    U8 colidx;
    U8 sizeidx;
    U32 collide;
    U32 userdata[10];
};

struct zNeptuneParticleBeamUser
{
    xVec3 velStart;
    xVec3 velEnd;
    F32 velRate;
};

struct zNeptuneParticleSpiralUser
{
    S16 basis[6];
    F32 anglecs[2];
    F32 rad;
    F32 delta;
};

struct zNeptuneParticleGravBounceUser
{
    F32 gravity;
    F32 bounceHeight;
};

union zNeptuneF32orU32
{
    F32 f;
    U32 u;
};

struct zNeptuneSortParticle
{
    zNeptuneF32orU32 x;
    zNeptuneF32orU32 y;
    zNeptuneF32orU32 z;
    U32 depthlife;
};

struct zCamNeptune : xCam
{
    struct config
    {
        xVec3 viewPoint;
        F32 targetHeight;
        F32 povfov;
    };

    config cfg;
    U32 lastFlyThru;

    // Member functions follow the original source order (the PS2 DWARF lists them reversed);
    // create is the key function that emits __vt__11zCamNeptune.
    zCamNeptune();
    // Inline and empty: zNMENeptune's destructor resets povcam's vtable pointer (and only that
    // one) in retail.
    ~zCamNeptune()
    {
    }
    void create();
    void start();
    void update(xScene& s, F32 dt);
    void destroy();
};

// Type-specific asset data of "Enemy:SB:Neptune" DYNAs. The PS2 DWARF has no type for it, so
// the name is ours; nothing reads the trailing fields yet, the total size (0x68) is the one
// ValidNeptune (zNMEAsset.cpp) checks for.
struct zNMEAssetNeptune : zNMEAsset
{
    U32 unk_60[2];
};

// Field order follows the PS2 DWARF.
struct zNMENeptune : zNMEBoss
{
    zNMENeptune()
    {
    }
    struct zLoopingSound
    {
        iSndHandle handle;
        U8 (*sndUpdateCB)(zLoopingSound*);
        void* context;
        F32 time;
        xVec3 pos;
        F32 volscale;
        F32 pitch;
    };

    struct zLightningRing
    {
        xMat4x3 mat;
        zLightning* lt;
        F32 rad;
        xVec3 vel;
        F32 radvel;
        F32 startTime;
        zLoopingSound* snd;
    };

    S32 pts_health[3]; // 0x1c0
    const NMECfgNeptune* cfg_neptune;
    F32 last_dt;
    U32 bossStage; // 0x1d4
    U32 state; // 0x1d8
    F32 timer;
    U32 camState;
    U8 stateFrozen;
    U8 minionsActive;
    U32 stage2wait;
    S32 gotPlayer;
    S32 gotPlayerThisAttack;
    S32 tauntEm;
    U32 beamParThisFrame;
    S32 outrageCount;
    U32 gotNeptune;
    F32 lightningDamageAfter;
    U8 tailAttackValid;
    U8 tailAttackRange;
    U8 tailAttackRight;
    U8 tailAttackToggle;
    U8 tailAttackKnocked;
    U8 pad0[3];
    F32 tailRepeatTimer;
    xModelInstance* bossModel[5];
    xModelTag bossTag[13];
    xVec3 lastTag[13];
    xVec3 currTag[13];
    U32 firstTag;
    xModelTag playerMidSect[4];
    xVec3 playerMidHist[3];
    xEnt* collideBucket;
    xEnt* collideDefend;
    xEnt* collideBody;
    xEnt* collidePOV;
    U8 bucketCollideActive;
    U8 pad1[3];
    S32 bucketInfl;
    RwMatrixTag bucketSkinMat;
    S32 defendLast;
    xVec3 defendLastVec;
    F32 cruiseTimer;
    xEnt* entDestructFloor;
    xEnt* entMrKrabs;
    xEnt* entReflectors[5];
    xEnt* entMinions[6];
    xBase* baseBeamFlythru;
    xJSPHeader* underFloorJSP;
    xEnt* entCrowd[1][21];
    S32 crowdSlaveUpdate;
    _zScript* deflectSFX;
    xVec3 aim_pos;
    xVec3 aim_dir;
    xQuat aim_momentum;
    xMat4x3 aim_mat;
    xVec3 locked_aim_pos;
    F32 locked_aim_lerp;
    F32 aim_tooCloseLerp;
    S32 povfire_shotcount;
    zCamNeptune povcam;
    zBossCam_Binary bosscam;
    xVec3 bossPlayerTarget;
    xVec3 bossBossTarget;
    xVec3 firePitchStart[17];
    xVec3 firePitchDir[17];
    xVec3 fireTarget;
    xVec3 fireFace;
    xVec3 firePlayerLagVel[45];
    S32 firePlayerLagIdx;
    F32 firePlayerLagDir;
    F32 fireTimer;
    F32 fireBeamEmit;
    F32 fireSpiralTineEmit[2];
    xVec3 fireTipAvg[30];
    U32 fireTipAvgCount;
    U32 fireTipAvgIdx;
    F32 fireBeamGroundHit;
    xVec3 fireBeamGroundLoc;
    U32 fireBeamParLaunched;
    xVec3 fireBeamParPos;
    xVec3 fireBeamParVel;
    F32 ltTimer;
    zLightning* ltBeam[1];
    zLightning* ltFeeler[12];
    xVec3 ltBeamTarget;
    xVec3 ltFeelerTargets[24];
    zLightningRing ltRing[12];
    S32 ltRingStarted;
    RwTexture* rwpovbeamtex;
    xMat4x3 povBeamMat;
    xVec3 povBeamHit;
    F32 povBeamFade;
    F32 povBucketFade;
    F32 povTridentGlow;
    F32 povBeamU;
    U32 povHitReflectors;
    U32 povStunnedMe;
    U16 povKrabsLockon;
    U16 povKrabsLockCloser;
    F32 povKrabsLockDist;
    xVec3 povStunTag;
    U8 povStunTagCalced;
    U8 povFirstReflect;
    U8 povBeamReflected;
    U8 povChargeSoundPlayed;
    xVec3 povBeamReflectDir;
    RwTexture* rwdecaltex;
    S32 scorchCount;
    zScorchDecal scorchList[16];
    F32 scorchEmit;
    F32 scorchArea;
    xFXCameraTexture scopeTex;
    RpWorld* scopeWorld;
    RpLight* scopeLight;
    RpAtomic* scopeModel[2];
    RpAtomic* tridentModel;
    xMat4x3 scopeCamMat;
    F32 scopeCamFOV;
    F32 scopeCamFOVLerp;
    xModelInstance scopeCamModelInst[3];
    xModelBucket** scopeCamModelBucket[3];
    RwRaster* scopeOldRast;
    xFXRibbon* tail_ribbon[2];
    F32 tail_bubcounter;
    S32 tridentInfl[4];
    xModelInstance* blurTrident[8];
    S32 blurHistLast;
    S32 blurHistTotal;
    xQuat blurHistoryQuat[6][4];
    xVec3 blurHistoryTran[6][4];
    zNeptuneParTex parTex[3];
    zNeptuneParCol parCol[7];
    zNeptuneParSize parSize[8];
    S32 parStartTex[3];
    S32 parStartCol[7];
    S32 parStartSize[8];
    S32 parCountTex;
    S32 parCountCol;
    S32 parCountSize;
    RwTexture* rwpartex;
    zNeptuneParticle par[1024];
    S32 parCount;
    zLoopingSound loopSnd[16];
    iSndHandle voSound; // 0x15fb8
    F32 voRand;
    F32 voTimer;
    F32 voTableHintTimer;
    S32 voTableHintCount;
    S32 voDamageToggle;
    S32 voPOVDamageToggle;
    S32 voOutrageTauntToggle;
    S32 voDodgeHoseToggle;
    S32 voCruiseBubbleToggle;

    // CreateConfig is the key function that emits __vt__11zNMENeptune.
    void CreateConfig(const char* name);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Init(xEntAsset* entasset);
    void CommonReset();
    zLoopingSound* SndLoop3D(Neptune::eSOUND soundEnum, xVec3* playpos, void* context);
    void CamStateChange(U32 newCamState, bool force_cut);
    void FirePitchUpdate();
    void ParArrayInit();
    void ParEmitBunch(F32* emitVar, F32 emitRate, F32 dt,
                      U32 (*emitCB)(zNeptuneParticle*, void*, F32), void* context);
    void ScorchUpdate(F32 dt);
    void ParUpdate(F32 dt, bool updatelast);
    void RenderLightBeam(xVec3* start, xVec3* end, xVec3* dir, xVec3* startnorm, xVec3* endnorm,
                         F32 radstart, F32 radend, xColor_tag color, F32 texu_start, F32 texu_end,
                         F32 texv_start, F32 texv_end, S32 numseg, S32 numradial);
    void DefendCollideUpdate();
    void CollideLightning(RxObjSpace3DVertex* verts, U32 numVerts);
    void TailSwipeUpdate();
    void FireFXUpdate(F32 dt);
    void PaintDecalOnPlayer();
    void BossCamSetTargets();
    U8 BeamCollide(xRay3* beamray, S32* aimedAtKrabs, xVec3* reflectnorm);
    void Setup();
    void Destroy();
    void Reset();
    void Process(F32 dt);
    void PostProcess(F32 dt);
    void Render();
    void RenderExtra();
    void RenderHud();
    void MeterRender();
    S32 TypeHandleMail(NMEMsg* mail);

    // GameCube-only members (absent from the PS2 demo DWARF).
    void MeterSetup();
    void MeterUpdate();
    bool PlayerGotPoked(xVec3* tri, F32 dist);
    F32 KrabAimDistance2(xMat4x3* mat);
    void PovCollideUpdate();
    void BodyCollideUpdate();
    void BucketCollideUpdate();
    void BucketCollideTransform(F32 scale);
    F32 DotToPlayer();
    bool SndVOTableHintCheck();
    void SndVOUpdate(F32 dt);
    iSndHandle SndPlay(Neptune::eSOUND soundEnum, F32 volume);
    iSndHandle SndPlay3D(Neptune::eSOUND soundEnum, xVec3* playpos, F32 volume);

    S32 WannaPoolShadow()
    {
        return 2;
    }

    void SetState(U32 newState)
    {
        state = newState;
        timer = 0.0f;
    }

    // No GC symbol (always inlined); the name is ours.
    bool ScopeViewActive()
    {
        if (!(state & 0x1000))
        {
            return false;
        }

        if (state == 0x1004 && timer > cfg_neptune->pov_stunModelSwapTime)
        {
            return false;
        }

        return true;
    }

    // No GC symbol (always inlined); the name is ours. True while Render draws the trident blur
    // (the taunt window or state 5).
    bool TridentBlurActive()
    {
        if (state == 5)
        {
            return true;
        }

        if (strcmp(model->Anim->Single->State->Name, "Taunt") == 0 &&
            model->Anim->Single->Time >= cfg_neptune->taunt_blurstart &&
            model->Anim->Single->Time <= cfg_neptune->taunt_blurend)
        {
            return true;
        }

        return false;
    }

    bool IsDead()
    {
        return bossStage == 2 && pts_health[2] == 0;
    }

    bool SndVOCheck(F32 mintime)
    {
        return voSound == -1 && voTimer > mintime && globals.player.s->Health != 0;
    }
};

namespace Neptune
{
    struct SoundTable
    {
        char* assetName;
        U8 (*updateCB)(zNMENeptune::zLoopingSound*);
        U32 assetID;
    };
} // namespace Neptune

#endif
