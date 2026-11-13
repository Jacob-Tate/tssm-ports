#include "zEGenerator.h"

#include "xColor.h"
#include "xMath.h"
#include "xMath3.h"
#include "xMovePoint.h"
#include "xRand.h"
#include "xString.h"
#include "xstransvc.h"
#include "zCollGeom.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zShrapnel.h"

#include <string.h>

void zEGenerator_Update(zEGenerator* egen, xScene* sc, F32 dt);

void zEGenerator_Init(void* egen, void* asset)
{
    zEGenerator_Init((zEGenerator*)egen, (xEntAsset*)asset);
}

inline void zEGenerator_Init(zEGenerator* egen, xEntAsset* asset)
{
    zEGenAsset* zasset;
    void* buf;
    U32 size;

    zEntInit(egen, asset, 'EGEN');
    zasset = (zEGenAsset*)asset;
    egen->zasset = zasset;
    if (egen->linkCount != 0)
    {
        egen->link = (xLinkAsset*)(zasset + 1);
    }
    else
    {
        egen->link = NULL;
    }

    egen->update = (xEntUpdateCallback)zEGenerator_Update;
    egen->move = (xEntMoveCallback)zEGenerator_Move;
    egen->eventFunc = zEGeneratorEventCB;
    egen->render = (xEntRenderCallback)zEGenerator_Render;
    egen->afile = NULL;

    if (zasset->onAnimID != 0)
    {
        buf = xSTFindAsset(zasset->onAnimID, &size);
        if (buf != NULL)
        {
            egen->afile = xAnimFileNew(buf, "", 0, NULL);
            egen->atbl = xAnimTableNew("", 0);
            xAnimTableNewState(egen->atbl, "On", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                               xAnimDefaultBeforeEnter, NULL, NULL);
            xAnimTableAddFile(egen->atbl, egen->afile, "On");
            xAnimPoolAlloc(&globals.sceneCur->mempool, egen, egen->atbl, egen->model);
        }
    }
}

void zEGenerator_Setup(zEGenerator* egen, xScene* sc)
{
    S32 i;
    xLinkAsset* la;
    xBase* b;

    zEntSetup(egen);
    egen->num_dsts = 0;
    for (i = 0; i < egen->linkCount; i++)
    {
        la = &egen->link[i];
        b = zSceneFindObject(la->dstAssetID);
        if (b != NULL && la->dstEvent == eEventArcto)
        {
            if (b->baseType == eBaseTypeMovePoint)
            {
                egen->num_dsts++;
            }
            else if (b->baseFlags & 0x20)
            {
                egen->num_dsts++;
            }
        }
    }
}

void zEGenerator_Save(zEGenerator* ent, xSerial* s)
{
    zEntSave(ent, s);
}

void zEGenerator_Load(zEGenerator* ent, xSerial* s)
{
    zEntLoad(ent, s);
}

void zEGenerator_Reset(zEGenerator* egen, xScene* sc)
{
    zEGenAsset* zasset;

    zEntReset(egen);
    zasset = (zEGenAsset*)egen->asset;
    egen->flags = zasset->flags;
    if (egen->flags & 1)
    {
        zEGenerator_TurnOn(egen);
    }
    else
    {
        zEGenerator_TurnOff(egen);
    }

    if (zasset->moreFlags & 0x4)
    {
        egen->chkby = XENT_COLLTYPE_NONE;
    }
    else
    {
        egen->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
    }
    egen->bupdate(egen, (xVec3*)&egen->model->Mat->pos);
}

void zEGenerator_Move(zEGenerator* egen, xScene* sc, F32 dt)
{
}

void zEGenerator_Update(zEGenerator* egen, xScene* sc, F32 dt)
{
    xBase* b;
    zEnt* ent;

    xEntUpdate(egen, sc, dt);
    xMat4x3Toworld(&egen->src_pos, (xMat4x3*)egen->model->Mat, &egen->zasset->src_dpos);

    b = egen->dst;
    if (b != NULL && b->baseFlags & 0x20)
    {
        ent = (zEnt*)b;
        if (ent->baseType == eBaseTypeBoulder)
        {
            xVec3Copy(&egen->dst_pos, xEntGetCenter(ent));
        }
        else
        {
            xMat4x3Toworld(&egen->dst_pos, (xMat4x3*)ent->model->Mat, &egen->dst_off);
        }
    }

    if (egen->lfx[0] != NULL)
    {
        zLightningModifyEndpoints(egen->lfx[0], &egen->src_pos, &egen->dst_pos);
    }

    if (egen->lfx[1] != NULL)
    {
        zLightningModifyEndpoints(egen->lfx[1], &egen->src_pos, &egen->dst_pos);
    }

    if (egen->tmr > 0.0f)
    {
        egen->tmr -= dt;
        if (egen->tmr <= 0.0f)
        {
            zEGenerator_TurnOff(egen);
            zEntEvent(egen, eEventOff);
        }
    }
}

void zEGenerator_Render(zEGenerator* egen)
{
    xEntRender(egen);
}

// The lightning colors are constant-initialized locals (names from Ratatouille): retail copies them
// from .sdata2 templates, which xColorFromRGBA cannot produce.
void zEGenerator_TurnOn(zEGenerator* egen)
{
    zEGenAsset* zasset;
    xAnimPlay* aplay;
    xAnimSingle* asing;
    U16 itgt;
    U16 imp;
    S32 i;
    xLinkAsset* link;
    U32 dstAssetID;
    xBase* b;
    xVec3 destOffset;
    xEnt* ent;
    _tagLightningAdd add;

    egen->flags |= 1;
    zasset = (zEGenAsset*)egen->asset;
    if (egen->afile != NULL)
    {
        aplay = egen->model->Anim;
        asing = aplay->Single;
        asing->CurrentSpeed = 1.0f;
    }

    egen->tmr = zasset->ontime;
    xMat4x3Toworld(&egen->src_pos, (xMat4x3*)egen->model->Mat, &zasset->src_dpos);

    if (egen->num_dsts)
    {
        itgt = xrand_RandomInt32() % egen->num_dsts;
        imp = 0;

        for (i = 0; i < egen->linkCount; i++)
        {
            link = &egen->link[i];
            dstAssetID = link->dstAssetID;
            b = zSceneFindObject(dstAssetID);

            destOffset.x = link->param[0];
            destOffset.y = link->param[1];
            destOffset.z = link->param[2];
            xVec3Copy(&egen->dst_off, &destOffset);

            if (b == NULL || link->dstEvent != eEventArcto)
            {
                continue;
            }

            if (imp == itgt)
            {
                if (b->baseType == eBaseTypeMovePoint)
                {
                    xVec3Copy(&egen->dst_pos, xMovePointGetPos((xMovePoint*)b));
                    xVec3AddTo(&egen->dst_pos, &egen->dst_off);
                    egen->dst = b;
                    break;
                }

                if (b->baseType == eBaseTypeBoulder)
                {
                    xVec3Copy(&egen->dst_pos, xEntGetCenter((xEnt*)b));
                    egen->dst = b;
                    break;
                }

                if (b->baseFlags & 0x20)
                {
                    ent = (xEnt*)b;
                    xMat4x3Toworld(&egen->dst_pos, (xMat4x3*)ent->model->Mat, &egen->dst_off);
                    egen->dst = b;
                    break;
                }
            }
            else
            {
                imp++;
            }
        }
    }

    for (S32 i = 0; i < 2; i++)
    {
        if (egen->lfx[i] != NULL)
        {
            zLightningKill(egen->lfx[i]);
            egen->lfx[i] = NULL;
        }
    }

    memset(&add, 0, sizeof(_tagLightningAdd));
    add.mainTexture = xStrHash("lightning");
    add.branchTexture = xStrHash("lightning");
    add.branchSpeed = 0.3f;
    add.type = 3;
    add.total_points = (xrand_RandomInt32() & 3) << 1;
    add.total_points += 8;
    add.start = &egen->src_pos;
    add.end = &egen->dst_pos;
    add.thickness = 0.3f;
    add.segmentsPerMeter = 3.0f;
    xColor_tag addColor1 = { 200, 200, 255, 200 };
    add.color = addColor1;
    add.arc_height = -0.2f;
    add.zeus_normal_offset = 0.2f;
    add.zeus_back_offset = 0.1f;
    add.zeus_side_offset = 0.1f;
    add.rand_radius = 15.0f;
    add.flags = 0x10;
    egen->lfx[0] = zLightningAdd(&add);

    add.total_points = 16;
    add.type = 3;
    add.move_degrees = 360.0f * xurand() + 180.0f;
    if (xrand_RandomInt32() & 1)
    {
        add.move_degrees = -add.move_degrees;
    }
    add.setup_degrees = 90.0f * xurand() + 20.0f;
    xColor_tag addColor2 = { 80, 100, 255, 200 };
    add.color = addColor2;
    add.rot_radius = 0.25f;
    add.thickness = 0.3f;
    add.segmentsPerMeter = 3.0f;
    add.rand_radius = 10.0f;
    add.flags = 0x10;
    egen->lfx[1] = zLightningAdd(&add);
}

inline void zEGenerator_TurnOff(zEGenerator* egen)
{
    egen->flags &= ~1;
    if (egen->afile != NULL)
    {
        egen->model->Anim->Single->CurrentSpeed = 0.0f;
    }
    for (S32 i = 0; i < 2; i++)
    {
        if (egen->lfx[i] != NULL)
        {
            zLightningKill(egen->lfx[i]);
            egen->lfx[i] = NULL;
        }
    }
}

inline void zEGenerator_ToggleOn(zEGenerator* egen)
{
    if (egen->flags & 1)
    {
        zEGenerator_TurnOff(egen);
    }
    else
    {
        zEGenerator_TurnOn(egen);
    }
}

void zEGeneratorEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, U32)
{
    zEGenerator* egen;
    zShrapnelAsset* shrap;

    egen = (zEGenerator*)to;

    switch (toEvent)
    {
    case eEventOn:
        zEGenerator_TurnOn(egen);
        break;
    case eEventOff:
        zEGenerator_TurnOff(egen);
        break;
    case eEventToggle:
        zEGenerator_ToggleOn(egen);
        break;
    case eEventReset:
        zEGenerator_Reset(egen, globals.sceneCur);
        break;
    case eEventVisible:
    case eEventFastVisible:
        xEntShow(egen);
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(egen);
        break;
    case eEventCollision_Visible_On:
        xEntShow(egen);
    case eEventCollisionOn:
        egen->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
        egen->bupdate(egen, (xVec3*)&egen->model->Mat->pos);
        break;
    case eEventCollision_Visible_Off:
        xEntHide(egen);
    case eEventCollisionOff:
        egen->chkby = XENT_COLLTYPE_NONE;
        break;
    case eEventCollisionReset:
        if (egen->asset->moreFlags & 0x4)
        {
            egen->chkby = XENT_COLLTYPE_NONE;
        }
        else
        {
            egen->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
        }
        egen->bupdate(egen, (xVec3*)&egen->model->Mat->pos);
        break;
    case eEventCameraCollidePartial:
        zCollGeom_CamEnable(egen);
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable(egen);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable(egen);
        break;
    case eEventLaunchShrapnel:
        shrap = (zShrapnelAsset*)toParamWidget;
        if (shrap != NULL && shrap->initCB != NULL)
        {
            shrap->initCB(shrap, egen->model, NULL, NULL);
        }
        break;
    case eEventSetLightKit:
        xModelSetLightKit(egen->model, (xLightKit*)toParamWidget);
        break;
    }
}
