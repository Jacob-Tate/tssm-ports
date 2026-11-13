#ifndef ZVENT_H
#define ZVENT_H

#include "xBase.h"
#include "xBound.h"
#include "xCollide.h"
#include "xDynAsset.h"
#include "xEnt.h"
#include "xMath3.h"
#include "xParEmitter.h"
#include "xSnd.h"
#include "zVentType.h"

enum eVentState
{
    eVentStateIdle,
    eVentStateWarn,
    eVentStateDamage,
    eVentStateOff,
    eVentStateMax
};

struct zVent : xBase
{
    struct asset_type : xDynAsset
    {
        U32 ventType;
        xVec3 loc;
        xVec3 dir;
        F32 lowerLeftX;
        F32 lowerLeftY;
        F32 lowerLeftZ;
        xVec3 upperRight;
        F32 boulderInfluence;
        U32 flags;
        F32 idleTime;
        F32 warnTime;
        F32 damageTime;
    };

    const asset_type* asset;
    xMat4x3 mat;
    eVentState state;
    F32 timeRemaining;
    iSndHandle snd;
    iSndGroupHandle idleSndID;
    iSndGroupHandle warningSndID;
    iSndGroupHandle damageSndID;
    zVentType* ventType;
    xVec3 initVelocity[4];
    xParEmitter idleEmit;
    xParEmitter warnEmit;
    xParEmitter damageEmit;

    static void cbDispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32);
    static void load(xBase& data, xDynAsset& asset, size_t);
    void changeState(eVentState newState);
    void render();
    static void renderAll();
    static void setupAll();
    void setup();
    static void updateAll(F32 dt);
    void update(F32 dt);
    void load(const asset_type& a);

    // No GC symbol (always inlined into update); the name is ours. Builds the vent's damage
    // volume: the asset's box, oriented and placed by mat.
    void getBound(xBound& bnd)
    {
        const asset_type* a = asset;
        bnd.type = XBOUND_TYPE_OBB;
        // The asset stores the lower corner as three floats, but retail copies it as one xVec3.
        bnd.box.box.lower = *(const xVec3*)&a->lowerLeftX;
        bnd.box.box.upper = a->upperRight;
        bnd.box.center.x = 0.5f * (a->upperRight.x + a->lowerLeftX);
        bnd.box.center.y = 0.5f * (a->upperRight.y + a->lowerLeftY);
        bnd.box.center.z = 0.5f * (a->upperRight.z + a->lowerLeftZ);
        bnd.mat = &mat;
        xMat4x3Toworld(&bnd.box.center, bnd.mat, &bnd.box.center);
        xQuickCullForBound(&bnd.qcd, &bnd);
    }

    // No GC symbol (always inlined into update).
    S32 checkHitPlayer(xEnt* tgt, xCollis* collide)
    {
        xBound bnd;
        getBound(bnd);

        collide->optr = tgt;
        collide->oid = tgt->id;
        collide->flags = 0x1f00;
        xBoundHitsBound(&bnd, &tgt->bound, collide);

        return collide->flags & 0x1;
    }
};

#endif
