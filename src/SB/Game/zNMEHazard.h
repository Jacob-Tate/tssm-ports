#ifndef ZNMEHAZARD_H
#define ZNMEHAZARD_H

#include "zNMECentral.h"
#include "zNMECommon.h"
#include "zNMEHazardTypeInfo.h"
#include "xMath3.h"
#include "xCollide.h"
#include "iSnd.h"

struct zNMEHazard;
struct zLightning;
struct xFXRibbon;
struct xShadowCache;
struct xShadowSimpleCache;

enum en_haznote
{
    HAZ_NOTE_DISCARD,
    HAZ_NOTE_ABORT,
    HAZ_NOTE_HITPLAYER,
    HAZ_NOTE_RECONFIG,
    HAZ_NOTE_NOMORE,
    HAZ_NOTE_FORCE = 0x7fffffff
};

enum en_hazcol
{
    HAZ_COLTYP_STAT,
    HAZ_COLTYP_DYN,
    HAZ_COLTYP_NME,
    HAZ_COLTYP_NOMORE,
    HAZ_COLTYP_FORCE = 0x7fffffff
};

struct HAZNotify
{
    virtual S32 Notify(en_haznote note, zNMEHazard* haz)
    {
        return 0;
    }
};

// xMat3x3 is 16-byte aligned on GC, so every custdata member is padded to 16 bytes
// (target: custdata at 0x40, HAZCollide::flg_collide at 0xAC, sizeof(zNMEHazard) == 0x110).
struct HAZTypical
{
    xMat3x3 mat_rotDelta;
    F32 rad_min;
    F32 rad_max;
    F32 rad_cur;
    xVec3 vel;
};

struct HAZCollide : HAZTypical
{
    xVec3 pos_collide;
    xVec3 dir_normal;
    xParabola* parabinfo;
    struct
    {
        S32 flg_collide : 8;
        S32 flg_result : 8;
        S32 flg_unused : 16;
    };
    S32 cnt_skipcol;
    en_hazcol idx_rotateCol;
};

struct HAZLob : HAZCollide
{
    xVec3 pos_tgt;
};

struct HAZBang : HAZTypical
{
    xEnt* ent;
    xVec3 offset;
    xVec3 offsetRot;
    xVec3 targetOffsetRot;
    S32 state;
};

struct HAZMisc : HAZTypical
{
    void* ptr;
};

struct HAZFXRibbon
{
    union
    {
        xFXRibbon* ribbon;
        xFXRibbon* riblist[4];
    };
    S32 cnt_skipRibInsert;
};

struct HAZFXLightning
{
    zLightning* lytlist[4];
    S32 cnt_skipRibInsert;
};

struct zNMEHazard
{
    zNMEHazard()
    {
    }
    zNMEHazard(en_haztyp haztype)
    {
        typ_hazard = haztype;
    }
    en_haztyp typ_hazard;
    zNMECommon* npc_owner;
    HAZNotify* cb_notify;
    zNMEHazard* haz_parent;
    S32 flg_hazard; // 0x10
    xVec3 pos_hazard;
    S32 flg_custom; // 0x20
    F32 tym_lifespan;
    F32 tmr_remain;
    F32 pam_interp;
    xModelInstance* mdl_hazard; // 0x30
    union
    {
        F32 tmr_generic;
        F32 tmr_parEmit;
        F32 tmr_lessismore;
        F32 tmr_nextEmit;
        F32 tmr_skipEmit;
        F32 tmr_skipNext;
    };
    union
    {
        S32 cnt_generic;
        S32 flg_casthurt;
    };
    struct
    {
        union
        {
            HAZTypical typical;
            HAZCollide collide;
            HAZLob lobdata;
            HAZBang bang;
            HAZMisc misctyp;
        };
    } custdata; // 0x40
    struct
    {
        HAZFXRibbon ribdata;
        HAZFXLightning lytdata;
    } fxdata; // 0xD0
    xShadowCache* shadCacheComplex;
    xShadowSimpleCache* shadCacheSimple;
    S32 cnt_nextFill;
    iSndHandle sndIDPlayed;

    void StagColGeneral(S32 who);
    S32 StaggeredCollide();
    void PreCollide(F32 acc_grav, F32 tym_collDelay);
    void OrientToDir(const xVec3* vec_path, S32 doTheTwist);
    void TypData_RotMatSet(const xMat3x3* mat_rot);
    // No GC or PS2 symbol (inlined into zNMEGoalEvilPat::GlyphStart and ZapBoom::Configure); body
    // from BfBB's NPCHazard::TypData_RotMatStore, as in the Xbox build's out-of-line copy. The
    // implicit 'if (euler)' test keeps mat_spin a variable, as retail's GlyphStart needs.
    void TypData_RotMatStore(const xVec3* euler)
    {
        xMat3x3* mat_spin = &custdata.typical.mat_rotDelta;

        if (euler)
        {
            xMat3x3Euler(mat_spin, euler);
        }
        else
        {
            xVec3 ang_spin;

            ang_spin.x = 12.566371f * (2.0f * (xurand() - 0.5f));
            ang_spin.y = 0.78539819f * (2.0f * (xurand() - 0.5f));
            ang_spin.z = 12.566371f * (2.0f * (xurand() - 0.5f));
            ang_spin *= (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            xMat3x3Euler(mat_spin, &ang_spin);
        }

        flg_hazard |= 0x4000;
    }
    // No GC or PS2 symbol (inlined into zMODHazard::Timestep); BfBB's NPCHazard::TypData_RotMatApply
    // (out of line there). Timestep's PS2 DWARF lists no frame local and puts the whole
    // GetFrame/Mul/SetFrame sequence on one line.
    void TypData_RotMatApply(const xMat3x3* mat)
    {
        xMat4x3* frame = xModelGetFrame(mdl_hazard);
        xMat3x3Mul(frame, mat, frame);
        xModelSetFrame(mdl_hazard, frame);
    }
    S32 ColTestCapsule(const xBound* bnd_tgt, const xVec3* pos, F32 radiusCaller, const xVec3* vel,
                       F32 tym, S32 isPlayer);
    // No GC or PS2 symbol (always inlined into the hazard types' Update functions; their PS2
    // DWARF lists no locals); bodies from BfBB's NPCHazard::ColTestSphere/ColPlyrSphere/HurtThePlayer.
    S32 ColTestSphere(const xBound* bnd_tgt, F32 rad)
    {
        S32 hit = 0;
        xCollis colrec;
        xBound bnd;

        memset(&colrec, 0, sizeof(xCollis));
        memset(&bnd, 0, sizeof(xBound));

        bnd.type = XBOUND_TYPE_SPHERE;
        bnd.sph.r = rad;
        xVec3Copy(&bnd.sph.center, &pos_hazard);

        xSphereHitsBound(&bnd.sph, bnd_tgt, &colrec);

        if (colrec.flags & 1)
        {
            hit = 1;
            if (!(flg_hazard & 0x40000000) && cb_notify != NULL)
            {
                cb_notify->Notify(HAZ_NOTE_HITPLAYER, this);
            }
            flg_hazard |= 0x40000000;
        }

        return hit;
    }
    S32 ColPlyrSphere(F32 rad)
    {
        return ColTestSphere(&xglobals->___player_ent_dont_use_directly->bound, rad);
    }
    void HurtThePlayer()
    {
        if (npc_owner == NULL)
        {
            zEntPlayer_Damage(NULL, 1);
        }
        else
        {
            zEntPlayer_DamageNPCKnockBack(npc_owner, 1, &pos_hazard,
                                          ePlayerNMEDamageEnemyProjectile);
        }
    }
    void PosSet(const xVec3* pos);
    void Start(const xVec3* pos, F32 tym);
    void Discard();
    S32 GrabModel(en_haztyp);
    S32 Reconfigure(en_haztyp haztype);
    S32 ConfigHelper(en_haztyp haztype);

    // No symbol in TSSM (always inlined); name unknown. Inferred from cruise_bubble's
    // RockStar_Update and CinFXDenTwo's NCIN_HazBowWave_AR: both load the hazard once and
    // divide by the lifespan argument instead of re-reading tym_lifespan.
    void LifespanSet(F32 tym_left, F32 tym_life)
    {
        tmr_remain = tym_left;
        tym_lifespan = tym_life;
        F32 pam = 1.0f - tmr_remain / tym_life;
        pam_interp = CLAMP(pam, 0.0f, 1.0f);
    }

    // No symbol in TSSM (always inlined); body from BfBB's NPCHazard::SetAlpha.
    void SetAlpha(F32 alpha)
    {
        if (mdl_hazard == NULL)
        {
            return;
        }
        mdl_hazard->Flags |= 0x4000;
        mdl_hazard->Alpha = alpha;
    }

    // No symbol in TSSM (always inlined); body from BfBB's NPCHazard::Kill.
    void Kill()
    {
        if (flg_hazard & 1)
        {
            if (cb_notify != NULL)
            {
                cb_notify->Notify(HAZ_NOTE_ABORT, this);
            }
            Discard();
        }
    }

    // No symbols in TSSM (always inlined); BfBB's NPCHazard has these const accessors. The xVec3*
    // return type is ours (as in BfBB's NPCC_faceDir and siblings); TuneBang's BubbleCannon matches
    // only with it.
    xVec3* At() const
    {
        return (xVec3*)&this->mdl_hazard->Mat->at;
    }

    xVec3* Right() const
    {
        return (xVec3*)&this->mdl_hazard->Mat->right;
    }

    xVec3* Up() const
    {
        return (xVec3*)&this->mdl_hazard->Mat->up;
    }
};

void HAZ_Iterate(bool (*fp)(zNMEHazard&, void*), void* context, S32 flag_filter);
zNMEHazard* HAZ_Acquire();
void HAZ_RegisterTypes();

// Defined in zNMEHazardTypes.cpp (they use its statics); no GC or PS2 symbol, the names are ours.
// zMODHazard::ScenePostInit inlines all three; a dead-stripped copy of HAZ_TypeTweaks left its
// strings in retail's pool.
void HAZ_PostInit_Shrapnel();
void HAZ_TypeTweaks(const char* prefix_root);
void HAZ_PostInit_Sounds();

void zNMEHazard_Startup();
void zNMEHazard_Shutdown();

#endif
