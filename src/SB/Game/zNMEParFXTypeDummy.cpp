#include "zNMEParFX.h"
#include "xMath.h"
#include "xMath3.h"
#include "xPtankPool.h"
#include "zEntPlayer.h"
#include "zGlobals.h"

#include <stdio.h>

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

// Size: 0x28 (PS2 DWARF)
struct DUMMY_PAREMITTER
{
    en_npartyp useNPARtype;
    F32 tym_lifespan;
    xColor_tag colr_base;
    F32 siz_base[2];
    xVec3 acc_base;
    F32 spd_rotate;
    U8 pct_keep;
    U8 doDamage;
    U8 unused[2];
};

// Size: 0x3C (PS2 DWARF)
struct DUMMY_PARSTRUCT
{
    xVec3 pos;
    F32 size;
    xVec3 vel;
    DUMMY_PAREMITTER* emitter;
    F32 tmr_remain;
    F32 tym_lifespan;
    F32 fac_abuse;
    xColor_tag color;
    U8 uv_numCell;
    U8 uv_cell;
    U8 pct_keep;
    U8 padding;
    F32 rot;
    F32 rot_vel;
};

void DUMMY_NAMESPACE::DUMMY_PREFIXSysTweaks(NPARMgmt* mgmt, const char* prefix_root)
{
    char prefix_base[128];
    char prefix[128];

    sprintf(prefix_base, "%s|%s", prefix_root, "DUMMY_PREFIX");
    sprintf(prefix, "%s|%s|", prefix_base, "Update|Plyr Collide");
}

void DUMMY_NAMESPACE::DUMMY_PREFIXSysInit(NPARMgmt* mgmt, xParticleBatchSystem* pbs)
{
    pbs->pool_type = PGT_POS_COLOR_SIZE_ROT_UV2;
    pbs->rs.texture = mgmt->txtr;
    pbs->rs.src_blend = 5;
    pbs->rs.dst_blend = 6;
    pbs->rs.flags = 0;
    pbs->order_group = 0;
    pbs->order_index = 0;
    pbs->stride = sizeof(DUMMY_PARSTRUCT);
    pbs->update = DUMMY_PREFIXSysUpdate;
    pbs->context = mgmt;
}

namespace DUMMY_NAMESPACE
{
    namespace Update
    {
        // Tweakable as "Update|Plyr Collide" (see DUMMY_PREFIXSysTweaks).
        static const F32 seg_allowCollide[2] = { 0.0f, 1.0f };
    } // namespace Update
} // namespace DUMMY_NAMESPACE

S32 DUMMY_NAMESPACE::DUMMY_PREFIXSysUpdate(U8* mem, S32 count, ptank_pool& apool, F32 dt,
                                           void* context)
{
    ptank_pool__pos_color_size_rot_uv2* pool;
    DUMMY_PARSTRUCT* par;
    DUMMY_PARSTRUCT* end;
    xVec3 pos_plyr;
    DUMMY_PAREMITTER* emitter;
    F32 tym_life;
    F32 rat_rev;
    F32 rat;
    F32 fac_keep;
    F32 arch;
    F32 dim;
    F32 beans;
    F32 ds2_plyr;

    pool = (ptank_pool__pos_color_size_rot_uv2*)&apool;
    par = (DUMMY_PARSTRUCT*)mem;
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

        par->pos += par->vel * dt;

        fac_keep = CLAMP(0.01f * par->pct_keep, 0.0f, 100.0f);
        beans = dt * par->fac_abuse;
        arch = SQR(2.0f * (rat_rev - 0.5f));
        par->vel *= fac_keep;
        dim = 1.0f - arch;
        par->vel += emitter->acc_base * beans;

        par->size = dim * (emitter->siz_base[1] - emitter->siz_base[0]) + emitter->siz_base[0];
        par->color.a = dim * emitter->colr_base.a;

        par->rot += par->rot_vel * dt;
        // Clamps rot_vel, not rot, as retail does.
        par->rot = xDangleClamp(par->rot_vel);

#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 13): on the host, the particle hurts the
        // guest too. Nothing when co-op is off.
        if (emitter->doDamage && rat_rev > Update::seg_allowCollide[0] &&
            rat_rev < Update::seg_allowCollide[1] && par->color.a > 0x20)
        {
            iCoopParticleHit((const F32*)&par->pos, 0.25f);
        }
#endif
        if (emitter->doDamage && !(globals.player.DamageTimer > 0.0f) && globals.player.s->Health &&
            rat_rev > Update::seg_allowCollide[0] && rat_rev < Update::seg_allowCollide[1] &&
            par->color.a > 0x20)
        {
            ds2_plyr = NMEC_DstSq(&par->pos, &pos_plyr, NULL);
            if (ds2_plyr < 0.25f)
            {
                zEntPlayer_DamageNPCKnockBack(NULL, 1, &par->pos, ePlayerNMEDamageEnemyParticle);
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
        pool->uv[0].x = 0.0f;
        pool->uv[0].y = 0.0f;
        pool->uv[1].x = 1.0f;
        pool->uv[1].y = 1.0f;

        par++;
    }

    return end - (DUMMY_PARSTRUCT*)mem;
}

// Name unknown (see zNMEParFX.h): inlined into zNMEParFX_RegisterTypes, dead-stripped.
void DUMMY_NAMESPACE::DUMMY_PREFIXSysRegister()
{
    static NPARInfo nparsys = { DUMMY_PREFIXSysInit, DUMMY_PREFIXSysTweaks, "particle_cloud", 2 };

    g_npar_info[NPAR_TYP_DUMMY] = &nparsys;
}
