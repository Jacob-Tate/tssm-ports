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

// Size: 0x60 (PS2 DWARF)
struct OrientParticle
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    const OrientEmitter* emitter;
    F32 tym_lifespan;
    F32 tmr_remain;
    F32 fac_abuse;
    F32 rot;
    xVec3 acc_emitaxis;
    S8 idx_curvNode;
    U8 uv_numCell;
    U8 uv_cell;
    S8 unused;
    xVec3 dir_emitAxis;
    S32 padding;
    xVec3 dir_emitPerp;
    S32 padtoo;
};

void OrientEmitter::EmitRing(const xVec3* pos_center, const xVec3* dir_axis, F32 rad_emitCaller,
                             F32 pct_spdRamp, S32 num_emitCaller) const
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
    OrientParticle* par;
    S32 i;
    F32 samecalc;
    F32 ang_curr;

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

        par = (OrientParticle*)mem;
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

            F32 rad_emit;
            if (rad_emitCaller > -10000.0f)
            {
                rad_emit = rad_emitCaller;
            }
            else
            {
                rad_emit = fac_rand * emit.rad_emitRand + emit.rad_emitBase;
            }

            par->dir_emitAxis = *dir_axis;
            par->dir_emitPerp = dir_outward;

            par->pos = *pos_center + dir_outward * rad_emit;

            par->vel = *dir_axis * (speed.spd_emitRand * fac_rand + speed.spd_emitBase);
            par->vel += dir_outward * (speed.spd_outRand * fac_rand + speed.spd_outBase);
            par->vel *= pct_spdRamp;

            par->acc_emitaxis = *dir_axis * speed.acc_emitaxis;
            par->acc_emitaxis += dir_outward * speed.acc_outward;

            switch (useNPARtype)
            {
            case NPAR_TYP_RKOWAVES:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_rand;
                break;
            case NPAR_TYP_RINGS:
            case NPAR_TYP_MACHRING:
            case NPAR_TYP_SPAWNRINGS:
            case NPAR_TYP_SEARAINBOW:
            case NPAR_TYP_SEAFOAM:
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

void Orient::OrientSysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "Orient");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
}

void Orient::OrientSysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_COLOR_MAT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(OrientParticle);
    pbs->update = OrientSysUpdate;
    pbs->context = mgmt;
}

void Orient::OrientSysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_COLOR_MAT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(OrientParticle);
    pbs->update = OrientSysUpdate;
    pbs->context = mgmt;
}

// Always inlined (no symbol; the name is ours). The Xbox build keeps it out of line as an emitter
// member called from EvalCurve.
inline S32 OrientEmitter::FindCurveNodes(F32 rat, OrientCurve*& lo, OrientCurve*& hi) const
{
    S32 idx;
    S32 i;

    if (curve.curveSize < 2)
    {
        idx = 0;
        lo = curve.curveData;
        hi = curve.curveData;
    }
    else if (rat < 0.0f)
    {
        idx = 0;
        lo = curve.curveData;
        hi = curve.curveData + 1;
    }
    else if (rat > 1.0f)
    {
        idx = curve.curveSize - 1;
        lo = curve.curveData + (idx - 1);
        hi = curve.curveData + idx;
    }
    else
    {
        S32 num = curve.curveSize;

        idx = -1;
        for (i = 0; i < num - 1; i++)
        {
            lo = &curve.curveData[i];
            hi = &curve.curveData[i + 1];
            if (hi->time <= rat)
            {
                continue;
            }

            idx = i;
            break;
        }
    }

    return idx;
}

// Weak in retail and emitted in this file's .text, so it is defined here, not in zNMEParFX.h.
inline S32 OrientEmitter::EvalCurve(F32 rat, OrientCurve* node_eval, S32 linearize,
                                    S32 idx_curr) const
{
    OrientCurve* lo;
    OrientCurve* hi;
    F32 fac;
    F32 fac_saddle;
    S32 idx;

    rat = CLAMP(rat, 0.0f, 1.0f);

    if (idx_curr < 0)
    {
        idx = FindCurveNodes(rat, lo, hi);
    }
    else
    {
        lo = &curve.curveData[idx_curr];
        if (idx_curr >= curve.curveSize - 1)
        {
            hi = &curve.curveData[idx_curr];
        }
        else
        {
            hi = &curve.curveData[idx_curr + 1];
        }
        idx = idx_curr;

        if (rat < lo->time || rat > hi->time)
        {
            idx = FindCurveNodes(rat, lo, hi);
        }
    }

    F32 tym_range = hi->time - lo->time;
    F32 tym_delta = rat - lo->time;
    fac = 0.0f;
    if (tym_range > 0.00001f)
    {
        fac = tym_delta / tym_range;
    }

    if (linearize)
    {
        fac_saddle = fac;
    }
    else
    {
        fac_saddle = fac * (3.0f * fac - 2.0f * fac * fac);
    }

    node_eval->scale = fac_saddle * (hi->scale - lo->scale) + lo->scale;
    node_eval->color.r = lo->color.r + (U8)(fac_saddle * (hi->color.r - lo->color.r));
    node_eval->color.g = lo->color.g + (U8)(fac_saddle * (hi->color.g - lo->color.g));
    node_eval->color.b = lo->color.b + (U8)(fac_saddle * (hi->color.b - lo->color.b));
    node_eval->color.a = lo->color.a + (U8)(fac_saddle * (hi->color.a - lo->color.a));

    return idx;
}

S32 Orient::OrientSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__color_mat_uv2* pool;
    OrientParticle* par;
    OrientParticle* end;
    xVec3 pos_plyr;
    const OrientEmitter* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    OrientCurve node_eval;
    RwSphere worldsph;
    S32 idx;
    S32 linearize;
    S32 systemUsesHDR;
    F32 fac_randSaddle;
    xColor_tag par_color;
    F32 adjAlpha;
    U8 alftest;
    F32 sum;
    const OrientEmitter::Damage* dmg;
    F32 ds2_plyr;
    F32 sv;
    F32 cv;
    xVec3 dir_over;
    F32 scaleSize;
    F32 wid_par;
    F32 hyt_par;

    pool = (ptank_pool__color_mat_uv2*)&apool;
    par = (OrientParticle*)mem;
    end = par + count;
    pos_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);

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

        worldsph.center.x = par->pos.x;
        worldsph.center.y = par->pos.y;
        worldsph.center.z = par->pos.z;
        worldsph.radius = par->size;

        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
        {
            systemUsesHDR = 0;
            if (emitter->useNPARtype == NPAR_TYP_NOTES_UPGRADE)
            {
                systemUsesHDR = 1;
            }

            switch (emitter->flags.opt_rotate)
            {
            case 2:
                par->rot += emitter->run.spd_rotate * dt;
                break;
            case 1:
                par->rot += dt * (emitter->run.spd_rotate * par->fac_abuse);
                break;
            case 0:
            default:
                fac_randSaddle = par->fac_abuse;
                par->rot += dt * (emitter->run.spd_rotate * (2.0f * (fac_randSaddle - 0.5f)));
                par->rot = xDangleClamp(par->rot);
                break;
            }
            par->rot = xDangleClamp(par->rot);

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

            pool->mat->pos = par->pos;

            if (emitter->run.spd_rotate == 0.0f)
            {
                pool->mat->at = par->dir_emitAxis;
                pool->mat->up = par->dir_emitPerp;
                xVec3Cross(&pool->mat->right, &par->dir_emitPerp, &par->dir_emitAxis);
            }
            else
            {
                sv = isin(par->rot);
                cv = icos(par->rot);

                xVec3Cross(&dir_over, &par->dir_emitPerp, &par->dir_emitAxis);
                pool->mat->at = par->dir_emitAxis;
                pool->mat->up = par->dir_emitPerp * sv + dir_over * cv;
                xVec3Cross(&pool->mat->right, &pool->mat->up, &par->dir_emitAxis);
            }

            scaleSize = node_eval.scale * emitter->curve.scl_master;
            wid_par = emitter->size.width * scaleSize;
            hyt_par = emitter->size.height * scaleSize;
            pool->mat->up *= hyt_par;
            pool->mat->right *= wid_par;

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

    return end - (OrientParticle*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void Orient::OrientSysRegister()
{
    static NPARInfo nparsys_rko = { OrientSysInit_Hot, OrientSysTweaks, "partex0", 2 };
    static NPARInfo nparsys_spawnRings = { OrientSysInit_Hot, OrientSysTweaks, "fx_spawnring", 2 };
    static NPARInfo nparsys_rings = { OrientSysInit_Dull, OrientSysTweaks, "fx_mach_ring", 2 };
    static NPARInfo nparsys_machRings = { OrientSysInit_Hot, OrientSysTweaks, "fx_mach_ring", 2 };
    static NPARInfo nparsys_seaFoam = { OrientSysInit_Dull, OrientSysTweaks,
                                        "ExplosionFlash_Tan_64", 2 };
    static NPARInfo nparsys_seaRainbow = { OrientSysInit_Dull, OrientSysTweaks,
                                           "rainbowfilm_smooth32", 2 };

    g_npar_info[NPAR_TYP_RINGS] = &nparsys_rings;
    g_npar_info[NPAR_TYP_SEAFOAM] = &nparsys_seaFoam;
    g_npar_info[NPAR_TYP_RKOWAVES] = &nparsys_rko;
    g_npar_info[NPAR_TYP_SPAWNRINGS] = &nparsys_spawnRings;
    g_npar_info[NPAR_TYP_MACHRING] = &nparsys_machRings;
    g_npar_info[NPAR_TYP_SEARAINBOW] = &nparsys_seaRainbow;
}
