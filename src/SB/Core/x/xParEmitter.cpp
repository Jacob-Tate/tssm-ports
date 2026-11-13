#include "xParEmitter.h"
#include "xParSys.h"
#include "zGlobals.h"
#include "xCam.h"
#include "iModel.h"
#include "xMathInlines.h"
#include "xstransvc.h"
#include "zScene.h"
#include "xGroup.h"
#include "xMarkerAsset.h"
#include "xMovePoint.h"
#include "xRand.h"
#include "zBase.h"
#include "xDebug.h"
#include "zAutoTweak.h"

static xParEmitterAsset sSaveEmmiterSettings;
static xParEmitterPropsAsset sSaveEmmiterPropSettings;
static xParEmitterPropsAsset sDummyProp;

// Interp modes may be given as small indices or as xStrHash of the mode name.
S32 xParInterpConvertInterpMode(xParInterp* p)
{
    if (p->interp < 8)
    {
        return p->interp;
    }

    if (p->interp == 0x48e48e7a) // xStrHash("ConstA")
    {
        return 0;
    }
    if (p->interp == 0x48e48e7b) // xStrHash("ConstB")
    {
        return 1;
    }
    if (p->interp == 0xb326f01) // xStrHash("Sine")
    {
        return 4;
    }
    if (p->interp == 0x498d7119) // xStrHash("Cosine")
    {
        return 5;
    }
    if (p->interp == 0xb7353b79) // xStrHash("Linear")
    {
        return 3;
    }
    if (p->interp == 0xb354bd4) // xStrHash("Step")
    {
        return 7;
    }
    if (p->interp == 0xfe111bf) // xStrHash("Random")
    {
        return 2;
    }

    return 0;
}

void xParEmitterInit(void* b, void* tasset)
{
    xParEmitterInit((xBase*)b, (xParEmitterAsset*)tasset);
}

void xParEmitterInit(xBase* b, xParEmitterAsset* pea)
{
    xParEmitterPropsAsset* prop;
    xParEmitter* t;
    S32 i;

    xBaseInit(b, pea);

    t = (xParEmitter*)b;
    t->eventFunc = xParEmitterEventCB;
    t->tasset = pea;

    t->prop = (xParEmitterPropsAsset*)xSTFindAsset(pea->propID, NULL);
    if (!t->prop)
    {
        t->prop = &sDummyProp;
    }

    if (t->linkCount)
    {
        t->link = (xLinkAsset*)(t->tasset + 1);
    }
    else
    {
        t->link = NULL;
    }

    prop = t->prop;
    t->emit_flags = pea->emit_flags;
    t->parSys = t->prop->parSysID ? (xParSys*)zSceneFindObject(t->prop->parSysID) : NULL;
    t->attachTo = NULL;
    t->emit_volume = NULL;

    for (i = 0; i < 14; i++)
    {
        if (prop->value[i].val[0] == prop->value[i].val[1])
        {
            prop->value[i].interp = 0;
        }
        else
        {
            prop->value[i].interp = xParInterpConvertInterpMode(&prop->value[i]);
        }
    }

    t->rate_mode = prop->rate.interp;
    if (t->rate_mode == 2 && prop->rate.val[0] == prop->rate.val[1])
    {
        t->rate_mode = 0;
    }

    if (t->rate_mode == 2)
    {
        prop->rate.order();
    }

    t->rate = (t->rate_mode == 2) ?
                  (prop->rate.val[1] - prop->rate.val[0]) * xurand() + prop->rate.val[0] :
                  prop->rate.val[0];
    t->rate_time = 0.0f;
    t->rate_fraction = 0.0f;
    t->rate_fraction_cull = 0.0f;

    t->oocull_distance_sqr = (pea->emit_flags & 0x2) ? 1.0f / pea->cull_dist_sqr : 0.0f;

    t->emit_volume = NULL;

    if (pea->emit_type == eParEmitterOCircle || pea->emit_type == eParEmitterOCircleEdge)
    {
        if (pea->e_circle.dir.length2() < 0.001f)
        {
            pea->e_circle.dir.assign(0.0f, 1.0f, 0.0f);
        }
        else
        {
            pea->e_circle.dir *= 1.0f / pea->e_circle.dir.length();
        }
    }

    t->last_attach_loc = 1e38f;
}

void xParEmitterSetup(xParEmitter* t)
{
    xEnt* ent;

    if (t->parSys)
    {
        t->group = t->parSys->group;
    }

    if (t->tasset->attachToID && t->tasset->emit_type == eParEmitterOffsetPoint)
    {
        ent = (xEnt*)zSceneFindObject(t->tasset->attachToID);
        iModelTagSetup(&t->tag, ent->model->Data, t->tasset->e_offsetp.offset.x,
                       t->tasset->e_offsetp.offset.y, t->tasset->e_offsetp.offset.z);
    }

    t->attachTo = t->tasset->attachToID ? zSceneFindObject(t->tasset->attachToID) : NULL;

    switch (t->tasset->emit_type)
    {
    case eParEmitterVolume:
        t->emit_volume = zSceneFindObject(t->tasset->e_volume.emit_volumeID);
        break;
    case eParEmitterEntityBone:
    case eParEmitterEntityBound:
        break;
    }
}

void xParEmitterEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, U32 toParamWidgetID)
{
    xParEmitter* t = (xParEmitter*)to;
    xParEmitterCustomSettings fake_settings;

    switch (toEvent)
    {
    case eEventReset:
        xBaseReset(t, t->tasset);
        t->emit_flags = t->tasset->emit_flags;
        break;
    case eEventOn:
        t->emit_flags |= 0x1;
        break;
    case eEventOff:
        t->emit_flags &= ~0x1;
        break;
    case eEventEmit:
        memset(&fake_settings, 0, sizeof(xParEmitterCustomSettings));
        xParEmitterEmitCustom(t, 1.0f / 30.0f, &fake_settings);
        break;
    }
}

xPar* xParEmitterEmitCustom(xParEmitter* p, F32 dt, xParEmitterCustomSettings* info)
{
    U32 flags;
    xParEmitterAsset* pe;

    pe = p->tasset;
    flags = info->custom_flags;

    if (!pe)
    {
        return NULL;
    }

    if (flags & 0x1)
    {
        memcpy(&sSaveEmmiterSettings, pe, sizeof(xParEmitterAsset));
        memcpy(&sSaveEmmiterPropSettings, p->prop, sizeof(xParEmitterPropsAsset));
    }

    if (flags & 0x100)
    {
        pe->pos = info->pos;
        p->last_emit_position = info->last_emit_position;
    }

    if (flags & 0x10)
    {
        p->prop->rate = info->rate;
    }

    if (flags & 0x2)
    {
        p->prop->life = info->life;
    }

    if (flags & 0x2000)
    {
        p->emit_volume = info->emit_volume;
    }

    if (flags & 0x4)
    {
        p->prop->size_birth = info->size_birth;
    }

    if (flags & 0x8)
    {
        p->prop->size_death = info->size_death;
    }

    if (flags & 0x200)
    {
        pe->vel = info->vel;
        p->last_emit_velocity = info->last_emit_velocity;
        p->prop->vel = info->vel;
    }

    if (flags & 0x40)
    {
        pe->vel_angle_variation = info->vel_angle_variation;
    }

    if (flags & 0x400)
    {
        p->prop->color_birth[0] = info->color_birth[0];
        p->prop->color_birth[1] = info->color_birth[1];
        p->prop->color_birth[2] = info->color_birth[2];
        p->prop->color_birth[3] = info->color_birth[3];
    }

    if (flags & 0x800)
    {
        p->prop->color_death[0] = info->color_death[0];
        p->prop->color_death[1] = info->color_death[1];
        p->prop->color_death[2] = info->color_death[2];
        p->prop->color_death[3] = info->color_death[3];
    }

    if (flags & 0x1000)
    {
        switch (p->tasset->emit_type)
        {
        case eParEmitterCircleEdge:
        case eParEmitterCircle:
        case eParEmitterOCircleEdge:
        case eParEmitterOCircle:
            p->tasset->e_circle.radius = info->radius;
            break;
        case eParEmitterSphereEdge1:
        case eParEmitterSphere:
        case eParEmitterSphereEdge2:
        case eParEmitterSphereEdge3:
            p->tasset->e_sphere.radius = info->radius;
            break;
        }
    }

    xPar* emitted_par = xParEmitterEmit(p, dt, dt);

    if (flags & 0x1)
    {
        memcpy(pe, &sSaveEmmiterSettings, sizeof(xParEmitterAsset));
        memcpy(p->prop, &sSaveEmmiterPropSettings, sizeof(xParEmitterPropsAsset));
    }

    return emitted_par;
}

// No GC symbol (always inlined); a static function in the Ratatouille GC DWARF.
static inline S32 xParEmitterCull(xParEmitter* pe, xPar* p)
{
    xParEmitterAsset* a = pe->tasset;

    if (a->emit_flags & 0x2)
    {
        xVec3 camera_pos = xglobals->cam->mat.pos;
        xVec3 d;
        xVec3Sub(&d, &camera_pos, &p->m_pos);
        F32 dmag = xVec3Length2(&d);

        pe->distance_to_cull_sqr = dmag;

        if (a->cull_mode == 3)
        {
            return dmag > a->cull_dist_sqr;
        }
        else
        {
            return dmag < a->cull_dist_sqr;
        }
    }
    else
    {
        return 0;
    }
}

// The compiled-out asserts (Ratatouille's case-3 one and three placeholders with unknown
// conditions) keep xParEmitterEmit from inlining this and xParGroupKillPar, as retail calls both.
F32 xParInterpCompute(S32 interp_mode, xParInterp* r, F32 time, S32 time_has_elapsed, F32 lastVal)
{
    F32 val = 0.0f;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    switch (interp_mode)
    {
    case 0:
        val = r->val[0];
        break;
    case 1:
        val = r->val[1];
        break;
    case 2:
        if (time_has_elapsed)
        {
            val = (r->val[1] - r->val[0]) * xurand() + r->val[0];
        }
        else
        {
            val = lastVal;
        }
        break;
    case 3:
        xASSERT(r->freq > 0.0f);
        if (r->freq < 0.00001f)
        {
            val = r->val[0];
        }
        else
        {
            val = (r->val[1] - r->val[0]) * (time / r->freq) + r->val[0];
        }
        break;
    case 4:
        val = (r->val[1] - r->val[0]) * (0.5f * isin(6.2831855f * (time * r->oofreq)) + 0.5f) +
              r->val[0];
        break;
    case 5:
        val = (r->val[1] - r->val[0]) * (0.5f * icos(6.2831855f * (time * r->oofreq)) + 0.5f) +
              r->val[0];
        break;
    case 7:
        if (time * r->freq >= 0.5f)
        {
            val = r->val[1];
        }
        else
        {
            val = r->val[0];
        }
        break;
    default:
        xASSERT_PLACEHOLDER();
    }

    return val;
}

xPar* xParEmitterEmitSetTexIdxs(xPar* p, const xParSys* ps)
{
    xParCmdTex* tex = ps->group->m_cmdTex;

    if (!tex)
    {
        return NULL;
    }

    switch (tex->birthMode)
    {
    case 1:
        p->m_texIdx[0] = (xrand_RandomInt32() >> 17) % tex->cols;
        p->m_texIdx[1] = (xrand_RandomInt32() >> 17) % tex->rows;
        break;
    case 0:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        p->m_texIdx[0] = 0;
        p->m_texIdx[1] = 0;
        break;
    }

    return p;
}

// The seven compiled-out asserts are Ratatouille's; with the four placeholders (conditions
// unknown) they push this function past CW's inline total, so it calls xParInterpCompute and
// xParGroupKillPar out of line, as retail does.
xPar* xParEmitterEmit(xParEmitter* pe, F32 emit_dt, F32 par_dt)
{
    xPar* last_p;
    xParEmitterAsset* pea;
    xParEmitterAsset* a;
    xParEmitterPropsAsset* prop;
    S32 rate_has_elapsed;
    F32 rate;
    xParInterp* r;
    S32 count;
    xParSys* ps;
    xParGroup* g;
    xPar* p;
    F32 life;
    F32 size_birth;
    F32 size_death;
    xVec3 emitPosition;
    U32 attachGroupIndex;
    U32 attachGroupTotal;
    S32 emitAgain;
    S32 emitterIsAttachedToObject;
    // Not in the DWARF order (attachObject follows emitPosition there): declaring it there changes
    // the generated code.
    xBase* attachObject;
    xBase* emitObj;
    S32 marker;
    U32 get_rnd_group_idx;
    xEnt* attach_ent;
    xMat4x3* bone_mat;
    xVec3 bone_vel;
    xVec3 add_vel;
    F32 last_emit_time;
    S32 i;
    S32 c;
    F32 fc1;
    F32 fc2;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (!pe->parSys)
    {
        return NULL;
    }

    last_p = NULL;

    xASSERT(pe);

    pea = pe->tasset;
    a = pea;
    prop = pe->prop;

    pe->rate_time += emit_dt;
    rate_has_elapsed = pe->rate_time > prop->rate.freq;

    if (prop->rate.freq == 0.0f)
    {
        pe->rate_time = 0.0f;
    }

    xASSERT(prop->rate.freq >= 0.0f);

    while (pe->rate_time > prop->rate.freq)
    {
        pe->rate_time -= prop->rate.freq;
    }

    r = &prop->rate;
    rate = xParInterpCompute(pe->rate_mode, r, pe->rate_time, rate_has_elapsed, pe->rate);
    pe->rate = rate;
    pe->rate_fraction += rate * emit_dt;
    pe->rate_fraction_cull += rate * emit_dt;

    count = std::floorf(pe->rate_fraction);

    if (count > 0)
    {
        pe->rate_fraction -= count;
    }

    if (count == 0)
    {
        return NULL;
    }

    xASSERT(count < 0xFFFF);

    ps = pe->parSys;

    if (!ps)
    {
        xASSERT(ps);
        return NULL;
    }

    g = ps->group;

    if (!g)
    {
        return NULL;
    }

    if (ps->tasset->maxPar && g->m_num_of_particles >= ps->tasset->maxPar)
    {
        return NULL;
    }

    if (ps->tasset->maxPar && ps->group->m_num_of_particles >= ps->tasset->maxPar)
    {
        return NULL;
    }

    emitPosition = a->pos;

    if (ps->tasset->maxPar && ps->group->m_num_of_particles + count >= ps->tasset->maxPar)
    {
        count = ps->tasset->maxPar - ps->group->m_num_of_particles;
    }

    emitObj = (xBase*)pe->attachTo;
    attachGroupTotal = (U32)-1;
    attachGroupIndex = (U32)-1;
    emitterIsAttachedToObject = 0;

    do
    {
        emitAgain = 0;
        attachObject = NULL;
        marker = 0;

        if (emitObj)
        {
            if (emitObj->baseType == eBaseTypeGroup)
            {
                xGroup* g = (xGroup*)emitObj;

                if (attachGroupIndex == (U32)-1)
                {
                    attachGroupTotal = xGroupGetCount(g);
                    attachGroupIndex = 0;
                }

                xASSERT(g->asset);

                if (g->asset->groupFlags & 0x1)
                {
                    get_rnd_group_idx = xrand_RandomChoice(attachGroupTotal);
                    attachObject = xGroupGetItemPtr(g, get_rnd_group_idx);
                    emitAgain = 0;

                    if (!attachObject)
                    {
                        attachObject =
                            (xBase*)xSTFindAsset(xGroupGetItem(g, get_rnd_group_idx), NULL);

                        if (attachObject)
                        {
                            marker = 1;
                        }
                    }
                }
                else
                {
                    attachObject = xGroupGetItemPtr(g, attachGroupIndex);

                    if (!attachObject)
                    {
                        attachObject =
                            (xBase*)xSTFindAsset(xGroupGetItem(g, attachGroupIndex), NULL);

                        if (attachObject)
                        {
                            marker = 1;
                        }
                    }

                    attachGroupIndex++;
                    emitAgain = attachGroupIndex < attachGroupTotal ? 1 : 0;
                }
            }
            else
            {
                attachObject = emitObj;
            }
        }

        attach_ent = (xEnt*)attachObject;

        if (attachObject)
        {
            emitterIsAttachedToObject = 1;

            if (marker)
            {
                // The group item had no object, only an asset (a marker) found by xSTFindAsset.
                emitPosition = ((xMarkerAsset*)attachObject)->pos;
                attach_ent = NULL;
            }
            else
            {
                switch (attachObject->baseType)
                {
                case eBaseTypeMovePoint:
                    emitPosition = *((xMovePoint*)attachObject)->pos;
                    attach_ent = NULL;
                    break;
                default:
                    if (!xEntValidType(attachObject->baseType) || !attach_ent->model)
                    {
                        emitPosition = 0.0f;
                        attach_ent = NULL;
                    }
                    else
                    {
                        emitPosition = *xEntGetPos(attach_ent);
                    }
                    break;
                }
            }
        }
        else
        {
            emitPosition = a->pos;
        }

        if (!(pe->emit_flags & 0x10) || !attach_ent || xEntIsVisible(attach_ent))
        {
            bone_mat = NULL;

            if (a->emit_type == eParEmitterEntityBone && attach_ent)
            {
                bone_mat = xParEmitterTransformEntBone(emitPosition, bone_vel, *a, *attach_ent);
                bone_vel *= par_dt;
            }

            if (pe->emit_flags & 0x8)
            {
                add_vel = emitPosition - pe->last_attach_loc;
                pe->last_attach_loc = emitPosition;

                if (add_vel.length2() > 25.0f)
                {
                    add_vel = 0.0f;
                }
            }
            else
            {
                add_vel = 0.0f;
            }

            last_emit_time = -emit_dt;

            for (i = 0; i < count; i++)
            {
                p = xParGroupAddPar(ps->group);

                if (p)
                {
                    last_p = p;

                    life =
                        xParInterpCompute(prop->life.interp, &prop->life, pe->rate_time, 1, 0.0f);
                    size_birth = xParInterpCompute(prop->size_birth.interp, &prop->size_birth,
                                                   pe->rate_time, 1, 0.0f);
                    size_death = xParInterpCompute(prop->size_death.interp, &prop->size_death,
                                                   pe->rate_time, 1, 0.0f);

                    p->m_lifetime = life;
                    p->totalLifespan = life;
                    p->m_size = size_birth;

                    if (size_death == size_birth)
                    {
                        p->m_sizeVel = 0.0f;
                    }
                    else
                    {
                        p->m_sizeVel = (size_death - size_birth) / life;
                    }

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

                    p->m_pos = emitPosition;
                    xParEmitterEmitSetTexIdxs(p, ps);

                    switch (a->emit_type)
                    {
                    case eParEmitterPoint:
                        xParEmitterEmitPoint(p, a, par_dt);
                        break;
                    case eParEmitterCircleEdge:
                        xParEmitterEmitCircleEdge(p, a, par_dt);
                        break;
                    case eParEmitterCircle:
                        xParEmitterEmitCircle(p, a, par_dt);
                        break;
                    case eParEmitterRectEdge:
                        xParEmitterEmitRectEdge(p, a, par_dt);
                        break;
                    case eParEmitterRect:
                        xParEmitterEmitRect(p, a, par_dt);
                        break;
                    case eParEmitterLine:
                        xParEmitterEmitLine(p, a, par_dt);
                        break;
                    case eParEmitterSphereEdge1:
                        xParEmitterEmitSphereEdge(p, a, par_dt, eParEmitterSphereEdge1);
                        break;
                    case eParEmitterSphereEdge2:
                        xParEmitterEmitSphereEdge(p, a, par_dt, eParEmitterSphereEdge2);
                        break;
                    case eParEmitterSphereEdge3:
                        xParEmitterEmitSphereEdge(p, a, par_dt, eParEmitterSphereEdge3);
                        break;
                    case eParEmitterSphere:
                        xParEmitterEmitSphere(p, a, par_dt);
                        break;
                    case eParEmitterVolume:
                    {
                        xBase* obj = pe->emit_volume;

                        if (obj)
                        {
                            if (obj->baseType == eBaseTypeVolume)
                            {
                                xParEmitterEmitVolume(p, a, par_dt, (xVolume*)obj);
                            }
                            else
                            {
                                xASSERT(xEntValidType(obj->baseType));
                                xParEmitterEmitEntity(p, a, par_dt, (xEnt*)obj);
                            }
                        }
                        break;
                    }
                    case eParEmitterOffsetPoint:
                        xParEmitterEmitOffsetPoint(pe, p, a, par_dt, (xEnt*)attachObject);
                        break;
                    case eParEmitterVCylEdge:
                        xParEmitterEmitVCylEdge(p, a, par_dt);
                        break;
                    case eParEmitterOCircleEdge:
                        xParEmitterEmitOCircleEdge(p, a, par_dt);
                        break;
                    case eParEmitterOCircle:
                        xParEmitterEmitOCircle(p, a, par_dt);
                        break;
                    case eParEmitterEntityBone:
                        if (bone_mat)
                        {
                            p->m_vel = bone_vel;
                            xParEmitterEmitEntBone(p, a, par_dt, *bone_mat);
                        }
                        break;
                    case eParEmitterEntityBound:
                        if (attach_ent)
                        {
                            xParEmitterEmitEntBound(p, a, par_dt, attach_ent);
                        }
                        break;
                    }

                    // Spread the particles over the frame: each new particle is aged by its share
                    // of emit_dt, alone in a temporary group run through the system's commands.
                    {
                        U8 tempGroupInitialized = 0;
                        xPar* save_next;
                        F32 u;
                        xVec3 pos_diff;
                        F32 initial_length;
                        static xParGroup temp_group;

                        last_emit_time += emit_dt / count;

                        xASSERT(last_emit_time <= 0.0001f);

                        if (!tempGroupInitialized)
                        {
                            xParGroupInit(&temp_group);
                            tempGroupInitialized = 1;
                        }

                        // Retail overwrites the previous emit position/velocity with the new
                        // particle's own before interpolating towards them below.
                        pe->last_emit_position = p->m_pos;
                        pe->last_emit_velocity = p->m_vel;

                        temp_group.m_root = NULL;
                        save_next = p->m_next;
                        xParGroupAddParP(&temp_group, p);

                        u = -last_emit_time / emit_dt;

                        if (emitterIsAttachedToObject)
                        {
                            pos_diff = p->m_pos - emitPosition;
                            p->m_pos =
                                pe->last_emit_position * u + emitPosition * (1.0f - u) + pos_diff;
                        }
                        else
                        {
                            pos_diff = p->m_pos - a->pos;
                            p->m_pos = pe->last_emit_position * u + a->pos * (1.0f - u) + pos_diff;
                        }

                        initial_length = p->m_vel.length();
                        p->m_vel = p->m_vel * (1.0f - u) + pe->last_emit_velocity * u;

                        if (initial_length > 0.00001f)
                        {
                            p->m_vel /= initial_length;
                        }

                        p->m_vel *=
                            initial_length * (1.0f - u) + u * pe->last_emit_velocity.length();

                        p->m_lifetime += last_emit_time;

                        if (p->m_lifetime < 0.0f)
                        {
                            p->m_next = save_next;
                            xParGroupKillPar(ps->group, p);
                        }
                        else
                        {
                            xParGroupUpdate(ps, &temp_group, -last_emit_time);

                            p->m_next = save_next;
                            p->m_vel += add_vel;

                            if (xParEmitterCull(pe, p))
                            {
                                xParGroupKillPar(ps->group, p);
                            }
                        }
                    }
                }
                else
                {
                    emitAgain = 0;
                }
            }
        }
    } while (emitAgain);

    pe->last_emit_position = emitPosition;
    pe->last_emit_velocity = a->vel;

    return last_p;
}

void xParEmitterUpdate(xBase* to, xScene*, F32 dt)
{
    xParEmitter* pe = (xParEmitter*)to;

    if (pe->parSys && (pe->emit_flags & 0x1))
    {
        xParEmitterEmit(pe, dt, dt);
    }
}

void xParEmitterDestroy()
{
}
