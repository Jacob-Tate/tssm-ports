#include "zEntTeleportBox.h"

#include "zGlobals.h"

#include "zFX.h"
#include "zScene.h"

#include "zEntPlayer.h"

#include "xAnim.h"
#include "xEvent.h"
#include "xMath3.h"
#include "xSnd.h"
#include "xString.h"
#include "iScrFX.h"

#define STATUS_OPEN 0
#define STATUS_CLOSED 1

#define PLAYER_STATE_OUTSIDE 0
#define PLAYER_STATE_INSIDE 1
#define PLAYER_STATE_TELEPORTING 2
#define PLAYER_STATE_TELEPORTED 3
#define PLAYER_STATE_EJECTING 4

static S32 sPlayerIn;
static U32 sPlayerNear;

// Name unknown (always inlined, no symbol): true when this box is open but its target is not.
static inline bool zEntTeleportBox_targetClosed(_zEntTeleportBox* ent)
{
    _zEntTeleportBox* target = (_zEntTeleportBox*)ent->target;

    return ent->status == STATUS_OPEN && target->status == STATUS_CLOSED;
}

static inline void VecFromAngle(F32 a, xVec3* v)
{
    xMat3x3 mat;

    xMat3x3Identity(&mat);
    xMat3x3Euler(&mat, (PI / 180.0f) * a, 0.0f, 0.0f);
    xVec3Init(v, 0.0f, 0.0f, 1.0f);
    xMat3x3LMulVec(v, &mat, v);
}

U32 OpenCheck(xAnimTransition*, xAnimSingle*, void* object)
{
    return ((_zEntTeleportBox*)object)->status != STATUS_CLOSED;
}

static U32 JumpInCheck(xAnimTransition*, xAnimSingle*, void* object)
{
    return ((_zEntTeleportBox*)object)->jumpInAnim != 0;
}

static U32 JumpInCB(xAnimTransition*, xAnimSingle*, void* object)
{
    ((_zEntTeleportBox*)object)->jumpInAnim = 0;
    return 0;
}

static U32 JumpOutCheck(xAnimTransition*, xAnimSingle*, void* object)
{
    return ((_zEntTeleportBox*)object)->jumpOutAnim != 0;
}

static U32 JumpOutCB(xAnimTransition*, xAnimSingle*, void* object)
{
    ((_zEntTeleportBox*)object)->jumpOutAnim = 0;
    return 0;
}

static U32 JItoOCheck(xAnimTransition*, xAnimSingle*, void* object)
{
    return ((_zEntTeleportBox*)object)->JItoOpenAnim != 0;
}

static U32 JItoOCB(xAnimTransition*, xAnimSingle*, void* object)
{
    _zEntTeleportBox* tbox = (_zEntTeleportBox*)object;

    tbox->JItoOpenAnim = 0;
    tbox->currPlrState = PLAYER_STATE_OUTSIDE;

    return 0;
}

static U32 JOtoOCheck(xAnimTransition*, xAnimSingle*, void* object)
{
    return ((_zEntTeleportBox*)object)->JOtoOpenAnim != 0;
}

static U32 JOtoOCB(xAnimTransition*, xAnimSingle*, void* object)
{
    ((_zEntTeleportBox*)object)->JOtoOpenAnim = 0;
    return 0;
}

static U32 CtoOCheck(xAnimTransition*, xAnimSingle*, void* object)
{
    zEnt* player = xglobals->___player_ent_dont_use_directly;

    F32 dx__ = player->bound.sph.center.x - ((_zEntTeleportBox*)object)->model->Mat->pos.x;
    F32 dy__ = player->bound.sph.center.y - ((_zEntTeleportBox*)object)->model->Mat->pos.y;
    F32 dz__ = player->bound.sph.center.z - ((_zEntTeleportBox*)object)->model->Mat->pos.z;

    return (SQR(dx__) + SQR(dy__) + SQR(dz__) < 11.56f);
}

static U32 CtoOCB(xAnimTransition*, xAnimSingle*, void* object)
{
    xVec3 tmp;

    xVec3Copy(&tmp, (xVec3*)&((_zEntTeleportBox*)object)->model->Mat->pos);

    tmp.y += 1.0f;

    zFX_SpawnBubbleHit(&tmp, 50);
    xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Box_open_grp")), 0, &tmp, NULL, NULL, NULL, NULL);

    return 0;
}

static U32 JumpInEffectPlrInvisibleCB(U32, xAnimActiveEffect*, xAnimSingle*, void*)
{
    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventInvisible);
    return 0;
}

static U32 JumpInEffectPlrTeleportCB(U32, xAnimActiveEffect*, xAnimSingle*, void* object)
{
    ((_zEntTeleportBox*)object)->currPlrState = PLAYER_STATE_TELEPORTING;
    return 0;
}

static U32 JumpInEffectJIAnimCB(U32, xAnimActiveEffect*, xAnimSingle*, void* object)
{
    ((_zEntTeleportBox*)object)->JItoOpenAnim = 1;
    return 0;
}

static U32 CtoOEffectTboxEnableCB(U32, xAnimActiveEffect*, xAnimSingle*, void* object)
{
    _zEntTeleportBox* tbox = (_zEntTeleportBox*)object;

    if (tbox->status == STATUS_CLOSED)
    {
        tbox->status = STATUS_OPEN;
    }

    return 0;
}

static U32 JumpOutEffectPlrVisibleCB(U32, xAnimActiveEffect*, xAnimSingle*, void*)
{
    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventVisible);
    iCameraMotionBlurActivate(0);
    return 0;
}

static U32 JumpOutEffectPlrEjectCB(U32, xAnimActiveEffect*, xAnimSingle*, void* object)
{
    _zEntTeleportBox* tbox = (_zEntTeleportBox*)object;
    xVec3 tmp;

    tbox->currPlrState = PLAYER_STATE_EJECTING;
    tbox->plrCtrlTimer = 1.2f;

    xVec3Copy(&tmp, (xVec3*)&tbox->model->Mat->pos);

    tmp.y += 1.0f;

    zFX_SpawnBubbleHit(&tmp, 50);

    return 0;
}

static U32 JumpOutEffectJOAnimCB(U32, xAnimActiveEffect*, xAnimSingle*, void* object)
{
    ((_zEntTeleportBox*)object)->JOtoOpenAnim = 1;
    return 0;
}

void zEntTeleportBox_Init(xBase& data, xDynAsset& asset, size_t)
{
    zEntTeleportBox_Init((_zEntTeleportBox*)&data, (teleport_asset*)&asset);
}

void zEntTeleportBox_Init(_zEntTeleportBox* ent, teleport_asset* asset)
{
    xEntAsset* easset = (xEntAsset*)xMemAllocSize(sizeof(xEntAsset));

    easset->id = asset->id;
    easset->baseType = eBaseTypeTeleportBox;
    easset->linkCount = asset->linkCount;
    easset->baseFlags = asset->baseFlags;
    easset->baseFlags |= 0x22;
    easset->flags = XENT_IS_VISIBLE;
    easset->moreFlags |= XENT_MORE_FLAGS_ANIM_COLL;
    easset->pflags = 0;
    easset->ang = 0.0f;
    easset->scale.x = 1.0f;
    easset->scale.y = 1.0f;
    easset->scale.z = 1.0f;
    easset->redMult = 1.0f;
    easset->greenMult = 1.0f;
    easset->blueMult = 1.0f;
    easset->modelInfoID = xStrHash("teleportation_box_bind");
    easset->animListID = 0;

    U32 size;
    xMarkerAsset* marker = (xMarkerAsset*)xSTFindAsset(asset->marker, &size);

    if (marker != NULL && size == sizeof(xMarkerAsset))
    {
        easset->pos.x = marker->pos.x;
        easset->pos.y = marker->pos.y;
        easset->pos.z = marker->pos.z;

        zEntInit(ent, easset, 'TBOX');

        ent->tasset = asset;
        ent->penby |= XENT_COLLTYPE_PLYR;
        ent->chkby |= XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
        ent->move = NULL;
        ent->eventFunc = zEntTeleportBoxEventCB;
        ent->update = zEntTeleportBox_Update;

        if (ent->linkCount != 0)
        {
            ent->link = (xLinkAsset*)(asset + 1);
        }
        else
        {
            ent->link = NULL;
        }

        ent->eventFunc = zEntTeleportBoxEventCB;

        xEntReset(ent);

        xAnimTable* table = xAnimTableNew("TBox", 0);

        xAnimTableNewState(table, "Closed", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, "Open", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, "JumpIn", 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, "Teleport", 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, "JumpOut", 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, "Closed2Open", 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);

        xAnimTableNewTransition(table, "Closed", "Closed2Open", OpenCheck, CtoOCB, 0, 0, 0.0f, 0.0f,
                                1, 0, 0.2f, NULL);
        xAnimTableNewTransition(table, "Closed", "JumpOut", JumpOutCheck, JumpOutCB, 0, 0, 0.0f,
                                0.0f, 1, 0, 0.1f, NULL);
        xAnimTableNewTransition(table, "Open", "JumpOut", JumpOutCheck, JumpOutCB, 0, 0, 0.0f, 0.0f,
                                1, 0, 0.1f, NULL);
        xAnimTableNewTransition(table, "Closed2Open", "JumpOut", JumpOutCheck, JumpOutCB, 0, 0,
                                0.0f, 0.0f, 1, 0, 0.1f, NULL);
        xAnimTableNewTransition(table, "Closed2Open", "JumpIn", JumpInCheck, JumpInCB, 0, 0, 0.0f,
                                0.0f, 10, 0, 0.1f, NULL);
        xAnimTableNewTransition(table, "Open", "JumpIn", JumpInCheck, JumpInCB, 0, 0, 0.0f, 0.0f,
                                10, 0, 0.1f, NULL);
        xAnimTableNewTransition(table, "JumpIn", "Open", JItoOCheck, JItoOCB, 0, 0, 0.0f, 0.0f, 1,
                                0, 0.1f, NULL);
        xAnimTableNewTransition(table, "JumpOut", "Open", JOtoOCheck, JOtoOCB, 0, 0, 0.0f, 0.0f, 1,
                                0, 0.1f, NULL);
        xAnimTableNewTransition(table, "Closed", "Closed2Open", CtoOCheck, CtoOCB, 0, 0, 0.0f, 0.0f,
                                1, 0, 0.1f, NULL);

        xAnimState* state = xAnimTableGetState(table, "JumpIn");
        xAnimStateNewEffect(state, 1, 100, 0.4f, 0.0f, JumpInEffectPlrInvisibleCB, 0);
        xAnimStateNewEffect(state, 1, 100, 0.5f, 0.0f, JumpInEffectPlrTeleportCB, 0);
        xAnimStateNewEffect(state, 1, 100, 1.2f, 0.0f, JumpInEffectJIAnimCB, 0);

        state = xAnimTableGetState(table, "JumpOut");
        xAnimStateNewEffect(state, 1, 100, 0.4f, 0.0f, JumpOutEffectPlrVisibleCB, 0);
        xAnimStateNewEffect(state, 1, 100, 0.9f, 0.0f, JumpOutEffectPlrEjectCB, 0);
        xAnimStateNewEffect(state, 1, 100, 1.2f, 0.0f, JumpOutEffectJOAnimCB, 0);

        state = xAnimTableGetState(table, "Closed2Open");
        xAnimStateNewEffect(state, 1, 100, 0.5f, 0.0f, CtoOEffectTboxEnableCB, 0);

        void* buf = xSTFindAsset(xStrHash("teleportation_box_closed.anm"), &size);
        if (buf != NULL)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableAddFile(table, afile, "Closed");
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, ent->model);
        }

        buf = xSTFindAsset(xStrHash("teleportation_box_jumpin_teleport.anm"), &size);
        if (buf != NULL)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableAddFile(table, afile, "JumpIn");
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, ent->model);
        }

        buf = xSTFindAsset(xStrHash("teleportation_box_open.anm"), &size);
        if (buf != NULL)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableAddFile(table, afile, "Open");
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, ent->model);
        }

        buf = xSTFindAsset(xStrHash("teleportation_box_teleport_jumpout.anm"), &size);
        if (buf != NULL)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableAddFile(table, afile, "JumpOut");
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, ent->model);
        }

        buf = xSTFindAsset(xStrHash("teleportation_box_teleport.anm"), &size);
        if (buf != NULL)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableAddFile(table, afile, "Teleport");
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, ent->model);
        }

        buf = xSTFindAsset(xStrHash("teleportation_box_closedtoopen.anm"), &size);
        if (buf != NULL)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableAddFile(table, afile, "Closed2Open");
            xAnimPoolAlloc(&globals.sceneCur->mempool, ent, table, ent->model);
        }
    }
}

void zEntTeleportBox_Setup(_zEntTeleportBox* ent)
{
    ent->asset->redMult = 1.0f;
    ent->asset->greenMult = 1.0f;
    ent->asset->blueMult = 1.0f;
    ent->asset->seeThru = 1.0f;

    zEntSetup(ent);

    ent->status = STATUS_CLOSED;
    ent->currPlrState = PLAYER_STATE_OUTSIDE;

    _zEntTeleportBox* target = (_zEntTeleportBox*)zSceneFindObject(ent->tasset->targetID);
    ent->target = target;

    ent->jumpInAnim = 0;
    ent->jumpOutAnim = 0;
    ent->JOtoOpenAnim = 0;
    ent->JItoOpenAnim = 0;
    ent->lastdt = 0.0f;
    ent->plrCtrlTimer = -1.0f;

    // not in the DWARF: retail passes this stack xVec3 to the out-of-line xVec3Copy
    // (the PS2 demo reads Mat->pos directly)
    xVec3 pos;
    xBox wbox;

    wbox.upper.x = 0.305f;
    wbox.lower.x = -0.305f;
    wbox.upper.z = 0.25f;
    wbox.lower.z = -0.25f;
    wbox.lower.y = -0.1f;
    wbox.upper.y = 2.5f;

    xVec3Copy(&pos, (xVec3*)&ent->model->Mat->pos);

    wbox.upper.x += pos.x + 0.028f;
    wbox.lower.x += pos.x + 0.028f;
    wbox.upper.z += pos.z + -0.16f;
    wbox.lower.z += pos.z + -0.16f;
    wbox.lower.y += pos.y;
    wbox.upper.y += pos.y;

    ent->trig[0] = wbox;

    wbox.upper.x = 0.61f;
    wbox.lower.x = -0.61f;
    wbox.upper.z = 0.5f;
    wbox.lower.z = -0.5f;
    wbox.lower.y = -0.1f;
    wbox.upper.y = 1.2f;

    xVec3Copy(&pos, (xVec3*)&ent->model->Mat->pos);

    wbox.upper.x += pos.x;
    wbox.lower.x += pos.x;
    wbox.upper.z += pos.z + -0.16f;
    wbox.lower.z += pos.z + -0.16f;
    wbox.lower.y += pos.y;
    wbox.upper.y += pos.y;

    ent->trig[1] = wbox;

    ent->baseFlags &= (U8)~0x40;
    // U16 flag constant, as in HI's k_XBASE_* macros
    ent->baseFlags |= (U16)0x80;
}

void zEntTeleportBox_Update(xEnt* rawent, xScene* sc, F32 dt)
{
    _zEntTeleportBox* ent = (_zEntTeleportBox*)rawent;

    xEntUpdate(ent, sc, dt);

    F32 dx__ =
        xglobals->___player_ent_dont_use_directly->bound.sph.center.x - ent->model->Mat->pos.x;
    F32 dy__ =
        xglobals->___player_ent_dont_use_directly->bound.sph.center.y - ent->model->Mat->pos.y;
    F32 dz__ =
        xglobals->___player_ent_dont_use_directly->bound.sph.center.z - ent->model->Mat->pos.z;
    F32 distsqr = SQR(dx__) + SQR(dy__) + SQR(dz__);

    if (distsqr < 11.56f)
    {
        ent->currPlayerNear = 1;
    }
    else
    {
        ent->currPlayerNear = 0;
    }

    if (ent->currPlayerNear != ent->prevPlayerNear)
    {
        if (ent->currPlayerNear)
        {
            sPlayerNear = 1;

            if (zEntTeleportBox_targetClosed(ent))
            {
                zEntEvent("MNUC TELEPORT BOX", eEventVisible);
            }
        }
        else
        {
            sPlayerNear = 0;
            zEntEvent("MNUC TELEPORT BOX", eEventInvisible);
        }
    }

    ent->prevPlayerNear = ent->currPlayerNear;

    xVec3 playerMid;
    xVec3Copy(&playerMid, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);

    playerMid.y += 0.5f;

    if (xPointInBox(&ent->trig[0], &playerMid) || xPointInBox(&ent->trig[1], &playerMid) ||
        xPointInBox(&ent->trig[0],
                    (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos) ||
        xPointInBox(&ent->trig[1],
                    (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos))
    {
        if (ent->currPlrState == PLAYER_STATE_OUTSIDE && zEntTeleportBox_isOpen(ent) &&
            xglobals->___player_ent_dont_use_directly->frame->vel.y < 0.0f &&
            globals.player.s->Health != 0)
        {
            ent->jumpInAnim = 1;
            ent->currPlrState = PLAYER_STATE_INSIDE;
        }

        ent->currPlayerIn = 1;
    }
    else
    {
        ent->currPlayerIn = 0;

        if (ent->currPlrState == PLAYER_STATE_EJECTING)
        {
            ent->currPlrState = PLAYER_STATE_OUTSIDE;
        }
    }

    if (ent->currPlayerIn != ent->prevPlayerIn)
    {
        if (ent->currPlayerIn)
        {
            sPlayerIn = 1;
            sPlayerKillAnimMovement |= 1;
        }
        else
        {
            sPlayerIn = 0;
            sPlayerKillAnimMovement &= ~1;
        }
    }

    ent->prevPlayerIn = ent->currPlayerIn;

    if (ent->prevPlrState != ent->currPlrState)
    {
        switch (ent->currPlrState)
        {
        case PLAYER_STATE_OUTSIDE:
            break;
        case PLAYER_STATE_INSIDE:
        {
            ent->chkby &= (U8)~XENT_COLLTYPE_PLYR;

            zEntPlayerControlOff(CONTROL_OWNER_TELEPORT_BOX);

            xglobals->___player_ent_dont_use_directly->frame->vel.x = 0.0f;
            xglobals->___player_ent_dont_use_directly->frame->vel.z = 0.0f;

            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Box_shuffle_alt_grp")), 0,
                        (xVec3*)&ent->model->Mat->pos, NULL, NULL, NULL, NULL);
            break;
        }
        case PLAYER_STATE_TELEPORTING:
        {
            ent->chkby |= XENT_COLLTYPE_PLYR;

            _zEntTeleportBox* target = (_zEntTeleportBox*)ent->target;

            if (target)
            {
                xVec3Copy((xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                          (xVec3*)&target->model->Mat->pos);
                xVec3Copy(&xglobals->___player_ent_dont_use_directly->frame->mat.pos,
                          (xVec3*)&target->model->Mat->pos);

                xMat4x3& mat = (xMat4x3&)*xglobals->___player_ent_dont_use_directly->model->Mat;
                xVec3 ypr;

                xMat3x3GetEuler(&mat, &ypr);

                ypr.x = (PI / 180.0f) * target->tasset->camAngle;

                xMat3x3Euler(&mat, &ypr);

                target->currPlrState = PLAYER_STATE_TELEPORTED;

                if (!zEntTeleportBox_isOpen(target))
                {
                    zEntEvent(target, eEventOpenTBox);
                }

                iCameraMotionBlurActivate(1);
            }
            break;
        }
        case PLAYER_STATE_TELEPORTED:
        {
            ent->jumpOutAnim = 1;
            zEntEvent(ent, eEventTBoxPlayerEjected);
            break;
        }
        case PLAYER_STATE_EJECTING:
        {
            xglobals->___player_ent_dont_use_directly->frame->vel.y = 15.0f;

            xVec3 dir;
            VecFromAngle(-1.0f * ent->tasset->camAngle, &dir);

            xglobals->___player_ent_dont_use_directly->frame->vel.x = 2.7f * dir.x;
            xglobals->___player_ent_dont_use_directly->frame->vel.z = 2.7f * dir.z;

            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Box_shuffle_open_grp")), 0, NULL, NULL, NULL,
                        NULL, NULL);
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Box_open_grp")), 0, NULL, NULL, NULL, NULL,
                        NULL);
            break;
        }
        }
    }

    ent->prevPlrState = ent->currPlrState;

    if (ent->lastdt > ent->plrCtrlTimer && ent->plrCtrlTimer > -ent->lastdt)
    {
        zEntPlayerControlOn(CONTROL_OWNER_TELEPORT_BOX);
    }

    ent->lastdt = dt;
    ent->plrCtrlTimer -= dt;
}

void zEntTeleportBox_Save(_zEntTeleportBox* ent, xSerial* s)
{
    zEntSave(ent, s);

    if (ent->status == STATUS_OPEN)
    {
        s->Write_b1(1);
    }
    else
    {
        s->Write_b1(0);
    }

    s->Write(ent->currPlrState);
}

void zEntTeleportBox_Load(_zEntTeleportBox* ent, xSerial* s)
{
    zEntLoad(ent, s);

    S32 open = 0;
    s->Read_b1(&open);

    if (open)
    {
        zEntTeleportBox_Open(ent);
    }
    else
    {
        zEntTeleportBox_Close(ent);
    }

    s->Read(&ent->currPlrState);
}

inline void zEntTeleportBox_Open(_zEntTeleportBox* ent)
{
    if (ent->status == STATUS_CLOSED)
    {
        ent->status = STATUS_OPEN;
    }
}

inline void zEntTeleportBox_Close(_zEntTeleportBox* ent)
{
    if (ent->status == STATUS_OPEN)
    {
        ent->status = STATUS_CLOSED;
    }
}

inline U32 zEntTeleportBox_isOpen(_zEntTeleportBox* ent)
{
    _zEntTeleportBox* target = (_zEntTeleportBox*)ent->target;

    if (ent->status == STATUS_OPEN && target->status == STATUS_OPEN)
    {
        return 1;
    }

    return 0;
}

S32 zEntTeleportBox_playerIn()
{
    return sPlayerIn;
}

void zEntTeleportBoxEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
{
    _zEntTeleportBox* s = (_zEntTeleportBox*)to;

    switch (toEvent)
    {
    case eEventVisible:
    case eEventFastVisible:
    {
        xEntShow(s);
        break;
    }
    case eEventInvisible:
    case eEventFastInvisible:
    {
        xEntHide(s);
        break;
    }
    case eEventCollisionOn:
    {
        s->chkby |= (U8)(XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR);
        break;
    }
    case eEventCollisionOff:
    {
        s->chkby &= (U8) ~(XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR);
        break;
    }
    case eEventCollision_Visible_On:
    {
        s->chkby |= (U8)(XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR);
        xEntShow(s);
        break;
    }
    case eEventCollision_Visible_Off:
    {
        s->chkby &= (U8) ~(XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR);
        xEntHide(s);
        break;
    }
    case eEventOpenTBox:
    {
        zEntTeleportBox_Open(s);
        break;
    }
    case eEventCloseTBox:
    {
        zEntTeleportBox_Close(s);
        break;
    }
    case eEventTBoxPlayerEjected:
    {
        break;
    }
    }
}
