#include "zVent.h"

#include <string.h>

#include "xEvent.h"
#include "xGrid.h"
#include "xMath3.h"
#include "xQuickCull.h"
#include "xScene.h"
#include "xEntBoulder.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zGrid.h"

void zVent::load(const asset_type& a)
{
    memset(this, 0, sizeof(zVent));
    xBaseInit(this, &a);

    baseType = eBaseTypeVent;
    asset = &a;
    eventFunc = cbDispatch;

    if (linkCount)
    {
        link = (xLinkAsset*)(&a + 1);
    }

    xMat3x3Euler(&mat, &asset->dir);
    mat.pos = asset->loc;

    if (asset->ventType)
    {
        ventType = (zVentType*)zSceneFindObject(asset->ventType);
    }

    state = eVentStateIdle;
    timeRemaining = asset->idleTime;

    initVelocity[eVentStateIdle] = ventType->idleEmitter->tasset->vel;
    initVelocity[eVentStateWarn] = ventType->warnEmitter->tasset->vel;
    initVelocity[eVentStateDamage] = ventType->damageEmitter->tasset->vel;

    idleEmit = *ventType->idleEmitter;
    warnEmit = *ventType->warnEmitter;
    damageEmit = *ventType->damageEmitter;

    snd = (iSndHandle)-1;
}

struct cbVentHitsBoulder
{
    xBase* owner;
    const xBox& box;
    const xMat4x3& mat;
    F32 boulderInfluence;
    U8 fDestroyBoulders;

    cbVentHitsBoulder(xBase* owner, const xBox& box, const xMat4x3& mat, F32 boulderInfluence,
                      U8 fDestroyBoulders)
        : owner(owner), box(box), mat(mat), boulderInfluence(boulderInfluence),
          fDestroyBoulders(fDestroyBoulders)
    {
    }

    bool operator()(xEnt& ent)
    {
        if (ent.baseType != eBaseTypeBoulder)
        {
            return true;
        }

        if ((ent.flags & 0x85) != 0x1)
        {
            return true;
        }

        if (!ent.model)
        {
            return true;
        }

        if (!xOBBHitsBound(box, mat, ent.bound))
        {
            return true;
        }

        if (gCurrentPlayer == eCurrentPlayerSpongeBob && gSpongeBall &&
            boulderVehicle == (xEntBoulder*)&ent)
        {
            fDestroyBoulders = false;
        }

        if (fDestroyBoulders)
        {
            zEntEvent(owner, 0, &ent, eEventHit, NULL, NULL, 0, FE_NO);
        }
        else
        {
            xVec3 force = { 0.0f, 0.0f, 0.0f };
            force.y = 1000.0f * boulderInfluence;
            xMat3x3RMulVec(&force, &mat, &force);
            ((xEntBoulder&)ent).force += force;
        }

        return true;
    }
};

void zVent::update(F32 dt)
{
    xParEmitter* pEmit;
    xParEmitterCustomSettings ventInfo;
    xMat4x3* pMat;
    xVec3 ventVel;
    U8 fIsSpongeball;
    U8 fDamageSpongeball;
    xCollis collide;
    xBound bnd;
    xBox box;

    if (!ventType)
    {
        return;
    }

    if (state == eVentStateOff)
    {
        return;
    }

    if (asset->flags & 0x2)
    {
        timeRemaining -= dt;
        if (timeRemaining < 0.0f)
        {
            switch (state)
            {
            case eVentStateIdle:
                changeState(eVentStateWarn);
                timeRemaining = asset->warnTime;
                break;
            case eVentStateWarn:
                changeState(eVentStateDamage);
                timeRemaining = asset->damageTime;
                break;
            case eVentStateDamage:
                changeState(eVentStateIdle);
                timeRemaining = asset->idleTime;
                break;
            }
        }
    }

    if (state == eVentStateIdle)
    {
        pEmit = &idleEmit;
    }
    else if (state == eVentStateWarn)
    {
        pEmit = &warnEmit;
    }
    else
    {
        pEmit = &damageEmit;
    }

    memset(&ventInfo, 0, sizeof(xParEmitterCustomSettings));
    ventInfo.custom_flags = 0x300;
    pMat = &mat;
    xMat3x3RMulVec(&ventVel, pMat, &initVelocity[state]);
    ventInfo.pos = asset->loc;
    ventInfo.vel = ventVel;
    ventInfo.last_emit_position = pEmit->last_emit_position;
    ventInfo.last_emit_velocity = pEmit->last_emit_velocity;
    xParEmitterEmitCustom(pEmit, dt, &ventInfo);

    if (state != eVentStateDamage)
    {
        return;
    }

    fIsSpongeball = (gCurrentPlayer == eCurrentPlayerSpongeBob && gSpongeBall);
    fDamageSpongeball = (asset->flags >> 2) & 0x1;
    if (!fIsSpongeball || fDamageSpongeball)
    {
        memset(&collide, 0, sizeof(xCollis));

        if (checkHitPlayer(xglobals->___player_ent_dont_use_directly, &collide))
        {
            zEntPlayer_Damage(this, 1);
        }
    }

    if (asset->boulderInfluence > 0.0f)
    {
        getBound(bnd);

        box.upper = asset->upperRight;
        box.lower.x = asset->lowerLeftX;
        box.lower.y = asset->lowerLeftY;
        box.lower.z = asset->lowerLeftZ;

        cbVentHitsBoulder cb(this, box, mat, asset->boulderInfluence, asset->flags & 0x1);

        xGridCheckBound(colls_grid, bnd, bnd.qcd, cb);
    }
}

// Dead-stripped placeholder, not recovered code: retail's .rodata keeps an unreferenced zero xVec3
// template after cbVentHitsBoulder's. In Ratatouille it belongs to the stripped distSquaredToCam's
// local 'delt'; the body is unknown, so only that local is restored.
static void __deadstripped_zVent()
{
    xVec3 delt = {};
}

inline void zVent::changeState(eVentState newState)
{
    switch (newState)
    {
    case eVentStateIdle:
        if (snd != (iSndHandle)-1)
        {
            xSndMgrStop(snd);
            snd = (iSndHandle)-1;
        }

        snd = (idleSndID == (iSndGroupHandle)-1) ?
                  (iSndHandle)-1 :
                  xSndMgrPlay(idleSndID, 0, &asset->loc, NULL, NULL, NULL, NULL);

        state = eVentStateIdle;
        break;
    case eVentStateWarn:
        if (snd != (iSndHandle)-1)
        {
            xSndMgrStop(snd);
            snd = (iSndHandle)-1;
        }

        snd = (warningSndID == (iSndGroupHandle)-1) ?
                  (iSndHandle)-1 :
                  xSndMgrPlay(warningSndID, 0, &asset->loc, NULL, NULL, NULL, NULL);

        state = eVentStateWarn;
        break;
    case eVentStateDamage:
        if (snd != (iSndHandle)-1)
        {
            xSndMgrStop(snd);
            snd = (iSndHandle)-1;
        }

        snd = (damageSndID == (iSndGroupHandle)-1) ?
                  (iSndHandle)-1 :
                  xSndMgrPlay(damageSndID, 0, &asset->loc, NULL, NULL, NULL, NULL);

        state = eVentStateDamage;
        break;
    case eVentStateOff:
        if (snd != (iSndHandle)-1)
        {
            xSndMgrStop(snd);
            snd = (iSndHandle)-1;
        }

        state = eVentStateOff;
        break;
    }
}

// No GC symbol: inlined into zSceneRenderPreFX. Empty on GC, as in Ratatouille.
void zVent::render()
{
}

void zVent::load(xBase& data, xDynAsset& asset, size_t)
{
    ((zVent&)data).load((const asset_type&)asset);
}

// No GC symbol: inlined into zSceneSetup.
void zVent::setup()
{
    state = eVentStateIdle;
    timeRemaining = asset->idleTime;
    idleSndID = xSndMgrGetSoundGroup(ventType->asset->idleSound);
    damageSndID = xSndMgrGetSoundGroup(ventType->asset->damageSound);
    warningSndID = xSndMgrGetSoundGroup(ventType->asset->warnSound);
}

// No GC symbol: inlined into zSceneSetup.
void zVent::setupAll()
{
    zScene& scene = *globals.sceneCur;
    zVent* it = (zVent*)scene.baseList[eBaseTypeVent];
    zVent* end = it + scene.baseCount[eBaseTypeVent];

    for (; it != end; ++it)
    {
        it->setup();
    }
}

// No GC symbol: inlined into zSceneUpdate.
void zVent::updateAll(F32 dt)
{
    zScene& scene = *globals.sceneCur;
    zVent* it = (zVent*)scene.baseList[eBaseTypeVent];
    zVent* end = it + scene.baseCount[eBaseTypeVent];

    for (; it != end; ++it)
    {
        it->update(dt);
    }
}

// No GC symbol: inlined into zSceneRenderPreFX.
void zVent::renderAll()
{
    zScene& scene = *globals.sceneCur;
    zVent* it = (zVent*)scene.baseList[eBaseTypeVent];
    zVent* end = it + scene.baseCount[eBaseTypeVent];

    for (; it != end; ++it)
    {
        it->render();
    }
}

void zVent::cbDispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32)
{
    zVent& e = *(zVent*)to;

    switch (event)
    {
    case eEventReset:
    case eEventSceneEnd:
        e.state = eVentStateIdle;
        e.timeRemaining = e.asset->idleTime;
        break;
    case eEventVentSetStateIdle:
        e.changeState(eVentStateIdle);
        break;
    case eEventVentSetStateWarn:
        e.changeState(eVentStateWarn);
        break;
    case eEventVentSetStateDamage:
        e.changeState(eVentStateDamage);
        break;
    case eEventVentSetStateOff:
        e.changeState(eVentStateOff);
        break;
    }
}
