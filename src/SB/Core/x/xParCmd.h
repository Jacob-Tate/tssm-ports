#ifndef XPARCMD_H
#define XPARCMD_H

#include "xParGroup.h"

#define XPARCMD_TYPE_MOVE 0
#define XPARCMD_TYPE_MOVERANDOM 1
#define XPARCMD_TYPE_ACCELERATE 2
#define XPARCMD_TYPE_VELOCITYAPPLY 3
#define XPARCMD_TYPE_JET 4
#define XPARCMD_TYPE_KILLOLD 5
#define XPARCMD_TYPE_KILLSLOW 6
#define XPARCMD_TYPE_FOLLOW 7
#define XPARCMD_TYPE_ORBITPOINT 8
#define XPARCMD_TYPE_ORBITLINE 9
#define XPARCMD_TYPE_MOVERANDOMPAR 10
#define XPARCMD_TYPE_SCALE3RDPOLYREG 11
#define XPARCMD_TYPE_TEX 12
#define XPARCMD_TYPE_TEXANIM 13
#define XPARCMD_TYPE_PLAYERCOLLISION 14
#define XPARCMD_TYPE_RANDOMVELOCITYPAR 15
#define XPARCMD_TYPE_CUSTOM 16
#define XPARCMD_TYPE_KILLDISTANCE 17
#define XPARCMD_TYPE_AGE 18
#define XPARCMD_TYPE_ALPHA3RDPOLYREG 19
#define XPARCMD_TYPE_APPLYWIND 20
#define XPARCMD_TYPE_ROTPAR 21
#define XPARCMD_TYPE_APPLYCAMMAT 22
#define XPARCMD_TYPE_ROTATEAROUND 23
#define XPARCMD_TYPE_SMOKEALPHA 24
#define XPARCMD_TYPE_SCALE 25
#define XPARCMD_TYPE_CLIPVOLUMES 26
#define XPARCMD_TYPE_ANIMALMAGENTISM 27
#define XPARCMD_TYPE_DAMAGEPLAYER 28
#define XPARCMD_TYPE_COLLIDEFALL 29
#define XPARCMD_TYPE_SHAPER 30
#define XPARCMD_TYPE_ALPHAINOUT 31
#define XPARCMD_TYPE_SIZEINOUT 32
#define XPARCMD_TYPE_DAMPENSPEED 33
#define XPARCMD_TYPE_COLLIDEFALLSTICKY 34
#define XPARCMD_TYPE_COUNT 35

struct xParCmdAsset
{
    U32 type;
    U8 enabled;
    U8 mode;
    U8 pad[2];
};

struct xParCmd
{
    U32 flag;
    xParCmdAsset* tasset;
};

struct xParGroup;

typedef void (*xParCmdUpdateFunc)(xParCmd* c, xParGroup* ps, F32 dt);

void xParCmdInit();
void xParCmdRegister(U32 parType, U32 size, xParCmdUpdateFunc func);
U32 xParCmdGetSize(U32 parType);
xParCmdUpdateFunc xParCmdGetUpdateFunc(U32 parType);

// XPARCMD_TYPE_MOVE
void xParCmdMove_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdMove : xParCmdAsset
{
    xVec3 dir;
};

// XPARCMD_TYPE_MOVERANDOM
void xParCmdMoveRandom_Update(xParCmd* c, xParGroup* ps, F32 dt);

// XPARCMD_TYPE_ACCELERATE
void xParCmdAccelerate_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdAccelerate : xParCmdAsset
{
    xVec3 acc;
};

// XPARCMD_TYPE_VELOCITYAPPLY
void xParCmdVelocityApply_Update(xParCmd* c, xParGroup* ps, F32 dt);

// XPARCMD_TYPE_JET
void xParCmdJet_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdJet : xParCmdAsset
{
    xVec3 center;
    xVec3 acc;
    F32 gravity;
    F32 epsilon;
    F32 radiusSqr;
};

// XPARCMD_TYPE_KILLOLD
// Not in the PS2 DWARF; from Ratatouille's GC DWARF. Type 5 is the only type number left for it
// (every other xParCmdAny member has its own), and the registered size matches.
struct xParCmdKillOld : xParCmdAsset
{
    F32 ageMax;
};

// XPARCMD_TYPE_KILLSLOW
void xParCmdKillSlow_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdKillSlow : xParCmdAsset
{
    F32 speedLimitSqr;
    U32 kill_less_than;
};

// XPARCMD_TYPE_FOLLOW
void xParCmdFollow_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdFollow : xParCmdAsset
{
    F32 gravity;
    F32 epsilon;
};

// XPARCMD_TYPE_ORBITPOINT
void xParCmdOrbitPoint_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdOrbitPoint : xParCmdAsset
{
    xVec3 center;
    F32 gravity;
    F32 epsilon;
    F32 maxRadiusSqr;
};

// XPARCMD_TYPE_ORBITLINE
void xParCmdOrbitLine_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdOrbitLine : xParCmdAsset
{
    xVec3 p;
    xVec3 axis;
    F32 gravity;
    F32 epsilon;
    F32 maxRadiusSqr;
};

// XPARCMD_TYPE_MOVERANDOMPAR
void xParCmdMoveRandomPar_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdMoveRandomPar : xParCmdAsset
{
    xVec3 dim;
};

// XPARCMD_TYPE_SCALE3RDPOLYREG
void xParCmdScale3rdPolyReg_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdScale3rdPolyReg : xParCmdAsset // Ratatouille GC DWARF
{
    F32 constant;
    F32 a;
    F32 a2;
    F32 a3;
};

// XPARCMD_TYPE_TEX
void xParCmdTex_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdTex : xParCmdAsset
{
    F32 x1;
    F32 y1;
    F32 x2;
    F32 y2;
    U8 birthMode;
    U8 rows;
    U8 cols;
    U8 unit_count;
    F32 unit_width;
    F32 unit_height;
};

// XPARCMD_TYPE_TEXANIM
void xParCmdTexAnim_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdTexAnim : xParCmdAsset
{
    U8 anim_mode;
    U8 anim_wrap_mode;
    U8 pad_anim;
    U8 throttle_spd_less_than;
    F32 throttle_spd_sqr;
    F32 throttle_time;
    F32 throttle_time_elapsed;
};

// XPARCMD_TYPE_PLAYERCOLLISION
void xParCmdPlayerCollision_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdPlayerCollision : xParCmdAsset // Ratatouille GC DWARF
{
    F32 min;
    F32 max;
};

// XPARCMD_TYPE_RANDOMVELOCITYPAR
void xParCmdRandomVelocityPar_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdRandomVelocityPar : xParCmdAsset
{
    F32 x;
    F32 y;
    F32 z;
};

// XPARCMD_TYPE_CUSTOM
void xParCmdCustom_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdCustom : xParCmdAsset // Ratatouille GC DWARF
{
    U32 user_id;
    F32 user_val[4];
};

void xParCmdCustom_Grass_Update(xParCmd* c, xParGroup* ps, F32 dt);

// XPARCMD_TYPE_KILLDISTANCE
void xParCmdKillDistance_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdKillDistance : xParCmdAsset
{
    F32 dSqr;
    U32 kill_greater_than;
};

// XPARCMD_TYPE_AGE
void xParCmdAge_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdAge : xParCmdAsset // Ratatouille GC DWARF
{
    F32 age_rate;
};

// XPARCMD_TYPE_ALPHA3RDPOLYREG
void xParCmdAlpha3rdPolyReg_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdAlpha3rdPolyReg : xParCmdAsset // Ratatouille GC DWARF
{
    F32 constant;
    F32 a;
    F32 a2;
    F32 a3;
};

// XPARCMD_TYPE_APPLYWIND
void xParCmdApplyWind_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdApplyWind : xParCmdAsset // Ratatouille GC DWARF
{
    F32 strength;
};

// XPARCMD_TYPE_ROTPAR
void xParCmdRotPar_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdRotPar : xParCmdAsset
{
    xVec3 min;
    xVec3 max;
};

// XPARCMD_TYPE_APPLYCAMMAT
void xParCmdApplyCamMat_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdApplyCamMat : xParCmdAsset
{
    xVec3 apply;
};

// XPARCMD_TYPE_ROTATEAROUND
void xParCmdRotateAround_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdRotateAround : xParCmdAsset
{
    xVec3 pos;
    F32 unused1;
    F32 radius_growth;
    F32 yaw;
};

// XPARCMD_TYPE_SMOKEALPHA
void xParCmdSmokeAlpha_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdSmokeAlpha : xParCmdAsset // Ratatouille GC DWARF
{
    S32 type;
};

// XPARCMD_TYPE_SCALE
void xParCmdScale_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdScale : xParCmdAsset // Ratatouille GC DWARF
{
    S32 type;
};

// XPARCMD_TYPE_CLIPVOLUMES
void xParCmdClipVolumes_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdClipVolumes : xParCmdAsset // Ratatouille GC DWARF
{
    S32 unused;
};

// XPARCMD_TYPE_ANIMALMAGENTISM
void xParCmdAnimalMagentism_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdAnimalMagentism : xParCmdAsset // Ratatouille GC DWARF (HI's spelling)
{
    F32 magnetism;
};

// XPARCMD_TYPE_DAMAGEPLAYER
void xParCmdDamagePlayer_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdDamagePlayer : xParCmdAsset
{
    S32 damage;
    S32 granular;
};

// XPARCMD_TYPE_COLLIDEFALL
void xParCmdCollideFall_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdCollideFall : xParCmdAsset
{
    F32 y;
    F32 bounce;
};

// XPARCMD_TYPE_SHAPER
void xParCmd_Shaper_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdShaperData : xParCmdAsset
{
    F32 custAlpha[4];
    F32 custSize[4];
    F32 dampSpeed;
    F32 gravity;
};

// XPARCMD_TYPE_ALPHAINOUT
void xParCmd_AlphaInOut_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdAlphaInOutData : xParCmdAsset
{
    F32 custAlpha[4];
};

// XPARCMD_TYPE_SIZEINOUT
void xParCmd_SizeInOut_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdSizeInOutData : xParCmdAsset
{
    F32 custSize[4];
};

// XPARCMD_TYPE_DAMPENSPEED
void xParCmd_DampenSpeed_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdDampenData : xParCmdAsset
{
    F32 dampSpeed;
};

// XPARCMD_TYPE_COLLIDEFALLSTICKY
void xParCmdCollideFallSticky_Update(xParCmd* c, xParGroup* ps, F32 dt);

struct xParCmdCollideFallSticky : xParCmdCollideFall
{
    F32 sticky;
};

#endif
