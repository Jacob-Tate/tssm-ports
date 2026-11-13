#include "zDestructible.h"

#include "iMath.h"
#include "xCollide.h"
#include "xEvent.h"
#include "xGlobals.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xQuickCull.h"
#include "xRand.h"
#include "xRumbleEmitter.h"
#include "xSnd.h"
#include "xString.h"
#include "xstransvc.h"
#include "zEntSimpleObj.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zNPC.h"
#include "zParEmitter.h"
#include "zPlatform.h"
#include "zScene.h"
#include "zShrapnel.h"

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

namespace
{
    F32 DEG_TO_RAD = 0.017460316f;
    F32 LAUNCH_ANGLE = 70.0f;
    F32 LAUNCH_VEL = 10.0f;
    F32 DAMAGE_RADIUS = 1.0f;
    F32 MAX_TIME_NO_COLLISIONS = 0.5f;
    F32 HIT_TO_DESTROY_TIME = 0.5f;
    F32 RESPAWN_THRESHOLD = 0.00001f;
    F32 DOBJ_STD_BOUND_VAR = 0.5f;
} // namespace

#ifdef PLATFORM_PC
// PLATFORM_PC: some destructibles have no xEntFrame (ent->frame is NULL), and the console code
// reads the frame's position and velocity regardless. On the console that reads low memory and
// gets junk; on PC page 0 is unmapped and it crashes. These give a sound no position (it follows
// the entity it is given), the rumble the model's position, and the shrapnel no velocity.
namespace
{
    xVec3 sNoFrameVel = { 0.0f, 0.0f, 0.0f };
}
#define DEST_SND_POS(ent) ((ent)->frame ? &(ent)->frame->mat.pos : (const xVec3*)NULL)
#define DEST_RUMBLE_POS(ent) ((ent)->frame ? (ent)->frame->mat.pos : *xEntGetPos(ent))
#define DEST_FRAME_VEL(ent) ((ent)->frame ? (ent)->frame->vel : sNoFrameVel)
#else
#define DEST_SND_POS(ent) (&(ent)->frame->mat.pos)
#define DEST_RUMBLE_POS(ent) ((ent)->frame->mat.pos)
#define DEST_FRAME_VEL(ent) ((ent)->frame->vel)
#endif

namespace
{
    // Not in the TSSM DWARF (inlined); name and parameters from Ratatouille.
    inline void UpdateModelBound(xEnt* ent, xModelInstance* model, U8 gridUpdate)
    {
        xBoxForModelLocal(ent->bound.box.box, model, true);
        ent->bound.mat = (xMat4x3*)model->Mat;
        xQuickCullForBound(&ent->bound.qcd, &ent->bound);
        xBoundUpdate(&ent->bound);
        if (gridUpdate)
        {
            zGridUpdateEnt(ent);
        }
    }

    void SwapModelThroughState(xEnt* ent, zDestructibleState* state, zDestructible* dest)
    {
        // Initialising curr keeps this just over the auto-inline limit; that lets
        // zDestructible_Swap be inlined into its two callers, as in retail.
        xModelInstance* curr = NULL;

        if (ent->model->modelID != state->model->modelID)
        {
            state->model->Scale = ent->model->Scale;
            state->model->BoneIndex = ent->model->BoneIndex;
            state->model->BoneCount = ent->model->BoneCount;
            state->model->Anim = ent->model->Anim;
            state->model->animBound = ent->model->animBound;
            state->model->anim_coll.verts = ent->model->anim_coll.verts;
            *state->model->Mat = *ent->model->Mat;
            state->model->Flags &= (U16)~0x400;

            if ((state->model->Pipe.Flags & 0xc0) == 0x40)
            {
                state->model->LightKit = NULL;
            }
            else
            {
                for (curr = ent->model; curr; curr = curr->Next)
                {
                    if ((curr->Pipe.Flags & 0xc0) != 0x40)
                    {
                        state->model->LightKit = curr->LightKit;
                        break;
                    }
                }
            }

            state->model->Pipe.Flags |= 0x100000;
            state->model->Surf = ent->model->Surf;
            *ent->model = *state->model;

            if (ent->collModel)
            {
                if (state->model->Data == dest->base_model->Data && dest->base_collision)
                {
                    *ent->collModel = *dest->base_collision;
                }
                else
                {
                    *ent->collModel = *state->model;
                }
            }

            UpdateModelBound(ent, ent->collModel ? ent->collModel : ent->model, FALSE);
        }
    }

    // The helpers below have no symbols (always inlined); names, parameters and locals are
    // Ratatouille's. The soundfx locals affect register allocation in zDestructible_StateProcess.
    inline void zDestructible_StopSounds(xEnt* ent, zDestructible* dest)
    {
        S32 lastidx = dest->curstateidx - 1;
        xSoundFX* soundfx;

        if (lastidx >= 0)
        {
            xSndMgrStop(dest->dstates[lastidx].sh_handle_idle);
            xSndMgrStop(dest->dstates[lastidx].sh_handle_hit);

            if (dest->dstates[lastidx].sg_fx_id)
            {
                soundfx = xSoundFXGet(dest->dstates[dest->curstateidx].sg_fx_id);
                xSoundFXEventCB(ent, soundfx, eEventStop, NULL, NULL, 0);
            }
        }
    }

    inline void zDestructible_StartSounds(xEnt* ent, zDestructible* dest)
    {
        xSoundFX* soundfx;

        dest->dstates[dest->curstateidx].sh_handle_idle =
            xSndMgrPlay(dest->dstates[dest->curstateidx].sg_handle_idle, 2, DEST_SND_POS(ent), NULL,
                        ent, NULL, NULL);
        dest->dstates[dest->curstateidx].sh_handle_hit =
            xSndMgrPlay(dest->dstates[dest->curstateidx].sg_handle_hit, 2, DEST_SND_POS(ent), NULL,
                        ent, NULL, NULL);

        if (dest->dstates[dest->curstateidx].sg_fx_id)
        {
            soundfx = xSoundFXGet(dest->dstates[dest->curstateidx].sg_fx_id);
            xSoundFXEventCB(ent, soundfx, eEventPlay, NULL, NULL, 0);
        }
    }

    inline void zDestructible_StateChange(xEnt* ent, zDestructible* dest)
    {
        zDestructibleState* state = &dest->dstates[dest->curstateidx];
        dest->dstates[dest->curstateidx].sh_handle_hit_switch =
            xSndMgrPlay(state->sg_handle_hit_switch, 2, DEST_SND_POS(ent), NULL, ent, NULL, NULL);

        if (dest->dstates[dest->curstateidx].sg_fx_id_switch)
        {
            xSoundFXEventCB(ent, xSoundFXGet(dest->dstates[dest->curstateidx].sg_fx_id_switch),
                            eEventPlay, NULL, NULL, 0);
        }

        if (state->rumbleSwitch && state->rumbleSwitch->pAsset)
        {
            state->rumbleSwitch->pAsset->position = DEST_RUMBLE_POS(ent);
            zEntEvent(state->rumbleSwitch, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                      0.0f, 0.0f);
        }

        if (state->shrapnel && state->shrapnel->initCB)
        {
            *state->model->Mat = *ent->model->Mat;
            state->shrapnel->initCB(state->shrapnel, state->model, NULL, NULL);
        }
    }

    inline void zDestructible_PlayAnim(xEnt* ent, zDestructibleState& state, S32 idx)
    {
        if (state.animList)
        {
            ((zEntSimpleObj*)ent)->anim = state.animList->animData[idx - 1];
        }
        else
        {
            ((zEntSimpleObj*)ent)->anim = NULL;
        }
        ((zEntSimpleObj*)ent)->animTime = 0.0f;
    }

    inline void zDestructible_Swap(xEnt* ent, zDestructible* dest, xVec3& initial_velocity,
                                   bool state_change)
    {
        zDestructibleState* state = &dest->dstates[dest->curstateidx];

        SwapModelThroughState(ent, state, dest);

        if (dest->has_animation)
        {
            zDestructible_PlayAnim(ent, dest->dstates[dest->curstateidx], 1);
        }

        if (state->rumbleHit && state->rumbleHit->pAsset)
        {
            state->rumbleHit->pAsset->position = DEST_RUMBLE_POS(ent);
            zEntEvent(state->rumbleHit, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                      0.0f, 0.0f);
        }

        if (!state_change && state->shrapnelhit && state->shrapnelhit->initCB)
        {
            state->shrapnelhit->initCB(state->shrapnelhit, state->model, &initial_velocity, NULL);
        }
    }

    inline void LoadModel(xEnt* ent, zDestructible* dest)
    {
        U32 modelID = dest->base_modelID;
        RpAtomic* imodel = (RpAtomic*)xSTFindAsset(modelID, NULL);

        if (imodel)
        {
            RwV3d temp_pos = ent->model->Mat->pos;

            if (ent->model)
            {
                xModelInstanceFree(ent->model);
                ent->model = NULL;
            }

            ent->model = xEntLoadModel(NULL, imodel, NULL);
            xVec3Init((xVec3*)&ent->model->Mat->pos, temp_pos.x, temp_pos.y, temp_pos.z);
            ent->model->Flags &= (U16)~0x400;
            ent->model->Pipe.Flags |= 0x100000;
            ent->model->modelID = modelID;
        }
    }

    inline void zDestructible_Launch(xEnt* ent, xVec3& direction)
    {
        xVec3 vec;
        xVec3 y_axis;
        xMat3x3 rot;

        ent->pflags |= 0x2;
        ent->pflags |= 0x4;
        ent->frame->vel.x = direction.x;
        ent->frame->vel.y = direction.y;
        ent->frame->vel.z = direction.z;
        vec = ent->frame->vel;
        y_axis.x = 0.0f;
        y_axis.y = 1.0f;
        y_axis.z = 0.0f;
        xVec3Rotate(&rot, &vec, &y_axis, LAUNCH_ANGLE * DEG_TO_RAD);
        xMat3x3RMulVec(&vec, &rot, &vec);
        xVec3Normalize(&vec, &vec);
        ent->frame->vel = vec;
        ent->frame->vel *= LAUNCH_VEL;
    }

    void DOBJ_alwaysUseSphere(xEnt* ent, xVec3*)
    {
        xVec3 bndcent = { 0.0f, 0.0f, 0.0f };

        xVec3Copy(&bndcent, xEntGetPos(ent));
        ent->bound.type = XBOUND_TYPE_SPHERE;
        xVec3Copy(&ent->bound.sph.center, &bndcent);
        ent->bound.sph.center.y += DOBJ_STD_BOUND_VAR;
        ent->bound.sph.r = DOBJ_STD_BOUND_VAR;
        if (ent->bound.type != XBOUND_TYPE_NA)
        {
            xQuickCullForBound(&ent->bound.qcd, &ent->bound);
        }
        xBoundUpdate(&ent->bound);
        zGridUpdateEnt(ent);
    }

    inline void zDestructible_SendEvent(xEnt* ent, xBase* dest)
    {
        S32 hit_sourcetype = zHS_GENERAL;
        const zHitSourceMapEntry* damage_type_map;

        // The target only ever reads the first map entry: the loop body always breaks.
        for (damage_type_map = zHitSourceMap; damage_type_map->name; damage_type_map++)
        {
            hit_sourcetype = damage_type_map->sourcetype;
            break;
        }

        F32 params[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        params[1] = hit_sourcetype;
        params[3] = ent->frame->vel.x;
        params[4] = ent->frame->vel.y;
        params[5] = ent->frame->vel.z;
        zEntEvent(ent, dest, eEventHit, params, NULL, 0);
    }

    void zDestructible_AffectAll(xEnt* ent, zDestructible* dest, xScene* sc, F32 dt)
    {
        if (!(dest->behaviour & 0x8))
        {
            xVec3 dist;
            dist = ent->frame->mat.pos - xglobals->___player_ent_dont_use_directly->frame->mat.pos;
            if (dist.length2() < SQR(DAMAGE_RADIUS) && xglobals->___player_ent_dont_use_directly)
            {
                zDestructible_SendEvent(ent, xglobals->___player_ent_dont_use_directly);
            }
        }

        void (*bak_bupdate)(xEnt*, xVec3*) = ent->bupdate;
        ent->bupdate = DOBJ_alwaysUseSphere;

        xEntCollis ent_collis_rec = { 0 };
        memset(&ent_collis_rec, 0, sizeof(xEntCollis));
        ent_collis_rec.post = NULL;
        ent_collis_rec.pen = 0;
        ent_collis_rec.depenq = NULL;
        ent_collis_rec.chk = 0x3e;
        ent->collis = &ent_collis_rec;
        DOBJ_STD_BOUND_VAR = 0.5f;
        xEntCollide(ent, sc, dt);

        if (ent->collis->env_sidx != ent->collis->env_eidx ||
            ent->collis->npc_sidx != ent->collis->npc_eidx ||
            ent->collis->dyn_sidx != ent->collis->dyn_eidx)
        {
            dest->hit_to_destroy_timer = 0.0f;
            dest->launch_flag = 0;
            zDestructibleAsset_EventCB(ent, dest->root_ent, dest, eEventDestroy, NULL, NULL, 0);

            memset(&ent_collis_rec, 0, sizeof(xEntCollis));
            ent_collis_rec.post = NULL;
            ent_collis_rec.chk = 0xc;
            ent_collis_rec.pen = 0;
            ent_collis_rec.depenq = NULL;
            ent->collis = &ent_collis_rec;
            DOBJ_STD_BOUND_VAR = DAMAGE_RADIUS;
            xEntCollide(ent, sc, dt);

            if (ent->collis->dyn_sidx != ent->collis->dyn_eidx)
            {
                for (S32 i = ent->collis->dyn_sidx; i < ent->collis->dyn_eidx; i++)
                {
                    if (ent->collis->colls[i].optr)
                    {
                        zDestructible_SendEvent(ent, (xBase*)ent->collis->colls[i].optr);
                    }
                }
            }

            if (ent->collis->npc_sidx != ent->collis->npc_eidx)
            {
                for (S32 i = ent->collis->npc_sidx; i < ent->collis->npc_eidx; i++)
                {
                    if (ent->collis->colls[i].optr)
                    {
                        zDestructible_SendEvent(ent, (xBase*)ent->collis->colls[i].optr);
                    }
                }
            }
        }

        if (dest->hit_to_destroy_timer > MAX_TIME_NO_COLLISIONS)
        {
            dest->hit_to_destroy_timer = 0.0f;
            dest->launch_flag = 0;
            zDestructibleAsset_EventCB(ent, dest->root_ent, dest, eEventDestroy, NULL, NULL, 0);
        }

        ent->bupdate = bak_bupdate;
        ent->collis = NULL;
    }

    U32 BurnSinRateCallback(F32 time)
    {
        return 500.0f * isin(-PI * ((6.0f - time) / 6.0f) + PI);
    }
} // namespace

void zDestructibleAsset_Reset(zDestructible*& dest, xEnt* ent)
{
    xDestructibleAssetState* d_states;
    U32 i;
    RpAtomic* imodel;
    U32 j;
    zDestructibleStateFXParams* fx_Param;
    xDestructibleAssetAttachedAnimList* ds_state_anim_list;
    U32* animListIDs;
    void* tmp_ptr;

    if (dest && dest->asset)
    {
        if (dest->dstates[dest->curstateidx].model &&
            dest->dstates[dest->curstateidx].model->LightKit)
        {
            ent->model->LightKit = dest->dstates[dest->curstateidx].model->LightKit;

            UpdateModelBound(ent, ent->collModel ? ent->collModel : ent->model, TRUE);
        }

        dest->cur_hit_pts = 0;
        dest->curstateidx = 0;
        dest->hit_pts = dest->asset->hit_points;
        dest->nstates = dest->asset->nstates;
        dest->hit_filter = dest->asset->hit_filter;
        dest->launch_flag = dest->asset->launch_flag;
        dest->behaviour = dest->asset->behaviour;
        dest->respawn_time = dest->asset->respawn;
        dest->timer = 0.0f;
        dest->destroy_notify = NULL;
        dest->notify_context = NULL;
        dest->flags = dest->asset->flags;
        dest->sg_handle_idle = xSndMgrGetSoundGroup(dest->asset->soundgroupidleID);
        dest->hit_to_destroy_timer = 0.0f;
        dest->root_ent = NULL;
        dest->has_animation = 0;
        dest->active = 1;
        dest->base_modelID = ent->model->modelID;
        ent->model->FadeEnd = dest->dstates[0].model->FadeEnd;
        ent->model->FadeStart = dest->dstates[0].model->FadeStart;

        d_states = (xDestructibleAssetState*)&dest->asset->states;

        for (i = 0; i < dest->asset->nstates; i++)
        {
            imodel = (RpAtomic*)xSTFindAsset(d_states->modelID, NULL);
            if (imodel)
            {
                *dest->dstates[i].model->Mat = *ent->model->Mat;
                dest->dstates[i].model->Surf = ent->model->Surf;
                dest->dstates[i].model->Flags &= (U16)~0x400;
                dest->dstates[i].model->modelID = d_states->modelID;
            }

            if (d_states->shrapnelID)
            {
                dest->dstates[i].shrapnel =
                    (zShrapnelAsset*)xSTFindAsset(d_states->shrapnelID, NULL);
            }
            else
            {
                dest->dstates[i].shrapnel = NULL;
            }

            if (d_states->shrapnelhitID)
            {
                dest->dstates[i].shrapnelhit =
                    (zShrapnelAsset*)xSTFindAsset(d_states->shrapnelhitID, NULL);
            }
            else
            {
                dest->dstates[i].shrapnelhit = NULL;
            }

            dest->dstates[i].percent = d_states->percent;

            if (dest->dstates[i].fxList.nfx)
            {
                for (j = 0; j < dest->dstates[i].fxList.nfx; j++)
                {
                    fx_Param = &dest->dstates[i].fxList.fxParams[j];
                    fx_Param->timer = 0.0f;
                    fx_Param->flags = 0;
                    fx_Param->flags |= 0x1;
                    fx_Param->flags |= 0x2;
                    fx_Param->fn = BurnSinRateCallback;
                    fx_Param->emitter = zParEmitterFind("PAREMIT_FIRESMOKE");
                }
            }

            ds_state_anim_list = (xDestructibleAssetAttachedAnimList*)&d_states->animlist;
            if (ds_state_anim_list && ds_state_anim_list->nanimations)
            {
                animListIDs = (U32*)&ds_state_anim_list->animationIDs;
                for (U32 j = 0; j < ds_state_anim_list->nanimations; j++)
                {
                    dest->dstates[i].animList->animData[j] = xSTFindAsset(animListIDs[j], NULL);
                }
            }

            dest->dstates[i].sg_handle_idle = xSndMgrGetSoundGroup(d_states->soundgroupidleID);
            dest->dstates[i].sh_handle_idle = (iSndHandle)-1;
            dest->dstates[i].sg_handle_hit = xSndMgrGetSoundGroup(d_states->soundgrouphitID);
            dest->dstates[i].sh_handle_hit = (iSndHandle)-1;
            dest->dstates[i].sg_handle_hit_switch =
                xSndMgrGetSoundGroup(d_states->soundgrouphitIDswitch);
            dest->dstates[i].sh_handle_hit_switch = (iSndHandle)-1;
            dest->dstates[i].sg_fx_id = d_states->soundgroupfxID;
            dest->dstates[i].sg_fx_id_switch = d_states->soundgroupfxIDswitch;

            if (d_states->rumbleIDhit)
            {
                dest->dstates[i].rumbleHit =
                    (xRumble::sphericalEmitter*)zSceneFindObject(d_states->rumbleIDhit);
            }

            if (d_states->rumbleIDswitch)
            {
                dest->dstates[i].rumbleSwitch =
                    (xRumble::sphericalEmitter*)zSceneFindObject(d_states->rumbleIDswitch);
            }

            tmp_ptr = d_states + 1;
            tmp_ptr = (U32*)tmp_ptr + ds_state_anim_list->nanimations;
            d_states = (xDestructibleAssetState*)tmp_ptr;
        }
    }
}

void zDestructibleAsset_ReReset(zDestructible*& dest, xEnt* ent)
{
    xDestructibleAssetState* d_states;
    U32 i;
    xDestructibleAssetAttachedAnimList* ds_state_anim_list;
    void* tmp_ptr;

    if (dest && dest->asset)
    {
        d_states = (xDestructibleAssetState*)&dest->asset->states;

        for (i = 0; i < dest->asset->nstates; i++)
        {
            if (d_states->rumbleIDhit)
            {
                dest->dstates[i].rumbleHit =
                    (xRumble::sphericalEmitter*)zSceneFindObject(d_states->rumbleIDhit);
            }

            if (d_states->rumbleIDswitch)
            {
                dest->dstates[i].rumbleSwitch =
                    (xRumble::sphericalEmitter*)zSceneFindObject(d_states->rumbleIDswitch);
            }

            ds_state_anim_list = (xDestructibleAssetAttachedAnimList*)&d_states->animlist;
            tmp_ptr = d_states + 1;
            tmp_ptr = (U32*)tmp_ptr + ds_state_anim_list->nanimations;
            d_states = (xDestructibleAssetState*)tmp_ptr;
        }
    }
}

void zDestructibleAsset_Init(U32 assetID, zDestructible*& dest, xEnt* ent, U32)
{
    xDestructibleAssetState* d_states;
    U32 i;
    U32 fx_count;
    xDestructibleAssetAttachedAnimList* ds_state_anim_list;
    U32 modelID;
    RpAtomic* imodel;
    void* tmp_ptr;

    if (assetID)
    {
        dest = (zDestructible*)xMemAlloc(gActiveHeap, sizeof(zDestructible), 0);
        dest->asset = (xDestructibleAsset*)xSTFindAsset(assetID, NULL);
        d_states = (xDestructibleAssetState*)&dest->asset->states;
        dest->dstates = (zDestructibleState*)xMemAlloc(
            gActiveHeap, dest->asset->nstates * sizeof(zDestructibleState), 0);

        for (i = 0; i < dest->asset->nstates; i++)
        {
            fx_count = 0;
            if (d_states->fx_flags & 0x1)
            {
                fx_count++;
            }
            if (d_states->fx_flags & 0x2)
            {
                fx_count++;
            }
            if (d_states->fx_flags & 0x4)
            {
                fx_count++;
            }
            if (d_states->fx_flags & 0x8)
            {
                fx_count++;
            }

            if (fx_count)
            {
                dest->dstates[i].fxList.nfx = fx_count;
                dest->dstates[i].fxList.fxParams = (zDestructibleStateFXParams*)xMemAlloc(
                    gActiveHeap, fx_count * sizeof(zDestructibleStateFXParams), 0);
            }
            else
            {
                dest->dstates[i].fxList.nfx = 0;
            }

            ds_state_anim_list = (xDestructibleAssetAttachedAnimList*)&d_states->animlist;
            if (ds_state_anim_list && ds_state_anim_list->nanimations)
            {
                dest->dstates[i].animList = (zDestructibleStateAttachedAnimList*)xMemAlloc(
                    gActiveHeap, sizeof(zDestructibleStateAttachedAnimList), 0);
                dest->dstates[i].animList->animData = (void**)xMemAlloc(
                    gActiveHeap, ds_state_anim_list->nanimations * sizeof(void*), 0);
            }

            modelID = d_states->modelID;
            imodel = (RpAtomic*)xSTFindAsset(modelID, NULL);
            if (!imodel)
            {
                modelID = xStrHashCat(d_states->modelID, ".dff");
                imodel = (RpAtomic*)xSTFindAsset(modelID, NULL);
            }

            dest->dstates[i].model = xEntLoadModel(NULL, imodel, NULL);
            dest->dstates[i].model->modelID = modelID;

            tmp_ptr = d_states + 1;
            tmp_ptr = (U32*)tmp_ptr + ds_state_anim_list->nanimations;
            d_states = (xDestructibleAssetState*)tmp_ptr;
        }

        zDestructibleAsset_Reset(dest, ent);
    }
}

void zDestructibleAsset_CopyLODDistances(xEnt* ent, zDestructible* destructible)
{
    U32 state;
    xModelInstance* model;

    for (state = 0; state < destructible->asset->nstates; state++)
    {
        model = destructible->dstates[state].model;
        model->FadeEnd = ent->model->FadeEnd;
        model->FadeStart = ent->model->FadeStart;
    }
}

static void zDestructible_StateProcess(xEnt* ent, zDestructible* dest, U32 damage, U32 idx,
                                       const F32* toParam, xVec3& initial_velocity,
                                       bool state_change)
{
    U32 _percent;

    if (idx >= dest->nstates)
    {
        dest->curstateidx--;
        if (dest->launch_flag)
        {
            zDestructibleAsset_EventCB(ent, dest->root_ent, dest, eEventDestructibleLaunch, toParam,
                                       NULL, 0);
        }
        else
        {
            zDestructibleAsset_EventCB(ent, dest->root_ent, dest, eEventDestroy, toParam, NULL, 0);
        }
        return;
    }

    dest->cur_hit_pts += damage;
    _percent = 100.0f * dest->cur_hit_pts / dest->hit_pts;

    if (_percent >= dest->dstates[dest->curstateidx].percent)
    {
        damage = std::ceilf(
            0.01f * (dest->hit_pts * (_percent - dest->dstates[dest->curstateidx].percent)));
        dest->cur_hit_pts -= damage;

        if (!state_change)
        {
            zDestructible_StateChange(ent, dest);
        }

        zDestructible_StateProcess(ent, dest, damage, ++dest->curstateidx, toParam,
                                   initial_velocity, true);
    }
    else
    {
        zDestructible_StopSounds(ent, dest);
        zDestructible_StartSounds(ent, dest);
        zDestructible_Swap(ent, dest, initial_velocity, state_change);
    }
}

void zDestructibleAsset_EventCB(xBase* from, xBase* root_base, zDestructible* to, U32 toEvent,
                                const F32* toParam, xBase*, U32)
{
    xEnt* ent = (xEnt*)from;
    xEnt* root_ent = (xEnt*)root_base;
    zDestructible* dest = to;
    zDestructibleState* dstates = dest->dstates;
    zDestructibleState* cstate = &dstates[dest->curstateidx];

    switch (toEvent)
    {
    case eEventSceneBegin:
        dest->sh_handle_idle =
            xSndMgrPlay(dest->sg_handle_idle, 0, DEST_SND_POS(ent), NULL, ent, NULL, NULL);
        break;
    case eEventAnimPlayRandom:
        if (cstate->animList)
        {
            S32 up = cstate->animList->nanimations;
            S32 idx = (S32)xrand_GenRandInt32() % up + 1;
            if (dest->has_animation)
            {
                zDestructible_PlayAnim(ent, *cstate, idx);
            }
        }
        break;
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
        if (cstate->animList)
        {
            S32 idx = toParam[0];
            if (dest->has_animation)
            {
                zDestructible_PlayAnim(ent, *cstate, idx);
            }
        }
        break;
    case eEventDestructibleRespawn:
        dest->timer = RESPAWN_THRESHOLD;
        break;
    case eEventDestructibleLaunch:
    {
        xVec3 dir;

        dest->hit_to_destroy_timer = 1e-6f;
        dir.x = toParam[2];
        dir.y = 0.0f;
        dir.z = toParam[3];
        zDestructible_Launch(ent, dir);
        zEntEvent(from, from, eEventCollisionOn);
        zEntEvent(from, from, eEventVisible);
        break;
    }
    case eEventDestroy:
    {
        xSurface* surf;

        if (dest->destroy_notify)
        {
            dest->destroy_notify(dest, dest->notify_context);
        }

        zDestructible_StopSounds(ent, dest);
        zDestructible_Swap(ent, dest, DEST_FRAME_VEL(ent), false);

        if (dest->dstates[dest->curstateidx].percent != 100)
        {
            surf = ent->model->Surf;
            LoadModel(ent, dest);
            ent->model->Surf = surf;
            zEntEvent(from, from, eEventCollisionOff);
            zEntEvent(from, from, eEventInvisible);
        }
        else
        {
            zDestructible_StartSounds(ent, dest);
        }

        dest->active = FALSE;
        dest->root_ent = NULL;
        zEntEvent(from, from, eEventDestroy);
        dest->timer = RESPAWN_THRESHOLD;

        if (dest->dstates[dest->curstateidx].percent != 100)
        {
            zEntEvent(from, from, eEventDisable);
        }
        break;
    }
    case eEventHit:
    case eEventHit_Melee:
    case eEventHit_BubbleBash:
    case eEventHit_BubbleBowl:
    case eEventHit_PatrickSlam:
    case eEventHit_Throw:
    case eEventHit_Cruise:
    case eEventHit_Cartwheel:
#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 13): on the guest, a hit on a
        // breakable the host owns is a claim; the host's break comes back.
        // FALSE at once when co-op is off.
        if (iCoopGuestBreakableHit(from, toEvent, toParam))
        {
            break;
        }
#endif
        if (dest->active)
        {
            U32 hit_source_flag;
            U32 damage;

            dest->root_ent = root_ent;

            if (!toParam)
            {
                hit_source_flag = 0x2;
                damage = 1;
            }
            else
            {
                hit_source_flag = 1 << (S32)toParam[1];
                damage = toParam[0];
            }

            if (dest->hit_filter & 0x2 || dest->hit_filter & hit_source_flag)
            {
                xVec3 kickback = { 0.0f, 3.0f, 0.0f };

                if (toParam)
                {
                    xVec3Init(&kickback, toParam[2], 3.0f, toParam[3]);
                }

                xSndMgrStop(dest->sh_handle_idle);
                zDestructible_StateProcess(ent, dest, damage, dest->curstateidx, toParam, kickback,
                                           false);
                zEntEvent(from, from, eEventHit);
            }
        }
        break;
    }
}

void zDestructibleAsset_Update(xEnt* ent, zDestructible* dest, xScene* sc, F32 dt)
{
    if ((dest->behaviour & 0x1 || dest->behaviour & 0x8) && dest->hit_to_destroy_timer)
    {
        if (dest->behaviour & 0x8 && dest->hit_to_destroy_timer > 0.3f)
        {
            dest->behaviour = dest->behaviour & ~0x8;
        }

        dest->hit_to_destroy_timer += dt;

        if (dest->behaviour & 0x2)
        {
            zDestructible_AffectAll(ent, dest, sc, dt);
        }
        else if (dest->hit_to_destroy_timer > HIT_TO_DESTROY_TIME)
        {
            dest->hit_to_destroy_timer = 0.0f;
            dest->launch_flag = 0;
            zDestructibleAsset_EventCB(ent, dest->root_ent, dest, eEventDestroy, NULL, NULL, 0);
        }
    }

    if (dest->respawn_time && dest->timer)
    {
        dest->timer += dt;
        if (dest->timer > dest->respawn_time)
        {
            dest->timer = 0.0f;
            zEntEvent(ent, ent, eEventCollisionOn);
            zEntEvent(ent, ent, eEventVisible);
            zEntEvent(ent, ent, eEventReset);
        }
    }
}

zDestructible* zDestructibleGetFromEntity(xEnt* ent)
{
    switch (ent->baseType)
    {
    case eBaseTypeStatic:
        return ((zEntSimpleObj*)ent)->destructible;
    case eBaseTypePlatform:
        return ((zPlatform*)ent)->destructible;
    }

    return NULL;
}
