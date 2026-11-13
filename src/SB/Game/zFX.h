#ifndef ZFX_H
#define ZFX_H

#include "xString.h"
#include "xEnt.h"
#include "xFX.h"
#include "xVec3Inlines.h"
#include "zGlobals.h"

#include <rwcore.h>
#include <rpworld.h>

struct tweak_info;
struct tweak_callback;

enum zFXGooState
{
    zFXGooStateNormal,
    zFXGooStateFreezing,
    zFXGooStateFrozen,
    zFXGooStateMelting,
    zFXGooStateInactive = 0xdeadbeef,
    zFXGooStateForce32Bit = 0xffffffff
};

struct zFXGooInstance
{
    RpAtomic* atomic;
    S32 freezeGroup;
    xVec3* orig_verts;
    RwRGBA* orig_colors;
    RwTexCoords* orig_uvs;
    F32 time;
    F32 timer;
    F32 w0;
    F32 w2;
    F32 warbc[4];
    F32 state_time[4];
    xVec3 center;
    zFXGooState state;
    F32 warb_time;
    F32 alpha;
    F32 min;
    F32 max;
    xVec3* ref_parentPos;
    xVec3 pos_parentOnFreeze;
    U32 pipeFlags;
    RpGeometry* geom2;
};

namespace
{
    enum state_enum
    {
        STATE_NONE,
        STATE_OFF,
        STATE_ON,
        STATE_CONSTANT
    };

    struct popper_data
    {
        state_enum state;
        xEnt* ent;
        RpAtomic* atomic[4];
        U32 atomic_size;
        F32 time;
        F32 end_time;
        union
        {
            xVec3 model_scale;
            U32 pipe_flags;
        };
        F32 rate;
        F32 vel;
        F32 rloc;
        F32 rvel;
        F32 emitted;
        F32 radius;
        F32 area;
        S32 faces;
        S32 max_faces;
        F32* weight;

        S32 find_weight(F32 v) const
        {
            S32 start = 0;
            S32 end = faces;
            do
            {
                S32 index = (start + end) / 2;
                if (v <= weight[index])
                {
                    end = index;
                }
                else
                {
                    start = index + 1;
                }
            } while (start < end);
            return start;
        }
    };
} // namespace

extern U32 gFXSurfaceFlags;

void zFX_SceneEnter(RpWorld* world);
void zFX_SceneExit(RpWorld* world);
void zFX_SceneReset();
void zFXHammer(xVec3* pos);
void zFXPorterWave(const xVec3* pos);

void zFXGooEnable(xModelInstance* gooModel, S32 freezeGroup);
void zFXGooFreeze(RpAtomic* atomic, const xVec3* center, xVec3* ref_parPosVec);

void zFXGoo_SceneEnter();

void zFXGooEventSetWarb(xEnt* obj, const F32* param);
void zFXGooEventSetFreezeDuration(xEnt* obj, const F32 param);
void zFXGooEventMelt(xEnt* obj);
void zFXGooUpdateInstance(zFXGooInstance* goo, F32 dt);

void zFXUpdate(F32 dt);
void zFXPatrickStun(const xVec3* pos);
void zFX_GameInit();

extern zFXGooInstance zFXGooInstances[24];

inline S32 zFXGooIs(xEnt* obj, F32& depth, U32 playerCheck)
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;

    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state == zFXGooStateInactive)
        {
            continue;
        }
        if (goo->atomic == obj->model->Data)
        {
            break;
        }
    }

    if (i == 24)
    {
        return TRUE;
    }

    if (goo->state == zFXGooStateNormal)
    {
        return TRUE;
    }

    xVec3 pos;
    xVec3Sub(&pos, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
             &goo->center);

    if (xVec3Dot(&pos, &pos) > goo->max)
    {
        return TRUE;
    }

    depth = 0.0f;

    if (playerCheck)
    {
        globals.player.ForceSlipperyFriction = 0.2f;
        globals.player.ForceSlipperyTimer = 0.1f;
    }

    return FALSE;
}

void zFX_SpawnBubbleHit(const xVec3* pos, U32 num);
void zFX_SpawnBubbleHit(const xVec3* pos, U32 num, const xVec3* pos_rnd, const xVec3* vel_rnd,
                        F32 vel_scale);
void zFX_SpawnBubbleWall();
void zFX_SpawnBubbleRing(const xVec3* pos, U32 num, F32 rang, F32 bvel, F32 rvel, xVec3* norm);
void zFX_SpawnBubbleSlam(const xVec3* pos, U32 num, F32 rang, F32 bvel, F32 rvel);
void zFX_SpawnBubbleBlast(const xVec3* pos, U32 num, F32 radius, F32 blast_vel, F32 rand_vel);
void zFX_SpawnBubblesOnSphere(S32 numBubbles, xSphere* genSphere, F32 velocity);

void xDebugAddTweak(const char* name, const char* message, const tweak_callback* callback,
                    void* context, U32 flags);

xFXRing* zFXMuscleArmWave(const xVec3* pos);

void zFX_SpawnBubbleTrail(const xVec3*, unsigned int);
void zFX_SpawnBubbleTrail(const xVec3* p1, const xVec3* p2, U32 num, const xVec3* pos_rnd,
                          const xVec3* vel_rnd);
void zFX_SpawnBubbleTrail(const xVec3* pos, unsigned int num, const xVec3* pos_rnd,
                          const xVec3* vel_rnd);
void zFX_SpawnBubbleTrail(const xVec3*, const xVec3*, const xVec3*, const xVec3*, unsigned int,
                          const xVec3*, const xVec3*, float);
void zFX_SpawnBubbleMenuTrail(const xVec3* pos, U32 num, const xVec3* pos_rnd,
                              const xVec3* vel_rnd);

void zFXPopOn(xEnt& ent, F32 rate, F32 time);
void zFXPopOff(xEnt& ent, F32 rate, F32 time);
void zFXPopOnOffAtomic(xModelInstance* model, F32 rate);
void zFXBubbleSpawnStart(xEnt& ent, F32 rate);
void zFXBubbleSpawnStop(xEnt& ent);

#endif
