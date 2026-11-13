#ifndef XENTBOULDER_H
#define XENTBOULDER_H

#include "xEnt.h"
#include "xDynAsset.h"
#include "xShadowSimple.h"
#include "iSnd.h"

struct xEntBoulderAsset
{
    F32 gravity;
    F32 mass;
    F32 bounce;
    F32 friction;
    F32 maxVel;
    F32 maxAngVel;
    F32 stickiness;
    F32 bounceDamp;
    U32 flags;
    F32 killtimer;
    U32 hitpoints;
    union
    {
        U32 uSoundGroupHash;
        iSndGroupHandle soundID;
    };
    F32 minSoundVel;
    F32 maxSoundVel;
    F32 fSphereRadius;
    U8 uPad0;
    U8 uPad1;
    U8 uPad2;
    U8 uBoneIndex;
};

struct xEntBoulder : xEnt
{
    xEntBoulderAsset* basset;
    xShadowSimpleCache simpShadow_embedded;
    xEntShadow entShadow_embedded;
    xVec3 localCenter;
    xVec3 vel;
    xVec3 rotVec;
    xVec3 force;
    xVec3 instForce;
    F32 angVel;
    F32 timeToLive;
    S32 hitpoints;
    F32 fOOSphRadius;
    iSndHandle currSndFX;
    U32 lastRolling;
    U32 rollingID;
    U8 collis_chk;
    U8 collis_pen;
    U8 pad1[2];
};

struct xBoulderGeneratorAsset : xDynAsset
{
    U32 object;
    xVec3 offset;
    F32 offsetRand;
    xVec3 initvel;
    F32 velAngleRand;
    F32 velMagRand;
    xVec3 rotation;
    U32 flags;
};

struct xBoulderGenerator : xBase
{
    xBoulderGeneratorAsset* bgasset;
    S32 numBoulders;
    S32 nextBoulder;
    xEntBoulder** boulderList;
    S32* boulderAges;
    U32 isMarker;
    void* objectPtr;
    F32 lengthOfInitVel;
    F32 angvel;
    xVec3 initaxis;
    xVec3 perp1;
    xVec3 perp2;
};

void xEntBoulder_Init(void* ent, void* asset);
void xEntBoulder_Init(xEntBoulder* ent, xEntAsset* asset);
void xEntBoulder_BubbleBowl(F32 multiplier);
void xEntBoulder_Setup(xEntBoulder* ent);
void xEntBoulder_Reset(xEntBoulder* ent, xScene* scene);
void xEntBoulder_Update(xEntBoulder* ent, xScene* sc, F32 dt);
void xEntBoulder_RealBUpdate(xEnt* ent, xVec3* pos);
void xEntBoulder_Kill(xEntBoulder* ent);
void xEntBoulder_AddForce(xEntBoulder* ent, xVec3* force);
void xEntBoulder_AddInstantForce(xEntBoulder* ent, xVec3* force);
void xEntBoulder_Render(xEnt* ent);
void xEntBoulder_ApplyForces(xEntCollis* collis);
void xBoulderGenerator_Init(xBase& data, xDynAsset& asset, size_t);
void xBoulderGenerator_Init(xBoulderGenerator* bg, xBoulderGeneratorAsset* asset);
void xBoulderGenerator_Init(xBase& data, xDynAsset& asset);

#endif
