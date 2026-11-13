#include "zNMEParFX.h"
#include "xMath.h"
#include "xMath3.h"
#include "xPtankPool.h"
#include "zEntPlayer.h"
#include "zGlobals.h"

#include <stdio.h>
#include "xRand.h"

// Size: 0x40 (PS2 DWARF)
struct FogParticle
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    const FogBreathEmitter* emitter;
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
    F32 unused;
};

// No symbol (always inlined; the name is ours). BfBB's BreathAttack computes the same cone velocity
// in place, but retail's stack layout needs this inline level.
static inline void FogBreath_ConeVel(xVec3& vel, const FogBreathEmitter::Volume& volume,
                                     const xMat3x3* axis)
{
    xVec3 vel_emit;

    vel_emit = axis->at * volume.vec_coneWeights.z;
    vel_emit += axis->right * (volume.vec_coneWeights.x * (2.0f * (xurand() - 0.5f)));
    vel_emit += axis->up * (volume.vec_coneWeights.y * (2.0f * (xurand() - 0.5f)));
    vel_emit.normalize();
    vel_emit *= volume.spd_emitRand * xurand() + volume.spd_emitBase;

    vel = vel_emit;
}

void FogBreathEmitter::SpewVolume(const xVec3* pos, const xMat3x3* axis) const
{
    S32 pbs_id;
    S32 cnt_remain;
    U8* mem;
    S32 num_par;
    FogParticle* par;
    S32 i;
    F32 samecalc;

    pbs_id = zNMEParFX_FindParty(useNPARtype)->pbs_sysid;
    cnt_remain = volume.num_emit;
    while (cnt_remain > 0)
    {
        num_par = xParticleBatchEmit(pbs_id, cnt_remain, &mem);
        if (num_par < 1)
        {
            break;
        }

        par = (FogParticle*)mem;
        for (i = 0; i < num_par; i++)
        {
            F32 fac_rand = xurand();

            par->pos = *pos;

            FogBreath_ConeVel(par->vel, volume, axis);

            par->emitter = this;
            par->size = siz_base[0];

            samecalc = tym_lifespan * (0.5f * fac_rand + 0.5f);
            par->tmr_remain = samecalc;
            par->tym_lifespan = samecalc;
            par->fac_abuse = fac_rand;

            par->color = colr_base;
            par->uv_numCell = 1;
            par->uv_cell = 0;
            par->pct_keep = pct_keep;

            par->rot = 0.0f;
            // Adds 2.0f as retail does (PuffRing scales by 2.0f * (fac - 0.5f)).
            par->rot_vel = spd_rotate * (2.0f + (fac_rand - 0.5f));

            par++;
        }

        cnt_remain -= num_par;
    }
}

void FogBreath::FogSysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "FogBreath");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
}

void FogBreath::FogSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(FogParticle);
    pbs->update = FogSysUpdate;
    pbs->context = mgmt;
}

namespace FogBreath
{
    namespace Update
    {
        static const F32 seg_allowCollide[2] = { 0.0f, 1.0f };
    } // namespace Update
} // namespace FogBreath

S32 FogBreath::FogSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt, void* context)
{
    ptank_pool__pos_color_size_rot* pool;
    FogParticle* par;
    FogParticle* end;
    xVec3 pos_plyr;
    const FogBreathEmitter* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    F32 fac_keep;
    F32 beans;
    F32 arch;
    F32 dim;
    F32 ds2_plyr;

    pool = (ptank_pool__pos_color_size_rot*)&apool;
    par = (FogParticle*)mem;
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

        arch = ARCH(rat_rev);
        dim = LERP(arch, emitter->siz_base[0], emitter->siz_base[1]);
        par->size = dim;

        RwSphere sph;
        sph.center = par->pos.m_RwV3d;
        sph.radius = par->size;
        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &sph))
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
        }

        par++;
    }

    return end - (FogParticle*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void FogBreath::FogSysRegister()
{
    static NPARInfo nparsys = { FogSysInit, FogSysTweaks, "fx_fogger_breath", 2 };

    g_npar_info[NPAR_TYP_FOGBREATH] = &nparsys;
}
