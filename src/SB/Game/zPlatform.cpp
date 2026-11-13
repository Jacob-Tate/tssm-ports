#include "zPlatform.h"

#include "xEvent.h"
#include "xFFX.h"
#include "xMathInlines.h"
#include "xQuickCull.h"
#include "xShadowSimple.h"
#include "xSkyDome.h"
#include "xSnd.h"
#include "xString.h"
#include "xstransvc.h"
#include "xUpdateCull.h"
#include "iDebug.h"
#include "iModel.h"
#include "zCar.h"
#include "zCollGeom.h"
#include "zFX.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zNMESupplement.h"
#include "zNMESupport.h"
#include "zShrapnel.h"
#include "xRand.h"

static zParEmitter* sEmitTremble;
static zParEmitter* sEmitBreakaway;
static xRumble::effect* sRumbleBreakaway;

namespace GG02
{
    static S32 specialsExist;
    static RwRaster* rast_goofyShadow;
    char* nam_shadRast = "shadow_round";
} // namespace GG02

static F32 SolvePaddleMotion(zPlatform* plat, F32* time, F32 tmr);
static void zPlatformTranslate(xEnt* xent, xVec3* dpos, xMat4x3* dmat);
static void zPlatform_GoofyRender(xEnt* ent);
static void zPlatform_BreakawayFallFX(zPlatform* ent, F32 dt);
static void zPlatFM_Update(zPlatform* plat, xScene* sc, F32 dt);

static void genericPlatRender(xEnt* ent)
{
    if (!ent->model || !xEntIsVisible(ent))
    {
        return;
    }

    xModelRender(ent->model);
}

void zPlatform_Init(void* plat, void* asset)
{
    zPlatform_Init((zPlatform*)plat, (xEntAsset*)asset);
}

void zPlatform_Init(zPlatform* plat, xEntAsset* asset)
{
    xPlatformAsset* plat_asset = (xPlatformAsset*)(asset + 1);
    xEntMotionAsset* ent_motion_asset = (xEntMotionAsset*)(plat_asset + 1);
    xAnimFile* spring_file;
    xAnimFile* idle_file;
    U32 size;
    U32 newid;
    xDestructibleAsset* destruct;

    zEntInit(plat, asset, 'PLAT');

    plat->plat_asset = plat_asset;
    plat->subType = plat_asset->type;

    if (plat->linkCount)
    {
        plat->link = (xLinkAsset*)(ent_motion_asset + 1);
    }
    else
    {
        plat->link = NULL;
    }

    plat->update = zPlatform_Update;
    plat->move = zPlatform_Move;
    plat->eventFunc = zPlatformEventCB;
    plat->transl = zPlatformTranslate;
    plat->render = genericPlatRender;
    plat->am = NULL;
    plat->bm = NULL;
    plat->state = 0;
    plat->plat_flags = 0;
    plat->fmrt = NULL;
    plat->elapsedTime = 0.0f;

    if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY)
    {
        plat->collis = (xEntCollis*)xMemAllocSize(sizeof(xEntCollis));
        plat->collis->chk = 0;
        plat->collis->pen = 0;
        plat->collis->post = NULL;
        plat->am = plat->model;
        plat->bm = NULL;
        plat->model = plat->am;
        plat->collModel = NULL;
    }
    else if (plat->subType == ZPLATFORM_SUBTYPE_SPRINGBOARD)
    {
        void* spring_anim;
        void* idle_anim;

        spring_file = NULL;
        spring_anim =
            plat_asset->sb.animID[0] ? xSTFindAsset(plat_asset->sb.animID[0], NULL) : NULL;
        idle_anim = plat_asset->sb.animID[1] ? xSTFindAsset(plat_asset->sb.animID[1], NULL) : NULL;

        if (spring_anim || idle_anim)
        {
            gxAnimUseGrowAlloc = 1;

            plat->atbl = xAnimTableNew("", 0);
            xAnimTableNewState(plat->atbl, "Idle", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                               xAnimDefaultBeforeEnter, NULL, NULL);

            if (spring_anim)
            {
                xAnimTableNewState(plat->atbl, "Spring", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                                   NULL, xAnimDefaultBeforeEnter, NULL, NULL);
                xAnimTableNewTransition(plat->atbl, "Spring", "Idle", NULL, NULL, 0x10, 0, 0.0f,
                                        0.0f, 0, 0, 0.1f, NULL);

                spring_file = xAnimFileNew(spring_anim, "", 0, NULL);
                xAnimTableAddFile(plat->atbl, spring_file, "Spring");
            }

            if (idle_anim)
            {
                idle_file = xAnimFileNew(idle_anim, "", 0, NULL);
                xAnimTableAddFile(plat->atbl, idle_file, "Idle");
            }
            else
            {
                xAnimTableAddFile(plat->atbl, spring_file, "Idle");
                plat->atbl->StateList->Speed = 0.0f;
            }

            gxAnimUseGrowAlloc = 0;

            xAnimPoolAlloc(&globals.sceneCur->mempool, plat, plat->atbl, plat->model);
        }
    }
    else if (plat->subType == ZPLATFORM_SUBTYPE_FM)
    {
        plat->fmrt = (zPlatFMRunTime*)xMemAllocSize(sizeof(zPlatFMRunTime));
    }

    xEntMotionInit(&plat->motion, plat, ent_motion_asset);

    xEntDriveInit(&plat->drv, plat);
    plat->drv.flags = 0x1;

    xEntOpacityInit(&plat->opacity, plat);

    plat->destructible = NULL;
    size = 0;
    newid = xStrHashCat(plat->model->modelID, "_destruct");
    destruct = (xDestructibleAsset*)xSTFindAsset(newid, &size);
    if (destruct != NULL)
    {
        zDestructibleAsset_Init(newid, plat->destructible, plat, plat->asset->modelInfoID);
    }

    if (plat->destructible != NULL)
    {
        plat->destructible->base_model = plat->model;
        plat->destructible->base_collision = plat->collModel;
        plat->destructible->has_animation = 0;
    }

    plat->sndID = (iSndHandle)-1;

    if (plat->id == 0x3EFFAC8C)
    {
        plat->render = zPlatform_GoofyRender;
        GG02::specialsExist = 1;
        GG02::rast_goofyShadow = NMEC_FindRWRaster(GG02::nam_shadRast);
    }
}

// No GC or PS2 symbol (inlined into zSceneSetup). Based on BfBB's zPlatform_Setup; TSSM also hooks
// up the destructible collision, sets up the motion, finds the breakaway rumble and starts
// motions that are not flagged to wait.
inline void zPlatform_Setup(zPlatform* plat, xScene* sc)
{
    zEntSetup(plat);

    if (plat->destructible)
    {
        plat->destructible->base_collision = plat->collModel;
    }

    xEntMotionSetup(&plat->motion, sc);

    sEmitTremble = zParEmitterFind("PAREMIT_PLAT_TREMBLE");
    sEmitBreakaway = zParEmitterFind("PAREMIT_PLAT_BREAKAWAY");
    sRumbleBreakaway =
        (xRumble::effect*)zSceneFindObject(xStrHash("RUMBLE_COLLAPSE_PLATFORM_WARN"));

    if (plat->subType == ZPLATFORM_SUBTYPE_PADDLE)
    {
        plat->tmr = 1e-9f;
        plat->state = 2;
        plat->ctr = plat->plat_asset->paddle.startOrient;
    }

    if (!(plat->motion.asset->ent_motion_flags & 0x4))
    {
        zEntEvent(plat, 0, plat, eEventRun, NULL, NULL, 0, FE_NO);
    }
}

void zPlatform_Reset(zPlatform* plat, xScene* sc)
{
    RpAtomic* imodel;
    xSurface* surf;
    S32 i;

    if (plat->destructible != NULL)
    {
        imodel = (RpAtomic*)xSTFindAsset(plat->destructible->base_modelID, NULL);
        if (imodel != NULL)
        {
            surf = NULL;
            if (plat != NULL && plat->model != NULL)
            {
                surf = plat->model->Surf;
            }

            if (plat->model != NULL)
            {
                xModelInstanceFree(plat->model);
                plat->model = NULL;
            }

            plat->model = xEntLoadModel(NULL, imodel, plat->destructible->base_model);

            if (surf != NULL && plat != NULL && plat->model != NULL)
            {
                plat->model->Surf = surf;
            }

            plat->model->Flags &= (U16)~0x400;
            plat->model->Pipe.Flags |= 0x100000;
            plat->model->modelID = plat->destructible->base_modelID;

            xBoxForModelLocal(plat->bound.box.box, plat->model, false);
            plat->bound.mat = (xMat4x3*)plat->model->Mat;
            xQuickCullForBound(&plat->bound.qcd, &plat->bound);
            xBoundUpdate(&plat->bound);
            zGridUpdateEnt(plat);
        }

        if (plat->destructible != NULL)
        {
            zDestructibleAsset_Reset(plat->destructible, plat);
            plat->destructible->base_model = plat->model;
            plat->destructible->base_collision = plat->collModel;
            plat->destructible->has_animation = 0;
        }
    }

    if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY)
    {
        plat->model = plat->am;
        plat->collModel = NULL;
    }

    zEntReset(plat);
    xEntMotionReset(&plat->motion, sc);

    plat->plat_flags = 0x1;

    switch (plat->subType)
    {
    case ZPLATFORM_SUBTYPE_PLATFORM:
        plat->state = 3;
        break;
    case ZPLATFORM_SUBTYPE_BREAKAWAY:
        plat->tmr = plat->plat_asset->ba.warningTime;
        plat->state = 0;
        plat->pflags &= (U8) ~(0x2 | 0x4);
        plat->collis->chk = 0;
        xVec3Copy(&plat->frame->vel, &g_O3);
        plat->bound.mat = (xMat4x3*)plat->model->Mat;
        break;
    case ZPLATFORM_SUBTYPE_MECH:
        plat->state = plat->motion.mech.state;
        break;
    case ZPLATFORM_SUBTYPE_SPRINGBOARD:
        plat->tmr = -1.0f;
        plat->ctr = 0;
        plat->state = 0;
        break;
    case ZPLATFORM_SUBTYPE_PADDLE:
        plat->tmr = 1e-9f;
        plat->state = 2;
        plat->ctr = plat->plat_asset->paddle.startOrient;
        break;
    case ZPLATFORM_SUBTYPE_FM:
        for (i = 0; i < 13; i++)
        {
            plat->fmrt->flags = 0;
            plat->fmrt->tmrs[i] = 0.0f;
            plat->fmrt->ttms[i] = 0.0f;
            plat->fmrt->atms[i] = 0.0f;
            plat->fmrt->dtms[i] = 0.0f;
            plat->fmrt->vms[i] = 0.0f;
            plat->fmrt->dss[i] = 0.0f;
        }
        break;
    }

    // motion type 3: move-point motion
    if (plat->motion.type == 3)
    {
        plat->src = plat->motion.mp.src;
    }

    if (!(plat->plat_asset->flags & 0x4))
    {
        plat->chkby = XENT_COLLTYPE_NONE;
    }
    else
    {
        plat->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
    }

    xGridRemove(&plat->gridb);
    plat->bupdate(plat, (xVec3*)&plat->model->Mat->pos);
    plat->moving = 0;

    // xStrHash("teeter_totter_pat"), xStrHash("teeter_totter_pat_bind")
    if (plat->asset->modelInfoID == 0x8A55676E || plat->asset->modelInfoID == 0xE4E4A02E)
    {
        plat->plat_flags |= 0x2;
    }

    xEntOpacityInit(&plat->opacity, plat);
    plat->sndID = (iSndHandle)-1;

    if (plat->id == 0x3EFFAC8C)
    {
        plat->simpShadow = NULL;
    }
}

static F32 SolvePaddleMotion(zPlatform* plat, F32* time, F32 tmr)
{
    xPlatformPaddleData* paddle = &plat->plat_asset->paddle;

    F32 destOrient = paddle->orient[plat->ctr];
    F32 srcOrient;

    if (plat->state == 1 || plat->state == 3)
    {
        if (plat->ctr + 1 == paddle->countOrient)
        {
            srcOrient = paddle->orientLoop;
        }
        else
        {
            srcOrient = paddle->orient[plat->ctr + 1];
        }
    }
    else if (plat->state == 2 || plat->state == 4)
    {
        if (plat->ctr == 0)
        {
            destOrient = paddle->orientLoop;
            srcOrient = paddle->orient[paddle->countOrient - 1];
        }
        else
        {
            srcOrient = paddle->orient[plat->ctr - 1];
        }
    }
    else
    {
        return destOrient;
    }

    F32 absDelta = xabs(destOrient - srcOrient);

    if (plat->state == 3 || plat->state == 4)
    {
        time[0] = 0.0f;
        time[1] = 0.2f;
        time[2] = 0.0f;

        if (tmr >= 0.0f)
        {
            if (tmr > 0.1f)
            {
                tmr = 0.2f - tmr;
            }

            F32 stutterAmount = 100.0f * tmr;
            if (destOrient < srcOrient)
            {
                stutterAmount = -stutterAmount;
            }

            return destOrient + stutterAmount;
        }

        return destOrient;
    }

    F32 A = 0.0f;
    F32 D = 0.0f;
    F32 distA, distC, distD;

    if (paddle->accelTime && paddle->decelTime)
    {
        A = paddle->rotateSpeed / paddle->accelTime;
        D = paddle->rotateSpeed / paddle->decelTime;

        time[0] = xsqrt(2.0f * D * absDelta / (A * D + A * A));
        if (time[0] < paddle->accelTime)
        {
            time[1] = 0.0f;
            time[2] = A * time[0] / D;
        }
        else
        {
            distA = 0.5f * paddle->rotateSpeed * paddle->accelTime;
            distD = 0.5f * paddle->rotateSpeed * paddle->decelTime;
            distC = absDelta - distA - distD;

            time[0] = paddle->accelTime;
            time[1] = distC / paddle->rotateSpeed;
            time[2] = paddle->decelTime;
        }
    }
    else if (paddle->accelTime)
    {
        A = paddle->rotateSpeed / paddle->accelTime;

        time[0] = xsqrt(2.0f * absDelta / A);
        time[2] = 0.0f;
        if (time[0] < paddle->accelTime)
        {
            time[1] = 0.0f;
        }
        else
        {
            distA = 0.5f * paddle->rotateSpeed * paddle->accelTime;
            distC = absDelta - distA;

            time[0] = paddle->accelTime;
            time[1] = distC / paddle->rotateSpeed;
        }
    }
    else if (paddle->decelTime)
    {
        D = paddle->rotateSpeed / paddle->decelTime;

        time[0] = 0.0f;
        time[2] = xsqrt(2.0f * absDelta / D);
        if (time[2] < paddle->decelTime)
        {
            time[1] = 0.0f;
        }
        else
        {
            distD = 0.5f * paddle->rotateSpeed * paddle->decelTime;
            distC = absDelta - distD;

            time[1] = distC / paddle->rotateSpeed;
            time[2] = paddle->decelTime;
        }
    }
    else
    {
        time[0] = 0.0f;
        time[1] = absDelta / paddle->rotateSpeed;
        time[2] = 0.0f;
    }

    if (tmr >= 0.0f)
    {
        F32 lerp;

        if (time[2] && tmr <= time[2])
        {
            lerp = 1.0f - 0.5f * D * tmr * tmr / absDelta;
        }
        else
        {
            F32 ttot = time[0] + time[1] + time[2];
            tmr = ttot - tmr;
            if (tmr > ttot)
            {
                tmr = ttot;
            }

            if (time[0] && (!time[1] || tmr <= time[0]))
            {
                lerp = 0.5f * A * tmr * tmr / absDelta;
            }
            else
            {
                tmr -= time[0];
                lerp =
                    (0.5f * paddle->rotateSpeed * paddle->accelTime + tmr * paddle->rotateSpeed) /
                    absDelta;
            }
        }

        return lerp * (destOrient - srcOrient) + srcOrient;
    }

    return 0.0f;
}

void zPlatform_PaddleStartRotate(xEnt* entplat, S32 direction, S32 stutter)
{
    zPlatform* plat = (zPlatform*)entplat;
    F32 time[3];

    if (stutter != 0)
    {
        if (direction > 0)
        {
            plat->state = 4;
        }
        else if (direction < 0)
        {
            plat->state = 3;
        }
    }
    else
    {
        if (direction > 0)
        {
            plat->state = 2;
            plat->ctr++;

            if (plat->ctr >= plat->plat_asset->paddle.countOrient)
            {
                plat->ctr = 0;
            }
        }
        else if (direction < 0)
        {
            plat->state = 1;
            plat->ctr--;

            if (plat->ctr < 0)
            {
                plat->ctr = plat->plat_asset->paddle.countOrient - 1;
            }
        }
    }

    SolvePaddleMotion(plat, time, -1.0f);
    plat->tmr = time[0] + time[1] + time[2];
}

U32 zPlatform_PaddleCollide(xCollis* coll, const xVec3* hitsource, const xVec3* hitvel,
                            U32 worldSpaceNorm)
{
    zPlatform* plat = (zPlatform*)coll->optr;
    xVec3 locnorm;
    xVec3 lochitsrc;
    xVec3 lochitvel;
    F32 hitdot;
    xVec3 hitsrcbot;
    xVec3 hitsrctop;
    xRay3 hitsrcray;
    xCollis hitsrccoll;
    S32 posX;
    S32 posZ;
    S32 direction;
    S32 stutter;

    if (plat->state != 0)
    {
        return 0;
    }

    if (worldSpaceNorm)
    {
        xMat3x3Tolocal(&locnorm, (xMat3x3*)plat->model->Mat, &coll->norm);
    }
    else
    {
        locnorm = coll->norm;
    }

    xMat4x3Tolocal(&lochitsrc, (xMat4x3*)plat->model->Mat, hitsource);
    xMat3x3Tolocal(&lochitvel, (xMat3x3*)plat->model->Mat, hitvel);

    xVec3Normalize(&locnorm, &locnorm);
    xVec3Normalize(&lochitvel, &lochitvel);

    hitdot = xVec3Dot(&locnorm, &lochitvel);

    hitsrcbot = lochitsrc;
    hitsrctop = lochitsrc;

    hitsrcbot.y -= 5.0f;
    hitsrctop.y += 5.0f;

    xMat4x3Toworld(&hitsrcbot, (xMat4x3*)plat->model->Mat, &hitsrcbot);
    xMat4x3Toworld(&hitsrctop, (xMat4x3*)plat->model->Mat, &hitsrctop);

    hitsrcray.origin.x = hitsrcbot.x;
    hitsrcray.origin.y = hitsrcbot.y;
    hitsrcray.origin.z = hitsrcbot.z;
    hitsrcray.dir.x = hitsrctop.x - hitsrcray.origin.x;
    hitsrcray.dir.y = hitsrctop.y - hitsrcray.origin.y;
    hitsrcray.dir.z = hitsrctop.z - hitsrcray.origin.z;
    hitsrcray.min_t = 0.0f;
    hitsrcray.max_t = xVec3Normalize(&hitsrcray.dir, &hitsrcray.dir);
    hitsrcray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;

    xRayHitsBound(&hitsrcray, &plat->bound, &hitsrccoll);

    if (!(hitsrccoll.flags & k_HIT_IT))
    {
        return 0;
    }

    if (xabs(lochitvel.x) < xabs(lochitvel.z))
    {
        posX = (lochitsrc.x > 0.0f);
        posZ = (lochitsrc.z > 0.0f);
        if ((posX ^ posZ) == 0)
        {
            direction = 1;
        }
        else
        {
            direction = -1;
        }
    }
    else
    {
        posX = (lochitsrc.x > 0.0f);
        posZ = (lochitsrc.z > 0.0f);
        if ((posX ^ posZ) == 0)
        {
            direction = -1;
        }
        else
        {
            direction = 1;
        }
    }

    stutter = 0;
    if (direction == 1)
    {
        if (!(plat->plat_asset->paddle.paddleFlags & 0x1) ||
            (!(plat->plat_asset->paddle.paddleFlags & 0x4) &&
             plat->ctr + 1 == plat->plat_asset->paddle.countOrient))
        {
            stutter = 1;
        }
    }
    if (direction == -1)
    {
        if (!(plat->plat_asset->paddle.paddleFlags & 0x2) ||
            (!(plat->plat_asset->paddle.paddleFlags & 0x4) && plat->ctr == 0))
        {
            stutter = 1;
        }
    }

    if (!stutter)
    {
        if (direction > 0)
        {
            zEntEvent(plat, eEventHit_PaddleLeft);
        }
        else
        {
            zEntEvent(plat, eEventHit_PaddleRight);
        }
    }

    zPlatform_PaddleStartRotate(plat, direction, stutter);

    return 1;
}

static void zPlatFM_Update(zPlatform* plat, xScene* sc, F32 dt)
{
    // Placeholder for a compiled-out debug statement: retail keeps this function's __FUNCTION__
    // string in .data with no code referencing it (Ratatouille's debug build uses it in asserts).
    // It generates no code; it only restores that data object.
    iDEBUG_EVAL_PLACEHOLDER(__FUNCTION__);

    for (S32 i = 0; i < 13; i++)
    {
        zPlatFMRunTime* fmrt = plat->fmrt;

        F32 tm = fmrt->tmrs[i];
        if (tm > 0.0f)
        {
            F32 ttm = fmrt->ttms[i];
            F32 atm = fmrt->atms[i];
            F32 dtm = fmrt->dtms[i];
            F32 vm = fmrt->vms[i];

            F32 ds;
            F32 etm = ttm - tm;

            if (tm > atm)
            {
                if (atm > 0.0f)
                {
                    if (tm - dt < atm)
                    {
                        if (dtm > 0.0f)
                        {
                            if (tm - dt < dtm)
                            {
                                if (tm - dt < 0.0f)
                                {
                                    F32 p = ttm - atm;
                                    F32 cfacc = 0.5f * (p + etm) * (p - etm) / p;
                                    F32 cfcs = atm - dtm;
                                    F32 cfdec = 0.5f * dtm;
                                    ds = cfacc + cfcs + cfdec;
                                }
                                else
                                {
                                    F32 p = ttm - atm;
                                    F32 cfacc = 0.5f * (p + etm) * (p - etm) / p;
                                    F32 cfcs = atm - dtm;
                                    F32 cfdec = 0.5f * (dtm + (tm - dt)) * (dtm - (tm - dt)) / dtm;
                                    ds = cfacc + cfcs + cfdec;
                                }
                            }
                            else
                            {
                                F32 p = ttm - atm;
                                F32 cfacc = 0.5f * (p + etm) * (p - etm) / p;
                                F32 cfcs = atm - (tm - dt);
                                ds = cfacc + cfcs;
                            }
                        }
                        else
                        {
                            if (tm - dt < 0.0f)
                            {
                                F32 p = ttm - atm;
                                F32 cfacc = 0.5f * (p + etm) * (p - etm) / p;
                                F32 cfcs = atm;
                                ds = cfacc + cfcs;
                            }
                            else
                            {
                                F32 p = ttm - atm;
                                F32 cfacc = 0.5f * (p + etm) * (p - etm) / p;
                                F32 cfcs = atm - (tm - dt);
                                ds = cfacc + cfcs;
                            }
                        }
                    }
                    else
                    {
                        F32 p = ttm - atm;
                        ds = (etm + 0.5f * dt) * dt / p;
                    }
                }
                else
                {
                    if (tm - dt < 0.0f)
                    {
                        ds = (etm + 0.5f * tm) * tm / ttm;
                    }
                    else
                    {
                        ds = (etm + 0.5f * dt) * dt / ttm;
                    }
                }
            }
            else
            {
                if (tm < dtm)
                {
                    if (tm - dt < 0.0f)
                    {
                        ds = 0.5f * tm * tm / dtm;
                    }
                    else
                    {
                        ds = (tm - 0.5f * dt) * dt / dtm;
                    }
                }
                else
                {
                    if (dtm > 0.0f)
                    {
                        if (tm - dt < dtm)
                        {
                            if (tm - dt < 0.0f)
                            {
                                F32 cfcs = tm - dtm;
                                F32 cfdec = 0.5f * dtm;
                                ds = cfcs + cfdec;
                            }
                            else
                            {
                                F32 cfcs = tm - dtm;
                                F32 cfdec = 0.5f * (dtm + (tm - dt)) * (dtm - (tm - dt)) / dtm;
                                ds = cfcs + cfdec;
                            }
                        }
                        else
                        {
                            ds = dt;
                        }
                    }
                    else
                    {
                        if (tm - dt < 0.0f)
                        {
                            ds = tm;
                        }
                        else
                        {
                            ds = dt;
                        }
                    }
                }
            }

            ds *= vm;
            fmrt->dss[i] -= ds;

            xMat4x3* pmat = (xMat4x3*)plat->model->Mat;
            xVec3 undoScale;
            // not a DWARF local: one loop-scope vector shared by cases 0-2 (retail keeps dead
            // stack copies of it)
            xVec3 dir;

            if (i == 0)
            {
                dir = pmat->right * (1.0f / pmat->right.length());
                pmat->pos += dir * ds;
            }
            else if (i == 1)
            {
                dir = pmat->up * (1.0f / pmat->up.length());
                pmat->pos += dir * ds;
            }
            else if (i == 2)
            {
                dir = pmat->at * (1.0f / pmat->at.length());
                pmat->pos += dir * ds;
            }
            else if (i == 3)
            {
                pmat->pos.x += ds;
            }
            else if (i == 4)
            {
                pmat->pos.y += ds;
            }
            else if (i == 5)
            {
                pmat->pos.z += ds;
            }
            else if (i == 6)
            {
                xMat3x3 preR;
                xMat3x3RotX(&preR, ds);
                xMat3x3Mul(pmat, &preR, pmat);
            }
            else if (i == 7)
            {
                xMat3x3 preR;
                xMat3x3RotY(&preR, ds);
                xMat3x3Mul(pmat, &preR, pmat);
            }
            else if (i == 8)
            {
                xMat3x3 preR;
                xMat3x3RotZ(&preR, ds);
                xMat3x3Mul(pmat, &preR, pmat);
            }
            else if (i == 9)
            {
                xMat3x3 postR;
                xMat3x3RotX(&postR, ds);
                xMat3x3Mul(pmat, pmat, &postR);
            }
            else if (i == 10)
            {
                xMat3x3 postR;
                xMat3x3RotY(&postR, ds);
                xMat3x3Mul(pmat, pmat, &postR);
            }
            else if (i == 11)
            {
                xMat3x3 postR;
                xMat3x3RotZ(&postR, ds);
                xMat3x3Mul(pmat, pmat, &postR);
            }
            else if (i == 12)
            {
                F32 scale_adjust = (1.0f + fmrt->total_change[i] - fmrt->dss[i]) /
                                   (1.0f + fmrt->total_change[i] - fmrt->dss[i] - ds);

                xVec3SMulBy((xVec3*)&plat->model->Mat->right, scale_adjust);
                xVec3SMulBy((xVec3*)&plat->model->Mat->up, scale_adjust);
                xVec3SMulBy((xVec3*)&plat->model->Mat->at, scale_adjust);
                xVec3SMulBy(&plat->frame->mat.right, scale_adjust);
                xVec3SMulBy(&plat->frame->mat.up, scale_adjust);
                xVec3SMulBy(&plat->frame->mat.at, scale_adjust);
            }

            fmrt->tmrs[i] -= dt;
            if (fmrt->tmrs[i] < 0.0f)
            {
                undoScale.x = xsqrt(SQR(pmat->right.x) + SQR(pmat->right.y) + SQR(pmat->right.z));
                undoScale.y = xsqrt(SQR(pmat->up.x) + SQR(pmat->up.y) + SQR(pmat->up.z));
                undoScale.z = xsqrt(SQR(pmat->at.x) + SQR(pmat->at.y) + SQR(pmat->at.z));

                {
                    F32 inv_t = 100.0f;
                    if (pmat->pos.x < 0.0f)
                    {
                        pmat->pos.x = 0.01f * (S32)(pmat->pos.x * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->pos.x = 0.01f * (S32)(pmat->pos.x * inv_t + 0.5f);
                    }
                    if (pmat->pos.y < 0.0f)
                    {
                        pmat->pos.y = 0.01f * (S32)(pmat->pos.y * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->pos.y = 0.01f * (S32)(pmat->pos.y * inv_t + 0.5f);
                    }
                    if (pmat->pos.z < 0.0f)
                    {
                        pmat->pos.z = 0.01f * (S32)(pmat->pos.z * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->pos.z = 0.01f * (S32)(pmat->pos.z * inv_t + 0.5f);
                    }
                }

                {
                    F32 inv_t = 100.0f;
                    if (pmat->right.x < 0.0f)
                    {
                        pmat->right.x = 0.01f * (S32)(pmat->right.x * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->right.x = 0.01f * (S32)(pmat->right.x * inv_t + 0.5f);
                    }
                    if (pmat->right.y < 0.0f)
                    {
                        pmat->right.y = 0.01f * (S32)(pmat->right.y * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->right.y = 0.01f * (S32)(pmat->right.y * inv_t + 0.5f);
                    }
                    if (pmat->right.z < 0.0f)
                    {
                        pmat->right.z = 0.01f * (S32)(pmat->right.z * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->right.z = 0.01f * (S32)(pmat->right.z * inv_t + 0.5f);
                    }
                }

                F32 len;
                xVec3NormalizeMacro(&pmat->right, &pmat->right, &len);

                {
                    F32 inv_t = 100.0f;
                    if (pmat->up.x < 0.0f)
                    {
                        pmat->up.x = 0.01f * (S32)(pmat->up.x * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->up.x = 0.01f * (S32)(pmat->up.x * inv_t + 0.5f);
                    }
                    if (pmat->up.y < 0.0f)
                    {
                        pmat->up.y = 0.01f * (S32)(pmat->up.y * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->up.y = 0.01f * (S32)(pmat->up.y * inv_t + 0.5f);
                    }
                    if (pmat->up.z < 0.0f)
                    {
                        pmat->up.z = 0.01f * (S32)(pmat->up.z * inv_t - 0.5f);
                    }
                    else
                    {
                        pmat->up.z = 0.01f * (S32)(pmat->up.z * inv_t + 0.5f);
                    }
                }

                xVec3NormalizeMacro(&pmat->up, &pmat->up, &len);

                F32 rdotu = -(pmat->right.x * pmat->up.x + pmat->right.y * pmat->up.y +
                              pmat->right.z * pmat->up.z);

                pmat->up.x += pmat->right.x * rdotu;
                pmat->up.y += pmat->right.y * rdotu;
                pmat->up.z += pmat->right.z * rdotu;

                xVec3NormalizeMacro(&pmat->up, &pmat->up, &len);

                pmat->at.x = pmat->right.y * pmat->up.z - pmat->up.y * pmat->right.z;
                pmat->at.y = pmat->right.z * pmat->up.x - pmat->up.z * pmat->right.x;
                pmat->at.z = pmat->right.x * pmat->up.y - pmat->up.x * pmat->right.y;
                pmat->right.x *= undoScale.x;
                pmat->right.y *= undoScale.x;
                pmat->right.z *= undoScale.x;
                pmat->up.x *= undoScale.y;
                pmat->up.y *= undoScale.y;
                pmat->up.z *= undoScale.y;
                pmat->at.x *= undoScale.z;
                pmat->at.y *= undoScale.z;
                pmat->at.z *= undoScale.z;

                fmrt->tmrs[i] = 0.0f;
                fmrt->flags &= ~(1 << i);
                fmrt->ttms[i] = 0.0f;
                fmrt->atms[i] = 0.0f;
                fmrt->dtms[i] = 0.0f;
                fmrt->vms[i] = 0.0f;
                fmrt->dss[i] = 0.0f;

                zEntEvent(plat, plat, eEventTranslLocalXDone + i);
            }
        }
    }
}

void zPlatformBreakaway_Update(zPlatform* plat, xScene* sc, F32 dt)
{
    if (plat->subType != ZPLATFORM_SUBTYPE_BREAKAWAY)
    {
        return;
    }

    switch (plat->state)
    {
    case 1:
    {
        plat->state = 2;

        if (plat->atbl != NULL)
        {
            xAnimState* ast = xAnimTableGetState(plat->atbl, "loop1");
            if (ast != NULL)
            {
                xAnimSingle* single = plat->model->Anim->Single;
                xAnimPlaySetState(single, ast, 0.0f);
                single->CurrentSpeed = 1.0f;
                xModelEval(plat->model);
            }

            plat->tmr = plat->plat_asset->ba.warningTime;
        }
        break;
    }
    case 2:
    {
        plat->tmr -= dt;
        if (plat->tmr <= 0.0f)
        {
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Collapse")), 0,
                        (xVec3*)&plat->model->Mat->pos, NULL, NULL, NULL, NULL);
            zPlatform_BreakawayFallFX(plat, dt);

            plat->state = 3;

            if (plat->bm != NULL)
            {
                plat->model = plat->bm;
                plat->collModel = plat->model;
                xMat4x3Copy((xMat4x3*)plat->bm->Mat, (xMat4x3*)plat->am->Mat);
                plat->bound.mat = (xMat4x3*)plat->model->Mat;
            }

            if (plat->atbl == NULL)
            {
                plat->tmr = 0.0f;
                zEntEvent(plat, eEventBreak);
                break;
            }

            xAnimState* ast = xAnimTableGetState(plat->atbl, "loop2");
            if (ast != NULL)
            {
                xAnimSingle* single = plat->model->Anim->Single;
                xAnimPlaySetState(single, ast, 0.0f);
                single->CurrentSpeed = 1.0f;
                xModelEval(plat->model);
                plat->tmr = single->State->Data->Duration;
            }
            else
            {
                plat->tmr = 0.0f;
            }

            zEntEvent(plat, eEventBreak);
        }
        break;
    }
    case 3:
    {
        plat->tmr -= dt;
        plat->elapsedTime += dt;
        if (plat->elapsedTime > plat->plat_asset->ba.collisionOffTime)
        {
            plat->chkby = XENT_COLLTYPE_NONE;
        }

        if (plat->collis->colls[0].flags & k_HIT_IT)
        {
            plat->state = 4;
            plat->pflags &= (U8) ~(0x2 | 0x4);
            plat->collis->chk = 0;
            xVec3Copy(&plat->frame->vel, &g_O3);
        }
        else if (plat->tmr <= 0.0f)
        {
            if (xBaseIsEnabled(plat))
            {
                plat->tmr = plat->plat_asset->ba.collapseIdleTime;
                plat->state = 4;

                if (plat->atbl != NULL)
                {
                    xAnimState* ast = xAnimTableGetState(plat->atbl, "loop3");
                    if (ast != NULL)
                    {
                        xAnimSingle* single = plat->model->Anim->Single;
                        xAnimPlaySetState(single, ast, 0.0f);
                        single->CurrentSpeed = 1.0f;
                        xModelEval(plat->model);
                    }
                }
            }
            else
            {
                zPlatform_Reset(plat, sc);
            }
        }
        else if (plat->plat_asset->ba.collapseIdleTime - plat->tmr >= 25.0f)
        {
            plat->state = 5;
            plat->pflags &= (U8) ~(0x2 | 0x4);
            plat->collis->chk = 0;
            xVec3Copy(&plat->frame->vel, &g_O3);
        }
        break;
    }
    case 4:
    {
        plat->tmr -= dt;
        plat->elapsedTime += dt;
        if (plat->elapsedTime > plat->plat_asset->ba.collisionOffTime)
        {
            plat->chkby = XENT_COLLTYPE_NONE;
        }

        if (plat->tmr <= 0.0f)
        {
            if (plat->atbl == NULL)
            {
                plat->tmr = 0.0f;
                plat->state = 5;
                break;
            }

            xAnimState* ast = xAnimTableGetState(plat->atbl, "loop4");
            if (ast != NULL)
            {
                xAnimSingle* single = plat->model->Anim->Single;
                xAnimPlaySetState(single, ast, 0.0f);
                single->CurrentSpeed = 1.0f;
                xModelEval(plat->model);
                plat->tmr = single->State->Data->Duration;
            }
            else
            {
                plat->tmr = 0.0f;
            }

            plat->state = 5;
        }
        break;
    }
    case 5:
    {
        plat->tmr -= dt;
        if (plat->tmr <= 0.0f)
        {
            if (plat->atbl == NULL)
            {
                plat->tmr = 0.0f;
                plat->state = 0;
                plat->elapsedTime = 0.0f;
                plat->pflags &= (U8) ~(0x2 | 0x4);
                plat->collis->chk = 0;
                plat->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
                break;
            }

            xAnimState* ast = xAnimTableGetState(plat->atbl, "idle");
            if (ast != NULL)
            {
                xAnimSingle* single = plat->model->Anim->Single;
                xAnimPlaySetState(single, ast, 0.0f);
                single->CurrentSpeed = 1.0f;
                xModelEval(plat->model);
            }

            plat->state = 0;
            plat->tmr = 0.0f;
            plat->elapsedTime = 0.0f;
            plat->pflags &= (U8) ~(0x2 | 0x4);
            plat->collis->chk = 0;
            plat->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
        }
        break;
    }
    }
}

void zPlatform_Update(xEnt* entplat, xScene* sc, F32 dt)
{
    zPlatform* plat = (zPlatform*)entplat;
    U16 state;
    xEntDrive* drv;

    xEntOpacityUpdate(&plat->opacity, plat, dt);

    xEntUpdate(plat, sc, dt);

    if (plat->destructible != NULL)
    {
        zDestructibleAsset_Update(plat, plat->destructible, sc, dt);
    }

    drv = &globals.player.drv;
    switch (plat->subType)
    {
    case ZPLATFORM_SUBTYPE_PLATFORM:
        plat->state = plat->motion.er.state;
        break;
    case ZPLATFORM_SUBTYPE_BREAKAWAY:
        zPlatformBreakaway_Update(plat, sc, dt);
        break;
    case ZPLATFORM_SUBTYPE_MECH:
        state = plat->motion.mech.state;
        if (state != plat->state)
        {
            xEntMotionMechData* mkasst = &plat->motion.asset->mech;
            if (plat->state == 2)
            {
                zEntEvent(plat, eEventArriveHalfway);
            }
            else if (state == 6 || state == 7 ||
                     (!(mkasst->flags & 0x2) && (state == 0 || state == 1)))
            {
                zEntEvent(plat, eEventArrive);
            }
            plat->state = state;
        }
        break;
    case ZPLATFORM_SUBTYPE_SPRINGBOARD:
        plat->tmr -= dt;
        break;
    case ZPLATFORM_SUBTYPE_CONVBELT:
        if (plat == drv->old_driver || plat == drv->driver)
        {
            F32 s = (plat == drv->driver) ? drv->s : drv->os;
            xEnt* p = drv->driven;
            xVec3SMul(&p->frame->dpos, (xVec3*)&plat->model->Mat->right,
                      s * plat->plat_asset->cb.speed * dt);
            xVec3AddTo((xVec3*)&p->model->Mat->pos, &p->frame->dpos);
        }
        break;
    case ZPLATFORM_SUBTYPE_TEETER:
        if (plat != drv->old_driver && plat != drv->driver)
        {
            F32 ctilt = plat->frame->rot.axis.z;
            F32 itilt = plat->asset->ang.z;
            F32 dtilt = xabs((PI / 180.0f) * plat->plat_asset->teet.invmass * dt);
            if (ctilt != itilt)
            {
                if (ctilt < itilt)
                {
                    dtilt = MIN(dtilt, itilt - ctilt);
                }
                else if (ctilt > itilt)
                {
                    dtilt = MAX(-dtilt, itilt - ctilt);
                }
                plat->frame->rot.axis.z += dtilt;
                xMat3x3Euler((xMat3x3*)plat->model->Mat, plat->frame->rot.axis.x,
                             plat->frame->rot.axis.y, plat->frame->rot.axis.z);
                xVec3* scale = &plat->asset->scale;
                xVec3SMulBy((xVec3*)&plat->model->Mat->right, scale->x);
                xVec3SMulBy((xVec3*)&plat->model->Mat->up, scale->y);
                xVec3SMulBy((xVec3*)&plat->model->Mat->at, scale->z);
            }
        }
        else
        {
            F32 s = (plat == drv->driver) ? drv->s : drv->os;
            xVec3 lpos;
            xMat4x3Tolocal(&lpos, (xMat4x3*)plat->model->Mat, &drv->driven->frame->mat.pos);
            F32 cangle = plat->frame->rot.axis.z;
            F32 dangle = -s * ((PI / 180.0f) * plat->plat_asset->teet.invmass) * lpos.x * dt;
            F32 mangle = xabs(plat->plat_asset->teet.maxtilt);
            F32 eangle = dangle + cangle;
            F32 tangle = xabs(eangle) - mangle;
            if (tangle > 0.0f)
            {
                if (dangle < 0.0f)
                {
                    dangle += tangle;
                }
                else if (dangle > 0.0f)
                {
                    dangle -= tangle;
                }
            }
            plat->frame->rot.axis.z += dangle;
            xMat3x3Euler((xMat3x3*)plat->model->Mat, plat->frame->rot.axis.x,
                         plat->frame->rot.axis.y, plat->frame->rot.axis.z);
            xVec3* scale = &plat->asset->scale;
            xVec3SMulBy((xVec3*)&plat->model->Mat->right, scale->x);
            xVec3SMulBy((xVec3*)&plat->model->Mat->up, scale->y);
            xVec3SMulBy((xVec3*)&plat->model->Mat->at, scale->z);
        }
        break;
    case ZPLATFORM_SUBTYPE_PADDLE:
        if (plat->tmr)
        {
            plat->tmr -= dt;
            if (plat->tmr < 0.0f)
            {
                plat->tmr = 0.0f;
                plat->state = 0;
            }
            F32 time[3];
            F32 orient = (PI / 180.0f) * SolvePaddleMotion(plat, time, plat->tmr);
            xMat3x3 orientrot;
            xMat3x3RotC(&orientrot, 0.0f, 1.0f, 0.0f, orient);
            xMat3x3 origrot;
            xMat3x3Euler(&origrot, plat->asset->ang.x, plat->asset->ang.y, plat->asset->ang.z);
            xMat3x3Mul((xMat3x3*)plat->model->Mat, &orientrot, &origrot);
            xVec3* scale = &plat->asset->scale;
            xVec3SMulBy((xVec3*)&plat->model->Mat->right, scale->x);
            xVec3SMulBy((xVec3*)&plat->model->Mat->up, scale->y);
            xVec3SMulBy((xVec3*)&plat->model->Mat->at, scale->z);
        }
        break;
    case ZPLATFORM_SUBTYPE_FM:
        if (!(plat->plat_flags & 0x4))
        {
            zPlatFM_Update(plat, sc, dt);
        }
        break;
    }

    if (plat->motion.type == XENTMOTION_TYPE_MP)
    {
        xMovePoint* src = plat->motion.mp.src;
        if (src != plat->src)
        {
            zEntEvent(plat, src, eEventArrive);
            plat->src = src;
        }
    }

    if (plat->motion.type == XENTMOTION_TYPE_SPL && plat->motion.spl.arrived)
    {
        zEntEvent(plat, eEventArrive);
    }

    S32 moving;
    xVec3* opos = &plat->frame->oldmat.pos;
    xVec3* pos = &plat->frame->mat.pos;
    xVec3* orot = &plat->frame->oldrot.axis;
    xVec3* rot = &plat->frame->rot.axis;
    if (plat->subType == ZPLATFORM_SUBTYPE_TEETER)
    {
        if (orot->x != rot->x || orot->y != rot->y || orot->z != rot->z)
        {
            moving = TRUE;
        }
        else
        {
            moving = FALSE;
        }
    }
    else if (plat->subType == ZPLATFORM_SUBTYPE_PADDLE)
    {
        if (plat->tmr != 0.0f)
        {
            moving = TRUE;
        }
        else
        {
            moving = FALSE;
        }
    }
    else
    {
        if (opos->x != pos->x || opos->y != pos->y || opos->z != pos->z || orot->x != rot->x ||
            orot->y != rot->y || orot->z != rot->z)
        {
            moving = TRUE;
        }
        else
        {
            moving = FALSE;
        }
    }

    if (moving != plat->moving)
    {
        if (moving)
        {
            zEntEvent(plat, eEventStartMoving);
        }
        else
        {
            if (plat->model->modelID == 0x383C7D8C)
            {
                xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("LOCK_SGRP")), 0,
                            (xVec3*)&plat->model->Mat->pos, NULL, NULL, NULL, NULL);
            }
            zEntEvent(plat, eEventStopMoving);
        }
    }

    plat->moving = moving;

    if (GG02::specialsExist && plat->id == 0x3EFFAC8C)
    {
        if (NMEC_ds2_toCam((xVec3*)&plat->model->Mat->pos, NULL) > 10000.0f)
        {
            if (plat->simpShadow != NULL)
            {
                NMEC_SimpShadowCacheRelease(plat->simpShadow);
            }
            plat->simpShadow = NULL;
        }
        else if (plat->simpShadow == NULL)
        {
            plat->simpShadow = NMEC_SimpShadowCacheReserve();
            if (plat->simpShadow != NULL)
            {
                xShadowSimple_CacheInit_Expert(plat->simpShadow, GG02::rast_goofyShadow, 0x7F);
                plat->simpShadow->tol_movement = 0.25f;
                plat->simpShadow->radiusOptional = 2.25f;
            }
        }
    }
}

void zPlatform_Move(xEnt* entplat, xScene* s, F32 dt, xEntFrame* frame)
{
    zPlatform* plat = (zPlatform*)entplat;

    xEntMotionMove(&plat->motion, s, dt, frame);
    xEntDriveUpdate(&plat->drv, s, dt, NULL);
}

void zPlatform_Shake(zPlatform* plat, F32 _unused, F32 ampl, F32 freq)
{
    xFFXShakeState* ss;
    xFFX* sfkt;

    ss = xFFXShakeAlloc();
    if (ss != NULL)
    {
        sfkt = xFFXAlloc();
        if (sfkt == NULL)
        {
            xFFXShakeFree(ss);
        }
        else
        {
            ss->disp.x = 0.0f;
            ss->disp.y = -ampl;
            ss->disp.z = 0.0f;
            ss->dur = 1.0f;
            ss->alpha = -7.0f / ss->dur;
            ss->freq = freq;
            ss->tmr = 0.0f;
            sfkt->doEffect = xFFXShakeUpdateEnt;
            sfkt->fdata = ss;
            xFFXAddEffect(plat, sfkt);
        }
    }
}

static void zPlatform_BreakawayFallFX(zPlatform* ent, F32 dt)
{
    if (sEmitBreakaway != NULL)
    {
        xParEmitterCustomSettings info;
        info.custom_flags = 0x100;
        info.pos = *xEntGetCenter(ent);
        info.pos.y += 0.5f;

        for (S32 i = 0; i < 25; i++)
        {
            xParEmitterEmitCustom(sEmitBreakaway, 1.0f / 30.0f, &info);
        }
    }
}

void zPlatform_Mount(zPlatform* plat)
{
    if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY ||
        plat->subType == ZPLATFORM_SUBTYPE_SPRINGBOARD)
    {
        if (plat->state == 0)
        {
            plat->state = 1;
        }

        if (sRumbleBreakaway != NULL && (plat->state == 0 || plat->state == 1))
        {
            zEntEvent(sRumbleBreakaway, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                      0.0f, 0.0f);
        }

        if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY)
        {
            plat->sndID = xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("CollapseWarn")), 0,
                                      (xVec3*)&plat->model->Mat->pos, NULL, NULL, NULL, NULL);
        }
    }
}

void zPlatform_Dismount(zPlatform* plat)
{
    if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY ||
        plat->subType == ZPLATFORM_SUBTYPE_SPRINGBOARD)
    {
        if (plat->state == 1 ||
            (plat->subType == ZPLATFORM_SUBTYPE_SPRINGBOARD && plat->state == 2))
        {
            plat->state = 0;
        }

        if (sRumbleBreakaway != NULL)
        {
            zEntEvent(sRumbleBreakaway, eEventStopRumbleEffect, globals.currentActivePad, 0.0f,
                      0.0f, 0.0f);

            xSndMgrStop(plat->sndID);
            plat->sndID = (iSndHandle)-1;
        }
    }
}

static void zPlatformTranslate(xEnt* xent, xVec3* dpos, xMat4x3* dmat)
{
    zPlatform* plat = (zPlatform*)xent;

    xEntDefaultTranslate(xent, dpos, dmat);
    xEntMotionTranslate(&plat->motion, dpos, dmat);
}

static void zPlatFM_EventSetup(zPlatform* plat, const F32* toParam, S32 idx, U32 mask)
{
    zPlatFMRunTime* fmrt = plat->fmrt;
    F32 ds = toParam[0];
    F32 ttm = toParam[1];
    F32 atm = toParam[2];
    F32 dtm = toParam[3];

    if (ttm <= 0.0f)
    {
        return;
    }

    if (atm < 0.0f)
    {
        atm = 0.0f;
    }
    else if (atm > ttm)
    {
        atm = ttm;
    }

    atm = ttm - atm;

    if (dtm < 0.0f)
    {
        dtm = 0.0f;
    }
    else if (dtm > atm)
    {
        dtm = atm;
    }

    fmrt->tmrs[idx] = ttm;
    fmrt->ttms[idx] = ttm;
    fmrt->atms[idx] = atm;
    fmrt->dtms[idx] = dtm;
    fmrt->vms[idx] = 2.0f * ds / (atm + (ttm - dtm));
    fmrt->dss[idx] = ds;
    fmrt->total_change[idx] = ds;
    fmrt->flags |= mask;
}

void zPlatformEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                      U32 toParamWidgetID)
{
    zPlatform* plat = (zPlatform*)to;

    if (plat->destructible && from != to)
    {
        zDestructibleAsset_EventCB(plat, from, plat->destructible, toEvent, toParam, toParamWidget,
                                   toParamWidgetID);
    }

    switch (toEvent)
    {
    case eEventOn:
        if (!(plat->plat_flags & 0x1))
        {
            plat->plat_flags |= 0x1;
        }
        break;
    case eEventOff:
        if (plat->plat_flags & 0x1)
        {
            plat->plat_flags &= (U16)~0x1;
        }
        break;
    case eEventToggle:
        if (plat->plat_flags & 0x1)
        {
            plat->plat_flags &= (U16)~0x1;
        }
        else
        {
            plat->plat_flags |= 0x1;
        }
        break;
    case eEventRun:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            plat->plat_flags &= (U16)~0x4;
        }
        else if (plat->subType == ZPLATFORM_SUBTYPE_PADDLE)
        {
            if (toParam)
            {
                S32 iParam0 = (S32)toParam[0];
                if (iParam0)
                {
                    if (plat->state == 0 &&
                        ((plat->plat_asset->paddle.paddleFlags & 0x4) ||
                         !((iParam0 < 0) ?
                               (plat->ctr == 0) :
                               (plat->ctr + 1 == plat->plat_asset->paddle.countOrient))))
                    {
                        zPlatform_PaddleStartRotate(plat, iParam0, (S32)toParam[1]);
                    }
                }
                else
                {
                    S32 destctr = (S32)toParam[1];
                    if (destctr >= 0 && destctr < plat->plat_asset->paddle.countOrient)
                    {
                        plat->tmr = 1e-9f;
                        plat->state = 2;
                        plat->ctr = destctr;
                    }
                }
            }
        }
        else
        {
            if (plat->subType == ZPLATFORM_SUBTYPE_MECH &&
                (plat->motion.mech.state == 6 || plat->motion.mech.state == 7))
            {
                xEntMotionReset(&plat->motion, globals.sceneCur);
            }
            xEntMotionRun(&plat->motion);
        }
        break;
    case eEventStop:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            plat->plat_flags |= 0x4;
        }
        else
        {
            xEntMotionStop(&plat->motion);
        }
        break;
    case eEventTranslLocalX:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            zPlatFM_EventSetup(plat, toParam, 0, 0x1);
        }
        break;
    case eEventTranslLocalY:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            zPlatFM_EventSetup(plat, toParam, 1, 0x2);
        }
        break;
    case eEventTranslLocalZ:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            zPlatFM_EventSetup(plat, toParam, 2, 0x4);
        }
        break;
    case eEventTranslWorldX:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            zPlatFM_EventSetup(plat, toParam, 3, 0x8);
        }
        break;
    case eEventTranslWorldY:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            zPlatFM_EventSetup(plat, toParam, 4, 0x10);
        }
        break;
    case eEventTranslWorldZ:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            zPlatFM_EventSetup(plat, toParam, 5, 0x20);
        }
        break;
    case eEventTransToAbsoluteX:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = toParam[0] - plat->model->Mat->pos.x;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 3, 0x8);
        }
        break;
    case eEventTransToAbsoluteY:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = toParam[0] - plat->model->Mat->pos.y;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 4, 0x10);
        }
        break;
    case eEventTransToAbsoluteZ:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = toParam[0] - plat->model->Mat->pos.z;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 5, 0x20);
        }
        break;
    case eEventRotLocalX:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = (PI / 180.0f) * toParam[0];
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 6, 0x40);
        }
        break;
    case eEventRotLocalY:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = (PI / 180.0f) * toParam[0];
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 7, 0x80);
        }
        break;
    case eEventRotLocalZ:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = (PI / 180.0f) * toParam[0];
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 8, 0x100);
        }
        break;
    case eEventRotWorldX:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = (PI / 180.0f) * toParam[0];
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 9, 0x200);
        }
        break;
    case eEventRotWorldY:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = (PI / 180.0f) * toParam[0];
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 10, 0x400);
        }
        break;
    case eEventRotWorldZ:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = (PI / 180.0f) * toParam[0];
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 11, 0x800);
        }
        break;
    case eEventRotToAbsoluteX:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 desired_angle = xAngleClampFast((PI / 180.0f) * toParam[0]);
            F32 delta_angle = xAngleClampFast(
                desired_angle - xatan2(plat->model->Mat->at.x, plat->model->Mat->at.z));
            if (delta_angle > PI)
            {
                delta_angle -= 2.0f * PI;
            }

            F32 tp[4];
            tp[0] = delta_angle;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 9, 0x200);
        }
        break;
    case eEventRotToAbsoluteY:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 desired_angle = xAngleClampFast((PI / 180.0f) * toParam[0]);
            F32 delta_angle = xAngleClampFast(
                desired_angle - xatan2(plat->model->Mat->at.x, plat->model->Mat->at.z));
            if (delta_angle > PI)
            {
                delta_angle -= 2.0f * PI;
            }

            F32 tp[4];
            tp[0] = delta_angle;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 10, 0x400);
        }
        break;
    case eEventRotToAbsoluteZ:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 desired_angle = xAngleClampFast((PI / 180.0f) * toParam[0]);
            F32 delta_angle = xAngleClampFast(
                desired_angle - xatan2(plat->model->Mat->right.y, plat->model->Mat->right.x));
            if (delta_angle > PI)
            {
                delta_angle -= 2.0f * PI;
            }

            F32 tp[4];
            tp[0] = delta_angle;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 11, 0x800);
        }
        break;
    case eEventScale:
        if (plat->subType == ZPLATFORM_SUBTYPE_FM)
        {
            F32 tp[4];
            tp[0] = toParam[0] - 1.0f;
            tp[1] = toParam[1];
            tp[2] = toParam[2];
            tp[3] = toParam[3];
            zPlatFM_EventSetup(plat, tp, 12, 0x1000);
        }
        break;
    case eEventForward:
        if (plat->subType == ZPLATFORM_SUBTYPE_MECH)
        {
            xEntMechForward(&plat->motion);
        }
        break;
    case eEventReverse:
        xEntMotionReverse(&plat->motion);
        break;
    case eEventReset:
        zPlatform_Reset(plat, globals.sceneCur);
        break;
    case eEventVisible:
    case eEventFastVisible:
        xEntShow(plat);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOn(*plat, toParam[1], toParam[2]);
        }
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(plat);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOff(*plat, toParam[1], toParam[2]);
        }
        break;
    case eEventCollision_Visible_On:
        xEntShow(plat);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOn(*plat, toParam[1], toParam[2]);
        }
        // fallthrough
    case eEventCollisionOn:
        plat->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        plat->bupdate(plat, (xVec3*)&plat->model->Mat->pos);
        break;
    case eEventSpawnBubblesOn:
        zFXBubbleSpawnStart(*plat, toParam[0]);
        break;
    case eEventSpawnBubblesOff:
        zFXBubbleSpawnStop(*plat);
        break;
    case eEventCollision_Visible_Off:
        xEntHide(plat);
        if (toParam && (S32)(0.5f + toParam[0]) == 77)
        {
            zFXPopOff(*plat, toParam[1], toParam[2]);
        }
        // fallthrough
    case eEventCollisionOff:
        plat->chkby = XENT_COLLTYPE_NONE;
        break;
    case eEventCollisionReset:
        if (!(plat->plat_asset->flags & 0x4))
        {
            plat->chkby = XENT_COLLTYPE_NONE;
        }
        else
        {
            plat->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        }
        plat->bupdate(plat, (xVec3*)&plat->model->Mat->pos);
        break;
    case eEventCameraCollidePartial:
        zCollGeom_CamEnable(plat);
        plat->plat_flags |= 0x8;
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable(plat);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable(plat);
        break;
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    case eEventAnimStop:
    case eEventAnimPause:
    case eEventAnimResume:
    case eEventAnimTogglePause:
    case eEventAnimPlayRandom:
    case eEventAnimPlayMaybe:
        zEntAnimEvent(plat, toEvent, toParam);
        break;
    case eEventSetSpeed:
        xEntMotionSetSpeed(&plat->motion, toParam[0]);
        break;
    case eEventIncreaseSpeed:
        xEntMotionSetSpeed(&plat->motion, toParam[0] + xEntMotionGetSpeed(&plat->motion));
        break;
    case eEventDecreaseSpeed:
        xEntMotionSetSpeed(&plat->motion, xEntMotionGetSpeed(&plat->motion) - toParam[0]);
        break;
    case eEventAccelerate:
        xEntMPAccelerate(&plat->motion, toParam[0], toParam[1]);
        break;
    case eEventSetSkyDome:
        xSkyDome_AddEntity(plat, (S32)toParam[0], (S32)toParam[1], toParam[2] == 0.0f);
        break;
    case eEventGooSetWarb:
        zFXGooEventSetWarb(plat, toParam);
        break;
    case eEventGooSetFreezeDuration:
        zFXGooEventSetFreezeDuration(plat, toParam[0]);
        break;
    case eEventGooMelt:
        zFXGooEventMelt(plat);
        break;
    case eEventSetAsBounceBack:
        plat->miscflags |= 0x20;
        break;
    case eEventSetAsBounceBack_Cancel:
        plat->miscflags &= (U8)~0x20;
        break;
    case eEventMount:
        if (toParamWidget)
        {
            F32 mt = 0.00001f;
            if (toParam[0])
            {
                mt = toParam[0];
            }
            xEntDriveMount(&plat->drv, (xEnt*)toParamWidget, mt, NULL);
        }
        if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY && (plat->state == 0 || plat->state == 1))
        {
            plat->state = 1;
        }
        break;
    case eEventDismount:
        if (toParamWidget)
        {
            F32 mt = 0.00001f;
            if (toParam[0])
            {
                mt = toParam[0];
            }
            xEntDriveDismount(&plat->drv, mt);
        }
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr)
        {
            if (toParam[0] <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, plat, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                FloatAndVoid dist;
                dist.f = SQR(toParam[0]);
                xUpdateCull_SetCB(globals.updateMgr, plat, xUpdateCull_DistanceSquaredCB, dist.v);
            }
        }
        break;
    case eEventLaunchShrapnel:
    {
        zShrapnelAsset* shrap = (zShrapnelAsset*)toParamWidget;
        if (shrap && shrap->initCB)
        {
            xVec3 currVel;
            xVec3Sub(&currVel, &plat->frame->mat.pos, &plat->frame->oldmat.pos);
            xVec3SMulBy(&currVel, 1.0f / globals.update_dt);
            shrap->initCB(shrap, plat->model, &currVel, NULL);
        }
        break;
    }
    case eEventSetLightKit:
        xModelSetLightKit(plat->model, (xLightKit*)toParamWidget);
        break;
    case eEventSetOpacity:
        if (toParam[0] >= 0.0f && plat->model)
        {
            plat->model->Alpha = toParam[0];
        }
        plat->opacity.delta_opacity = toParam[1];
        plat->opacity.delta_opacity_2 = toParam[2];
        break;
    // Both are explicit no-op cases in retail's compare tree.
    case eEventBreak:
    case eEventDispatcher_UserSelectionReset:
        break;
    case eEventLaunchFireWorks:
        if (toParam)
        {
            U32 numFireworks = (toParam[3] == 0.0f) ? 10.0f : toParam[3];
            for (U32 i = 0; i < numFireworks; i++)
            {
                xVec3 position;
                if (to)
                {
                    position = *xEntGetCenter(plat);
                    position -= *(xVec3*)&plat->model->Mat->pos;
                    xMat3x3Tolocal(&position, (xMat3x3*)plat->model->Mat, &position);
                    position.x += toParam[0];
                    position.y += toParam[1];
                    position.z += toParam[2];
                    xMat4x3Toworld(&position, (xMat4x3*)plat->model->Mat, &position);
                }
                else
                {
                    position.x = toParam[0];
                    position.y = toParam[1];
                    position.z = toParam[2];
                }
                xFXFireworksLaunch(0.5f * xurand(), &position, 0.5f * xurand() + 0.1f);
            }
        }
        else
        {
            xEnt* ent = (xEnt*)toParamWidget;
            for (U32 i = 0; i < 10; i++)
            {
                xVec3 position;
                if (to)
                {
                    position = *xEntGetCenter(ent);
                }
                else
                {
                    position.assign(0.0f, 0.0f, 0.0f);
                }
                xFXFireworksLaunch(0.5f * xurand(), &position, 0.5f * xurand() + 0.1f);
            }
        }
        break;
    }
}

U8 zPlatform_UsePartialCameraCollision(zPlatform* ent)
{
    return (ent->plat_flags >> 3) & 0x1;
}

static void zPlatform_GoofyRender(xEnt* ent)
{
    S32 cull_shad;
    xVec3 vec;
    xSphere* sph_world;
    S32 cull_mod;

    if (ent->model == NULL)
    {
        return;
    }

    if (!xEntIsVisible(ent))
    {
        return;
    }

    if (ent->model->Flags & 0x400)
    {
        return;
    }

    if (ent->flags & 0x40)
    {
        xModelRender(ent->model);
        return;
    }

    if (ent->simpShadow == NULL)
    {
        xModelRender(ent->model);
        return;
    }

    vec.x = ent->model->Mat->pos.x;
    vec.y = ent->model->Mat->pos.y - 25.0f;
    vec.z = ent->model->Mat->pos.z;

    sph_world = iModelWorldBoundSphere(ent->model->Data, ent->model->Mat, NULL);
    cull_mod = xModelSphereCullPlusShadow(sph_world, &vec, &cull_shad);

    if (!cull_mod)
    {
        xModelRender(ent->model);
    }

    if (!cull_shad)
    {
        xShadowSimple_Add_Expert(ent->simpShadow, (xMat4x3*)ent->model->Mat, 127, 2.25f, 25.0f,
                                 1.0f);
    }
}
