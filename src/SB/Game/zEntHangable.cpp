#include "zEntHangable.h"

#include <types.h>

#include "zGlobals.h"
#include "zParEmitter.h"
#include "xEnt.h"
#include "xMath.h"
#include "xMath3.h"
#include "xUpdateCull.h"
#include "xRand.h"

static zParEmitter* sCandleEmitter;
static zParEmitter* sCandleSmokeEmitter;
static zParEmitter* sMountEmitter;
static U32 sChandelierHash;

void zEntHangable_Update(zEntHangable* ent, xScene*, F32 dt);
void zEntHangableEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, U32 toParamWidgetID);

static inline void HangableSetup(zEntHangable* ent, xEntAsset* asset)
{
    xEntHangableAsset* hangAsset = (xEntHangableAsset*)(asset + 1);
    xVec3* center;
    xMat3x3 hackMat;

    ent->pflags = 0;
    ent->chkby = 0;
    ent->penby = 0;
    ent->update = (xEntUpdateCallback)zEntHangable_Update;
    ent->move = NULL;
    ent->eventFunc = zEntHangableEventCB;
    ent->hangInfo = hangAsset;

    center = &ent->asset->pos;
    ent->pivot.x = center->x;
    ent->pivot.y = center->y + hangAsset->pivotOffset;
    ent->pivot.z = center->z;
    ent->endpos.x = center->x;
    ent->endpos.y = ent->pivot.y - hangAsset->leverArm;
    ent->endpos.z = center->z;
    ent->vel.x = 0.0f;
    ent->vel.y = 0.0f;
    ent->vel.z = 0.0f;
    ent->grabTimer = 0.0f;
    ent->spin = 0.0f;
    ent->state = 0;
    ent->follow = NULL;
    ent->enabled = 1;
    ent->moving = 0;
    ent->candle_timer = 3.0f;
    ent->candle_state = 0;

    center = &ent->asset->ang;
    xMat3x3Euler(&hackMat, center->x, center->y, center->z);
    ent->swingplane.x = hackMat.right.x;
    ent->swingplane.y = hackMat.right.y;
    ent->swingplane.z = hackMat.right.z;
}

static inline void zEntHangable_SetFollow(zEntHangable* ent, zEnt* b)
{
    if (b != NULL)
    {
        ent->follow = b;
    }
    else if (ent->follow != NULL)
    {
        ent->follow = NULL;
    }
}

static inline void zEntHangableMountFX(zEntHangable* ent)
{
    xParEmitterCustomSettings info;

    if (sMountEmitter != NULL)
    {
        info.custom_flags = 0x300;
        for (S32 i = 0; i < 10; i++)
        {
            info.pos = ent->pivot;
            info.vel.x = 0.0f;
            info.vel.y = -2.0f - 2.0f * xurand();
            info.vel.z = 0.0f;
            xParEmitterEmitCustom(sMountEmitter, (1.0f / 30.0f), &info);
        }
    }
}

static inline void zEntHangable_Init(zEntHangable* ent, xEntAsset* asset)
{
    zEntInit(ent, asset, 'HANG');

    if (ent->linkCount)
    {
        ent->link = (xLinkAsset*)((xEntHangableAsset*)(ent->asset + 1) + 1);
    }
    else
    {
        ent->link = NULL;
    }

    HangableSetup(ent, asset);
}

void zEntHangable_SetupFX()
{
    sCandleEmitter = zParEmitterFind("PAREMIT_CHAND_CANDLE");
    sCandleSmokeEmitter = zParEmitterFind("PAREMIT_CHAND_CANDLE_SMOKE");
    sMountEmitter = zParEmitterFind("PAREMIT_HANGABLE_MOUNT");
    sChandelierHash = 0xA43B805C; // xStrHash("rlii0006")
}

void zEntHangable_Init(void* ent, void* asset)
{
    zEntHangable_Init((zEntHangable*)ent, (xEntAsset*)asset);
}

// Stripped by the linker; only its table survives, unreferenced, in retail .rodata. Body and
// values are BfBB's. The flat list (no inner braces) is deliberate: GC/2.6 lays it out oddly for
// an xVec3 array, and that layout is retail's.
static void zEntHangable_UpdateFX(zEntHangable* ent)
{
    // Points of a (wobbly) circle in 3D space.
    xVec3 offset_rlii0006[8] = { 0.0,    0.561, 0.613,  0.43345639,  0.432, 0.43345639,
                                 0.613,  0.543, 0.0,    0.43345639,  0.526, -0.43345639,
                                 0.0,    0.526, -0.613, -0.43345639, 0.505, -0.43345639,
                                 -0.613, 0.543, 0.0,    -0.43345639, 0.48,  0.43345639 };

    xVec3* local_offset;
    xParEmitterCustomSettings info;
    zParEmitter* emitter;

    if ((ent->flags & 1) && (ent->asset->modelInfoID == sChandelierHash))
    {
        local_offset = &offset_rlii0006[0];

        if (ent->candle_state == 2)
        {
            return;
        }

        info.custom_flags = 0x100;

        if (ent->candle_state == 0)
        {
            emitter = sCandleEmitter;
        }
        else
        {
            emitter = sCandleSmokeEmitter;
        }

        for (S32 i = 0; i < (S32)(sizeof(offset_rlii0006) / sizeof(xVec3)); i++)
        {
            xVec3 mul;
            xMat3x3RMulVec(&mul, xEntGetFrame(ent), &local_offset[i]);
            xVec3Add(&info.pos, &mul, xEntGetPos(ent));
            xParEmitterEmitCustom(emitter, (1.0f / 30.0f), &info);
        }
    }
}

void zEntHangable_Update(zEntHangable* ent, xScene*, F32 dt)
{
}

void zEntHangableEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, U32 toParamWidgetID)
{
    // not in the DWARF: visibility before the event is compared with the visibility after it.
    U32 wasVisible = xEntIsVisible((xEnt*)to);
    zEntHangable* ent = (zEntHangable*)to;
    zEnt* follow = (zEnt*)from;
    FloatAndVoid dist;

    switch (toEvent)
    {
    case eEventVisible:
    case eEventFastVisible:
        xEntShow(ent);
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(ent);
        break;
    case eEventCollisionOn:
        ent->enabled = 1;
        break;
    case eEventCollisionOff:
        ent->enabled = 0;
        break;
    case eEventCollisionReset:
        ent->enabled = 1;
        break;
    case eEventMount:
        zEntHangableMountFX(ent);
        if (ent->asset->modelInfoID == sChandelierHash && ent->candle_state == 0)
        {
            ent->candle_timer = 3.0f;
        }
        break;
    case eEventDismount:
        ent->enabled = -2;
        break;
    case eEventCollision_Visible_On:
        xEntShow(ent);
        ent->enabled = 1;
        break;
    case eEventCollision_Visible_Off:
        xEntHide(ent);
        ent->enabled = 0;
        break;
    case eEventReset:
        zEntHangable_Reset(ent);
        break;
    case eEventSwingerFollow:
        zEntHangable_SetFollow(ent, follow);
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr != NULL)
        {
            if (*toParam <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, ent, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                dist.f = *toParam * *toParam;
                xUpdateCull_SetCB(globals.updateMgr, ent, xUpdateCull_DistanceSquaredCB, dist.v);
            }
        }
        break;
    case eEventSetLightKit:
        xModelSetLightKit(ent->model, (xLightKit*)toParamWidget);
        break;
    }

    if (!wasVisible && xEntIsVisible(ent))
    {
        ent->vel.x = 0.0f;
        ent->vel.y = 0.0f;
        ent->vel.z = 0.0f;
        ent->endpos.x = ent->pivot.x;
        ent->endpos.y = ent->pivot.y - ent->hangInfo->leverArm;
        ent->endpos.z = ent->pivot.z;
    }
}

void zEntHangable_Save(zEntHangable* ent, xSerial* s)
{
    zEntSave(ent, s);
}

void zEntHangable_Load(zEntHangable* ent, xSerial* s)
{
    zEntLoad(ent, s);
}

void zEntHangable_Reset(zEntHangable* ent)
{
    zEntHangable_SetFollow(ent, NULL);
    xEntReset(ent);
    HangableSetup(ent, ent->asset);
}
