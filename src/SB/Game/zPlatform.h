#ifndef ZPLATFORM_H
#define ZPLATFORM_H

#include "zEnt.h"
#include "xEntMotion.h"
#include "xEntDrive.h"
#include "zDestructible.h"

struct xPlatformERData
{
    S32 nodata;
};

struct xPlatformOrbitData
{
    S32 nodata;
};

struct xPlatformSplineData
{
    S32 nodata;
};

struct xPlatformMPData
{
    S32 nodata;
};

struct xPlatformMechData
{
    S32 nodata;
};

struct xPlatformPenData
{
    S32 nodata;
};

struct xPlatformConvBeltData
{
    F32 speed;
};

struct xPlatformFallingData
{
    F32 speed;
    U32 bustModelID;
};

struct xPlatformFRData
{
    F32 fspeed;
    F32 rspeed;
    F32 ret_delay;
    F32 post_ret_delay;
};

struct xPlatformBreakawayData
{
    F32 warningTime;
    F32 collapseIdleTime;
    U32 breakflags;
    F32 collisionOffTime;
};

struct xPlatformSpringboardData
{
    F32 jmph[3];
    F32 jmpbounce;
    U32 animID[3];
    xVec3 jmpdir;
    U32 springflags;
};

struct xPlatformTeeterData
{
    F32 itilt;
    F32 maxtilt;
    F32 invmass;
};

struct xPlatformPaddleData
{
    S32 startOrient;
    S32 countOrient;
    F32 orientLoop;
    F32 orient[6];
    U32 paddleFlags;
    F32 rotateSpeed;
    F32 accelTime;
    F32 decelTime;
    F32 hubRadius;
};

struct xPlatformFMData
{
    S32 nothingyet;
};

struct xPlatformAsset
{
    U8 type;
    U8 pad;
    U16 flags;
    union
    {
        xPlatformERData er;
        xPlatformOrbitData orb;
        xPlatformSplineData spl;
        xPlatformMPData mp;
        xPlatformMechData mech;
        xPlatformPenData pen;
        xPlatformConvBeltData cb;
        xPlatformFallingData fall;
        xPlatformFRData fr;
        xPlatformBreakawayData ba;
        xPlatformSpringboardData sb;
        xPlatformTeeterData teet;
        xPlatformPaddleData paddle;
        xPlatformFMData fm;
    };
};

struct zPlatFMRunTime
{
    U32 flags;
    F32 tmrs[13];
    F32 ttms[13];
    F32 atms[13];
    F32 dtms[13];
    F32 vms[13];
    F32 dss[13];
    F32 total_change[13];
};

struct zPlatform : zEnt
{
    xPlatformAsset* plat_asset; // 0xDC
    xEntMotion motion; // 0xE0
    U16 state; // 0x1B0
    U16 plat_flags; // 0x1B2
    F32 tmr; // 0x1B4
    S32 ctr; // 0x1B8
    xMovePoint* src; // 0x1BC
    xModelInstance* am; // 0x1C0
    xModelInstance* bm; // 0x1C4
    S32 moving; // 0x1C8
    xEntDrive drv; // 0x1D0
    zPlatFMRunTime* fmrt; // 0x290
    zDestructible* destructible; // 0x294
    F32 elapsedTime; // 0x298
    xEntOpacity opacity; // 0x29C
    iSndHandle sndID; // 0x2A4
};

#define ZPLATFORM_SUBTYPE_PLATFORM 0
#define ZPLATFORM_SUBTYPE_MECH 4
#define ZPLATFORM_SUBTYPE_PEN 5
#define ZPLATFORM_SUBTYPE_CONVBELT 6
#define ZPLATFORM_SUBTYPE_FALLING 7
#define ZPLATFORM_SUBTYPE_FR 8
#define ZPLATFORM_SUBTYPE_BREAKAWAY 9
#define ZPLATFORM_SUBTYPE_SPRINGBOARD 10
#define ZPLATFORM_SUBTYPE_TEETER 11
#define ZPLATFORM_SUBTYPE_PADDLE 12
#define ZPLATFORM_SUBTYPE_FM 13

#define ZPLATFORM_STATE_INIT 0x0
#define ZPLATFORM_STATE_UNK1 0x3
#define ZPLATFORM_STATE_UNK2 0x1
#define ZPLATFORM_STATE_UNK3 0x2
#define ZPLATFORM_STATE_UNK4 0x4

void zPlatform_Init(void* plat, void* asset);
void zPlatform_Init(zPlatform* plat, xEntAsset* asset);
void zPlatform_Setup(zPlatform* plat, xScene* sc);
void zPlatform_Save(zPlatform* ent, xSerial* s);
void zPlatform_Load(zPlatform* ent, xSerial* s);
void zPlatform_Reset(zPlatform* plat, xScene* sc);
void zPlatform_PaddleStartRotate(struct xEnt* entplat, S32 direction, S32 stutter);
void zPlatform_Update(xEnt* ent, xScene* sc, float dt);
void zPlatform_Shake(zPlatform* plat, F32 _unused, F32 ampl, F32 freq);
void zPlatform_Mount(zPlatform* plat);
void zPlatform_Dismount(zPlatform* plat);
U8 zPlatform_UsePartialCameraCollision(zPlatform* ent);
void zPlatform_Move(xEnt* entplat, xScene* s, F32 dt, xEntFrame* frame);
U32 zPlatform_PaddleCollide(xCollis* coll, const xVec3* hitsource, const xVec3* hitvel,
                            U32 worldSpaceNorm);
void zPlatformEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* base3, U32);

#endif
