#include "zThrown.h"

#include <string.h>

#include "xEntBoulder.h"
#include "xEvent.h"
#include "xMath2.h"
#include "xMathInlines.h"
#include "xString.h"
#include "xstransvc.h"
#include "zBase.h"
#include "zEntButton.h"
#include "zEntPlayer.h"
#include "zFX.h"
#include "zGlobals.h"
#include "zGoo.h"
#include "zNMECommon.h"
#include "zNPCTypes.h"
#include "zScene.h"

static void zThrownCollide_StoneTiki(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                     F32* friction);
static void zThrownCollide_Tiki(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                F32* friction);
static void zThrownCollide_DestructObj(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                       F32* friction);
static void zThrownCollide_ThrowFreeze(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                       F32* friction);
static void zThrownCollide_ThrowFruit(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                      F32* friction);
static S32 zThrownCollide_CauseDamage(zThrownStruct* thrown, xEntCollis* collis);
static void zThrown_Update(xEnt* ent, xScene* sc, F32 dt);
static void zFruit_Update(xEnt* ent, xScene* sc, F32 dt);

extern xEnt* gReticleTarget[2];

static zThrownStruct zThrownList[32];
static U32 zThrownCount;
static U32 sThrowButtonMask;
static F32 sSNDLandTimer;
static LaunchStats l_normal = { 10.0f, 15.0f, 0.2f };
static CarryableStats c_normal = {};
static CarryableStats c_fruit = { 24.0f };
static U32 sFruitIsFreezy;

static ThrowableStats zThrowableModels[21] = {
    { "__default__", zThrownCollide_DestructObj, &c_normal, &l_normal, {}, NULL, 0.0f, 0, 0, NULL },
    { "reallybigrock", zThrownCollide_DestructObj, &c_normal, &l_normal, {}, NULL, 0.0f, 0, 0, NULL },
    { "b3_roof_chunk_sm_a",
      zThrownCollide_DestructObj,
      &c_normal,
      &l_normal,
      {},
      NULL,
      0.0f,
      0,
      0,
      NULL },
    { "b3_roof_chunk_sm_b",
      zThrownCollide_DestructObj,
      &c_normal,
      &l_normal,
      {},
      NULL,
      0.0f,
      0,
      0,
      NULL },
    { "b3_roof_chunk_sm_c",
      zThrownCollide_DestructObj,
      &c_normal,
      &l_normal,
      {},
      NULL,
      0.0f,
      0,
      0,
      NULL },
    { "b3_roof_chunk_sm_d",
      zThrownCollide_DestructObj,
      &c_normal,
      &l_normal,
      {},
      NULL,
      0.0f,
      0,
      0,
      NULL },
    { "b3_roof_chunk_sm_e",
      zThrownCollide_DestructObj,
      &c_normal,
      &l_normal,
      {},
      NULL,
      0.0f,
      0,
      0,
      NULL },
    { "b3_roof_chunk_sm_f",
      zThrownCollide_DestructObj,
      &c_normal,
      &l_normal,
      {},
      NULL,
      0.0f,
      0,
      0,
      NULL },
    { "crate_wood_bind", zThrownCollide_Tiki, &c_normal, &l_normal, {}, NULL, 0.0f, 0, 0, NULL },
    { "crate_explode_bind", zThrownCollide_Tiki, &c_normal, &l_normal, {}, NULL, 0.0f, 0, 0, NULL },
    { "crate_steel_bind", zThrownCollide_StoneTiki, &c_normal, &l_normal, {}, NULL, 0.0f, 0, 0, NULL },
    { "fruit_freezy",
      zThrownCollide_ThrowFreeze,
      &c_fruit,
      &l_normal,
      {},
      "fruit_freezy_shrapnel",
      0.77f,
      0,
      0,
      NULL },
    { "fruit_freezy_bind",
      zThrownCollide_ThrowFreeze,
      &c_fruit,
      &l_normal,
      {},
      "fruit_freezy_shrapnel",
      0.77f,
      0,
      0,
      NULL },
    { "fruit_glow_blue",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 32, 32, 128, 255 },
      NULL,
      0.92f,
      0,
      0,
      NULL },
    { "fruit_glow_green",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 32, 128, 32, 255 },
      NULL,
      0.92f,
      0,
      0,
      NULL },
    { "fruit_glow_orange",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 128, 96, 32, 255 },
      NULL,
      0.92f,
      0,
      0,
      NULL },
    { "fruit_glow_red",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 128, 32, 32, 255 },
      NULL,
      0.92f,
      0,
      0,
      NULL },
    { "fruit_glow_yellow",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 128, 128, 32, 255 },
      NULL,
      0.92f,
      0,
      0,
      NULL },
    { "fruit_throw",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 32, 32, 32, 255 },
      "fruit_throw_shrapnel",
      0.92f,
      0,
      0,
      NULL },
    { "fruit_throw_bind",
      zThrownCollide_ThrowFruit,
      &c_fruit,
      &l_normal,
      { 32, 32, 32, 255 },
      "fruit_throw_shrapnel",
      0.92f,
      0,
      0,
      NULL },
    {},
};

static S32 sDebugDepth;
static F32 sLastThrownDistance;

// No GC symbol (always inlined into zThrown_AddFruit and zThrown_LaunchVel); name from BfBB.
static inline void zThrown_AddTempFrame(zThrownStruct* thrown)
{
    xEnt* ent = thrown->ent;
    if (ent->frame == NULL)
    {
        memset(&thrown->frame, 0, sizeof(xEntFrame));
        ent->frame = &thrown->frame;
        ent->frame->mat = *(xMat4x3*)ent->model->Mat;
    }
}

// No GC symbol: inlined into zSceneExit, which only clears the thrown list. Name from BfBB.
inline void zThrown_Reset()
{
    zThrownCount = 0;
}

// No GC symbol: inlined into zSceneReset. BfBB's zThrown_Reset body (plus newUpdate); the name
// is ours, since zThrown_Reset is the count-only clear above.
inline void zThrown_ResetAll()
{
    for (U32 i = 0; i < zThrownCount; i++)
    {
        zThrownList[i].ent->update = zThrownList[i].oldupdate;
        zThrownList[i].newUpdate = 0;
        zThrownList[i].ent->baseFlags &= 0xff7f;
        if (zThrownList[i].ent->frame == &zThrownList[i].frame)
        {
            zThrownList[i].ent->frame = NULL;
        }

        xModelInstance* mod = zThrownList[i].ent->model;
        mod->RedMultiplier = mod->GreenMultiplier = mod->BlueMultiplier = 1.0f;
        zThrownList[i].ent->chkby |= XENT_COLLTYPE_PLYR;
        zThrownList[i].ent->baseFlags |= (U16)zThrownList[i].oldRecShadow;
    }

    zThrownCount = 0;
}

void zThrown_Setup(zScene* sc)
{
    ThrowableStats* stats;
    char tmpstr[256];
    S32 i;
    xEnt* myent;

    c_fruit.killTimer = globals.player.carry.fruitLifetime;

    F32 airTime =
        xsqrt(2.0f * globals.player.carry.throwHeight / globals.player.carry.throwGravity);
    l_normal.throwSpeedY = globals.player.carry.throwGravity * airTime;
    l_normal.throwSpeedXZ = globals.player.carry.throwDistance / (2.0f * airTime);

    for (stats = zThrowableModels; stats->name != NULL; stats++)
    {
        stats->nameHash = xStrHash(stats->name);
        strcpy(tmpstr, stats->name);
        strcat(tmpstr, ".MINF");
        stats->nameHashMINF = xStrHash(tmpstr);
        if (stats->shrapName != NULL)
        {
            stats->shrapAsset = (zShrapnelAsset*)xSTFindAsset(xStrHash(stats->shrapName), NULL);
        }
        else
        {
            stats->shrapAsset = NULL;
        }
    }

    for (i = 0; i < sc->num_base; i++)
    {
        myent = (xEnt*)sc->base[i];
        if (myent->baseType == eBaseTypeStatic || myent->baseType == eBaseTypeDestructObj)
        {
            for (stats = zThrowableModels; stats->name != NULL; stats++)
            {
                if (stats->nameHash == myent->asset->modelInfoID ||
                    stats->nameHashMINF == myent->asset->modelInfoID)
                {
                    myent->moreFlags |= 0x8;
                    myent->asset->moreFlags |= 0x8;
                    break;
                }
            }
        }
    }
}

// No GC symbol (always inlined into zFruit_Update and zThrown_Update); name from BfBB.
static inline void zFruit_ColorFade(zThrownStruct* thrown)
{
    static F32 fruitPattern[18] = { 6.0f, 1.0f, 3.2f, 0.3f, 3.0f, 1.0f, 2.2f,  0.3f,  2.0f,
                                    1.0f, 1.2f, 0.3f, 1.0f, 1.0f, 0.0f, 0.15f, -1.0f, 0.0f };

    xModelInstance* mod = thrown->ent->model;

    if (!thrown->killTimer || thrown->killTimer > fruitPattern[0])
    {
        mod->RedMultiplier = mod->GreenMultiplier = mod->BlueMultiplier = fruitPattern[1];
    }
    else
    {
        F32* p = fruitPattern;
        while (thrown->killTimer < p[2])
        {
            p += 2;
        }

        F32 lerp = (thrown->killTimer - p[0]) / (p[2] - p[0]);
        mod->RedMultiplier = mod->GreenMultiplier = mod->BlueMultiplier =
            lerp * p[3] + (1.0f - lerp) * p[1];
    }
}

static void zFruit_Update(xEnt* ent, xScene* sc, F32 dt)
{
    U32 i;
    zThrownStruct* thrown;
    zShrapnelAsset* shrap;

    if (ent->model->Scale.x)
    {
        if (ent->model->Scale.x == 1e-5f)
        {
            if (xVec3Dist((xVec3*)&ent->model->Mat->pos,
                          (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos) <
                1.4f)
            {
                xEntBeginUpdate(ent, sc, dt);
                xEntEndUpdate(ent, sc, dt);
                return;
            }
            ent->chkby |= XENT_COLLTYPE_PLYR;
        }

        ent->model->Scale.x += dt / 0.5f;
        ent->model->Scale.y = ent->model->Scale.x;
        ent->model->Scale.z = ent->model->Scale.x;

        if (ent->model->Scale.x >= 1.0f)
        {
            ent->model->Scale.x = 0.0f;
            zThrown_Remove(ent);
        }

        xEntBeginUpdate(ent, sc, dt);
        xEntEndUpdate(ent, sc, dt);
    }
    else
    {
        for (i = 0; i < zThrownCount; i++)
        {
            if (ent == zThrownList[i].ent)
            {
                break;
            }
        }

        thrown = &zThrownList[i];

        xEntBeginUpdate(ent, sc, dt);
        xEntEndUpdate(ent, sc, dt);

        if (thrown->killTimer)
        {
            thrown->killTimer -= dt;
            if (thrown->killTimer <= 0.0f)
            {
                thrown->killTimer = 0.0f;
                zFruit_ColorFade(thrown);

                if (globals.player.carry.grabbed == ent)
                {
                    globals.player.carry.grabbed = NULL;
                    gReticleTarget[0] = NULL;
                }
                if (gReticleTarget[0] == ent)
                {
                    gReticleTarget[0] = NULL;
                }

                shrap = thrown->stats->shrapAsset;
                if (shrap != NULL && shrap->initCB != NULL)
                {
                    shrap->initCB(shrap, ent->model, NULL, NULL);
                }

                xEntReset(ent);
                zEntEvent(ent, eEventCollision_Visible_On);
                ent->model->Scale.x = 1e-5f;
                ent->model->Scale.y = 1e-5f;
                ent->model->Scale.z = 1e-5f;
                ent->chkby &= (U8)~XENT_COLLTYPE_PLYR;
                ent->baseFlags &= (U16)~0x10;

                for (i = 0; i < zThrownCount; i++)
                {
                    if (zThrownList[i].stackEnt == ent)
                    {
                        zThrownList[i].stackEnt = NULL;
                        zThrownList[i].ent->update = zThrown_Update;
                    }
                    if (zThrownList[i].stackTgt == ent)
                    {
                        zThrownList[i].stackTgt = NULL;
                    }
                }
            }
            else
            {
                zFruit_ColorFade(thrown);
            }
        }
    }
}

static void Recurse_TranslateStack(xEnt* ent, xVec3* delta)
{
    U32 i;

    sDebugDepth++;
    for (i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].stackEnt == ent)
        {
            Recurse_TranslateStack(zThrownList[i].ent, delta);
            zThrownList[i].ent->model->Mat->pos.x += delta->x;
            zThrownList[i].ent->model->Mat->pos.y += delta->y;
            zThrownList[i].ent->model->Mat->pos.z += delta->z;
        }
    }
    sDebugDepth--;
}

// No GC symbol (inlined into zThrown_Update through zSceneForAllBase); name from BfBB.
static inline xBase* zThrown_ButtonIteratorCB(xBase* b, zScene* scn, void* user)
{
    xCollis coll;

    coll.flags = 0;
    xSphereHitsModel((xSphere*)user, ((xEnt*)b)->model, &coll);
    if (coll.flags & k_HIT_IT)
    {
        zEntButton_Hold((_zEntButton*)b, 0x10000);
    }
    return b;
}

// No GC symbol (always inlined into zThrown_Update); name from BfBB.
static inline void checkAgainstButtons(xEnt* ent)
{
    xSphere sph;

    xVec3Copy(&sph.center, xBoundCenter(&ent->bound));
    sph.r = 0.5f;
    zSceneForAllBase(zThrown_ButtonIteratorCB, eBaseTypeButton, &sph);
}

// The hammer test uses the component overload xVec3Dist2(ax, ay, az, bx, by, bz), as in
// Ratatouille's xVec3Inlines.h; the pointer form colours registers differently from retail.
static void zThrown_Update(xEnt* ent, xScene* sc, F32 dt)
{
    U32 i;
    U32 removethis;
    zThrownStruct* thrown;
    F32 dist2;
    xVec3 stackDelta;
    xEntCollis collis;
    xEntCollis* oldcollis;
    xBound oldbound;
    U8 oldpflags;
    U8 oldcollType;
    void (*old_bupdateFunc)(xEnt*, xVec3*);
    static xEntFrame tmpNPCframe;

    removethis = 0;

    for (i = 0; i < zThrownCount; i++)
    {
        if (ent == zThrownList[i].ent)
        {
            break;
        }
    }

    thrown = &zThrownList[i];

    if (ent->baseType == eBaseTypeNPC && ent->frame == NULL)
    {
        ent->frame = &tmpNPCframe;
        ent->frame->mat.pos = *(xVec3*)&ent->model->Mat->pos;
    }

    if (thrown->hthrown && globals.player.carry.flyingToTarget != NULL)
    {
        dist2 = xVec3Dist2(thrown->ent->bound.sph.center.x, thrown->ent->bound.sph.center.y,
                           thrown->ent->bound.sph.center.z,
                           globals.player.carry.flyingToTarget->bound.sph.center.x,
                           globals.player.carry.flyingToTarget->bound.sph.center.y,
                           globals.player.carry.flyingToTarget->bound.sph.center.z);

        if (dist2 < 16.0f)
        {
            if (dist2 > sLastThrownDistance)
            {
                zEntPlayerExplodeHammerThrow(&ent->bound.sph.center);
                if (zThrown_IsFruit(ent, NULL))
                {
                    zThrown_KillFruit(ent);
                }
                if (ent->baseType == eBaseTypeNPC &&
                    ((zNMECommon*)ent)->itemType == NME_TYPE_TIKI_STONE)
                {
                    zEntEvent(thrown->ent, eEventKill);
                }
            }
            sLastThrownDistance = dist2;
        }
    }

    if (thrown->collResetTimer)
    {
        thrown->collResetTimer -= dt;
        if (thrown->collResetTimer <= 0.0f)
        {
            thrown->collResetTimer = 0.0f;
            ent->chkby |= XENT_COLLTYPE_PLYR;
        }
    }

    oldcollis = ent->collis;
    oldpflags = ent->pflags;
    oldcollType = ent->collType;
    stackDelta.x = -ent->model->Mat->pos.x;
    stackDelta.y = -ent->model->Mat->pos.y;
    stackDelta.z = -ent->model->Mat->pos.z;
    old_bupdateFunc = ent->bupdate;
    ent->bupdate = NULL;
    ent->collis = &collis;
    collis.chk = 0x2e;
    collis.pen = 0x2e;
    collis.post = NULL;
    collis.depenq = NULL;
    ent->frame->vel = thrown->vel;
    ent->pflags |= XENT_PFLAGS_HAS_GRAVITY;
    ent->collType = XENT_COLLTYPE_PLYR;

    oldbound = ent->bound;
    switch (oldbound.type)
    {
    case XBOUND_TYPE_BOX:
    case XBOUND_TYPE_OBB:
        ent->bound.sph.r = (0.5f * (oldbound.box.box.upper.x - oldbound.box.box.lower.x) <
                            0.5f * (oldbound.box.box.upper.z - oldbound.box.box.lower.z)) ?
                               0.5f * (oldbound.box.box.upper.x - oldbound.box.box.lower.x) :
                               0.5f * (oldbound.box.box.upper.z - oldbound.box.box.lower.z);
        ent->bound.sph.center.x = 0.5f * (oldbound.box.box.upper.x + oldbound.box.box.lower.x);
        ent->bound.sph.center.y = oldbound.box.box.lower.y + ent->bound.sph.r;
        ent->bound.sph.center.z = 0.5f * (oldbound.box.box.upper.z + oldbound.box.box.lower.z);
        if (oldbound.type == XBOUND_TYPE_OBB)
        {
            xMat4x3Toworld(&ent->bound.sph.center, oldbound.mat, &ent->bound.sph.center);
        }
        break;
    }

    ent->bound.type = XBOUND_TYPE_SPHERE;

    if (thrown->stackTgt != NULL)
    {
        ent->bound.sph.center.y -= 0.25f * ent->bound.sph.r;
        ent->bound.sph.r *= 0.75f;
    }

    if (ent->baseType == eBaseTypeNPC)
    {
        xEntBeginUpdate(ent, sc, 0.0f);
    }
    else
    {
        xEntBeginUpdate(ent, sc, dt);
    }

    {
        F32 oldgrav = globals.sceneCur->gravity;
        globals.sceneCur->gravity = -globals.player.carry.throwGravity;
        xVec3 dposvel = { 0.0f, 0.0f, 0.0f };

        ent->frame->vel.y += -globals.player.carry.throwGravity * dt;
        if (ent->frame->vel.y < -35.0f)
        {
            ent->frame->vel.y = -35.0f;
        }
        xVec3Add(&dposvel, &ent->frame->vel, &ent->frame->oldvel);
        xVec3SMulBy(&dposvel, 0.5f * dt);
        xVec3AddTo(&ent->frame->mat.pos, &dposvel);
        globals.sceneCur->gravity = oldgrav;
    }

    if (thrown->stackTgt != NULL)
    {
        F32 posdot =
            ent->frame->vel.x * (thrown->stackTgt->model->Mat->pos.x - ent->frame->mat.pos.x) +
            ent->frame->vel.z * (thrown->stackTgt->model->Mat->pos.z - ent->frame->mat.pos.z);
        if (posdot <= 0.0f)
        {
            xVec3 velunit;
            // Not in the PS2 DWARF; retail keeps these in registers across the frame stores.
            F32 newx;
            F32 newz;

            velunit.x = ent->frame->vel.x;
            velunit.y = 0.0f;
            velunit.z = ent->frame->vel.z;
            xVec3Normalize(&velunit, &velunit);

            posdot = velunit.x * (thrown->stackTgt->model->Mat->pos.x - ent->frame->mat.pos.x) +
                     velunit.z * (thrown->stackTgt->model->Mat->pos.z - ent->frame->mat.pos.z);
            newx = posdot * velunit.x + ent->frame->mat.pos.x;
            newz = posdot * velunit.z + ent->frame->mat.pos.z;
            if (SQR(newx - thrown->stackTgt->model->Mat->pos.x) +
                    SQR(newz - thrown->stackTgt->model->Mat->pos.z) <
                0.02f)
            {
                ent->frame->mat.pos.x = newx;
                ent->frame->mat.pos.z = newz;
                ent->frame->vel.x = 0.0f;
                ent->frame->vel.z = 0.0f;
            }
            else if (ent->frame->vel.x || ent->frame->vel.z)
            {
                thrown->stackTgt = NULL;
            }
        }
    }

    if (thrown->drv.driver != NULL)
    {
        xEntDriveUpdate(&thrown->drv, sc, dt, NULL);
    }

    thrown->oldcollpos = ent->frame->mat.pos;

    if (ent->baseType == eBaseTypeBoulder)
    {
        ((xEntBoulder*)ent)->vel.x = 0.0f;
        ((xEntBoulder*)ent)->vel.y = 0.0f;
        ((xEntBoulder*)ent)->vel.z = 0.0f;
        xEntBoulder_RealBUpdate(ent, &ent->frame->mat.pos);
    }
    else if (ent->bupdate != NULL)
    {
        ent->bupdate(ent, &ent->frame->mat.pos);
    }
    else
    {
        xEntDefaultBoundUpdate(ent, &ent->frame->mat.pos);
    }

    for (i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].stackEnt == ent)
        {
            zThrownList[i].ent->chkby = 0;
        }
        if (zThrownList[i].stackTgt == ent)
        {
            zThrownList[i].ent->chkby = 0;
        }
    }

    xSweptSphere sws;
    xVec3 start;

    start.x = ent->bound.sph.center.x + ent->model->Mat->pos.x - ent->frame->mat.pos.x;
    start.y = ent->bound.sph.center.y + ent->model->Mat->pos.y - ent->frame->mat.pos.y;
    start.z = ent->bound.sph.center.z + ent->model->Mat->pos.z - ent->frame->mat.pos.z;
    xSweptSpherePrepare(&sws, &start, &ent->bound.sph.center, ent->bound.sph.r);

    if (sws.dist > 0.05f && xSweptSphereToScene(&sws, globals.sceneCur, ent, XENT_COLLTYPE_PLYR, 0))
    {
        F32 lerp = sws.dist - sws.curdist;
        F32 lerpdist = 0.25f * ent->bound.sph.r;
        if (lerp < lerpdist)
        {
            lerpdist = lerp;
        }
        lerp = (sws.curdist + lerpdist) / sws.dist;
        ent->frame->mat.pos.x =
            ent->model->Mat->pos.x * (1.0f - lerp) + ent->frame->mat.pos.x * lerp;
        ent->frame->mat.pos.y =
            ent->model->Mat->pos.y * (1.0f - lerp) + ent->frame->mat.pos.y * lerp;
        ent->frame->mat.pos.z =
            ent->model->Mat->pos.z * (1.0f - lerp) + ent->frame->mat.pos.z * lerp;

        if (ent->baseType == eBaseTypeBoulder)
        {
            ((xEntBoulder*)ent)->vel.x = 0.0f;
            ((xEntBoulder*)ent)->vel.y = 0.0f;
            ((xEntBoulder*)ent)->vel.z = 0.0f;
            xEntBoulder_RealBUpdate(ent, &ent->frame->mat.pos);
        }
        else if (ent->bupdate != NULL)
        {
            ent->bupdate(ent, &ent->frame->mat.pos);
        }
        else
        {
            xEntDefaultBoundUpdate(ent, &ent->frame->mat.pos);
        }
    }

    xEntCollide(ent, sc, dt);

    for (i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].stackEnt == ent)
        {
            zThrownList[i].ent->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        }
        if (zThrownList[i].stackTgt == ent)
        {
            zThrownList[i].ent->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        }
    }

    if (ent->collis->colls[0].flags & k_HIT_IT)
    {
        thrown->stackTgt = NULL;
    }

    xEntEndUpdate(ent, sc, dt);

    if (thrown->stats->collCB != NULL &&
        (collis.stat_eidx > collis.stat_sidx || collis.dyn_eidx > collis.dyn_sidx ||
         collis.npc_eidx > collis.npc_sidx || collis.env_eidx > collis.env_sidx))
    {
        if (0.0f == sSNDLandTimer)
        {
            if (zThrown_IsFruit(ent, NULL))
            {
                xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("TFRUIT_LAND_GRP")), 0, NULL, NULL, ent,
                            NULL, NULL);
            }
        }
        sSNDLandTimer = 0.2f;

        if (ent->baseType == eBaseTypeNPC)
        {
            xCollis* coll = ent->collis->colls;
            xCollis* cend = coll + ent->collis->idx;
            for (; coll < cend; coll++)
            {
                if (coll->flags & k_HIT_IT)
                {
                    // 'surf' is not in the PS2 DWARF (only 'prop' is).
                    xSurface* surf = zSurfaceGetSurface(coll);
                    if (surf != NULL && surf->state == 0 && surf->moprops != NULL)
                    {
                        zSurfaceProps* prop = (zSurfaceProps*)surf->moprops;
                        if (prop->asset != NULL && prop->asset->game_damage_type)
                        {
                            ((zNMECommon*)ent)
                                ->Damage(DMGTYP_DAMAGE_SURFACE, NULL, NULL, -1,
                                         ePowerupLevelNormal);
                        }
                    }
                }
            }
        }

        F32 bounce;
        F32 friction;
        thrown->stats->collCB(thrown, &collis, &bounce, &friction);

        if (thrown->hthrown)
        {
            zEntPlayerExplodeHammerThrow(&ent->bound.sph.center);
            if (zThrown_IsFruit(ent, NULL))
            {
                zThrown_KillFruit(ent);
            }
            if (ent->baseType == eBaseTypeNPC &&
                ((zNMECommon*)ent)->itemType == NME_TYPE_TIKI_STONE)
            {
                zEntEvent(thrown->ent, eEventKill);
            }
        }

        if (0.0f == bounce && 0.0f == friction)
        {
            removethis = 1;
            if (ent->baseType != eBaseTypeBoulder)
            {
                ent->frame->vel.x = 0.0f;
                ent->frame->vel.y = 0.0f;
                ent->frame->vel.z = 0.0f;
            }
            else
            {
                ((xEntBoulder*)ent)->vel.x = 0.0f;
                ((xEntBoulder*)ent)->vel.y = 0.0f;
                ((xEntBoulder*)ent)->vel.z = 0.0f;
            }
        }
        else
        {
            for (i = 0; i < 18; i++)
            {
                if (collis.colls[i].flags & k_HIT_IT)
                {
                    // nrm, perp and tang are not in the DWARF (names ours); retail's register
                    // colouring needs them as xVec3 locals.
                    xVec3 nrm;
                    nrm.x = -collis.colls[i].hdng.x;
                    nrm.y = -collis.colls[i].hdng.y;
                    nrm.z = -collis.colls[i].hdng.z;
                    F32 dothdng =
                        nrm.x * thrown->vel.x + nrm.y * thrown->vel.y + nrm.z * thrown->vel.z;
                    xVec3 perp;
                    xVec3 tang;
                    perp.x = nrm.x * dothdng;
                    perp.y = nrm.y * dothdng;
                    perp.z = nrm.z * dothdng;
                    tang.x = thrown->vel.x - perp.x;
                    tang.y = thrown->vel.y - perp.y;
                    tang.z = thrown->vel.z - perp.z;
                    ent->frame->vel.x =
                        thrown->vel.x - (1.0f + bounce) * perp.x - (1.0f - friction) * tang.x;
                    ent->frame->vel.y =
                        thrown->vel.y - (1.0f + bounce) * perp.y - (1.0f - friction) * tang.y;
                    ent->frame->vel.z =
                        thrown->vel.z - (1.0f + bounce) * perp.z - (1.0f - friction) * tang.z;
                    break;
                }
            }
        }
    }

    thrown->vel = ent->frame->vel;
    ent->collis = oldcollis;
    ent->pflags = oldpflags;
    ent->collType = oldcollType;
    ent->bupdate = old_bupdateFunc;

    switch (oldbound.type)
    {
    case XBOUND_TYPE_BOX:
    {
        F32 boxX;
        F32 boxYupper;
        F32 boxZ;
        xVec3 boxCenter;

        ent->bound.type = XBOUND_TYPE_BOX;
        boxX = 0.5f * (oldbound.box.box.upper.x - oldbound.box.box.lower.x);
        // 'r' is not in the PS2 DWARF (its single use is propagated away). Read before the box
        // stores, it lets the boxYupper read share its load, as in retail. Like boxCenter, it keeps
        // a sphere value that the box stores overwrite (xBound is a union).
        F32 r = ent->bound.sph.r;
        boxYupper = (oldbound.box.box.upper.y - oldbound.box.box.lower.y) - ent->bound.sph.r;
        boxZ = 0.5f * (oldbound.box.box.upper.z - oldbound.box.box.lower.z);
        boxCenter = ent->bound.sph.center;
        ent->bound.box.box.lower.x = boxCenter.x - boxX;
        ent->bound.box.box.lower.y = boxCenter.y - r;
        ent->bound.box.box.lower.z = boxCenter.z - boxZ;
        ent->bound.box.box.upper.x = boxCenter.x + boxX;
        ent->bound.box.box.upper.y = boxCenter.y + boxYupper;
        ent->bound.box.box.upper.z = boxCenter.z + boxZ;
        ent->bound.box.center.x = 0.5f * (ent->bound.box.box.lower.x + ent->bound.box.box.upper.x);
        ent->bound.box.center.y = 0.5f * (ent->bound.box.box.lower.y + ent->bound.box.box.upper.y);
        ent->bound.box.center.z = 0.5f * (ent->bound.box.box.lower.z + ent->bound.box.box.upper.z);
        break;
    }
    case XBOUND_TYPE_OBB:
        ent->bound.type = oldbound.type;
        ent->bound.box = oldbound.box;
        ent->bound.mat = oldbound.mat;
        break;
    case XBOUND_TYPE_SPHERE:
        if (thrown->stackTgt != NULL)
        {
            ent->bound.sph.r = oldbound.sph.r;
            ent->bound.sph.center.y += 0.25f * oldbound.sph.r;
        }
        break;
    }

    stackDelta.x += ent->model->Mat->pos.x;
    stackDelta.y += ent->model->Mat->pos.y;
    stackDelta.z += ent->model->Mat->pos.z;
    Recurse_TranslateStack(ent, &stackDelta);

    if (thrown->killTimer)
    {
        thrown->killTimer -= dt;
        if (thrown->killTimer < 0.0f)
        {
            thrown->killTimer = 0.0f;
            zFruit_ColorFade(thrown);
            ent->update = zFruit_Update;

            if (globals.player.carry.grabbed == ent)
            {
                globals.player.carry.grabbed = NULL;
                gReticleTarget[0] = NULL;
            }
            if (gReticleTarget[0] == ent)
            {
                gReticleTarget[0] = NULL;
            }

            zShrapnelAsset* shrap = thrown->stats->shrapAsset;
            if (shrap != NULL && shrap->initCB != NULL)
            {
                shrap->initCB(shrap, ent->model, NULL, NULL);
            }

            xEntReset(ent);
            zEntEvent(ent, eEventCollision_Visible_On);
            ent->model->Scale.x = 1e-5f;
            ent->model->Scale.y = 1e-5f;
            ent->model->Scale.z = 1e-5f;
            ent->chkby &= (U8)~XENT_COLLTYPE_PLYR;
            ent->baseFlags &= (U16)~0x10;

            for (i = 0; i < zThrownCount; i++)
            {
                if (zThrownList[i].stackEnt == ent)
                {
                    zThrownList[i].stackEnt = NULL;
                    zThrownList[i].ent->update = zThrown_Update;
                }
                if (zThrownList[i].stackTgt == ent)
                {
                    zThrownList[i].stackTgt = NULL;
                }
            }
            return;
        }
        zFruit_ColorFade(thrown);
    }

    checkAgainstButtons(ent);
    zFX_SpawnBubbleTrail((xVec3*)&ent->model->Mat->pos, 1);

    if (ent->bupdate != NULL)
    {
        ent->bupdate(ent, (xVec3*)&ent->model->Mat->pos);
    }
    else
    {
        xEntDefaultBoundUpdate(ent, (xVec3*)&ent->model->Mat->pos);
    }

    xCollis* fcoll = &collis.colls[0];
    if (thrown->drv.driver == NULL)
    {
        if ((fcoll->flags & k_HIT_IT) && fcoll->optr != NULL &&
            fcoll->optr == thrown->driveLastFloor && ((xEnt*)fcoll->optr)->frame != NULL &&
            thrown->vel.y <= 0.0f)
        {
            thrown->driveDebounce++;
            if (thrown->driveDebounce > 2)
            {
                xEntDriveMount(&thrown->drv, (xEnt*)fcoll->optr, 0.1f, NULL);
            }
        }
        else
        {
            thrown->driveDebounce = 0;
        }
    }
    else if (!(fcoll->flags & k_HIT_IT) || fcoll->optr != thrown->drv.driver)
    {
        thrown->driveDebounce++;
        if (thrown->driveDebounce > 4)
        {
            xEntDriveDismount(&thrown->drv, 0.3f);
        }
    }
    else
    {
        thrown->driveDebounce = 0;
    }

    thrown->driveLastFloor = (xEnt*)fcoll->optr;

    if (sSNDLandTimer)
    {
        sSNDLandTimer -= dt;
        if (sSNDLandTimer < 0.0f)
        {
            sSNDLandTimer = 0.0f;
        }
    }

    if (removethis)
    {
        if (thrown->stats->carry == &c_fruit)
        {
            if (thrown->stackEnt != NULL)
            {
                ent->update = zFruit_Update;
            }
        }
        else
        {
            zThrown_Remove(ent);
        }
    }

    if (ent->baseType == eBaseTypeNPC)
    {
        ent->frame = NULL;
    }
}

void zThrown_NMEUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    U32 i;

    for (i = 0; i < zThrownCount; i++)
    {
        if (ent == zThrownList[i].ent)
        {
            break;
        }
    }

    if (i < zThrownCount && zThrownList[i].newUpdate)
    {
        zThrown_Update(ent, sc, dt);
    }
}

void zThrown_LaunchVel(xEnt* ent, xVec3* vel)
{
    ThrowableStats* stats;
    zThrownStruct* newThrown;
    U32 i;

    stats = &zThrowableModels[1];
    while (stats->name != NULL)
    {
        if (stats->nameHash == ent->asset->modelInfoID ||
            stats->nameHashMINF == ent->asset->modelInfoID)
        {
            break;
        }
        stats++;
    }

    if (stats->name == NULL)
    {
        stats = zThrowableModels;
    }

    newThrown = NULL;
    if (ent->update == zFruit_Update)
    {
        for (i = 0; i < zThrownCount; i++)
        {
            if (zThrownList[i].ent == ent)
            {
                newThrown = &zThrownList[i];
                break;
            }
        }
    }
    else
    {
        zThrown_Remove(ent);
        newThrown = &zThrownList[zThrownCount];
        zThrownCount++;
        newThrown->killTimer = stats->carry->killTimer;
        newThrown->stats = stats;
        newThrown->oldupdate = ent->update;
        newThrown->ent = ent;
        newThrown->stackEnt = NULL;
        newThrown->stackTgt = NULL;
        newThrown->patLauncher = NULL;
        newThrown->oldRecShadow = ent->baseFlags & 0x10;
        newThrown->newUpdate = 0;
        xEntDriveInit(&newThrown->drv, ent);
        newThrown->driveDebounce = 0;
        newThrown->driveLastFloor = NULL;
    }

    // Retail does not check newThrown: a zFruit_Update entity is always in zThrownList.
    newThrown->vel = *vel;
    newThrown->collResetTimer = stats->launch->collResetTimer;
    if (gThrowPower)
    {
        newThrown->hthrown = 1;
        sLastThrownDistance = 16.0f;
    }
    else
    {
        newThrown->hthrown = 0;
    }

    ent->update = zThrown_Update;
    newThrown->newUpdate = 1;
    ent->baseFlags |= 0x80;
    zThrown_AddTempFrame(newThrown);
}

void zThrown_LaunchDir(xEnt* ent, xVec3* dir)
{
    ThrowableStats* stats;
    F32 throwSpeedXZ;
    xVec3 tempVel;

    globals.player.carry.flyingToTarget = NULL;

    stats = &zThrowableModels[1];
    while (stats->name != NULL)
    {
        if (stats->nameHash == ent->asset->modelInfoID ||
            stats->nameHashMINF == ent->asset->modelInfoID)
        {
            break;
        }
        stats++;
    }

    // Not in the PS2 DWARF (names ours): retail computes the distance before the square root and
    // keeps it live across it.
    F32 throwHeight =
        globals.player.carry.throwHeight - gThrowPower * (globals.player.carry.throwHeight - 1.0f);
    F32 throwDistance = globals.player.carry.throwDistance +
                        gThrowPower * globals.player.carry.throwPowerAddDistance;
    F32 airTime = xsqrt(2.0f * throwHeight / globals.player.carry.throwGravity);

    throwSpeedXZ = throwDistance / (2.0f * airTime);
    tempVel.x = throwSpeedXZ * dir->x;
    tempVel.y = globals.player.carry.throwGravity * airTime;
    tempVel.z = throwSpeedXZ * dir->z;

    zThrown_LaunchVel(ent, &tempVel);
}

// 'delta' and 'grav' are not in the TSSM PS2 DWARF (BfBB's has grav). Retail's xsqrt nesting
// and registers need the xVec2 with its components stored separately, then length(); the gravity
// is loaded once and kept in a register.
S32 zThrown_LaunchPos(xEnt* ent, xVec3* pos, xVec3* dir)
{
    S32 retval;
    ThrowableStats* stats;
    F32 grav;
    F32 throwHeight;
    F32 throwDistance;
    F32 throwHeightFudge;
    F32 throwSpeedY;
    F32 throwSpeedXZ;
    F32 tpeak;
    F32 hpeak;
    xVec3 tempVel;
    F32 speed;

    stats = &zThrowableModels[1];
    while (stats->name != NULL)
    {
        if (stats->nameHash == ent->asset->modelInfoID ||
            stats->nameHashMINF == ent->asset->modelInfoID)
        {
            break;
        }
        stats++;
    }

    if (stats->name == NULL)
    {
        stats = zThrowableModels;
    }

    grav = globals.player.carry.throwGravity;
    throwHeight =
        globals.player.carry.throwHeight - gThrowPower * (globals.player.carry.throwHeight - 1.0f);
    throwDistance = globals.player.carry.throwDistance +
                    gThrowPower * globals.player.carry.throwPowerAddDistance;

    xVec2 delta;
    delta.x = pos->x - ent->bound.sph.center.x;
    delta.y = pos->z - ent->bound.sph.center.z;
    speed = 1.4f * delta.length();
    throwHeightFudge = 0.0f;
    if (throwDistance < speed)
    {
        throwDistance = speed;
        throwHeightFudge = 1.0f;
    }

    if (throwHeight < throwHeightFudge + (pos->y - ent->model->Mat->pos.y))
    {
        throwHeight = throwHeightFudge + pos->y - ent->model->Mat->pos.y;
    }

    tpeak = xsqrt(2.0f * throwHeight / grav);
    throwSpeedXZ = throwDistance / (2.0f * tpeak);
    throwSpeedY = grav * tpeak;
    tpeak = throwSpeedY / grav;

    if (ent->baseType == eBaseTypeBoulder)
    {
        hpeak = stats->launch->throwSpeedY * tpeak + (ent->bound.sph.center.y - ent->bound.sph.r) -
                0.5f * grav * tpeak * tpeak;
    }
    else
    {
        hpeak = throwSpeedY * tpeak + ent->model->Mat->pos.y - 0.5f * grav * tpeak * tpeak;
    }

    if (hpeak < 0.5f + pos->y)
    {
        retval = 0;
        tempVel.x = throwSpeedXZ * dir->x;
        tempVel.y = throwSpeedY;
        tempVel.z = throwSpeedXZ * dir->z;
    }
    else
    {
        // fall time from the peak first, then the horizontal speed over the whole flight
        speed = xsqrt(2.0f * (hpeak - pos->y) / grav);
        speed = xsqrt(SQR(ent->model->Mat->pos.x - pos->x) + SQR(ent->model->Mat->pos.z - pos->z)) /
                (speed + tpeak);
        retval = 1;
        tempVel.x = speed * dir->x;
        tempVel.y = throwSpeedY;
        tempVel.z = speed * dir->z;
    }

    zThrown_LaunchVel(ent, &tempVel);
    return retval;
}

void zThrown_LaunchStack(xEnt* ent, xEnt* stackTgt)
{
    xVec3 stackPos;
    xVec3 stackDir;
    xBox tmpbox;
    U32 i;

    switch (stackTgt->bound.type)
    {
    case XBOUND_TYPE_SPHERE:
        stackPos.y = stackTgt->bound.sph.center.y + stackTgt->bound.sph.r;
        break;
    case XBOUND_TYPE_BOX:
        stackPos.y = stackTgt->bound.box.box.upper.y;
        break;
    case XBOUND_TYPE_OBB:
        xBoxInitBoundOBB(&tmpbox, &stackTgt->bound.box.box, stackTgt->bound.mat);
        stackPos.y = tmpbox.upper.y;
        break;
    default:
        stackPos.y = stackTgt->model->Mat->pos.y;
        break;
    }

    stackPos.y += 0.2f;
    stackPos.x = stackTgt->model->Mat->pos.x;
    stackPos.z = stackTgt->model->Mat->pos.z;
    stackDir.x = stackPos.x - ent->model->Mat->pos.x;
    stackDir.y = 0.0f;
    stackDir.z = stackPos.z - ent->model->Mat->pos.z;
    xVec3Normalize(&stackDir, &stackDir);

    if (zThrown_LaunchPos(ent, &stackPos, &stackDir))
    {
        for (i = 0; i < zThrownCount; i++)
        {
            if (ent == zThrownList[i].ent)
            {
                break;
            }
        }
        zThrownStruct* listInd = &zThrownList[i];
        listInd->stackTgt = stackTgt;
    }
}

void zThrown_PatrickLauncher(xEnt* ent, xEnt* launcher)
{
    U32 i;

    for (i = 0; i < zThrownCount; i++)
    {
        if (ent == zThrownList[i].ent)
        {
            break;
        }
    }

    if (i != zThrownCount)
    {
        zThrownStruct* listInd = &zThrownList[i];
        listInd->patLauncher = launcher;
    }
}

void zThrown_AddFruit(xEnt* ent)
{
    U32 i;
    ThrowableStats* stats;
    zThrownStruct* newThrown;

    for (i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].ent == ent)
        {
            zThrownList[i].stackEnt = NULL;
            zThrownList[i].stackTgt = NULL;
            ent->update = zFruit_Update;
            ent->baseFlags |= (U16)0x80;
            if (zThrownList[i].drv.driver != NULL)
            {
                xEntDriveDismount(&zThrownList[i].drv, 1e-5f);
            }
            zThrownList[i].driveDebounce = 0;
            zThrownList[i].driveLastFloor = NULL;
            zThrownList[i].hthrown = 0;
            return;
        }
    }

    stats = &zThrowableModels[1];
    while (stats->name != NULL)
    {
        if (stats->nameHash == ent->asset->modelInfoID ||
            stats->nameHashMINF == ent->asset->modelInfoID)
        {
            break;
        }
        stats++;
    }

    if (stats->name == NULL || stats->carry != &c_fruit)
    {
        return;
    }

    newThrown = &zThrownList[zThrownCount];
    zThrownCount++;
    newThrown->killTimer = stats->carry->killTimer;
    newThrown->stats = stats;
    newThrown->oldupdate = ent->update;
    newThrown->ent = ent;
    newThrown->stackEnt = NULL;
    newThrown->stackTgt = NULL;
    newThrown->oldRecShadow = ent->baseFlags & 0x10;
    ent->update = zFruit_Update;
    ent->baseFlags |= (U16)0x80;
    zThrown_AddTempFrame(newThrown);
    xEntDriveInit(&newThrown->drv, ent);
    newThrown->driveDebounce = 0;
    newThrown->driveLastFloor = NULL;
    newThrown->hthrown = 0;
}

void zThrown_Remove(xEnt* ent)
{
    U32 i;
    xModelInstance* mod;
    void (*thisCollCB)(zThrownStruct*, xEntCollis*, F32*, F32*);

    for (i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].ent == ent)
        {
            zThrownList[i].ent->update = zThrownList[i].oldupdate;
            ent->baseFlags &= 0xff7f;
            if (zThrownList[i].ent->frame == &zThrownList[i].frame)
            {
                zThrownList[i].ent->frame = NULL;
            }

            mod = zThrownList[i].ent->model;
            mod->RedMultiplier = mod->GreenMultiplier = mod->BlueMultiplier = 1.0f;
            ent->baseFlags |= (U16)zThrownList[i].oldRecShadow;

            thisCollCB = zThrownList[i].stats->collCB;
            zThrownList[i] = zThrownList[zThrownCount - 1];
            zThrownCount--;

            if (ent->baseType == eBaseTypeNPC)
            {
                ((zNMECommon*)ent)->SetCarryState(zNMECARRY_NONE);
            }

            if (ent->baseType == eBaseTypeBoulder)
            {
                ((xEntBoulder*)ent)->vel = zThrownList[i].vel;
            }

            if (xEntIsVisible(ent) && (thisCollCB == zThrownCollide_StoneTiki ||
                                       thisCollCB == zThrownCollide_ThrowFreeze ||
                                       thisCollCB == zThrownCollide_ThrowFruit))
            {
                ent->chkby |= XENT_COLLTYPE_PLYR;
            }
            return;
        }
    }
}

S32 zThrown_KillFruit(xEnt* ent)
{
    for (U32 i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].ent == ent)
        {
            if (zThrownList[i].stats->carry == &c_fruit)
            {
                zThrownList[i].killTimer = 1e-6f;
                return 1;
            }
            return 0;
        }
    }

    return 0;
}

static S32 zThrownCollide_CauseDamage(zThrownStruct* thrown, xEntCollis* collis)
{
    S32 damageInflicted = 0;
    U32 collidx;
    xCollis* currcoll;
    xEnt* hitent;
    F32 dummy;
    xVec3 pos;

    for (collidx = k_XCOLLS_IDX_COUNT; collidx < 18 && (collis->colls[collidx].flags & k_HIT_IT);
         collidx++)
    {
        currcoll = &collis->colls[collidx];
        hitent = (xEnt*)currcoll->optr;
        if (hitent == NULL)
        {
            continue;
        }

        if (thrown->patLauncher != NULL && thrown->patLauncher == hitent)
        {
            zEntPlayer_PatrickLaunch(thrown->patLauncher);
            return 1;
        }

        if (hitent->baseType == eBaseTypeDestructObj)
        {
            zEntEvent(hitent, eEventHit_Throw);
            damageInflicted = 1;
        }
        else if (hitent->baseType == eBaseTypeNPC)
        {
            ((zNMECommon*)hitent)
                ->Damage(DMGTYP_HITBYTOSS, NULL, NULL,
                         DmgAmtForPlayerAttack(PLYR_ATAK_PAT_THROW, thrown->hthrown == 0 ?
                                                                        ePowerupLevelNormal :
                                                                        ePowerupLevelLevelMax),
                         ePowerupLevelNormal);
            damageInflicted = 1;
        }
        else if (hitent->baseType == eBaseTypeButton)
        {
            zEntEvent(hitent, eEventHit_Throw);
            zEntButton_Press((_zEntButton*)hitent, sThrowButtonMask);
            zEntButton_Hold((_zEntButton*)hitent, 0x12000);
        }
        else if (hitent->moreFlags & 0x10)
        {
            zEntEvent(hitent, eEventHit_Throw);
            zEntEvent(hitent, eEventHit);
        }
        // Retail tests the same flag twice; the second branch is unreachable.
        else if (hitent->moreFlags & 0x10)
        {
            zEntEvent(hitent, eEventHit_Throw);
            zEntEvent(hitent, eEventHit);
        }
        else if (hitent->baseType == eBaseTypeStatic &&
                 hitent->model->modelID == xStrHash("port_o_head_bind"))
        {
            damageInflicted = 1;
        }

        if (sFruitIsFreezy && hitent->model != NULL && zGooIs(hitent, dummy, 0))
        {
            pos = *(xVec3*)&thrown->ent->model->Mat->pos;
            pos.x = currcoll->tohit.x * currcoll->dist + pos.x;
            pos.y = currcoll->tohit.y * currcoll->dist + pos.y;
            pos.z = currcoll->tohit.z * currcoll->dist + pos.z;
            zFXGooFreeze(hitent->model->Data, &pos, (xVec3*)&hitent->model->Mat->pos);
            zEntEvent(hitent, eEventPlatPause, 0.25f, 0.0f, 0.0f, 0.0f);
        }

        if (zGooIs(hitent, dummy, 0) || (hitent->miscflags & 0x20))
        {
            damageInflicted = 1;
        }
    }

    return damageInflicted;
}

static void zThrownCollide_ThrowFruit(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                      F32* friction)
{
    U32 collfound;

    sThrowButtonMask = 0x80;

    if (zThrownCollide_CauseDamage(thrown, collis))
    {
        *bounce = 0.0f;
        *friction = 0.0f;
        thrown->killTimer = 1e-6f;
        return;
    }

    if (collis->colls[0].flags & k_HIT_IT)
    {
        F32 stackHeight;
        xEnt* landEnt;
        U32 i;
        F32 killTimer;
        F32 dx;
        F32 dz;
        F32 lerp;

        landEnt = (xEnt*)collis->colls[0].optr;
        if (landEnt != NULL && landEnt->baseType == eBaseTypeStatic &&
            zThrown_IsFruit((xEnt*)collis->colls[0].optr, &stackHeight))
        {
            killTimer = 1000.0f;
            for (i = 0; i < zThrownCount; i++)
            {
                if (zThrownList[i].ent == landEnt)
                {
                    killTimer = zThrownList[i].killTimer;
                }
            }

            dx = thrown->ent->model->Mat->pos.x - landEnt->model->Mat->pos.x;
            dz = thrown->ent->model->Mat->pos.z - landEnt->model->Mat->pos.z;
            if (dx * dx + dz * dz < 0.0225f && killTimer > 0.1f)
            {
                thrown->ent->model->Mat->pos.y = landEnt->model->Mat->pos.y + stackHeight;
                *bounce = 0.0f;
                *friction = 0.0f;
                thrown->stackEnt = landEnt;
                return;
            }
        }

        if (collis->colls[0].optr != NULL &&
            ((xEnt*)collis->colls[0].optr)->baseType == eBaseTypeButton &&
            xabs(collis->colls[0].norm.y) < 0.9659258f)
        {
            *friction = 1.0f;
            *bounce = globals.player.carry.fruitWallBounce;
            return;
        }

        lerp = xVec3Length(&thrown->vel);
        if (lerp > globals.player.carry.fruitFloorDecayMax)
        {
            *bounce = globals.player.carry.fruitFloorBounce;
            *friction = globals.player.carry.fruitFloorFriction;
        }
        else if (lerp < globals.player.carry.fruitFloorDecayMin)
        {
            *bounce = 0.0f;
            *friction = 0.0f;
        }
        else
        {
            lerp =
                (lerp - globals.player.carry.fruitFloorDecayMin) /
                (globals.player.carry.fruitFloorDecayMax - globals.player.carry.fruitFloorDecayMin);
            *bounce = lerp * globals.player.carry.fruitFloorBounce;
            *friction = lerp * globals.player.carry.fruitFloorFriction;
        }
        return;
    }

    collfound = 0;
    if (collis->env_eidx > collis->env_sidx)
    {
        collfound = collis->env_sidx;
    }
    else if (collis->dyn_eidx > collis->dyn_sidx)
    {
        collfound = collis->dyn_sidx;
    }
    else if (collis->stat_eidx > collis->stat_sidx)
    {
        collfound = collis->stat_sidx;
    }

    if (collfound != 0)
    {
        F32 stackHeight;
        xEnt* landEnt;
        U32 i;
        F32 killTimer;
        F32 dx;
        F32 dy;
        F32 dz;

        landEnt = (xEnt*)collis->colls[collfound].optr;
        if (landEnt != NULL && landEnt->baseType == eBaseTypeStatic &&
            zThrown_IsFruit(landEnt, &stackHeight))
        {
            killTimer = 1000.0f;
            for (i = 0; i < zThrownCount; i++)
            {
                if (zThrownList[i].ent == landEnt)
                {
                    killTimer = zThrownList[i].killTimer;
                }
            }

            dx = thrown->oldcollpos.x - landEnt->model->Mat->pos.x;
            dy = thrown->oldcollpos.y - landEnt->model->Mat->pos.y;
            dz = thrown->oldcollpos.z - landEnt->model->Mat->pos.z;
            if (dx * dx + dz * dz < 0.0225f && dy > 0.8f && killTimer > 0.1f)
            {
                thrown->ent->model->Mat->pos.x = thrown->oldcollpos.x;
                thrown->ent->model->Mat->pos.y = landEnt->model->Mat->pos.y + stackHeight;
                thrown->ent->model->Mat->pos.z = thrown->oldcollpos.z;
                *bounce = 0.0f;
                *friction = 0.0f;
                thrown->stackEnt = landEnt;
                return;
            }
        }

        *friction = 1.0f;
        if (collis->colls[collfound].norm.y < -0.5f)
        {
            *bounce = globals.player.carry.fruitCeilingBounce;
        }
        else
        {
            *bounce = globals.player.carry.fruitWallBounce;
        }
    }
    else
    {
        *bounce = 0.0f;
        *friction = 0.0f;
    }
}

static void zThrownCollide_ThrowFreeze(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                       F32* friction)
{
    sFruitIsFreezy = 1;
    zThrownCollide_ThrowFruit(thrown, collis, bounce, friction);
    sFruitIsFreezy = 0;
}

static void zThrownCollide_DestructObj(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                       F32* friction)
{
    S32 didDamage;

    sThrowButtonMask = 0x40;
    didDamage = zThrownCollide_CauseDamage(thrown, collis);
    *bounce = 0.0f;
    *friction = 0.0f;

    if (!didDamage || thrown->ent == NULL || thrown->ent->baseType != eBaseTypeNPC)
    {
        zEntEvent(thrown->ent, eEventDestroy);
    }
}

static void zThrownCollide_Tiki(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                F32* friction)
{
    S32 didDamage;

    sThrowButtonMask = 0x40;
    didDamage = zThrownCollide_CauseDamage(thrown, collis);
    *bounce = 0.0f;
    *friction = 0.0f;

    if (!didDamage)
    {
        zEntEvent(thrown->ent, eEventDestroy);
    }
}

static void zThrownCollide_StoneTiki(zThrownStruct* thrown, xEntCollis* collis, F32* bounce,
                                     F32* friction)
{
    // Not in the DWARF order (collidx, hitent), which changes the generated code.
    xEnt* hitent;
    U32 collidx;
    en_npctyp type;
    U32 collfound;

    *bounce = 0.0f;
    *friction = 0.0f;
    sThrowButtonMask = 0x40;
    zThrownCollide_CauseDamage(thrown, collis);

    for (collidx = k_XCOLLS_IDX_COUNT; collidx < 18 && (collis->colls[collidx].flags & k_HIT_IT);
         collidx++)
    {
        hitent = (xEnt*)collis->colls[collidx].optr;
        if (hitent != NULL)
        {
            if (hitent->baseType == eBaseTypeNPC)
            {
                type = (en_npctyp)((zNPC::base*)hitent)->itemType;
                if (type >= NME_TYPE_MINDY && type <= NME_TYPE_PATRICK)
                {
                    zEntEvent(thrown->ent, eEventKill);
                }
            }
            if (hitent->baseType == eBaseTypeTeleportBox)
            {
                zEntEvent(thrown->ent, eEventKill);
            }
            if (hitent->baseType == eBaseTypeStatic &&
                hitent->model->modelID == xStrHash("port_o_head_bind"))
            {
                zEntEvent(thrown->ent, eEventKill);
            }
        }
    }

    collfound = 0;
    if (collis->env_eidx > collis->env_sidx)
    {
        collfound = collis->env_sidx;
    }
    else if (collis->dyn_eidx > collis->dyn_sidx)
    {
        collfound = collis->dyn_sidx;
    }
    else if (collis->stat_eidx > collis->stat_sidx)
    {
        collfound = collis->stat_sidx;
    }

    if (collfound != 0)
    {
        if (collis->colls[collfound].norm.y < 0.707f)
        {
            *friction = 1.0f;
            *bounce = globals.player.carry.fruitWallBounce;
        }
    }
    else
    {
        *bounce = 0.0f;
        *friction = 0.0f;
    }
}

S32 zThrown_IsFruit(xEnt* ent, F32* stackHeight)
{
    ThrowableStats* stats = &zThrowableModels[1];

    while (stats->name != NULL)
    {
        if (stats->nameHash == ent->asset->modelInfoID ||
            stats->nameHashMINF == ent->asset->modelInfoID)
        {
            break;
        }
        stats++;
    }

    if (stats != NULL && stats->carry == &c_fruit)
    {
        if (stackHeight != NULL)
        {
            *stackHeight = stats->stackHeight;
        }
        return 1;
    }

    return 0;
}

S32 zThrown_IsStacked(xEnt* ent)
{
    for (U32 i = 0; i < zThrownCount; i++)
    {
        if (zThrownList[i].stackEnt == ent ||
            (zThrownList[i].stackEnt != NULL && zThrownList[i].ent == ent))
        {
            return 1;
        }
    }

    return 0;
}
