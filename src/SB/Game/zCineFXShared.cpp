#include "zCineFX.h"
#include "zCineFXShared.h"

#include "xMathInlines.h"

#include "xFX.h"
#include "xMath3.h"
#include "xParEmitter.h"
#include "xParEmitterType.h"
#include "xVec3Inlines.h"

#include "zCutsceneMgr.h"
#include "zGlobals.h"

void zCinFX_Generic_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
    }
}

void zCinFX_GenNCINInput_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (fxrec->input.fxparm == NULL)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
        return;
    }

    if (killit)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
    }
}

void zCinFX_CycleOnce_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
    }
    else if (!(fxrec->flg_stat & NCIN_STAT_FIRST))
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
    }
}

void zCinFX_EntityBonePar_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                             U32 animIndex, U32)
{
    F32 dt;
    xParEmitter* pe;
    xParEmitterAsset* a;
    xParEmitterPropsAsset* prop;
    xParSys* ps;
    xParGroup* g;
    S32 rate_has_elapsed;
    F32 rate;
    S32 count;
    F32 life;
    F32 size_birth;
    F32 size_death;
    xMat4x3 bone_mat;
    xVec3 oldloc;
    xVec3 loc;
    xVec3 vel;
    S32 i;
    xPar* p;
    S32 c;
    F32 fc1;
    F32 fc2;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    pe = fxrec->fxdata.pardata.emitter;
    dt = globals.update_dt;

    if (pe == NULL)
    {
        return;
    }

    ps = pe->parSys;
    a = pe->tasset;
    prop = pe->prop;

    if (ps == NULL)
    {
        return;
    }

    g = ps->group;

    if (g == NULL)
    {
        return;
    }

    if (a->emit_type != eParEmitterEntityBone)
    {
        return;
    }

    pe->rate_time += dt;
    rate_has_elapsed = pe->rate_time > prop->rate.freq;

    if (prop->rate.freq == 0.0f)
    {
        pe->rate_time = 0.0f;
    }

    while (pe->rate_time > prop->rate.freq)
    {
        pe->rate_time -= prop->rate.freq;
    }

    rate = xParInterpCompute(pe->rate_mode, &prop->rate, pe->rate_time, rate_has_elapsed, pe->rate);
    pe->rate = rate;
    pe->rate_fraction += rate * dt;
    pe->rate_fraction_cull += rate * dt;

    count = std::floorf(pe->rate_fraction);

    if (count <= 0)
    {
        return;
    }

    pe->rate_fraction -= count;

    if (ps->tasset->maxPar != 0)
    {
        if (g->m_num_of_particles >= ps->tasset->maxPar)
        {
            return;
        }

        if (ps->group->m_num_of_particles + count >= ps->tasset->maxPar)
        {
            count = ps->tasset->maxPar - ps->group->m_num_of_particles;
        }
    }

    bone_mat = *(xMat4x3*)&animMat[fxrec->index.idx_bone];
    if (fxrec->index.idx_bone != 0)
    {
        bone_mat.pos += ((xMat4x3*)&animMat[0])->pos;
    }

    oldloc = fxrec->basic.pos_B[0];
    xParEmitterTransformEntBone(loc, vel, *a, bone_mat);
    fxrec->basic.pos_B[0] = loc;
    vel *= dt;

    // not in the DWARF: the emitter velocity is set in two branches and then added.
    xVec3 emitvel;

    if (pe->emit_flags & 0x8)
    {
        emitvel = loc - oldloc;

        if (emitvel.length2() > 25.0f)
        {
            emitvel = 0.0f;
        }
    }
    else
    {
        emitvel = 0.0f;
    }

    for (i = 0; i < count; i++)
    {
        p = xParGroupAddPar(ps->group);

        if (p == NULL)
        {
            return;
        }

        life = xParInterpCompute(prop->life.interp, &prop->life, pe->rate_time, 1, 0.0f);
        size_birth =
            xParInterpCompute(prop->size_birth.interp, &prop->size_birth, pe->rate_time, 1, 0.0f);
        size_death =
            xParInterpCompute(prop->size_death.interp, &prop->size_death, pe->rate_time, 1, 0.0f);

        p->m_lifetime = life;
        p->totalLifespan = life;
        p->m_size = size_birth;
        p->m_sizeVel = (size_death - size_birth) / life;
        p->m_flag = 0;
        p->m_rotdeg[0] = pe->rot[0];
        p->m_rotdeg[1] = pe->rot[1];
        p->m_rotdeg[2] = pe->rot[2];

        for (c = 0; c < 4; c++)
        {
            fc1 = xParInterpCompute(prop->color_birth[c].interp, &prop->color_birth[c],
                                    pe->rate_time, 1, 0.0f);
            fc2 = xParInterpCompute(prop->color_death[c].interp, &prop->color_death[c],
                                    pe->rate_time, 1, 0.0f);

            p->m_cfl[c] = fc1;
            p->m_c[c] = fc1;
            p->m_cvel[c] = (fc2 - fc1) / life;
        }

        p->m_pos = loc;
        xParEmitterEmitSetTexIdxs(p, ps);
        p->m_vel = vel;
        xParEmitterEmitEntBone(p, a, dt, bone_mat);
        p->m_vel += emitvel;
    }
}

void zCinFX_StreakBone_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    NCINStreak* streak;

    if (fxrec->input.streak == NULL)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
        return;
    }

    streak = &fxrec->fxdata.streakdata;

    if (killit)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
        xFXStreakStop(streak->streakID);
        streak->streakID = 0xDEAD;
    }
    else if (fxrec->flg_stat & NCIN_STAT_FIRST)
    {
        streak->streakID = xFXStreakStart(
            fxrec->input.streak->streakInfo.freq, fxrec->input.streak->streakInfo.alf_fade,
            fxrec->input.streak->streakInfo.alf_start, fxrec->input.streak->streakInfo.idx_useTxtr,
            &fxrec->input.streak->streakInfo.rgba_left, &fxrec->input.streak->streakInfo.rgba_right,
            fxrec->input.streak->streakInfo.taper);
    }
}

void zCinFX_StreakBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 animIndex, U32)
{
    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    xMat4x3* mat_bone = (xMat4x3*)&animMat[fxrec->index.idx_bone];
    xMat4x3* mat_root = (xMat4x3*)&animMat[0];
    NCINStreakInput* streakIn = fxrec->input.streak;

    if (streakIn == NULL)
    {
        return;
    }

    xVec3 pos_a = mat_root->pos + mat_bone->pos;
    xVec3 pos_b = pos_a + mat_bone->at * streakIn->length;

    xFXStreakUpdate(fxrec->fxdata.streakdata.streakID, &pos_a, &pos_b);
}
