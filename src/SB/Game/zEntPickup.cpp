#include "zEntPickup.h"

#include <types.h>
#include <stdio.h>

#include "zCar.h"
#include "zEntPlayerBungeeState.h"
#include "zGame.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zMusic.h"
#include "zNMEMessenger.h"
#include "zNMESupport.h"
#include "zRewards.h"
#include "zSaveLoad.h"
#include "zScene.h"
#include "zEntPlayer.h"
#include "zEntPlayerFX.h"
#include "zEntSimpleObj.h"
#include "zFXRibbonPool.h"
#include "zStreamedSound.h"
#include "zUICustom.h"
#include "xCounter.h"
#include "xEvent.h"
#include "xFX.h"
#include "xLightKit.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xModel.h"
#include "xQuickCull.h"
#include "xRand.h"
#include "xScrFx.h"
#include "xserializer.h"
#include "xString.h"
#include "xstransvc.h"
#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

struct zEntPickup_cbData
{
    xScene* sc;
    F32 dt;
};

struct KnownPickups
{
    char dffFileName[40];
    char minfFileName[40];
};

struct LevelPickupScale
{
    U32 levelID;
    F32* scaling;
};

static zEntPickup** sPickupUpdateList;
static U32 sPickupUpdateCount;
static U32 sPickupUpdateTotal;
S32 gRenderPickups = 1;
zParEmitter* gEmitShinySparkles;

// clang-format off
zPickupAuraInfo zPickupAuraTable[5] =
{
    { 7.0f, 0.1f, { 0x0A, 0x64, 0xE0, 0xC8 } },
    { 3.0f, 0.0f, { 0xF0, 0xEB, 0xE0, 0x80 } },
    { 5.0f, 0.0f, { 0xC8, 0x64, 0xC8, 0xC8 } },
    { 4.0f, 0.5f, { 0xB4, 0x64, 0x2A, 0xC8 } },
    { 5.0f, 0.5f, { 0x32, 0xEB, 0xC8, 0xC8 } }
};

zPickupAuraInfo zManlinessAuraTable[5] =
{
    { 4.0f, 0.0f, { 0xF0, 0x00, 0x00, 0x80 } },
    { 4.0f, 0.0f, { 0xE0, 0xEB, 0x00, 0x80 } },
    { 4.0f, 0.0f, { 0x00, 0xEB, 0x00, 0x80 } },
    { 4.0f, 0.0f, { 0x00, 0x00, 0xE0, 0x80 } },
    { 4.0f, 0.0f, { 0xF0, 0x00, 0xE0, 0x80 } }
};

KnownPickups AnimatedPickups[3] =
{
    { "nitro_bind.dff", "nitro_bind.minf" },
    { "krabby_patty_bind.dff", "krabby_patty_bind.minf" },
    { "chest_treasure_bind.dff", "chest_treasure_bind.minf" }
};
// clang-format on

static F32* sPickupScaling;
static F32 sPickupScalingPlatform[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static F32 sPickupScalingDrive[5] = { 1.0f, 2.0f, 1.0f, 2.0f, 2.0f };
static F32 sPickupScalingSlide[5] = { 1.0f, 1.3f, 1.0f, 1.3f, 2.0f };

// clang-format off
static LevelPickupScale sCustomLevelScales[8] =
{
    { 'TR01', sPickupScalingSlide },
    { 'DE02', sPickupScalingSlide },
    { 'SC02', sPickupScalingSlide },
    { 'BB01', sPickupScalingDrive },
    { 'TT02', sPickupScalingDrive },
    { 'GG02', sPickupScalingDrive },
    { 'PT02', sPickupScalingDrive },
    { 0, NULL }
};

static RwMatrix sPickupOrientation =
{
    { 1.0f, 0.0f, 0.0f }, 0,
    { 0.0f, 1.0f, 0.0f }, 0,
    { 0.0f, 0.0f, 1.0f }, 0,
    { 0.0f, 0.0f, 0.0f }, 0
};
// clang-format on

static F32 sPickupOrientAngle;
static F32 PickupRenderMaxDistanceSqr = 2500.0f;
static xEntFrame gPickupFrame;
static xEntCollis gPickupCollis;
static _xCounter* keyCounter;
static xVec3 sGoofyGooberOffset;
static S32 sGoofyGooberGlareId;
static F32 sGoofyGooberPickupOrientAngleZ;
static F32 sGoofyGooberPickupOrientAngleY;
static U8 sGoofyGooberSoundPlayed;
static U8 sGoofyGooberEventSent = 1;
static xFXRibbon* ggRibbon;
static S32 ggCntSkipRibbonJoint;
static U8 sGoofyGooberNoTranslation;

// clang-format off
static const xFXRibbon::curve_node ggRaysCurveData[2] =
{
    { 0.0f, { 255, 255, 0, 128 }, 1.0f },
    { 1.0f, { 255, 255, 0, 50 }, 0.0f }
};

NMEHelpRib::RibData ribGGRays =
{
    { 1, 1, 0, 0 },
    { "fx_streak1", (xFXRibbon::curve_node*)ggRaysCurveData, 2, 0.35f, 0.6f },
    { 1, 1.0f, 1.0f },
    { NULL, 0 }
};

SparkyCurve sGGSparksCurveData[2] =
{
    { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
    { 1.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f }
};

static const SparkyEmitter sGGSparks =
{
    NPAR_TYP_COLORS,
    { 0, 0, { 0, 0 } },
    { 0.0f, 0.0f },
    { 0.25f, 0.25f },
    { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
    { 2.0f, 0.4f },
    { 1, 0, 0.0f, 0.0f },
    { 2.0f, -4.0f, 3.0f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { sGGSparksCurveData, 2, 1.0f, 2.0f }
};
// clang-format on

xLightKit* PickupLightKit;

// zEntPlayer.cpp's static, defined later in the wad (see the note in zEntButton.cpp).
extern zStreamedSoundList sPlayerOneLiners[4];

// zEntPlayerCar.cpp's static, defined later in the wad (same as above).
extern zCar playerCar;

// More zEntPlayer.cpp statics, defined later in the wad (same as above).
extern zStreamedSoundList sSlidingOneLiners;
extern S32 boulderRollShouldEnd;
extern S32 boulderRollShouldStart;
extern U32 sSpatulaGrabbed;
extern F32 sOldMusicVolume;
// zEntPlayer.cpp's static, defined later in the wad.
static U32 BoulderRollDoneCB();

static S32 CheckPickupAgainstPlayer(xEnt* cbent, void* cbdata);
void zEntPickup_SetState(zEntPickup* ent, U32 state);
void zEntPickup_UpdateReset();
void GGRibbonLoop(zEntPickup* ent);

static inline bool zEntPickup_NeedsUpdate(zEntPickup* ent)
{
    if (ent->state & 0x8)
    {
        if (ent->pickupFlags & 0x1)
        {
            return true;
        }

        return false;
    }

    if (!(ent->state & 0x1) || ent->drv.driver || ent->followTarget || ent->anim)
    {
        return true;
    }

    return false;
}

static inline void zEntPickup_SwapUpdate(U32 a, U32 b)
{
    zEntPickup* pa = sPickupUpdateList[a];
    zEntPickup* pb = sPickupUpdateList[b];

    sPickupUpdateList[a] = pb;
    pb->updateIndex = a;
    sPickupUpdateList[b] = pa;
    pa->updateIndex = b;
}

static inline void zEntPickup_UpdateListCheck(zEntPickup* ent)
{
    if (zEntPickup_NeedsUpdate(ent))
    {
        if (ent->updateIndex >= sPickupUpdateCount)
        {
            zEntPickup_SwapUpdate(ent->updateIndex, sPickupUpdateCount);
            sPickupUpdateCount++;
        }
    }
    else if (ent->updateIndex < sPickupUpdateCount)
    {
        zEntPickup_SwapUpdate(ent->updateIndex, sPickupUpdateCount - 1);
        sPickupUpdateCount--;
    }
}

static inline F32 zEntPickup_GetScale(zEntPickup* ent)
{
    switch (ent->p->pickupType)
    {
    case 0:
        return sPickupScaling[ent->p->pickupIndex];
    case 1:
        return sPickupScaling[1];
    default:
        return 1.0f;
    }
}

inline void zEntPickup_MakeDroppable(zEntPickup* ent, xVec3* pos, xBase* dropParent)
{
    ent->droppos = *(xVec3*)&ent->model->Mat->pos;
    *(xVec3*)&ent->model->Mat->pos = *pos;
    ent->state = 0x10;
    zEntPickup_UpdateListCheck(ent);

    xEntHide(ent);

    ent->dropParent = dropParent;
}

void zEntPickupSceneInit(U32 sceneID)
{
    U32 hudLightKitID;
    S32 i;

    hudLightKitID = xStrHash("hud_lightkit");
    PickupLightKit = (xLightKit*)xSTFindAsset(hudLightKitID, NULL);

    sPickupScaling = sPickupScalingPlatform;
    PickupRenderMaxDistanceSqr = 2500.0f;

    for (i = 0; sCustomLevelScales[i].scaling != NULL; i++)
    {
        if (sceneID == sCustomLevelScales[i].levelID)
        {
            PickupRenderMaxDistanceSqr = 10000.0f;
            sPickupScaling = sCustomLevelScales[i].scaling;
            break;
        }
    }
}

static void PickupFallPhysics(zEntPickup* ent, xScene* sc, F32 dt)
{
    gPickupFrame.mat = *(xMat4x3*)ent->model->Mat;
    gPickupFrame.mat.pos.y -= 0.8f;
    gPickupFrame.oldmat.pos = gPickupFrame.mat.pos;

    gPickupCollis.chk = 0x22;
    gPickupCollis.pen = 0x22;

    ent->vel.y -= 15.0f * dt;

    if (ent->vel.y < -20.0f)
    {
        ent->vel.y = -20.0f;
    }

    gPickupFrame.mat.pos.x += ent->vel.x * dt;
    gPickupFrame.mat.pos.y += ent->vel.y * dt;
    gPickupFrame.mat.pos.z += ent->vel.z * dt;

    if (!(ent->state & 0x100) || ent->vel.y < 0.0f)
    {
        ent->frame = &gPickupFrame;
        ent->collis = &gPickupCollis;

        xEntCollide(ent, sc, dt);

        ent->frame = NULL;
        ent->collis = NULL;
    }

    gPickupFrame.mat.pos.y += 0.8f;

    *(xMat4x3*)ent->model->Mat = gPickupFrame.mat;

    if ((gPickupCollis.colls[0].flags & 0x1) && ent->vel.y < 0.0f)
    {
        ent->vel.x = 0.0f;
        ent->vel.y = 0.0f;
        ent->vel.z = 0.0f;
        zEntPickup_SetState(ent, 0x1);
    }
}

void zEntPickupInit(void* ent, void* asset)
{
    zEntPickupInit((zEntPickup*)ent, (xEntAsset*)asset);
}

// No symbol: retail hoists the model ID out of the search loop and merges the three model
// stores, so this was an inline helper taking the ID by value.
static inline xModelInstance* zEntPickup_AllocAnimatedModel(U32 modelID, RpAtomic* modelData)
{
    for (U32 i = 0; i < 3; i++)
    {
        if (modelID == xStrHash(AnimatedPickups[i].dffFileName))
        {
            void* info = xSTFindAsset(xStrHash(AnimatedPickups[i].minfFileName), NULL);

            if (info)
            {
                return zEntRecurseModelInfo(info, NULL);
            }

            return xModelInstanceAlloc(modelData, NULL, 0, 0, NULL);
        }
    }

    return xModelInstanceAlloc(modelData, NULL, 0, 0, NULL);
}

void zEntPickupInit(zEntPickup* ent, xEntAsset* asset)
{
    xEntPickupAsset* pickupAsset = (xEntPickupAsset*)(asset + 1);
    U32 i;
    U32 tmpsize;

    zEntInit(ent, asset, 'PKUP');

    if (ent->linkCount)
    {
        ent->link = (xLinkAsset*)((xEntPickupAsset*)(ent->asset + 1) + 1);
    }
    else
    {
        ent->link = NULL;
    }

    ent->pflags = 0;
    ent->chkby = 0;
    ent->penby = 0;
    ent->update = (xEntUpdateCallback)zEntPickup_Update;
    ent->move = NULL;
    ent->eventFunc = zEntPickupEventCB;
    ent->followTarget = NULL;
    ent->followOffset.x = 0.0f;
    ent->followOffset.y = 0.0f;
    ent->followOffset.z = 0.0f;
    ent->dropParent = NULL;

    zAssetPickup* ptbl = (zAssetPickup*)(globals.pickupTable + 1);

    for (i = 0; i < globals.pickupTable->Count; i++)
    {
        if (pickupAsset->pickupHash == ptbl[i].pickupHash)
        {
            ptbl += i;
            break;
        }
    }

    RpAtomic* modelData = (RpAtomic*)xSTFindAsset(ptbl->modelID, &tmpsize);
    void* animData = NULL;

    if (!modelData)
    {
        ptbl = (zAssetPickup*)(globals.pickupTable + 1);
        modelData = (RpAtomic*)xSTFindAsset(ptbl->modelID, &tmpsize);
    }

    if (ptbl->animID)
    {
        animData = xSTFindAsset(ptbl->animID, &tmpsize);

        if (iAnimBoneCount(animData) != iModelNumBones(modelData))
        {
            animData = NULL;
        }
        else
        {
            ent->model = zEntPickup_AllocAnimatedModel(ptbl->modelID, modelData);
        }
    }
    else
    {
        ent->model = xModelInstanceAlloc(modelData, ent, 0, 0, NULL);
    }

    ent->model->Flags &= (U16)~0x2;
    ent->model->modelID = ptbl->modelID;
    ent->simpShadow = &ent->simpShadow_embedded;

    xShadowSimple_CacheInit(ent->simpShadow, ent, 80);

    ent->state = 0x81;
    ent->p = ptbl;
    ent->pickupFlags = 0;

    if (pickupAsset->pickupFlags & 0x1)
    {
        ent->pickupFlags |= 0x1;
    }

    if (pickupAsset->pickupFlags & 0x2)
    {
        ent->pickupFlags |= 0x2;
    }

    ent->anim = animData;
    ent->animTime = 0.0f;
    ent->timer = 0.0f;
    ent->fx_scale = zEntPickup_GetScale(ent);
    ent->flyflags = 0;
    ent->reward_timer = 0.0f;

    zEntReset(ent);

    ent->bound.type = XBOUND_TYPE_SPHERE;
    ent->bound.sph.r = 1.0f;

    xVec3Add(&ent->bound.sph.center, (xVec3*)&ent->model->Mat->pos,
             (xVec3*)&ent->model->Data->boundingSphere.center);

    xEntDriveInit(&ent->drv, ent);
}

void zEntPickup_Setup(zEntPickup* p)
{
    zEntSetup(p);
    keyCounter = (_xCounter*)zSceneFindObject(xStrHash("KEY_COUNTER"));
}

void zEntPickup_Setup()
{
    // not in the DWARF order (zsc, i, base): that order changes the generated code
    xBase* base;
    zScene* zsc;
    S32 i;
    U32 j;
    zEntPickup* p;
    xVec3* srcPos;

    zsc = globals.sceneCur;

    for (i = 0; i < zsc->num_base; i++)
    {
        base = zsc->base[i];

        if (base)
        {
            for (j = 0; j < base->linkCount; j++)
            {
                if (base->link && base->link[j].dstEvent == eEventDrop)
                {
                    p = (zEntPickup*)zSceneFindObject(base->link[j].dstAssetID);

                    if (p && p->baseType == eBaseTypePickup && p->state != 0x10)
                    {
                        srcPos = &p->asset->pos;

                        switch (base->baseType)
                        {
                        case eBaseTypeVillain:
                            p->flg_opts |= 0x1;
                            p->flg_opts |= 0x2;
                            p->useThisEntPos = (xEnt*)base;
                        case eBaseTypeStatic:
                        case eBaseTypeDynamic:
                        case eBaseTypeButton:
                        case eBaseTypeDestructObj:
                        case eBaseTypeLobMaster:
                            srcPos = &((xEnt*)base)->asset->pos;
                        }

                        zEntPickup_MakeDroppable(p, srcPos, NULL);
                    }
                }
            }
        }
    }
}

void zEntPickupEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                       U32)
{
    zEntPickup* p = (zEntPickup*)to;
    xEntFrame frame;

    switch (toEvent)
    {
    case eEventVisible:
    case eEventFastVisible:
        if (!(p->state & 0x8))
        {
            xEntShow(p);
        }
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(p);
        break;
    case eEventCollision_Visible_On:
        if (!(p->state & 0x8))
        {
            p->pickupFlags |= 0x2;
            xEntShow(p);
        }
        break;
    case eEventCollision_Visible_Off:
        p->pickupFlags &= (U8)~0x2;
        xEntHide(p);
        break;
    case eEventReset:
        zEntPickup_Reset(p);
        break;
    case eEventPickup:
        if (toParam && (p->state & 0x1))
        {
#ifdef PLATFORM_PC
            // Online co-op (docs/MULTIPLAYER.md 13): on the guest, a token or
            // chest its own script gives is asked of the host. FALSE at once
            // when co-op is off.
            if (iCoopPickupGiven((xBase*)p))
            {
                break;
            }
#endif
            zEntPickup_DoPickup(p, toParam[0] ? true : false);
        }
        break;
    case eEventDrop:
        if (p->state == 0x10)
        {
            zEntPickup_Drop(p);
        }
        break;
    case eEventFollowTarget:
        p->followTarget = toParamWidget;
        p->followOffset.x = toParam[0];
        p->followOffset.y = toParam[1];
        p->followOffset.z = toParam[2];
        break;
    case eEventMount:
        frame.mat = *(xMat4x3*)p->model->Mat;
        p->frame = &frame;
        xEntDriveMount(&p->drv, (xEnt*)toParamWidget, 0.00001f, NULL);
        p->frame = NULL;
        break;
    case eEventDismount:
        frame.mat = *(xMat4x3*)p->model->Mat;
        p->frame = &frame;
        xEntDriveDismount(&p->drv, 0.00001f);
        p->frame = NULL;
        break;
    }

    zEntPickup_UpdateListCheck(p);
}

void zEntPickup_CheckAllPickupsAgainstPlayer(xScene* sc, F32 dt)
{
    zEntPickup_cbData cbdata;

    cbdata.sc = sc;
    cbdata.dt = dt;

    if (zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_MagneticPickups))
    {
        xQCData qcd;
        xBound bound = xglobals->___player_ent_dont_use_directly->bound;

        bound.sph.r *= 4.0f;
        xQuickCullForBound(&qcd, &bound);

        xGridCheckPosition(&colls_grid,
                           (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                           &qcd, CheckPickupAgainstPlayer, &cbdata);
    }
    else
    {
        xGridCheckPosition(&colls_grid,
                           (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                           &xglobals->___player_ent_dont_use_directly->bound.qcd,
                           CheckPickupAgainstPlayer, &cbdata);
    }
}

// top/bottom, list and ribhelp are not in the PS2 DWARF, but retail copies the pickup position
// twice into stack temporaries and computes the list and ribbon addresses once.
static S32 CheckPickupAgainstPlayer(xEnt* cbent, void* cbdata)
{
    if (cbent->baseType != eBaseTypePickup)
    {
        return 1;
    }

    zEntPickup* ent = (zEntPickup*)cbent;
    F32 dt = ((zEntPickup_cbData*)cbdata)->dt;

    if ((ent->state & 0x80) && !(ent->state & 0x8) && !(ent->state & 0x4) && !(ent->state & 0x20) &&
        !xglobals->___player_ent_dont_use_directly->IsDead())
    {
        if (ent->timer > 0.0f)
        {
            ent->timer -= dt;

            if (ent->timer <= 0.0f)
            {
                ent->timer = 0.0f;
            }
        }
        else
        {
            zEnt* plent = xglobals->___player_ent_dont_use_directly;
            F32 distsqr;

            {
                F32 dx__ = plent->bound.sph.center.x - ent->model->Mat->pos.x;
                F32 dy__ = plent->bound.sph.center.y - ent->model->Mat->pos.y;
                F32 dz__ = plent->bound.sph.center.z - ent->model->Mat->pos.z;

                distsqr = SQR(dx__) + SQR(dy__) + SQR(dz__);
            }

            F32 distance =
                (gCurrentPlayer == eCurrentPlayerCar || gCurrentPlayer == eCurrentPlayerSpongePat) ?
                    8.0f :
                    4.0f;
            F32 chkdist = distance * plent->bound.sph.r;

            if (zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_MagneticPickups))
            {
                chkdist *= 4.0f;
            }

            if (distsqr <= SQR(chkdist) && (ent->pickupFlags & 0x2))
            {
#ifdef PLATFORM_PC
                // Online co-op (6.5): on the guest, a pickup the host owns is
                // asked of the host; it is taken only when the host says so.
                // FALSE at once when co-op is off.
                if (iCoopPickupTouch((xBase*)ent))
                {
                    return 1;
                }
#endif
                if (ent->p->pickupType == 0)
                {
                    switch (ent->p->pickupIndex)
                    {
                    case 3:
                        if (globals.player.s->Health < globals.player.s->MaxHealth)
                        {
                            zEntPickup_DoPickup(ent, false);
                        }
                        break;
                    case 2:
                        if (bungee_state::active())
                        {
                            chkdist = distance * plent->bound.sph.r;
                            distance = distsqr;
                        }
                        else
                        {
                            chkdist = distance * plent->bound.sph.r;

                            xVec3 top;
                            xVec3 bottom;

                            xVec3Copy(&top, (xVec3*)&ent->model->Mat->pos);
                            xVec3Copy(&bottom, (xVec3*)&ent->model->Mat->pos);

                            top.y += 0.5f * chkdist;
                            bottom.y -= 0.5f * chkdist;

                            F32 dist2_1;
                            F32 dist2_2;

                            {
                                F32 dx__ = plent->bound.sph.center.x - top.x;
                                F32 dy__ = plent->bound.sph.center.y - top.y;
                                F32 dz__ = plent->bound.sph.center.z - top.z;

                                dist2_1 = SQR(dx__) + SQR(dy__) + SQR(dz__);
                            }

                            {
                                F32 dx__ = plent->bound.sph.center.x - bottom.x;
                                F32 dy__ = plent->bound.sph.center.y - bottom.y;
                                F32 dz__ = plent->bound.sph.center.z - bottom.z;

                                dist2_2 = SQR(dx__) + SQR(dy__) + SQR(dz__);
                            }

                            distance = MIN(dist2_1, dist2_2);
                        }

                        if (distance <= SQR(chkdist))
                        {
                            ent->grab_pos = *xEntGetPos(ent);
                            ent->flyflags |= 0x1;
                            zEntPickup_SetState(ent, 0x4);
                            zEntPickup_GivePickup(ent);

                            zStreamedSoundList* list = &sPlayerOneLiners[gCurrentPlayer];
                            if (ePlayerStreamSnd_GotGGToken < list->mNumSounds)
                            {
                                list->mSounds[ePlayerStreamSnd_GotGGToken].PlaySoundDelayed(0.0f);
                            }

                            sGoofyGooberEventSent = 0;
                            sGoofyGooberSoundPlayed = 0;

                            ribGGRays.FindTexture();

                            NMEHelpRib::RibData* ribhelp = &ribGGRays;
                            ggRibbon = zFXRibbonPoolNew();
                            if (ggRibbon != NULL)
                            {
                                ribhelp->ConfigRibbon(ggRibbon);
                                ggCntSkipRibbonJoint = (S8)ribhelp->updinfo.frq_ribJoint;
                            }

                            sGoofyGooberNoTranslation = 0;
                            ent->reward_timer = 5.0f;
                        }
                        break;
                    case 0:
                        if (gCurrentPlayer == eCurrentPlayerCar)
                        {
                            chkdist = 8.0f * plent->bound.sph.r;

                            if (zUICustomExtrasManager::IsCheatEnabled(
                                    eExtrasScreenCheats_MagneticPickups))
                            {
                                chkdist *= 4.0f;
                            }

                            if (playerCar.CanPickupNitros() && distsqr <= SQR(chkdist))
                            {
                                zEntPickup_DoPickup(ent, false);
                            }
                        }
                        break;
                    case 4:
                        for (S32 i = 0; i < 5; i++)
                        {
                            xFXFireworksLaunch(0.5f + xurand(), (xVec3*)&ent->model->Mat->pos,
                                               0.5f * xurand() + 0.1f);
                        }

                        zEntPickup_DoPickup(ent, false);
                        break;
                    }
                }
                else if (ent->p->pickupType == 1)
                {
                    switch (ent->p->pickupIndex)
                    {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                        zEntPickup_DoPickup(ent, false);
                        break;
                    }
                }
            }
        }
    }

    return 1;
}

#ifdef PLATFORM_PC
void zEntPickup_PcTakeTouched(zEntPickup* ent)
{
    // The touch's way differs from a script's give only here: the token's
    // eEventPickup is sent two seconds into the reward.
    zEntPickup_DoPickup(ent, false);
    sGoofyGooberEventSent = 0;
}
#endif

// The PS2 DWARF lists no locals here; the list/ribhelp/pos pointers give retail's single
// address computations.
void zEntPickup_DoPickup(zEntPickup* ent, bool instant)
{
    if (ent->p->pickupType == 0 && ent->p->pickupIndex == 2)
    {
        ent->grab_pos = *xEntGetPos(ent);
        ent->flyflags |= 0x1;
        zEntPickup_SetState(ent, 0x4);
        zEntPickup_GivePickup(ent);

        if (gCurrentPlayer == eCurrentPlayerSpongeBob || gCurrentPlayer == eCurrentPlayerPatrick)
        {
            zStreamedSoundList* list = &sPlayerOneLiners[gCurrentPlayer];
            if (ePlayerStreamSnd_GotGGToken < list->mNumSounds)
            {
                list->mSounds[ePlayerStreamSnd_GotGGToken].PlaySoundDelayed(0.0f);
            }
        }

        sGoofyGooberEventSent = 1;
        sGoofyGooberSoundPlayed = 0;
        sGoofyGooberNoTranslation = 0;

        ribGGRays.FindTexture();

        NMEHelpRib::RibData* ribhelp = &ribGGRays;
        ggRibbon = zFXRibbonPoolNew();
        if (ggRibbon != NULL)
        {
            ribhelp->ConfigRibbon(ggRibbon);
            ggCntSkipRibbonJoint = (S8)ribhelp->updinfo.frq_ribJoint;
        }

        ent->reward_timer = 5.0f;
    }
    else if (!instant)
    {
        ent->grab_pos = *xEntGetPos(ent);
        ent->flyflags |= 0x1;
        zEntEvent(ent, eEventPickup);
    }
    else
    {
        ent->grab_pos = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        xVec3* pos = xEntGetPos(ent);
        pos->x = ent->grab_pos.x;
        pos->y = ent->grab_pos.y;
        pos->z = ent->grab_pos.z;
        zEntPickup_GivePickup(ent);
    }

    zEntPickup_SetState(ent, 0x4);
}

void zEntPickup_GivePickup(zEntPickup* ent)
{
    switch (ent->p->pickupType)
    {
    case 1:
        switch (ent->p->pickupIndex)
        {
        case 4:
            globals.player.rewardsMgr->IncManlinessPoints(globals.player.g.ShinyValuePurple, false);
            break;
        case 3:
            globals.player.rewardsMgr->IncManlinessPoints(globals.player.g.ShinyValueBlue, false);
            break;
        case 2:
            globals.player.rewardsMgr->IncManlinessPoints(globals.player.g.ShinyValueGreen, false);
            break;
        case 1:
            globals.player.rewardsMgr->IncManlinessPoints(globals.player.g.ShinyValueYellow, false);
            break;
        case 0:
            globals.player.rewardsMgr->IncManlinessPoints(globals.player.g.ShinyValueRed, false);
            break;
        }

        if (globals.player.InvManlinessPoints > 99999)
        {
            globals.player.InvManlinessPoints = 99999;
        }

        switch (gCurrentPlayer)
        {
        case eCurrentPlayerSpongeBob:
        case eCurrentPlayerPatrick:
            zEntPlayer_SNDPlayStream(ePlayerStreamSnd_GotManliness);
            break;
        case eCurrentPlayerCar:
        case eCurrentPlayerSpongePat:
            break;
        }
        break;
    case 0:
        switch (ent->p->pickupIndex)
        {
        case 3:
            if (gCurrentPlayer == eCurrentPlayerCar)
            {
                playerCar.GiveHealth(1);
            }
            else if (globals.player.s->Health != 0)
            {
                globals.player.s->Health++;

                if (globals.player.s->Health > globals.player.s->MaxHealth)
                {
                    globals.player.s->Health = globals.player.s->MaxHealth;
                }

                switch (gCurrentPlayer)
                {
                case eCurrentPlayerSpongeBob:
                case eCurrentPlayerPatrick:
                    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_GotHealth);
                    break;
                case eCurrentPlayerSpongePat:
                    // Retail re-tests gCurrentPlayer here (probably an inlined helper).
                    if (gCurrentPlayer == eCurrentPlayerSpongePat)
                    {
                        sSlidingOneLiners.PlaySound(3);
                    }
                    break;
                }

                if (zEntPlayerFX::SFX::Snd[4] != -1 &&
                    !xSndMgrIsPlaying(zEntPlayerFX::SFX::SndGroup[4]))
                {
                    zEntPlayerFX::SFX::Snd[4] = xSndMgrPlay(zEntPlayerFX::SFX::SndGroup[4], 0, NULL,
                                                            NULL, NULL, NULL, NULL);
                }
            }
            break;
        case 2:
            globals.player.InvGoofyGoober++;

            switch (gCurrentPlayer)
            {
            case eCurrentPlayerSpongeBob:
            case eCurrentPlayerPatrick:
            case eCurrentPlayerCar:
            case eCurrentPlayerSpongePat:
            {
                if (player_is_sponge_ball())
                {
                    boulderRollShouldStart = 0;
                    boulderRollShouldEnd = 1;

                    if (gCurrentPlayer == eCurrentPlayerSpongeBob)
                    {
                        BoulderRollDoneCB();
                        lastgCurrentPlayer = eCurrentPlayerCount;
                        gCurrentPlayer = eCurrentPlayerSpongeBob;
                    }
                }

                zEntPlayerControlOff(CONTROL_OWNER_REWARDANIM);

                sSpatulaGrabbed = 1;

                if (globals.player.ControlOffTimer < 0.1f)
                {
                    globals.player.ControlOffTimer = 0.1f;
                }

                zNMEMsg_AreaNotify(NULL, NME_MID_PLYRSPATULA, 30.0f, 0x104, NME_TYPE_UNKNOWN);

                if (!(globals.player.ControlOff & CONTROL_OWNER_TALK_BOX))
                {
                    sOldMusicVolume = zMusicGetCurrentVolume(0);
                    zMusicSetVolume(gDialogueMusicLevel, 0.2f);
                }

                xMat4x3 mat;
                *(RwMatrix*)&mat = sPickupOrientation;
                xVec3Copy(&mat.pos, (xVec3*)&ent->model->Mat->pos);
                *ent->model->Mat = *(RwMatrix*)&mat;

                if (globals.autoSaveFeature)
                {
                    zSaveLoadPreAutoSave(true);
                }
                break;
            }
            }
            break;
        case 4:
            globals.player.InvTreasureChest[zSceneGetLevelIndex()]++;
            globals.player.InvTreasureChestCurrentLevel =
                globals.player.InvTreasureChest[zSceneGetLevelIndex()];
            zUICustomExtrasManager::GivePlayerTreasureChest(1);

            switch (gCurrentPlayer)
            {
            case eCurrentPlayerSpongeBob:
            case eCurrentPlayerPatrick:
                zEntPlayer_SNDPlayStream(ePlayerStreamSnd_GotTreasureChest);
                break;
            case eCurrentPlayerCar:
            case eCurrentPlayerSpongePat:
                break;
            }

            if (zEntPlayerFX::SFX::Snd[2] != -1 &&
                !xSndMgrIsPlaying(zEntPlayerFX::SFX::SndGroup[2]))
            {
                zEntPlayerFX::SFX::Snd[2] =
                    xSndMgrPlay(zEntPlayerFX::SFX::SndGroup[2], 0, NULL, NULL, NULL, NULL, NULL);
            }
            break;
        case 0:
            playerCar.AddNitros(1);
            break;
        }
        break;
    }
}

// Not in the PS2 DWARF: model/minst/scale/scaled/boundMat (probably an inlined helper's) and the
// Goofy Goober mat/pos/dir.
void zEntPickup_Update(zEntPickup* ent, xScene* sc, F32 dt)
{
    xEntFrame frame;

    if (!gRenderPickups || zGameIsPaused())
    {
        return;
    }

    if (ent->state & 0x8)
    {
        if ((ent->pickupFlags & 0x1) && ent->timer > 0.0f)
        {
            ent->timer -= dt;

            if (ent->timer <= 0.0f)
            {
                ent->timer = 0.0f;
                zEntPickup_SetState(ent, 0x20);
                xEntShow(ent);
            }
            else
            {
                return;
            }
        }
        else
        {
            return;
        }
    }

    if (ent->drv.old_driver || ent->drv.driver)
    {
        frame.mat = *(xMat4x3*)ent->model->Mat;

        ent->frame = &frame;
        xEntDriveUpdate(&ent->drv, sc, dt, NULL);
        ent->frame = NULL;

        *(xMat4x3*)ent->model->Mat = frame.mat;

        xVec3Add(&ent->bound.sph.center, (xVec3*)&ent->model->Mat->pos,
                 (xVec3*)&ent->model->Data->boundingSphere.center);
        xQuickCullForBound(&ent->bound.qcd, &ent->bound);
        zGridUpdateEnt(ent);
    }

    if (!xEntIsVisible(ent) || (ent->model->Flags & 0x400))
    {
        return;
    }

    if (ent->anim)
    {
        F32 duration = iAnimDuration(ent->anim);

        ent->animTime += dt;

        if (ent->animTime >= duration)
        {
            ent->animTime -= duration;
        }

        // not in the DWARF order (q0, t0): that order changes the generated code
        xVec3* t0;
        xQuat* q0 = (xQuat*)g_iAnim_workBuffer;
        t0 = (xVec3*)(q0 + 65);

        iAnimEval(ent->anim, ent->animTime, 0, t0, q0);
        iModelAnimMatrices(ent->model->Data, q0, t0, ent->model->Mat + 1);

        xModelInstance* model = ent->model;
        xModelInstance* minst = model;

        model->combinedAnimBound.upper.x = -1e38f;

        F32 scale = ent->fx_scale;

        do
        {
            RwMatrix scaled;
            RwMatrix* mat = minst->Mat;
            RwMatrix* boundMat = mat;

            if (scale != 1.0f)
            {
                xVec3SMul((xVec3*)&scaled.right, (xVec3*)&mat->right, scale);
                xVec3SMul((xVec3*)&scaled.up, (xVec3*)&mat->up, scale);
                xVec3SMul((xVec3*)&scaled.at, (xVec3*)&mat->at, scale);
                scaled.flags = 0;
                scaled.pos.x = mat->pos.x;
                scaled.pos.y = mat->pos.y;
                scaled.pos.z = mat->pos.z;
                boundMat = &scaled;
            }

            iModelAnimBoundWorld(&minst->animBound, &model->combinedAnimBound, minst->Data,
                                 boundMat, minst->Mat + 1);
            minst = minst->Next;
        } while (minst);
    }

    switch (ent->state & 0x7F)
    {
    case 0x1:
        if (ent->followTarget)
        {
            xVec3* p = xEntGetPos((xEnt*)ent->followTarget);

            if (p)
            {
                ent->model->Mat->pos = *(RwV3d*)p;
                xVec3AddTo((xVec3*)&ent->model->Mat->pos, &ent->followOffset);
            }
        }
        break;
    case 0x2:
        PickupFallPhysics(ent, sc, dt);
        break;
    case 0x20:
        if (!globals.player.ControlOff)
        {
            ent->timer -= dt;
        }

        if (ent->timer <= 0.0f)
        {
            zEntPickup_SetState(ent, 0x1);
        }
        break;
    case 0x4:
    {
        F32 chkdist;
        zEnt* plent = xglobals->___player_ent_dont_use_directly;
        xVec3* player = xEntGetCenter(plent);
        chkdist = plent->bound.sph.r;
        F32 distsqr;

        {
            F32 dx__ = plent->bound.sph.center.x - ent->model->Mat->pos.x;
            F32 dy__ = plent->bound.sph.center.y - ent->model->Mat->pos.y;
            F32 dz__ = plent->bound.sph.center.z - ent->model->Mat->pos.z;

            distsqr = SQR(dx__) + SQR(dy__) + SQR(dz__);
        }
        xVec3 vec;

        if (ent->p->pickupType == 0 && ent->p->pickupIndex == 2)
        {
            ent->reward_timer -= dt;

            if (ent->reward_timer <= 3.0f && !sGoofyGooberEventSent)
            {
                zEntEvent(ent, eEventPickup);
                sGoofyGooberEventSent = 1;
            }

            if (ent->reward_timer <= 3.0f && !sGoofyGooberSoundPlayed)
            {
                zEntPlayer_SNDPlay(ePlayerSnd_PickupSpatula, true);
                sGoofyGooberSoundPlayed = 1;
            }

            if (ent->reward_timer < 0.0f)
            {
                xEntHide(ent);
                zEntPickup_SetState(ent, 0x8);
            }
            else
            {
                if (ent->reward_timer < 3.0f)
                {
                    sGoofyGooberPickupOrientAngleY =
                        xatan2(xglobals->cam->mat.pos.x - ent->model->Mat->pos.x,
                               xglobals->cam->mat.pos.z - ent->model->Mat->pos.z);

                    if (ggRibbon)
                    {
                        zFXRibbonPoolDelete(ggRibbon);
                        ggRibbon = NULL;
                    }
                }
                else
                {
                    sGoofyGooberPickupOrientAngleY += 8.0f * PI * dt;
                    if (sGoofyGooberPickupOrientAngleY > 6.285714f)
                    {
                        sGoofyGooberPickupOrientAngleY -= 6.285714f;
                    }

                    GGRibbonLoop(ent);
                }

                sGoofyGooberPickupOrientAngleZ += 8.0f * PI * dt;
                if (sGoofyGooberPickupOrientAngleZ > 6.285714f)
                {
                    sGoofyGooberPickupOrientAngleZ -= 6.285714f;
                }

                xVec3 dir;
                xVec3 pos;
                xMat4x3 mat = *(xMat4x3*)ent->model->Mat;

                xMat3x3RotZ(&mat, sGoofyGooberPickupOrientAngleZ);
                xMat3x3RMulRotY(&mat, &mat, sGoofyGooberPickupOrientAngleY);

                pos.x = 0.4f * ent->fx_scale * isin(0.25f * PI);
                pos.y = 0.4f * ent->fx_scale * icos(0.25f * PI);
                pos.z = 0.0f;

                dir.x = -2.0f * pos.y;
                dir.y = 2.0f * pos.x;
                dir.z = 0.0f;

                xMat3x3RMulVec(&pos, &mat, &pos);
                xVec3AddTo(&pos, &mat.pos);
                xMat3x3RMulVec(&dir, &mat, &dir);

                sGGSparks.EmitInDir(&pos, &dir, 1);

                xMat3x3RotY((xMat3x3*)ent->model->Mat, sGoofyGooberPickupOrientAngleY);

                if (ent->reward_timer < 3.5f && ent->reward_timer > 3.0f &&
                    !sGoofyGooberNoTranslation)
                {
                    ent->fx_scale += 3.0f * dt;
                }
                else if (ent->reward_timer > 3.0f && !sGoofyGooberNoTranslation)
                {
                    ent->model->Mat->pos.y += 1.1999999f * dt;
                }
            }
        }
        else if (distsqr <= SQR(chkdist))
        {
            xEntHide(ent);
            zEntPickup_GivePickup(ent);
            zEntPickup_SetState(ent, 0x8);

            ent->timer = 10.0f;
            ent->model->Mat->pos.x = ent->asset->pos.x;
            ent->model->Mat->pos.y = ent->asset->pos.y;
            ent->model->Mat->pos.z = ent->asset->pos.z;
        }
        else
        {
            xVec3* dest = xEntGetPos(ent);
            F32 mult;

            vec.x = player->x - dest->x;
            vec.y = player->y - dest->y;
            vec.z = player->z - dest->z;

            xVec3Normalize(&vec, &vec);
            xVec3Copy(&ent->vel, &vec);

            {
                F32 dx__ = player->x - ent->grab_pos.x;
                F32 dy__ = player->y - ent->grab_pos.y;
                F32 dz__ = player->z - ent->grab_pos.z;

                mult = SQR(dx__) + SQR(dy__) + SQR(dz__);
            }

            if (mult < 1.2f)
            {
                mult = 1.2f;
            }
            else if (mult > 30.0f)
            {
                mult = 30.0f;
            }

            ent->model->Mat->pos.x += mult * (ent->vel.x * dt);
            ent->model->Mat->pos.y += mult * (ent->vel.y * dt);
            ent->model->Mat->pos.z += mult * (ent->vel.z * dt);
        }
        break;
    }
    case 0x40:
        break;
    }
}

void zEntPickup_SetState(zEntPickup* ent, U32 state)
{
    ent->state = (ent->state & ~0x7F) | state;
    zEntPickup_UpdateListCheck(ent);
}

void zEntPickup_UpdateInit(zEntPickup* plist, U32 pcount)
{
    U32 i;

    sPickupUpdateList = (zEntPickup**)xMemAlloc(gActiveHeap, pcount * sizeof(zEntPickup*), 0);
    sPickupUpdateCount = 0;
    sPickupUpdateTotal = pcount;

    for (i = 0; i < pcount; i++)
    {
        sPickupUpdateList[i] = &plist[i];
        sPickupUpdateList[i]->updateIndex = i;
    }

    zEntPickup_UpdateReset();
}

void zEntPickup_UpdateReset()
{
    U32 i;

    sPickupUpdateCount = 0;

    for (i = 0; i < sPickupUpdateTotal; i++)
    {
        if (zEntPickup_NeedsUpdate(sPickupUpdateList[i]))
        {
            zEntPickup_SwapUpdate(i, sPickupUpdateCount);
            sPickupUpdateCount++;
        }
    }
}

void zEntPickup_UpdateAll(F32 dt)
{
    S32 i;

    for (i = 0; i < (S32)sPickupUpdateCount; i++)
    {
        if (xBaseIsEnabled(sPickupUpdateList[i]))
        {
            zEntPickup* ent = sPickupUpdateList[i];

            zEntPickup_Update(ent, globals.sceneCur, dt);

            if (ent != sPickupUpdateList[i])
            {
                i--;
            }
        }
    }
}

void zEntPickup_Reset(zEntPickup* ent)
{
    zEntReset(ent);

    ent->state = 0x81;
    ent->animTime = 0.0f;
    ent->timer = 0.0f;
    ent->vel.x = ent->vel.y = ent->vel.z = 0.0f;
    ent->bound.type = XBOUND_TYPE_SPHERE;

    ent->fx_scale = zEntPickup_GetScale(ent);

    ent->flyflags = 0;

    gEmitShinySparkles = zParEmitterFind("PAREMIT_SHINY_SPARKLES");
}

void zEntPickup_Save(zEntPickup* ent, xSerial* s)
{
    zEntSave(ent, s);

    s->Write_bn(ent->state, 8);

    if (ent->pickupFlags & 0x2)
    {
        s->Write_b1(1);
    }
    else
    {
        s->Write_b1(0);
    }
}

void zEntPickup_Load(zEntPickup* ent, xSerial* s)
{
    S32 is_a_dropper = 0;
    U32 temp;
    U32 temp2;

    zEntLoad(ent, s);

    if (ent->state & 0x10)
    {
        is_a_dropper = 1;
    }

    temp = temp2 = 0;

    s->Read_bn(&temp, 8);
    s->Read_b1(&temp2);

    if (temp & 0x4)
    {
        zEntPickup_SetState(ent, 0x8);
        xEntHide(ent);
    }

    ent->state = temp;

    if (temp2)
    {
        ent->pickupFlags |= 0x2;
    }
    else
    {
        ent->pickupFlags &= (U8)~0x2;
    }

    if (ent->pickupFlags & 0x1)
    {
        zEntPickup_SetState(ent, 0x1);
        xEntShow(ent);
    }

    if (is_a_dropper && (ent->state & 0x23))
    {
        if (ent->dropParent && ent->dropParent->baseType == eBaseTypeStatic &&
            !(ent->dropParent->baseFlags & 0x2) &&
            (((zEntSimpleObj*)ent->dropParent)->sflags & 0x1))
        {
            printf("Put back  %f %f %f    %f %f %f\n", ent->model->Mat->pos.x,
                   ent->model->Mat->pos.y, ent->model->Mat->pos.z, ent->droppos.x, ent->droppos.y,
                   ent->droppos.z);

            ent->state = 0x10;

            xEntHide(ent);
        }
        else
        {
            xVec3Copy((xVec3*)&ent->model->Mat->pos, &ent->droppos);
        }
    }
}

void zEntPickup_Drop(zEntPickup* ent)
{
    ent->state = 0x182;
    zEntPickup_UpdateListCheck(ent);

    xEntShow(ent);

    if ((ent->flg_opts & 0x2) && ent->useThisEntPos)
    {
        xVec3Copy(&ent->droppos, xEntGetPos(ent->useThisEntPos));
        xVec3Copy((xVec3*)&ent->model->Mat->pos, xEntGetPos(ent->useThisEntPos));
    }

    F32 b = xsqrt(75.0f);
    F32 c = ent->model->Mat->pos.y - ent->droppos.y;

    ent->vel.x = 0.0f;
    ent->vel.y = b;
    ent->vel.z = 0.0f;

    if (!(ent->flg_opts & 0x1))
    {
        F32 sol[2];
        U32 solcnt = xMathSolveQuadratic(-7.5f, b, c, &sol[0], &sol[1]);

        if (solcnt)
        {
            if (solcnt == 2 && sol[1] > sol[0])
            {
                sol[0] = sol[1];
            }

            if (sol[0] > 0.0001f)
            {
                sol[0] = 1.0f / sol[0];

                ent->vel.x = sol[0] * (ent->droppos.x - ent->model->Mat->pos.x);
                ent->vel.z = sol[0] * (ent->droppos.z - ent->model->Mat->pos.z);
            }
        }
    }

    ent->timer = 1.2f;
}

// mat, aura and inYRange are not in the PS2 DWARF, but retail keeps Mat and the aura entry in
// saved registers and materializes the y-range test as a bool.
void zEntPickup_RenderList(zEntPickup* plist, U32 pcount)
{
    U32 i;
    RpAtomic* imodel;
    xVec3* campos;
    F32 dist2;

    if (!gRenderPickups || zGameIsPaused())
    {
        return;
    }

    xLightKit_Enable(PickupLightKit, globals.currWorld);

    for (i = 0; i < pcount; i++, plist++)
    {
        if (!xEntIsVisible(plist))
        {
            continue;
        }

        RwMatrix* mat = plist->model->Mat;
        campos = &globals.cam->mat.pos;
        imodel = plist->model->Data;
        dist2 =
            SQR(plist->model->Mat->pos.x - campos->x) + SQR(plist->model->Mat->pos.z - campos->z);

        bool inYRange = false;
        if (campos->y <= 70.0f + plist->model->Mat->pos.y &&
            campos->y >= plist->model->Mat->pos.y - 70.0f)
        {
            inYRange = true;
        }

        if (!(inYRange && dist2 < PickupRenderMaxDistanceSqr) && plist->p->pickupIndex != 2)
        {
            continue;
        }

        S32 shadowResult;
        xVec3 shadVec;

        shadVec.x = plist->model->Mat->pos.x;
        shadVec.y = plist->model->Mat->pos.y - 10.0f;
        shadVec.z = plist->model->Mat->pos.z;

        zPickupAuraInfo* aura =
            &(plist->p->pickupType == 0 ? zPickupAuraTable :
                                          zManlinessAuraTable)[plist->p->pickupIndex];

        xSphere bound = *iModelWorldBoundSphere(imodel, mat, NULL);

        F32 auraRadius = 0.375f * aura->size;
        F32 prescale = (bound.r > auraRadius) ? bound.r : auraRadius;
        bound.r = plist->fx_scale * prescale;

        if (!xModelSphereCullPlusShadow(&bound, &shadVec, &shadowResult))
        {
            xFXAuraAdd(&bound.center, &aura->color, aura->size * plist->fx_scale, false);

            if (!plist->anim && plist->reward_timer <= 0.0f)
            {
                *(xMat3x3*)mat = *(xMat3x3*)&sPickupOrientation;
            }

            xModelInstance* minst = plist->model;

            if (plist->p->pickupType == 0 && plist->p->pickupIndex == 3)
            {
                if (globals.player.s->Health == globals.player.s->MaxHealth)
                {
                    minst->Alpha = 0.51f;
                    minst->Pipe.Flags = (minst->Pipe.Flags & ~0x30000) | 0x10000;
                }
                else
                {
                    minst->Alpha = 1.0f;
                    minst->Pipe.Flags = minst->Pipe.Flags & ~0x30000;
                }
            }

            while (minst != NULL)
            {
                minst->Scale.x = minst->Scale.y = minst->Scale.z =
                    (plist->fx_scale != 1.0f) ? plist->fx_scale : 0.0f;
                xModelBucket_Add(minst);
                minst = minst->Next;
            }
        }

        if (!shadowResult && (plist->state & 0x23))
        {
            S32 alpha;
            F32 radius =
                (gCurrentPlayer == eCurrentPlayerCar || gCurrentPlayer == eCurrentPlayerSpongePat) ?
                    50.0f :
                    10.0f;

            if (dist2 < radius * radius)
            {
                if (dist2 > 64.0f)
                {
                    // The fade distance is its own statement, stored back into dist2 (dead
                    // afterwards), which gives retail's evaluation order.
                    dist2 = radius - xsqrt(dist2);
                    alpha = 80.0f * dist2 * (1.0f / (radius - 8.0f));

                    if (alpha > 80)
                    {
                        alpha = 80;
                    }

                    if (alpha < 0)
                    {
                        alpha = 1;
                    }
                }
                else
                {
                    alpha = 80;
                }

                plist->simpShadow->alpha = alpha;

                // ecc (named after the xShadowSimple_Add parameter) is not in the DWARF, but
                // retail evaluates it before fx_scale.
                radius = (plist->p->pickupIndex == 0) ? 1.0f : 0.667f;
                F32 ecc = (plist->p->pickupIndex == 0) ? 1.5f : 1.0f;
                radius *= plist->fx_scale;

                xShadowSimple_Add(plist->simpShadow, plist, radius, ecc);
            }
        }

        if (plist->p->pickupType == 0 && plist->p->pickupIndex == 2)
        {
            if (xScrFXGlareActive(sGoofyGooberGlareId))
            {
                xVec3 currOffset;
                xVec3 pos;

                xMat3x3RMulVec(&currOffset, (xMat3x3*)mat, &sGoofyGooberOffset);
                pos = *(xVec3*)&plist->model->Mat->pos;
                pos += currOffset;

                xScrFXGlareChange(sGoofyGooberGlareId, &pos, -1.0f, -1.0f);
            }
            else
            {
                xVec3 currOffset;
                F32 ang;
                xVec3 pos;

                ang = 2.0f * PI * xurand() - PI;

                sGoofyGooberOffset.x = 0.4f * plist->fx_scale * isin(ang);
                sGoofyGooberOffset.y = 0.4f * plist->fx_scale * icos(ang);
                sGoofyGooberOffset.z = 0.0f;

                xMat3x3RMulVec(&currOffset, (xMat3x3*)mat, &sGoofyGooberOffset);
                pos = *(xVec3*)&plist->model->Mat->pos;
                pos += currOffset;

                sGoofyGooberGlareId =
                    xScrFXGlareAdd(&pos, 0.5f, 1.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, NULL);
            }
        }
    }

    xLightKit_Enable(NULL, globals.currWorld);
}

void zEntPickup_SceneUpdate(F32 dt)
{
    sPickupOrientAngle += PI * dt;
    if (sPickupOrientAngle > 6.285714f)
    {
        sPickupOrientAngle -= 6.285714f;
    }

    xMat3x3RotY((xMat3x3*)&sPickupOrientation, sPickupOrientAngle);
    sPickupOrientation.flags = rwMATRIXTYPEORTHONORMAL;
}

void GGRibbonLoop(zEntPickup* ent)
{
    NMEHelpRib::RibData* ribhelp;
    xVec3 pos1;
    xVec3 pos2;
    xMat4x3* mat_root;

    if (ggRibbon == NULL)
    {
        return;
    }

    ribhelp = &ribGGRays;

    // unlike the NME ribbon loops there is no early return on the skip counter here: the target
    // decrements it and then always reloads it (truncated to S8) from frq_ribJoint
    ggCntSkipRibbonJoint--;
    ggCntSkipRibbonJoint = (S8)ribhelp->updinfo.frq_ribJoint;

    pos1.x = 0.5f * ent->fx_scale;
    pos1.y = -0.5f * ent->fx_scale;
    pos1.z = 0.0f;

    pos2.x = -pos1.x;
    pos2.y = pos1.y;
    pos2.z = 0.0f;

    mat_root = (xMat4x3*)ent->model->Mat;

    xMat4x3Toworld(&pos1, mat_root, &pos1);
    xMat4x3Toworld(&pos2, mat_root, &pos2);

    xVec3 dir_emit = pos1 - pos2;

    ggRibbon->insert(pos2, dir_emit, ribhelp->updinfo.scl_master, 1.0f, 0);
}
