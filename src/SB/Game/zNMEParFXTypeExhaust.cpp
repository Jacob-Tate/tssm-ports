#include "zNMEParFX.h"
#include "xMath.h"
#include "xVec3Inlines.h"
#include "iMath.h"
#include "xMath2.h"
#include "xPtankPool.h"
#include "xEnt.h"
#include "xGlobals.h"
#include "zGlobals.h"
#include "zEntPlayer.h"

#include <stdio.h>
#include "xRand.h"

// Size: 0x40 (PS2 DWARF)
struct ExhaustParticle
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    const ExhaustEmitter* emitter;
    F32 tym_lifespan;
    F32 tmr_remain;
    F32 fac_abuse;
    F32 rot;
    xVec3 acc_emitaxis;
    S8 idx_curvNode;
    U8 uv_numCell;
    U8 uv_cell;
    S8 unused;
};

void ExhaustEmitter::EmitOnAxis(const xVec3* pos_center, const xVec3* dir_axis,
                                F32 pct_spdRamp) const
{
    EmitRing(pos_center, dir_axis, -100000.0f, 1.0f, -1, NULL);
}

void ExhaustEmitter::EmitRing(const xVec3* pos_center, const xVec3* dir_axis, F32 rad_emitCaller,
                              F32 pct_spdRamp, S32 num_emitCaller, xVec3* vel_drift) const
{
    S32 pbs_id;
    S32 num_desire;
    S32 cnt_remain;
    S32 num_emitted;
    xVec3 dir_perp;
    xVec3 dir_orth;
    F32 darc;
    U8* mem;
    S32 num_par;
    ExhaustParticle* par;
    S32 i;
    F32 samecalc;
    F32 ang_curr;
    F32 rad_emit;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    num_desire = (num_emitCaller > 0) ? num_emitCaller :
                                        emit.num_emitBase + (S32)(emit.num_emitRand * xurand());
    if (num_desire == 0)
    {
        return;
    }

    cnt_remain = num_desire;
    num_emitted = 0;

    dir_perp.x = dir_axis->y - dir_axis->z;
    dir_perp.y = dir_axis->z - dir_axis->x;
    dir_perp.z = dir_axis->x - dir_axis->y;
    xVec3Normalize(&dir_perp, &dir_perp);

    xVec3Cross(&dir_orth, &dir_perp, dir_axis);

    darc = (2.0f * PI) / num_desire;

    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        par = (ExhaustParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            par->rot = 0.0f;
            par->emitter = this;
            F32 fac_rand = xurand();
            samecalc = life.tym_lifeRand * fac_rand + life.tym_lifeBase;
            par->fac_abuse = fac_rand;
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->idx_curvNode = -1;

            ang_curr = -PI + darc * (num_emitted + xurand());
            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);
            xVec3 dir_outward = dir_perp * ang_sin + dir_orth * ang_cos;

            if (rad_emitCaller > -10000.0f)
            {
                rad_emit = rad_emitCaller;
            }
            else
            {
                rad_emit = fac_rand * emit.rad_emitRand + emit.rad_emitBase;
            }

            par->pos = *pos_center + dir_outward * rad_emit;

            par->vel = *dir_axis * (speed.spd_emitRand * fac_rand + speed.spd_emitBase);
            par->vel += dir_outward * (speed.spd_outRand * fac_rand + speed.spd_outBase);
            par->vel *= pct_spdRamp;
            if (vel_drift != NULL)
            {
                par->vel += *vel_drift;
            }

            par->acc_emitaxis = *dir_axis * speed.acc_emitaxis;
            par->acc_emitaxis += dir_outward * speed.acc_outward;

            switch (useNPARtype)
            {
            case NPAR_TYP_COLORS:
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_rand;
                break;
            case NPAR_TYP_SEASPARKLE:
            case NPAR_TYP_SEASPRAY:
            case NPAR_TYP_EXHAUST:
            case NPAR_TYP_NOTES_UPGRADE:
            case NPAR_TYP_NOTES:
            case NPAR_TYP_HEATED:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_rand;
                break;
            case NPAR_TYP_SEAFOG_NOROT:
            case NPAR_TYP_SEAFOG:
            case NPAR_TYP_SEASPLASH:
            case NPAR_TYP_JETFLAME:
            case NPAR_TYP_JETSMOKE:
            case NPAR_TYP_CLOUDS:
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par++;
            num_emitted++;
        }

        cnt_remain -= num_par;
    }
}

void ExhaustEmitter::EmitLineUp(const xVec3* pos_beg, const xVec3* pos_end, xVec3* vel_drift,
                                F32 pct_spdRamp, S32 num_emitCaller, F32 rad_emitCaller) const
{
    S32 pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    S32 num_desire = (num_emitCaller > 0) ? num_emitCaller :
                                            emit.num_emitBase + (S32)(emit.num_emitRand * xurand());

    S32 cnt_remain = num_desire;
    S32 num_emitted = 0;

    xVec3 dir_axis = g_Y3;
    xVec3 dir_perp = g_X3;
    xVec3 dir_orth = g_Z3;

    xVec3 dir_seg = *pos_end - *pos_beg;
    F32 dseg = dir_seg.length();
    if (dseg > 0.00001f)
    {
        dir_seg /= dseg;
    }
    else
    {
        dir_seg = g_O3;
    }

    dseg = dseg / num_desire;
    F32 darc = (2.0f * PI) / num_desire;

    U8* mem;
    S32 num_par;
    ExhaustParticle* par;
    S32 i;
    F32 samecalc;
    F32 ang_curr;
    F32 rad_emit;

    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        par = (ExhaustParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            par->emitter = this;
            F32 fac_rand = xurand();
            samecalc = life.tym_lifeRand * fac_rand + life.tym_lifeBase;
            par->fac_abuse = fac_rand;
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->idx_curvNode = -1;

            ang_curr = -PI + darc * (num_emitted + xurand());
            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);
            xVec3 dir_outward = dir_perp * ang_sin + dir_orth * ang_cos;

            if (rad_emitCaller > -10000.0f)
            {
                rad_emit = rad_emitCaller;
            }
            else
            {
                rad_emit = fac_rand * emit.rad_emitRand + emit.rad_emitBase;
            }

            par->rot = 0.0f;
            par->pos = *pos_beg;
            par->pos += dir_seg * (dseg * (num_emitted + xurand()));
            par->pos += dir_outward * rad_emit;

            par->vel = dir_axis * (speed.spd_emitRand * fac_rand + speed.spd_emitBase);
            par->vel += dir_outward * (speed.spd_outRand * fac_rand + speed.spd_outBase);
            par->vel *= pct_spdRamp;
            if (vel_drift != NULL)
            {
                par->vel += *vel_drift;
            }

            par->acc_emitaxis = dir_axis * speed.acc_emitaxis;
            par->acc_emitaxis += dir_outward * speed.acc_outward;

            switch (useNPARtype)
            {
            case NPAR_TYP_COLORS:
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_rand;
                break;
            case NPAR_TYP_SEASPARKLE:
            case NPAR_TYP_SEASPRAY:
            case NPAR_TYP_EXHAUST:
            case NPAR_TYP_NOTES_UPGRADE:
            case NPAR_TYP_NOTES:
            case NPAR_TYP_HEATED:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_rand;
                break;
            case NPAR_TYP_SEAFOG_NOROT:
            case NPAR_TYP_SEAFOG:
            case NPAR_TYP_SEASPLASH:
            case NPAR_TYP_JETFLAME:
            case NPAR_TYP_JETSMOKE:
            case NPAR_TYP_CLOUDS:
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par++;
            num_emitted++;
        }

        cnt_remain -= num_par;
    }
}

void Exhaust::ExhaustSysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "Exhaust");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
}

void Exhaust::ExhaustSysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(ExhaustParticle);
    pbs->update = ExhaustSysUpdate;
    pbs->context = mgmt;
}

void Exhaust::ExhaustSysInit_noRot_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(ExhaustParticle);
    pbs->update = ExhaustSysUpdate_noRot;
    pbs->context = mgmt;
}

void Exhaust::ExhaustSysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(ExhaustParticle);
    pbs->update = ExhaustSysUpdate;
    pbs->context = mgmt;
}

void Exhaust::ExhaustSysInit_Glow(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 2;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 8;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(ExhaustParticle);
    pbs->update = ExhaustSysUpdate;
    pbs->context = mgmt;
}

// Always inlined (no symbol; the name is ours). The Xbox build keeps it out of line as an emitter
// member called from EvalCurve.
inline S32 ExhaustEmitter::FindCurveNodes(F32 rat, ExhaustCurve*& node_lo,
                                          ExhaustCurve*& node_hi) const
{
    S32 idx;
    S32 i;

    if (curve.curveSize < 2)
    {
        idx = 0;
        node_lo = curve.curveData;
        node_hi = curve.curveData;
    }
    else if (rat < 0.0f)
    {
        idx = 0;
        node_lo = &curve.curveData[0];
        node_hi = &curve.curveData[1];
    }
    else if (rat > 1.0f)
    {
        idx = curve.curveSize - 1;
        node_lo = &curve.curveData[idx - 1];
        node_hi = &curve.curveData[idx];
    }
    else
    {
        S32 num = curve.curveSize;

        idx = -1;
        for (i = 0; i < num - 1; i++)
        {
            node_lo = &curve.curveData[i];
            node_hi = &curve.curveData[i + 1];
            if (!(node_hi->time <= rat))
            {
                idx = i;
                break;
            }
        }
    }

    return idx;
}

// Weak and out of line in retail (inlined on PS2).
inline S32 ExhaustEmitter::EvalCurve(F32 rat, ExhaustCurve* node_eval, S32 linearize,
                                     S32 idx_prev) const
{
    ExhaustCurve* node_lo;
    ExhaustCurve* node_hi;
    F32 fac;
    F32 fac_saddle;
    S32 idx;
    F32 tym_span;
    F32 tym_rel;

    rat = CLAMP(rat, 0.0f, 1.0f);

    if (idx_prev < 0)
    {
        idx = FindCurveNodes(rat, node_lo, node_hi);
    }
    else
    {
        idx = idx_prev;
        node_lo = &curve.curveData[idx];
        if (idx >= curve.curveSize - 1)
        {
            node_hi = &curve.curveData[idx];
        }
        else
        {
            node_hi = &curve.curveData[idx + 1];
        }
        if (rat < node_lo->time || rat > node_hi->time)
        {
            idx = FindCurveNodes(rat, node_lo, node_hi);
        }
    }

    tym_span = node_hi->time - node_lo->time;
    tym_rel = rat - node_lo->time;
    fac = 0.0f;
    if (tym_span > 0.00001f)
    {
        fac = tym_rel / tym_span;
    }

    if (linearize)
    {
        fac_saddle = fac;
    }
    else
    {
        fac_saddle = fac * (3.0f * fac - 2.0f * fac * fac);
    }

    node_eval->scale = fac_saddle * (node_hi->scale - node_lo->scale) + node_lo->scale;
    node_eval->color.r =
        node_lo->color.r + (U8)(fac_saddle * (node_hi->color.r - node_lo->color.r));
    node_eval->color.g =
        node_lo->color.g + (U8)(fac_saddle * (node_hi->color.g - node_lo->color.g));
    node_eval->color.b =
        node_lo->color.b + (U8)(fac_saddle * (node_hi->color.b - node_lo->color.b));
    node_eval->color.a =
        node_lo->color.a + (U8)(fac_saddle * (node_hi->color.a - node_lo->color.a));

    return idx;
}

S32 Exhaust::ExhaustSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__pos_color_size_rot_uv2* pool = (ptank_pool__pos_color_size_rot_uv2*)&apool;
    ExhaustParticle* par = (ExhaustParticle*)mem;
    ExhaustParticle* end = par + count;
    xVec3 pos_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);
    const ExhaustEmitter* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    ExhaustCurve node_eval;
    S32 idx;
    S32 linearize;
    S32 systemUsesHDR;
    xColor_tag par_color;
    F32 adjAlpha;
    U8 alftest;
    F32 sum;
    const ExhaustEmitter::Damage* dmg;
    F32 ds2_plyr;
    F32 scaleSize;

    while (par != end)
    {
        emitter = par->emitter;
        par->tmr_remain -= dt;
        if (emitter == NULL || par->tmr_remain < 0.0f)
        {
            end--;
            *par = *end;
            continue;
        }

        tym_life = MAX(0.0f, par->tmr_remain);
        rat = tym_life / par->tym_lifespan;
        rat_rev = 1.0f - rat;

        idx = par->idx_curvNode;
        linearize = emitter->flags.useLinear;
        idx = emitter->EvalCurve(rat_rev, &node_eval, linearize, idx);
        par->idx_curvNode = idx;

        par->pos += par->vel * dt;

        if (emitter->run.fac_damp != 0.0f)
        {
            par->vel -= par->vel * (emitter->run.fac_damp * dt);
        }
        par->vel.y -= emitter->run.acc_gravity * dt;
        par->vel += par->acc_emitaxis * dt;
        par->vel += emitter->speed.acc_world * dt;

        par->size = MAX(emitter->size.height, emitter->size.width);
        par->size *= node_eval.scale * emitter->curve.scl_master;

        RwSphere worldsph;
        worldsph.center.x = par->pos.x;
        worldsph.center.y = par->pos.y;
        worldsph.center.z = par->pos.z;
        worldsph.radius = par->size;

        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
        {
            systemUsesHDR = 0;
            switch (emitter->useNPARtype)
            {
            case NPAR_TYP_NOTES:
            case NPAR_TYP_NOTES_UPGRADE:
                systemUsesHDR = 1;
                break;
            }

            par->rot += dt * (emitter->run.spd_rotate * (2.0f * (par->fac_abuse - 0.5f)));
            par->rot = xDangleClamp(par->rot);

            par_color = node_eval.color;
            adjAlpha = emitter->curve.alf_master * par_color.a;
            par_color.a = CLAMP(adjAlpha, 0.0f, 255.0f);
            alftest = par_color.a;
            if (systemUsesHDR)
            {
                sum = (F32)par_color.r + par_color.g + par_color.b;
                sum /= 3.0f;
                alftest = CLAMP(sum, 0.0f, 255.0f);
            }

            dmg = &emitter->dmg;
            if (emitter->flags.doDamage && !(globals.player.DamageTimer > 0.0f) &&
                globals.player.s->Health && rat_rev > dmg->pam_collide[0] &&
                rat_rev < dmg->pam_collide[1] && alftest > dmg->alf_collide)
            {
                ds2_plyr = NMEC_DstSq(&par->pos, &pos_plyr, NULL);
                if (ds2_plyr < dmg->rad_collide * dmg->rad_collide)
                {
                    zEntPlayer_DamageNPCKnockBack(NULL, 1, &par->pos,
                                                  ePlayerNMEDamageEnemyParticle);
                }

                static S32 howfreq = 0;
            }

            pool->next();
            if (!pool->valid())
            {
                end = par;
                break;
            }

            *pool->pos = par->pos;
            *pool->color = par_color;
            *pool->rot = par->rot;

            scaleSize = node_eval.scale * emitter->curve.scl_master;
            pool->size->x = emitter->size.width * scaleSize;
            pool->size->y = emitter->size.height * scaleSize;

            if (par->uv_numCell == 4)
            {
                pool->uv[0] = uv_cell_2x2[par->uv_cell][0];
                pool->uv[1] = uv_cell_2x2[par->uv_cell][1];
            }
            else if (par->uv_numCell == 9)
            {
                pool->uv[0] = uv_cell_3x3[par->uv_cell][0];
                pool->uv[1] = uv_cell_3x3[par->uv_cell][1];
            }
            else if (par->uv_numCell == 16)
            {
                pool->uv[0] = uv_cell_4x4[par->uv_cell][0];
                pool->uv[1] = uv_cell_4x4[par->uv_cell][1];
            }
            else if (par->uv_numCell == 1)
            {
                pool->uv[0].x = 0.0f;
                pool->uv[0].y = 0.0f;
                pool->uv[1].x = 1.0f;
                pool->uv[1].y = 1.0f;
            }
            else
            {
                pool->uv[0].x = 0.0f;
                pool->uv[0].y = 0.0f;
                pool->uv[1].x = 1.0f;
                pool->uv[1].y = 1.0f;
            }
        }

        par++;
    }

    return end - (ExhaustParticle*)mem;
}

S32 Exhaust::ExhaustSysUpdate_noRot(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__pos_color_size_uv2* pool = (ptank_pool__pos_color_size_uv2*)&apool;
    ExhaustParticle* par = (ExhaustParticle*)mem;
    ExhaustParticle* end = par + count;
    xVec3 pos_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);
    const ExhaustEmitter* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    ExhaustCurve node_eval;
    S32 idx;
    S32 linearize;
    S32 systemUsesHDR;
    xColor_tag par_color;
    F32 adjAlpha;
    U8 alftest;
    F32 sum;
    const ExhaustEmitter::Damage* dmg;
    F32 ds2_plyr;
    F32 scaleSize;

    while (par != end)
    {
        emitter = par->emitter;
        par->tmr_remain -= dt;
        if (emitter == NULL || par->tmr_remain < 0.0f)
        {
            end--;
            *par = *end;
            continue;
        }

        tym_life = MAX(0.0f, par->tmr_remain);
        rat = tym_life / par->tym_lifespan;
        rat_rev = 1.0f - rat;

        idx = par->idx_curvNode;
        linearize = emitter->flags.useLinear;
        idx = emitter->EvalCurve(rat_rev, &node_eval, linearize, idx);
        par->idx_curvNode = idx;

        par->pos += par->vel * dt;

        if (emitter->run.fac_damp != 0.0f)
        {
            par->vel -= par->vel * (emitter->run.fac_damp * dt);
        }
        par->vel.y -= emitter->run.acc_gravity * dt;
        par->vel += par->acc_emitaxis * dt;
        par->vel += emitter->speed.acc_world * dt;

        par->size = MAX(emitter->size.height, emitter->size.width);
        par->size *= node_eval.scale * emitter->curve.scl_master;

        RwSphere worldsph;
        worldsph.center.x = par->pos.x;
        worldsph.center.y = par->pos.y;
        worldsph.center.z = par->pos.z;
        worldsph.radius = par->size;

        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
        {
            systemUsesHDR = 0;
            switch (emitter->useNPARtype)
            {
            case NPAR_TYP_NOTES:
            case NPAR_TYP_NOTES_UPGRADE:
                systemUsesHDR = 1;
                break;
            }

            par_color = node_eval.color;
            adjAlpha = emitter->curve.alf_master * par_color.a;
            par_color.a = CLAMP(adjAlpha, 0.0f, 255.0f);
            alftest = par_color.a;
            if (systemUsesHDR)
            {
                sum = (F32)par_color.r + par_color.g + par_color.b;
                sum /= 3.0f;
                alftest = CLAMP(sum, 0.0f, 255.0f);
            }

            dmg = &emitter->dmg;
            if (emitter->flags.doDamage && !(globals.player.DamageTimer > 0.0f) &&
                globals.player.s->Health && rat_rev > dmg->pam_collide[0] &&
                rat_rev < dmg->pam_collide[1] && alftest > dmg->alf_collide)
            {
                ds2_plyr = NMEC_DstSq(&par->pos, &pos_plyr, NULL);
                if (ds2_plyr < dmg->rad_collide * dmg->rad_collide)
                {
                    zEntPlayer_DamageNPCKnockBack(NULL, 1, &par->pos,
                                                  ePlayerNMEDamageEnemyParticle);
                }

                static S32 howfreq = 0;
            }

            pool->next();
            if (!pool->valid())
            {
                end = par;
                break;
            }

            *pool->pos = par->pos;
            *pool->color = par_color;

            scaleSize = node_eval.scale * emitter->curve.scl_master;
            pool->size->x = emitter->size.width * scaleSize;
            pool->size->y = emitter->size.height * scaleSize;

            if (par->uv_numCell == 4)
            {
                pool->uv[0] = uv_cell_2x2[par->uv_cell][0];
                pool->uv[1] = uv_cell_2x2[par->uv_cell][1];
            }
            else if (par->uv_numCell == 9)
            {
                pool->uv[0] = uv_cell_3x3[par->uv_cell][0];
                pool->uv[1] = uv_cell_3x3[par->uv_cell][1];
            }
            else if (par->uv_numCell == 16)
            {
                pool->uv[0] = uv_cell_4x4[par->uv_cell][0];
                pool->uv[1] = uv_cell_4x4[par->uv_cell][1];
            }
            else if (par->uv_numCell == 1)
            {
                pool->uv[0].x = 0.0f;
                pool->uv[0].y = 0.0f;
                pool->uv[1].x = 1.0f;
                pool->uv[1].y = 1.0f;
            }
            else
            {
                pool->uv[0].x = 0.0f;
                pool->uv[0].y = 0.0f;
                pool->uv[1].x = 1.0f;
                pool->uv[1].y = 1.0f;
            }
        }

        par++;
    }

    return end - (ExhaustParticle*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void Exhaust::ExhaustSysRegister()
{
    static NPARInfo nparsys_clouds = { ExhaustSysInit_Dull, ExhaustSysTweaks, "particle_cloud", 2 };
    static NPARInfo nparsys_exhaust = { ExhaustSysInit_Dull, ExhaustSysTweaks, "fx_gen_dust", 2 };
    static NPARInfo nparsys_heated = { ExhaustSysInit_Hot, ExhaustSysTweaks, "fx_gen_dust", 2 };
    static NPARInfo nparsys_jetsmoke = { ExhaustSysInit_Dull, ExhaustSysTweaks, "particle_cloud",
                                         2 };
    static NPARInfo nparsys_jetflame = { ExhaustSysInit_Hot, ExhaustSysTweaks, "LensFlare01", 2 };
    static NPARInfo nparsys_notes = { ExhaustSysInit_Glow, ExhaustSysTweaks,
                                      "cruise_guitar_notes01", 2 };
    static NPARInfo nparsys_notesUpgrade = { ExhaustSysInit_Glow, ExhaustSysTweaks,
                                             "cruise_guitar_notes02", 2 };
    static NPARInfo nparsys_seaFog = { ExhaustSysInit_Dull, ExhaustSysTweaks, "particle_cloud", 2 };
    static NPARInfo nparsys_seaFog_noRot = { ExhaustSysInit_noRot_Dull, ExhaustSysTweaks,
                                             "particle_cloud", 2 };
    static NPARInfo nparsys_seaSpray = { ExhaustSysInit_Hot, ExhaustSysTweaks, "fx_spray", 2 };
    static NPARInfo nparsys_seaSplash = { ExhaustSysInit_Hot, ExhaustSysTweaks, "splash_particle",
                                          2 };
    static NPARInfo nparsys_seaSparkle = { ExhaustSysInit_Hot, ExhaustSysTweaks, "fx_sparky", 2 };

    g_npar_info[NPAR_TYP_CLOUDS] = &nparsys_clouds;
    g_npar_info[NPAR_TYP_EXHAUST] = &nparsys_exhaust;
    g_npar_info[NPAR_TYP_JETSMOKE] = &nparsys_jetsmoke;
    g_npar_info[NPAR_TYP_SEAFOG] = &nparsys_seaFog;
    g_npar_info[NPAR_TYP_SEAFOG_NOROT] = &nparsys_seaFog_noRot;
    g_npar_info[NPAR_TYP_HEATED] = &nparsys_heated;
    g_npar_info[NPAR_TYP_JETFLAME] = &nparsys_jetflame;
    g_npar_info[NPAR_TYP_SEASPRAY] = &nparsys_seaSpray;
    g_npar_info[NPAR_TYP_SEASPLASH] = &nparsys_seaSplash;
    g_npar_info[NPAR_TYP_SEASPARKLE] = &nparsys_seaSparkle;
    g_npar_info[NPAR_TYP_NOTES] = &nparsys_notes;
    g_npar_info[NPAR_TYP_NOTES_UPGRADE] = &nparsys_notesUpgrade;
}
