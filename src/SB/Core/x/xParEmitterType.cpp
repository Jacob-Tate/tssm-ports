#include "xParEmitterType.h"
#include "xParEmitter.h"
#include "xEnt.h"
#include "xMath.h"
#include "xMath2.h"
#include "xMath3.h"
#include "iMath.h"
#include "iModel.h"
#include "xVolume.h"
#include "xRand.h"

static void xParEmitterAngleVariation(xPar* p, xParEmitterAsset* a);

namespace
{
    xVec3 get_random_offset(const xBound& bound, F32 expand, U32 vtype);
    xVec3 get_random_offset(const xPEEntBone& region, const xMat4x3& mat);
    void transform_ent_bone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a, const xMat4x3& mat);
    void ocircle_emit(xPar& p, xParEmitterAsset& a, F32 dt, F32 radius);
} // namespace

void xParEmitterEmitPoint(xPar* p, xParEmitterAsset* a, F32 dt)
{
    p->m_vel = a->vel;

    xParEmitterAngleVariation(p, a);
}

static void xParEmitterAngleVariation(xPar* p, xParEmitterAsset* a)
{
    if (a->vel_angle_variation != 0.0f)
    {
        xMat3x3 mat_rot = { 0 };
        F32 ang_debrisCone = a->vel_angle_variation;
        F32 ang[3] = { 0.0f, 0.0f, 0.0f };

        if (a->emit_flags & 0x20)
        {
            ang[0] = a->vel_angle_variation * (xurand() - 0.5f);
        }
        else
        {
            ang[0] = ang_debrisCone * (xurand() - 0.5f);
            ang[1] = ang_debrisCone * (xurand() - 0.5f);
            ang[2] = ang_debrisCone * (xurand() - 0.5f);
        }

        xMat3x3Euler(&mat_rot, ang[0], ang[1], ang[2]);
        xMat3x3LMulVec(&p->m_vel, &mat_rot, &p->m_vel);
    }
}

void xParEmitterEmitCircleEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    p->m_vel = a->vel;

    F32 rot_amount = 6.2831855f * xurand();
    F32 ct = icos(rot_amount);
    F32 st = isin(rot_amount);

    xMat2x2 rot_mat = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    xVec2Init(&rot_mat.right, ct, st);
    xVec2Init(&rot_mat.up, -st, ct);

    xVec2 vec2 = { 0.0f, 1.0f };
    xVec2 new_vec;
    new_vec.x = xVec2Dot(&rot_mat.right, &vec2);
    new_vec.y = xVec2Dot(&rot_mat.up, &vec2);
    new_vec.x *= a->e_circle.radius;
    new_vec.y *= a->e_circle.radius;

    p->m_pos.x += new_vec.x;
    p->m_pos.z += new_vec.y;

    xParEmitterAngleVariation(p, a);

    if (a->e_circle.deflection != 0.0f)
    {
        F32 dt_radius = a->e_circle.radius * a->e_circle.deflection;

        p->m_vel.x += new_vec.x * dt_radius;
        p->m_vel.z += new_vec.y * dt_radius;
    }
}

void xParEmitterEmitCircle(xPar* p, xParEmitterAsset* a, F32 dt)
{
    p->m_vel = a->vel;

    F32 rot_amount = 6.2831855f * xurand();
    F32 ct = icos(rot_amount);
    F32 st = isin(rot_amount);

    xMat2x2 rot_mat = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    xVec2Init(&rot_mat.right, ct, st);
    xVec2Init(&rot_mat.up, -st, ct);

    xVec2 vec2 = { 0.0f, 1.0f };
    F32 rr = xurand();
    rr = 1.0f - rr * rr;
    vec2.y = rr * a->e_circle.radius;

    xVec2 new_vec;
    new_vec.x = xVec2Dot(&rot_mat.right, &vec2);
    new_vec.y = xVec2Dot(&rot_mat.up, &vec2);

    p->m_pos.x += new_vec.x;
    p->m_pos.z += new_vec.y;

    xParEmitterAngleVariation(p, a);

    if (a->e_circle.deflection != 0.0f)
    {
        F32 dt_radius = a->e_circle.radius * a->e_circle.deflection;

        p->m_vel.x += new_vec.x * dt_radius;
        p->m_vel.z += new_vec.y * dt_radius;
    }
}

void xParEmitterEmitRect(xPar* p, xParEmitterAsset* a, F32 dt)
{
    p->m_vel = a->vel;
    p->m_pos.x += 2.0f * (a->e_rect.x_len * xurand()) - a->e_rect.x_len;
    p->m_pos.z += 2.0f * (a->e_rect.z_len * xurand()) - a->e_rect.z_len;

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitRectEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 rand;
    F32 val;

    p->m_vel = a->vel;

    rand = xurand();
    val = a->e_rect.x_len;

    if (2.0f * (rand * val) > val)
    {
        p->m_pos.x += val;
    }
    else
    {
        p->m_pos.x -= val;
    }

    rand = xurand();
    val = a->e_rect.z_len;

    if (2.0f * (rand * val) >= val)
    {
        p->m_pos.z += val;
    }
    else
    {
        p->m_pos.z -= val;
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitLine(xPar* p, xParEmitterAsset* a, F32 dt)
{
    xVec3 vec;
    F32 len;
    F32 rand_len;
    F32 yaw;
    F32 pitch;

    p->m_vel = a->vel;

    xVec3Sub(&vec, &a->e_line.pos2, &a->e_line.pos1);
    len = xVec3Normalize(&vec, &vec);
    rand_len = len * xurand();

    p->m_pos.x = vec.x * rand_len + a->e_line.pos1.x;
    p->m_pos.y = vec.y * rand_len + a->e_line.pos1.y;
    p->m_pos.z = vec.z * rand_len + a->e_line.pos1.z;

    if (a->e_line.radius > 0.0f)
    {
        yaw = 6.2831855f * xurand();
        pitch = 6.2831855f * xurand();

        F32 sy = isin(yaw);
        F32 cy = icos(yaw);
        F32 sp = isin(pitch);
        F32 cp = icos(pitch);

        xVec3 dir = { -sy, cy * sp, cy * cp };

        F32 rr = xurand();
        p->m_pos += dir * (a->e_line.radius * (1.0f - rr * rr * rr));
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitSphere(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 yaw;
    F32 pitch;

    p->m_vel = a->vel;

    yaw = 6.2831855f * xurand();
    pitch = 6.2831855f * xurand();

    F32 sy = isin(yaw);
    F32 cy = icos(yaw);
    F32 sp = isin(pitch);
    F32 cp = icos(pitch);

    xVec3 dir = { -sy, cy * sp, cy * cp };

    p->m_pos += dir * (a->e_sphere.radius * xurand());

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitSphereEdge(xPar* p, xParEmitterAsset* a, F32 dt, S32 subtype)
{
    // Retail keeps this initializer's unreferenced { 0, 0, 1 } template; the value is dead
    // (dirvec = mat_rot.at).
    xVec3 dirvec = { 0.0f, 0.0f, 1.0f };
    xVec3 offset = { 0 };
    xMat3x3 mat_rot = { 0 };

    p->m_vel = a->vel;

    switch (subtype)
    {
    case 7:
        xMat3x3Euler(&mat_rot, 6.2831855f * xurand(), 6.2831855f * xurand(), 6.2831855f * xurand());
        break;
    case 10:
        xMat3x3Euler(&mat_rot, 6.2831855f * xurand(), 3.1415927f * xurand() + 3.1415927f, 0.0f);
        break;
    case 11:
        xMat3x3Euler(&mat_rot, 6.2831855f * xurand(), 3.1415927f * xurand(), 0.0f);
        break;
    }

    dirvec = mat_rot.at;

    xVec3SMul(&p->m_vel, &dirvec, xVec3Length(&p->m_vel));

    xVec3SMul(&offset, &dirvec, a->e_sphere.radius);
    xVec3AddTo(&p->m_pos, &offset);

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitVolume(xPar* p, xParEmitterAsset* a, F32 dt, xVolume* vol)
{
    xBound* b;

    p->m_vel = a->vel;

    if (vol != NULL)
    {
        b = vol->GetBound();

        if (b->type == XBOUND_TYPE_BOX)
        {
            xVec3 diff;
            xVec3Sub(&diff, &b->box.box.upper, &b->box.box.lower);
            diff.x *= xurand();
            diff.y *= xurand();
            diff.z *= xurand();
            xVec3Add(&p->m_pos, &diff, &b->box.box.lower);
        }
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitEntity(xPar* p, xParEmitterAsset* a, F32 dt, xEnt* ent)
{
    U32 size = 0;

    for (xModelInstance* model = ent->model; model; model = model->Next)
    {
        if (!(model->Flags & 0x8000))
        {
            size += model->Data->geometry->numVertices;
        }
    }

    xVec3 loc;
    U32 which = (xrand_RandomInt32() >> 13) % size;

    for (xModelInstance* model = ent->model; model; model = model->Next)
    {
        if (!(model->Flags & 0x8000))
        {
            U32 size = model->Data->geometry->numVertices;

            if (which < size)
            {
                iModelVertEval(model->Data, which, 1, model->Mat, NULL, &loc);
                break;
            }

            which -= size;
        }
    }

    p->m_pos = loc;
    p->m_vel = a->vel;

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitOffsetPoint(xParEmitter* pe, xPar* p, xParEmitterAsset* a, F32 dt, xEnt* ent)
{
    if (ent)
    {
        RpGeometry* geom = ent->model->Data->geometry;

        if (geom->morphTarget->verts)
        {
            iModelTagEval(ent->model->Data, &pe->tag, ent->model->Mat, &p->m_pos);
        }
    }
    else
    {
        p->m_pos += a->e_offsetp.offset;
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitVCylEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 ang = 6.2831855f * xurand();
    xVec2 dir = { isin(ang), icos(ang) };
    xVec2 offset = dir * a->e_vcyl.radius;

    p->m_pos.x += offset.x;
    p->m_pos.y += a->e_vcyl.height * xurand();
    p->m_pos.z += offset.y;

    F32 deflect = a->e_vcyl.deflection;

    p->m_vel.x += offset.x * deflect;
    p->m_vel.z += offset.y * deflect;

    xParEmitterAngleVariation(p, a);
}

// namespace

void xParEmitterEmitOCircleEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    ocircle_emit(*p, *a, dt, a->e_circle.radius);
}

namespace
{

    void ocircle_emit(xPar& p, xParEmitterAsset& a, F32 dt, F32 radius)
    {
        xMat3x3 mat;
        xMat3x3LookVec(&mat, &a.e_circle.dir);

        F32 theta = 6.2831855f * xurand();
        F32 ct = icos(theta);
        F32 st = isin(theta);

        xVec3 offset = { ct, st, 0.0f };
        offset *= radius;

        xMat3x3RMulVec(&offset, &mat, &offset);

        p.m_pos += offset;
        p.m_vel = a.e_circle.dir * a.vel.y;

        xParEmitterAngleVariation(&p, &a);

        if (a.e_circle.deflection != 0.0f)
        {
            p.m_vel += offset * (radius * a.e_circle.deflection);
        }
    }
} // namespace

void xParEmitterEmitOCircle(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 rr = xurand();
    rr = 1.0f - rr * rr;

    ocircle_emit(*p, *a, dt, rr * a->e_circle.radius);
}

// namespace

xMat4x3* xParEmitterTransformEntBone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a,
                                     const xEnt& ent)
{
    static xMat4x3 buffer_mat;
    const xPEEntBone& region = a.e_entbone;
    xMat4x3* root_mat = xModelGetFrame(ent.model);
    xMat4x3* mat;

    if (region.bone == 0)
    {
        mat = root_mat;
    }
    else
    {
        xMat4x3Mul(&buffer_mat, &root_mat[region.bone], root_mat);
        mat = &buffer_mat;
    }

    transform_ent_bone(loc, vel, a, *mat);

    return mat;
}

namespace
{

    void transform_ent_bone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a, const xMat4x3& mat)
    {
        const xPEEntBone& region = a.e_entbone;

        if (region.flags & 0x1)
        {
            xMat3x3RMulVec(&vel, &mat, &a.vel);
        }
        else
        {
            vel = a.vel;
        }

        if (region.flags & 0x2)
        {
            if (region.flags & 0x4)
            {
                xVec3 offset = { region.offset.x, 0.0f, region.offset.z };

                xMat4x3Toworld(&loc, &mat, &offset);
                loc.y = region.offset.y;
            }
            else
            {
                xMat4x3Toworld(&loc, &mat, &region.offset);
            }
        }
        else
        {
            loc = mat.pos + region.offset;

            if (region.flags & 0x4)
            {
                loc.y = region.offset.y;
            }
        }

        if (region.flags & 0x4)
        {
            loc.y = region.offset.y;
        }
    }
} // namespace

void xParEmitterTransformEntBone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a,
                                 const xMat4x3& mat)
{
    transform_ent_bone(loc, vel, a, mat);
}

namespace
{
    xVec3 get_random_offset(const xPEEntBone& region, const xMat4x3& mat)
    {
        xVec3 offset;

        switch (region.type)
        {
        case 0:
        {
            offset = 0.0f;
            break;
        }
        case 1:
        {
            F32 ang = 6.2831855f * xurand();
            F32 uz = 2.0f * xurand() - 1.0f;
            F32 r = xsqrt(1.0f - uz * uz);
            F32 rr = xurand();

            rr = 1.0f - rr * (rr * rr);
            rr *= region.radius;

            offset.assign(rr * r * icos(ang), rr * r * isin(ang), rr * uz);
            break;
        }
        case 2:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius * xsqrt(xurand());

            offset.assign(rr * isin(ang), 0.0f, rr * icos(ang));
            break;
        }
        case 3:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius * xsqrt(xurand());

            offset = mat.up * (rr * isin(ang)) + mat.at * (rr * icos(ang));
            break;
        }
        case 4:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius * xsqrt(xurand());

            offset = mat.right * (rr * isin(ang)) + mat.at * (rr * icos(ang));
            break;
        }
        case 5:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius * xsqrt(xurand());

            offset = mat.right * (rr * isin(ang)) + mat.up * (rr * icos(ang));
            break;
        }
        case 6:
        {
            F32 ang = 6.2831855f * xurand();
            F32 uz = 2.0f * xurand() - 1.0f;
            F32 r = region.radius * xsqrt(1.0f - uz * uz);

            offset.assign(r * icos(ang), r * isin(ang), uz * region.radius);
            break;
        }
        case 7:
        {
            F32 ang = 6.2831855f * xurand();

            offset.assign(region.radius * isin(ang), 0.0f, region.radius * icos(ang));
            break;
        }
        case 8:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius;

            offset = mat.up * (rr * isin(ang)) + mat.at * (rr * icos(ang));
            break;
        }
        case 9:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius;

            offset = mat.right * (rr * isin(ang)) + mat.at * (rr * icos(ang));
            break;
        }
        case 10:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rr = region.radius;

            offset = mat.right * (rr * isin(ang)) + mat.up * (rr * icos(ang));
            break;
        }
        }

        return offset;
    }
} // namespace

void xParEmitterEmitEntBone(xPar* p, xParEmitterAsset* a, F32 dt, const xMat4x3& mat)
{
    const xPEEntBone& region = a->e_entbone;
    xVec3 offset = get_random_offset(region, mat);

    p->m_pos += offset;

    if (region.deflection != 0.0f)
    {
        p->m_vel += offset * region.deflection;
    }

    xParEmitterAngleVariation(p, a);
}

namespace
{
    xVec3 get_random_offset(const xBound& bound, F32 expand, U32 vtype)
    {
        xVec3 offset;

        switch (bound.type | (vtype << 3))
        {
        case (0 << 3) | XBOUND_TYPE_SPHERE:
        {
            F32 ang = 6.2831855f * xurand();
            F32 uz = 2.0f * xurand() - 1.0f;
            F32 r = xsqrt(1.0f - uz * uz);
            F32 rr = xurand();

            rr = 1.0f - rr * (rr * rr);
            rr *= bound.sph.r + expand;

            offset.assign(rr * r * icos(ang), rr * r * isin(ang), rr * uz);
            break;
        }
        case (0 << 3) | XBOUND_TYPE_BOX:
        {
            offset = bound.box.box.upper - bound.box.center + expand;

            offset.x *= 2.0f * xurand() - 1.0f;
            offset.y *= 2.0f * xurand() - 1.0f;
            offset.z *= 2.0f * xurand() - 1.0f;
            break;
        }
        case (0 << 3) | XBOUND_TYPE_OBB:
        {
            offset = bound.box.box.upper + expand;

            offset.x *= 2.0f * xurand() - 1.0f;
            offset.y *= 2.0f * xurand() - 1.0f;
            offset.z *= 2.0f * xurand() - 1.0f;

            xMat3x3RMulVec(&offset, bound.mat, &offset);
            break;
        }
        case (1 << 3) | XBOUND_TYPE_SPHERE:
        {
            F32 ang = 6.2831855f * xurand();
            F32 uz = 2.0f * xurand() - 1.0f;
            F32 r = (bound.sph.r + expand) * xsqrt(1.0f - uz * uz);

            offset.assign(r * icos(ang), r * isin(ang), uz * (bound.sph.r + expand));
            break;
        }
        case (1 << 3) | XBOUND_TYPE_BOX:
        {
            U32 pick = xrand_RandomInt32() >> 13;
            U32 side = pick % 3;
            F32 sign = (pick & 0x4) ? 1.0f : -1.0f;
            xVec3 scale;

            if (side == 0)
            {
                scale.assign(sign, 2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f);
            }
            else if (side == 1)
            {
                scale.assign(2.0f * xurand() - 1.0f, sign, 2.0f * xurand() - 1.0f);
            }
            else
            {
                scale.assign(2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f, sign);
            }

            offset = (bound.box.box.upper - bound.box.center + expand) * scale;
            break;
        }
        case (1 << 3) | XBOUND_TYPE_OBB:
        {
            U32 pick = xrand_RandomInt32() >> 13;
            U32 side = pick % 3;
            F32 sign = (pick & 0x4) ? 1.0f : -1.0f;
            xVec3 scale;

            if (side == 0)
            {
                scale.assign(sign, 2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f);
            }
            else if (side == 1)
            {
                scale.assign(2.0f * xurand() - 1.0f, sign, 2.0f * xurand() - 1.0f);
            }
            else
            {
                scale.assign(2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f, sign);
            }

            offset = (bound.box.box.upper + expand) * scale;

            xMat3x3RMulVec(&offset, bound.mat, &offset);
            break;
        }
        }

        return offset;
    }
} // namespace

void xParEmitterEmitEntBound(xPar* p, xParEmitterAsset* a, F32 dt, const xEnt* ent)
{
    const xPEEntBound& region = a->e_entbound;
    const xMat4x3& mat = *xModelGetFrame(ent->model);

    if (region.flags & 0x1)
    {
        xMat3x3RMulVec(&p->m_vel, &mat, &a->vel);
    }
    else
    {
        p->m_vel = a->vel;
    }

    xVec3 offset = get_random_offset(ent->bound, region.expand, region.type);

    p->m_pos = mat.pos + offset;

    if (region.deflection != 0.0f)
    {
        p->m_vel += offset * region.deflection;
    }

    xParEmitterAngleVariation(p, a);
}
