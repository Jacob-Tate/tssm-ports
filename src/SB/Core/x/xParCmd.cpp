#include "xParCmd.h"
#include "xParGroup.h"
#include "xRand.h"
#include "xMathInlines.h"

struct xCmdInfo
{
    U32 type;
    U32 size;
    xParCmdUpdateFunc func;
};

static xCmdInfo sCmdInfo[XPARCMD_TYPE_COUNT] = {};

void xParCmdInit()
{
    xParCmdRegister(XPARCMD_TYPE_MOVE, sizeof(xParCmdMove), xParCmdMove_Update);
    xParCmdRegister(XPARCMD_TYPE_MOVERANDOM, sizeof(xParCmdMove), xParCmdMoveRandom_Update);
    xParCmdRegister(XPARCMD_TYPE_ACCELERATE, sizeof(xParCmdAccelerate), xParCmdAccelerate_Update);
    xParCmdRegister(XPARCMD_TYPE_VELOCITYAPPLY, sizeof(xParCmdAsset), xParCmdVelocityApply_Update);
    xParCmdRegister(XPARCMD_TYPE_KILLOLD, sizeof(xParCmdKillOld), NULL);
    xParCmdRegister(XPARCMD_TYPE_KILLSLOW, sizeof(xParCmdKillSlow), xParCmdKillSlow_Update);
    xParCmdRegister(XPARCMD_TYPE_FOLLOW, sizeof(xParCmdFollow), xParCmdFollow_Update);
    xParCmdRegister(XPARCMD_TYPE_ORBITPOINT, sizeof(xParCmdOrbitPoint), xParCmdOrbitPoint_Update);
    xParCmdRegister(XPARCMD_TYPE_ORBITLINE, sizeof(xParCmdOrbitLine), xParCmdOrbitLine_Update);
    xParCmdRegister(XPARCMD_TYPE_MOVERANDOMPAR, sizeof(xParCmdMoveRandomPar),
                    xParCmdMoveRandomPar_Update);
    xParCmdRegister(XPARCMD_TYPE_SCALE3RDPOLYREG, sizeof(xParCmdScale3rdPolyReg),
                    xParCmdScale3rdPolyReg_Update);
    xParCmdRegister(XPARCMD_TYPE_TEX, sizeof(xParCmdTex), xParCmdTex_Update);
    xParCmdRegister(XPARCMD_TYPE_TEXANIM, sizeof(xParCmdTexAnim), xParCmdTexAnim_Update);
    xParCmdRegister(XPARCMD_TYPE_RANDOMVELOCITYPAR, sizeof(xParCmdRandomVelocityPar),
                    xParCmdRandomVelocityPar_Update);
    xParCmdRegister(XPARCMD_TYPE_AGE, sizeof(xParCmdAge), xParCmdAge_Update);
    xParCmdRegister(XPARCMD_TYPE_ALPHA3RDPOLYREG, sizeof(xParCmdAlpha3rdPolyReg),
                    xParCmdAlpha3rdPolyReg_Update);
    xParCmdRegister(XPARCMD_TYPE_APPLYWIND, sizeof(xParCmdApplyWind), xParCmdApplyWind_Update);
    xParCmdRegister(XPARCMD_TYPE_ROTPAR, sizeof(xParCmdRotPar), xParCmdRotPar_Update);
    xParCmdRegister(XPARCMD_TYPE_ROTATEAROUND, sizeof(xParCmdRotateAround),
                    xParCmdRotateAround_Update);
    xParCmdRegister(XPARCMD_TYPE_SMOKEALPHA, sizeof(xParCmdSmokeAlpha), xParCmdSmokeAlpha_Update);
    xParCmdRegister(XPARCMD_TYPE_SCALE, sizeof(xParCmdScale), xParCmdScale_Update);
    xParCmdRegister(XPARCMD_TYPE_COLLIDEFALL, sizeof(xParCmdCollideFall),
                    xParCmdCollideFall_Update);
    xParCmdRegister(XPARCMD_TYPE_COLLIDEFALLSTICKY, sizeof(xParCmdCollideFallSticky),
                    xParCmdCollideFallSticky_Update);
    xParCmdRegister(XPARCMD_TYPE_SHAPER, sizeof(xParCmdShaperData), xParCmd_Shaper_Update);
    xParCmdRegister(XPARCMD_TYPE_ALPHAINOUT, sizeof(xParCmdAlphaInOutData),
                    xParCmd_AlphaInOut_Update);
    xParCmdRegister(XPARCMD_TYPE_SIZEINOUT, sizeof(xParCmdSizeInOutData), xParCmd_SizeInOut_Update);
    xParCmdRegister(XPARCMD_TYPE_DAMPENSPEED, sizeof(xParCmdDampenData),
                    xParCmd_DampenSpeed_Update);
}

void xParCmdRegister(U32 parType, U32 size, xParCmdUpdateFunc func)
{
    sCmdInfo[parType].type = parType;
    sCmdInfo[parType].size = size;
    sCmdInfo[parType].func = func;
}

// No symbol in the retail DOL: always inlined (xParSysInit).
inline U32 xParCmdGetSize(U32 parType)
{
    for (S32 i = 0; i < XPARCMD_TYPE_COUNT; i++)
    {
        if (sCmdInfo[i].type == parType)
        {
            return sCmdInfo[i].size;
        }
    }

    return 0;
}

xParCmdUpdateFunc xParCmdGetUpdateFunc(U32 parType)
{
    for (S32 i = 0; i < XPARCMD_TYPE_COUNT; i++)
    {
        if (sCmdInfo[i].type == parType)
        {
            return sCmdInfo[i].func;
        }
    }

    return NULL;
}

void xParCmdKillSlow_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdKillSlow* cmd = (xParCmdKillSlow*)c->tasset;
    F32 speedLimit = cmd->speedLimitSqr * dt;

    if (cmd->kill_less_than)
    {
        while (p)
        {
            if (xVec3Length2(&p->m_vel) < speedLimit)
            {
                p->m_lifetime = -1.0f;
            }

            p = p->m_next;
        }
    }
    else
    {
        while (p)
        {
            if (xVec3Length2(&p->m_vel) > speedLimit)
            {
                p->m_lifetime = -1.0f;
            }

            p = p->m_next;
        }
    }
}

void xParCmdAge_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    F32 age_rate = ((xParCmdAge*)c->tasset)->age_rate * dt;

    while (p)
    {
        p->m_lifetime -= age_rate;
        p = p->m_next;
    }
}

void xParCmdFollow_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdFollow* cmd = (xParCmdFollow*)c->tasset;
    F32 mdt = cmd->gravity * dt;

    while (p && p->m_next)
    {
        xVec3 delta;

        xVec3Sub(&delta, &p->m_next->m_pos, &p->m_pos);

        F32 lenSqr2 = xVec3Length2(&delta);
        F32 lenSqr = xVec3LengthFast(delta.x, delta.y, delta.z);

        F32 force = mdt / (lenSqr * (lenSqr2 + cmd->epsilon));

        p->m_vel.x += delta.x * force;
        p->m_vel.y += delta.y * force;
        p->m_vel.z += delta.z * force;

        p = p->m_next;
    }
}

void xParCmdOrbitPoint_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdOrbitPoint* cmd = (xParCmdOrbitPoint*)c->tasset;
    F32 mdt = cmd->gravity * dt;

    while (p)
    {
        xVec3 dir;

        xVec3Sub(&dir, &cmd->center, &p->m_pos);

        F32 rSqr2 = xVec3Length2(&dir);

        if (rSqr2 < cmd->maxRadiusSqr)
        {
            F32 rSqr = xVec3LengthFast(dir.x, dir.y, dir.z);

            F32 force = mdt / (rSqr + (rSqr2 + cmd->epsilon));

            p->m_vel.x += dir.x * force;
            p->m_vel.y += dir.y * force;
            p->m_vel.z += dir.z * force;
        }

        p = p->m_next;
    }
}

void xParCmdOrbitLine_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdOrbitLine* cmd = (xParCmdOrbitLine*)c->tasset;
    F32 mdt = cmd->gravity * dt;

    while (p)
    {
        xVec3 f, w, tmp, into;

        xVec3Sub(&f, &p->m_pos, &cmd->p);
        xVec3Cross(&tmp, &f, &cmd->axis);
        xVec3Cross(&w, &cmd->axis, &tmp);
        xVec3Sub(&into, &w, &f);

        F32 rSqr2 = xVec3Length2(&into);

        if (rSqr2 < cmd->maxRadiusSqr)
        {
            F32 rSqr = xVec3LengthFast(into.x, into.y, into.z);

            F32 force = mdt / (rSqr + (rSqr2 + cmd->epsilon));

            p->m_vel.x += into.x * force;
            p->m_vel.y += into.y * force;
            p->m_vel.z += into.z * force;
        }

        p = p->m_next;
    }
}

void xParCmdAccelerate_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdAccelerate* cmd = (xParCmdAccelerate*)c->tasset;

    xVec3 delta;
    delta = cmd->acc;

    xVec3SMulBy(&delta, dt);

    p = ps->m_root;

    while (p)
    {
        p->m_vel.x += delta.x;
        p->m_vel.y += delta.y;
        p->m_vel.z += delta.z;

        p = p->m_next;
    }
}

void xParCmdMove_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdMove* cmd = (xParCmdMove*)c->tasset;

    xVec3 delta;
    delta = cmd->dir;

    xVec3SMulBy(&delta, dt);

    p = ps->m_root;

    while (p)
    {
        xVec3Add(&p->m_pos, &p->m_pos, &delta);

        p = p->m_next;
    }
}

void xParCmdMoveRandom_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdMove* cmd = (xParCmdMove*)c->tasset;

    xVec3 delta;
    delta = cmd->dir;

    delta.x *= 2.0f * xurand();
    delta.y *= 2.0f * xurand();
    delta.z *= 2.0f * xurand();

    xVec3Sub(&delta, &delta, &cmd->dir);
    xVec3SMulBy(&delta, dt);

    p = ps->m_root;

    while (p)
    {
        xVec3Add(&p->m_pos, &p->m_pos, &delta);

        p = p->m_next;
    }
}

// The two scale locals are not in the PS2 DWARF, but retail computes both dim scales once
// before the loop.
void xParCmdMoveRandomPar_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdMoveRandomPar* cmd = (xParCmdMoveRandomPar*)c->tasset;
    F32 xscale = cmd->dim.x * (dt / 2.0f);
    F32 zscale = cmd->dim.z * (dt / 2.0f);

    while (p)
    {
        p->m_pos.x += xscale * (xurand() - 0.5f);
        p->m_pos.z += zscale * (xurand() - 0.5f);

        p = p->m_next;
    }
}

void xParCmdScale3rdPolyReg_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdSmokeAlpha_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdScale_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdAlpha3rdPolyReg_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdRandomVelocityPar_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdRandomVelocityPar* cmd = (xParCmdRandomVelocityPar*)c->tasset;
    xVec3 rot;

    rot.x = cmd->x * dt;
    rot.y = cmd->y * dt;
    rot.z = cmd->z * dt;

    while (p)
    {
        xMat3x3 rotmat;

        F32 y = 2.0f * (rot.x * xurand()) - rot.x;
        F32 x = 2.0f * (rot.y * xurand()) - rot.y;
        F32 z = 2.0f * (rot.z * xurand()) - rot.z;

        xMat3x3Euler(&rotmat, x, y, z);
        xMat3x3LMulVec(&p->m_vel, &rotmat, &p->m_vel);

        p = p->m_next;
    }
}

void xParCmdApplyWind_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdApplyWind* cmd = (xParCmdApplyWind*)c->tasset;
    xVec3 wind_vec;

    wind_vec.x = 1.0f;
    wind_vec.y = 0.0f;
    wind_vec.z = 1.0f;
    xVec3SMulBy(&wind_vec, cmd->strength * dt);

    p = ps->m_root;

    while (p)
    {
        p->m_vel.x += wind_vec.x;
        p->m_vel.z += wind_vec.z;

        p = p->m_next;
    }
}

void xParCmdRotPar_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdRotPar* cmd = (xParCmdRotPar*)c->tasset;
    xVec3 range;
    xVec3 min;

    range.x = 255.0f * ((cmd->max.x - cmd->min.x) / 360.0f);
    range.y = 255.0f * ((cmd->max.y - cmd->min.y) / 360.0f);
    range.z = 255.0f * ((cmd->max.z - cmd->min.z) / 360.0f);

    min.x = 255.0f * (cmd->min.x / 360.0f);
    min.y = 255.0f * (cmd->min.y / 360.0f);
    min.z = 255.0f * (cmd->min.z / 360.0f);

    while (p)
    {
        p->m_rotdeg[0] += dt * (range.x * xurand() + min.x);
        p->m_rotdeg[1] += dt * (range.y * xurand() + min.y);
        p->m_rotdeg[2] += dt * (range.z * xurand() + min.z);

        p = p->m_next;
    }
}

void xParCmdVelocityApply_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;

    while (p)
    {
        // vel is not in the PS2 DWARF, but retail stores a second, dead stack copy of the scaled
        // velocity, which this local reproduces.
        xVec3 vel;
        vel = p->m_vel * dt;
        p->m_pos += vel;

        p = p->m_next;
    }
}

void xParCmdRotateAround_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdRotateAround* cmd = (xParCmdRotateAround*)c->tasset;

    F32 yaw = (PI / 180.0f) * (dt * cmd->yaw);
    F32 radius_growth = dt * cmd->radius_growth;

    while (p)
    {
        xVec3 at;
        xVec3Sub(&at, &cmd->pos, &p->m_pos);

        at.y = 0.0f;

        xMat3x3 lookmat;

        F32 radius = xMat3x3LookVec(&lookmat, &at);

        xVec3 angles;
        xMat3x3GetEuler(&lookmat, &angles);

        angles.x += yaw;

        xMat3x3 rotmat;
        xMat3x3Euler(&rotmat, angles.x, angles.y, angles.z);

        xVec3 offset, out_offset;

        offset.x = 0.0f;
        offset.y = 0.0f;
        offset.z = radius + radius_growth;

        xMat3x3RMulVec(&out_offset, &rotmat, &offset);

        p->m_pos.x = out_offset.x + cmd->pos.x;
        p->m_pos.z = out_offset.z + cmd->pos.z;

        p = p->m_next;
    }
}

void xParCmdTex_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdTexAnim_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdTexAnim* cmd = (xParCmdTexAnim*)c->tasset;
    xParCmdTex* tex = ps->m_cmdTex;

    if (!tex)
    {
        return;
    }

    if (cmd->throttle_time > 0.0f)
    {
        cmd->throttle_time_elapsed -= dt;

        if (cmd->throttle_time_elapsed > 0.0f)
        {
            return;
        }

        cmd->throttle_time_elapsed = cmd->throttle_time;
    }

    p = ps->m_root;

    if (cmd->anim_mode == 0)
    {
        while (p)
        {
            p->m_texIdx[0] = xrand_RandomChoice(tex->cols);
            p->m_texIdx[1] = xrand_RandomChoice(tex->rows);

            p = p->m_next;
        }
    }
    else if (cmd->anim_mode == 1)
    {
        if (tex->cols > 1)
        {
            while (p)
            {
                p->m_texIdx[0]++;

                if (p->m_texIdx[0] >= tex->cols)
                {
                    p->m_texIdx[0] = 0;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[1]++;

                        if (p->m_texIdx[1] >= tex->rows)
                        {
                            p->m_texIdx[1] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[1] == 0)
                        {
                            p->m_texIdx[1] = tex->rows - 1;
                        }
                        else
                        {
                            p->m_texIdx[1]--;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 3)
                    {
                        p->m_texIdx[1]++;

                        if (p->m_texIdx[1] >= tex->rows)
                        {
                            p->m_texIdx[1] = tex->rows - 1;
                            p->m_texIdx[0] = tex->cols - 1;
                        }
                    }
                }

                p = p->m_next;
            }
        }
    }
    else if (cmd->anim_mode == 2)
    {
        if (tex->cols > 1)
        {
            while (p)
            {
                if (p->m_texIdx[0] == 0)
                {
                    p->m_texIdx[0] = tex->cols - 1;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[1]++;

                        if (p->m_texIdx[1] >= tex->rows)
                        {
                            p->m_texIdx[1] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[1] == 0)
                        {
                            p->m_texIdx[1] = tex->rows - 1;
                        }
                        else
                        {
                            p->m_texIdx[1]--;
                        }
                    }
                }
                else
                {
                    p->m_texIdx[0]--;
                }

                p = p->m_next;
            }
        }
    }
    else if (cmd->anim_mode == 3)
    {
        if (tex->rows > 1)
        {
            while (p)
            {
                if (p->m_texIdx[1] == 0)
                {
                    p->m_texIdx[1] = tex->rows - 1;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[0]++;

                        if (p->m_texIdx[0] >= tex->cols)
                        {
                            p->m_texIdx[0] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[0] == 0)
                        {
                            p->m_texIdx[0] = tex->cols - 1;
                        }
                        else
                        {
                            p->m_texIdx[0]--;
                        }
                    }
                }
                else
                {
                    p->m_texIdx[1]--;
                }

                p = p->m_next;
            }
        }
    }
    else if (cmd->anim_mode == 4)
    {
        if (tex->rows > 1)
        {
            while (p)
            {
                p->m_texIdx[1]++;

                if (p->m_texIdx[1] >= tex->rows)
                {
                    p->m_texIdx[1] = 0;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[0]++;

                        if (p->m_texIdx[0] >= tex->cols)
                        {
                            p->m_texIdx[0] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[0] == 0)
                        {
                            p->m_texIdx[0] = tex->cols - 1;
                        }
                        else
                        {
                            p->m_texIdx[0]--;
                        }
                    }
                }

                p = p->m_next;
            }
        }
    }
}

void xParCmdCollideFall_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xParCmdCollideFall& cmd = *(xParCmdCollideFall*)c->tasset;
    xPar* p = ps->m_root;

    while (p)
    {
        F32& loc = p->m_pos.y;
        F32& vel = p->m_vel.y;
        F32 dloc = cmd.y - loc;

        if (dloc < 0.0f)
        {
            // above the collision plane: nothing to do
        }
        else
        {
            loc = dloc * cmd.bounce + cmd.y;

            if (vel < 0.0f)
            {
                vel = -vel * cmd.bounce;
            }
        }

        p = p->m_next;
    }
}

void xParCmdCollideFallSticky_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xParCmdCollideFallSticky& cmd = *(xParCmdCollideFallSticky*)c->tasset;
    F32 xzdamp = 1.0f - cmd.sticky;
    xPar* p = ps->m_root;

    while (p)
    {
        F32& loc = p->m_pos.y;
        F32& vel = p->m_vel.y;
        F32 dloc = cmd.y - loc;

        if (dloc < 0.0f)
        {
            // above the collision plane: nothing to do
        }
        else
        {
            loc = dloc * cmd.bounce + cmd.y;

            if (vel < 0.0f)
            {
                vel = -vel * cmd.bounce;
            }

            p->m_vel.x *= xzdamp;
            p->m_vel.z *= xzdamp;
        }

        p = p->m_next;
    }
}

void xParCmd_DampenSpeed_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdDampenData* cmd = (xParCmdDampenData*)c->tasset;

    if (cmd->enabled)
    {
        p = ps->m_root;

        F32 damp = dt * cmd->dampSpeed;

        while (p)
        {
            xVec3AddScaled(&p->m_vel, &p->m_vel, damp);

            p = p->m_next;
        }
    }
}

void xParCmd_SizeInOut_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdSizeInOutData* cmd = (xParCmdSizeInOutData*)c->tasset;
    S32 i;
    S32 seg;
    F32 slope_size[3];
    F32 frac;

    if (cmd->enabled)
    {
        p = ps->m_root;

        slope_size[0] = 3.0f * (cmd->custSize[1] - cmd->custSize[0]);
        slope_size[1] = 3.0f * (cmd->custSize[2] - cmd->custSize[1]);
        slope_size[2] = 3.0f * (cmd->custSize[3] - cmd->custSize[2]);

        while (p)
        {
            frac = 1.0f - p->m_lifetime / p->totalLifespan;
            frac = CLAMP(frac, 0.0f, 1.0f);

            if (frac < 0.33333334f)
            {
                seg = 0;
            }
            else if (frac < 0.6666667f)
            {
                seg = 1;
            }
            else
            {
                seg = 2;
            }

            for (i = seg; i > 0; i--)
            {
                frac -= 0.33333334f;
            }

            p->m_size = frac * slope_size[seg] + cmd->custSize[seg];

            p = p->m_next;
        }
    }
}

void xParCmd_AlphaInOut_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdAlphaInOutData* cmd = (xParCmdAlphaInOutData*)c->tasset;
    S32 i;
    S32 seg;
    F32 slope_alfa[3];
    F32 frac;
    F32 alfa;

    if (cmd->enabled)
    {
        p = ps->m_root;

        slope_alfa[0] = 3.0f * (cmd->custAlpha[1] - cmd->custAlpha[0]);
        slope_alfa[1] = 3.0f * (cmd->custAlpha[2] - cmd->custAlpha[1]);
        slope_alfa[2] = 3.0f * (cmd->custAlpha[3] - cmd->custAlpha[2]);

        while (p)
        {
            frac = 1.0f - p->m_lifetime / p->totalLifespan;
            frac = CLAMP(frac, 0.0f, 1.0f);

            if (frac < 0.33333334f)
            {
                seg = 0;
            }
            else if (frac < 0.6666667f)
            {
                seg = 1;
            }
            else
            {
                seg = 2;
            }

            for (i = seg; i > 0; i--)
            {
                frac -= 0.33333334f;
            }

            alfa = frac * slope_alfa[seg] + cmd->custAlpha[seg];
            p->m_cfl[3] = CLAMP(alfa, 0.0f, 255.0f);
            p->m_c[3] = (U8)p->m_cfl[3];

            p = p->m_next;
        }
    }
}

void xParCmd_Shaper_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdShaperData* cmd = (xParCmdShaperData*)c->tasset;
    F32 damp;
    F32 grav;
    S32 doalpha;
    S32 dosize;
    S32 i;
    S32 seg;
    F32 slope_alfa[3];
    F32 slope_size[3];
    F32 frac;
    F32 alfa;

    if (cmd->enabled)
    {
        damp = dt * cmd->dampSpeed;
        grav = dt * cmd->gravity;
        doalpha = 1;
        dosize = 1;

        if (cmd->custAlpha[0] < 0.0f)
        {
            doalpha = 0;
        }

        if (cmd->custSize[0] < 0.0f)
        {
            dosize = 0;
        }

        for (i = 0; i < 3; i++)
        {
            slope_size[i] = 3.0f * (cmd->custSize[i + 1] - cmd->custSize[i]);
            slope_alfa[i] = 3.0f * (cmd->custAlpha[i + 1] - cmd->custAlpha[i]);
        }

        p = ps->m_root;

        while (p)
        {
            xVec3AddScaled(&p->m_vel, &p->m_vel, damp);
            p->m_vel.y -= grav;

            if (p->totalLifespan < 1e-5f || (!dosize && !doalpha))
            {
                p = p->m_next;
                continue;
            }

            frac = 1.0f - p->m_lifetime / p->totalLifespan;
            frac = CLAMP(frac, 0.0f, 1.0f);

            if (frac < 0.33333334f)
            {
                seg = 0;
            }
            else if (frac < 0.6666667f)
            {
                seg = 1;
            }
            else
            {
                seg = 2;
            }

            for (i = seg; i > 0; i--)
            {
                frac -= 0.33333334f;
            }

            if (dosize)
            {
                p->m_size = frac * slope_size[seg] + cmd->custSize[seg];
            }

            if (doalpha)
            {
                alfa = frac * slope_alfa[seg] + cmd->custAlpha[seg];
                p->m_cfl[3] = CLAMP(alfa, 0.0f, 255.0f);
                p->m_c[3] = (U8)p->m_cfl[3];
            }

            p = p->m_next;
        }
    }
}
