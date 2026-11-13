#include "zNMEParFX.h"
#include "xMath.h"
#include "iMath.h"
#include "xMath2.h"
#include "xMathInlines.h"
#include "xPtankPool.h"
#include "xEnt.h"
#include "xGlobals.h"
#include "zGlobals.h"
#include "zEntPlayer.h"

#include <stdio.h>
#include "xRand.h"

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

// Size: 0x40 (PS2 DWARF)
struct PuffParticle
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    const PuffEmitter* emitter;
    F32 tmr_remain;
    F32 tym_lifespan;
    F32 fac_abuse;
    xColor_tag color;
    F32 rot;
    F32 rot_vel;
    U8 uv_numCell;
    U8 uv_cell;
    U8 pct_keep;
    U8 padding;
    S32 unused;
};

void PuffEmitter::PropWash(const xVec3* pos, const xVec3* vel_drift) const
{
    S32 pbs_id;
    S32 cnt_remain;
    S32 num_emitted;
    U8* mem;
    S32 num_par;
    F32 darc;
    PuffParticle* par;
    S32 i;
    F32 ang_curr;
    F32 fac_randSaddle;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    cnt_remain = num_emit;
    num_emitted = 0;
    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        darc = (2.0f * PI) / num_par;
        par = (PuffParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            fac_randSaddle = xurand();
            ang_curr = -PI + num_emitted * darc + darc * fac_randSaddle;

            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);
            xVec3 dir = { ang_sin, 0.0f, ang_cos };

            par->pos = *pos + dir * wash.rad_ring;
            par->vel = dir * (wash.spd_outward * fac_randSaddle);
            if (vel_drift != NULL)
            {
                par->vel += *vel_drift;
            }

            par->size = siz_base[0];
            par->emitter = this;
            F32 fac_spin = 2.0f * (fac_randSaddle - 0.5f);
            F32 samecalc = tym_lifespan * (0.5f * fac_randSaddle + 0.5f);
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->fac_abuse = fac_randSaddle;
            par->color = colr_base;

            switch (useNPARtype)
            {
            case 99999999: // no en_npartyp has this value
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_randSaddle;
                break;
            case NPAR_TYP_DUSTPUFF:
            case NPAR_TYP_DUSTHEAT:
            case NPAR_TYP_RADIOACTIVE:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_randSaddle;
                break;
            case NPAR_TYP_FLINGVAPORS_DE:
            case NPAR_TYP_FLINGVAPORS_TR:
            case NPAR_TYP_FLINGVAPORS_JK:
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par->pct_keep = pct_keep;
            par->rot = 0.0f;
            par->rot_vel = spd_rotate * fac_spin;

            num_emitted++;
            par++;
        }

        cnt_remain -= num_par;
    }
}

void PuffEmitter::ImpactRing(const xVec3* pos, const xVec3* vel_drift) const
{
    S32 pbs_id;
    S32 cnt_remain;
    S32 num_emitted;
    U8* mem;
    S32 num_par;
    F32 darc;
    PuffParticle* par;
    S32 i;
    F32 ang_curr;
    F32 fac_randSaddle;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    cnt_remain = num_emit;
    num_emitted = 0;
    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        darc = (2.0f * PI) / num_par;
        par = (PuffParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            fac_randSaddle = xurand();
            ang_curr = -PI + num_emitted * darc + darc * fac_randSaddle;

            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);
            xVec3 dir = { ang_sin, 0.0f, ang_cos };

            par->pos = *pos + dir * ring.rad_ring;
            par->vel = dir * (ring.spd_outward * fac_randSaddle);
            if (vel_drift != NULL)
            {
                par->vel += *vel_drift;
            }

            par->size = siz_base[0];
            par->emitter = this;
            F32 fac_spin = 2.0f * (fac_randSaddle - 0.5f);
            F32 samecalc = tym_lifespan * (0.5f * fac_randSaddle + 0.5f);
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->fac_abuse = fac_randSaddle;
            par->color = colr_base;

            switch (useNPARtype)
            {
            case 99999999: // no en_npartyp has this value
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_randSaddle;
                break;
            case NPAR_TYP_DUSTPUFF:
            case NPAR_TYP_DUSTHEAT:
            case NPAR_TYP_RADIOACTIVE:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_randSaddle;
                break;
            case NPAR_TYP_FLINGVAPORS_DE:
            case NPAR_TYP_FLINGVAPORS_TR:
            case NPAR_TYP_FLINGVAPORS_JK:
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par->pct_keep = pct_keep;
            par->rot = 0.0f;
            par->rot_vel = spd_rotate * fac_spin;

            num_emitted++;
            par++;
        }

        cnt_remain -= num_par;
    }
}

void PuffEmitter::RoundRing(const xVec3* pos, F32 radius, const xVec3* vel_drift) const
{
    S32 pbs_id;
    S32 cnt_remain;
    S32 num_emitted;
    U8* mem;
    S32 num_par;
    F32 darc;
    PuffParticle* par;
    S32 i;
    F32 ang_curr;
    F32 fac_randSaddle;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    cnt_remain = num_emit;
    num_emitted = 0;
    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        darc = (2.0f * PI) / num_par;
        par = (PuffParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            fac_randSaddle = xurand();
            ang_curr = -PI + num_emitted * darc + darc * fac_randSaddle;

            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);
            xVec3 dir = { ang_sin, 0.0f, ang_cos };

            par->pos = *pos + dir * radius;
            par->vel = dir * (wash.spd_outward * fac_randSaddle);
            if (vel_drift != NULL)
            {
                par->vel += *vel_drift;
            }

            par->size = siz_base[0];
            par->emitter = this;
            F32 fac_spin = 2.0f * (fac_randSaddle - 0.5f);
            F32 samecalc = tym_lifespan * (0.5f * fac_randSaddle + 0.5f);
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->fac_abuse = fac_randSaddle;
            par->color = colr_base;

            switch (useNPARtype)
            {
            case 99999999: // no en_npartyp has this value
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_randSaddle;
                break;
            case NPAR_TYP_DUSTPUFF:
            case NPAR_TYP_DUSTHEAT:
            case NPAR_TYP_RADIOACTIVE:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_randSaddle;
                break;
            case NPAR_TYP_FLINGVAPORS_DE:
            case NPAR_TYP_FLINGVAPORS_TR:
            case NPAR_TYP_FLINGVAPORS_JK:
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par->pct_keep = pct_keep;
            par->rot = 0.0f;
            par->rot_vel = spd_rotate * fac_spin;

            num_emitted++;
            par++;
        }

        cnt_remain -= num_par;
    }
}

void PuffEmitter::Footstroke(const xVec3* pos, const xVec3* dir_emit) const
{
    S32 pbs_id;
    S32 cnt_remain;
    U8* mem;
    S32 num_par;
    PuffParticle* par;
    S32 i;
    F32 spd;
    F32 fac_randSaddle;
    F32 samecalc;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    cnt_remain = num_emit;
    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        par = (PuffParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            fac_randSaddle = xurand();

            par->pos = *pos;
            spd = stroke.spd_emitRand * fac_randSaddle + stroke.spd_emitBase;
            par->vel = *dir_emit * spd;

            par->size = siz_base[0];
            par->emitter = this;
            F32 fac_spin = 2.0f * (fac_randSaddle - 0.5f);
            samecalc = tym_lifespan * (0.5f * fac_randSaddle + 0.5f);
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->fac_abuse = fac_randSaddle;
            par->color = colr_base;

            switch (useNPARtype)
            {
            case 99999999: // no en_npartyp has this value
                par->uv_numCell = 16;
                par->uv_cell = 15.9f * fac_randSaddle;
                break;
            case NPAR_TYP_DUSTPUFF:
            case NPAR_TYP_DUSTHEAT:
            case NPAR_TYP_RADIOACTIVE:
                par->uv_numCell = 4;
                par->uv_cell = 3.9f * fac_randSaddle;
                break;
            case NPAR_TYP_FLINGVAPORS_DE:
            case NPAR_TYP_FLINGVAPORS_TR:
            case NPAR_TYP_FLINGVAPORS_JK:
            default:
                par->uv_numCell = 1;
                par->uv_cell = 0;
                break;
            }

            par->pct_keep = pct_keep;
            par->rot = 0.0f;
            par->rot_vel = spd_rotate * fac_spin;

            par++;
        }

        cnt_remain -= num_par;
    }
}

void PuffRing::PuffSysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "Puff");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
}

void PuffRing::PuffSysInit_Dull(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(PuffParticle);
    pbs->update = PuffSysUpdate;
    pbs->context = mgmt;
}

void PuffRing::PuffSysInit_Hot(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(PuffParticle);
    pbs->update = PuffSysUpdate;
    pbs->context = mgmt;
}

void PuffRing::PuffSysInit_Glow(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 8;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(PuffParticle);
    pbs->update = PuffSysUpdate;
    pbs->context = mgmt;
}

namespace PuffRing
{
    namespace Update
    {
        // Tweakable as "Update|Plyr Collide" (see PuffSysTweaks).
        static const F32 seg_allowCollide[2] = { 0.0f, 1.0f };
    } // namespace Update
} // namespace PuffRing

S32 PuffRing::PuffSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__pos_color_size_rot_uv2* pool = (ptank_pool__pos_color_size_rot_uv2*)&apool;
    PuffParticle* par = (PuffParticle*)mem;
    PuffParticle* end = par + count;
    xVec3 pos_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);
    const PuffEmitter* emitter;
    F32 rat_rev;
    F32 rat;
    F32 fac_keep;
    F32 beans;
    F32 arch;
    F32 dim;
    F32 ds2_plyr;

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

        rat = MAX(0.0f, par->tmr_remain) / par->tym_lifespan;
        rat_rev = 1.0f - rat;

        par->pos += par->vel * dt;

        fac_keep = CLAMP(0.01f * par->pct_keep, 0.0f, 100.0f);
        beans = dt * par->fac_abuse;
        par->vel *= fac_keep;
        par->vel += emitter->acc_base * beans;

        arch = ARCH(rat_rev);
        dim = LERP(arch, emitter->siz_base[0], emitter->siz_base[1]);
        par->size = dim;

#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 13): on the host, the particle hurts the
        // guest too, wherever this player's camera looks. Nothing when co-op is off.
        if (emitter->doDamage && rat_rev > Update::seg_allowCollide[0] &&
            rat_rev < Update::seg_allowCollide[1] && arch * emitter->colr_base.a > 0x20)
        {
            iCoopParticleHit((const F32*)&par->pos, 0.25f);
        }
#endif

        RwSphere worldsph;
        worldsph.center.x = par->pos.x;
        worldsph.center.y = par->pos.y;
        worldsph.center.z = par->pos.z;
        worldsph.radius = par->size;

        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
        {
            par->color.a = arch * emitter->colr_base.a;

            par->rot += par->rot_vel * dt;
            par->rot = xDangleClamp(par->rot);

            if (emitter->doDamage && !(globals.player.DamageTimer > 0.0f) &&
                globals.player.s->Health && rat_rev > Update::seg_allowCollide[0] &&
                rat_rev < Update::seg_allowCollide[1] && par->color.a > 0x20)
            {
                ds2_plyr = NMEC_DstSq(&par->pos, &pos_plyr, NULL);
                if (ds2_plyr < 0.25f)
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
            *pool->color = par->color;
            pool->size->assign(par->size);
            *pool->rot = par->rot;

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

    return end - (PuffParticle*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void PuffRing::PuffSysRegister()
{
    static NPARInfo nparsys_dust = { PuffSysInit_Dull, PuffSysTweaks, "fx_gen_dust", 2 };
    static NPARInfo nparsys_heat = { PuffSysInit_Hot, PuffSysTweaks, "fx_gen_dust", 2 };
    static NPARInfo nparsys_nuke = { PuffSysInit_Glow, PuffSysTweaks, "fx_gen_dust", 2 };
    static NPARInfo nparsys_tartar_desert = { PuffSysInit_Hot, PuffSysTweaks,
                                              "fx_par_tartar_desert", 2 };
    static NPARInfo nparsys_tartar_trench = { PuffSysInit_Hot, PuffSysTweaks,
                                              "fx_par_tartar_trench", 2 };
    static NPARInfo nparsys_tartar_junk = { PuffSysInit_Hot, PuffSysTweaks, "fx_par_tartar_junk",
                                            2 };

    g_npar_info[NPAR_TYP_DUSTPUFF] = &nparsys_dust;
    g_npar_info[NPAR_TYP_DUSTHEAT] = &nparsys_heat;
    g_npar_info[NPAR_TYP_RADIOACTIVE] = &nparsys_nuke;
    g_npar_info[NPAR_TYP_FLINGVAPORS_DE] = &nparsys_tartar_desert;
    g_npar_info[NPAR_TYP_FLINGVAPORS_TR] = &nparsys_tartar_trench;
    g_npar_info[NPAR_TYP_FLINGVAPORS_JK] = &nparsys_tartar_junk;
}
