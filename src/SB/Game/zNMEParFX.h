#ifndef ZNMEPARFX_H
#define ZNMEPARFX_H

#include <types.h>
#include "xVec3.h"
#include "iColor.h"
#include "xParticleBatch.h"
#include "zNMESupport.h"
#include "zNMECentral.h"

struct xMat3x3;

enum en_npartyp
{
    NPAR_TYP_UNKNOWN,
    NPAR_TYP_DUMMY,
    NPAR_TYP_DUSTPUFF,
    NPAR_TYP_DUSTHEAT,
    NPAR_TYP_RADIOACTIVE,
    NPAR_TYP_FLINGVAPORS_DE,
    NPAR_TYP_FLINGVAPORS_TR,
    NPAR_TYP_FLINGVAPORS_JK,
    NPAR_TYP_FOGBREATH,
    NPAR_TYP_POPGAS,
    NPAR_TYP_SPITSPRAY,
    NPAR_TYP_SLIMED,
    NPAR_TYP_PLASMA,
    NPAR_TYP_ENERGY,
    NPAR_TYP_MUZZLE,
    NPAR_TYP_CLOUDS,
    NPAR_TYP_EXHAUST,
    NPAR_TYP_HEATED,
    NPAR_TYP_NOTES,
    NPAR_TYP_NOTES_UPGRADE,
    NPAR_TYP_RKOWAVES,
    NPAR_TYP_JETFLAME,
    NPAR_TYP_JETSMOKE,
    NPAR_TYP_SEASPRAY,
    NPAR_TYP_SEASPLASH,
    NPAR_TYP_SEAMIST,
    NPAR_TYP_SEAFOG,
    NPAR_TYP_SEAFOG_NOROT,
    NPAR_TYP_SEASPARKLE,
    NPAR_TYP_SEAFOAM,
    NPAR_TYP_SEARAINBOW,
    NPAR_TYP_RAIN,
    NPAR_TYP_SPAWNRINGS,
    NPAR_TYP_MACHRING,
    NPAR_TYP_RINGS,
    NPAR_TYP_SPARKS,
    NPAR_TYP_COLORS,
    NPAR_TYP_SPARKY,
    NPAR_TYP_FOUNTAIN,
    NPAR_TYP_FIRESPIRAL,
    NPAR_TYP_FIREWORKS,
    NPAR_TYP_NOMORE,
    NPAR_TYP_FORCE = 0x7fffffff
};

// zNMEParFXTypePuffRing

struct PropWashParm
{
    F32 rad_ring;
    F32 spd_outward;
};

struct ImpactRingParm
{
    F32 rad_ring;
    F32 spd_outward;
};

struct FootstrokeParm
{
    F32 spd_emitBase;
    F32 spd_emitRand;
};

// Size: 0x30
struct PuffEmitter
{
    en_npartyp useNPARtype;
    F32 tym_lifespan;
    xColor_tag colr_base;
    F32 siz_base[2];
    xVec3 acc_base;
    F32 spd_rotate;
    S8 pct_keep;
    S8 doDamage;
    S8 num_emit;
    S8 unused;
    union
    {
        PropWashParm wash;
        ImpactRingParm ring;
        FootstrokeParm stroke;
    };

    void Footstroke(const xVec3* pos, const xVec3* dir_emit) const;
    void RoundRing(const xVec3* pos, F32 radius, const xVec3* vel_drift) const;
    void ImpactRing(const xVec3* pos, const xVec3* vel_drift) const;
    void PropWash(const xVec3* pos, const xVec3* vel_drift) const;
};

// zNMEParFXTypePopGas

struct FountainParm
{
    F32 spd_emitBase;
    F32 spd_emitRand;
    F32 rad_emit;
    F32 spd_outBase;
    F32 spd_outRand;
};

struct GroundCoverParm
{
    F32 spd_emitBase;
    F32 spd_emitRand;
    F32 rad_ring;
};

struct PopRockTrailParm
{
    F32 spd_emitBase;
    F32 spd_emitRand;
};

// Size: 0x3C
struct PopGasEmitter
{
    en_npartyp useNPARtype;
    F32 tym_lifespan;
    xColor_tag colr_base;
    F32 siz_base[2];
    xVec3 acc_base;
    F32 spd_rotate;
    U8 pct_keep;
    U8 doDamage;
    U8 num_emit;
    U8 pct_accelSide;
    union
    {
        FountainParm fount;
        GroundCoverParm cover;
        PopRockTrailParm trail;
    };

    void PopRockTrail(const xVec3* pos, const xVec3* dir_emit) const;
    void Fountain(const xVec3* pos, const xVec3* vel_drift) const;
};

// zNMEParFXTypePlasma

// Size: 0x3C
struct PlasmaEmitter
{
    en_npartyp useNPARtype;
    F32 tym_lifespan;
    xColor_tag colr_base;
    F32 siz_base[2];
    xVec3 acc_base;
    F32 spd_rotate;
    U8 pct_keep;
    U8 doDamage;
    U8 num_emit;
    U8 pct_accelSide;
    FountainParm fount;

    void ArcWelder(const xVec3* pos, const xVec3* dir_axis, F32 pct_spdRamp,
                   S32 num_emitOverride) const;
};

// zNMEParFXTypeExhaust

struct ExhaustCurve
{
    F32 time;
    xColor_tag color;
    F32 scale;
};

// Size: 0x78
struct ExhaustEmitter
{
    struct Flags
    {
        S8 doDamage;
        S8 useLinear;
        S8 unused[2];
    };
    struct RunTime
    {
        F32 acc_gravity;
        F32 fac_damp;
        F32 spd_rotate;
    };
    struct Size
    {
        F32 width;
        F32 height;
    };
    struct Damage
    {
        F32 rad_collide;
        F32 pam_collide[2];
        U8 alf_collide;
        U8 unused[3];
    };
    struct Life
    {
        F32 tym_lifeBase;
        F32 tym_lifeRand;
    };
    struct Emit
    {
        S32 num_emitBase;
        S32 num_emitRand;
        F32 rad_emitBase;
        F32 rad_emitRand;
    };
    struct Speed
    {
        F32 spd_emitBase;
        F32 spd_emitRand;
        F32 spd_outBase;
        F32 spd_outRand;
        F32 acc_emitaxis;
        F32 acc_outward;
        xVec3 acc_world;
    };
    struct CurveInfo
    {
        ExhaustCurve* curveData;
        S32 curveSize;
        F32 alf_master;
        F32 scl_master;
    };

    en_npartyp useNPARtype;
    Flags flags;
    RunTime run;
    Size size;
    Damage dmg;
    Life life;
    Emit emit;
    Speed speed;
    CurveInfo curve;

    void EmitLineUp(const xVec3* pos_beg, const xVec3* pos_end, xVec3* vel_drift, F32 pct_spdRamp,
                    S32 num_emitCaller, F32 rad_emitCaller) const;
    void EmitRing(const xVec3* pos_center, const xVec3* dir_axis, F32 rad_emitCaller,
                  F32 pct_spdRamp, S32 num_emitCaller, xVec3* vel_drift) const;
    void EmitOnAxis(const xVec3* pos_center, const xVec3* dir_axis, F32 pct_spdRamp) const;
    S32 FindCurveNodes(F32 rat, ExhaustCurve*& node_lo, ExhaustCurve*& node_hi) const;
    S32 EvalCurve(F32 rat, ExhaustCurve* node_eval, S32 linearize, S32 idx) const;
};

// zNMEParFXTypeOrient

struct OrientCurve
{
    F32 time;
    xColor_tag color;
    F32 scale;
};

// Size: 0x78 (PS2 DWARF)
struct OrientEmitter
{
    struct Flags
    {
        S8 doDamage;
        S8 useLinear;
        S8 opt_rotate;
        S8 notused;
    };
    struct RunTime
    {
        F32 acc_gravity;
        F32 fac_damp;
        F32 spd_rotate;
    };
    struct Size
    {
        F32 width;
        F32 height;
    };
    struct Damage
    {
        F32 rad_collide;
        F32 pam_collide[2];
        U8 alf_collide;
        U8 unused[3];
    };
    struct Life
    {
        F32 tym_lifeBase;
        F32 tym_lifeRand;
    };
    struct Emit
    {
        S32 num_emitBase;
        S32 num_emitRand;
        F32 rad_emitBase;
        F32 rad_emitRand;
    };
    struct Speed
    {
        F32 spd_emitBase;
        F32 spd_emitRand;
        F32 spd_outBase;
        F32 spd_outRand;
        F32 acc_emitaxis;
        F32 acc_outward;
        xVec3 acc_world;
    };
    struct CurveInfo
    {
        OrientCurve* curveData;
        S32 curveSize;
        F32 alf_master;
        F32 scl_master;
    };

    en_npartyp useNPARtype;
    Flags flags;
    RunTime run;
    Size size;
    Damage dmg;
    Life life;
    Emit emit;
    Speed speed;
    CurveInfo curve;

    void EmitRing(const xVec3* pos_center, const xVec3* dir_axis, F32 rad_emitCaller,
                  F32 pct_spdRamp, S32 num_emitCaller) const;
    S32 FindCurveNodes(F32 rat, OrientCurve*& lo, OrientCurve*& hi) const;
    S32 EvalCurve(F32 rat, OrientCurve* node_eval, S32 linearize, S32 idx_curr) const;
};

// zNMEParFXTypeSparky

struct SparkyCurve
{
    F32 time;
    xColor_tag color;
    F32 scale;
};

// Size: 0x74
struct SparkyEmitter
{
    struct Flags
    {
        S8 doDamage;
        S8 useLinear;
        S8 unused[2];
    };
    struct RunTime
    {
        F32 acc_gravity;
        F32 fac_damp;
    };
    struct Size
    {
        F32 width;
        F32 height;
    };
    struct Damage
    {
        F32 rad_collide;
        F32 pam_collide[2];
        U8 alf_collide;
        S8 unused[3];
    };
    struct Life
    {
        F32 tym_lifeBase;
        F32 tym_lifeRand;
    };
    struct Emit
    {
        S32 num_emitBase;
        S32 num_emitRand;
        F32 rad_emitBase;
        F32 rad_emitRand;
    };
    struct Speed
    {
        F32 spd_emitBase;
        F32 spd_emitRand;
        F32 spd_outBase;
        F32 spd_outRand;
        F32 acc_emitaxis;
        F32 acc_outward;
        xVec3 acc_world;
    };
    struct CurveInfo
    {
        SparkyCurve* curveData;
        S32 curveSize;
        F32 alf_master;
        F32 scl_master;
    };

    en_npartyp useNPARtype;
    Flags flags;
    RunTime run;
    Size size;
    Damage dmg;
    Life life;
    Emit emit;
    Speed speed;
    CurveInfo curve;

    void DenSeaItRain(const xVec3* pos_beg, const xVec3* pos_end, const xVec3* vel_drift,
                      F32 rad_emitCaller, F32 fac_spdRamp, S32 num_emitCaller) const;
    void EmitInDir(const xVec3* pos, const xVec3* vel, S32 num_emitCaller) const;
    void EmitRing(const xVec3* pos_center, const xVec3* dir_axis, F32 rad_emitCaller,
                  F32 fac_spdRamp, S32 num_emitCaller) const;
    void EmitOnAxis(const xVec3* pos_center, const xVec3* dir_axis, F32 fac_spdRamp) const;
    S32 FindCurveNodes(F32 rat, SparkyCurve*& node_lo, SparkyCurve*& node_hi) const;
    S32 EvalCurve(F32 rat, SparkyCurve* node_eval, S32 linearize, S32 idx_prev) const;
};

// zNMEParFXTypeFogBreath

// Size: 0x40
struct FogBreathEmitter
{
    struct Volume
    {
        S32 num_emit;
        F32 spd_emitBase;
        F32 spd_emitRand;
        xVec3 vec_coneWeights;
    };

    en_npartyp useNPARtype;
    F32 tym_lifespan;
    xColor_tag colr_base;
    F32 siz_base[2];
    xVec3 acc_base;
    F32 spd_rotate;
    S8 pct_keep;
    S8 doDamage;
    S8 unused1;
    S8 unused2;
    Volume volume;

    void SpewVolume(const xVec3* pos, const xMat3x3* axis) const;
};

struct NPARMgmt;

struct NPARInfo
{
    void (*fun_initsys)(NPARMgmt*, xParticleBatchSystem*);
    void (*fun_tweaks)(NPARMgmt*, const char*);
    char* nam_texture;
    S32 flg_npar;
};

// Defined static in zNMEParFX.cpp (scope:local).
extern NPARInfo* g_npar_info[NPAR_TYP_NOMORE];

struct NPARMgmt
{
    en_npartyp typ_npar;
    S32 pbs_sysid;
    S32 flg_npar;
    NPARInfo* inf_npar;
    RwTexture* txtr;
    void** user_data;
    struct
    {
        S32 flg_killall : 1;
        S32 flg_unusedFlags : 31;
    };

    // No GC symbol: always inlined (zMODParFX::ScenePrepare/SceneFinish).
    void Clear()
    {
        typ_npar = NPAR_TYP_UNKNOWN;
        flg_npar = 0;
        pbs_sysid = 0;
        inf_npar = NULL;
        txtr = NULL;
        user_data = NULL;
        flg_killall = 0;
    }

    // No GC symbol: always inlined (zMODParFX::SceneFinish).
    void Done()
    {
        Clear();
    }

    // No GC symbol: always inlined (zMODParFX::SceneReset); does nothing on GC.
    void Reset()
    {
    }

    // No GC symbol: always inlined into zNMEParFX_PartyInvite.
    void Init(en_npartyp npartype, void** userData)
    {
        NPARInfo* info;
        xParticleBatchSystem pbs;

        info = g_npar_info[npartype];
        if (info == NULL)
        {
            return;
        }

        typ_npar = npartype;
        inf_npar = info;
        flg_npar = info->flg_npar;
        user_data = userData;
        if (info->nam_texture == NULL)
        {
            txtr = NULL;
        }
        else
        {
            txtr = NMEC_FindRWTexture(info->nam_texture);
        }
        info->fun_initsys(this, &pbs);
        pbs_sysid = xParticleBatchAddSystem(pbs);
    }
};

extern NPARMgmt g_npar_mgmt[NPAR_TYP_NOMORE];

// Texture cell corners (top-left, bottom-right) for 2x2, 3x3 and 4x4 particle atlases, filled in
// by zNMEParFX_RegisterTypes and indexed [cell][corner].
extern xVec2 uv_cell_2x2[4][2];
extern xVec2 uv_cell_3x3[9][2];
extern xVec2 uv_cell_4x4[16][2];

// No GC or PS2 symbol: always inlined into the emitters (e.g. PuffEmitter::Footstroke); the name
// follows BfBB's NPAR_FindParty, which has the same body.
inline NPARMgmt* zNMEParFX_FindParty(en_npartyp npartype)
{
    NPARMgmt* mgmt = &g_npar_mgmt[npartype];
    if (mgmt->inf_npar != NULL)
    {
        return mgmt;
    }
    return NULL;
}

// Member functions follow the original source order (the PS2 DWARF lists them reversed);
// ScenePrepare is the key function that emits the vtable.
struct zMODParFX : zModule
{
    S32 gameExtrasFlags;
    S32 isSpecialDay;
    S32 month;
    S32 day;

    void ScenePrepare();
    void SceneFinish();
    void SceneReset();
    void Timestep(F32 dt);
};

// Every particle type registers its NPARInfo records through a SysRegister function in its own
// file. zNMEParFX_RegisterTypes inlines them all, so their names are ours.
namespace DUMMY_NAMESPACE
{
    S32 DUMMY_PREFIXSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void DUMMY_PREFIXSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void DUMMY_PREFIXSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void DUMMY_PREFIXSysRegister();
} // namespace DUMMY_NAMESPACE

namespace Orient
{
    S32 OrientSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void OrientSysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void OrientSysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void OrientSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void OrientSysRegister();
} // namespace Orient

namespace Plasma
{
    S32 PlasmaSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void PlasmaSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void PlasmaSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void PlasmaSysRegister();
} // namespace Plasma

namespace PopGas
{
    S32 PopGasSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void PopGasSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void PopGasSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void PopGasSysRegister();
} // namespace PopGas

namespace PuffRing
{
    S32 PuffSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void PuffSysInit_Glow(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void PuffSysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void PuffSysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void PuffSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void PuffSysRegister();
} // namespace PuffRing

namespace Exhaust
{
    S32 ExhaustSysUpdate_noRot(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    S32 ExhaustSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void ExhaustSysInit_Glow(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void ExhaustSysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void ExhaustSysInit_noRot_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void ExhaustSysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void ExhaustSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void ExhaustSysRegister();
} // namespace Exhaust

namespace FogBreath
{
    S32 FogSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void FogSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void FogSysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void FogSysRegister();
} // namespace FogBreath

namespace Sparky
{
    S32 SparkySysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context);
    void SparkySysInit_Glow(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void SparkySysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void SparkySysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs);
    void SparkySysTweaks(NPARMgmt* mgmt, const char* prefix_root);
    void SparkySysRegister();
} // namespace Sparky

void zNMEParFX_Startup();
void zNMEParFX_RegisterTypes();

NPARMgmt* zNMEParFX_PartyInvite(en_npartyp npartype, void** userData, S32 skipWarn);

#endif
