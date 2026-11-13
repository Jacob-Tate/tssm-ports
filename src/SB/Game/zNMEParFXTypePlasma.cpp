#include "zNMEParFX.h"
#include "xMath.h"
#include "xVec3Inlines.h"
#include "iMath.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xPtankPool.h"
#include "zEntPlayer.h"
#include "zGlobals.h"

#include <stdio.h>
#include "xRand.h"

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

// Size: 0x40 (PS2 DWARF)
struct PlasmaParticle
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    const PlasmaEmitter* emitter;
    F32 tmr_remain;
    F32 tym_lifespan;
    F32 fac_abuse;
    xColor_tag color;
    F32 rot;
    F32 rot_vel;
    U8 uv_numCell;
    U8 uv_cell;
    U8 pct_keep;
    U8 pad1;
    S32 unused;
};

void PlasmaEmitter::ArcWelder(const xVec3* pos, const xVec3* dir_axis, F32 pct_spdRamp,
                              S32 num_emitOverride) const
{
    S32 pbs_id;
    S32 num_emitted;
    S32 cnt_remain;
    xVec3 dir_perp;
    xVec3 dir_orth;
    U8* mem;
    S32 num_par;
    F32 darc;
    PlasmaParticle* par;
    S32 i;
    F32 ang_curr;
    F32 samecalc;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    num_emitted = 0;
    cnt_remain = (num_emitOverride < 1) ? num_emit : num_emitOverride;

    dir_perp.x = dir_axis->y - dir_axis->z;
    dir_perp.y = dir_axis->z - dir_axis->x;
    dir_perp.z = dir_axis->x - dir_axis->y;
    xVec3Normalize(&dir_perp, &dir_perp);

    xVec3Cross(&dir_orth, &dir_perp, dir_axis);

    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        darc = (2.0f * PI) / num_par;

        par = (PlasmaParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            F32 fac_rand = xurand();
            ang_curr = -PI + darc * (num_emitted + fac_rand);
            F32 ang_cos = icos(ang_curr);
            F32 ang_sin = isin(ang_curr);
            xVec3 dir_outward = dir_perp * ang_sin + dir_orth * ang_cos;

            par->pos = *pos + dir_outward * fount.rad_emit;

            par->vel = *dir_axis * (fount.spd_emitRand * fac_rand + fount.spd_emitBase);
            par->vel += dir_outward * (fount.spd_outRand * fac_rand + fount.spd_outBase);
            par->vel *= pct_spdRamp;

            par->size = siz_base[0];
            samecalc = tym_lifespan * (0.5f * fac_rand + 0.5f);
            par->emitter = this;
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->fac_abuse = fac_rand;
            par->color = colr_base;
            par->uv_numCell = 4;
            par->uv_cell = 3.9f * fac_rand;
            par->pct_keep = pct_keep;
            par->rot = 0.0f;
            par->rot_vel = spd_rotate * (2.0f * (fac_rand - 0.5f));

            par++;
            num_emitted++;
        }

        cnt_remain -= num_par;
    }
}

// Placeholder for dead-stripped code, not recovered code: retail's .rodata keeps an unreferenced
// all-zero 0xC template from this part of the file. Its owner and type are unknown.
static void __deadstripped_zNMEParFXTypePlasma()
{
    xVec3 unknown = {};
}

void Plasma::PlasmaSysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "Plasma");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
}

void Plasma::PlasmaSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 2;
    pbs->rs.dst_blend = 2;
    pbs->rs.flags = 8;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(PlasmaParticle);
    pbs->update = PlasmaSysUpdate;
    pbs->context = mgmt;
}

namespace Plasma
{
    namespace Update
    {
        static const F32 seg_allowCollide[2] = { 0.0f, 1.0f };
    } // namespace Update
} // namespace Plasma

S32 Plasma::PlasmaSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__pos_color_size_rot_uv2* pool;
    PlasmaParticle* par;
    PlasmaParticle* end;
    xVec3 pos_plyr;
    const PlasmaEmitter* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    F32 fac_keep;
    F32 beans;
    F32 fac_accelSide;
    F32 arch;
    F32 dim;
    F32 cfac;
    F32 ds2_plyr;

    pool = (ptank_pool__pos_color_size_rot_uv2*)&apool;
    par = (PlasmaParticle*)mem;
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

        par->pos += par->vel * dt;

        fac_keep = CLAMP(0.01f * par->pct_keep, 0.0f, 100.0f);
        beans = dt * par->fac_abuse;
        par->vel *= fac_keep;
        par->vel += emitter->acc_base * beans;

        if (emitter->pct_accelSide)
        {
            fac_accelSide = (1.0f + emitter->pct_accelSide) * (dt * par->fac_abuse);
            par->vel.x *= fac_accelSide;
            par->vel.z *= fac_accelSide;
        }

        arch = ARCH(rat_rev);
        dim = LERP(arch, emitter->siz_base[0], emitter->siz_base[1]);
        par->size = dim;

#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 13): on the host, the particle hurts the
        // guest too, wherever this player's camera looks. Nothing when co-op is off.
        if (emitter->doDamage && rat_rev > Update::seg_allowCollide[0] &&
            rat_rev < Update::seg_allowCollide[1] &&
            SMOOTH(rat_rev, 1.0f, 0.0f) * emitter->colr_base.a > 0x20)
        {
            iCoopParticleHit((const F32*)&par->pos, 0.25f);
        }
#endif

        RwSphere sph;
        sph.center = par->pos.m_RwV3d;
        sph.radius = par->size;
        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &sph))
        {
            cfac = SMOOTH(rat_rev, 1.0f, 0.0f);
            par->color.r = cfac * emitter->colr_base.r;
            par->color.g = cfac * emitter->colr_base.g;
            par->color.b = cfac * emitter->colr_base.b;
            par->color.a = cfac * emitter->colr_base.a;

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
        }

        par++;
    }

    return end - (PlasmaParticle*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void Plasma::PlasmaSysRegister()
{
    static NPARInfo nparsys_plasma = { PlasmaSysInit, PlasmaSysTweaks, "fx_plasma", 2 };
    static NPARInfo nparsys_energy = { PlasmaSysInit, PlasmaSysTweaks, "fx_energy", 2 };
    static NPARInfo nparsys_muzzle = { PlasmaSysInit, PlasmaSysTweaks, "fx_beam_muzzle_flash", 2 };

    g_npar_info[NPAR_TYP_PLASMA] = &nparsys_plasma;
    g_npar_info[NPAR_TYP_ENERGY] = &nparsys_energy;
    g_npar_info[NPAR_TYP_MUZZLE] = &nparsys_muzzle;
}
