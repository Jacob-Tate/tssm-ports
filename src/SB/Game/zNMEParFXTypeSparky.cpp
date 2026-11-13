#include "zNMEParFX.h"
#include "xMath.h"
#include "xVec3Inlines.h"
#include "iMath.h"

#include <stdio.h>
#include "xRand.h"

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

// Size: 0x40 (PS2 DWARF)
struct SparkyParticle
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    const SparkyEmitter* emitter;
    F32 tym_lifespan;
    F32 tmr_remain;
    F32 fac_abuse;
    S8 idx_curvNode;
    U8 uv_numCell;
    U8 uv_cell;
    S8 unused;
    xVec3 acc_emitaxis;
    S32 useme;
};

void SparkyEmitter::EmitOnAxis(const xVec3* pos_center, const xVec3* dir_axis,
                               F32 fac_spdRamp) const
{
    EmitRing(pos_center, dir_axis, -100000.0f, 1.0f, -1);
}

void SparkyEmitter::EmitRing(const xVec3* pos_center, const xVec3* dir_axis, F32 rad_emitCaller,
                             F32 fac_spdRamp, S32 num_emitCaller) const
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
    SparkyParticle* par;
    S32 i;
    F32 samecalc;
    F32 ang_curr;
    F32 rad_emit;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    num_desire = (num_emitCaller > 0) ? num_emitCaller :
                                        emit.num_emitBase + (S32)(emit.num_emitRand * xurand());

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

        par = (SparkyParticle*)mem;
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

            par->pos = *pos_center + dir_outward * rad_emit;

            par->vel = *dir_axis * (speed.spd_emitRand * fac_rand + speed.spd_emitBase);
            par->vel += dir_outward * (speed.spd_outRand * fac_rand + speed.spd_outBase);
            par->vel *= fac_spdRamp;

            par->acc_emitaxis = *dir_axis * speed.acc_emitaxis;
            par->acc_emitaxis += dir_outward * speed.acc_outward;

            switch (useNPARtype)
            {
            case NPAR_TYP_SPARKS:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            case NPAR_TYP_SEAMIST:
            case NPAR_TYP_RAIN:
            case NPAR_TYP_SPARKY:
            case NPAR_TYP_FOUNTAIN:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_rand;
                break;
            case NPAR_TYP_COLORS:
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_rand;
                break;
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

void SparkyEmitter::EmitInDir(const xVec3* pos, const xVec3* vel, S32 num_emitCaller) const
{
    S32 pbs_id;
    S32 num_desire;
    S32 cnt_remain;
    U8* mem;
    S32 num_par;
    SparkyParticle* par;
    S32 i;
    F32 samecalc;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    num_desire = (num_emitCaller > 0) ? num_emitCaller :
                                        emit.num_emitBase + (S32)(emit.num_emitRand * xurand());

    cnt_remain = num_desire;
    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        par = (SparkyParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            par->emitter = this;
            F32 fac_rand = xurand();
            samecalc = life.tym_lifeRand * fac_rand + life.tym_lifeBase;
            par->fac_abuse = fac_rand;
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->idx_curvNode = -1;

            par->pos = *pos;
            par->vel = *vel;

            switch (useNPARtype)
            {
            case NPAR_TYP_SPARKS:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            case NPAR_TYP_SEAMIST:
            case NPAR_TYP_RAIN:
            case NPAR_TYP_SPARKY:
            case NPAR_TYP_FOUNTAIN:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_rand;
                break;
            case NPAR_TYP_COLORS:
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_rand;
                break;
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par++;
        }

        cnt_remain -= num_par;
    }
}

void SparkyEmitter::DenSeaItRain(const xVec3* pos_beg, const xVec3* pos_end, const xVec3* vel_drift,
                                 F32 rad_emitCaller, F32 fac_spdRamp, S32 num_emitCaller) const
{
    S32 pbs_id;
    S32 num_desire;
    S32 cnt_remain;
    S32 num_emitted;
    xVec3 dir_axis;
    xVec3 dir_perp;
    xVec3 dir_orth;
    F32 dseg;
    F32 darc;
    U8* mem;
    S32 num_par;
    SparkyParticle* par;
    S32 i;
    F32 samecalc;
    F32 ang_curr;
    F32 rad_emit;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    num_desire = (num_emitCaller > 0) ? num_emitCaller :
                                        emit.num_emitBase + (S32)(emit.num_emitRand * xurand());

    cnt_remain = num_desire;
    num_emitted = 0;

    dir_axis = g_Y3;
    dir_perp = g_X3;
    dir_orth = g_Z3;

    xVec3 dir_seg = *pos_end - *pos_beg;
    dseg = dir_seg.length();
    if (dseg > 0.00001f)
    {
        dir_seg /= dseg;
    }
    else
    {
        dir_seg = g_O3;
    }

    dseg = dseg / num_desire;
    darc = (2.0f * PI) / num_desire;

    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        par = (SparkyParticle*)mem;
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

            par->pos = *pos_beg;
            par->pos += dir_seg * (dseg * (xurand() + num_emitted));
            par->pos += dir_outward * rad_emit;

            par->vel = dir_axis * (speed.spd_emitRand * fac_rand + speed.spd_emitBase);
            par->vel += dir_outward * (speed.spd_outRand * fac_rand + speed.spd_outBase);
            par->vel *= fac_spdRamp;
            if (vel_drift)
            {
                par->vel += *vel_drift;
            }

            par->acc_emitaxis = dir_axis * speed.acc_emitaxis;
            par->acc_emitaxis += dir_outward * speed.acc_outward;

            switch (useNPARtype)
            {
            case NPAR_TYP_RAIN:
            case NPAR_TYP_SPARKS:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            case NPAR_TYP_SEAMIST:
            case NPAR_TYP_SPARKY:
            case NPAR_TYP_FOUNTAIN:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_rand;
                break;
            case NPAR_TYP_COLORS:
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_rand;
                break;
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

void Sparky::SparkySysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "Sparky");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Streak");
}

void Sparky::SparkySysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_COLOR_MAT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(SparkyParticle);
    pbs->update = SparkySysUpdate;
    pbs->context = mgmt;
}

void Sparky::SparkySysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_COLOR_MAT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(SparkyParticle);
    pbs->update = SparkySysUpdate;
    pbs->context = mgmt;
}

void Sparky::SparkySysInit_Glow(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_COLOR_MAT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 2;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 8;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(SparkyParticle);
    pbs->update = SparkySysUpdate;
    pbs->context = mgmt;
}

// Always inlined (no symbol; the name is ours). The Xbox build keeps it out of line as an emitter
// member called from EvalCurve.
inline S32 SparkyEmitter::FindCurveNodes(F32 rat, SparkyCurve*& node_lo,
                                         SparkyCurve*& node_hi) const
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

// Weak in retail and emitted in this file's .text, so it is defined here, not in zNMEParFX.h.
inline S32 SparkyEmitter::EvalCurve(F32 rat, SparkyCurve* node_eval, S32 linearize,
                                    S32 idx_prev) const
{
    SparkyCurve* node_lo;
    SparkyCurve* node_hi;
    F32 fac;
    F32 fac_saddle;
    S32 idx;
    F32 den;
    F32 num;

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

    den = node_hi->time - node_lo->time;
    num = rat - node_lo->time;
    fac = 0.0f;
    if (den > 0.00001f)
    {
        fac = num / den;
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

S32 Sparky::SparkySysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__color_mat_uv2* pool;
    SparkyParticle* par;
    SparkyParticle* end;
    xVec3 pos_plyr;
    const SparkyEmitter* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    SparkyCurve node_eval;
    S32 idx;
    S32 linearize;
    F32 factor;
    xVec3 dir_trav;
    F32 spd;
    F32 recip;
    S32 systemUsesHDR;
    xColor_tag par_color;
    F32 adjAlpha;
    U8 alftest;
    F32 sum;
    const SparkyEmitter::Damage* dmg;
    F32 ds2_plyr;
    F32 fac_scale;
    F32 w;
    F32 h;

    pool = (ptank_pool__color_mat_uv2*)&apool;
    par = (SparkyParticle*)mem;
    end = par + count;
    pos_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);

    while (par != end)
    {
        emitter = par->emitter;
        par->tmr_remain -= dt;
        if (par->tmr_remain < 0.0f)
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

        xVec3AddScaled(&par->pos, &par->vel, dt);

        if (emitter->run.fac_damp != 0.0f)
        {
            factor = emitter->run.fac_damp * dt;
            par->vel.x -= par->vel.x * factor;
            par->vel.y -= par->vel.y * factor;
            par->vel.z -= par->vel.z * factor;
        }

        par->vel.y -= emitter->run.acc_gravity * dt;
        xVec3AddScaled(&par->vel, &par->acc_emitaxis, dt);
        xVec3AddScaled(&par->vel, &emitter->speed.acc_world, dt);

        xVec3Copy(&dir_trav, &par->vel);
        spd = xVec3Length(&dir_trav);
        if (spd < 0.00001f)
        {
            xVec3Copy(&dir_trav, &globals.oldSkoolCamera.mat.right);
        }
        else
        {
            recip = 1.0f / spd;
            dir_trav *= recip;
        }

        par->size = MAX(emitter->size.width, emitter->size.height);
        par->size *= node_eval.scale * emitter->curve.scl_master;

        RwSphere worldsph;
        worldsph.center.x = par->pos.x;
        worldsph.center.y = par->pos.y;
        worldsph.center.z = par->pos.z;
        worldsph.radius = par->size;

#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 13): on the host, the particle hurts the
        // guest too, wherever this player's camera looks (its alpha as worked out
        // below). FALSE at once when co-op is off.
        if (iCoopParticleWanted() && emitter->flags.doDamage &&
            rat_rev > emitter->dmg.pam_collide[0] && rat_rev < emitter->dmg.pam_collide[1])
        {
            xColor_tag c = node_eval.color;
            U8 a = CLAMP(emitter->curve.alf_master * c.a, 0.0f, 255.0f);
            switch (emitter->useNPARtype)
            {
            case NPAR_TYP_SPARKS:
            case NPAR_TYP_COLORS:
            case NPAR_TYP_SPARKY:
            case NPAR_TYP_FOUNTAIN:
                a = CLAMP(((F32)c.r + (F32)c.g + (F32)c.b) / 3.0f, 0.0f, 255.0f);
                break;
            default:
                break;
            }
            if (a > emitter->dmg.alf_collide)
            {
                iCoopParticleHit((const F32*)&par->pos, SQR(emitter->dmg.rad_collide));
            }
        }
#endif
        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
        {
            systemUsesHDR = 0;
            switch (emitter->useNPARtype)
            {
            case NPAR_TYP_SPARKS:
            case NPAR_TYP_COLORS:
            case NPAR_TYP_SPARKY:
            case NPAR_TYP_FOUNTAIN:
                systemUsesHDR = 1;
                break;
            }

            par_color = node_eval.color;
            adjAlpha = emitter->curve.alf_master * par_color.a;
            par_color.a = CLAMP(adjAlpha, 0.0f, 255.0f);
            alftest = par_color.a;

            if (systemUsesHDR)
            {
                sum = (F32)par_color.r + (F32)par_color.g + (F32)par_color.b;
                sum /= 3.0f;
                alftest = CLAMP(sum, 0.0f, 255.0f);
            }

            dmg = &emitter->dmg;
            if (emitter->flags.doDamage && !(globals.player.DamageTimer > 0.0f) &&
                globals.player.s->Health && rat_rev > dmg->pam_collide[0] &&
                rat_rev < dmg->pam_collide[1] && alftest > dmg->alf_collide)
            {
                ds2_plyr = NMEC_DstSq(&par->pos, &pos_plyr, NULL);
                if (ds2_plyr < SQR(dmg->rad_collide))
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

            xVec3Copy(&pool->mat->pos, &par->pos);
            xVec3Copy(&pool->mat->up, &dir_trav);
            xVec3Copy(&pool->mat->at, &globals.oldSkoolCamera.mat.at);
            xVec3Cross(&pool->mat->right, &pool->mat->up, &globals.oldSkoolCamera.mat.at);

            fac_scale = node_eval.scale * emitter->curve.scl_master;
            w = emitter->size.width;
            pool->mat->right.x *= w * fac_scale;
            pool->mat->right.y *= w * fac_scale;
            pool->mat->right.z *= w * fac_scale;
            h = emitter->size.height;
            pool->mat->up.x *= h * fac_scale;
            pool->mat->up.y *= h * fac_scale;
            pool->mat->up.z *= h * fac_scale;

            *pool->color = par_color;

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

    return end - (SparkyParticle*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void Sparky::SparkySysRegister()
{
    static NPARInfo nparsys_solid = { SparkySysInit_Glow, SparkySysTweaks, "spark_single2", 2 };
    static NPARInfo nparsys_sparky = { SparkySysInit_Glow, SparkySysTweaks, "fx_sparky", 2 };
    static NPARInfo nparsys_colors = { SparkySysInit_Glow, SparkySysTweaks, "fx_color_patches", 2 };
    static NPARInfo nparsys_fountain = { SparkySysInit_Glow, SparkySysTweaks, "fx_sparky", 2 };
    static NPARInfo nparsys_rain = { SparkySysInit_Dull, SparkySysTweaks, "fx_gen_dust", 2 };
    static NPARInfo nparsys_seaMist = { SparkySysInit_Hot, SparkySysTweaks, "fx_spray", 2 };

    g_npar_info[NPAR_TYP_SPARKS] = &nparsys_solid;
    g_npar_info[NPAR_TYP_SPARKY] = &nparsys_sparky;
    g_npar_info[NPAR_TYP_COLORS] = &nparsys_colors;
    g_npar_info[NPAR_TYP_FOUNTAIN] = &nparsys_fountain;
    g_npar_info[NPAR_TYP_RAIN] = &nparsys_rain;
    g_npar_info[NPAR_TYP_SEAMIST] = &nparsys_seaMist;
}
