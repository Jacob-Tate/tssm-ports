#include "xEntMotion.h"
#include "xSplineAsset.h"
#include "xstransvc.h"
#include "xSpline.h"
#include "xCam.h"

static U32 xEntRotMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static U32 xEntSldMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static U32 xEntScale(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static void xEntMechMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static void xEntSplineMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static void xEntMPMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static void xEntERMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame);
static void xEntMPGetNext(xEntMotion* motion, xMovePoint* prev, xScene* sc);

void xEntMotionInit(xEntMotion* motion, xEnt* owner, xEntMotionAsset* asset)
{
    motion->asset = asset;
    motion->type = asset->type;
    motion->ent_motion_flags = asset->ent_motion_flags;

    if (motion->type == XENTMOTION_TYPE_ER)
    {
        xVec3Copy(&motion->er.a, &asset->er.ret_pos);
        xVec3Add(&motion->er.b, &asset->er.ret_pos, &asset->er.ext_dpos);

        motion->er.et = asset->er.ext_tm;
        motion->er.wet = asset->er.ext_wait_tm;
        motion->er.rt = asset->er.ret_tm;
        motion->er.wrt = asset->er.ret_wait_tm;

        if (motion->er.p <= 0.0f)
        {
            motion->er.p = 1.0f;
        }
    }
    else if (motion->type == XENTMOTION_TYPE_ORB)
    {
        xVec3Copy(&motion->orb.c, &asset->orb.center);

        motion->orb.a = asset->orb.w;
        motion->orb.b = asset->orb.h;

        if (asset->orb.period <= 1e-5f)
        {
            asset->orb.period = 1.0f;
        }

        motion->orb.p = asset->orb.period;
        motion->orb.w = (2 * PI) / asset->orb.period;
    }
    else if (motion->type == XENTMOTION_TYPE_MP)
    {
    }
    else if (motion->type == XENTMOTION_TYPE_SPL)
    {
    }
    else if (motion->type == XENTMOTION_TYPE_PEN)
    {
        if (asset->pen.period <= 1e-5f)
        {
            asset->pen.period = 1.0f;
        }

        motion->pen.w = (2 * PI) / asset->pen.period;
    }
    else if (motion->type == XENTMOTION_TYPE_MECH)
    {
        xEntMotionMechData* mkasst = &asset->mech;

        if (mkasst->sld_tm < 1e-5f)
        {
            mkasst->sld_tm = 1.0f;
        }

        if (mkasst->sld_acc_tm + mkasst->sld_dec_tm > mkasst->sld_tm)
        {
            mkasst->sld_dec_tm = mkasst->sld_acc_tm = mkasst->sld_tm * 0.5f;
        }

        if (mkasst->rot_tm < 1e-5f)
        {
            mkasst->rot_tm = 1.0f;
        }

        if (mkasst->scale_duration < 1e-5f)
        {
            mkasst->scale_duration = 1.0f;
        }

        if (mkasst->type == 2)
        {
            if (mkasst->rot_tm != mkasst->sld_tm)
            {
                mkasst->rot_tm = mkasst->sld_tm;
            }
        }

        if (mkasst->type == 6)
        {
            if (mkasst->rot_tm != mkasst->sld_tm)
            {
                mkasst->sld_tm = mkasst->scale_duration;
            }
        }

        if (mkasst->rot_acc_tm + mkasst->rot_dec_tm > mkasst->rot_tm)
        {
            mkasst->rot_dec_tm = mkasst->rot_acc_tm = mkasst->rot_tm * 0.5f;
        }
    }

    motion->owner = owner;
    motion->target = NULL;
}

static inline void xEntSpeedDataReset(xEntSpeedData* speed_data)
{
    speed_data->speed = speed_data->acceleration = 0.0f;
    speed_data->dest_speed = 0.0f;
}

void xEntMotionSetup(xEntMotion* motion, xScene* sc)
{
    motion->mech.current_scale = 1.0f;

    if (motion->type == XENTMOTION_TYPE_ER)
    {
        motion->er.brt = motion->er.et + motion->er.wet;
        motion->er.ert = motion->er.brt + motion->er.rt;
        motion->er.p = motion->er.ert + motion->er.wrt;
    }
    else if (motion->type == XENTMOTION_TYPE_ORB)
    {
    }
    else if (motion->type == XENTMOTION_TYPE_MP)
    {
    }
    else if (motion->type == XENTMOTION_TYPE_SPL)
    {
        motion->spl.spline = (x::NURBS*)xSTFindAsset(motion->asset->spl.spline_id, NULL);
        motion->spl.u = motion->spl.spline->start();
    }
    else if (motion->type == XENTMOTION_TYPE_PEN)
    {
    }
    else if (motion->type == XENTMOTION_TYPE_MECH)
    {
        if (motion->asset->mech.type == 6 || motion->asset->mech.type == 5)
        {
            xModelInstance* model = motion->owner->model;

            if (model->Scale.x == 0.0f)
            {
                xVec3Init(&model->Scale, 1.0f, 1.0f, 1.0f);
            }

            motion->mech.original_bound = motion->owner->bound;
        }
    }
}

void xEntMotionReset(xEntMotion* motion, xScene* sc)
{
    xEntMotionPenData* aspen;
    F32 dangle;
    xMat3x3 pshrot;
    xVec3 totop;
    xVec3 pshtotop;
    xVec3 pshdelta;
    xMat3x3* modlrot;
    xVec3* modlpos;
    xEnt* ownr;
    xEntMotionMechData* mkasst;
    xEntMechData* mech;
    F32 scale;
    F32 model_scale;
    F32 asset_scale;
    F32 drot;

    motion->ent_motion_flags = motion->asset->ent_motion_flags;
    motion->t = 0.0f;
    motion->mech.current_scale = 1.0f;

    if (motion->type == XENTMOTION_TYPE_ER)
    {
        motion->er.state = 0;
        xVec3Copy(&motion->er.a, &motion->asset->er.ret_pos);
        xVec3Add(&motion->er.b, &motion->asset->er.ret_pos, &motion->asset->er.ext_dpos);
    }
    else if (motion->type == XENTMOTION_TYPE_MP)
    {
        motion->mp.src = (xMovePoint*)xSceneResolvID(sc, motion->asset->mp.mp_id);
        xEntSpeedDataReset(&motion->mp.speed_data);
        xEntMPSetSpeed(motion, motion->asset->mp.speed);
        xEntMPGetNext(motion, NULL, sc);
        xVec3Copy((xVec3*)&motion->owner->model->Mat->pos, xMovePointGetPos(motion->mp.src));
        motion->tmr = 0.0f;
    }
    else if (motion->type == XENTMOTION_TYPE_SPL)
    {
        xEntSpeedDataReset(&motion->spl.speed_data);
        motion->spl.speed_data.speed = motion->asset->spl.speed;
        motion->spl.arrived = 0;
        motion->spl.position_blend_v = 0.0f;
        motion->spl.orient_blend_v = 0.0f;

        if (motion->spl.spline != NULL)
        {
            motion->spl.u = motion->spl.spline->start();
        }
    }
    else if (motion->type == XENTMOTION_TYPE_ORB)
    {
        xVec3Copy(&motion->orb.orig, (xVec3*)&motion->owner->model->Mat->pos);
    }
    else if (motion->type == XENTMOTION_TYPE_PEN)
    {
        modlpos = (xVec3*)&motion->owner->model->Mat->pos;
        modlrot = (xMat3x3*)motion->owner->model->Mat;
        aspen = &motion->asset->pen;

        dangle = aspen->range * isin(aspen->phase);
        xMat3x3Rot(&pshrot, &modlrot->at, dangle);

        totop.x = 0.0f;
        totop.y = aspen->len;
        totop.z = 0.0f;

        xMat3x3RMulVec(&pshtotop, &pshrot, &totop);
        xMat3x3Mul(modlrot, modlrot, &pshrot);
        xVec3Sub(&pshdelta, &totop, &pshtotop);
        xVec3AddTo(modlpos, &pshdelta);
        xVec3Add(&motion->pen.top, modlpos, &pshtotop);
        xMat4x3Copy(&motion->pen.omat, (xMat4x3*)modlrot);
    }
    else if (motion->type == XENTMOTION_TYPE_MECH)
    {
        ownr = motion->owner;
        mkasst = &motion->asset->mech;
        mech = &motion->mech;

        if (ownr != NULL && ownr->frame != NULL && ownr->model != NULL)
        {
            xVec3Copy(&mech->original_position, (xVec3*)&ownr->model->Mat->pos);

            if (mkasst->sld_axis == 0)
            {
                xVec3Copy(&mech->direction, (xVec3*)&ownr->model->Mat->right);
            }
            else if (mkasst->sld_axis == 1)
            {
                xVec3Copy(&mech->direction, (xVec3*)&ownr->model->Mat->up);
            }
            else
            {
                xVec3Copy(&mech->direction, (xVec3*)&ownr->model->Mat->at);
            }

            scale = 1.0f;
            model_scale = 1.0f;
            asset_scale = 1.0f;

            switch (mkasst->sld_axis)
            {
            case 0:
                model_scale = !motion->owner->model->Scale.x ? 1.0f : motion->owner->model->Scale.x;
                asset_scale = !motion->owner->asset->scale.x ? 1.0f : motion->owner->asset->scale.x;
                break;
            case 1:
                model_scale = !motion->owner->model->Scale.y ? 1.0f : motion->owner->model->Scale.y;
                asset_scale = !motion->owner->asset->scale.y ? 1.0f : motion->owner->asset->scale.y;
                break;
            case 2:
                model_scale = !motion->owner->model->Scale.z ? 1.0f : motion->owner->model->Scale.z;
                asset_scale = !motion->owner->asset->scale.z ? 1.0f : motion->owner->asset->scale.z;
                break;
            }

            scale /= asset_scale * model_scale;

            xVec3SMul(&mech->destination_position, &mech->direction, mkasst->sld_dist * scale);
            xVec3AddTo(&mech->destination_position, &mech->original_position);

            mech->sliding_speed =
                mkasst->sld_dist /
                (mkasst->sld_tm - (mkasst->sld_acc_tm + mkasst->sld_dec_tm) * 0.5f);
            mech->sliding_forth_decel_time = mkasst->sld_tm - mkasst->sld_dec_tm;
            mech->sliding_back_decel_time = mkasst->sld_tm - mkasst->sld_acc_tm;

            if (mkasst->rot_axis == 0)
            {
                mech->entity_rotation_angle = &ownr->frame->rot.axis.y;
            }
            else if (mkasst->rot_axis == 1)
            {
                mech->entity_rotation_angle = &ownr->frame->rot.axis.x;
            }
            else
            {
                mech->entity_rotation_angle = &ownr->frame->rot.axis.z;
            }

            mech->original_rotation = *mech->entity_rotation_angle;
            drot = (PI / 180.0f) * mkasst->rot_dist;
            mech->destination_rotation = xAngleClamp(mech->original_rotation + drot);
            mech->rotation_speed =
                drot / (mkasst->rot_tm - (mkasst->rot_acc_tm + mkasst->rot_dec_tm) * 0.5f);
            mech->rotation_forth_decel_time = mkasst->rot_tm - mkasst->rot_dec_tm;
            mech->rotating_back_decel_time = mkasst->rot_tm - mkasst->rot_acc_tm;

            if (mkasst->type == 0 || mkasst->type == 2 || mkasst->type == 3)
            {
                mech->state = 0;
            }
            else if (mkasst->type == 5 || mkasst->type == 6)
            {
                mech->state = 8;
            }
            else
            {
                mech->state = 1;
            }

            motion->tmr = 0.0f;
        }
    }
}

static inline void xEntOrbitMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    F32 rem = dt * 0.5f;
    F32 u = motion->orb.w * (motion->t + rem);
    F32 th = isin(motion->orb.w * rem);
    F32 newt = motion->t - motion->orb.p;

    if (newt > 0.0f)
    {
        motion->t = newt;
    }

    if (-newt < dt && -newt > 0.0f)
    {
        xVec3Sub(&frame->dpos, &motion->orb.orig, &frame->mat.pos);
    }
    else
    {
        frame->dpos.x = 2.0f * th * motion->orb.a * isin(u);
        frame->dpos.y = 0.0f;
        frame->dpos.z = -2.0f * th * motion->orb.b * icos(u);
    }

    frame->mode = 2;
}

static inline void xEntPenMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntPenData* pen = &motion->pen;
    xEntMotionPenData* aspen = &motion->asset->pen;
    F32 tf = dt * 0.5f;
    F32 th = tf * pen->w;
    F32 rem = motion->t + tf;
    F32 u = rem * pen->w + aspen->phase;
    F32 dangle = 2.0f * aspen->range * isin(th) * icos(u);
    F32 newt = motion->t - aspen->period;

    if (newt > 0.0f)
    {
        motion->t = newt;
    }

    if (-newt < dt && -newt > 0.0f)
    {
        xMat4x3Copy(&frame->mat, &motion->pen.omat);
        frame->mode = 0x11;
    }
    else
    {
        xMat4x3 delta_mat;
        xMat4x3Rot(&delta_mat, &frame->mat.at, dangle, &pen->top);
        xMat4x3Mul(&frame->mat, &frame->mat, &delta_mat);
        frame->mode = 0x41;
    }
}

void xEntMotionMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    if (motion->ent_motion_flags & 4)
    {
        return;
    }

    if (motion->type == XENTMOTION_TYPE_ER)
    {
        xEntERMove(motion, sc, dt, frame);
    }
    else if (motion->type == XENTMOTION_TYPE_ORB)
    {
        xEntOrbitMove(motion, sc, dt, frame);
    }
    else if (motion->type == XENTMOTION_TYPE_MP)
    {
        xEntMPMove(motion, sc, dt, frame);
    }
    else if (motion->type == XENTMOTION_TYPE_SPL)
    {
        xEntSplineMove(motion, sc, dt, frame);
    }
    else if (motion->type == XENTMOTION_TYPE_PEN)
    {
        xEntPenMove(motion, sc, dt, frame);
    }
    else if (motion->type == XENTMOTION_TYPE_MECH)
    {
        xEntMechMove(motion, sc, dt, frame);
    }
    else if (motion->type == 6)
    {
    }
    else
    {
        frame->mode = 0;
    }

    motion->t += dt;
}

static void xEntERMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    F32 newt = motion->t - motion->er.p;

    if (newt > 0.0f)
    {
        motion->t = newt;
    }

    if (xEntERIsExtending(motion))
    {
        F32 rem = motion->er.et - motion->t;
        F32 scale = MIN(rem, dt) / motion->er.et;

        if (rem < dt)
        {
            xVec3Sub(&frame->dpos, &motion->er.b, &frame->mat.pos);
        }
        else
        {
            xVec3Sub(&frame->dpos, &motion->er.b, &motion->er.a);
            xVec3SMulBy(&frame->dpos, scale);
        }

        frame->mode = 2;
        motion->er.state = 0;
    }
    else if (xEntERIsExtended(motion))
    {
        motion->er.state = 1;
    }
    else if (xEntERIsRetracting(motion))
    {
        F32 rem = motion->er.ert - motion->t;
        F32 scale = MIN(rem, dt) / motion->er.rt;

        if (rem < dt)
        {
            xVec3Sub(&frame->dpos, &motion->er.a, &frame->mat.pos);
        }
        else
        {
            xVec3Sub(&frame->dpos, &motion->er.a, &motion->er.b);
            xVec3SMulBy(&frame->dpos, scale);
        }

        frame->mode = 2;
        motion->er.state = 2;
    }
    else if (xEntERIsRetracted(motion))
    {
        motion->er.state = 3;
    }
}

static inline void xEntMPGetNext(xEntMotion* motion, xMovePoint* prev, xScene* sc)
{
    xEntMPData* mp = &motion->mp;
    xVec3 tempdir;
    xMat3x3 mat;

    mp->spl = NULL;
    mp->curdist = 0.0f;
    mp->dist = xMovePointGetNext(mp->src, prev, &mp->dest, &tempdir);

    if (mp->dest == mp->src)
    {
        xEntMotionStop(motion);
    }
    else if (mp->dest == NULL)
    {
        xEntMotionStop(motion);
    }
    else
    {
        if (motion->asset->ent_motion_flags & 1)
        {
            xQuatFromMat(&mp->aquat, (xMat3x3*)motion->owner->model->Mat);
            xVec3Inv(&tempdir, &tempdir);
            xMat3x3LookVec(&mat, &tempdir);
            xQuatFromMat(&mp->bquat, &mat);
        }

        if (mp->dest->spl != NULL)
        {
            mp->spl = mp->dest->spl;

            while (mp->dest->asset->bezIndex != 0)
            {
                mp->dest = mp->dest->nodes[0];
            }

            mp->dist = xSpline3_ArcTotal(mp->spl);
        }
    }
}

// No GC symbol (always inlined); name and signature from the Ratatouille GC DWARF.
static inline void xEntSpeedDataUpdate(xEntSpeedData* speed_data, F32 dt)
{
    if (!xfeq0(speed_data->acceleration))
    {
        speed_data->speed += speed_data->acceleration * dt;

        if ((speed_data->acceleration < 0.0f && speed_data->speed <= speed_data->dest_speed) ||
            (speed_data->acceleration > 0.0f && speed_data->speed >= speed_data->dest_speed))
        {
            speed_data->speed = speed_data->dest_speed;
            speed_data->acceleration = 0.0f;
        }
    }
}

static void xEntMPMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntMPData* mp = &motion->mp;

    if (xEntMPAtEnd(motion))
    {
        return;
    }

    if (motion->tmr > 0.0f)
    {
        motion->tmr -= dt;
        return;
    }

    xEntSpeedDataUpdate(&mp->speed_data, dt);

    frame->mode = 2;

    F32 newdist = mp->curdist + mp->speed_data.speed * dt;

    if (newdist >= mp->dist)
    {
        xVec3Sub(&frame->dpos, mp->dest->pos, &frame->mat.pos);

        if (motion->asset->mp.flags & 0x1)
        {
            xEntMotionStop(motion);
        }

        xMovePoint* prev = mp->src;
        mp->src = mp->dest;
        xEntMPGetNext(motion, prev, sc);

        motion->tmr = mp->src->asset->delay;
        return;
    }

    mp->curdist = newdist;

    F32 qdot;
    xVec3 tgt;
    xVec3 direction;
    xVec3 bank;
    xQuat quat;
    xQuat qold;
    xMat3x3 tmpmat;
    xVec3 scale;

    scale.x = frame->mat.right.length();
    scale.y = frame->mat.up.length();
    scale.z = frame->mat.at.length();

    if (mp->spl != NULL)
    {
        F32 u = xSpline3_EvalArcApprox(mp->spl, newdist, 0, &tgt);

        if (motion->asset->ent_motion_flags & 0x1)
        {
            xSpline3_EvalSeg(mp->spl, u, 1, &direction);

            if (motion->asset->use_banking == 1)
            {
                xSpline3_EvalSeg(mp->spl, u, 2, &bank);
            }

            if (xVec3Length2(&direction) < 0.000001f)
            {
                if (u < 0.1f)
                {
                    u += 0.01f;
                }
                else
                {
                    u -= 0.01f;
                }

                xSpline3_EvalSeg(mp->spl, u, 1, &direction);

                if (motion->asset->use_banking == 1)
                {
                    xSpline3_EvalSeg(mp->spl, u, 2, &bank);
                }
            }

            if (motion->asset->use_banking != 1)
            {
                xVec3Inv(&direction, &direction);
                xMat3x3LookVec(&tmpmat, &direction);
            }
            else
            {
                xVec3 gravity = { 0.0f, -sc->gravity, 0.0f };

                bank = bank * mp->speed_data.speed * 0.01f + gravity;
                direction.normalize();
                bank -= direction * bank.dot(direction);
                bank.normalize();

                tmpmat.at = direction;
                tmpmat.right = bank.cross(direction);
                tmpmat.up = bank;
            }

            xQuatFromMat(&quat, &tmpmat);
            xQuatFromMat(&qold, &frame->mat);

            qdot = xQuatDot(&quat, &qold);
            if (qdot < 0.0f)
            {
                xQuatFlip(&quat, &quat);
                qdot = -qdot;
            }

            if (qdot > 1.0f)
            {
                qdot = 1.0f;
            }
            qdot = 2.0f * xacos(qdot);

            if (qdot <= PI * dt)
            {
                frame->mode |= 0x10;
                xMat3x3Copy(&frame->mat, &tmpmat);

                frame->mat.right *= scale.x;
                frame->mat.up *= scale.y;
                frame->mat.at *= scale.z;
            }
            else
            {
                qdot = PI * dt / qdot;
                xQuatSlerp(&quat, &qold, &quat, qdot);
                xQuatNormalize(&quat, &quat);
                xQuatToMat(&quat, &frame->mat);

                frame->mat.right *= scale.x;
                frame->mat.up *= scale.y;
                frame->mat.at *= scale.z;
            }
        }
    }
    else
    {
        xVec3Lerp(&tgt, mp->src->pos, mp->dest->pos, mp->curdist / mp->dist);

        if ((motion->asset->ent_motion_flags & 0x1) && !xQuatEquals(&mp->aquat, &mp->bquat))
        {
            xQuatSlerp(&quat, &mp->aquat, &mp->bquat, MIN(1.0f, 6.0f * (mp->curdist / mp->dist)));
            xQuatToMat(&quat, &frame->mat);

            frame->mat.right *= scale.x;
            frame->mat.up *= scale.y;
            frame->mat.at *= scale.z;

            frame->mode |= 0x10;
        }
    }

    xVec3Sub(&frame->dpos, &tgt, &frame->mat.pos);
}

static void xEntSplineMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntSplineData* spline_data = &motion->spl;

    if (spline_data->arrived == 1)
    {
        return;
    }

    xEntSpeedDataUpdate(&spline_data->speed_data, dt);

    U8 still_on_spline = spline_data->spline->advance_u(
        spline_data->u, dt * spline_data->speed_data.speed, true, spline_data->u);

    if (!still_on_spline)
    {
        if (motion->asset->ent_motion_flags & 0x10)
        {
            spline_data->u = spline_data->spline->start();
        }
        else
        {
            spline_data->arrived = 1;
        }
    }

    xVec3 target_position;
    spline_data->spline->evaluate(spline_data->u, target_position);

    frame->mode |= 0x2;

    if (spline_data->arrived == 1)
    {
        frame->dpos = target_position - frame->mat.pos;
    }
    else
    {
        xVec3 new_position;
        xCam::spring_interp(new_position, spline_data->position_blend_v, frame->mat.pos,
                            target_position, dt, 10.0f);
        frame->dpos = new_position - frame->mat.pos;
    }

    if (motion->asset->ent_motion_flags & 0x1)
    {
        xVec3 scale;
        scale.x = frame->mat.right.length();
        scale.y = frame->mat.up.length();
        scale.z = frame->mat.at.length();

        xVec3 dir = { 0.0f, 1.0f, 0.0f };
        dir += spline_data->spline->evaluate(spline_data->u, 2) * motion->asset->spl.lean_modifier *
               spline_data->speed_data.speed;
        dir.safe_normalize(g_O3);

        xMat3x3 mat;
        mat.at = spline_data->spline->evaluate(spline_data->u, 1);
        mat.at.safe_normalize(frame->mat.at);

        if (spline_data->speed_data.speed < 0.0f)
        {
            mat.at = -mat.at;
        }

        mat.up = dir - mat.at * dir.dot(mat.at);
        mat.up.safe_normalize(frame->mat.up);
        mat.right = mat.up.cross(mat.at);

        xQuat target_orient;
        xQuatFromMat(&target_orient, &mat);
        xCam::spring_interp(spline_data->current_orient, spline_data->orient_blend_v,
                            spline_data->current_orient, target_orient, dt, 5.0f);

        frame->mode |= 0x40;
        xQuatToMat(&spline_data->current_orient, &frame->mat);

        frame->mat.right *= scale.x;
        frame->mat.up *= scale.y;
        frame->mat.at *= scale.z;
    }
}

static void xEntMechMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntMechData* mech = &motion->mech;
    xEntMotionMechData* mkasst = &motion->asset->mech;
    U32 last;

    frame->mode = 0;
    last = 0;

    if (mech->state == 8)
    {
        last = xEntScale(motion, sc, dt, frame);

        if (mkasst->type == 6)
        {
            xEntSldMove(motion, sc, dt, frame);
        }

        if (last)
        {
            if (mkasst->flags & 1)
            {
                mech->state = 2;
                mech->scale_speed = -mech->scale_speed;
                mech->sliding_speed = -mech->sliding_speed;
            }
            else if (mkasst->flags & 2)
            {
                mech->state = 6;
            }
            else
            {
                mech->state = 5;
            }
        }
    }
    else if (mech->state == 9)
    {
        last = xEntScale(motion, sc, dt, frame);

        if (mkasst->type == 6)
        {
            xEntSldMove(motion, sc, dt, frame);
        }

        if (last)
        {
            if (mkasst->flags & 2)
            {
                mech->state = 7;
            }
            else
            {
                mech->state = 5;
            }
        }
    }
    else if (mech->state == 0)
    {
        last = xEntSldMove(motion, sc, dt, frame);

        if (mkasst->type == 2)
        {
            xEntRotMove(motion, sc, dt, frame);
        }

        if (last)
        {
            if (mkasst->type == 3)
            {
                mech->state = 1;
            }
            else if (mkasst->flags & 1)
            {
                mech->state = 2;
                mech->sliding_speed = -mech->sliding_speed;
                mech->rotation_speed = -mech->rotation_speed;
            }
            else if (mkasst->flags & 2)
            {
                mech->state = 6;
            }
            else
            {
                mech->state = 5;
            }
        }
    }
    else if (mech->state == 1)
    {
        last = xEntRotMove(motion, sc, dt, frame);

        if (last)
        {
            if (mkasst->type == 4)
            {
                mech->state = 0;
            }
            else if (mkasst->flags & 1)
            {
                mech->state = 2;
                mech->sliding_speed = -mech->sliding_speed;
                mech->rotation_speed = -mech->rotation_speed;
            }
            else if (mkasst->flags & 2)
            {
                mech->state = 6;
            }
            else if (mkasst->post_ret_delay != 0.0f)
            {
                mech->state = 5;
            }
        }
    }
    else if (mech->state == 2)
    {
        if (motion->tmr >= mkasst->ret_delay)
        {
            if (mkasst->type == 0 || mkasst->type == 2 || mkasst->type == 4)
            {
                mech->state = 3;
            }
            else if (mkasst->type == 5 || mkasst->type == 6)
            {
                mech->state = 9;
            }
            else
            {
                mech->state = 4;
            }

            last = 1;
        }
    }
    else if (mech->state == 3)
    {
        last = xEntSldMove(motion, sc, dt, frame);

        if (mkasst->type == 2)
        {
            xEntRotMove(motion, sc, dt, frame);
        }

        if (last)
        {
            if (mkasst->type == 4)
            {
                mech->state = 4;
            }
            else if (mkasst->flags & 2)
            {
                mech->state = 7;
            }
            else
            {
                mech->state = 5;
            }
        }
    }
    else if (mech->state == 4)
    {
        last = xEntRotMove(motion, sc, dt, frame);

        if (last)
        {
            if (mkasst->type == 3)
            {
                mech->state = 3;
            }
            else if (mkasst->flags & 2)
            {
                mech->state = 7;
            }
            else
            {
                mech->state = 5;
            }
        }
    }
    else if (mech->state == 5 && motion->tmr >= mkasst->post_ret_delay)
    {
        xEntMotionReset(motion, sc);
        last = 1;
        xEntMotionRun(motion);
    }

    if (last)
    {
        motion->tmr = 0.0f;
    }
    else
    {
        motion->tmr += dt;
    }
}

static U32 xEntScale(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntMechData* mech = &motion->mech;
    xModelInstance* model = motion->owner->model;
    xEntMotionMechData* mkasst;
    U32 last;
    F32 scale;
    F32 u;
    F32 scale_adjust;

    if (model == NULL)
    {
        return 0;
    }

    mkasst = &motion->asset->mech;
    last = 0;
    u = (motion->tmr + dt) / mkasst->scale_duration;

    if (u > 1.0f)
    {
        u = 1.0f;
        last = 1;
    }

    if (mech->state == 8)
    {
        scale = mkasst->scale_amount * u + (1.0f - u);
    }
    else
    {
        scale = mkasst->scale_amount * (1.0f - u) + u;
    }

    scale_adjust = scale / mech->current_scale;
    mech->current_scale = scale;

    switch (mkasst->scale_axis)
    {
    case 0:
        xVec3SMulBy((xVec3*)&model->Mat->right, scale_adjust);
        xVec3SMulBy(&frame->mat.right, scale_adjust);
        break;
    case 1:
        xVec3SMulBy((xVec3*)&model->Mat->up, scale_adjust);
        xVec3SMulBy(&frame->mat.up, scale_adjust);
        break;
    case 2:
        xVec3SMulBy((xVec3*)&model->Mat->at, scale_adjust);
        xVec3SMulBy(&frame->mat.at, scale_adjust);
        break;
    case 3:
        xVec3SMulBy((xVec3*)&model->Mat->right, scale_adjust);
        xVec3SMulBy((xVec3*)&model->Mat->up, scale_adjust);
        xVec3SMulBy(&frame->mat.right, scale_adjust);
        xVec3SMulBy(&frame->mat.up, scale_adjust);
        break;
    case 4:
        xVec3SMulBy((xVec3*)&model->Mat->right, scale_adjust);
        xVec3SMulBy((xVec3*)&model->Mat->at, scale_adjust);
        xVec3SMulBy(&frame->mat.right, scale_adjust);
        xVec3SMulBy(&frame->mat.at, scale_adjust);
        break;
    case 5:
        xVec3SMulBy((xVec3*)&model->Mat->up, scale_adjust);
        xVec3SMulBy((xVec3*)&model->Mat->at, scale_adjust);
        xVec3SMulBy(&frame->mat.up, scale_adjust);
        xVec3SMulBy(&frame->mat.at, scale_adjust);
        break;
    case 6:
        xVec3SMulBy((xVec3*)&model->Mat->right, scale_adjust);
        xVec3SMulBy((xVec3*)&model->Mat->up, scale_adjust);
        xVec3SMulBy((xVec3*)&model->Mat->at, scale_adjust);
        xVec3SMulBy(&frame->mat.right, scale_adjust);
        xVec3SMulBy(&frame->mat.up, scale_adjust);
        xVec3SMulBy(&frame->mat.at, scale_adjust);
        break;
    }

    return last;
}

static U32 xEntSldMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntMechData* mech = &motion->mech;
    xEntMotionMechData* mkasst = &motion->asset->mech;
    U32 last;
    F32 tmradj = motion->tmr + dt * 0.5f;
    F32 rem = mkasst->sld_tm - tmradj;
    F32 speed;

    if (motion->tmr + dt >= mkasst->sld_tm)
    {
        if (mech->state == 0 || mech->state == 8 && mkasst->type == 6)
        {
            xVec3Sub(&frame->dpos, &mech->destination_position, &frame->mat.pos);
        }
        else
        {
            xVec3Sub(&frame->dpos, &mech->original_position, &frame->mat.pos);
        }

        last = 1;
    }
    else
    {
        if (mech->state == 0 || mech->state == 8 && mkasst->type == 6)
        {
            if (tmradj < mkasst->sld_acc_tm && mkasst->sld_acc_tm > 1e-5f)
            {
                speed = mech->sliding_speed * tmradj / mkasst->sld_acc_tm;
            }
            else if (tmradj > mech->sliding_forth_decel_time && mkasst->sld_dec_tm > 1e-5f)
            {
                speed = mech->sliding_speed * rem / mkasst->sld_dec_tm;
            }
            else
            {
                speed = mech->sliding_speed;
            }
        }
        else if (tmradj < mkasst->sld_dec_tm && mkasst->sld_dec_tm > 1e-5f)
        {
            speed = mech->sliding_speed * tmradj / mkasst->sld_dec_tm;
        }
        else if (tmradj > mech->sliding_back_decel_time && mkasst->sld_acc_tm > 1e-5f)
        {
            speed = mech->sliding_speed * rem / mkasst->sld_acc_tm;
        }
        else
        {
            speed = mech->sliding_speed;
        }

        switch (mkasst->sld_axis)
        {
        case 0:
            if (mkasst->scale_axis == 0 || mkasst->scale_axis == 3 || mkasst->scale_axis == 4 ||
                mkasst->scale_axis == 6)
            {
                if (motion->owner->asset->scale.x * mech->current_scale != 0.0f)
                {
                    speed /= motion->owner->asset->scale.x * mech->current_scale;
                }
            }
            else if (motion->owner->asset->scale.x != 0.0f)
            {
                speed /= motion->owner->asset->scale.x;
            }
            break;
        case 1:
            if (mkasst->scale_axis == 1 || mkasst->scale_axis == 3 || mkasst->scale_axis == 5 ||
                mkasst->scale_axis == 6)
            {
                if (motion->owner->asset->scale.y * mech->current_scale != 0.0f)
                {
                    speed /= motion->owner->asset->scale.y * mech->current_scale;
                }
            }
            else if (motion->owner->asset->scale.y != 0.0f)
            {
                speed /= motion->owner->asset->scale.y;
            }
            break;
        case 2:
            if (mkasst->scale_axis == 2 || mkasst->scale_axis == 4 || mkasst->scale_axis == 5 ||
                mkasst->scale_axis == 6)
            {
                if (motion->owner->asset->scale.z * mech->current_scale != 0.0f)
                {
                    speed /= motion->owner->asset->scale.z * mech->current_scale;
                }
            }
            else if (motion->owner->asset->scale.z != 0.0f)
            {
                speed /= motion->owner->asset->scale.z;
            }
            break;
        }

        xVec3SMul(&frame->dpos, &mech->direction, speed * dt);

        last = 0;
    }

    frame->mode |= 2;

    return last;
}

static U32 xEntRotMove(xEntMotion* motion, xScene* sc, F32 dt, xEntFrame* frame)
{
    xEntMechData* mech = &motion->mech;
    xEntMotionMechData* mkasst = &motion->asset->mech;
    U32 last;
    F32 tmradj = motion->tmr + dt * 0.5f;
    F32 rem = mkasst->rot_tm - tmradj;
    F32 speed;
    xEnt* ownr;
    xEntAsset* easst;

    if (motion->tmr + dt >= mkasst->rot_tm)
    {
        if (mech->state == 1 || mkasst->type == 2 && mech->state == 0)
        {
            *mech->entity_rotation_angle = mech->destination_rotation;
        }
        else
        {
            *mech->entity_rotation_angle = mech->original_rotation;
        }

        last = 1;
    }
    else
    {
        if (mech->state == 1 || mkasst->type == 2 && mech->state == 0)
        {
            if (tmradj < mkasst->rot_acc_tm && mkasst->rot_acc_tm > 1e-5f)
            {
                speed = mech->rotation_speed * tmradj / mkasst->rot_acc_tm;
            }
            else if (tmradj > mech->rotation_forth_decel_time && mkasst->rot_dec_tm > 1e-5f)
            {
                speed = mech->rotation_speed * rem / mkasst->rot_dec_tm;
            }
            else
            {
                speed = mech->rotation_speed;
            }
        }
        else if (tmradj < mkasst->rot_dec_tm && mkasst->rot_dec_tm > 1e-5f)
        {
            speed = mech->rotation_speed * tmradj / mkasst->rot_dec_tm;
        }
        else if (tmradj > mech->rotating_back_decel_time && mkasst->rot_acc_tm > 1e-5f)
        {
            speed = mech->rotation_speed * rem / mkasst->rot_acc_tm;
        }
        else
        {
            speed = mech->rotation_speed;
        }

        *mech->entity_rotation_angle = xAngleClamp(speed * dt + *mech->entity_rotation_angle);

        last = 0;
    }

    xMat3x3Euler(&frame->mat, frame->rot.axis.x, frame->rot.axis.y, frame->rot.axis.z);

    ownr = motion->owner;
    if (ownr != NULL)
    {
        easst = ownr->asset;
        xVec3SMulBy(&frame->mat.right, easst->scale.x);
        xVec3SMulBy(&frame->mat.up, easst->scale.y);
        xVec3SMulBy(&frame->mat.at, easst->scale.z);
    }

    frame->mode |= 0x10;

    return last;
}

void xEntMechForward(xEntMotion* motion)
{
    xEntMechData* mech = &motion->mech;
    xEntMotionMechData* mkasst = &motion->asset->mech;

    xEntMotionRun(motion);

    if (mech->state != 0 && mech->state != 1 && mech->state != 2)
    {
        if (mech->state == 3)
        {
            mech->sliding_speed = -mech->sliding_speed;
            mech->rotation_speed = -mech->rotation_speed;
            motion->tmr = mkasst->sld_tm - motion->tmr;
            mech->state = 0;
        }
        else if (mech->state == 4)
        {
            mech->sliding_speed = -mech->sliding_speed;
            mech->rotation_speed = -mech->rotation_speed;
            motion->tmr = mkasst->rot_tm - motion->tmr;
            mech->state = 1;
        }
        else if (mech->state != 5 && mech->state != 6 && mech->state == 7)
        {
            mech->sliding_speed = -mech->sliding_speed;
            mech->rotation_speed = -mech->rotation_speed;
            motion->tmr = 0.0f;

            if (mkasst->type == 0 || mkasst->type == 2 || mkasst->type == 4)
            {
                mech->state = 0;
            }
            else
            {
                mech->state = 1;
            }
        }
    }
}

void xEntMechReverse(xEntMotion* motion)
{
    xEntMechData* mech = &motion->mech;
    xEntMotionMechData* mkasst = &motion->asset->mech;

    xEntMotionRun(motion);

    if (mech->state == 0)
    {
        mech->sliding_speed = -mech->sliding_speed;
        mech->rotation_speed = -mech->rotation_speed;
        motion->tmr = mkasst->sld_tm - motion->tmr;
        mech->state = 3;
    }
    else if (mech->state == 1)
    {
        mech->sliding_speed = -mech->sliding_speed;
        mech->rotation_speed = -mech->rotation_speed;
        motion->tmr = mkasst->rot_tm - motion->tmr;
        mech->state = 4;
    }
    else if (mech->state == 8)
    {
        mech->scale_speed = -mech->scale_speed;
        mech->sliding_speed = -mech->sliding_speed;
        motion->tmr = mkasst->scale_duration - motion->tmr;
        mech->state = 9;
    }
    else if (mech->state != 2 && mech->state != 3 && mech->state != 4 && mech->state != 9 &&
             mech->state != 5 && mech->state == 6)
    {
        mech->sliding_speed = -mech->sliding_speed;
        mech->rotation_speed = -mech->rotation_speed;
        motion->tmr = 0.0f;

        if (mkasst->type == 0 || mkasst->type == 2 || mkasst->type == 4)
        {
            mech->state = 3;
        }
        else if (mkasst->type == 5 || mkasst->type == 6)
        {
            mech->state = 9;
        }
        else
        {
            mech->state = 4;
        }
    }
}

void xEntMotionTranslate(xEntMotion* motion, const xVec3* dpos, xMat4x3* dmat)
{
    if (dmat != NULL)
    {
        if (motion->type == XENTMOTION_TYPE_ER)
        {
            xMat4x3Toworld(&motion->er.a, dmat, &motion->er.a);
            xMat4x3Toworld(&motion->er.b, dmat, &motion->er.b);
        }
        else if (motion->type == XENTMOTION_TYPE_ORB)
        {
            xMat4x3Toworld(&motion->orb.orig, dmat, &motion->orb.orig);
            xMat4x3Toworld(&motion->orb.c, dmat, &motion->orb.c);
        }
        else if (motion->type == XENTMOTION_TYPE_MP)
        {
            return;
        }
        else if (motion->type == XENTMOTION_TYPE_SPL)
        {
            return;
        }
        else if (motion->type == XENTMOTION_TYPE_PEN)
        {
            xMat4x3Toworld(&motion->pen.top, dmat, &motion->pen.top);
            xMat4x3Mul(&motion->pen.omat, &motion->pen.omat, dmat);
        }
        else if (motion->type == XENTMOTION_TYPE_MECH)
        {
            xMat4x3Toworld(&motion->mech.original_position, dmat, &motion->mech.original_position);
            xMat4x3Toworld(&motion->mech.destination_position, dmat,
                           &motion->mech.destination_position);
            xMat3x3RMulVec(&motion->mech.direction, dmat, &motion->mech.direction);
        }
    }
    else
    {
        if (motion->type == XENTMOTION_TYPE_ER)
        {
            xVec3AddTo(&motion->er.a, dpos);
            xVec3AddTo(&motion->er.b, dpos);
        }
        else if (motion->type == XENTMOTION_TYPE_ORB)
        {
            xVec3AddTo(&motion->orb.orig, dpos);
            xVec3AddTo(&motion->orb.c, dpos);
        }
        else if (motion->type == XENTMOTION_TYPE_MP)
        {
            return;
        }
        else if (motion->type == XENTMOTION_TYPE_SPL)
        {
            return;
        }
        else if (motion->type == XENTMOTION_TYPE_PEN)
        {
            xVec3AddTo(&motion->pen.top, dpos);
            xVec3AddTo(&motion->pen.omat.pos, dpos);
        }
        else if (motion->type == XENTMOTION_TYPE_MECH)
        {
            xVec3AddTo(&motion->mech.original_position, dpos);
            xVec3AddTo(&motion->mech.destination_position, dpos);
        }
    }
}
