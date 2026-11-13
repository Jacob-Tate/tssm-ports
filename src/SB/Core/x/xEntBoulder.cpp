#include "xEntBoulder.h"

#include "xMarkerAsset.h"
#include "xRumbleEmitter.h"
#include "xShadow.h"
#include "xSnd.h"

#include "zEntButton.h"
#include "zEntPlayer.h"
#include "zFX.h"
#include "zGoo.h"
#include "zMovePoint.h"
#include "zNMECommon.h"
#include "zPlatform.h"
#include "zReactiveAnimation.h"
#include "zShrapnel.h"
#include "zSurface.h"
#include "xRand.h"

static U32 sBubbleStreakID = 0xdead;
static xEntCollis sBoulderCollis;
extern xEntBoulder* boulderVehicle;
static void RecurseChild(xBase* child, xEntBoulder** boulList, S32& currBoul);

void xEntBoulder_AddForce(xEntBoulder* ent, xVec3* force);
void xEntBoulder_Update(xEntBoulder* ent, xScene* sc, F32 dt);
S32 xEntBoulder_KilledBySurface(xEntBoulder* ent, xScene* sc, F32 dt);
void xBoulderGenerator_EventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                               xBase* toParamWidget, U32 toParamWidgetID);
void xEntBoulderEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, U32 toParamWidgetID);
void xEntBoulder_BUpdate(xEnt*, xVec3*);
void xBoulderGenerator_GenBoulder(xBoulderGenerator* bg);

void xEntBoulder_Render(xEnt* ent)
{
    xModelInstance* model = ent->model;
    S32 shadowResult;
    xVec3 shadVec;

    if ((model != NULL) && xEntIsVisible(ent))
    {
        switch (model->Flags & 0x400)
        {
        case 0:
            if (ent->flags & 0x40)
            {
                xModelRender(model);
            }
            else
            {
                shadVec.x = ent->frame->mat.pos.x;
                shadVec.y = ent->frame->mat.pos.y - 10.0f;
                shadVec.z = ent->frame->mat.pos.z;

                if (!xModelSphereCullPlusShadow(
                        iModelWorldBoundSphere(model->Data, (RwMatrixTag*)&ent->frame->mat, NULL),
                        &shadVec, &shadowResult))
                {
                    xModelRender(model);
                }

                if (!shadowResult)
                {
                    if (ent->flags & 0x10)
                    {
                        xShadowManager_Add(ent);
                    }
                    else if (ent == boulderVehicle)
                    {
                        if (ent->flags & 0x8)
                        {
                            F32 radius = ent->model->Data->boundingSphere.radius;
                            if (radius > 0.75f)
                            {
                                radius = 0.75f;
                            }
                            xShadowSimple_Add(ent->simpShadow, ent, 2.0f * radius, 1.0f);
                        }
                    }
                    else
                    {
                        F32 radius = ent->model->Data->boundingSphere.radius;
                        if (radius > 0.75f)
                        {
                            radius = 0.75f;
                        }
                        xShadowSimple_Add(ent->simpShadow, ent, 2.0f * radius, 1.0f);
                    }
                }
            }
            break;
        }
    }
}

void xEntBoulder_Init(void* ent, void* asset)
{
    xEntBoulder_Init((xEntBoulder*)ent, (xEntAsset*)asset);
}

void xEntBoulder_Init(xEntBoulder* ent, xEntAsset* asset)
{
    xEntBoulderAsset* basset;

    xEntInit(ent, asset);
    ent->collType = XENT_COLLTYPE_DYN;
    ent->collLev = 4;
    ent->bound.type = XBOUND_TYPE_SPHERE;
    ent->moreFlags |= 0x10;
    zEntParseModelInfo(ent, asset->modelInfoID);

    ent->entShadow = &ent->entShadow_embedded;
    ent->simpShadow = &ent->simpShadow_embedded;
    xShadowSimple_CacheInit(ent->simpShadow, ent, 0x50);
    xVec3Copy(&ent->entShadow->pos, &asset->pos);
    xVec3Init(&ent->entShadow->vec, 0.0f, 1.0f, 0.0f);
    ent->entShadow->shadowModel = NULL;

    ent->frame = (xEntFrame*)xMemAlloc(gActiveHeap, sizeof(xEntFrame), 0);
    memset(ent->frame, 0, sizeof(xEntFrame));

    basset = (xEntBoulderAsset*)&asset[1];
    ent->collis = NULL;
    ent->basset = basset;
    ent->eventFunc = xEntBoulderEventCB;
    ent->update = (xEntUpdateCallback)xEntBoulder_Update;
    ent->bupdate = xEntBoulder_BUpdate;
    ent->render = xEntBoulder_Render;

    if (ent->linkCount != 0)
    {
        ent->link = (xLinkAsset*)((xEntBoulderAsset*)&ent->asset[1] + 1);
    }
    else
    {
        ent->link = NULL;
    }

    xVec3Copy(&ent->bound.sph.center, (xVec3*)&ent->model->Data->boundingSphere.center);
    ent->bound.sph.r = ent->model->Data->boundingSphere.radius;
    ent->fOOSphRadius = 1.0f / ent->bound.sph.r;
    if ((ent->basset->flags & 0x400) && ent->basset->fSphereRadius > 0.0f)
    {
        ent->fOOSphRadius = 1.0f / ent->basset->fSphereRadius;
        ent->bound.sph.r = ent->basset->fSphereRadius;
    }
    xVec3Copy(&ent->localCenter, &ent->bound.sph.center);
    xVec3AddTo(&ent->bound.sph.center, &ent->frame->mat.pos);

    if (basset->mass <= 0.0f)
    {
        basset->mass = 1.0f;
    }

    xEntBoulder_Reset(ent, globals.sceneCur);

    if (ent->asset->id == xStrHash("BUBBLEBOWL"))
    {
        ent->update = NULL;
        xEntHide(ent);
        ent->chkby = XENT_COLLTYPE_NONE;
        ent->penby = XENT_COLLTYPE_NONE;
        ent->collis_chk = 0;
        ent->collis_pen = 0;
        globals.player.bubblebowl[0] = ent;
    }

    if (ent->asset->id == xStrHash("BUBBLEBOWL2"))
    {
        ent->update = NULL;
        xEntHide(ent);
        ent->chkby = XENT_COLLTYPE_NONE;
        ent->penby = XENT_COLLTYPE_NONE;
        ent->collis_chk = 0;
        ent->collis_pen = 0;
        globals.player.bubblebowl[1] = ent;
    }

    ent->currSndFX = (iSndHandle)-1;
    ent->lastRolling = 0;
    ent->rollingID = 0;
    ent->baseFlags |= 0x20;

    if (basset->uSoundGroupHash != 0)
    {
        basset->soundID = xSndMgrGetSoundGroup(basset->uSoundGroupHash);
    }
}

void xEntBoulder_ApplyForces(xEntCollis* collis)
{
    S32 i;
    xEntBoulder* boul;
    xEntDrive* drv = &globals.player.drv;

    for (i = collis->dyn_sidx; i < collis->dyn_eidx; i++)
    {
        if (collis->colls[i].optr != NULL &&
            ((xBase*)collis->colls[i].optr)->baseType == eBaseTypeBoulder)
        {
            xVec3 force;

            boul = (xEntBoulder*)collis->colls[i].optr;
            xVec3SMul(&force, &collis->colls[i].norm, -5.0f);
            if (boul == drv->old_driver || boul == drv->driver)
            {
                if (collis->colls[i].norm.y > 0.5f)
                {
                    force.x = -force.x;
                    force.z = -force.z;
                }
            }

            xEntBoulder_AddForce(boul, &force);

            if (boul->basset->flags & 0x2)
            {
                if (zEntPlayerIsInSlidingMode())
                {
                    zEntPlayer_Damage(boul, 1);
                    zEntEvent(NULL, 0, boul, eEventKill, NULL, NULL, 0, FE_NO);
                }
                else
                {
                    zEntPlayer_DamageNPCKnockBack(boul, 1, &boul->bound.sph.center,
                                                  ePlayerNMEDamageDefault);
                }
            }
        }
    }
}

void xEntBoulder_AddInstantForce(xEntBoulder* ent, xVec3* force)
{
    if (ent->update != 0)
    {
        xVec3AddTo(&ent->instForce, force);
    }
}

void xEntBoulder_AddForce(xEntBoulder* ent, xVec3* force)
{
    if (ent->update != 0)
    {
        xVec3AddTo(&ent->force, force);
    }
}

void xEntBoulder_BUpdate(xEnt*, xVec3*)
{
}

void xEntBoulder_RealBUpdate(xEnt* ent, xVec3* pos)
{
    xEntBoulder* boul = (xEntBoulder*)ent;
    xVec3 rotatedLC;

    xMat3x3RMulVec(&rotatedLC, &boul->frame->mat, &boul->localCenter);
    xVec3Add(&boul->bound.sph.center, pos, &rotatedLC);
    xBoundUpdate(&boul->bound);
    zGridUpdateEnt(boul);
}

void xEntBoulder_Update(xEntBoulder* ent, xScene* sc, F32 dt)
{
    S32 i;

    if (ent->timeToLive > 0.0f)
    {
        ent->timeToLive -= dt;
        if (ent->timeToLive <= 0.0f)
        {
            zEntEvent(ent, eEventKill);
            return;
        }
    }

    if (ent == zEntPlayerGetBBowl())
    {
        zEnt* plent = xglobals->___player_ent_dont_use_directly;
        F32 dx__ = plent->bound.sph.center.x - ent->frame->mat.pos.x;
        F32 dy__ = plent->bound.sph.center.y - ent->frame->mat.pos.y;
        F32 dz__ = plent->bound.sph.center.z - ent->frame->mat.pos.z;
        F32 distsqr = dx__ * dx__ + dy__ * dy__ + dz__ * dz__;

        if (distsqr > 3600.0f)
        {
            zEntEvent(ent, eEventKill);
            return;
        }
    }

    if (ent->collis == NULL)
    {
        ent->collis = &sBoulderCollis;
    }

    ent->collis->chk = ent->collis_chk;
    ent->collis->pen = ent->collis_pen;
    ent->collis->post = NULL;
    ent->collis->depenq = NULL;

    xVec3 rotatedLC;
    xMat3x3RMulVec(&rotatedLC, &ent->frame->mat, &ent->localCenter);
    xVec3Add(&ent->bound.sph.center, &ent->frame->mat.pos, &rotatedLC);

    xVec3 a;
    xVec3 b;
    if (sBubbleStreakID != 0xdead && zEntPlayerGetBBowl() == ent)
    {
        a = ent->bound.sph.center - ent->rotVec * ent->bound.sph.r;
        b = ent->bound.sph.center + ent->rotVec * ent->bound.sph.r;
        xFXStreakUpdate(sBubbleStreakID, &a, &b);
    }

    xVec3 tmp;
    tmp.x = tmp.y = tmp.z = ent->bound.sph.r * 4.0f;
    F32 fn = xVec3LengthFast(&ent->vel) * 0.1f;
    U32 n;
    if (fn >= 1.0f)
    {
        n = fn;
    }
    else
    {
        n = xurand() < fn;
    }

    if (n != 0)
    {
        zFX_SpawnBubbleTrail(&ent->bound.sph.center, n, NULL, &tmp);
    }

    xVec3 oldPos;
    xVec3Copy(&oldPos, &ent->bound.sph.center);

    ent->vel.y -= ent->basset->gravity * dt;
    xVec3AddScaled(&ent->vel, &ent->instForce, 1.0f / ent->basset->mass);
    xVec3Init(&ent->instForce, 0.0f, 0.0f, 0.0f);
    xVec3AddScaled(&ent->vel, &ent->force, dt / ent->basset->mass);
    xVec3Init(&ent->force, 0.0f, 0.0f, 0.0f);

    xVec3 velNorm;
    xVec3Normalize(&velNorm, &ent->vel);

    xVec3 sweptDisp;
    xVec3SMul(&sweptDisp, &ent->vel, dt);
    xVec3AddTo(&ent->bound.sph.center, &sweptDisp);
    xQuickCullForBound(&ent->bound.qcd, &ent->bound);
    zGridUpdateEnt(ent);

    zGooCollsBegin();
    xEntBeginUpdate(ent, sc, dt);

    ent->collType = XENT_COLLTYPE_PLYR;

    U8 definitelyBounce = 0;
    F32 sweptDistSqr = xVec3Length2(&sweptDisp);
    F32 penAllowed = 0.5f * ent->bound.sph.r;
    if (sweptDistSqr > penAllowed * penAllowed)
    {
        definitelyBounce = 1;

        xSweptSphere sws;
        xSweptSpherePrepare(&sws, &oldPos, &ent->bound.sph.center, ent->bound.sph.r);
        if (xSweptSphereToScene(&sws, globals.sceneCur, ent, XENT_COLLTYPE_PLYR, 0))
        {
            F32 pen = sws.dist - sws.curdist;
            if (pen > penAllowed)
            {
                F32 scaleBack = (penAllowed + sws.curdist) / sws.dist - 1.0f;
                xVec3AddScaled(&ent->bound.sph.center, &sweptDisp, scaleBack);
                xVec3AddScaled(&oldPos, &sweptDisp, scaleBack);
                xQuickCullForBound(&ent->bound.qcd, &ent->bound);
                zGridUpdateEnt(ent);
            }
        }
    }

    xEntCollide(ent, sc, dt);
    ent->collType = XENT_COLLTYPE_DYN;
    zGooCollsEnd();

    if (xEntBoulder_KilledBySurface(ent, sc, dt))
    {
        if (ent->collis == &sBoulderCollis)
        {
            ent->collis = NULL;
        }
        return;
    }

    S32 numDepens = 0;
    xVec3 depen;

    if (ent->collis->env_eidx > ent->collis->env_sidx ||
        ent->collis->dyn_eidx > ent->collis->dyn_sidx ||
        ent->collis->npc_eidx > ent->collis->npc_sidx ||
        ent->collis->stat_eidx > ent->collis->stat_sidx)
    {
        xVec3Init(&depen, 0.0f, 0.0f, 0.0f);

        for (i = ent->collis->env_sidx; i < ent->collis->env_eidx; i++)
        {
            if (ent->basset->flags & 0x1)
            {
                xVec3AddTo(&depen, &ent->collis->colls[i].depen);
            }
            else
            {
                F32 penComp = xVec3Dot(&ent->collis->colls[i].norm, &ent->collis->colls[i].depen);

                if (ent == zEntPlayerGetBBowl())
                {
                    // not in the DWARF: the normal velocity component is tested twice.
                    F32 velComp = xVec3Dot(&ent->collis->colls[i].norm, &velNorm);
                    if (globals.sceneCur != NULL && globals.sceneCur->sceneID == 'B301')
                    {
                        if (velComp < -0.9f && ent->timeToLive > 0.05f)
                        {
                            ent->timeToLive = 0.05f;
                        }
                    }
                    else if (velComp < -0.70710677f && ent->timeToLive > 0.05f)
                    {
                        ent->timeToLive = 0.05f;
                    }
                }

                xVec3AddScaled(&depen, &ent->collis->colls[i].norm, penComp);
            }

            numDepens++;
        }

        for (i = ent->collis->dyn_sidx; i < ent->collis->dyn_eidx; i++)
        {
            if (ent->basset->flags & 0x1)
            {
                xVec3AddTo(&depen, &ent->collis->colls[i].depen);
            }
            else
            {
                xVec3AddScaled(&depen, &ent->collis->colls[i].norm,
                               xVec3Dot(&ent->collis->colls[i].norm, &ent->collis->colls[i].depen));
            }

            xEnt* xb = (xEnt*)ent->collis->colls[i].optr;
            if (xb->baseType == eBaseTypeBoulder)
            {
                xEntBoulder* boul = (xEntBoulder*)xb;
                xVec3 force;
                xVec3 toAdd;

                xVec3Normalize(&force, &ent->collis->colls[i].depen);
                F32 v1 = xVec3Dot(&ent->vel, &force);
                F32 v2 = xVec3Dot(&boul->vel, &force);
                F32 forceMag = (2.0f * ent->basset->mass * boul->basset->mass * (v2 - v1)) /
                               (ent->basset->mass + boul->basset->mass);
                xVec3SMulBy(&force, forceMag);

                xVec3SMul(&toAdd, &force, ent->basset->bounce);
                xEntBoulder_AddInstantForce(ent, &toAdd);
                xVec3SMul(&toAdd, &force, -boul->basset->bounce);
                xEntBoulder_AddInstantForce(boul, &toAdd);

                if (boul == boulderVehicle && (ent->basset->flags & 0x2))
                {
                    zEntPlayer_Damage(boul, 1);
                }
            }

            if (xb->baseType == eBaseTypeButton)
            {
                zEntButton_Press((_zEntButton*)xb, 0x8);
                zEntButton_Hold((_zEntButton*)xb, 0x1000);
            }

            if (ent == zEntPlayerGetBBowl() && (xb->moreFlags & 0x10) &&
                xb->baseType == eBaseTypePlatform && xb->subType == ZPLATFORM_SUBTYPE_PADDLE &&
                (((zPlatform*)xb)->plat_asset->paddle.paddleFlags & 0x10))
            {
                zPlatform_PaddleCollide(&ent->collis->colls[i], &ent->frame->mat.pos, &ent->vel, 1);
            }

            numDepens++;
        }

        for (i = ent->collis->stat_sidx; i < ent->collis->stat_eidx; i++)
        {
            if (ent->basset->flags & 0x1)
            {
                xVec3AddTo(&depen, &ent->collis->colls[i].depen);
            }
            else
            {
                F32 penComp = xVec3Dot(&ent->collis->colls[i].norm, &ent->collis->colls[i].depen);

                if (ent == zEntPlayerGetBBowl() &&
                    xVec3Dot(&ent->collis->colls[i].norm, &velNorm) < -0.70710677f &&
                    ent->timeToLive > 0.05f)
                {
                    ent->timeToLive = 0.05f;
                }

                xVec3AddScaled(&depen, &ent->collis->colls[i].norm, penComp);
            }

            xEnt* xb = (xEnt*)ent->collis->colls[i].optr;
            if (!((ent->basset->flags & 0x4) && xb->baseType == eBaseTypeDestructObj) &&
                ent == zEntPlayerGetBBowl() && (xb->moreFlags & 0x10) &&
                (xb->baseType != eBaseTypeBoulder || (((xEntBoulder*)xb)->basset->flags & 0x100)))
            {
                F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
                zEntEvent(ent, xb, eEventHit_BubbleBowl, hitParams, NULL, 0);
                zEntEvent(ent, xb, eEventHit, hitParams, NULL, 0);
                if (ent->timeToLive > 0.05f)
                {
                    ent->timeToLive = 0.05f;
                }
            }

            numDepens++;
        }

        for (i = ent->collis->npc_sidx; i < ent->collis->npc_eidx; i++)
        {
            zNMECommon* npc = (zNMECommon*)ent->collis->colls[i].optr;

            if (ent->basset->flags & 0x1)
            {
                xVec3AddTo(&depen, &ent->collis->colls[i].depen);
            }
            else
            {
                F32 penComp = xVec3Dot(&ent->collis->colls[i].norm, &ent->collis->colls[i].depen);

                if (ent != zEntPlayerGetBBowl() || npc->itemType < NME_TYPE_TIKI ||
                    npc->itemType > NME_TYPE_TIKI_STONE)
                {
                    xVec3AddScaled(&depen, &ent->collis->colls[i].norm, penComp);
                }
            }

            if (ent->basset->flags & 0x8)
            {
                if (ent == zEntPlayerGetBBowl())
                {
                    npc->Damage(DMGTYP_BUBBOWL, ent, &ent->collis->colls[i].hdng, -1,
                                ePowerupLevelNormal);
                }
                else
                {
                    npc->Damage(DMGTYP_BOULDER, ent, &ent->collis->colls[i].hdng, -1,
                                ePowerupLevelNormal);
                }

                if (ent == zEntPlayerGetBBowl())
                {
                    if (npc->itemType >= NME_TYPE_TIKI && npc->itemType <= NME_TYPE_TIKI_STONE)
                    {
                        zEntPlayer_TikiKilledBy(npc, ent);
                    }

                    zEntPlayer_SNDPlayStream(0, 16, ePlayerStreamSnd_BowlComment1);
                    zEntPlayer_SNDPlayStream(17, 35, ePlayerStreamSnd_BowlComment2);
                    zEntPlayer_SNDPlayStream(36, 100, ePlayerStreamSnd_BowlComment3);
                }

                // retail tests !(timeToLive <= 0.05f) here (cror form), not timeToLive > 0.05f
                if (ent == zEntPlayerGetBBowl() && !(ent->timeToLive <= 0.05f) &&
                    (!(npc->itemType >= NME_TYPE_TIKI && npc->itemType <= NME_TYPE_TIKI_STONE) ||
                     npc->itemType == NME_TYPE_TIKI_STONE) &&
                    ((npc->itemType >= NME_TYPE_TIKI && npc->itemType <= NME_TYPE_TIKI_STONE) ||
                     (npc->flg_vuln & 0x100000)))
                {
                    ent->timeToLive = 0.05f;
                }
            }

            numDepens++;
        }

        if (numDepens != 0)
        {
            xVec3AddScaled(&ent->bound.sph.center, &depen, 1.0f / numDepens);
        }
    }

    xVec3 newVel;
    xVec3Sub(&newVel, &ent->bound.sph.center, &oldPos);
    xVec3SMulBy(&newVel, 1.0f / dt);

    F32 oldMag = xVec3Length(&ent->vel);
    F32 newMag = xVec3Length(&newVel);
    if (oldMag > ent->basset->maxVel)
    {
        oldMag = ent->basset->maxVel;
    }

    if (newMag > oldMag)
    {
        xVec3SMulBy(&newVel, oldMag / newMag);
    }

    xVec3 depenNorm;
    F32 depenComp = 0.0f;
    if (numDepens != 0)
    {
        xVec3Normalize(&depenNorm, &depen);
        depenComp = xVec3Dot(&ent->vel, &depenNorm);
    }

    if (!(ent->basset->flags & 0x1) && numDepens != 0)
    {
        if (ent->basset->bounce &&
            (depenComp / oldMag < -1.0f + ent->basset->bounce || definitelyBounce))
        {
            F32 afterBounce = xVec3Dot(&newVel, &depenNorm);
            if (-depenComp > ent->basset->bounceDamp)
            {
                xVec3AddScaled(&newVel, &depenNorm, -ent->basset->bounce * depenComp - afterBounce);
            }
        }

        if (ent->basset->friction)
        {
            xVec3 fricComp;
            xVec3Copy(&fricComp, &ent->vel);
            xVec3AddScaled(&fricComp, &depenNorm, -depenComp);
            xVec3AddScaled(&newVel, &fricComp, -ent->basset->friction * dt);
        }
    }

    xVec3Copy(&ent->vel, &newVel);

    if (numDepens != 0)
    {
        if (ent->basset->flags & 0x400)
        {
            xMat4x3 baseMatrix;
            xVec3 vForcedRotationAxis;

            if (ent->basset->uBoneIndex != 0)
            {
                xMat4x3Mul(&baseMatrix, (xMat4x3*)&ent->model->Mat[ent->basset->uBoneIndex],
                           (xMat4x3*)ent->model->Mat);
            }
            else
            {
                xMat4x3Copy(&baseMatrix, (xMat4x3*)ent->model->Mat);
            }

            if (ent->basset->flags & 0x800)
            {
                vForcedRotationAxis = baseMatrix.right;
            }
            else if (ent->basset->flags & 0x1000)
            {
                vForcedRotationAxis = baseMatrix.up;
            }
            else
            {
                vForcedRotationAxis = baseMatrix.at;
            }

            F32 fMagnitudeAgainstAxis = xVec3Dot(&ent->vel, &vForcedRotationAxis);
            xVec3AddScaled(&ent->vel, &vForcedRotationAxis, -fMagnitudeAgainstAxis);
        }

        xVec3 newRotVec;
        if (xVec3Length2(&depen) < 0.001f)
        {
            xVec3Init(&newRotVec, ent->vel.z, 0.0f, -ent->vel.x);
        }
        else
        {
            xVec3Cross(&newRotVec, &depen, &ent->vel);
        }

        xVec3Normalize(&newRotVec, &newRotVec);
        xVec3SMulBy(&ent->rotVec, 1.0f - ent->basset->stickiness);
        xVec3AddScaled(&ent->rotVec, &newRotVec, ent->basset->stickiness);

        if (xVec3Normalize(&ent->rotVec, &ent->rotVec) < 1e-5f)
        {
            ent->angVel = 0.0f;
        }
        else
        {
            F32 newAngVel = ent->fOOSphRadius * xVec3Length(&ent->vel);
            ent->angVel = (1.0f - ent->basset->stickiness) * ent->angVel +
                          ent->basset->stickiness * newAngVel;
        }
    }

    if (ent->angVel > ent->basset->maxAngVel)
    {
        ent->angVel = ent->basset->maxAngVel;
    }

    if (ent->angVel < -ent->basset->maxAngVel)
    {
        ent->angVel = -ent->basset->maxAngVel;
    }

    if (ent->angVel > 0.075f || ent->angVel < -0.075f)
    {
        xMat3x3 rotM;
        xMat3x3Rot(&rotM, &ent->rotVec, ent->angVel * dt);
        xMat3x3Mul(&ent->frame->mat, &ent->frame->mat, &rotM);
    }

    xMat3x3RMulVec(&rotatedLC, &ent->frame->mat, &ent->localCenter);
    xVec3Sub(&ent->frame->mat.pos, &ent->bound.sph.center, &rotatedLC);
    xEntEndUpdate(ent, sc, dt);

    if (ent->basset->soundID != 0 && numDepens != 0 && ent->lastRolling > 15)
    {
        if (-depenComp > ent->basset->minSoundVel)
        {
            F32 vol = (-depenComp - ent->basset->minSoundVel) /
                      (ent->basset->maxSoundVel - ent->basset->minSoundVel);
            if (vol > 1.0f)
            {
                vol = 1.0f;
            }

            ent->currSndFX = xSndMgrPlay(ent->basset->soundID, 0, NULL, NULL, ent, &vol, NULL);
        }
    }

    if (numDepens != 0)
    {
        ent->lastRolling = 0;
    }
    else
    {
        ent->lastRolling++;
    }

    if (ent->currSndFX != 0)
    {
        if (ent->lastRolling > 15)
        {
            xSndMgrStop(ent->currSndFX);
        }
    }
    else if (numDepens != 0 && ent->rollingID != 0)
    {
        ent->currSndFX =
            xSndMgrPlay(xSndMgrGetSoundGroup(ent->rollingID), 2, NULL, NULL, ent, NULL, NULL);
    }

    if (ent->collis == &sBoulderCollis)
    {
        ent->collis = NULL;
    }
}

S32 xEntBoulder_KilledBySurface(xEntBoulder* ent, xScene* sc, F32 dt)
{
    xCollis* coll;
    xCollis* cend;
    F32 depthUNUSED;
    xSurface* surf;
    zSurfaceProps* prop;

    if (!(ent->basset->flags & 0x70))
    {
        return 0;
    }

    coll = &ent->collis->colls[0];
    cend = &coll[ent->collis->idx];
    for (; coll < cend; coll++)
    {
        if (!(coll->flags & 1))
        {
            continue;
        }

        if ((ent->basset->flags & 0x40) && (coll->optr != NULL) &&
            zGooIs((xEnt*)coll->optr, depthUNUSED, 0))
        {
            xVec3AddScaled(&ent->vel, &coll->norm,
                           -(1.0f + ent->basset->bounce) * xVec3Dot(&ent->vel, &coll->norm));
            zEntEvent(ent, eEventKill);
            return 1;
        }

        surf = zSurfaceGetSurface(coll);
        if ((surf == NULL) || (surf->state != 0))
        {
            continue;
        }

        prop = (zSurfaceProps*)surf->moprops;
        if ((prop == NULL) || (prop->asset == NULL))
        {
            continue;
        }

        if (ent->basset->flags & 0x10)
        {
            switch (prop->asset->game_damage_type)
            {
            case 0:
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 6:
                ent->hitpoints--;
                if (ent->hitpoints <= 0)
                {
                    xVec3AddScaled(&ent->vel, &coll->norm,
                                   -(1.0f + ent->basset->bounce) *
                                       xVec3Dot(&ent->vel, &coll->norm));
                    zEntEvent(ent, eEventKill);
                    return 1;
                }
                break;
            case 5:
                xVec3AddScaled(&ent->vel, &coll->norm,
                               -(1.0f + ent->basset->bounce) * xVec3Dot(&ent->vel, &coll->norm));
                zEntEvent(ent, eEventKill);
                return 1;
            }
        }

        if ((ent->basset->flags & 0x20) && (prop->asset->phys_flags & 0x10))
        {
            xVec3AddScaled(&ent->vel, &coll->norm,
                           -(1.0f + ent->basset->bounce) * xVec3Dot(&ent->vel, &coll->norm));
            zEntEvent(ent, eEventKill);
            return 1;
        }
    }

    return 0;
}

void xEntBoulder_Kill(xEntBoulder* ent)
{
    xRumble::sphericalEmitter* bbowlExplodeRumble;

    if (ent == NULL)
    {
        return;
    }

    ent->chkby = XENT_COLLTYPE_NONE;
    ent->penby = XENT_COLLTYPE_NONE;
    ent->collis_chk = 0;
    ent->collis_pen = 0;
    ent->update = NULL;
    xEntHide(ent);

    if (ent->currSndFX != 0)
    {
        xSndMgrStop(ent->currSndFX);
    }

    if (ent == zEntPlayerGetBBowl())
    {
        if (sBubbleStreakID != 0xdead)
        {
            xFXStreakStop(sBubbleStreakID);
            sBubbleStreakID = 0xdead;
        }

        bbowlExplodeRumble =
            (xRumble::sphericalEmitter*)zSceneFindObject(xStrHash("RUMBLE_BOWL_EXPLODE"));
        if (bbowlExplodeRumble != NULL)
        {
            bbowlExplodeRumble->pAsset->position = ent->frame->mat.pos;
            zEntEvent(bbowlExplodeRumble, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                      0.0f, 0.0f);
        }
    }
}

void xEntBoulder_BubbleBowl(F32 multiplier)
{
    xEntBoulder* ent = zEntPlayerGetBBowl();
    xRay3 ray;
    xCollis rayCollis;

    if (ent == NULL)
    {
        return;
    }

    if (ent->update == (xEntUpdateCallback)xEntBoulder_Update)
    {
        zEntEvent(ent, eEventKill);
    }

    xEntBoulder_Reset(ent, globals.sceneCur);
    ent->update = (xEntUpdateCallback)xEntBoulder_Update;

    if (sBubbleStreakID != 0xdead)
    {
        xFXStreakStop(sBubbleStreakID);
        sBubbleStreakID = 0xdead;
    }

    xVec3Copy((xVec3*)&ent->model->Mat->pos,
              (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
    xVec3AddScaled((xVec3*)&ent->model->Mat->pos,
                   (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right,
                   globals.player.g.BubbleBowlLaunchPosLeft);
    xVec3AddScaled((xVec3*)&ent->model->Mat->pos,
                   (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->up,
                   globals.player.g.BubbleBowlLaunchPosUp);
    xVec3AddScaled((xVec3*)&ent->model->Mat->pos,
                   (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->at,
                   globals.player.g.BubbleBowlLaunchPosAt);
    xVec3Copy(&ent->bound.sph.center, (xVec3*)&ent->model->Mat->pos);
    xVec3Copy(&ent->frame->mat.pos, (xVec3*)&ent->model->Mat->pos);
    xVec3SMul(&ent->vel, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right,
              globals.player.g.BubbleBowlLaunchVelLeft);
    xVec3AddScaled(&ent->vel, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->up,
                   globals.player.g.BubbleBowlLaunchVelUp);
    xVec3AddScaled(&ent->vel, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->at,
                   globals.player.g.BubbleBowlLaunchVelAt);
    xVec3SMulBy(&ent->vel, multiplier);
    xVec3Copy(&ent->rotVec, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right);
    ent->angVel = multiplier * 10.0f;

    xVec3Copy(&ray.origin, &xglobals->___player_ent_dont_use_directly->bound.sph.center);
    xVec3Sub(&ray.dir, (xVec3*)&ent->model->Mat->pos, &ray.origin);
    ray.max_t = xVec3Normalize(&ray.dir, &ray.dir);
    ray.min_t = 0.01f + xglobals->___player_ent_dont_use_directly->bound.sph.r;
    ray.flags = 0xc00;
    xRayHitsScene(globals.sceneCur, &ray, &rayCollis);

    if (rayCollis.flags & 0x1)
    {
        xEnt* optr = (xEnt*)rayCollis.optr;

        if (optr == NULL)
        {
            ent->timeToLive = 0.0001f;
        }
        else if ((optr->chkby & XENT_COLLTYPE_PLYR) && optr->baseType != eBaseTypeNPC &&
                 (optr->baseType == eBaseTypeStatic || !(optr->moreFlags & 0x10)) &&
                 !zReactiveIsReactive(*optr))
        {
            if (optr->collLev == 5)
            {
                // not in the DWARF: collision record passed by address to xRayHitsModel.
                xCollis modelCollis;

                memset(&modelCollis, 0, sizeof(xCollis));
                xRayHitsModel(&ray, optr->model, &modelCollis);

                if (modelCollis.flags & 0x1)
                {
                    ent->timeToLive = 0.0001f;
                }
            }
            else
            {
                ent->timeToLive = 0.0001f;
            }
        }
    }

    ent->lastRolling = 0;
    ent->rollingID = 0x8883b40d; // xStrHash("sound_bubblebowl_loop")
}

void xEntBoulder_Setup(xEntBoulder* ent)
{
    ent->asset->redMult = 1.0f;
    ent->asset->greenMult = 1.0f;
    ent->asset->blueMult = 1.0f;

    if (ent->model != NULL)
    {
        ent->asset->seeThru = ent->model->Alpha;
    }
    else
    {
        ent->asset->seeThru = 1.0f;
    }

    xEntSetup(ent);
}

void xEntBoulder_Reset(xEntBoulder* ent, xScene*)
{
    ent->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC | XENT_COLLTYPE_DYN;
    ent->penby = XENT_COLLTYPE_PLYR;
    ent->collis_chk = 0x2e;
    ent->collis_pen = 0;

    xEntReset(ent);

    xVec3Init(&ent->force, 0.0f, 0.0f, 0.0f);
    xVec3Init(&ent->instForce, 0.0f, 0.0f, 0.0f);
    xVec3Init(&ent->vel, 0.0f, 0.0f, 0.0f);
    xVec3Init(&ent->rotVec, 1.0f, 0.0f, 0.0f);

    ent->angVel = 0.0f;

    if (ent->basset->flags & 0x200)
    {
        ent->timeToLive = ent->basset->killtimer;
    }
    else
    {
        ent->timeToLive = -1.0f;
    }

    ent->hitpoints = ent->basset->hitpoints;
    xEntBoulder_RealBUpdate(ent, &ent->frame->mat.pos);
}

void xEntBoulderEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, U32 toParamWidgetID)
{
    xEntBoulder* s = (xEntBoulder*)to;

    switch (toEvent)
    {
    case eEventVisible:
    case eEventFastVisible:
        xEntShow(s);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOn(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(s);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOff(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventCollisionOn:
        s->chkby |= XENT_COLLTYPE_PLYR;
        break;
    case eEventCollisionOff:
        s->chkby &= (U8)~XENT_COLLTYPE_PLYR;
        break;
    case eEventCollisionReset:
        s->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC | XENT_COLLTYPE_DYN;
        break;
    case eEventCollision_Visible_On:
        s->chkby |= XENT_COLLTYPE_PLYR;
        xEntShow(s);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOn(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventCollision_Visible_Off:
        s->chkby &= (U8)~XENT_COLLTYPE_PLYR;
        xEntHide(s);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOff(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventSpawnBubblesOn:
        zFXBubbleSpawnStart(*s, toParam[0]);
        break;
    case eEventSpawnBubblesOff:
        zFXBubbleSpawnStop(*s);
        break;
    case eEventCameraCollidePartial:
        zCollGeom_CamEnable(s);
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable(s);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable(s);
        break;
    case eEventReset:
        xEntBoulder_Reset(s, globals.sceneCur);
        if (xEntIsVisible(s))
        {
            s->update = (xEntUpdateCallback)xEntBoulder_Update;
        }
        break;
    case eEventHit:
        if (s->update)
        {
            if (from == xglobals->___player_ent_dont_use_directly && !(s->basset->flags & 0x100))
            {
                break;
            }

            if (from && from->baseType == eBaseTypeNPC && !(s->basset->flags & 0x80))
            {
                break;
            }

            s->hitpoints--;
            if (s->hitpoints <= 0)
            {
                zEntEvent(s, eEventKill);
            }
        }
        break;
    case eEventKill:
        if (s == zEntPlayerGetBBowl() && zEntPlayerGetBowlBlastRadius() == 0.0f)
        {
            zFX_SpawnBubbleHit(&s->bound.sph.center, xrand_RandomInt32() % 64 + 36);
            zEntPlayerBowlTargetRemoveAll();
            zEntPlayerExplodeBowl();
        }
        xEntBoulder_Kill(s);
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr)
        {
            if (toParam[0] <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, s, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                FloatAndVoid dist;
                dist.f = SQR(toParam[0]);
                xUpdateCull_SetCB(globals.updateMgr, s, xUpdateCull_DistanceSquaredCB, dist.v);
            }
        }
        break;
    case eEventLaunchShrapnel:
        if (toParamWidget)
        {
            zShrapnelAsset* shrap = (zShrapnelAsset*)toParamWidget;
            if (shrap->initCB)
            {
                shrap->initCB(shrap, s->model, &s->vel, NULL);
            }
        }
        break;
    case eEventSetLightKit:
        xModelSetLightKit(s->model, (xLightKit*)toParamWidget);
        break;
    }
}

static S32 RecurseLinks(const xLinkAsset* link, S32 count, xEntBoulder** boulList)
{
    S32 i;
    S32 numInList = 0;

    for (i = 0; i < count; i++)
    {
        const xLinkAsset* currLink = &link[i];
        if (currLink->dstEvent == eEventConnectToChild)
        {
            xBase* mychild = zSceneFindObject(currLink->dstAssetID);
            if (mychild)
            {
                RecurseChild(mychild, boulList, numInList);
            }
        }
    }

    return numInList;
}

static void RecurseChild(xBase* child, xEntBoulder** boulList, S32& currBoul)
{
    switch (child->baseType)
    {
    case eBaseTypeBoulder:
        if (boulList)
        {
            boulList[currBoul] = (xEntBoulder*)child;
        }
        currBoul++;
        break;
    case eBaseTypeGroup:
    {
        U32 i;
        U32 cnt;
        xGroup* grp = (xGroup*)child;

        cnt = xGroupGetCount(grp);
        for (i = 0; i < cnt; i++)
        {
            xBase* grpitem = xGroupGetItemPtr(grp, i);
            if (grpitem)
            {
                if (grpitem->baseType == eBaseTypeBoulder)
                {
                    if (boulList)
                    {
                        boulList[currBoul] = (xEntBoulder*)grpitem;
                    }
                    currBoul++;
                }
                else if (grpitem->baseType == eBaseTypeGroup)
                {
                    RecurseChild(grpitem, boulList, currBoul);
                }
            }
        }

        break;
    }
    }
}

void xBoulderGenerator_Init(xBase& data, xDynAsset& asset, size_t)
{
    xBoulderGenerator_Init((xBoulderGenerator*)&data, (xBoulderGeneratorAsset*)&asset);
}

static inline void xBoulderGenerator_Reset(xBoulderGenerator* bg)
{
    for (S32 i = 0; i < bg->numBoulders; i++)
    {
        xEntBoulder_Kill(bg->boulderList[i]);
        bg->boulderAges[i] = 0;
    }
}

void xBoulderGenerator_Init(xBoulderGenerator* bg, xBoulderGeneratorAsset* asset)
{
    U32 size;

    xBaseInit(bg, asset);

    bg->eventFunc = xBoulderGenerator_EventCB;
    bg->bgasset = asset;

    if (bg->linkCount != 0)
    {
        bg->link = (xLinkAsset*)(&bg->bgasset[1]);
    }
    else
    {
        bg->link = NULL;
    }

    size = 0;
    bg->objectPtr = xSTFindAsset(asset->object, &size);

    if (size == sizeof(xMarkerAsset))
    {
        bg->isMarker = 1;
    }
    else
    {
        bg->isMarker = 0;
        bg->objectPtr = zSceneFindObject(asset->object);
    }

    bg->numBoulders = RecurseLinks(bg->link, bg->linkCount, NULL);
    bg->nextBoulder = bg->numBoulders;
    bg->boulderList = (xEntBoulder**)xMemAlloc(
        gActiveHeap, bg->numBoulders * sizeof(xEntBoulder*) + bg->numBoulders * sizeof(S32), 0);
    bg->boulderAges = (S32*)&bg->boulderList[bg->numBoulders];
    RecurseLinks(bg->link, bg->linkCount, bg->boulderList);

    bg->initaxis.y = bg->bgasset->rotation.x;
    bg->initaxis.x = bg->bgasset->rotation.y;
    bg->initaxis.z = bg->bgasset->rotation.z;
    bg->angvel = 0.017453292f * xVec3Normalize(&bg->initaxis, &bg->initaxis);

    bg->lengthOfInitVel = xVec3Length(&bg->bgasset->initvel);
    if (bg->lengthOfInitVel > 0.00001f)
    {
        bg->perp1.x = bg->bgasset->initvel.z - bg->bgasset->initvel.y;
        bg->perp1.y = bg->bgasset->initvel.x - bg->bgasset->initvel.z;
        bg->perp1.z = bg->bgasset->initvel.y - bg->bgasset->initvel.x;
        xVec3Normalize(&bg->perp1, &bg->perp1);
        xVec3Cross(&bg->perp2, &bg->bgasset->initvel, &bg->perp1);
        xVec3SMulBy(&bg->perp1, bg->lengthOfInitVel);
    }

    xBoulderGenerator_Reset(bg);
}

static S32 GetBoulderForGenerating(xBoulderGenerator* bg)
{
    S32 i;
    S32 j = 0;
    S32 oldestCulled = -1;
    S32 minAge = bg->numBoulders >> 1;
    S32 numList;

    for (i = 0, numList = bg->numBoulders; i < numList; i++)
    {
        j = i + bg->nextBoulder;

        if (j >= numList)
        {
            j -= numList;
        }

        if (!xEntIsVisible(bg->boulderList[j]))
        {
            break;
        }

        if (bg->boulderAges[j] < minAge)
        {
        }
        else if (oldestCulled < 0)
        {
            oldestCulled = j;
        }
        else if (bg->boulderList[oldestCulled]->_isCulled)
        {
            if (bg->boulderList[j]->_isCulled &&
                (bg->boulderAges[oldestCulled] < bg->boulderAges[j]))
            {
                oldestCulled = j;
            }
        }
        else if (bg->boulderList[j]->_isCulled ||
                 (bg->boulderAges[oldestCulled] < bg->boulderAges[j]))
        {
            oldestCulled = j;
        }
    }

    if (i == numList)
    {
        if (oldestCulled < 0)
        {
            j = (numList - 1) * xurand();
            if (j >= numList)
            {
                j = numList - 1;
            }
        }
        else
        {
            j = oldestCulled;
        }
    }

    bg->nextBoulder = j + 1;
    if (bg->nextBoulder >= numList)
    {
        bg->nextBoulder = 0;
    }

    return j;
}

static void BoulderGen_GiveBirth(xBoulderGenerator* bg, S32 indx)
{
    for (S32 j = 0; j < bg->numBoulders; j++)
    {
        bg->boulderAges[j]++;
    }

    bg->boulderAges[indx] = 1;
}

void xBoulderGenerator_Launch(xBoulderGenerator* bg, xVec3* pnt, F32 t)
{
    S32 i = GetBoulderForGenerating(bg);
    xEntBoulder* b = bg->boulderList[i];
    xEnt* genEnt;
    xVec3 worldOffset;
    F32 invTime;

    if (b != bg->objectPtr || !xEntIsVisible(b))
    {
        xEntBoulder_Reset(b, globals.sceneCur);
        zEntEvent(b, eEventBorn);
        b->update = (xEntUpdateCallback)xEntBoulder_Update;

        genEnt = (xEnt*)bg->objectPtr;
        if (bg->isMarker)
        {
            xVec3Copy(&b->frame->mat.pos, &((xMarkerAsset*)bg->objectPtr)->pos);
        }
        else
        {
            xVec3Copy(&b->frame->mat.pos, (xVec3*)&genEnt->model->Mat->pos);
            if (b->asset->modelInfoID == genEnt->asset->modelInfoID)
            {
                *(xMat3x3*)&b->frame->mat = *(xMat3x3*)genEnt->model->Mat;
                *(xMat3x3*)b->model->Mat = *(xMat3x3*)genEnt->model->Mat;
            }
        }

        if ((bg->bgasset->flags & 0x1) && !bg->isMarker)
        {
            xMat3x3RMulVec(&worldOffset, (xMat3x3*)genEnt->model->Mat, &bg->bgasset->offset);
            xVec3AddTo(&b->frame->mat.pos, &worldOffset);
        }
        else
        {
            xVec3AddTo(&b->frame->mat.pos, &bg->bgasset->offset);
        }

        xVec3Copy(&b->rotVec, &bg->initaxis);
        if ((bg->bgasset->flags & 0x1) && !bg->isMarker)
        {
            xMat3x3RMulVec(&b->rotVec, (xMat3x3*)genEnt->model->Mat, &b->rotVec);
            xVec3Normalize(&b->rotVec, &b->rotVec);
        }

        b->angVel = bg->angvel;
    }

    if (t == 0.0f)
    {
        t = 2.0f;
    }

    invTime = 1.0f / t;
    b->vel.x = invTime * (pnt->x - b->frame->mat.pos.x);
    b->vel.z = invTime * (pnt->z - b->frame->mat.pos.z);
    b->vel.y = invTime * (pnt->y - b->frame->mat.pos.y) + b->basset->gravity * t * 0.5f;

    BoulderGen_GiveBirth(bg, i);
}

void xBoulderGenerator_EventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                               xBase* toParamWidget, U32 toParamWidgetID)
{
    xBoulderGenerator* bg = (xBoulderGenerator*)to;
    xVec3 pnt;

    switch (toEvent)
    {
    case eEventGenerateBoulder:
        xBoulderGenerator_GenBoulder(bg);
        break;
    case eEventLaunchBoulderAtWidget:
        switch (toParamWidget->baseType)
        {
        case eBaseTypeVillain:
        case eBaseTypePlayer:
        case eBaseTypePickup:
        case eBaseTypePlatform:
        case eBaseTypeDoor:
        case eBaseTypeStatic:
        case eBaseTypeDynamic:
        case eBaseTypePendulum:
        case eBaseTypeHangable:
        case eBaseTypeButton:
        case eBaseTypeDestructObj:
        case eBaseTypeNPC:
        case eBaseTypeBoulder:
            xVec3Copy(&pnt, xEntGetPos((xEnt*)toParamWidget));
            if (toParam[1])
            {
                pnt.x += toParam[1] * (2.0f * (xurand() - 0.5f));
                pnt.y += toParam[1] * (2.0f * (xurand() - 0.5f));
                pnt.z += toParam[1] * (2.0f * (xurand() - 0.5f));
            }
            xBoulderGenerator_Launch(bg, &pnt, toParam[0]);
            break;
        case eBaseTypeMovePoint:
            xVec3Copy(&pnt, zMovePointGetPos((zMovePoint*)toParamWidget));
            if (toParam[1])
            {
                pnt.x += toParam[1] * (2.0f * (xurand() - 0.5f));
                pnt.y += toParam[1] * (2.0f * (xurand() - 0.5f));
                pnt.z += toParam[1] * (2.0f * (xurand() - 0.5f));
            }
            xBoulderGenerator_Launch(bg, &pnt, toParam[0]);
            break;
        }
        break;
    case eEventLaunchBoulderAtPoint:
        pnt.x = toParam[0];
        pnt.y = toParam[1];
        pnt.z = toParam[2];
        xBoulderGenerator_Launch(bg, &pnt, toParam[3]);
        break;
    case eEventLaunchBoulderAtPlayer:
        xVec3Copy(&pnt, xEntGetPos(xglobals->___player_ent_dont_use_directly));
        xVec3AddScaled(&pnt, &xglobals->___player_ent_dont_use_directly->frame->dpos,
                       toParam[0] * toParam[1] / globals.update_dt);
        if (toParam[2])
        {
            pnt.x += toParam[2] * (2.0f * (xurand() - 0.5f));
            pnt.y += toParam[2] * (2.0f * (xurand() - 0.5f));
            pnt.z += toParam[2] * (2.0f * (xurand() - 0.5f));
        }
        xBoulderGenerator_Launch(bg, &pnt, toParam[0]);
        break;
    case eEventReset:
        xBoulderGenerator_Reset(bg);
        break;
    }
}

void xBoulderGenerator_GenBoulder(xBoulderGenerator* bg)
{
    S32 i = GetBoulderForGenerating(bg);
    xEntBoulder* b = bg->boulderList[i];
    xEnt* genEnt = (xEnt*)bg->objectPtr;

    if (bg->objectPtr == zEntPlayerGetBBowl())
    {
        xEntBoulder_Reset(b, globals.sceneCur);
        zEntEvent(b, eEventBorn);
        b->update = (xEntUpdateCallback)xEntBoulder_Update;

        xEntBoulder* bb = zEntPlayerGetBBowl();

        xVec3Copy(&b->frame->mat.pos, &bb->frame->mat.pos);
        xVec3Copy(&b->rotVec, &bb->rotVec);
        b->angVel = bb->angVel;
        xVec3Copy(&b->vel, &bb->vel);

        xEntBoulder_Kill(bb);
    }
    else
    {
        if (b != bg->objectPtr || !xEntIsVisible(b))
        {
            xEntBoulder_Reset(b, globals.sceneCur);
            zEntEvent(b, eEventBorn);
            b->update = (xEntUpdateCallback)xEntBoulder_Update;

            if (bg->isMarker)
            {
                xVec3Copy(&b->frame->mat.pos, &((xMarkerAsset*)bg->objectPtr)->pos);
            }
            else
            {
                xVec3Copy(&b->frame->mat.pos, (xVec3*)&genEnt->model->Mat->pos);
                if (b->asset->modelInfoID == genEnt->asset->modelInfoID)
                {
                    *(xMat3x3*)&b->frame->mat = *(xMat3x3*)genEnt->model->Mat;
                    *(xMat3x3*)b->model->Mat = *(xMat3x3*)genEnt->model->Mat;
                }
            }

            if ((bg->bgasset->flags & 0x1) && !bg->isMarker)
            {
                xVec3 worldOffset;
                xMat3x3RMulVec(&worldOffset, (xMat3x3*)genEnt->model->Mat, &bg->bgasset->offset);
                xVec3AddTo(&b->frame->mat.pos, &worldOffset);
            }
            else
            {
                xVec3AddTo(&b->frame->mat.pos, &bg->bgasset->offset);
            }

            if (bg->bgasset->offsetRand)
            {
                b->frame->mat.pos.x += bg->bgasset->offsetRand * (2.0f * (xurand() - 0.5f));
                b->frame->mat.pos.y += bg->bgasset->offsetRand * (2.0f * (xurand() - 0.5f));
                b->frame->mat.pos.z += bg->bgasset->offsetRand * (2.0f * (xurand() - 0.5f));
            }

            xVec3Copy(&b->rotVec, &bg->initaxis);
            if ((bg->bgasset->flags & 0x1) && !bg->isMarker)
            {
                xMat3x3RMulVec(&b->rotVec, (xMat3x3*)genEnt->model->Mat, &b->rotVec);
                xVec3Normalize(&b->rotVec, &b->rotVec);
            }

            b->angVel = bg->angvel;
        }

        if (bg->lengthOfInitVel > 0.00001f && bg->bgasset->velAngleRand > 0.00001f)
        {
            F32 p1c = xurand() - 0.5f;
            F32 p2c = xurand() - 0.5f;
            F32 nf = 1.0f / xsqrt(SQR(p1c) + SQR(p2c));

            if (nf < 0.00001f)
            {
                p1c = 1.0f;
                p2c = 0.0f;
            }
            else
            {
                p1c *= nf;
                p2c *= nf;
            }

            xVec3 perpRand;
            xVec3SMul(&perpRand, &bg->perp1, p1c);
            xVec3AddScaled(&perpRand, &bg->perp2, p2c);

            F32 randAng = bg->bgasset->velAngleRand * (xurand() - 0.5f);
            randAng = 0.017453292f * randAng;

            p1c = icos(randAng);
            p2c = isin(randAng);

            xVec3SMul(&b->vel, &bg->bgasset->initvel, p1c);
            xVec3AddScaled(&b->vel, &perpRand, p2c);
        }
        else
        {
            xVec3Copy(&b->vel, &bg->bgasset->initvel);
        }

        if (bg->bgasset->velMagRand > 0.00001f)
        {
            if (bg->lengthOfInitVel > 0.00001f)
            {
                F32 sclMag = bg->bgasset->velMagRand * xurand() + 1.0f;
                xVec3SMulBy(&b->vel, sclMag);
            }
            else
            {
                b->vel.x = bg->bgasset->velMagRand * (2.0f * (xurand() - 0.5f));
                b->vel.y = bg->bgasset->velMagRand * (2.0f * (xurand() - 0.5f));
                b->vel.z = bg->bgasset->velMagRand * (2.0f * (xurand() - 0.5f));
            }
        }

        if ((bg->bgasset->flags & 0x1) && !bg->isMarker)
        {
            xMat3x3RMulVec(&b->vel, (xMat3x3*)genEnt->model->Mat, &b->vel);
        }
    }

    BoulderGen_GiveBirth(bg, i);
}
